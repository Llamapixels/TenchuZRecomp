// tenchuz - gameplay cheats built on recompiled-function hooks
//
// See gameplay.h for the guest layout these hooks rely on.

#include "gameplay.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cmath>

#include "controller.h"
#include "display_settings.h"

#include <atomic>
#include <bit>
#include <cstring>
#include <algorithm>
#include <format>
#include <mutex>
#include <string>
#include <vector>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>

REXCVAR_DEFINE_BOOL(god_mode, false, "Tenchu Z",
                    "The local player takes no damage (F8 toggles in game)");
REXCVAR_DEFINE_BOOL(free_shopping, false, "Tenchu Z",
                    "Shop purchases do not cost money");
REXCVAR_DEFINE_BOOL(hide_game_hud, false, "Tenchu Z",
                    "Hide the game's HUD (F11 toggles in game)");
REXCVAR_DEFINE_BOOL(frame_rate_60, false, "Tenchu Z",
                    "Experimental: 60 fps while your character is in the "
                    "world (menus stay at 30)");
REXCVAR_DEFINE_BOOL(camera_invert_x, false, "Tenchu Z",
                    "Invert the camera's horizontal (right stick X) axis");
REXCVAR_DEFINE_BOOL(camera_invert_y, false, "Tenchu Z",
                    "Invert the camera's vertical (right stick Y) axis");
REXCVAR_DEFINE_DOUBLE(camera_sensitivity_x, 1.0, "Tenchu Z",
                      "Camera horizontal speed multiplier")
    .range(0.1, 3.0);
REXCVAR_DEFINE_DOUBLE(camera_sensitivity_y, 1.0, "Tenchu Z",
                      "Camera vertical speed multiplier")
    .range(0.1, 3.0);
REXCVAR_DEFINE_DOUBLE(camera_fov, 48.0, "Tenchu Z",
                      "Camera field of view in degrees (game default 48)")
    .range(30.0, 100.0);
REXCVAR_DEFINE_DOUBLE(camera_distance, 1.0, "Tenchu Z",
                      "Camera follow distance multiplier (game default 1)")
    .range(0.5, 2.5);
REXCVAR_DEFINE_BOOL(camera_mouse_direct, true, "Tenchu Z",
                    "Mouse look turns the camera by exactly the distance moved "
                    "(off: the mouse acts as a right stick)");
REXCVAR_DEFINE_DOUBLE(camera_mouse_speed, 1.0, "Tenchu Z",
                      "Direct mouse look speed")
    .range(0.05, 20.0);
REXCVAR_DEFINE_DOUBLE(player_damage_taken, 1.0, "Tenchu Z",
                      "Damage the local player takes (0 = none, 3 = triple)")
    .range(0.0, 3.0);
REXCVAR_DEFINE_DOUBLE(enemy_damage_taken, 1.0, "Tenchu Z",
                      "Damage other characters take")
    .range(0.0, 5.0);
REXCVAR_DEFINE_BOOL(one_hit_kills, false, "Tenchu Z",
                    "Any damage kills a non-player character");
REXCVAR_DEFINE_BOOL(skip_intro_movies, false, "Tenchu Z",
                    "Skip the logo and opening movies at startup");

// Original (unhooked) bodies of the functions replaced below.
REX_EXTERN(__imp__sub_822D8C20);
REX_EXTERN(__imp__sub_8229A940);
REX_EXTERN(__imp__sub_8241BDE0);
REX_EXTERN(__imp__sub_8228C7D8);
REX_EXTERN(__imp__sub_82296FF8);
REX_EXTERN(__imp__sub_8241BF50);
REX_EXTERN(__imp__sub_824103F8);
REX_EXTERN(__imp__sub_823E0A88);
REX_EXTERN(__imp__sub_823E1128);
REX_EXTERN(__imp__sub_823E0C40);
REX_EXTERN(__imp__sub_823E3830);
REX_EXTERN(__imp__sub_82410408);
REX_EXTERN(__imp__sub_8229EDE0);
REX_EXTERN(__imp__sub_82342500);
REX_EXTERN(__imp__sub_823E1A20);
REX_EXTERN(__imp__sub_823E1C58);
REX_EXTERN(__imp__sub_823E1E18);
REX_EXTERN(__imp__sub_823E22C8);

namespace tenchuz::gameplay {
namespace {

constexpr uint32_t kOffsetControlFlags = 136;
constexpr uint32_t kLocalControlBit = 0x10;
constexpr uint32_t kOffsetActorClass = 160;
constexpr uint32_t kPlayerClassBit = 0x1000;
constexpr uint32_t kOffsetMaxHp = 4488;
constexpr uint32_t kOffsetHp = 4492;

constexpr uint32_t kActorTable = 0x82B57A58u;
constexpr uint32_t kActorTableSlots = 1024;

constexpr uint32_t kPadTable = 0x82B57470u;
constexpr uint32_t kPadStride = 192;
constexpr uint32_t kPadCount = 4;
constexpr uint32_t kPadRightX = 44;
constexpr uint32_t kPadRightY = 48;

constexpr uint32_t kXInputRightX = 12;  // within XINPUT_STATE
constexpr uint32_t kXInputRightY = 14;

// The active gameplay camera (see gameplay.h).
constexpr uint32_t kCameraPointer = 0x8340BCF4u;
constexpr uint32_t kCameraVtable = 0x821DB970u;
constexpr uint32_t kCameraFov = 64;
constexpr uint32_t kCameraProjectionDirty = 72;

// Camera fields used by direct mouse look (gameplay.h).
constexpr uint32_t kCameraEye = 16;
constexpr uint32_t kCameraLookAt = 32;
constexpr uint32_t kCameraInvertY = 84;  // the game's vertical invert; 0 = off
constexpr uint32_t kCameraFollowPoint = 256;  // the player's position
constexpr uint32_t kCameraPitch = 288;
constexpr uint32_t kCameraYaw = 292;
constexpr uint32_t kCameraLookPitch = 304;  // first-person look mode
constexpr uint32_t kCameraLookYaw = 308;
constexpr uint32_t kCameraMode = 320;
constexpr uint32_t kCameraYawRamp = 360;
constexpr uint32_t kCameraPitchRamp = 364;

// Camera modes (functions from the table at 0x82AB2C78) that turn with the
// right stick through the yaw / pitch steps, or (kLookMode) by its own angles.
constexpr uint32_t kFollowMode = 0x823E38D0u;
constexpr uint32_t kLookMode = 0x823E22C8u;
constexpr uint32_t kDirectMouseModes[] = {kFollowMode, kLookMode, 0x823E3D58u,
                                          0x823E3FC8u};

// Player characters, the list the game's own local-player lookup walks.
constexpr uint32_t kPlayerSlots = 0x82B58A58u;
constexpr uint32_t kPlayerSlotCount = 4;
constexpr uint32_t kOffsetPlayerOptions = 5504;
constexpr uint32_t kInvertXOption = 0x2;  // the game's horizontal invert

// Direct mouse look turn per physical pixel (counts, under pointer lock) at
// speed 1: 0.07 degrees, 360 degrees for about 5000 counts.
constexpr double kMouseRadiansPerPixel = 0.0012;

// This frame's mouse turn, from BeginMouseTurn to EndMouseTurn. The camera
// update runs on one guest thread, so no lock.
struct MouseTurn {
  uint32_t camera = 0;
  float yaw = 0, pitch = 0;  // still to apply, in the game's yaw / pitch sense
  float turned_yaw = 0, turned_pitch = 0;  // applied, after the game's limits
};
MouseTurn g_mouse_turn;
// Whether the camera took the mouse motion since the last controller read.
std::atomic<bool> g_mouse_taken{false};

// The field of view the current camera mode asked for (SetFov), before
// scaling, and the factor last written into the live camera.
std::atomic<float> g_game_fov{0.0f};
std::atomic<uint32_t> g_fov_camera{0};
double g_applied_fov_factor = 1.0;  // guest pad thread only
float g_applied_aspect = 16.0f / 9.0f;

rex::memory::Memory* g_memory = nullptr;

std::atomic<bool> g_stick_override{false};
std::atomic<float> g_stick_override_x{0.0f};
std::atomic<float> g_stick_override_y{0.0f};
std::atomic<uint32_t> g_frame_size{0};  // width << 16 | height
std::atomic<uint64_t> g_frames{0};
std::atomic<int> g_mission{-1};
std::atomic<uint32_t> g_mission_loads{0};
std::atomic<uint32_t> g_last_hit{0};  // character the player last damaged

constexpr uint32_t kOffsetPosition = 3456;
constexpr uint32_t kOffsetHeading = 3492;

// Teleport requested from the UI thread, carried out on the guest thread.
std::mutex g_teleport_mutex;
bool g_teleport_pending = false;
float g_teleport_target[3] = {};
constexpr uint32_t kGameManager = 0x82B57814u;
constexpr uint32_t kProfileSlots = 144;
constexpr uint32_t kProfileActive = 20;
constexpr uint32_t kProfileMoney = 328;
constexpr double kOneHitDamage = 1.0e6;

// Scripted deaths (pits, mission-failure traps) call TakeDamage with exactly
// this amount. God mode lets them through: blocking one leaves the death
// animation and state machine running on a character that is still alive,
// which soft-locks the mission.
constexpr double kScriptedKillDamage = 10000.0;

// The cvars are plain variables written from the UI thread and read from
// guest threads, so every access goes through atomic_ref.
template <typename T>
T LoadFlag(T& flag) {
  return std::atomic_ref<T>(flag).load(std::memory_order_relaxed);
}

template <typename T>
void StoreFlag(T& flag, T value) {
  std::atomic_ref<T>(flag).store(value, std::memory_order_relaxed);
}

// Guest memory is big-endian.
uint32_t LoadBeU32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, sizeof(v));
  return std::byteswap(v);
}

float LoadBeFloat(const uint8_t* p) {
  return std::bit_cast<float>(LoadBeU32(p));
}

void StoreBeFloat(uint8_t* p, float f) {
  const uint32_t v = std::byteswap(std::bit_cast<uint32_t>(f));
  std::memcpy(p, &v, sizeof(v));
}

void StoreBeU32(uint8_t* p, uint32_t v) {
  v = std::byteswap(v);
  std::memcpy(p, &v, sizeof(v));
}

bool IsPlayerClass(const uint8_t* object) {
  return (LoadBeU32(object + kOffsetActorClass) & kPlayerClassBit) != 0;
}

bool IsLocalPlayer(const uint8_t* object) {
  return (LoadBeU32(object + kOffsetActorClass) & kPlayerClassBit) != 0 &&
         (LoadBeU32(object + kOffsetControlFlags) & kLocalControlBit) != 0;
}

// Memory::TranslateVirtual does no validation, and the actor table can hold
// pointers into pages the guest has since released, so every object read from
// the UI thread is checked against the heap's committed pages first.
bool GuestRangeReadable(rex::memory::Memory* memory, uint32_t address,
                        uint32_t length) {
  rex::memory::BaseHeap* heap = memory->LookupHeap(address);
  if (!heap) {
    return false;
  }
  rex::memory::HeapAllocationInfo info{};
  if (!heap->QueryRegionInfo(address, &info)) {
    return false;
  }
  constexpr uint32_t kCommit =
      static_cast<uint32_t>(rex::memory::kMemoryAllocationCommit);
  if ((info.state & kCommit) == 0 ||
      address + length > info.base_address + info.region_size) {
    return false;
  }
  return heap->QueryRangeAccess(address, address + length - 1) !=
         rex::memory::PageAccess::kNoAccess;
}

}  // namespace

bool god_mode() { return LoadFlag(REXCVAR_GET(god_mode)); }

void set_god_mode(bool enabled) {
  StoreFlag(REXCVAR_GET(god_mode), enabled);
  REXLOG_INFO("god mode {}", enabled ? "on" : "off");
}

bool free_shopping() { return LoadFlag(REXCVAR_GET(free_shopping)); }

void set_free_shopping(bool enabled) {
  StoreFlag(REXCVAR_GET(free_shopping), enabled);
  REXLOG_INFO("free shopping {}", enabled ? "on" : "off");
}

bool hide_game_hud() { return LoadFlag(REXCVAR_GET(hide_game_hud)); }

bool frame_rate_60() { return LoadFlag(REXCVAR_GET(frame_rate_60)); }

void set_frame_rate_60(bool enabled) {
  StoreFlag(REXCVAR_GET(frame_rate_60), enabled);
  REXLOG_INFO("60 fps {}", enabled ? "on" : "off");
}

// The HUD is hidden in the GPU plugin: in a mission the game draws the 3D
// frame, then its final screen pass (pixel shader 822F99ECDB1B0ACD), then all
// 2D on top. The plugin skips the draws after that pass until the frame is
// shown. The HUD's own code (CCockpit, sub_82394AF0) only updates sprites that
// another part of the game draws, so skipping it froze the HUD instead.
void set_hide_game_hud(bool hidden) {
  StoreFlag(REXCVAR_GET(hide_game_hud), hidden);
  using HideFn = void (*)(uint64_t);
  static const HideFn hide = [] {
    HMODULE plugin = GetModuleHandleW(L"rexgpu-xenos.dll");
    return plugin ? reinterpret_cast<HideFn>(
                        GetProcAddress(plugin, "rex_hide_draws_after_shader"))
                  : nullptr;
  }();
  if (!hide) {
    REXLOG_WARN("hide HUD: GPU plugin has no rex_hide_draws_after_shader");
    return;
  }
  constexpr uint64_t kFinalScreenPass = 0x822F99ECDB1B0ACDull;
  hide(hidden ? kFinalScreenPass : 0);
  REXLOG_INFO("game HUD {}", hidden ? "hidden" : "shown");
}

CameraInput camera_input() {
  CameraInput input;
  input.invert_x = LoadFlag(REXCVAR_GET(camera_invert_x));
  input.invert_y = LoadFlag(REXCVAR_GET(camera_invert_y));
  input.sensitivity_x = LoadFlag(REXCVAR_GET(camera_sensitivity_x));
  input.sensitivity_y = LoadFlag(REXCVAR_GET(camera_sensitivity_y));
  input.fov_degrees = LoadFlag(REXCVAR_GET(camera_fov));
  input.distance_scale = LoadFlag(REXCVAR_GET(camera_distance));
  input.mouse_direct = LoadFlag(REXCVAR_GET(camera_mouse_direct));
  input.mouse_speed = LoadFlag(REXCVAR_GET(camera_mouse_speed));
  return input;
}

void SetMemory(rex::memory::Memory* memory) { g_memory = memory; }

void set_camera_input(const CameraInput& input) {
  StoreFlag(REXCVAR_GET(camera_invert_x), input.invert_x);
  StoreFlag(REXCVAR_GET(camera_invert_y), input.invert_y);
  StoreFlag(REXCVAR_GET(camera_sensitivity_x), input.sensitivity_x);
  StoreFlag(REXCVAR_GET(camera_sensitivity_y), input.sensitivity_y);
  StoreFlag(REXCVAR_GET(camera_fov), input.fov_degrees);
  StoreFlag(REXCVAR_GET(camera_distance), input.distance_scale);
  StoreFlag(REXCVAR_GET(camera_mouse_direct), input.mouse_direct);
  StoreFlag(REXCVAR_GET(camera_mouse_speed), input.mouse_speed);
}

bool skip_intro_movies() { return LoadFlag(REXCVAR_GET(skip_intro_movies)); }

void set_skip_intro_movies(bool enabled) {
  StoreFlag(REXCVAR_GET(skip_intro_movies), enabled);
}

void SetRightStickOverride(bool active, float x, float y) {
  g_stick_override_x.store(x, std::memory_order_relaxed);
  g_stick_override_y.store(y, std::memory_order_relaxed);
  g_stick_override.store(active, std::memory_order_release);
}

bool LastFrameSize(uint32_t* width, uint32_t* height) {
  const uint32_t packed = g_frame_size.load(std::memory_order_relaxed);
  if (packed == 0) {
    return false;
  }
  *width = packed >> 16;
  *height = packed & 0xFFFF;
  return true;
}

bool FindLocalPlayer(rex::memory::Memory* memory, PlayerHealth* out) {
  if (!memory) {
    return false;
  }
  const uint8_t* table = memory->TranslateVirtual<const uint8_t*>(kActorTable);
  for (uint32_t slot = 0; slot < kActorTableSlots; ++slot) {
    const uint32_t object = LoadBeU32(table + slot * 4);
    if (object == 0 ||
        !GuestRangeReadable(memory, object, kOffsetHp + sizeof(float))) {
      continue;
    }
    const uint8_t* p = memory->TranslateVirtual<const uint8_t*>(object);
    if (!IsLocalPlayer(p)) {
      continue;
    }
    const float max_hp = LoadBeFloat(p + kOffsetMaxHp);
    const float hp = LoadBeFloat(p + kOffsetHp);
    // Plausibility guard against a stale slot that happens to carry the flag
    // bits: health is finite, non-negative and never above its maximum.
    if (!(max_hp > 0.0f && max_hp < 1.0e6f && hp >= 0.0f && hp <= max_hp)) {
      continue;
    }
    out->object = object;
    out->hp = hp;
    out->max_hp = max_hp;
    return true;
  }
  return false;
}

bool RefillLocalPlayer(rex::memory::Memory* memory) {
  PlayerHealth player;
  if (!FindLocalPlayer(memory, &player)) {
    return false;
  }
  // A dead player (hp below 1.0) is left alone: the death state machine has
  // already started, and reviving underneath it is the same soft-lock as
  // blocking a scripted kill.
  if (player.hp < 1.0f) {
    return false;
  }
  uint8_t* p = memory->TranslateVirtual<uint8_t*>(player.object);
  StoreBeFloat(p + kOffsetHp, player.max_hp);
  return true;
}

DamageSettings damage_settings() {
  DamageSettings d;
  d.player_taken = LoadFlag(REXCVAR_GET(player_damage_taken));
  d.enemy_taken = LoadFlag(REXCVAR_GET(enemy_damage_taken));
  d.one_hit_kills = LoadFlag(REXCVAR_GET(one_hit_kills));
  return d;
}

void set_damage_settings(const DamageSettings& d) {
  StoreFlag(REXCVAR_GET(player_damage_taken), d.player_taken);
  StoreFlag(REXCVAR_GET(enemy_damage_taken), d.enemy_taken);
  StoreFlag(REXCVAR_GET(one_hit_kills), d.one_hit_kills);
}

namespace {

// Reads a non-player character, if `object` is one.
bool ReadActor(rex::memory::Memory* memory, uint32_t object, Actor* out) {
  if (object == 0 ||
      !GuestRangeReadable(memory, object, kOffsetHp + sizeof(float))) {
    return false;
  }
  const uint8_t* p = memory->TranslateVirtual<const uint8_t*>(object);
  if (IsPlayerClass(p) || LoadBeU32(p + kOffsetActorClass) == 0) {
    return false;
  }
  const float max_hp = LoadBeFloat(p + kOffsetMaxHp);
  const float hp = LoadBeFloat(p + kOffsetHp);
  if (!(max_hp >= 1.0f && max_hp < 1.0e5f && hp >= 0.0f && hp <= max_hp)) {
    return false;
  }
  out->object = object;
  out->hp = hp;
  out->max_hp = max_hp;
  out->x = LoadBeFloat(p + kOffsetPosition);
  out->y = LoadBeFloat(p + kOffsetPosition + 4);
  out->z = LoadBeFloat(p + kOffsetPosition + 8);
  return std::isfinite(out->x) && std::isfinite(out->y) &&
         std::isfinite(out->z);
}

}  // namespace

EnemySummary SummarizeEnemies(rex::memory::Memory* memory) {
  EnemySummary summary;
  if (!memory) {
    return summary;
  }
  float me[3] = {0, 0, 0};
  const bool have_me = GetPlayerPosition(memory, me);
  auto distance = [&](const Actor& a) {
    const float dx = a.x - me[0], dy = a.y - me[1], dz = a.z - me[2];
    return std::sqrt(dx * dx + dy * dy + dz * dz);
  };
  const uint8_t* table = memory->TranslateVirtual<const uint8_t*>(kActorTable);
  for (uint32_t slot = 0; slot < kActorTableSlots; ++slot) {
    Actor actor;
    if (!ReadActor(memory, LoadBeU32(table + slot * 4), &actor) ||
        actor.hp < 1.0f) {
      continue;
    }
    ++summary.alive;
    actor.distance = have_me ? distance(actor) : 0.0f;
    if (have_me && (!summary.have_nearest ||
                    actor.distance < summary.nearest.distance)) {
      summary.nearest = actor;
      summary.have_nearest = true;
    }
  }
  const uint32_t last = g_last_hit.load(std::memory_order_relaxed);
  if (ReadActor(memory, last, &summary.last_hit)) {
    summary.last_hit.distance = have_me ? distance(summary.last_hit) : 0.0f;
    summary.have_last_hit = true;
  }
  return summary;
}

bool GetPlayerPosition(rex::memory::Memory* memory, float xyz[3]) {
  PlayerHealth player;
  if (!FindLocalPlayer(memory, &player)) {
    return false;
  }
  const uint8_t* p = memory->TranslateVirtual<const uint8_t*>(player.object);
  for (int i = 0; i < 3; ++i) {
    xyz[i] = LoadBeFloat(p + kOffsetPosition + i * 4);
  }
  return true;
}

bool SetPlayerPosition(rex::memory::Memory* memory, const float xyz[3]) {
  PlayerHealth player;
  if (!FindLocalPlayer(memory, &player) || player.hp < 1.0f) {
    return false;
  }
  // Writing +3456 directly works once and then fights the game, which also
  // keeps the previous position (+3472) and movement state. The game's own
  // placement routine sets all of it, so the move is queued and done with
  // that routine at the start of the next frame (see the pad update hook).
  std::lock_guard lock(g_teleport_mutex);
  std::memcpy(g_teleport_target, xyz, sizeof(g_teleport_target));
  g_teleport_pending = true;
  return true;
}

bool GetCameraView(rex::memory::Memory* memory, float eye[3], float target[3],
                   float* fov) {
  if (!memory) {
    return false;
  }
  const uint32_t camera = LoadBeU32(
      memory->TranslateVirtual<const uint8_t*>(kCameraPointer));
  if (camera == 0) {
    return false;
  }
  const uint8_t* c = memory->TranslateVirtual<const uint8_t*>(camera);
  if (LoadBeU32(c) != kCameraVtable) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    eye[i] = LoadBeFloat(c + 16 + i * 4);
    target[i] = LoadBeFloat(c + 32 + i * 4);
  }
  *fov = LoadBeFloat(c + kCameraFov);
  return *fov > 0.0f;
}

namespace {

uint32_t ActiveProfile(rex::memory::Memory* memory) {
  if (!memory) {
    return 0;
  }
  const uint32_t manager =
      LoadBeU32(memory->TranslateVirtual<const uint8_t*>(kGameManager));
  if (manager == 0 ||
      !GuestRangeReadable(memory, manager, kProfileSlots + 16)) {
    return 0;
  }
  const uint8_t* m = memory->TranslateVirtual<const uint8_t*>(manager);
  for (uint32_t i = 0; i < 4; ++i) {
    const uint32_t profile = LoadBeU32(m + kProfileSlots + i * 4);
    if (profile &&
        GuestRangeReadable(memory, profile, kProfileMoney + 4) &&
        LoadBeU32(memory->TranslateVirtual<const uint8_t*>(profile) +
                  kProfileActive) == 1) {
      return profile;
    }
  }
  return 0;
}

}  // namespace

bool GetMoney(rex::memory::Memory* memory, uint32_t* money) {
  const uint32_t profile = ActiveProfile(memory);
  if (!profile) {
    return false;
  }
  *money = LoadBeU32(memory->TranslateVirtual<const uint8_t*>(profile) +
                     kProfileMoney);
  return true;
}

bool SetMoney(rex::memory::Memory* memory, uint32_t money) {
  const uint32_t profile = ActiveProfile(memory);
  if (!profile) {
    return false;
  }
  // The game's own heal-money routine (sub_8229A918) caps at 99,999,999.
  StoreBeU32(memory->TranslateVirtual<uint8_t*>(profile) + kProfileMoney,
             std::min<uint32_t>(money, 99999999u));
  return true;
}

int current_mission() { return g_mission.load(std::memory_order_relaxed); }

uint32_t mission_loads() {
  return g_mission_loads.load(std::memory_order_relaxed);
}

uint64_t presented_frames() {
  return g_frames.load(std::memory_order_relaxed);
}

}  // namespace tenchuz::gameplay

// TakeDamage. With god mode on, damage to the local player is dropped before
// any of it is applied, and the function reports "not killed" (r3 = 0) exactly
// as it does for a non-lethal hit. Enemies, co-op partners and scripted kills
// are unaffected.
REX_HOOK_RAW(sub_822D8C20) {
  namespace gp = tenchuz::gameplay;
  const uint8_t* target = base + ctx.r3.u32;
  const bool scripted = ctx.f1.f64 >= gp::kScriptedKillDamage;
  if (gp::IsLocalPlayer(target)) {
    if (!scripted) {
      if (gp::god_mode()) {
        ctx.r3.u64 = 0;
        return;
      }
      ctx.f1.f64 *= gp::damage_settings().player_taken;
    }
  } else if (!gp::IsPlayerClass(target)) {
    // Remember who the local player is fighting, for the enemy readout.
    const uint32_t attacker = ctx.r5.u32;
    if (attacker != 0 && gp::IsLocalPlayer(base + attacker)) {
      gp::g_last_hit.store(ctx.r3.u32, std::memory_order_relaxed);
    }
    if (!scripted && ctx.f1.f64 > 0.0) {
      const gp::DamageSettings d = gp::damage_settings();
      ctx.f1.f64 = d.one_hit_kills ? gp::kOneHitDamage
                                   : ctx.f1.f64 * d.enemy_taken;
    }
  }
  __imp__sub_822D8C20(ctx, base);
}

// D3D: before every Swap, builds the GPU's wait for the vblank the frame may
// be shown on, from the device's presentation interval (device + 13248). The
// game sets it to 2, so it shows a frame every second vblank: 30 fps at 60 Hz.
// Its gameplay advances by elapsed time, so with 60 fps on the interval is 1
// for this call while the player's character exists. Menus keep 2: their
// animations step per frame and would run twice as fast.
REX_HOOK_RAW(sub_8241BDE0) {
  namespace gp = tenchuz::gameplay;
  constexpr uint32_t kOffsetPresentInterval = 13248;
  uint8_t* interval = base + ctx.r3.u32 + kOffsetPresentInterval;
  bool changed = false;
  if (gp::frame_rate_60() && gp::LoadBeU32(interval) == 2) {
    gp::PlayerHealth player;
    if (gp::FindLocalPlayer(gp::g_memory, &player)) {
      gp::StoreBeU32(interval, 1);
      changed = true;
    }
  }
  __imp__sub_8241BDE0(ctx, base);
  if (changed) {
    gp::StoreBeU32(interval, 2);
  }
}

// Spend money. A plain return leaves the balance untouched; the shop code has
// already checked the price against the balance, so nothing else depends on
// the subtraction.
REX_HOOK_RAW(sub_8229A940) {
  namespace gp = tenchuz::gameplay;
  if (gp::free_shopping()) {
    return;
  }
  __imp__sub_8229A940(ctx, base);
}

// Raw controller read. Inverting here, before the game sees the state, keeps
// every value it derives from the stick consistent; flipping only the
// normalised floats (the first attempt) did not invert the in-mission camera.
REX_HOOK_RAW(sub_824103F8) {
  const uint32_t slot = ctx.r3.u32;
  const uint32_t state = ctx.r4.u32;
  __imp__sub_824103F8(ctx, base);

  namespace gp = tenchuz::gameplay;
  if (ctx.r3.u32 != 0 || state == 0) {
    return;  // no controller
  }
  const gp::CameraInput input = gp::camera_input();
  tenchuz::controller::ProcessState(base + state, slot, input.invert_x,
                                    input.invert_y);
}

// XInputSetState wrapper (user, XINPUT_VIBRATION*): rumble strength.
REX_HOOK_RAW(sub_82410408) {
  if (ctx.r4.u32 != 0) {
    tenchuz::controller::ProcessVibration(base + ctx.r4.u32);
  }
  __imp__sub_82410408(ctx, base);
}

// Mission loader state machine; its object holds the mission number at +80.
REX_HOOK_RAW(sub_8229EDE0) {
  namespace gp = tenchuz::gameplay;
  const uint8_t* object = base + ctx.r3.u32;
  const int state = static_cast<int16_t>((object[76] << 8) | object[77]);
  const int mission = static_cast<int16_t>((object[80] << 8) | object[81]);
  if (mission >= 0 && mission < 100) {
    gp::g_mission.store(mission, std::memory_order_relaxed);
    if (state == 0) {  // first step of a new load
      gp::g_mission_loads.fetch_add(1, std::memory_order_relaxed);
    }
  }
  __imp__sub_8229EDE0(ctx, base);
}

namespace tenchuz::gameplay {
namespace {

// Every 4-byte-aligned place in the guest heap and image data holding the
// exact (x, y, z) triple. Exact float equality on all three is specific
// enough that unrelated matches are not a practical concern.
std::vector<uint32_t> FindPositionCopies(const uint8_t* base,
                                         const float xyz[3]) {
  std::vector<uint32_t> found;
  uint32_t want[3];
  for (int i = 0; i < 3; ++i) {
    want[i] = std::byteswap(std::bit_cast<uint32_t>(xyz[i]));
  }
  constexpr struct {
    uint32_t lo, hi;
  } kRanges[] = {{0x40000000u, 0x70000000u}, {0x82000000u, 0x83440000u}};
  for (const auto& range : kRanges) {
    uint32_t addr = range.lo;
    while (addr < range.hi && found.size() < 256) {
      rex::memory::BaseHeap* heap = g_memory->LookupHeap(addr);
      rex::memory::HeapAllocationInfo info{};
      if (!heap || !heap->QueryRegionInfo(addr, &info) ||
          info.region_size == 0) {
        addr = (addr & ~0xFFFu) + 0x1000u;
        continue;
      }
      const uint32_t end =
          std::min(info.base_address + info.region_size, range.hi);
      constexpr uint32_t kCommit =
          static_cast<uint32_t>(rex::memory::kMemoryAllocationCommit);
      if ((info.state & kCommit) &&
          heap->QueryRangeAccess(addr, end - 1) !=
              rex::memory::PageAccess::kNoAccess) {
        for (uint32_t a = addr; a + 12 <= end; a += 4) {
          if (std::memcmp(base + a, want, 12) == 0) {
            found.push_back(a);
          }
        }
      }
      addr = std::max(end, addr + 0x1000u);
    }
  }
  return found;
}

// Moves the local player with the game's own placement routine
// sub_82342500(character, v1 = position, f1 = heading), the one spawns and
// checkpoints use: it writes the position and the previous position, resets
// the movement state flags (+4656) and raises the "moved" flag (+4800). The
// guest registers are saved and restored around the call, so the frame
// carries on exactly as before.
void PerformPendingTeleport(PPCContext& ctx, uint8_t* base) {
  float target[3];
  {
    std::lock_guard lock(g_teleport_mutex);
    if (!g_teleport_pending) {
      return;
    }
    g_teleport_pending = false;
    std::memcpy(target, g_teleport_target, sizeof(target));
  }
  PlayerHealth player;
  if (!FindLocalPlayer(g_memory, &player) || player.hp < 1.0f) {
    return;
  }
  const uint8_t* p = base + player.object;
  float from[3];
  for (int i = 0; i < 3; ++i) {
    from[i] = LoadBeFloat(p + kOffsetPosition + i * 4);
  }
  const std::vector<uint32_t> copies = FindPositionCopies(base, from);
  const PPCContext saved = ctx;
  ctx.r3.u64 = player.object;
  // Vector registers hold PPC element i in f32[3 - i].
  ctx.v1.f32[3] = target[0];
  ctx.v1.f32[2] = target[1];
  ctx.v1.f32[1] = target[2];
  ctx.v1.f32[0] = 1.0f;
  ctx.f1.f64 = LoadBeFloat(p + kOffsetHeading);  // keep facing the same way
  __imp__sub_82342500(ctx, base);
  ctx = saved;
  // Move every other exact copy of the old position too (physics and
  // collision keep their own); the game restores from those otherwise.
  std::string where;
  for (uint32_t address : copies) {
    for (int i = 0; i < 3; ++i) {
      StoreBeFloat(base + address + i * 4, target[i]);
    }
    if (where.size() < 400) {
      where += std::format(" {:08X}", address);
    }
  }
  REXLOG_INFO("teleport: player {:08X} ({:.1f}, {:.1f}, {:.1f}) -> ({:.1f}, "
              "{:.1f}, {:.1f}); {} other copies:{}",
              player.object, from[0], from[1], from[2], target[0], target[1],
              target[2], copies.size(), where);
}

// Applies a changed field-of-view setting to the live camera at once, rather
// than waiting for the next camera mode change to call SetFov. Writes the
// scaled value and raises the camera's "projection changed" flag, exactly as
// SetFov does.
void ApplyFovSetting(uint8_t* base, const CameraInput& input) {
  const double factor = input.fov_degrees / kGameFovDegrees;
  const float aspect = display_settings::aspect();
  if (factor == g_applied_fov_factor && aspect == g_applied_aspect) {
    return;
  }
  const uint32_t camera = LoadBeU32(base + kCameraPointer);
  const float game_fov = g_game_fov.load(std::memory_order_relaxed);
  if (camera == 0 || camera != g_fov_camera.load(std::memory_order_relaxed) ||
      game_fov <= 0.0f || LoadBeU32(base + camera) != kCameraVtable) {
    return;  // no camera yet; SetFov will apply the setting when it comes
  }
  StoreBeFloat(base + camera + kCameraFov,
               static_cast<float>(game_fov * factor));
  const uint32_t dirty = std::byteswap(uint32_t{1});
  std::memcpy(base + camera + kCameraProjectionDirty, &dirty, 4);
  g_applied_fov_factor = factor;
  g_applied_aspect = aspect;
}

// --- Direct mouse look (gameplay.h) ------------------------------------------

float WrapAngle(double a) {
  constexpr double kTwoPi = 6.283185307179586;
  a = std::remainder(a, kTwoPi);
  return static_cast<float>(a);
}

void LoadVec3(const uint8_t* p, float v[3]) {
  for (int i = 0; i < 3; ++i) {
    v[i] = LoadBeFloat(p + i * 4);
  }
}

void StoreVec3(uint8_t* p, const float v[3]) {
  for (int i = 0; i < 3; ++i) {
    StoreBeFloat(p + i * 4, v[i]);
  }
}

// The rows of the game's yaw matrix (sub_82273E90), which it applies to a
// camera-space offset as offset * M.
void YawRows(float yaw, float m[3][3]) {
  const float c = std::cos(yaw), s = std::sin(yaw);
  const float rows[3][3] = {{c, 0, -s}, {0, 1, 0}, {s, 0, c}};
  std::memcpy(m, rows, sizeof(rows));
}

// The rows of the game's pitch matrix (sub_822CA550, used by the eye
// placement sub_823E3830), applied the same way.
void PitchRows(float pitch, float m[3][3]) {
  const float c = std::cos(pitch), s = std::sin(pitch);
  const float rows[3][3] = {{1, 0, 0}, {0, c, s}, {0, -s, c}};
  std::memcpy(m, rows, sizeof(rows));
}

// out = v * M
void TimesRows(const float v[3], const float m[3][3], float out[3]) {
  float r[3];
  for (int i = 0; i < 3; ++i) {
    r[i] = v[0] * m[0][i] + v[1] * m[1][i] + v[2] * m[2][i];
  }
  std::memcpy(out, r, sizeof(r));
}

// The locally controlled player character, from the game's player list.
uint32_t LocalPlayerObject(const uint8_t* base) {
  for (uint32_t i = 0; i < kPlayerSlotCount; ++i) {
    const uint32_t object = LoadBeU32(base + kPlayerSlots + i * 4);
    if (object != 0 && IsLocalPlayer(base + object)) {
      return object;
    }
  }
  return 0;
}

// Whether the active camera turns with the mouse directly this frame: on, and
// in a mode that turns with the stick.
bool DirectMouseCamera(const uint8_t* base) {
  if (!LoadFlag(REXCVAR_GET(camera_mouse_direct))) {
    return false;
  }
  const uint32_t camera = LoadBeU32(base + kCameraPointer);
  if (camera == 0 || LoadBeU32(base + camera) != kCameraVtable) {
    return false;
  }
  const uint32_t mode = LoadBeU32(base + camera + kCameraMode);
  return std::find(std::begin(kDirectMouseModes), std::end(kDirectMouseModes),
                   mode) != std::end(kDirectMouseModes);
}

// Before the game reads the controllers: the mouse is a stick this frame
// unless the camera turns with it directly. Motion the camera did not take
// since the last read (menus, movies, a stick mode) is dropped, so it never
// arrives later as a jump.
void PrepareMouseForFrame(const uint8_t* base) {
  rex::input::mnk::SetMouseStickEnabled(!DirectMouseCamera(base));
  if (!g_mouse_taken.exchange(false, std::memory_order_relaxed)) {
    float dx, dy;
    rex::input::mnk::TakeMouseDelta(&dx, &dy);
  }
}

// Turns the eased eye and look-at point by this frame's mouse turn, just
// before the game eases them toward where the new angles put them. Turning
// both the same way as their targets keeps the easing (which is linear) from
// lagging behind the turn, while movement is eased as before.
//   yaw:   around the vertical through the player, as the yaw matrix turns
//          the camera's offsets;
//   pitch: the eye around the look-at point, about the camera's sideways
//          axis, as the eye placement does.
void TurnEasedView(uint8_t* camera, float yaw_turn, float pitch_turn) {
  float pivot[3], eye[3], look[3];
  LoadVec3(camera + kCameraFollowPoint, pivot);
  LoadVec3(camera + kCameraEye, eye);
  LoadVec3(camera + kCameraLookAt, look);
  if (yaw_turn != 0.0f) {
    float m[3][3];
    YawRows(yaw_turn, m);
    for (float* p : {eye, look}) {
      float d[3] = {p[0] - pivot[0], p[1] - pivot[1], p[2] - pivot[2]};
      TimesRows(d, m, d);
      for (int i = 0; i < 3; ++i) {
        p[i] = pivot[i] + d[i];
      }
    }
  }
  if (pitch_turn != 0.0f) {
    float yaw_m[3][3], pitch_m[3][3];
    YawRows(LoadBeFloat(camera + kCameraYaw), yaw_m);  // already this frame's
    PitchRows(pitch_turn, pitch_m);
    const float d[3] = {eye[0] - look[0], eye[1] - look[1], eye[2] - look[2]};
    // Into camera space (d * M^T), turn, and back out (* M).
    float local[3];
    for (int i = 0; i < 3; ++i) {
      local[i] = d[0] * yaw_m[i][0] + d[1] * yaw_m[i][1] + d[2] * yaw_m[i][2];
    }
    TimesRows(local, pitch_m, local);
    float turned[3];
    TimesRows(local, yaw_m, turned);
    for (int i = 0; i < 3; ++i) {
      eye[i] = look[i] + turned[i];
    }
  }
  StoreVec3(camera + kCameraEye, eye);
  StoreVec3(camera + kCameraLookAt, look);
}

}  // namespace

bool TakeMouseLook(uint8_t* base, uint32_t camera, MouseLook* look) {
  if (rex::input::mnk::IsMouseStickEnabled() ||
      camera != LoadBeU32(base + kCameraPointer)) {
    return false;
  }
  g_mouse_taken.store(true, std::memory_order_relaxed);
  float dx, dy;
  if (!rex::input::mnk::TakeMouseDelta(&dx, &dy)) {
    return false;
  }
  const double k = kMouseRadiansPerPixel * LoadFlag(REXCVAR_GET(camera_mouse_speed));
  // Mouse right is stick right; screen y grows downward, the stick's upward.
  look->x = static_cast<float>(dx * k);
  look->y = static_cast<float>(-dy * k);
  return std::isfinite(look->x) && std::isfinite(look->y);
}

void BeginMouseTurn(uint8_t* base, uint32_t camera, const MouseLook& look) {
  const CameraInput input = camera_input();
  float x = input.invert_x ? -look.x : look.x;
  float y = input.invert_y ? -look.y : look.y;
  // The game's own invert options, applied where the stick passes them: the
  // player's input update (sub_82342DB8) flips x on its way to +4740, and the
  // camera update flips y into +344 while its +84 is 0. The yaw and pitch
  // steps then add +340 / +344.
  const uint32_t player = LocalPlayerObject(base);
  if (player != 0 &&
      (LoadBeU32(base + player + kOffsetPlayerOptions) & kInvertXOption)) {
    x = -x;
  }
  if (LoadBeU32(base + camera + kCameraInvertY) == 0) {
    y = -y;
  }
  g_mouse_turn = {camera, x, y, 0.0f, 0.0f};
}

void EndMouseTurn() { g_mouse_turn = {}; }

}  // namespace tenchuz::gameplay

// Follow camera yaw step (r3 = camera, new yaw returned in f1). With a mouse
// turn pending, the step runs twice, without and with the turn added to the
// yaw: the difference is the turn after the game's own rules (yaw held
// behind the player, limits while hanging from a ledge), which the easing
// hook below needs. The stick's speed ramp is put back between the runs.
REX_HOOK_RAW(sub_823E1A20) {
  namespace gp = tenchuz::gameplay;
  gp::MouseTurn& turn = gp::g_mouse_turn;
  if (turn.camera != ctx.r3.u32 || turn.yaw == 0.0f) {
    __imp__sub_823E1A20(ctx, base);
    return;
  }
  uint8_t* c = base + turn.camera;
  const float yaw = gp::LoadBeFloat(c + gp::kCameraYaw);
  const float ramp = gp::LoadBeFloat(c + gp::kCameraYawRamp);
  __imp__sub_823E1A20(ctx, base);
  const double without = ctx.f1.f64;
  ctx.r3.u64 = turn.camera;
  gp::StoreBeFloat(c + gp::kCameraYawRamp, ramp);
  gp::StoreBeFloat(c + gp::kCameraYaw, yaw + turn.yaw);
  __imp__sub_823E1A20(ctx, base);
  gp::StoreBeFloat(c + gp::kCameraYaw, yaw);  // the caller stores the result
  turn.turned_yaw = gp::WrapAngle(ctx.f1.f64 - without);
  turn.yaw = 0.0f;
}

// Follow camera pitch step (r3 = camera, new pitch in f1); as the yaw step.
// Its other inputs (the recentre button, the state) are plain reads.
REX_HOOK_RAW(sub_823E1C58) {
  namespace gp = tenchuz::gameplay;
  gp::MouseTurn& turn = gp::g_mouse_turn;
  if (turn.camera != ctx.r3.u32 || turn.pitch == 0.0f) {
    __imp__sub_823E1C58(ctx, base);
    return;
  }
  uint8_t* c = base + turn.camera;
  const float pitch = gp::LoadBeFloat(c + gp::kCameraPitch);
  const float ramp = gp::LoadBeFloat(c + gp::kCameraPitchRamp);
  __imp__sub_823E1C58(ctx, base);
  const double without = ctx.f1.f64;
  ctx.r3.u64 = turn.camera;
  gp::StoreBeFloat(c + gp::kCameraPitchRamp, ramp);
  gp::StoreBeFloat(c + gp::kCameraPitch, pitch + turn.pitch);
  __imp__sub_823E1C58(ctx, base);
  gp::StoreBeFloat(c + gp::kCameraPitch, pitch);
  turn.turned_pitch = gp::WrapAngle(ctx.f1.f64 - without);
  turn.pitch = 0.0f;
}

// Eases a camera's eye (r4) and look-at point (r5) toward v1 / v2. The
// follow camera calls it for its view after the yaw and pitch steps.
REX_HOOK_RAW(sub_823E1E18) {
  namespace gp = tenchuz::gameplay;
  gp::MouseTurn& turn = gp::g_mouse_turn;
  const uint32_t camera = ctx.r3.u32;
  if (turn.camera == camera && ctx.r4.u32 == camera + gp::kCameraEye &&
      ctx.r5.u32 == camera + gp::kCameraLookAt &&
      (turn.turned_yaw != 0.0f || turn.turned_pitch != 0.0f)) {
    gp::TurnEasedView(base + camera, turn.turned_yaw, turn.turned_pitch);
    turn.turned_yaw = 0.0f;
    turn.turned_pitch = 0.0f;
  }
  __imp__sub_823E1E18(ctx, base);
}

// First-person look mode: its own angles, which the mouse turn is added to
// before the game's step applies their limits. The view is placed without
// easing in a frame the mouse moved (+404 bit 0, the game's own "cut").
REX_HOOK_RAW(sub_823E22C8) {
  namespace gp = tenchuz::gameplay;
  gp::MouseTurn& turn = gp::g_mouse_turn;
  if (turn.camera == ctx.r3.u32 && (turn.yaw != 0.0f || turn.pitch != 0.0f)) {
    uint8_t* c = base + turn.camera;
    gp::StoreBeFloat(c + gp::kCameraLookYaw,
                     gp::LoadBeFloat(c + gp::kCameraLookYaw) + turn.yaw);
    gp::StoreBeFloat(c + gp::kCameraLookPitch,
                     gp::LoadBeFloat(c + gp::kCameraLookPitch) + turn.pitch);
    constexpr uint32_t kCameraFlags = 404;
    gp::StoreBeU32(c + kCameraFlags, gp::LoadBeU32(c + kCameraFlags) | 1u);
    turn.yaw = 0.0f;
    turn.pitch = 0.0f;
  }
  __imp__sub_823E22C8(ctx, base);
}

// Controller update, once per frame. Before the game reads the controllers,
// choose whether the mouse is a stick this frame (direct mouse look). After
// the game has normalised the sticks, scale the right stick of every pad (or
// replace it while the camera probe is driving), then apply the camera's
// field of view and distance.
REX_HOOK_RAW(sub_8228C7D8) {
  tenchuz::gameplay::PrepareMouseForFrame(base);
  __imp__sub_8228C7D8(ctx, base);

  namespace gp = tenchuz::gameplay;
  const gp::CameraInput input = gp::camera_input();
  gp::ApplyFovSetting(base, input);
  gp::PerformPendingTeleport(ctx, base);

  const bool override_active =
      gp::g_stick_override.load(std::memory_order_acquire);
  const float scale_x = static_cast<float>(input.sensitivity_x);
  const float scale_y = static_cast<float>(input.sensitivity_y);
  if (!override_active && scale_x == 1.0f && scale_y == 1.0f) {
    return;
  }
  for (uint32_t pad = 0; pad < gp::kPadCount; ++pad) {
    uint8_t* entry = base + gp::kPadTable + pad * gp::kPadStride;
    float x, y;
    if (override_active) {
      x = gp::g_stick_override_x.load(std::memory_order_relaxed);
      y = gp::g_stick_override_y.load(std::memory_order_relaxed);
    } else {
      x = gp::LoadBeFloat(entry + gp::kPadRightX) * scale_x;
      y = gp::LoadBeFloat(entry + gp::kPadRightY) * scale_y;
    }
    gp::StoreBeFloat(entry + gp::kPadRightX, x);
    gp::StoreBeFloat(entry + gp::kPadRightY, y);
  }
}

// Movie player. With skip_intro_movies on, the two boot movies return at
// once with the player's normal result (1); everything else plays.
REX_HOOK_RAW(sub_82296FF8) {
  namespace gp = tenchuz::gameplay;
  if (gp::skip_intro_movies()) {
    const char* path = reinterpret_cast<const char*>(base + ctx.r3.u32);
    const size_t len = strnlen(path, 260);
    auto ends_with = [&](const char* suffix) {
      const size_t n = std::strlen(suffix);
      return len >= n && _strnicmp(path + len - n, suffix, n) == 0;
    };
    if (ends_with("\\LOGO.wmv") || ends_with("\\OP.wmv")) {
      REXLOG_INFO("skipping intro movie {}", std::string_view(path, len));
      ctx.r3.u64 = 1;
      return;
    }
  }
  struct MovieScope {
    MovieScope() { tenchuz::display_settings::SetMoviePlaying(true); }
    ~MovieScope() { tenchuz::display_settings::SetMoviePlaying(false); }
  } movie_scope;
  __imp__sub_82296FF8(ctx, base);
}

// D3D Swap. Records the size of the frame being presented, for the menu's
// video tab; the runtime's resolution scale multiplies this.
REX_HOOK_RAW(sub_8241BF50) {
  namespace gp = tenchuz::gameplay;
  if (ctx.r4.u32 != 0) {
    const uint32_t size = gp::LoadBeU32(base + ctx.r4.u32 + 36);
    const uint32_t width = (size & 0x1FFF) + 1;
    const uint32_t height = ((size >> 13) & 0x1FFF) + 1;
    gp::g_frames.fetch_add(1, std::memory_order_relaxed);
    const uint32_t packed = width << 16 | height;
    if (gp::g_frame_size.exchange(packed, std::memory_order_relaxed) !=
        packed) {
      REXLOG_INFO("game frame size now {}x{}", width, height);
    }
  }
  __imp__sub_8241BF50(ctx, base);
}

// Camera SetFov(camera, f1 = radians): stores the field of view at +64 and
// marks the projection for rebuild at +72. Every camera mode calls it with its
// own value (48 degrees for the follow camera), so scaling here keeps each
// mode's relative zoom.
REX_HOOK_RAW(sub_823E0A88) {
  namespace gp = tenchuz::gameplay;
  const float requested = static_cast<float>(ctx.f1.f64);
  gp::g_game_fov.store(requested, std::memory_order_relaxed);
  gp::g_fov_camera.store(ctx.r3.u32, std::memory_order_relaxed);
  const double factor = gp::camera_input().fov_degrees / gp::kGameFovDegrees;
  ctx.f1.f64 = requested * factor;
  __imp__sub_823E0A88(ctx, base);
}

// Perspective matrix: f1 vertical FOV, f2 aspect, f3 near, f4 far.
// Restrict this to the main camera projection builder's call site, leaving
// unrelated projections alone. Correcting X before stretching the original
// render target to the output preserves geometry and vertical framing.
REX_HOOK_RAW(sub_823E1128) {
  if (ctx.lr == 0x823E1244 &&
      std::abs(ctx.f2.f64 - 16.0 / 9.0) < 0.01) {
    ctx.f2.f64 = tenchuz::display_settings::aspect();
  }
  __imp__sub_823E1128(ctx, base);
}

// Camera manager: choose the same active slot as the original code. Gameplay
// and photo mode use the known follow-camera class; scripted camera classes
// retain 16:9 composition. Dirty the chosen projection on aspect transitions.
REX_HOOK_RAW(sub_823E0C40) {
  namespace gp = tenchuz::gameplay;
  uint32_t slot = 0;
  for (uint32_t i = 0; i < 5; ++i) {
    const int32_t selected = int32_t(gp::LoadBeU32(base + 0x8340BCD4u + i * 4));
    if (selected >= 0 && selected < 16) { slot = uint32_t(selected); break; }
  }
  const uint32_t camera = gp::LoadBeU32(base + 0x8340BCF0u + slot * 4);
  const bool readable = camera && gp::g_memory && gp::GuestRangeReadable(gp::g_memory, camera, 80);
  tenchuz::display_settings::SetSceneCamera(readable && gp::LoadBeU32(base + camera) == gp::kCameraVtable);
  static float last_aspect = 16.0f / 9.0f;
  static uint32_t last_camera = 0;
  const float aspect = tenchuz::display_settings::aspect();
  if (readable && (aspect != last_aspect || camera != last_camera)) {
    gp::StoreBeU32(base + camera + gp::kCameraProjectionDirty, 1);
    last_aspect = aspect;
    last_camera = camera;
  }
  __imp__sub_823E0C40(ctx, base);
}

// Camera eye placement (out, v1 = eye offset, v2 = look-at offset, f1 = yaw):
// rotates (v1 - v2) by the yaw and adds v2. Moving the eye along that line,
// v1' = v2 + k * (v1 - v2), changes the follow distance and keeps the angle.
// The offsets come fresh from the mode's table every frame, so nothing
// accumulates.
REX_HOOK_RAW(sub_823E3830) {
  namespace gp = tenchuz::gameplay;
  const float k = static_cast<float>(gp::camera_input().distance_scale);
  if (k != 1.0f) {
    for (int lane = 0; lane < 4; ++lane) {
      const float look = ctx.v2.f32[lane];
      ctx.v1.f32[lane] = look + k * (ctx.v1.f32[lane] - look);
    }
  }
  __imp__sub_823E3830(ctx, base);
}
