// tenchuz - speedrun timer and splits. See speedrun.h.

#include "speedrun.h"

#include <cmath>
#include <ctime>
#include <format>
#include <fstream>
#include <sstream>

#include <rex/logging.h>

#include "gameplay.h"

namespace tenchuz {

void Speedrun::SetDirectory(const std::filesystem::path& dir) {
  dir_ = dir;
  LoadBests();
}

void Speedrun::Tick(rex::memory::Memory* memory) {
  gameplay::PlayerHealth player;
  const bool present = gameplay::FindLocalPlayer(memory, &player);
  if (present) {
    player_dead_ = player.hp < 1.0f;
  }

  if (present && !player_present_) {
    // The character appeared. It is a mission only if a mission load
    // happened since the last time a character was around.
    const uint32_t loads = gameplay::mission_loads();
    if (loads != seen_loads_) {
      seen_loads_ = loads;
      in_mission_ = true;
      mission_ = gameplay::current_mission();
      mission_start_ = Clock::now();
      player_dead_ = false;
      if (!run_active_) {
        StartRun();
      }
    }
  } else if (!present && player_present_ && in_mission_) {
    FinishMission(player_dead_);
  }
  player_present_ = present;
}

void Speedrun::StartRun() {
  run_active_ = true;
  run_start_ = Clock::now();
  run_completed_ = 0;
  splits_.clear();
}

void Speedrun::ResetRun() {
  run_active_ = false;
  run_completed_ = 0;
  splits_.clear();
  in_mission_ = false;
  // Treat whatever is loaded now as already seen, so the next mission load
  // starts the next run cleanly.
  seen_loads_ = gameplay::mission_loads();
}

double Speedrun::mission_seconds() const {
  if (!in_mission_) {
    return 0;
  }
  return std::chrono::duration<double>(Clock::now() - mission_start_).count();
}

double Speedrun::run_igt_seconds() const {
  return run_active_ ? run_completed_ + mission_seconds() : 0;
}

double Speedrun::run_real_seconds() const {
  if (!run_active_) {
    return 0;
  }
  return std::chrono::duration<double>(Clock::now() - run_start_).count();
}

std::string Speedrun::Format(double seconds) {
  const int total_ms = static_cast<int>(std::lround(seconds * 1000.0));
  const int h = total_ms / 3600000;
  const int m = total_ms / 60000 % 60;
  const int s = total_ms / 1000 % 60;
  const int ms = total_ms % 1000;
  return h ? std::format("{}:{:02}:{:02}.{:03}", h, m, s, ms)
           : std::format("{:02}:{:02}.{:03}", m, s, ms);
}

void Speedrun::FinishMission(bool died) {
  Split split;
  split.mission = mission_;
  split.seconds = mission_seconds();
  split.died = died;
  in_mission_ = false;
  run_completed_ += split.seconds;

  if (!died) {
    auto it = bests_.find(split.mission);
    if (it == bests_.end() || split.seconds < it->second) {
      bests_[split.mission] = split.seconds;
      split.personal_best = true;
      SaveBests();
    }
  }
  splits_.push_back(split);

  if (!dir_.empty()) {
    std::ofstream log(dir_ / "speedrun_log.txt", std::ios::app);
    const std::time_t now = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S",
                  std::localtime(&now));
    log << stamp << "  mission M-" << std::format("{:02}", split.mission)
        << "  " << Format(split.seconds)
        << (split.died ? "  (died)" : "")
        << (split.personal_best ? "  PB" : "") << "\n";
  }
  REXLOG_INFO("speedrun: mission M-{:02} {}{}", split.mission,
              Format(split.seconds), split.died ? " (died)" : "");
}

void Speedrun::LoadBests() {
  bests_.clear();
  std::ifstream in(dir_ / "speedrun_bests.txt");
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    std::istringstream fields(line);
    int mission;
    double seconds;
    if (fields >> mission >> seconds) {
      bests_[mission] = seconds;
    }
  }
}

void Speedrun::SaveBests() const {
  if (dir_.empty()) {
    return;
  }
  std::ofstream out(dir_ / "speedrun_bests.txt", std::ios::trunc);
  out << "# mission  best seconds  (Tenchu Z speedrun personal bests)\n";
  for (const auto& [mission, seconds] : bests_) {
    out << mission << " " << std::format("{:.3f}", seconds) << "\n";
  }
}

}  // namespace tenchuz
