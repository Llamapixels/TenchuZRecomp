// tenchuz - camera probe (developer tool). See camera_probe.h.

#include "camera_probe.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <format>
#include <fstream>

#include <rex/logging.h>
#include <rex/system/xmemory.h>

#include "gameplay.h"

namespace tenchuz {
namespace {

// Guest ranges that can hold game objects: the main heap (thread stacks at
// 0x70000000 are excluded - they churn and never hold the camera) and the
// image's data sections.
constexpr struct {
  uint32_t lo, hi;
} kRanges[] = {{0x40000000u, 0x70000000u}, {0x82000000u, 0x83440000u}};

constexpr int kStickSteps = 4;        // per axis, see Start()
constexpr int kRequiredMoves = 3;     // tolerate one step against a limit

uint32_t LoadBe(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return std::byteswap(v);
}

bool Committed(rex::memory::Memory* memory, uint32_t addr, uint32_t* end) {
  rex::memory::BaseHeap* heap = memory->LookupHeap(addr);
  if (!heap) {
    *end = (addr & ~0xFFFu) + 0x1000u;
    return false;
  }
  rex::memory::HeapAllocationInfo info{};
  if (!heap->QueryRegionInfo(addr, &info) || info.region_size == 0) {
    *end = (addr & ~0xFFFu) + 0x1000u;
    return false;
  }
  *end = info.base_address + info.region_size;
  constexpr uint32_t kCommit =
      static_cast<uint32_t>(rex::memory::kMemoryAllocationCommit);
  return (info.state & kCommit) != 0 &&
         heap->QueryRangeAccess(addr, *end - 1) !=
             rex::memory::PageAccess::kNoAccess;
}

}  // namespace

bool CameraProbe::Start(const std::filesystem::path& report_path) {
  if (!memory_) {
    status_ = "guest memory not available";
    return false;
  }
  gameplay::PlayerHealth player;
  if (!gameplay::FindLocalPlayer(memory_, &player)) {
    status_ = "start a mission first";
    return false;
  }
  // One file per run, so runs in different areas can be compared.
  const auto now = std::chrono::floor<std::chrono::seconds>(
      std::chrono::system_clock::now());
  report_path_ = report_path;
  report_path_.replace_filename(std::format(
      "{}_{:%Y%m%d_%H%M%S}{}", report_path.stem().string(), now,
      report_path.extension().string()));
  regions_.clear();
  steps_.clear();
  using F = Filter;
  steps_.push_back({0, 0, 1.0, F::kNone});  // baseline
  for (int axis = 0; axis < 2; ++axis) {
    const Filter mark = axis == 0 ? F::kMarkX : F::kMarkY;
    const double hold = axis == 0 ? 0.5 : 0.35;
    for (int rep = 0; rep < kStickSteps / 2; ++rep) {
      for (float dir : {1.0f, -1.0f}) {
        steps_.push_back({0, 0, 0.6, F::kKeepUnchanged});
        steps_.push_back({axis == 0 ? dir : 0, axis == 1 ? dir : 0, hold, mark});
        steps_.push_back({0, 0, 0.8, F::kNone});  // let smoothing settle
      }
    }
  }
  steps_.push_back({0, 0, 0.6, F::kKeepUnchanged});
  step_ = 0;
  step_started_ = std::chrono::steady_clock::now();
  gameplay::SetRightStickOverride(true, 0, 0);
  status_ = "running - do not touch the controller";
  REXLOG_INFO("camera probe: started (player object {:08X})", player.object);
  return true;
}

void CameraProbe::Cancel() {
  gameplay::SetRightStickOverride(false);
  step_ = -1;
  regions_.clear();
  regions_.shrink_to_fit();
  status_ = "cancelled";
}

void CameraProbe::Tick() {
  if (step_ < 0) {
    return;
  }
  const Step& step = steps_[static_cast<size_t>(step_)];
  const double elapsed = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - step_started_)
                             .count();
  if (elapsed < step.seconds) {
    return;
  }

  if (step_ == 0) {
    CaptureBaseline();
  } else {
    ApplyFilter(step.filter);
  }

  ++step_;
  if (step_ >= static_cast<int>(steps_.size())) {
    gameplay::SetRightStickOverride(false);
    WriteReport();
    step_ = -1;
    regions_.clear();
    regions_.shrink_to_fit();
    return;
  }
  const Step& next = steps_[static_cast<size_t>(step_)];
  gameplay::SetRightStickOverride(true, next.stick_x, next.stick_y);
  step_started_ = std::chrono::steady_clock::now();
  status_ = std::format("running step {}/{} ({} candidate words) - do not "
                        "touch the controller",
                        step_, steps_.size() - 1, AliveCount());
}

void CameraProbe::CaptureBaseline() {
  const uint8_t* membase = memory_->virtual_membase();
  for (const auto& range : kRanges) {
    uint32_t addr = range.lo;
    while (addr < range.hi) {
      uint32_t end = 0;
      const bool ok = Committed(memory_, addr, &end);
      end = std::min(std::max(end, addr + 0x1000u), range.hi);
      if (ok) {
        Region region;
        region.lo = addr;
        region.hi = end;
        region.bytes.assign(membase + addr, membase + end);
        const size_t words = (end - addr) / 4;
        region.alive.assign((words + 63) / 64, ~0ull);
        region.moved_axes.assign(words, 0);
        regions_.push_back(std::move(region));
      }
      addr = end;
    }
  }
  size_t bytes = 0;
  for (const Region& r : regions_) {
    bytes += r.bytes.size();
  }
  REXLOG_INFO("camera probe: baseline {} region(s), {} MB", regions_.size(),
              bytes >> 20);
}

bool CameraProbe::RegionStillMapped(const Region& region) const {
  uint32_t end = 0;
  return Committed(memory_, region.lo, &end) && end >= region.hi;
}

void CameraProbe::ApplyFilter(Filter filter) {
  const uint8_t* membase = memory_->virtual_membase();
  for (Region& region : regions_) {
    if (region.bytes.empty()) {
      continue;
    }
    if (!RegionStillMapped(region)) {
      region.bytes.clear();  // released mid-probe; drop it
      std::fill(region.alive.begin(), region.alive.end(), 0);
      continue;
    }
    const uint8_t* now = membase + region.lo;
    const size_t words = region.bytes.size() / 4;
    for (size_t w = 0; w < words; ++w) {
      uint64_t& bits = region.alive[w / 64];
      const uint64_t bit = 1ull << (w % 64);
      if (!(bits & bit)) {
        continue;
      }
      const bool changed =
          std::memcmp(now + w * 4, region.bytes.data() + w * 4, 4) != 0;
      switch (filter) {
        case Filter::kKeepUnchanged:
          if (changed) bits &= ~bit;
          break;
        case Filter::kKeepChanged:
          if (!changed) bits &= ~bit;
          break;
        case Filter::kMarkX:
          if (changed && (region.moved_axes[w] & 0x0F) < 0x0F) {
            ++region.moved_axes[w];
          }
          break;
        case Filter::kMarkY:
          if (changed && (region.moved_axes[w] >> 4) < 0x0F) {
            region.moved_axes[w] += 0x10;
          }
          break;
        case Filter::kNone:
          break;
      }
    }
    std::memcpy(region.bytes.data(), now, region.bytes.size());
  }
}

size_t CameraProbe::AliveCount() const {
  size_t n = 0;
  for (const Region& r : regions_) {
    for (uint64_t bits : r.alive) {
      n += static_cast<size_t>(std::popcount(bits));
    }
  }
  return n;
}

void CameraProbe::WriteReport() {
  struct Hit {
    uint32_t addr;
    uint8_t x, y;
  };
  std::vector<Hit> hits;
  for (const Region& region : regions_) {
    const size_t words = region.bytes.size() / 4;
    for (size_t w = 0; w < words; ++w) {
      if (!(region.alive[w / 64] & (1ull << (w % 64)))) {
        continue;
      }
      const uint8_t x = region.moved_axes[w] & 0x0F;
      const uint8_t y = region.moved_axes[w] >> 4;
      if (x >= kRequiredMoves || y >= kRequiredMoves) {
        hits.push_back({region.lo + static_cast<uint32_t>(w * 4), x, y});
      }
    }
  }

  std::ofstream out(report_path_, std::ios::trunc);
  gameplay::PlayerHealth player;
  const bool have_player = gameplay::FindLocalPlayer(memory_, &player);
  out << "# Tenchu Z camera probe\n";
  out << "# player object: "
      << (have_player ? std::format("{:08X}", player.object) : "none") << "\n";
  out << "# " << hits.size() << " word(s) moved only with the camera "
      << "(x/y = stick steps out of " << kStickSteps << " that changed it)\n\n";

  const uint8_t* membase = memory_->virtual_membase();
  auto value_of = [&](uint32_t addr) {
    const uint32_t raw = LoadBe(membase + addr);
    return std::format("{:08X} {:>14.6g}", raw, std::bit_cast<float>(raw));
  };

  // Clusters: hits closer than 0x200 bytes belong to the same object.
  size_t cluster_count = 0;
  std::vector<uint32_t> cluster_starts;
  for (size_t i = 0; i < hits.size() && cluster_count < 40;) {
    size_t j = i + 1;
    while (j < hits.size() && hits[j].addr - hits[j - 1].addr < 0x200) {
      ++j;
    }
    ++cluster_count;
    const uint32_t lo = (hits[i].addr - 0x100) & ~0xFu;
    const uint32_t hi = (hits[j - 1].addr + 0x104 + 0xF) & ~0xFu;
    out << std::format("== cluster {} : {} hit(s), {:08X}..{:08X}\n",
                       cluster_count, j - i, hits[i].addr, hits[j - 1].addr);
    uint32_t end = 0;
    if (!Committed(memory_, lo, &end) || end < hi) {
      for (size_t k = i; k < j; ++k) {
        out << std::format("  {:08X}  {}  x{} y{}\n", hits[k].addr,
                           value_of(hits[k].addr), hits[k].x, hits[k].y);
      }
    } else {
      size_t k = i;
      for (uint32_t a = lo; a < hi; a += 4) {
        std::string mark;
        if (k < j && hits[k].addr == a) {
          mark = std::format("  <== x{} y{}", hits[k].x, hits[k].y);
          ++k;
        }
        out << std::format("  {:08X} +{:04X}  {}{}\n", a, a - lo,
                           value_of(a), mark);
      }
    }
    out << "\n";
    cluster_starts.push_back(hits[i].addr);
    i = j;
  }

  // Who points at the camera? A camera object's fields move, but its address
  // is held somewhere - ideally in a global (0x82xxxxxx), which gives a stable
  // way to reach it in every area. For each cluster, list every word in
  // memory holding an address up to 0x3000 bytes before the cluster's first
  // hit, i.e. a pointer to the start of the object that contains it.
  constexpr uint32_t kBackReach = 0x3000;
  for (size_t c = 0; c < cluster_starts.size(); ++c) {
    const uint32_t target = cluster_starts[c];
    struct Ref {
      uint32_t holder;
      uint32_t value;
    };
    std::vector<Ref> refs;
    for (const auto& range : kRanges) {
      uint32_t addr = range.lo;
      while (addr < range.hi && refs.size() < 4000) {
        uint32_t end = 0;
        const bool ok = Committed(memory_, addr, &end);
        end = std::min(std::max(end, addr + 0x1000u), range.hi);
        if (ok) {
          for (uint32_t a = addr; a + 4 <= end; a += 4) {
            const uint32_t v = LoadBe(membase + a);
            if (v <= target && target - v <= kBackReach) {
              refs.push_back({a, v});
            }
          }
        }
        addr = end;
      }
    }
    // Globals first, then the pointers closest to the cluster.
    std::sort(refs.begin(), refs.end(), [&](const Ref& a, const Ref& b) {
      const bool ga = a.holder >= 0x82000000u, gb = b.holder >= 0x82000000u;
      if (ga != gb) return ga;
      return target - a.value < target - b.value;
    });
    out << std::format("== pointers to cluster {} (hit {:08X}): {} found\n",
                       c + 1, target, refs.size());
    for (size_t k = 0; k < refs.size() && k < 60; ++k) {
      const Ref& r = refs[k];
      out << std::format("  {:08X} -> {:08X}  (cluster - 0x{:04X})  "
                         "first word there {:08X}{}\n",
                         r.holder, r.value, target - r.value,
                         LoadBe(membase + r.value),
                         r.holder >= 0x82000000u ? "  [global]" : "");
    }
    out << "\n";
  }

  status_ = std::format("done: {} camera word(s) in {} cluster(s) -> {}",
                        hits.size(), cluster_count, report_path_.string());
  REXLOG_INFO("camera probe: {}", status_);
}

}  // namespace tenchuz
