// tenchuz - speedrun timer and splits
//
// A mission is timed from the moment the local player's character appears
// after the game loads a mission (M-xx.set, see gameplay::mission_loads) to
// the moment it disappears (mission complete, failed or quit). Loading
// screens are therefore excluded. Characters that appear without a mission
// load (the hub) are not timed.
//
// A run is a sequence of missions started with Start run (or automatically
// at the first mission). Run time is the sum of mission times (in-game
// time); real time is wall-clock since the run started.
//
// Every finished mission is appended to speedrun_log.txt and personal bests
// per mission are kept in speedrun_bests.txt, both next to tenchuz.exe. An
// attempt that ended with the player dead is logged but never a best.

#pragma once

#include <chrono>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace rex::memory {
class Memory;
}

namespace tenchuz {

class Speedrun {
 public:
  struct Split {
    int mission = -1;
    double seconds = 0;
    bool died = false;
    bool personal_best = false;
  };

  void SetDirectory(const std::filesystem::path& dir);

  // Call once per frame.
  void Tick(rex::memory::Memory* memory);

  void StartRun();
  void ResetRun();

  bool in_mission() const { return in_mission_; }
  int mission() const { return mission_; }
  double mission_seconds() const;
  bool run_active() const { return run_active_; }
  double run_igt_seconds() const;   // sum of missions + current
  double run_real_seconds() const;  // wall clock
  const std::vector<Split>& splits() const { return splits_; }
  const std::map<int, double>& bests() const { return bests_; }

  static std::string Format(double seconds);

 private:
  using Clock = std::chrono::steady_clock;

  void FinishMission(bool died);
  void LoadBests();
  void SaveBests() const;

  std::filesystem::path dir_;
  bool loaded_ = false;
  bool in_mission_ = false;
  bool player_present_ = false;
  bool player_dead_ = false;
  uint32_t seen_loads_ = 0;
  int mission_ = -1;
  Clock::time_point mission_start_;
  bool run_active_ = false;
  Clock::time_point run_start_;
  double run_completed_ = 0;  // sum of finished missions in the run
  std::vector<Split> splits_;
  std::map<int, double> bests_;
};

}  // namespace tenchuz
