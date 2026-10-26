#include "display_settings.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <sstream>
#include <toml++/toml.hpp>
#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/window.h>
#include "settings_store.h"

REXCVAR_DEFINE_DOUBLE(menu_ui_scale, 1.0, "Tenchu Z/Display",
                      "F6 menu and overlay scale").range(0.75, 2.0);
REXCVAR_DEFINE_STRING(display_device, "", "Tenchu Z/Display",
                      "Windows monitor device name; empty follows system default (restart)");
REXCVAR_DEFINE_BOOL(widescreen, false, "Tenchu Z/Display",
                    "Experimental: expand the 3D view to the output aspect ratio");
REXCVAR_DEFINE_BOOL(widescreen_hud_correct, true, "Tenchu Z/Display",
                    "Keep the original HUD's proportions and center it in ultrawide");
REXCVAR_DEFINE_DOUBLE(game_hud_scale, 1.0, "Tenchu Z/Display",
                      "Original HUD size multiplier").range(0.5, 1.5);
REXCVAR_DEFINE_DOUBLE(game_hud_offset_x, 0.0, "Tenchu Z/Display",
                      "Original HUD horizontal offset as a fraction of screen width").range(-0.4, 0.4);
REXCVAR_DEFINE_DOUBLE(game_hud_offset_y, 0.0, "Tenchu Z/Display",
                      "Original HUD vertical offset as a fraction of screen height").range(-0.4, 0.4);
REXCVAR_DEFINE_BOOL(safe_launch, false, "Tenchu Z/Display",
                    "Temporary recovery run; do not save settings");

namespace tenchuz::display_settings {
namespace {
std::atomic<float> g_aspect{16.0f / 9.0f};
std::atomic<bool> g_gameplay_camera{false}, g_movie_playing{false};
using OverlayLayoutFn = void (*)(uint64_t, float, float, float, float, float, float);
OverlayLayoutFn OverlayLayout() {
  HMODULE plugin = GetModuleHandleW(L"rexgpu-xenos.dll");
  return plugin ? reinterpret_cast<OverlayLayoutFn>(GetProcAddress(plugin, "rex_set_overlay_layout")) : nullptr;
}
BOOL CALLBACK CollectMonitor(HMONITOR handle, HDC, LPRECT, LPARAM user) {
  MONITORINFOEXA info{};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoA(handle, &info)) {
    auto& list = *reinterpret_cast<std::vector<Monitor>*>(user);
    const int w = info.rcMonitor.right - info.rcMonitor.left;
    const int h = info.rcMonitor.bottom - info.rcMonitor.top;
    list.push_back({info.szDevice,
        std::string(info.szDevice) + "  " + std::to_string(w) + " x " +
        std::to_string(h) + ((info.dwFlags & MONITORINFOF_PRIMARY) ? " (primary)" : ""),
        info.rcMonitor.left, info.rcMonitor.top, w, h});
  }
  return TRUE;
}
}

std::vector<Monitor> monitors() {
  std::vector<Monitor> result;
  EnumDisplayMonitors(nullptr, nullptr, CollectMonitor, reinterpret_cast<LPARAM>(&result));
  return result;
}

void ApplyMonitor(rex::ui::Window* window) {
  if (!window) return;
  const std::string device = REXCVAR_GET(display_device);
  if (device.empty()) return;
  const auto list = monitors();
  const auto found = std::find_if(list.begin(), list.end(), [&](const Monitor& m) {
    return m.device == device;
  });
  if (found == list.end()) {
    REXLOG_WARN("Display {} is disconnected; using the default monitor", device);
    return;
  }
  const bool fullscreen = window->IsFullscreen();
  if (fullscreen) window->SetFullscreen(false);
  const auto hwnd = static_cast<HWND>(window->GetNativeWindowHandle());
  RECT rect{};
  if (hwnd && GetWindowRect(hwnd, &rect)) {
    SetWindowPos(hwnd, nullptr,
        found->x + std::max<LONG>(0, (found->width - (rect.right - rect.left)) / 2),
        found->y + std::max<LONG>(0, (found->height - (rect.bottom - rect.top)) / 2),
        0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  }
  if (fullscreen) window->SetFullscreen(true);
}

void UpdateOutputSize(float width, float height) {
  if (std::isfinite(width) && std::isfinite(height) && width > 0 && height > 0)
    g_aspect.store(std::clamp(width / height, 1.0f, 4.0f), std::memory_order_relaxed);
}
float aspect() {
  return std::atomic_ref<bool>(REXCVAR_GET(widescreen)).load(std::memory_order_relaxed) &&
      g_gameplay_camera.load(std::memory_order_relaxed) && !g_movie_playing.load(std::memory_order_relaxed)
      ? g_aspect.load(std::memory_order_relaxed) : 16.0f / 9.0f;
}
void SetSceneCamera(bool gameplay_camera) {
  g_gameplay_camera.store(gameplay_camera, std::memory_order_relaxed);
}
void SetMoviePlaying(bool playing) { g_movie_playing.store(playing, std::memory_order_relaxed); }
bool overlay_layout_supported() { return OverlayLayout() != nullptr; }
void UpdatePresentation() {
  const bool expanded = REXCVAR_GET(widescreen) &&
      g_gameplay_camera.load(std::memory_order_relaxed) && !g_movie_playing.load(std::memory_order_relaxed);
  // Titles, movies and scripted cameras retain the original composition.
  static int previous_letterbox = -1;
  const bool letterbox = !expanded;
  if (REXCVAR_GET(widescreen) && previous_letterbox != int(letterbox)) {
    rex::cvar::SetFlagByName("present_letterbox", letterbox ? "true" : "false");
    previous_letterbox = int(letterbox);
  } else if (!REXCVAR_GET(widescreen)) {
    previous_letterbox = -1;
  }
  if (const auto layout = OverlayLayout()) {
    const float size = float(REXCVAR_GET(game_hud_scale));
    const float correction = expanded && REXCVAR_GET(widescreen_hud_correct)
        ? (16.0f / 9.0f) / aspect() : 1.0f;
    constexpr uint64_t final_screen_pass = 0x822F99ECDB1B0ACDull;
    layout(final_screen_pass, 1280.0f, 720.0f, size * correction, size,
           float(REXCVAR_GET(game_hud_offset_x)), float(REXCVAR_GET(game_hud_offset_y)));
  }
}
float ui_scale() { return static_cast<float>(REXCVAR_GET(menu_ui_scale)); }
bool safe_launch() { return REXCVAR_GET(safe_launch); }

const std::vector<std::string>& profile_keys() {
  // Shared with the launcher. Deliberately excludes refresh, vsync, tearing,
  // frame_rate_60, cheats, mods, network settings, and user data paths.
  static const std::vector<std::string> keys = {
      "gpu_backend", "resolution_scale", "window_width", "window_height",
      "fullscreen", "display_device", "present_letterbox", "widescreen",
      "menu_ui_scale", "camera_fov", "camera_distance", "shader_preset",
      "shader_effects", "shader_overrides_enabled", "widescreen_hud_correct",
      "game_hud_scale", "game_hud_offset_x", "game_hud_offset_y"};
  return keys;
}
bool valid_profile_name(const std::string& name) {
  if (name.empty() || name.size() > 48) return false;
  std::string upper = name;
  for (auto& c : upper) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
  if (upper == "CON" || upper == "PRN" || upper == "AUX" || upper == "NUL" ||
      (upper.size() == 4 && (upper.starts_with("COM") || upper.starts_with("LPT")) &&
       upper[3] >= '1' && upper[3] <= '9')) return false;
  return std::all_of(name.begin(), name.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
  });
}
std::vector<std::string> profiles(const std::filesystem::path& exe_dir) {
  std::vector<std::string> names;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(exe_dir / "graphics_profiles", ec), end;
       !ec && it != end; it.increment(ec)) {
    const auto name = it->path().stem().string();
    if (it->is_regular_file(ec) && it->path().extension() == ".toml" && valid_profile_name(name))
      names.push_back(name);
  }
  std::sort(names.begin(), names.end());
  return names;
}
bool SaveProfile(const std::filesystem::path& exe_dir, const std::string& name) {
  if (safe_launch() || !valid_profile_name(name)) return false;
  std::error_code ec;
  std::filesystem::create_directories(exe_dir / "graphics_profiles", ec);
  return !ec && SaveSettings(exe_dir / "graphics_profiles" / (name + ".toml"), profile_keys());
}
bool LoadProfile(const std::filesystem::path& exe_dir, const std::string& name) {
  if (safe_launch() || !valid_profile_name(name)) return false;
  try {
    const auto table = toml::parse_file((exe_dir / "graphics_profiles" / (name + ".toml")).string());
    std::vector<std::pair<std::string, std::string>> values;
    for (const auto& key : profile_keys()) {
      const auto* node = table.get(key);
      if (!node) continue;
      const auto* flag = rex::cvar::GetFlagInfo(key);
      if (!flag) { REXLOG_ERROR("Profile setting is unavailable: {}", key); return false; }
      if (key == "gpu_backend" && node->value<std::string>() != "d3d12" &&
          node->value<std::string>() != "vulkan") return false;
      using Type = rex::cvar::FlagType;
      if ((flag->type == Type::String && !node->is_string()) ||
          (flag->type == Type::Boolean && !node->is_boolean()) ||
          (flag->type == Type::Int32 && !node->is_integer()) ||
          (flag->type == Type::Double && !node->is_integer() && !node->is_floating_point())) {
        REXLOG_ERROR("Profile setting has wrong type: {}", key);
        return false;
      }
      if (node->is_number()) {
        const auto number = node->value<double>();
        if (!number || !std::isfinite(*number) ||
            (flag->constraints.min && *number < *flag->constraints.min) ||
            (flag->constraints.max && *number > *flag->constraints.max)) {
          REXLOG_ERROR("Profile setting is out of range: {}", key);
          return false;
        }
      }
      // value<bool>() also converts integers to bool in toml++; dispatch by
      // the actual node type so a render scale of 3 never becomes "true".
      if (node->is_string()) values.emplace_back(key, *node->value<std::string>());
      else if (node->is_boolean()) values.emplace_back(key, *node->value<bool>() ? "true" : "false");
      else if (node->is_integer()) values.emplace_back(key, std::to_string(*node->value<int64_t>()));
      else if (node->is_floating_point()) {
        const auto v = *node->value<double>();
        if (!std::isfinite(v)) return false;
        values.emplace_back(key, std::to_string(v));
      } else return false;
    }
    if (values.empty()) return false;
    std::vector<std::pair<std::string, std::string>> previous;
    for (const auto& [key, value] : values)
      previous.emplace_back(key, rex::cvar::GetFlagByName(key));
    auto rollback = [&] {
      for (const auto& [key, value] : previous) rex::cvar::SetFlagByName(key, value);
    };
    for (const auto& [key, value] : values) {
      if (!rex::cvar::SetFlagByName(key, value)) {
        REXLOG_ERROR("Could not apply profile setting {} = {}", key, value);
        rollback(); return false;
      }
    }
    if (SaveSettings(exe_dir / "tenchuz.toml", profile_keys())) return true;
    rollback();
    return false;
  } catch (const std::exception& e) {
    REXLOG_ERROR("Graphics profile: {}", e.what());
    return false;
  }
}
}
