// Standalone persistence checks. No game assets or generated code are used.
#include "display_settings.h"
#include "settings_store.h"
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

// Game/plugin cvars, normally registered by the game and GPU DLL.
REXCVAR_DEFINE_STRING(gpu_backend, "d3d12", "Test", "");
REXCVAR_DEFINE_INT32(resolution_scale, 1, "Test", "").range(1, 7);
REXCVAR_DEFINE_DOUBLE(camera_fov, 48.0, "Test", "").range(30, 100);
REXCVAR_DEFINE_DOUBLE(camera_distance, 1.0, "Test", "").range(0.5, 2.5);
REXCVAR_DEFINE_STRING(shader_preset, "Modern Look", "Test", "");
REXCVAR_DEFINE_BOOL(shader_effects, true, "Test", "");
REXCVAR_DEFINE_BOOL(shader_overrides_enabled, true, "Test", "");
REXCVAR_DEFINE_BOOL(frame_rate_60, true, "Test", "");

void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
std::string Read(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), {}};
}
int main(int argc, char** argv) {
  using namespace tenchuz;
  namespace ds = display_settings;
  namespace fs = std::filesystem;
  try {
    Check(argc == 2, "pass a disposable test directory");
    const fs::path root = fs::absolute(argv[1]) /
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    fs::create_directories(root);
    const auto log_path = (root / "test.log").string();
    rex::InitLogging(log_path.c_str());
    Check(rex::cvar::SetFlagByName("mnk_sensitivity", "100.0"), "extended mouse sensitivity rejected (SDK patch 0012 required)");
    Check(rex::cvar::Query<double>("mnk_sensitivity") == 100.0, "mouse sensitivity was clamped");
    Check(!rex::cvar::SetFlagByName("mnk_sensitivity", "101.0"), "out-of-range mouse sensitivity accepted");
    // Explicit default-valued guest dimensions must not follow an ultrawide
    // host window. Otherwise even present_letterbox=true stretches title art.
    rex::cvar::SetFlagByName("window_width", "1680");
    rex::cvar::SetFlagByName("window_height", "720");
    rex::cvar::SetFlagByName("video_mode_width", "1280");
    rex::cvar::SetFlagByName("video_mode_height", "720");
    rex::system::X_VIDEO_MODE video_mode{};
    rex::kernel::xboxkrnl::VdQueryVideoMode(&video_mode);
    Check(video_mode.display_width == 1280 && video_mode.display_height == 720,
          "explicit native video mode incorrectly follows host window (SDK patch 0011 required)");
    for (const auto& key : ds::profile_keys())
      Check(rex::cvar::GetFlagInfo(key) != nullptr, ("missing cvar " + key).c_str());
    for (const auto* bad : {"../escape", "", "CON", "lpt1", "has space"})
      Check(!ds::valid_profile_name(bad), "unsafe profile name accepted");
    Check(ds::valid_profile_name("Ultrawide_21-9"), "valid profile rejected");
    Check(rex::cvar::SetFlagByName("display_device", R"(\\.\DISPLAY2)"), "set device");
    Check(rex::cvar::SetFlagByName("camera_fov", "67.5"), "set camera");
    Check(rex::cvar::SetFlagByName("resolution_scale", "3"), "set scale");
    {
      std::ofstream config(root / "tenchuz.toml");
      config << "# keep me\nframe_rate_60 = true\nvideo_mode_refresh_rate = 144.0\n"
                "vsync = false\n[log.levels]\ngpu = \"warning\"\n";
    }
    Check(ds::SaveProfile(root, "Test"), "save profile");
    const auto profile_path = root / "graphics_profiles" / "Test.toml";
    const auto saved = Read(profile_path);
    Check(saved.find("frame_rate_60") == std::string::npos, "profile captured framerate");
    Check(saved.find("video_mode_refresh_rate") == std::string::npos, "profile captured refresh");
    rex::cvar::SetFlagByName("camera_fov", "48.0");
    rex::cvar::SetFlagByName("display_device", "");
    {
      std::ofstream edit(profile_path, std::ios::app);
      edit << "frame_rate_60 = false\nvideo_mode_refresh_rate = 30.0\n";
    }
    const bool loaded = ds::LoadProfile(root, "Test");
    if (!loaded) { rex::FlushLogging(); std::cerr << Read(root / "test.log"); }
    Check(loaded, "load profile");
    Check(rex::cvar::Query<double>("camera_fov") == 67.5, "camera not restored");
    Check(rex::cvar::GetFlagByName("display_device") == R"(\\.\DISPLAY2)", "device escaping changed");
    Check(rex::cvar::Query<bool>("frame_rate_60"), "profile changed fps patch");
    const auto config = Read(root / "tenchuz.toml");
    Check(config.find("video_mode_refresh_rate = 144.0") != std::string::npos, "profile changed refresh");
    Check(config.find("# keep me") != std::string::npos, "config comment lost");
    Check(config.find("camera_fov") < config.find("[log.levels]"), "settings put in wrong TOML table");
    {
      std::ofstream invalid(root / "graphics_profiles" / "Invalid.toml");
      invalid << "resolution_scale = 2\ncamera_fov = \"wrong type\"\n";
    }
    Check(!ds::LoadProfile(root, "Invalid"), "invalid profile accepted");
    Check(rex::cvar::Query<int32_t>("resolution_scale") == 3, "invalid profile partially applied");
    Check(Read(root / "tenchuz.toml") == config, "invalid profile changed config");
    ds::UpdateOutputSize(3440, 1440);
    ds::SetSceneCamera(true);
    rex::cvar::SetFlagByName("widescreen", "true");
    Check(ds::aspect() > 2.38f && ds::aspect() < 2.40f, "ultrawide aspect incorrect");
    ds::UpdateOutputSize(0, 0);
    Check(ds::aspect() > 2.38f, "minimize invalidated aspect");
    ds::SetMoviePlaying(true);
    Check(ds::aspect() < 1.78f, "movie framing is not preserved");
    ds::SetMoviePlaying(false);
    ds::SetSceneCamera(false);
    Check(ds::aspect() < 1.78f, "scripted camera framing is not preserved");
    ds::SetSceneCamera(true);
    rex::cvar::SetFlagByName("widescreen", "false");
    Check(ds::aspect() > 1.77f && ds::aspect() < 1.78f, "original framing not restored");
    rex::cvar::SetFlagByName("safe_launch", "true");
    Check(!ds::SaveProfile(root, "Recovery"), "recovery saved profile");
    Check(!ds::LoadProfile(root, "Test"), "recovery loaded profile");
    Check(!SaveSettings(root / "tenchuz.toml", {"camera_fov"}), "recovery saved settings");
    Check(Read(root / "tenchuz.toml") == config, "recovery changed config");
    std::cout << "PASS: profile round-trip, escaping, validation, timing isolation, aspect updates and safe-launch persistence\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
