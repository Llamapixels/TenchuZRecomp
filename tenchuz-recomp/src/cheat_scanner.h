// tenchuz - cheat / memory scanner (game-side, no SDK changes)
//
// Purpose: locate gameplay values by differential scanning, then optionally
// pin them. (Health does not need this: gameplay.cpp hooks the damage code.) Driven from an in-game ImGui overlay on F6.
//
// Why this lives in the game rather than an external tool: nothing outside the
// process can read the guest arena (it is a host-side mapping owned by the
// runtime), so the scan has to run in-process.
//
// ---------------------------------------------------------------------------
// BYTE ORDER - the single most important detail here
// ---------------------------------------------------------------------------
// Memory::TranslateVirtual() returns a pointer to the raw guest arena, and the
// contents are BIG-ENDIAN (see the comment on TranslateVirtual in
// include/rex/system/xmemory.h: "Note that the contents at the specified host
// address are big-endian"). Xbox 360 is a big-endian PowerPC.
//
// Therefore every read/write of a multi-byte value MUST go through the explicit
// byte-order helpers below. Using memcpy into a native uint32_t would silently
// produce byte-swapped garbage and every scan would find nothing.
//
// ---------------------------------------------------------------------------
// Address space
// ---------------------------------------------------------------------------
// From src/system/xmemory.cpp the guest heaps are:
//   0x00000000..0x40000000  v00000000  (guest virtual)
//   0x40000000..0x7F000000  v40000000  <- the game's main allocation arena
//   0x80000000..0x90000000  v80000000  (loaded XEX image, 64K pages)
//   0x90000000..0xA0000000  v90000000  (XEX, 4K pages)
//   0x70000000..0x72000000  guest thread stacks (observed in crash logs)
//
// Crash-log evidence that game objects live in v40000000:
//   r11=0x40a10078, r27=0x400f5c50
//
// We scan 0x40000000..0x50000000 by default: the live object arena, and small
// enough to snapshot quickly. The thread-stack range is deliberately excluded
// (stack values churn constantly and produce nothing but false positives).

#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>

namespace tenchuz {

// ---------------------------------------------------------------------------
// Big-endian accessors.
//
// The guest arena is big-endian; the host is little-endian. Every multi-byte
// access is composed byte by byte so the result does not depend on host order.
// ---------------------------------------------------------------------------

inline uint32_t BeLoad(const uint8_t* p, int width) {
  switch (width) {
    case 1:
      return p[0];
    case 2:
      return (static_cast<uint32_t>(p[0]) << 8) | static_cast<uint32_t>(p[1]);
    case 4:
      return (static_cast<uint32_t>(p[0]) << 24) |
             (static_cast<uint32_t>(p[1]) << 16) |
             (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
    default:
      return 0;
  }
}

inline void BeStore(uint8_t* p, int width, uint32_t v) {
  switch (width) {
    case 1:
      p[0] = static_cast<uint8_t>(v & 0xFF);
      break;
    case 2:
      p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
      p[1] = static_cast<uint8_t>(v & 0xFF);
      break;
    case 4:
      p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
      p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
      p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
      p[3] = static_cast<uint8_t>(v & 0xFF);
      break;
    default:
      break;
  }
}

// Interpret a big-endian word as an IEEE-754 float. Width is 4 only.
inline float BeLoadFloat(const uint8_t* p) {
  uint32_t bits = BeLoad(p, 4);
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

inline void BeStoreFloat(uint8_t* p, float f) {
  uint32_t bits;
  std::memcpy(&bits, &f, sizeof(bits));
  BeStore(p, 4, bits);
}

// A value must look like something the game would plausibly store, or we drown
// in noise. Rejects NaN/Inf and absurd magnitudes.
inline bool PlausibleFloat(float f) {
  if (!(f == f)) {
    return false;  // NaN
  }
  if (f > 1.0e12f || f < -1.0e12f) {
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Comparison filters for successive snapshots.
// ---------------------------------------------------------------------------
enum class Filter {
  kChanged,
  kUnchanged,
  kDecreased,
  kIncreased,
};

inline const char* FilterName(Filter f) {
  switch (f) {
    case Filter::kChanged:
      return "changed";
    case Filter::kUnchanged:
      return "unchanged";
    case Filter::kDecreased:
      return "decreased";
    case Filter::kIncreased:
      return "increased";
  }
  return "?";
}

enum class ValueType { kU8, kU16, kU32, kFloat };

inline const char* TypeName(ValueType t) {
  switch (t) {
    case ValueType::kU8:
      return "u8";
    case ValueType::kU16:
      return "u16";
    case ValueType::kU32:
      return "u32";
    case ValueType::kFloat:
      return "float";
  }
  return "?";
}

inline int TypeWidth(ValueType t) {
  switch (t) {
    case ValueType::kU8:
      return 1;
    case ValueType::kU16:
      return 2;
    case ValueType::kU32:
    case ValueType::kFloat:
      return 4;
  }
  return 4;
}

// ---------------------------------------------------------------------------
// A single candidate: guest address plus its value at the time it was recorded.
// ---------------------------------------------------------------------------
struct Candidate {
  uint32_t address;
  uint32_t value;     // raw bits (for float, the float's bit pattern)
  float as_float;     // valid when type == kFloat
};

// A watched address and its most recently observed value. Unlike a candidate,
// a watch is not filtered - it just displays whatever is there right now.
struct Watch {
  uint32_t address;
  uint32_t value;    // raw bits
  float as_float;    // valid when type == kFloat
  bool readable;     // false once the page is gone / unmapped
};

// ---------------------------------------------------------------------------
// The scanner.
//
// Runs on the UI thread, which blocks the guest while it works. A full scan of
// 256 MB at 4-byte stride with a first-pass plausibility filter takes well
// under a second in a release build, so blocking is acceptable and keeps the
// implementation simple (no tearing from the guest writing mid-scan).
// ---------------------------------------------------------------------------
class MemoryScanner {
 public:
  void SetMemory(rex::memory::Memory* mem) { memory_ = mem; }

  bool ready() const { return memory_ != nullptr; }
  ValueType type() const { return type_; }
  size_t candidate_count() const { return candidates_.size(); }
  int pass() const { return pass_; }
  uint32_t range_lo() const { return lo_; }
  uint32_t range_hi() const { return hi_; }

  // Bytes actually scanned / skipped in the last first pass, for the UI, so a
  // surprising result can be told apart from a range that was mostly holes.
  uint64_t scanned_bytes() const { return scanned_bytes_; }
  uint64_t skipped_bytes() const { return skipped_bytes_; }

  void SetType(ValueType t) {
    if (t == type_) {
      return;
    }
    type_ = t;
    Reset();
  }

  void SetRange(uint32_t lo, uint32_t hi) {
    lo_ = lo;
    hi_ = hi;
    Reset();
  }

  void Reset() {
    candidates_.clear();
    pass_ = 0;
    pinned_address_ = 0;
    pin_active_ = false;
    last_error_.clear();
  }

  const std::string& last_error() const { return last_error_; }

  // First pass.
  //
  // With `exact` disabled this collects every address whose initial value is
  // plausible. For integers "plausible" means non-zero, which is the standard
  // 1st-pass heuristic: a counter sitting at 0 is not interesting, and skipping
  // zeros removes the overwhelming majority of untouched memory.
  //
  // With `exact` enabled it keeps only addresses already holding that exact
  // value, which is dramatically more precise when the value is known (money,
  // an item count, a max health you can read off the UI).
  bool FirstPass(bool use_exact = false, uint32_t exact_bits = 0,
                 float exact_float = 0.0f) {
    candidates_.clear();
    pass_ = 0;
    last_error_.clear();
    scanned_bytes_ = 0;
    skipped_bytes_ = 0;
    if (!memory_) {
      last_error_ = "guest memory not available";
      return false;
    }

    const int w = TypeWidth(type_);
    const uint8_t* base = memory_->virtual_membase();
    VisitCommitted([&](uint32_t region_lo, uint32_t region_hi) {
      const uint32_t start = (region_lo + static_cast<uint32_t>(w) - 1) &
                             ~(static_cast<uint32_t>(w) - 1);
      const uint32_t limit =
          (region_hi > static_cast<uint32_t>(w)) ? region_hi - w : 0;
      for (uint32_t addr = start; addr <= limit; addr += static_cast<uint32_t>(w)) {
        const uint8_t* p = base + addr;
        if (type_ == ValueType::kFloat) {
          const float f = BeLoadFloat(p);
          if (PlausibleFloat(f) && (!use_exact || f == exact_float)) {
            candidates_.push_back({addr, BeLoad(p, 4), f});
          }
        } else {
          const uint32_t v = BeLoad(p, w);
          if (use_exact ? (v == exact_bits) : (v != 0)) {
            candidates_.push_back({addr, v, static_cast<float>(v)});
          }
        }
      }
    });

    pass_ = 1;
    return true;
  }

  // Subsequent pass: keep only candidates whose value now satisfies `filter`
  // relative to the value recorded when they were captured. Surviving
  // candidates have their recorded value refreshed so the next pass compares
  // against the newest state.
  //
  // The pre-refine candidate list is saved so UndoRefine() can restore it. This
  // exists because a mis-aimed filter (clicking "decreased" when nothing moved)
  // otherwise destroys a good candidate set irrecoverably - which happened
  // repeatedly during testing and is the single most frustrating failure mode
  // of the differential workflow.
  bool Refine(Filter filter, float float_epsilon = 0.0f) {
    if (!memory_) {
      last_error_ = "guest memory not available";
      return false;
    }
    if (candidates_.empty()) {
      last_error_ = "no candidates left";
      return false;
    }

    const int w = TypeWidth(type_);
    const uint8_t* base = memory_->virtual_membase();
    std::vector<Candidate> keep;
    keep.reserve(candidates_.size());

    // Snapshot for UndoRefine(). Taken before any mutation, and includes the
    // recorded values from *before* this pass, so restoring it also restores the
    // baselines the next comparison needs.
    undo_candidates_ = candidates_;
    undo_pass_ = pass_;
    undo_filter_ = filter;
    has_undo_ = true;

    for (const auto& c : candidates_) {
      if (!AddressInMappedRange(c.address)) {
        continue;
      }
      const uint8_t* p = base + c.address;

      bool match = false;
      uint32_t now_bits = 0;
      float now_f = 0.0f;

      if (type_ == ValueType::kFloat) {
        now_f = BeLoadFloat(p);
        now_bits = BeLoad(p, 4);
        if (!PlausibleFloat(now_f)) {
          continue;
        }
        const float was = c.as_float;
        switch (filter) {
          case Filter::kChanged:
            match = (now_f > was + float_epsilon) || (now_f < was - float_epsilon);
            break;
          case Filter::kUnchanged:
            match = (now_f <= was + float_epsilon) && (now_f >= was - float_epsilon);
            break;
          case Filter::kDecreased:
            match = now_f < was - float_epsilon;
            break;
          case Filter::kIncreased:
            match = now_f > was + float_epsilon;
            break;
        }
      } else {
        const uint32_t now = BeLoad(p, w);
        now_bits = now;
        now_f = static_cast<float>(now);
        const uint32_t was = c.value;
        switch (filter) {
          case Filter::kChanged:
            match = now != was;
            break;
          case Filter::kUnchanged:
            match = now == was;
            break;
          case Filter::kDecreased:
            match = now < was;
            break;
          case Filter::kIncreased:
            match = now > was;
            break;
        }
      }

      if (match) {
        keep.push_back({c.address, now_bits, now_f});
      }
    }

    candidates_.swap(keep);
    last_before_ = undo_candidates_.size();
    last_removed_ = last_before_ - candidates_.size();
    ++pass_;
    return true;
  }

  // Restore the candidate list to its state before the last Refine().
  // Returns false when there is nothing to undo.
  bool UndoRefine() {
    if (!has_undo_) {
      last_error_ = "nothing to undo";
      return false;
    }
    candidates_ = undo_candidates_;
    pass_ = undo_pass_;
    has_undo_ = false;
    last_error_.clear();
    return true;
  }

  bool can_undo() const { return has_undo_; }

  // How many candidates the last Refine() removed. Shown so a filter that
  // matches nothing ("0 removed, list unchanged") is distinguishable from one
  // that wiped everything ("all removed") - the failure the UI previously hid.
  size_t last_removed() const { return last_removed_; }
  size_t last_before() const { return last_before_; }

  // -------------------------------------------------------------------------
  // Watch list.
  //
  // The differential scan above is inherently timing-sensitive: the user has to
  // notice a change and click a filter before the value moves again, and a
  // mis-aimed click destroys the list. Watching a fixed address removes all of
  // that - the value is simply displayed every frame and can be pinned, with no
  // snapshot cadence to get wrong.
  //
  // This is the intended way to confirm a candidate: add it to the watch list,
  // observe whether it tracks the value the game shows on screen, then pin it.
  // -------------------------------------------------------------------------
  void AddWatch(uint32_t address) {
    for (const auto& w : watches_) {
      if (w.address == address) {
        return;  // already watched
      }
    }
    watches_.push_back({address, 0, 0.0f, true});
  }

  void RemoveWatch(size_t index) {
    if (index < watches_.size()) {
      watches_.erase(watches_.begin() + static_cast<ptrdiff_t>(index));
    }
  }

  void ClearWatches() { watches_.clear(); }

  // Move every current candidate into the watch list. After a scan has been
  // narrowed to a handful of hits, watching them all is the fastest way to see
  // which one tracks the value shown on screen - and unlike a scan, a watch has
  // no timing requirement: the value is simply displayed live every frame.
  size_t WatchAllCandidates() {
    size_t added = 0;
    for (const auto& c : candidates_) {
      const size_t before = watches_.size();
      AddWatch(c.address);
      if (watches_.size() != before) {
        ++added;
      }
    }
    return added;
  }

  const std::vector<Watch>& watches() const { return watches_; }

  // Refresh every watched value. Called once per frame from the UI.
  void RefreshWatches() {
    if (!memory_) {
      return;
    }
    const int width = TypeWidth(type_);
    const uint8_t* base = memory_->virtual_membase();
    for (auto& watch : watches_) {
      if (!AddressInMappedRange(watch.address)) {
        watch.readable = false;
        continue;
      }
      watch.readable = true;
      const uint8_t* p = base + watch.address;
      if (type_ == ValueType::kFloat) {
        watch.as_float = BeLoadFloat(p);
        watch.value = BeLoad(p, 4);
      } else {
        watch.value = BeLoad(p, width);
        watch.as_float = static_cast<float>(watch.value);
      }
    }
  }

  // Re-read every candidate's current value without filtering, so the list
  // shown in the UI is live rather than frozen at capture time.
  void RefreshValues() {
    if (!memory_) {
      return;
    }
    const int w = TypeWidth(type_);
    const uint8_t* base = memory_->virtual_membase();
    for (auto& c : candidates_) {
      if (!AddressInMappedRange(c.address)) {
        continue;
      }
      const uint8_t* p = base + c.address;
      if (type_ == ValueType::kFloat) {
        c.as_float = BeLoadFloat(p);
        c.value = BeLoad(p, 4);
      } else {
        c.value = BeLoad(p, w);
        c.as_float = static_cast<float>(c.value);
      }
    }
  }

  const std::vector<Candidate>& candidates() const { return candidates_; }

  // -------------------------------------------------------------------------
  // Pinning: continuously rewrite a chosen address so the value cannot drop.
  // Called once per frame from the UI draw. Called on the UI thread, which is
  // the same thread the guest is blocked on during draw, so this cannot tear
  // against guest writes in a way that matters for a cheat.
  // -------------------------------------------------------------------------
  void Pin(uint32_t address, uint32_t value_bits, float value_float) {
    pinned_address_ = address;
    pinned_bits_ = value_bits;
    pinned_float_ = value_float;
    pin_active_ = true;
  }

  void Unpin() { pin_active_ = false; }

  bool pin_active() const { return pin_active_; }
  uint32_t pinned_address() const { return pinned_address_; }

  // The value the pin is currently forcing, for display in the UI. Floats are
  // shown through this as their bit pattern, which is what the store writes.
  uint32_t pinned_value() const {
    if (type_ == ValueType::kFloat) {
      uint32_t bits = 0;
      std::memcpy(&bits, &pinned_float_, sizeof(bits));
      return bits;
    }
    return pinned_bits_;
  }

  void ApplyPin() {
    if (!pin_active_ || !memory_ || pinned_address_ == 0) {
      return;
    }
    if (!AddressInMappedRange(pinned_address_)) {
      return;
    }
    uint8_t* p = memory_->virtual_membase() + pinned_address_;
    if (type_ == ValueType::kFloat) {
      BeStoreFloat(p, pinned_float_);
    } else {
      BeStore(p, TypeWidth(type_), pinned_bits_);
    }
  }

  // Read a candidate's current value for display.
  bool ReadValue(uint32_t address, uint32_t* out_bits, float* out_float) const {
    if (!memory_ || !AddressInMappedRange(address)) {
      return false;
    }
    const uint8_t* p = memory_->virtual_membase() + address;
    if (type_ == ValueType::kFloat) {
      *out_float = BeLoadFloat(p);
      *out_bits = BeLoad(p, 4);
    } else {
      *out_bits = BeLoad(p, TypeWidth(type_));
      *out_float = static_cast<float>(*out_bits);
    }
    return true;
  }

 private:
  // Walk the scan range as a sequence of COMMITTED regions, invoking `fn(lo,hi)`
  // for each.
  //
  // This is the fix for the access violation that a naive linear scan caused.
  // The guest arena is *reserved* as a whole but committed lazily by the guest
  // allocator, so a large fraction of any heap range has no page behind it.
  // Memory::TranslateVirtual() performs no validation, so touching an
  // uncommitted page faults and takes the process down:
  //
  //   read of guest 0x451C0000 (host 0x00000001451C0000)
  //
  // Region/access queries live on BaseHeap, not on Memory, so each address is
  // first resolved to its heap via LookupHeap(); an address with no heap is not
  // mappable at all and is skipped.
  //
  // For a *free* region QueryRegionInfo reports state == 0 and leaves
  // protect == 0, and QueryRangeAccess then returns kNoAccess because the read
  // bit is clear - so the access check alone is sufficient. The explicit state
  // test is kept as a second, independent guard.
  template <typename Fn>
  void VisitCommitted(Fn&& fn) {
    constexpr uint32_t kCommitBit =
        static_cast<uint32_t>(rex::memory::kMemoryAllocationCommit);

    uint32_t addr = lo_;
    int guard = 0;
    while (addr < hi_ && guard++ < 200000) {
      rex::memory::BaseHeap* heap = memory_->LookupHeap(addr);
      if (!heap) {
        // Hole between heaps (or an address the platform does not map).
        const uint32_t next = (addr & ~0xFFFu) + 0x1000u;
        skipped_bytes_ += (next > addr ? next - addr : 0);
        addr = next;
        continue;
      }

      rex::memory::HeapAllocationInfo info{};
      if (!heap->QueryRegionInfo(addr, &info)) {
        const uint32_t next = (addr & ~0xFFFu) + 0x1000u;
        skipped_bytes_ += (next > addr ? next - addr : 0);
        addr = next;
        continue;
      }

      // info.base_address is the address we asked about; region_size is the run
      // of pages sharing its attributes, so their sum is the region end.
      const uint32_t region_hi = info.base_address + info.region_size;
      if (info.region_size == 0 || region_hi <= addr) {
        const uint32_t next = (addr & ~0xFFFu) + 0x1000u;
        skipped_bytes_ += (next > addr ? next - addr : 0);
        addr = next;
        continue;
      }
      const uint32_t visit_hi = region_hi < hi_ ? region_hi : hi_;

      // Only committed, readable pages may be dereferenced.
      bool ok = (info.state & kCommitBit) != 0;
      if (ok) {
        // QueryRangeAccess() rejects a high_address at/after the heap end, so
        // pass the last byte of the region rather than the exclusive end.
        const auto access = heap->QueryRangeAccess(addr, visit_hi - 1);
        ok = access != rex::memory::PageAccess::kNoAccess;
      }

      if (ok) {
        scanned_bytes_ += visit_hi - addr;
        fn(addr, visit_hi);
      } else {
        skipped_bytes_ += visit_hi - addr;
      }

      addr = visit_hi;
    }
  }

  // Whether an address may be dereferenced right now. Used for the pin and for
  // per-candidate refreshes, which happen outside the region walk.
  bool AddressInMappedRange(uint32_t addr) const {
    if (!memory_ || addr < lo_ || addr >= hi_) {
      return false;
    }
    constexpr uint32_t kCommitBit =
        static_cast<uint32_t>(rex::memory::kMemoryAllocationCommit);
    rex::memory::BaseHeap* heap = memory_->LookupHeap(addr);
    if (!heap) {
      return false;
    }
    rex::memory::HeapAllocationInfo info{};
    if (!heap->QueryRegionInfo(addr, &info)) {
      return false;
    }
    if ((info.state & kCommitBit) == 0 || info.region_size == 0) {
      return false;
    }
    // The whole value must sit inside the region, not straddle its end.
    const uint32_t region_hi = info.base_address + info.region_size;
    if (addr + 8 > region_hi) {
      return false;
    }
    return heap->QueryRangeAccess(addr, addr + 7) !=
           rex::memory::PageAccess::kNoAccess;
  }

  rex::memory::Memory* memory_ = nullptr;
  ValueType type_ = ValueType::kU32;
  uint32_t lo_ = 0x40000000u;
  uint32_t hi_ = 0x50000000u;
  int pass_ = 0;
  std::string last_error_;
  std::vector<Candidate> candidates_;
  uint64_t scanned_bytes_ = 0;
  uint64_t skipped_bytes_ = 0;

  // Undo snapshot for the last Refine().
  std::vector<Candidate> undo_candidates_;
  int undo_pass_ = 0;
  Filter undo_filter_ = Filter::kChanged;
  bool has_undo_ = false;
  size_t last_removed_ = 0;
  size_t last_before_ = 0;

  std::vector<Watch> watches_;

  bool pin_active_ = false;
  uint32_t pinned_address_ = 0;
  uint32_t pinned_bits_ = 0;
  float pinned_float_ = 0.0f;

};

}  // namespace tenchuz
