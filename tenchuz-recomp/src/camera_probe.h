// tenchuz - camera probe (developer tool)
//
// Locates the in-mission camera's data by experiment rather than by reading
// code. While the player stands still, the probe turns the camera with a
// synthetic right stick (gameplay::SetRightStickOverride) and diffs guest
// memory between steps:
//
//   idle            -> keep words that did not change   (removes timers,
//                                                         animation, enemies)
//   stick held      -> keep words that changed          (camera moved)
//   idle, settle    -> new baseline (camera smoothing may still be moving)
//
// repeated in both directions on X, then on Y. What survives moves when, and
// only when, the camera turns: yaw/pitch, the camera position, view
// matrices. The report lists them, flags which axis moved each, and dumps the
// memory around each cluster as floats, which is where fields like field of
// view and follow distance sit.
//
// Runs on the UI thread from the menu's per-frame draw; each step copies the
// committed guest heap once (a few hundred MB), which takes a few frames.

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rex::memory {
class Memory;
}

namespace tenchuz {

class CameraProbe {
 public:
  void SetMemory(rex::memory::Memory* memory) { memory_ = memory; }

  // Begins a probe; the report is written to `report_path` at the end.
  bool Start(const std::filesystem::path& report_path);
  void Cancel();

  // Advances the probe. Call once per frame.
  void Tick();

  bool running() const { return step_ >= 0; }
  const std::string& status() const { return status_; }

 private:
  struct Region {
    uint32_t lo = 0;
    uint32_t hi = 0;
    std::vector<uint8_t> bytes;       // copy at the last snapshot
    std::vector<uint64_t> alive;      // one bit per 4-byte word
    std::vector<uint8_t> moved_axes;  // per word: bit0 X moved, bit1 Y moved
  };
  enum class Filter { kNone, kKeepUnchanged, kKeepChanged, kMarkX, kMarkY };
  struct Step {
    float stick_x;
    float stick_y;
    double seconds;
    Filter filter;  // applied when the step ends, against the last snapshot
  };

  void CaptureBaseline();
  void ApplyFilter(Filter filter);
  void WriteReport();
  size_t AliveCount() const;
  bool RegionStillMapped(const Region& region) const;

  rex::memory::Memory* memory_ = nullptr;
  std::filesystem::path report_path_;
  std::vector<Region> regions_;
  std::vector<Step> steps_;
  int step_ = -1;
  std::chrono::steady_clock::time_point step_started_;
  std::string status_;
};

}  // namespace tenchuz
