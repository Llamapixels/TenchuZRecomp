#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rex::ui { class Window; }

namespace tenchuz::display_settings {

struct Monitor {
  std::string device;
  std::string label;
  int x, y, width, height;
};
std::vector<Monitor> monitors();
// Called on the UI thread at window creation. Uses the Windows device name,
// avoiding a dependency on SDL and Windows monitor enumeration orders matching.
void ApplyMonitor(rex::ui::Window* window);
void UpdateOutputSize(float width, float height);
float aspect();  // thread-safe; projection and shader reconstruction agree
void SetSceneCamera(bool gameplay_camera);
void SetMoviePlaying(bool playing);
void UpdatePresentation();  // UI thread: framing and GPU overlay layout
bool overlay_layout_supported();
float ui_scale();
bool safe_launch();

const std::vector<std::string>& profile_keys();
bool valid_profile_name(const std::string& name);
std::vector<std::string> profiles(const std::filesystem::path& exe_dir);
bool SaveProfile(const std::filesystem::path& exe_dir, const std::string& name);
bool LoadProfile(const std::filesystem::path& exe_dir, const std::string& name);

}  // namespace tenchuz::display_settings
