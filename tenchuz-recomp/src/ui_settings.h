// tenchuz - settings for the on-screen panel and window behaviour

#pragma once

#include <string>

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

// Renderer: "d3d12" or "vulkan". The setting applies at the next start.
// requested_renderer() is what the setting was at start-up, and
// active_renderer() the one this run uses (D3D12 when Vulkan failed).
std::string renderer();
void set_renderer(const std::string& name);
const std::string& requested_renderer();
const std::string& active_renderer();
void set_active_renderer(const std::string& requested,
                         const std::string& active);

}  // namespace tenchuz::ui_settings
