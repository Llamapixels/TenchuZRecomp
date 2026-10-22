// tenchuz - controller options applied to the raw XInput state
//
// Everything here runs inside the hook on the game's XInputGetState wrapper
// (sub_824103F8, see gameplay.h), before the game derives anything from the
// state, so the game sees a consistent controller:
//
//   * button remapping - each game button can be fed by any physical button
//     or trigger
//   * stick deadzones - the game applies its own fixed deadzone (7849 left,
//     8689 right, from the table at 0x82833E78); the raw value is rescaled so
//     the effective deadzone becomes the configured one
//   * camera invert (right stick)
//
// Rumble strength is applied in the hook on the XInputSetState wrapper
// (sub_82410408).
//
// XINPUT_STATE in guest memory, big-endian: +4 u16 buttons, +6 u8 left
// trigger, +7 u8 right trigger, +8/+10 left stick, +12/+14 right stick.

#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace tenchuz::controller {

// The 16 controls, in the order used by the remap table.
enum Control : int {
  kA, kB, kX, kY, kLB, kRB, kLT, kRT, kBack, kStart, kLS, kRS,
  kUp, kDown, kLeft, kRight, kControlCount
};
const char* ControlName(int control);

// Reads the button_map setting. Call once after the config is loaded.
void LoadFromConfig();

// remap[game_control] = physical control feeding it.
using Remap = std::array<int, kControlCount>;
Remap remap();
void set_remap(const Remap& remap);
Remap IdentityRemap();

// Deadzones as a fraction of full deflection (game default 0.24 / 0.27).
double left_deadzone();
double right_deadzone();
void set_deadzones(double left, double right);
inline constexpr double kGameLeftDeadzone = 7849.0 / 32767.0;
inline constexpr double kGameRightDeadzone = 8689.0 / 32767.0;

double rumble_strength();  // 0 = off, 1 = normal, up to 2
void set_rumble_strength(double strength);

// Applies remap, deadzones and right-stick invert to a guest XINPUT_STATE.
void ProcessState(uint8_t* state, uint32_t slot, bool invert_x,
                  bool invert_y);

// Scales a guest XINPUT_VIBRATION (two big-endian u16 motor speeds).
void ProcessVibration(uint8_t* vibration);

// Raw physical state of the first controller, captured before any
// processing (used by photo mode, which drives its own camera).
struct RawPad {
  uint16_t buttons = 0;
  float lt = 0, rt = 0;              // 0..1
  float lx = 0, ly = 0, rx = 0, ry = 0;  // -1..1, deadzone applied
};
RawPad last_raw_pad();
RawPad raw_pad(int slot);  // one controller slot, for diagnostics

// While set, the game receives a neutral controller (photo mode).
void set_block_game_input(bool block);

}  // namespace tenchuz::controller
