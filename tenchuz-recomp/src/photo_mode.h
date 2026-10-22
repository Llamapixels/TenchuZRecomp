// tenchuz - photo mode: a free camera
//
// Hooks the gameplay camera's update (sub_823E1F30, the second method of the
// camera class at vtable 0x821DB970, see gameplay.h). After the game has
// placed the camera, the eye (+16) and look-at point (+32) are replaced by a
// free camera's. While photo mode is on, the game receives a neutral
// controller, so the character stands still, and the controller flies the
// camera instead:
//
//   left stick   move            right stick  look
//   RT / LT      up / down       RB / LB      fast / slow
//
// The game keeps running (enemies still move); combine with god mode.

#pragma once

namespace tenchuz::photo_mode {

bool active();
void set_active(bool active);
inline void Toggle() { set_active(!active()); }

}  // namespace tenchuz::photo_mode
