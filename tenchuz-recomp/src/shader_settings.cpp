// tenchuz - shader presets and their live settings. See shader_settings.h.

#include "shader_settings.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <utility>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "gameplay.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

REXCVAR_DEFINE_STRING(shader_preset, "Modern Look", "Tenchu Z/Shader",
                      "Shader preset folder (applies at startup; empty = "
                      "the game's original shaders)");
REXCVAR_DEFINE_BOOL(shader_effects, true, "Tenchu Z/Shader",
                    "Shader preset effects on (F6 > Shader)");

namespace tenchuz::shader_settings {
namespace {

namespace fs = std::filesystem;

// Slot layout: see shader_settings.h.
constexpr size_t kSlots = 64;
constexpr size_t kMaxSliders = 40;
constexpr size_t kSlotPlayerScreen = 40;  // x, y in 0..1 (top left = 0, 0)
constexpr size_t kSlotPlayerDepth = 42;
constexpr size_t kSlotPlayerValid = 43;
// Aim at the chest rather than the feet (world units above the position).
constexpr float kPlayerChestHeight = 90.0f;
constexpr size_t kSlotEye = 48;
constexpr size_t kSlotForward = 51;
constexpr size_t kSlotRight = 54;
constexpr size_t kSlotUp = 57;
constexpr size_t kSlotTime = 60;
constexpr size_t kSlotCameraValid = 61;
constexpr size_t kSlotState = 63;
// The game always draws a 16:9 frame (1280x720).
constexpr float kAspect = 16.0f / 9.0f;

std::vector<fs::path> roots_;
fs::path values_path_;
std::vector<std::string> preset_names_;
std::string loaded_;
std::string description_;
std::vector<Slider> sliders_;
std::vector<float> values_;

std::string Trim(std::string s) {
  const char* space = " \t\r\n";
  s.erase(0, s.find_first_not_of(space));
  s.erase(s.find_last_not_of(space) + 1);
  return s;
}

std::vector<std::string> SplitFields(const std::string& s) {
  std::vector<std::string> fields;
  size_t start = 0;
  while (true) {
    size_t bar = s.find('|', start);
    fields.push_back(Trim(s.substr(start, bar - start)));
    if (bar == std::string::npos) {
      return fields;
    }
    start = bar + 1;
  }
}

float ToFloat(const std::string& s, float fallback) {
  char* end = nullptr;
  float v = std::strtof(s.c_str(), &end);
  return end != s.c_str() ? v : fallback;
}

// shader_settings.ini: [Preset] sections of key = value lines.
using ValueFile = std::map<std::string, std::map<std::string, std::string>>;

ValueFile ReadValueFile() {
  ValueFile file;
  std::ifstream in(values_path_);
  std::string line, section;
  while (std::getline(in, line)) {
    line = Trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#') {
      continue;
    }
    if (line.front() == '[' && line.back() == ']') {
      section = Trim(line.substr(1, line.size() - 2));
      continue;
    }
    size_t eq = line.find('=');
    if (eq != std::string::npos) {
      file[section][Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
    }
  }
  return file;
}

void LoadPresetIni(const fs::path& path) {
  std::ifstream in(path);
  std::string line;
  int line_number = 0;
  while (std::getline(in, line)) {
    ++line_number;
    line = Trim(line);
    if (line.empty() || line[0] == ';' || line[0] == '#') {
      continue;
    }
    size_t eq = line.find('=');
    if (eq == std::string::npos) {
      continue;
    }
    const std::string kind = Trim(line.substr(0, eq));
    const std::string rest = Trim(line.substr(eq + 1));
    if (kind == "description") {
      description_ = rest;
      continue;
    }
    const std::vector<std::string> f = SplitFields(rest);
    Slider s;
    if (kind == "slider" && f.size() >= 5) {
      s.key = f[0];
      s.label = f[1];
      s.min = ToFloat(f[2], 0.0f);
      s.max = ToFloat(f[3], 1.0f);
      s.default_value = ToFloat(f[4], s.min);
      s.help = f.size() > 5 ? f[5] : "";
    } else if (kind == "checkbox" && f.size() >= 3) {
      s.key = f[0];
      s.label = f[1];
      s.default_value = ToFloat(f[2], 0.0f) != 0.0f ? 1.0f : 0.0f;
      s.help = f.size() > 3 ? f[3] : "";
      s.checkbox = true;
    } else {
      REXLOG_WARN("shader preset: {} line {} not understood", path.string(),
                  line_number);
      continue;
    }
    if (sliders_.size() == kMaxSliders) {
      REXLOG_WARN("shader preset: {} has more than {} settings; the rest are "
                  "ignored",
                  path.string(), kMaxSliders);
      return;
    }
    sliders_.push_back(std::move(s));
  }
}

using SetValuesFn = void (*)(const float*, uint32_t);

SetValuesFn FindSetValues() {
  HMODULE plugin = GetModuleHandleW(L"rexgpu-xenos.dll");
  auto fn = plugin ? reinterpret_cast<SetValuesFn>(
                         GetProcAddress(plugin, "rex_shader_override_set_values"))
                   : nullptr;
  if (!fn) {
    REXLOG_WARN("shader settings: GPU plugin has no rex_shader_override_set_values "
                "(SDK without the live-values patch); sliders have no effect");
  }
  return fn;
}

}  // namespace

void Init(const fs::path& exe_dir, const std::vector<fs::path>& roots) {
  roots_ = roots;
  values_path_ = exe_dir / "shader_settings.ini";
  std::error_code ec;
  std::set<std::string> names;
  for (const auto& root : roots_) {
    for (const auto& entry : fs::directory_iterator(root, ec)) {
      const std::string name = entry.path().filename().string();
      if (entry.is_directory() && !name.empty() && name[0] != '_' &&
          name[0] != '.') {
        names.insert(name);
      }
    }
  }
  preset_names_.assign(names.begin(), names.end());

  loaded_ = REXCVAR_GET(shader_preset);
  if (!loaded_.empty() && !names.count(loaded_)) {
    REXLOG_WARN("shader preset '{}' not found; using the original shaders",
                loaded_);
    loaded_.clear();
  }
  if (loaded_.empty()) {
    return;
  }
  for (const auto& root : roots_) {
    const fs::path ini = root / loaded_ / "preset.ini";
    if (fs::exists(ini, ec)) {
      LoadPresetIni(ini);
      break;
    }
  }
  const ValueFile file = ReadValueFile();
  const auto section = file.find(loaded_);
  for (const Slider& s : sliders_) {
    float v = s.default_value;
    if (section != file.end()) {
      const auto it = section->second.find(s.key);
      if (it != section->second.end()) {
        v = ToFloat(it->second, v);
      }
    }
    values_.push_back(v);
  }
  REXLOG_INFO("shader preset: {} ({} setting(s))", loaded_, sliders_.size());
}

std::vector<fs::path> OverrideDirs() {
  std::vector<fs::path> dirs;
  std::error_code ec;
  if (!loaded_.empty()) {
    for (const auto& root : roots_) {
      if (fs::is_directory(root / loaded_, ec)) {
        dirs.push_back(root / loaded_);
      }
    }
  }
  dirs.insert(dirs.end(), roots_.begin(), roots_.end());
  return dirs;
}

const std::vector<std::string>& preset_names() { return preset_names_; }
const std::string& loaded_preset() { return loaded_; }
std::string selected_preset() { return REXCVAR_GET(shader_preset); }
void select_preset(const std::string& name) { REXCVAR_SET(shader_preset, name); }

const std::string& description() { return description_; }
const std::vector<Slider>& sliders() { return sliders_; }
float value(size_t index) { return values_[index]; }
void set_value(size_t index, float v) { values_[index] = v; }
void reset_values() {
  for (size_t i = 0; i < sliders_.size(); ++i) {
    values_[i] = sliders_[i].default_value;
  }
}

bool SaveValues() {
  if (loaded_.empty()) {
    return true;
  }
  ValueFile file = ReadValueFile();
  auto& section = file[loaded_];
  for (size_t i = 0; i < sliders_.size(); ++i) {
    section[sliders_[i].key] = std::to_string(values_[i]);
  }
  std::ofstream out(values_path_, std::ios::trunc);
  out << "; Tenchu Z shader preset settings (F6 > Shader), one section per "
         "preset.\n";
  for (const auto& [name, keys] : file) {
    out << "\n[" << name << "]\n";
    for (const auto& [key, v] : keys) {
      out << key << " = " << v << "\n";
    }
  }
  if (!out) {
    REXLOG_ERROR("shader settings: could not write {}", values_path_.string());
    return false;
  }
  return true;
}

bool effects_enabled() { return REXCVAR_GET(shader_effects); }
void set_effects_enabled(bool enabled) { REXCVAR_SET(shader_effects, enabled); }

// Camera basis for rebuilding world positions in shaders. The game's
// projection is left-handed with +Y up and a vertical field of view.
static void FillCamera(rex::memory::Memory* memory, std::array<float, kSlots>& values) {
  float eye[3], target[3], fov;
  if (!gameplay::GetCameraView(memory, eye, target, &fov)) {
    return;
  }
  float f[3] = {target[0] - eye[0], target[1] - eye[1], target[2] - eye[2]};
  const float f_length = std::sqrt(f[0] * f[0] + f[1] * f[1] + f[2] * f[2]);
  if (f_length < 1e-6f) {
    return;
  }
  for (float& c : f) c /= f_length;
  // right = cross(world up, forward), up = cross(forward, right)
  float r[3] = {f[2], 0.0f, -f[0]};
  const float r_length = std::sqrt(r[0] * r[0] + r[2] * r[2]);
  if (r_length < 1e-6f) {
    return;  // looking straight up or down
  }
  for (float& c : r) c /= r_length;
  const float u[3] = {f[1] * r[2] - f[2] * r[1], f[2] * r[0] - f[0] * r[2],
                      f[0] * r[1] - f[1] * r[0]};
  const float tan_y = std::tan(fov * 0.5f);
  const float tan_x = tan_y * kAspect;
  for (int i = 0; i < 3; ++i) {
    values[kSlotEye + i] = eye[i];
    values[kSlotForward + i] = f[i];
    values[kSlotRight + i] = r[i] * tan_x;
    values[kSlotUp + i] = u[i] * tan_y;
  }
  values[kSlotCameraValid] = 1.0f;

  // Where the player is on screen and how far in front of the camera, so
  // shaders can calibrate the game's depth texture against a known distance.
  float player[3];
  if (!gameplay::GetPlayerPosition(memory, player)) {
    return;
  }
  const float p[3] = {player[0] - eye[0], player[1] + kPlayerChestHeight - eye[1],
                      player[2] - eye[2]};
  const float depth = p[0] * f[0] + p[1] * f[1] + p[2] * f[2];
  if (depth < 1.0f) {
    return;
  }
  const float ndc_x = (p[0] * r[0] + p[1] * r[1] + p[2] * r[2]) / (depth * tan_x);
  const float ndc_y = (p[0] * u[0] + p[1] * u[1] + p[2] * u[2]) / (depth * tan_y);
  if (std::fabs(ndc_x) > 1.0f || std::fabs(ndc_y) > 1.0f) {
    return;  // off screen
  }
  values[kSlotPlayerScreen] = ndc_x * 0.5f + 0.5f;
  values[kSlotPlayerScreen + 1] = 0.5f - ndc_y * 0.5f;
  values[kSlotPlayerDepth] = depth;
  values[kSlotPlayerValid] = 1.0f;
}

void Push(rex::memory::Memory* memory) {
  static const SetValuesFn set_values = FindSetValues();
  static const auto start = std::chrono::steady_clock::now();
  static std::array<float, kSlots> sent{};
  static bool sent_once = false;
  if (!set_values) {
    return;
  }
  std::array<float, kSlots> values{};
  for (size_t i = 0; i < values_.size(); ++i) {
    values[i] = values_[i];
  }
  FillCamera(memory, values);
  const double seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
  values[kSlotTime] = static_cast<float>(std::fmod(seconds, 3600.0));
  if (!effects_enabled()) {
    values[kSlotState] = 2.0f;
  } else if (!sliders_.empty()) {
    values[kSlotState] = 1.0f;
  }
  if (sent_once && values == sent) {
    return;
  }
  set_values(values.data(), static_cast<uint32_t>(values.size()));
  sent = values;
  sent_once = true;
}

}  // namespace tenchuz::shader_settings
