// tenchuz - controller options applied to the raw XInput state.
// See controller.h.

#include "controller.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <sstream>

#include <rex/cvar.h>

REXCVAR_DEFINE_STRING(button_map, "", "Tenchu Z/Controller",
                      "Button remap: 16 physical controls, in the order A B X Y "
                      "LB RB LT RT Back Start LS RS Up Down Left Right, that "
                      "feed those game buttons. Empty = default");
REXCVAR_DEFINE_DOUBLE(left_stick_deadzone, 7849.0 / 32767.0,
                      "Tenchu Z/Controller",
                      "Left stick deadzone, fraction of full deflection")
    .range(0.0, 0.6);
REXCVAR_DEFINE_DOUBLE(right_stick_deadzone, 8689.0 / 32767.0,
                      "Tenchu Z/Controller",
                      "Right stick deadzone, fraction of full deflection")
    .range(0.0, 0.6);
REXCVAR_DEFINE_DOUBLE(rumble_strength, 1.0, "Tenchu Z/Controller",
                      "Rumble strength: 0 off, 1 normal, 2 double")
    .range(0.0, 2.0);

namespace tenchuz::controller {
namespace {

constexpr const char* kNames[kControlCount] = {
    "A", "B", "X", "Y", "LB", "RB", "LT", "RT",
    "Back", "Start", "LS", "RS", "Up", "Down", "Left", "Right"};

// XINPUT button bits; triggers have none (0).
constexpr uint16_t kBits[kControlCount] = {
    0x1000, 0x2000, 0x4000, 0x8000, 0x0100, 0x0200, 0, 0,
    0x0020, 0x0010, 0x0040, 0x0080, 0x0001, 0x0002, 0x0004, 0x0008};

constexpr uint8_t kTriggerPressed = 30;  // XINPUT_GAMEPAD_TRIGGER_THRESHOLD

std::mutex g_remap_mutex;
Remap g_remap = {};
bool g_remap_identity = true;

std::atomic<bool> g_block{false};
std::mutex g_raw_mutex;
RawPad g_raw[4];  // per controller slot

template <typename T>
T Load(T& v) {
  return std::atomic_ref<T>(v).load(std::memory_order_relaxed);
}

Remap Parse(const std::string& text) {
  Remap map = IdentityRemap();
  std::istringstream in(text);
  std::string token;
  for (int i = 0; i < kControlCount && in >> token; ++i) {
    for (int c = 0; c < kControlCount; ++c) {
      if (token == kNames[c]) {
        map[i] = c;
        break;
      }
    }
  }
  return map;
}

// Parses the cvar into the table the hook uses. Only called on the UI side
// (startup and set_remap); the guest thread never touches the cvar's string.
void RefreshRemap() {
  const std::string text = REXCVAR_GET(button_map);
  const Remap parsed = Parse(text);
  std::lock_guard lock(g_remap_mutex);
  g_remap = parsed;
  g_remap_identity = parsed == IdentityRemap();
}

uint16_t LoadU16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }
void StoreU16(uint8_t* p, uint16_t v) {
  p[0] = uint8_t(v >> 8);
  p[1] = uint8_t(v);
}
int16_t LoadS16(const uint8_t* p) { return int16_t(LoadU16(p)); }
void StoreS16(uint8_t* p, int16_t v) { StoreU16(p, uint16_t(v)); }

// Rescales one stick axis so that the game's fixed deadzone `game` acts like
// `wanted`: values inside `wanted` become 0, the rest are spread over
// game..32767 so they clear the game's own deadzone.
int16_t Deadzone(int16_t raw, double wanted, double game) {
  const double v = raw;
  const double mag = std::fabs(v);
  const double d = wanted * 32767.0;
  const double g = game * 32767.0;
  if (mag <= d) {
    return 0;
  }
  const double out = g + (mag - d) * (32767.0 - g) / (32767.0 - d);
  const double clamped = std::min(out, 32767.0);
  return int16_t(v < 0 ? -clamped : clamped);
}

float Normalise(int16_t raw, double dz) {
  const double d = dz * 32767.0;
  const double mag = std::fabs(double(raw));
  if (mag <= d) {
    return 0.0f;
  }
  const double n = (mag - d) / (32767.0 - d);
  return float(raw < 0 ? -n : n);
}

}  // namespace

const char* ControlName(int control) {
  return control >= 0 && control < kControlCount ? kNames[control] : "?";
}

Remap IdentityRemap() {
  Remap map;
  for (int i = 0; i < kControlCount; ++i) {
    map[i] = i;
  }
  return map;
}

void LoadFromConfig() { RefreshRemap(); }

Remap remap() {
  std::lock_guard lock(g_remap_mutex);
  return g_remap;
}

void set_remap(const Remap& map) {
  std::string text;
  if (map != IdentityRemap()) {
    for (int i = 0; i < kControlCount; ++i) {
      if (i) text += ' ';
      text += kNames[map[i]];
    }
  }
  rex::cvar::SetFlagByName("button_map", text);
  RefreshRemap();
}

double left_deadzone() { return Load(REXCVAR_GET(left_stick_deadzone)); }
double right_deadzone() { return Load(REXCVAR_GET(right_stick_deadzone)); }
void set_deadzones(double left, double right) {
  std::atomic_ref<double>(REXCVAR_GET(left_stick_deadzone)).store(left);
  std::atomic_ref<double>(REXCVAR_GET(right_stick_deadzone)).store(right);
}

double rumble_strength() { return Load(REXCVAR_GET(rumble_strength)); }
void set_rumble_strength(double strength) {
  std::atomic_ref<double>(REXCVAR_GET(rumble_strength)).store(strength);
}

void set_block_game_input(bool block) { g_block.store(block); }

RawPad raw_pad(int slot) {
  std::lock_guard lock(g_raw_mutex);
  return g_raw[slot & 3];
}

RawPad last_raw_pad() {
  std::lock_guard lock(g_raw_mutex);
  // Merge the slots so whichever controller (or the keyboard, which the
  // runtime presents as its own slot) is in use drives photo mode.
  RawPad out;
  // Larger magnitude wins. Compared as squares: std::fabs here dropped
  // negative values in the built game (photo mode only moved right / up).
  auto bigger = [](float a, float b) { return b * b > a * a ? b : a; };
  for (const RawPad& p : g_raw) {
    out.buttons |= p.buttons;
    out.lt = std::max(out.lt, p.lt);
    out.rt = std::max(out.rt, p.rt);
    out.lx = bigger(out.lx, p.lx);
    out.ly = bigger(out.ly, p.ly);
    out.rx = bigger(out.rx, p.rx);
    out.ry = bigger(out.ry, p.ry);
  }
  return out;
}

void ProcessState(uint8_t* state, uint32_t slot, bool invert_x,
                  bool invert_y) {
  uint8_t* buttons_p = state + 4;
  const uint16_t buttons = LoadU16(buttons_p);
  const uint8_t lt = state[6];
  const uint8_t rt = state[7];

  {
    std::lock_guard lock(g_raw_mutex);
    RawPad& raw = g_raw[slot & 3];
    raw.buttons = buttons;
    raw.lt = lt / 255.0f;
    raw.rt = rt / 255.0f;
    raw.lx = Normalise(LoadS16(state + 8), kGameLeftDeadzone);
    raw.ly = Normalise(LoadS16(state + 10), kGameLeftDeadzone);
    raw.rx = Normalise(LoadS16(state + 12), kGameRightDeadzone);
    raw.ry = Normalise(LoadS16(state + 14), kGameRightDeadzone);
  }

  if (g_block.load(std::memory_order_relaxed)) {
    std::memset(state + 4, 0, 12);  // neutral controller
    return;
  }

  // Remap: each game control is pressed when its physical source is.
  bool identity;
  Remap map;
  {
    std::lock_guard lock(g_remap_mutex);
    identity = g_remap_identity;
    map = g_remap;
  }
  if (!identity) {
    auto physical = [&](int c) {
      if (c == kLT) return lt >= kTriggerPressed;
      if (c == kRT) return rt >= kTriggerPressed;
      return (buttons & kBits[c]) != 0;
    };
    // Analog triggers stay analog when they are mapped straight through.
    auto trigger_value = [&](int game_trigger, uint8_t own) -> uint8_t {
      const int src = map[game_trigger];
      if (src == kLT) return lt;
      if (src == kRT) return rt;
      (void)own;
      return physical(src) ? 255 : 0;
    };
    uint16_t out = buttons & 0x0400;  // keep the guide button
    for (int c = 0; c < kControlCount; ++c) {
      if (kBits[c] && physical(map[c])) {
        out |= kBits[c];
      }
    }
    StoreU16(buttons_p, out);
    state[6] = trigger_value(kLT, lt);
    state[7] = trigger_value(kRT, rt);
  }

  // Deadzones.
  const double ldz = left_deadzone();
  const double rdz = right_deadzone();
  if (ldz != kGameLeftDeadzone) {
    StoreS16(state + 8, Deadzone(LoadS16(state + 8), ldz, kGameLeftDeadzone));
    StoreS16(state + 10,
             Deadzone(LoadS16(state + 10), ldz, kGameLeftDeadzone));
  }
  if (rdz != kGameRightDeadzone) {
    StoreS16(state + 12,
             Deadzone(LoadS16(state + 12), rdz, kGameRightDeadzone));
    StoreS16(state + 14,
             Deadzone(LoadS16(state + 14), rdz, kGameRightDeadzone));
  }

  // Camera invert.
  auto flip = [&](uint8_t* p) {
    const int16_t v = LoadS16(p);
    StoreS16(p, v == INT16_MIN ? INT16_MAX : int16_t(-v));
  };
  if (invert_x) flip(state + 12);
  if (invert_y) flip(state + 14);
}

void ProcessVibration(uint8_t* vibration) {
  const double k = rumble_strength();
  if (k == 1.0) {
    return;
  }
  for (int motor = 0; motor < 2; ++motor) {
    uint8_t* p = vibration + motor * 2;
    const double v = LoadU16(p) * k;
    StoreU16(p, uint16_t(std::min(v, 65535.0)));
  }
}

}  // namespace tenchuz::controller
