// tenchuz - photo mode: a free camera. See photo_mode.h.

#include "photo_mode.h"

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>
#include <string>

#include <rex/hook.h>
#include <rex/logging.h>

#include "controller.h"

REX_EXTERN(__imp__sub_823E1F30);

namespace tenchuz::photo_mode {
namespace {

constexpr uint32_t kEye = 16;
constexpr uint32_t kTarget = 32;
constexpr float kMoveSpeed = 6.0f;    // world units per frame at full stick
constexpr float kLookSpeed = 0.035f;  // radians per frame at full stick
constexpr float kMaxPitch = 1.5f;

std::atomic<bool> g_active{false};

// Free-camera state; only touched on the guest thread inside the hook.
struct State {
  bool initialised = false;
  float pos[3] = {};
  float yaw = 0, pitch = 0;
} g_state;

float LoadF(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return std::bit_cast<float>(std::byteswap(v));
}

void StoreF(uint8_t* p, float f) {
  const uint32_t v = std::byteswap(std::bit_cast<uint32_t>(f));
  std::memcpy(p, &v, 4);
}

void Direction(float yaw, float pitch, float out[3]) {
  out[0] = std::cos(pitch) * std::sin(yaw);
  out[1] = std::sin(pitch);
  out[2] = std::cos(pitch) * std::cos(yaw);
}

void Update(uint8_t* camera) {
  if (!g_state.initialised) {
    // Start exactly where the game's camera is looking.
    float target[3];
    for (int i = 0; i < 3; ++i) {
      g_state.pos[i] = LoadF(camera + kEye + i * 4);
      target[i] = LoadF(camera + kTarget + i * 4);
    }
    const float dx = target[0] - g_state.pos[0];
    const float dy = target[1] - g_state.pos[1];
    const float dz = target[2] - g_state.pos[2];
    const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(len > 1e-3f) || !std::isfinite(len)) {
      return;  // camera not set up yet
    }
    g_state.yaw = std::atan2(dx, dz);
    g_state.pitch = std::asin(std::clamp(dy / len, -1.0f, 1.0f));
    g_state.initialised = true;
  }

  const controller::RawPad pad = controller::last_raw_pad();
  // Diagnostic: log what photo mode receives while a stick is pushed, twice
  // a second, so input problems can be read from the log.
  static int log_counter = 0;
  auto mag = [](float v) { return v < 0 ? -v : v; };
  const bool pushed = mag(pad.lx) > 0.2f || mag(pad.ly) > 0.2f ||
                      mag(pad.rx) > 0.2f || mag(pad.ry) > 0.2f;
  if (pushed && ++log_counter % 15 == 0) {
    std::string slots;
    for (int i = 0; i < 4; ++i) {
      const controller::RawPad r = controller::raw_pad(i);
      slots += std::format(" | s{} {:+.2f} {:+.2f} {:+.2f} {:+.2f}", i, r.lx,
                           r.ly, r.rx, r.ry);
    }
    REXLOG_INFO("photo pad: used {:+.2f} {:+.2f} {:+.2f} {:+.2f}{}", pad.lx,
                pad.ly, pad.rx, pad.ry, slots);
  }
  float speed = kMoveSpeed;
  if (pad.buttons & 0x0200) speed *= 4.0f;   // RB
  if (pad.buttons & 0x0100) speed *= 0.25f;  // LB

  g_state.yaw += pad.rx * kLookSpeed;
  g_state.pitch = std::clamp(g_state.pitch + pad.ry * kLookSpeed,
                             -kMaxPitch, kMaxPitch);

  float forward[3];
  Direction(g_state.yaw, g_state.pitch, forward);
  const float right[3] = {std::cos(g_state.yaw), 0.0f,
                          -std::sin(g_state.yaw)};
  for (int i = 0; i < 3; ++i) {
    g_state.pos[i] += (forward[i] * pad.ly + right[i] * pad.lx) * speed;
  }
  g_state.pos[1] += (pad.rt - pad.lt) * speed;

  for (int i = 0; i < 3; ++i) {
    StoreF(camera + kEye + i * 4, g_state.pos[i]);
    StoreF(camera + kTarget + i * 4, g_state.pos[i] + forward[i] * 100.0f);
  }
}

}  // namespace

bool active() { return g_active.load(std::memory_order_relaxed); }

void set_active(bool on) {
  g_active.store(on, std::memory_order_relaxed);
  controller::set_block_game_input(on);
  REXLOG_INFO("photo mode {}", on ? "on" : "off");
}

}  // namespace tenchuz::photo_mode

REX_HOOK_RAW(sub_823E1F30) {
  const uint32_t camera = ctx.r3.u32;
  __imp__sub_823E1F30(ctx, base);
  namespace pm = tenchuz::photo_mode;
  if (pm::active()) {
    pm::Update(base + camera);
  } else {
    pm::g_state.initialised = false;
  }
}
