// tenchuz - settings for the on-screen panel and window behaviour

#pragma once

namespace tenchuz::ui_settings {

// The always-on panel drawn over the game (F6 menu -> HUD tab).
struct Hud {
  bool health = false;
  bool enemies = false;   // alive count, nearest and last-hit enemy
  bool timer = false;     // speedrun mission / run timer
  bool fps = false;       // host and game frame rate
  bool position = false;  // player coordinates
  int corner = 0;         // 0 top-left, 1 top-right, 2 bottom-left, 3 bottom-right
};
Hud hud();
void set_hud(const Hud& hud);

bool mute_when_unfocused();
void set_mute_when_unfocused(bool enabled);

}  // namespace tenchuz::ui_settings
