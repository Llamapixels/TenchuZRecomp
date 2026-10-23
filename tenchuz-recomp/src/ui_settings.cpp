// tenchuz - settings for the on-screen panel and window behaviour

#include "ui_settings.h"

#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(developer_tools, false, "Tenchu Z",
                    "Show the F6 > Tools tab (memory scanner, camera probe)");
REXCVAR_DEFINE_BOOL(hud_health, false, "Tenchu Z/HUD", "Show health");
REXCVAR_DEFINE_BOOL(hud_enemies, false, "Tenchu Z/HUD",
                    "Show enemy count, nearest and last-hit enemy");
REXCVAR_DEFINE_BOOL(hud_timer, false, "Tenchu Z/HUD", "Show speedrun timer");
REXCVAR_DEFINE_BOOL(hud_fps, false, "Tenchu Z/HUD", "Show frame rate");
REXCVAR_DEFINE_BOOL(hud_position, false, "Tenchu Z/HUD",
                    "Show player position");
REXCVAR_DEFINE_INT32(hud_corner, 0, "Tenchu Z/HUD",
                     "Panel corner: 0 top-left, 1 top-right, 2 bottom-left, "
                     "3 bottom-right")
    .range(0, 3);
REXCVAR_DEFINE_BOOL(shader_overrides_enabled, true, "Tenchu Z",
                    "Load HLSL shader overrides from shader_overrides/ and "
                    "mods/<mod>/shaders/ (applies at startup)");
REXCVAR_DEFINE_BOOL(mute_when_unfocused, true, "Tenchu Z",
                    "Mute the game while its window is not focused");
REXCVAR_DEFINE_STRING(gpu_backend, "d3d12", "Tenchu Z",
                      "Renderer: d3d12 or vulkan (applies at startup)");

namespace tenchuz::ui_settings {

// These are only read and written on the UI thread.
Hud hud() {
  Hud h;
  h.health = REXCVAR_GET(hud_health);
  h.enemies = REXCVAR_GET(hud_enemies);
  h.timer = REXCVAR_GET(hud_timer);
  h.fps = REXCVAR_GET(hud_fps);
  h.position = REXCVAR_GET(hud_position);
  h.corner = REXCVAR_GET(hud_corner);
  return h;
}

void set_hud(const Hud& h) {
  REXCVAR_SET(hud_health, h.health);
  REXCVAR_SET(hud_enemies, h.enemies);
  REXCVAR_SET(hud_timer, h.timer);
  REXCVAR_SET(hud_fps, h.fps);
  REXCVAR_SET(hud_position, h.position);
  REXCVAR_SET(hud_corner, h.corner);
}

bool mute_when_unfocused() { return REXCVAR_GET(mute_when_unfocused); }
void set_mute_when_unfocused(bool enabled) {
  REXCVAR_SET(mute_when_unfocused, enabled);
}

std::string renderer() {
  return REXCVAR_GET(gpu_backend) == "vulkan" ? "vulkan" : "d3d12";
}
void set_renderer(const std::string& name) {
  rex::cvar::SetFlagByName("gpu_backend", name);
}

namespace {
std::string g_requested_renderer = "d3d12";
std::string g_active_renderer = "d3d12";
}  // namespace

const std::string& requested_renderer() { return g_requested_renderer; }
const std::string& active_renderer() { return g_active_renderer; }
void set_active_renderer(const std::string& requested,
                         const std::string& active) {
  g_requested_renderer = requested;
  g_active_renderer = active;
}

}  // namespace tenchuz::ui_settings
