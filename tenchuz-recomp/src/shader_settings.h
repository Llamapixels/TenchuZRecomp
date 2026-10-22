// tenchuz - shader presets and their live settings (F6 > Shader)
//
// A shader preset is a subfolder of a shader folder (shader_overrides\ or an
// enabled mod's shaders\), e.g. shader_overrides\Modern Look\. Exactly one
// preset is active per session, picked in F6 > Shader and stored as
// shader_preset in tenchuz.toml; switching needs a restart, because the
// runtime replaces shaders when it first translates them. "Original" (an
// empty shader_preset) uses no preset. .hlsl files directly in a shader
// folder are not part of any preset and always apply.
//
// A preset's preset.ini describes its sliders:
//
//   description = One line shown under the selector.
//   slider   = key | Label | min | max | default | Tooltip
//   checkbox = key | Label | default | Tooltip
//
// The n-th slider or checkbox is XeUser(n) in the preset's HLSL (up to 40).
// The game fills the rest every frame:
//   XeUser(40..41)  player's chest on screen (0..1, top left = 0, 0)
//   XeUser(42)      player's view depth (world units in front of the camera)
//   XeUser(43)      1 when the player values are valid (player on screen)
//   XeUser(48..50)  camera eye (world space)
//   XeUser(51..53)  camera forward (unit vector)
//   XeUser(54..56)  camera right * tan(horizontal half field of view)
//   XeUser(57..59)  camera up * tan(vertical half field of view)
//                   so a pixel at normalized device coordinates (x, y) and
//                   view depth d is at eye + d * (forward + x*right + y*up)
//   XeUser(60)      time in seconds (wraps every hour)
//   XeUser(61)      1 when the camera values are valid
//   XeUser(63)      2 when Effects on is unticked, otherwise 1 when the
//                   sliders are live and 0 without a preset.ini (the shader
//                   then uses its own defaults)
// The player's values are saved per preset in shader_settings.ini.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace rex::memory {
class Memory;
}

namespace tenchuz::shader_settings {

struct Slider {
  std::string key;
  std::string label;
  std::string help;
  float min = 0.0f;
  float max = 1.0f;
  float default_value = 0.0f;
  bool checkbox = false;
};

// Finds the presets in `roots` (highest priority first) and loads the
// selected one's sliders and saved values. Call once at startup.
void Init(const std::filesystem::path& exe_dir,
          const std::vector<std::filesystem::path>& roots);

// Folders for the runtime's shader_override_dir, highest priority first:
// the loaded preset's folder in each root, then each root itself.
std::vector<std::filesystem::path> OverrideDirs();

// Preset names found in any root, sorted. "" (Original) is not listed.
const std::vector<std::string>& preset_names();
// The preset this session runs ("" = Original).
const std::string& loaded_preset();
// The preset chosen for the next start.
std::string selected_preset();
void select_preset(const std::string& name);  // saved to tenchuz.toml

const std::string& description();
const std::vector<Slider>& sliders();
float value(size_t index);
void set_value(size_t index, float value);
void reset_values();
// Writes the loaded preset's values to shader_settings.ini.
bool SaveValues();

bool effects_enabled();
void set_effects_enabled(bool enabled);

// Sends the values (sliders, camera, time) to the GPU plugin if any changed
// since the last call. Cheap; called once per UI frame.
void Push(rex::memory::Memory* memory);

}  // namespace tenchuz::shader_settings
