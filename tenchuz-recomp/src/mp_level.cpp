// tenchuz - play single-player levels in multiplayer (see mp_level.h)

#include "mp_level.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/system/xmemory.h>

namespace tenchuz::mp_level {
namespace {

constexpr uint32_t kScanLo = 0x40000000u;
constexpr uint32_t kScanHi = 0x50000000u;
constexpr uint32_t kBlockSize = 0x18;          // through the trailing 0
constexpr uint32_t kCamRikiOffset = 0xA9;       // "cam_riki" from the block start
constexpr char kCamRiki[] = "cam_riki";

std::atomic<rex::memory::Memory*> g_memory{nullptr};
std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_level{49};
std::atomic<bool> g_thread_started{false};

std::mutex g_status_mutex;
Status g_status;

uint32_t LoadBeU32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return std::byteswap(v);
}

void StoreBeU32(uint8_t* p, uint32_t v) {
  v = std::byteswap(v);
  std::memcpy(p, &v, sizeof(v));
}

// Memory::TranslateVirtual does no validation; only committed, readable pages
// may be touched.
bool Readable(rex::memory::Memory* memory, uint32_t address, uint32_t length) {
  rex::memory::BaseHeap* heap = memory->LookupHeap(address);
  if (!heap) {
    return false;
  }
  rex::memory::HeapAllocationInfo info{};
  if (!heap->QueryRegionInfo(address, &info)) {
    return false;
  }
  constexpr uint32_t kCommit =
      static_cast<uint32_t>(rex::memory::kMemoryAllocationCommit);
  if ((info.state & kCommit) == 0 ||
      address + length > info.base_address + info.region_size) {
    return false;
  }
  return heap->QueryRangeAccess(address, address + length - 1) !=
         rex::memory::PageAccess::kNoAccess;
}

bool LooksLikeBlock(const uint8_t* p) {
  const uint32_t level = LoadBeU32(p);
  return level >= 1 && level <= 63 && LoadBeU32(p + 0x04) <= 0xFF &&
         LoadBeU32(p + 0x08) <= 0xFF && LoadBeU32(p + 0x0C) <= 0xFF &&
         LoadBeU32(p + 0x10) == 0xFFFFFFFFu && LoadBeU32(p + 0x14) == 0;
}

bool BlockAt(rex::memory::Memory* memory, uint32_t a) {
  return (a & 0xF) == 4 && Readable(memory, a, kBlockSize) &&
         LooksLikeBlock(memory->TranslateVirtual<const uint8_t*>(a));
}

// Calls fn(lo, hi) for each committed, readable run of pages in the scan range.
template <typename Fn>
void VisitCommitted(rex::memory::Memory* memory, Fn&& fn) {
  constexpr uint32_t kCommit =
      static_cast<uint32_t>(rex::memory::kMemoryAllocationCommit);
  uint32_t addr = kScanLo;
  for (int guard = 0; addr < kScanHi && guard < 200000; ++guard) {
    const uint32_t next_page = (addr & ~0xFFFu) + 0x1000u;
    rex::memory::BaseHeap* heap = memory->LookupHeap(addr);
    rex::memory::HeapAllocationInfo info{};
    if (!heap || !heap->QueryRegionInfo(addr, &info) || info.region_size == 0) {
      addr = next_page;
      continue;
    }
    const uint32_t region_hi = std::min(info.base_address + info.region_size, kScanHi);
    if (region_hi <= addr) {
      addr = next_page;
      continue;
    }
    if ((info.state & kCommit) != 0 &&
        heap->QueryRangeAccess(addr, region_hi - 1) != rex::memory::PageAccess::kNoAccess) {
      fn(addr, region_hi);
    }
    addr = region_hi;
  }
}

// Finds the lobby block. Prefers blocks confirmed by the "cam_riki" text the
// cheat table measures from; falls back to the layout alone if there is
// exactly one match.
uint32_t Find(rex::memory::Memory* memory, int* candidates) {
  std::vector<uint32_t> confirmed;
  std::vector<uint32_t> layout_only;
  VisitCommitted(memory, [&](uint32_t lo, uint32_t hi) {
    const uint8_t* base = memory->TranslateVirtual<const uint8_t*>(lo);
    const size_t size = hi - lo;
    // "cam_riki" hits.
    const uint8_t* p = base;
    const uint8_t* end = base + size;
    while (p + sizeof(kCamRiki) - 1 <= end) {
      p = static_cast<const uint8_t*>(std::memchr(p, 'c', end - p));
      if (!p || p + sizeof(kCamRiki) - 1 > end) {
        break;
      }
      if (std::memcmp(p, kCamRiki, sizeof(kCamRiki) - 1) == 0) {
        const uint32_t a = lo + static_cast<uint32_t>(p - base) - kCamRikiOffset;
        if (a >= kScanLo && BlockAt(memory, a)) {
          confirmed.push_back(a);
        }
      }
      ++p;
    }
    // Layout-only matches, 4 bytes into each 16-byte line.
    for (uint32_t a = (lo & ~0xFu) + 4; a + kBlockSize <= hi; a += 16) {
      if (a >= lo && LooksLikeBlock(base + (a - lo))) {
        layout_only.push_back(a);
      }
    }
  });
  *candidates = static_cast<int>(confirmed.empty() ? layout_only.size() : confirmed.size());
  if (!confirmed.empty()) {
    return confirmed.front();
  }
  return layout_only.size() == 1 ? layout_only.front() : 0;
}

void Worker() {
  using namespace std::chrono_literals;
  uint32_t address = 0;
  auto next_search = std::chrono::steady_clock::now();
  for (;;) {
    std::this_thread::sleep_for(250ms);
    rex::memory::Memory* memory = g_memory.load();
    if (!memory || !g_enabled.load()) {
      continue;
    }
    if (address && !BlockAt(memory, address)) {
      REXLOG_INFO("mp_level: lobby block at {:08X} is gone", address);
      address = 0;
      std::lock_guard lock(g_status_mutex);
      g_status.address = 0;
    }
    if (!address) {
      if (std::chrono::steady_clock::now() < next_search) {
        continue;
      }
      {
        std::lock_guard lock(g_status_mutex);
        g_status.searching = true;
      }
      int candidates = 0;
      address = Find(memory, &candidates);
      next_search = std::chrono::steady_clock::now() + 2s;
      std::lock_guard lock(g_status_mutex);
      g_status.searching = false;
      g_status.candidates = candidates;
      g_status.address = address;
      if (address) {
        g_status.lobby_level =
            LoadBeU32(memory->TranslateVirtual<const uint8_t*>(address));
        REXLOG_INFO("mp_level: lobby block at {:08X}, level {} ({} candidates)",
                    address, g_status.lobby_level, candidates);
      }
      if (!address) {
        continue;
      }
    }
    uint8_t* p = memory->TranslateVirtual<uint8_t*>(address);
    const uint32_t want = g_level.load();
    const uint32_t have = LoadBeU32(p);
    if (have != want) {
      StoreBeU32(p, want);
      REXLOG_INFO("mp_level: level {} -> {}", have, want);
    }
    std::lock_guard lock(g_status_mutex);
    g_status.current_level = LoadBeU32(p);
  }
}

}  // namespace

void SetMemory(rex::memory::Memory* memory) { g_memory.store(memory); }

void SetEnabled(bool enabled) {
  g_enabled.store(enabled);
  if (enabled && !g_thread_started.exchange(true)) {
    std::thread(Worker).detach();
  }
}

bool enabled() { return g_enabled.load(); }

void SetLevel(uint32_t id) { g_level.store(id); }

uint32_t level() { return g_level.load(); }

Status GetStatus() {
  std::lock_guard lock(g_status_mutex);
  return g_status;
}

}  // namespace tenchuz::mp_level
