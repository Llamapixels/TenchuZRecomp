// tenchuz - gameplay cheats built on recompiled-function hooks
//
// Every recompiled guest function is emitted as a weak symbol (see
// DEFINE_REX_FUNC in generated/default/tenchuz_pch.h), so the game can replace
// any of them with a strong definition and still reach the original through
// its __imp__ alias. That is the only reliable way to change game behaviour in
// a static recompilation: the original PowerPC bytes in guest memory are never
// executed, so patching them (the previous approach) has no effect at all.
//
// Guest facts used here, all read from the recompiled code:
//
//   Character object (one per actor - players and enemies alike)
//     +136  u32    bit 0x10 set on the locally controlled character
//     +160  u32    actor class flags; 0x1000 = player ninja
//     +4488 float  maximum health
//     +4492 float  current health (dead below 1.0)
//
//   sub_822D8C20  TakeDamage(this, f1 = amount, r5 = attacker, r6, r7)
//                 hp -= amount * defence * random; at hp < 1.0 it zeroes hp,
//                 flags the actor dead and runs the death handler. It is the
//                 single place character health is reduced. Its own
//                 "is this the local player" test is the +160/+136 pair above.
//   sub_822CAA08  hp / max_hp, the HUD health-bar ratio (a vtable slot).
//   sub_822E3940  Heal: hp = min(hp + amount, max_hp).
//
//   0x82B57A58    actor table: 1024 guest pointers, looked up by actor id in
//                 sub_8228FE80.
//
//   sub_8229A940  Spend money: money = max(money - price, 0) on the profile
//                 object (+328). Its three callers are the shop screens.
//
//   sub_8228C7D8  Controller update, once per frame. Fills a 192-byte entry
//                 per pad at 0x82B57470 with the raw XInput state and, from
//                 it, normalised floats: +28/+32 triggers, +36/+40 left
//                 stick, +44/+48 right stick (-1..1, deadzone applied). Every
//                 camera reads the right stick from there.
//   sub_82296FF8  Play a movie (path, mode); blocks until it ends, returns 1.
//                 The boot scenes call it with game:\DATA\MOVIE\LOGO.wmv and
//                 OP.wmv; story cutscenes come from sub_82271408.
//   sub_8241BF50  D3D Swap (device, front-buffer texture, ...). The texture's
//                 fetch constant at +36 holds width-1 (13 bits) and
//                 height-1 (next 13 bits) of the frame sent to the display.
//   sub_824103F8  XInputGetState(user, XINPUT_STATE*) wrapper; the pad update
//                 above is its only caller. Thumbsticks are big-endian s16 at
//                 +8 LX, +10 LY, +12 RX, +14 RY of the state.
//
//   Gameplay camera, found with the camera probe (camera_probe.h) and
//   confirmed in the hub and in a mission:
//     0x8340BCF4    pointer to the active camera (the player also holds one
//                   at +0x1264); the object starts with vtable 0x821DB970,
//                   whose update is sub_823E1F30
//     +64 float     field of view (radians), +72 "projection changed" flag
//     +192 / +208   eye / look-at offsets from the mode's table, eased in
//     +200 float    current follow distance (eases toward the table value)
//     +288 / +292   pitch / yaw
//   sub_823E0A88  SetFov(camera, f1): writes +64 and sets +72. Every camera
//                 mode calls it; the follow camera asks for 0.837758 (48 deg).
//   sub_823E3830  Eye placement (out, v1 eye offset, v2 look-at offset,
//                 f1 yaw): out = v2 + rotate(v1 - v2). Called by the follow,
//                 and two other, camera modes.
//   The player reads its sticks through a controller vtable (0x82002F64)
//   whose GetAnalog is sub_8228C4D0, i.e. the pad floats above. Bit 2 of
//   player +5504 is the game's own horizontal invert, and the camera's +84
//   flag its vertical one.

#pragma once

#include <cstdint>

namespace rex::memory {
class Memory;
}

namespace tenchuz::gameplay {

struct PlayerHealth {
  uint32_t object = 0;  // guest address of the character
  float hp = 0.0f;
  float max_hp = 0.0f;
};

bool god_mode();
void set_god_mode(bool enabled);

bool free_shopping();
void set_free_shopping(bool enabled);

// The game's own HUD (health, item ring, ki and partner cards): hidden while
// this is on, for screenshots and videos (F11, F6 > HUD).
bool hide_game_hud();
void set_hide_game_hud(bool hidden);

// Experimental 60 fps: while the player's character exists (hub and
// missions) the game presents every vblank instead of every second one.
// Menus and loading screens stay at 30 because their animations are per frame.
bool frame_rate_60();
void set_frame_rate_60(bool enabled);

// Locates the locally controlled player character. Returns false outside of a
// mission (no player character exists) or before the runtime is up.
bool FindLocalPlayer(rex::memory::Memory* memory, PlayerHealth* out);

// Sets the local player's health back to its maximum.
bool RefillLocalPlayer(rex::memory::Memory* memory);

// Gives the hooks access to guest memory for player lookups. Call once the
// runtime is up.
void SetMemory(rex::memory::Memory* memory);

// Camera settings. Invert is applied to the raw controller state the game
// receives (sub_824103F8), so every value the game derives from the stick -
// floats, direction bits, its own raw copies - agrees. Speed scales the
// normalised floats (sub_8228C7D8). Field of view scales what each camera
// mode asks SetFov for; distance moves the eye along its line to the look-at
// point.
struct CameraInput {
  bool invert_x = false;
  bool invert_y = false;
  double sensitivity_x = 1.0;
  double sensitivity_y = 1.0;
  double fov_degrees = 48.0;    // the game's own value is 48
  double distance_scale = 1.0;  // multiplies the game's follow distance
};
inline constexpr double kGameFovDegrees = 48.0;
CameraInput camera_input();
void set_camera_input(const CameraInput& input);

bool skip_intro_movies();
void set_skip_intro_movies(bool enabled);

// Damage scaling, applied in TakeDamage. Scripted kills are never scaled.
struct DamageSettings {
  double player_taken = 1.0;  // damage the local player takes (0..3)
  double enemy_taken = 1.0;   // damage every non-player character takes
  bool one_hit_kills = false;
};
DamageSettings damage_settings();
void set_damage_settings(const DamageSettings& settings);

// A character other than a player: actor table entries whose class is not
// the player class and whose health fields hold plausible values.
struct Actor {
  uint32_t object = 0;
  float hp = 0, max_hp = 0;
  float x = 0, y = 0, z = 0;  // position, character +3456
  float distance = 0;         // to the local player
};
struct EnemySummary {
  int alive = 0;
  bool have_nearest = false;
  Actor nearest;
  bool have_last_hit = false;
  Actor last_hit;  // the last character the local player damaged
};
EnemySummary SummarizeEnemies(rex::memory::Memory* memory);

// The local player's position (character +3456, read by the game's own
// position getter sub_823402F0). Setting it is experimental: other copies
// of the position may pull the character back.
bool GetPlayerPosition(rex::memory::Memory* memory, float xyz[3]);
bool SetPlayerPosition(rex::memory::Memory* memory, const float xyz[3]);

// The active camera's eye (+16), look-at point (+32) and field of view in
// radians (+64), as last placed by the game (or photo mode). False when no
// gameplay camera exists.
bool GetCameraView(rex::memory::Memory* memory, float eye[3], float target[3],
                   float* fov);

// Money (ryo) of the active profile: the game manager (*0x82B57814) holds
// four profile slots at +144; the shop uses the first whose +20 is 1, and
// keeps the balance at +328 (see sub_8229C290 / sub_8229A940).
bool GetMoney(rex::memory::Memory* memory, uint32_t* money);
bool SetMoney(rex::memory::Memory* memory, uint32_t money);

// Number of the last mission the game loaded (M-xx.set), from the mission
// loader sub_8229EDE0 (index at +80 of its object); -1 before the first.
int current_mission();

// Increments each time the game starts loading a mission's M-xx.set, which
// tells a mission apart from the hub (which loads no M-xx.set).
uint32_t mission_loads();

// Frames the game has presented (counts calls to D3D Swap).
uint64_t presented_frames();

// Replaces the right stick of every pad with (x, y) until cleared. Used by
// the camera probe to turn the camera without anyone holding a controller.
void SetRightStickOverride(bool active, float x = 0.0f, float y = 0.0f);

// Size of the last frame the game presented, before the runtime's
// resolution scale. False until the first frame.
bool LastFrameSize(uint32_t* width, uint32_t* height);

}  // namespace tenchuz::gameplay
