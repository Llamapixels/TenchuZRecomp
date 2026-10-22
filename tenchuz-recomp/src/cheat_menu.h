// tenchuz - in-game menu (F6): cheats, camera, video, game options, mods and
// developer tools. Settings are saved to tenchuz.toml as they change.
//
// An ImGuiDialog hosted by the app's own ImGuiDrawer. No SDK changes: ReXApp
// already exposes OnCreateDialogs(ImGuiDrawer*) for exactly this, and
// rex::ui::RegisterBind() gives us a rebindable hotkey.
//
// ---------------------------------------------------------------------------
// Why the dialog stays resident and only the *window* is toggled
// ---------------------------------------------------------------------------
// ImGuiDrawer draws every registered dialog every frame
// (imgui_drawer.cpp: dialogs_[dialog_loop_next_index_++]->Draw()). The app has
// no per-frame virtual hook of its own, so if the dialog were created and
// destroyed by the hotkey the pin would only be applied while the menu happened
// to be open - pressing F6 again would silently stop the cheat working.
//
// Instead the dialog is added once and never removed; OnDraw always runs, and
// only the ImGui window is hidden when `visible_` is false. The pin is applied
// unconditionally at the top of OnDraw. That makes pinning independent of menu
// visibility, which is the whole point.
//
// ---------------------------------------------------------------------------
// Two workflows, and why the watch list is the primary one
// ---------------------------------------------------------------------------
// A differential scan (First pass / Decreased / ...) requires the player to be
// the timing oracle: notice a change, open the menu, and click the matching
// filter before the value moves again. In practice this is miserable - damage
// resolves faster than the menu can be driven, and clicking the wrong filter
// silently empties the list.
//
// So the UI leads with a *watch list*: type an address, see its live value every
// frame, and pin it with one click. No snapshot cadence to get wrong, and the
// answer to "is this value health?" becomes "take a hit and watch the number"
// instead of a race.
//
// The scanner is still there and still useful - it is how an address is found in
// the first place - but it now has Undo, reports how many candidates a filter
// actually removed, and can push a hit straight into the watch list.
//
// Suggested flow:
//   1. Read the value off the HUD.
//   2. Type it into "exact value", click First pass.
//   3. Take a hit, click Decreased. Repeat until a handful remain.
//   4. "Watch" a candidate, then pin it and confirm it tracks the HUD.

#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>

#include <imgui.h>
#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ui/imgui_dialog.h>

#include "camera_probe.h"
#include "controller.h"
#include "cheat_scanner.h"
#include "gameplay.h"
#include "net_settings.h"
#include "photo_mode.h"
#include "settings_store.h"
#include "shader_settings.h"
#include "speedrun.h"
#include "ui_settings.h"
#include "upnp.h"
#include "version.h"

namespace tenchuz {

class CheatMenuDialog : public rex::ui::ImGuiDialog {
 public:
  CheatMenuDialog(rex::ui::ImGuiDrawer* drawer, rex::memory::Memory* memory)
      : rex::ui::ImGuiDialog(drawer), memory_(memory) {
    scanner_.SetMemory(memory);
    probe_.SetMemory(memory);
  }

  void SetMemory(rex::memory::Memory* memory) {
    memory_ = memory;
    scanner_.SetMemory(memory);
    probe_.SetMemory(memory);
  }

  // Where settings are saved (tenchuz.toml) and reports are written.
  void SetExeDir(const std::filesystem::path& exe_dir) {
    exe_dir_ = exe_dir;
    speedrun_.SetDirectory(exe_dir);
  }

  // Hotkey actions (F1 / F2 / F10), shared with the menu buttons.
  void SavePosition() {
    have_saved_position_ =
        gameplay::GetPlayerPosition(memory_, saved_position_);
    status_ = have_saved_position_ ? "position saved" : "no player character";
  }
  void LoadPosition() {
    status_ = have_saved_position_ &&
                      gameplay::SetPlayerPosition(memory_, saved_position_)
                  ? "position restored"
                  : "nothing saved, or no living player";
  }

  // Called by the Exit game button (on the UI thread).
  void SetExitHandler(std::function<void()> handler) {
    exit_handler_ = std::move(handler);
  }

  // Mods mounted at startup, for the Mods tab.
  void SetMods(std::vector<std::string> mods) { mods_ = std::move(mods); }

  // Invoked at the top of every draw, before the visibility check, for work
  // that must happen once per frame whether or not the menu is open.
  void SetFrameHook(std::function<void()> hook) {
    frame_hook_ = std::move(hook);
  }

  void Toggle() { visible_ = !visible_; }
  bool visible() const { return visible_; }

  // Mouse look keeps the cursor captured and hidden. It is paused while the
  // menu is open (so the menu can be clicked) and can be paused by hotkey.
  void ToggleMouseLookPause() { mouse_look_paused_ = !mouse_look_paused_; }

  bool pin_active() const { return scanner_.pin_active(); }

 protected:
  void OnDraw(ImGuiIO& io) override {
    (void)io;

    if (frame_hook_) {
      frame_hook_();
    }

    // Applied unconditionally and every frame, before any visibility check, so
    // they keep working while the menu is closed.
    scanner_.ApplyPin();
    scanner_.RefreshWatches();
    probe_.Tick();
    speedrun_.Tick(memory_);
    shader_settings::Push(memory_);
    rex::input::mnk::SetMouseLookActive(!visible_ && !mouse_look_paused_);
    DrawOverlay();
    DrawStartHint();

    if (!visible_) {
      return;
    }

    ImGui::SetNextWindowSize(ImVec2(660, 560), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Tenchu Z v" TENCHUZ_VERSION " (F6)###Tenchu Z (F6)", &visible_,
                      ImGuiWindowFlags_NoCollapse)) {
      ImGui::End();
      return;
    }

    if (ImGui::BeginTabBar("tabs")) {
      if (ImGui::BeginTabItem("Cheats")) {
        DrawPlayer();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Speedrun")) {
        DrawSpeedrun();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Controller")) {
        DrawController();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("HUD")) {
        DrawHudSettings();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Camera")) {
        DrawCamera();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Shader")) {
        DrawShader();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Online")) {
        DrawOnline();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Game")) {
        DrawGame();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Mods")) {
        DrawMods();
        ImGui::EndTabItem();
      }
      // Developer tools (memory scanner, camera probe): only with
      // developer_tools = true in tenchuz.toml.
      if (rex::cvar::Query<bool>("developer_tools") && ImGui::BeginTabItem("Tools")) {
        DrawTools();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ImGui::Button("Exit game")) {
      ImGui::OpenPopup("Exit Tenchu Z?");
    }
    if (ImGui::BeginPopupModal("Exit Tenchu Z?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextUnformatted("Unsaved progress will be lost.");
      if (ImGui::Button("Exit")) {
        ImGui::CloseCurrentPopup();
        if (exit_handler_) {
          exit_handler_();
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel")) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    if (!saved_note_.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", saved_note_.c_str());
    }
    ImGui::End();
  }

 private:
  // Settings are written to tenchuz.toml as soon as they change, so the menu
  // and the launcher always agree.
  void Save(std::initializer_list<std::string_view> names) {
    if (exe_dir_.empty()) {
      return;
    }
    saved_note_ = SaveSettings(exe_dir_ / "tenchuz.toml", names)
                      ? "Settings saved to tenchuz.toml"
                      : "Could not save settings (see log)";
  }

  // ---------------------------------------------------------------------
  // Cheats - hooks on the game's own damage and shop functions
  // (gameplay.cpp), so they need no address hunting.
  // ---------------------------------------------------------------------
  void DrawPlayer() {
    gameplay::PlayerHealth player;
    if (gameplay::FindLocalPlayer(memory_, &player)) {
      ImGui::Text("Health: %.0f / %.0f", player.hp, player.max_hp);
      ImGui::SameLine();
      if (ImGui::Button("Refill (F5)")) {
        status_ = gameplay::RefillLocalPlayer(memory_)
                      ? "health refilled"
                      : "cannot refill (player is dead)";
      }
    } else {
      ImGui::TextDisabled("Health: no player character (not in a mission)");
    }

    bool god = gameplay::god_mode();
    if (ImGui::Checkbox("God mode (F8)", &god)) {
      gameplay::set_god_mode(god);
      Save({"god_mode"});
    }
    ImGui::SameLine();
    ImGui::TextDisabled("no damage; pits and scripted deaths still kill");

    bool free_shop = gameplay::free_shopping();
    if (ImGui::Checkbox("Free shop purchases", &free_shop)) {
      gameplay::set_free_shopping(free_shop);
      Save({"free_shopping"});
    }

    uint32_t money = 0;
    if (gameplay::GetMoney(memory_, &money)) {
      ImGui::Text("Money: %u ryo", money);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(120);
      ImGui::InputInt("##money", &money_input_, 1000, 10000);
      ImGui::SameLine();
      if (ImGui::Button("Set money")) {
        status_ = gameplay::SetMoney(memory_, static_cast<uint32_t>(
                                                  std::max(0, money_input_)))
                      ? "money set"
                      : "no active profile";
      }
    } else {
      ImGui::TextDisabled("Money: no active profile");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Damage");
    gameplay::DamageSettings d = gameplay::damage_settings();
    float taken = static_cast<float>(d.player_taken);
    float dealt = static_cast<float>(d.enemy_taken);
    ImGui::SetNextItemWidth(260);
    bool moved = ImGui::SliderFloat("Damage you take", &taken, 0.0f, 3.0f,
                                    "%.2fx");
    bool done = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(260);
    moved |= ImGui::SliderFloat("Damage enemies take", &dealt, 0.0f, 5.0f,
                                "%.2fx");
    done |= ImGui::IsItemDeactivatedAfterEdit();
    bool ohk = d.one_hit_kills;
    const bool ohk_changed = ImGui::Checkbox("One-hit kills", &ohk);
    if (moved || ohk_changed) {
      d.player_taken = taken;
      d.enemy_taken = dealt;
      d.one_hit_kills = ohk;
      gameplay::set_damage_settings(d);
    }
    if (done || ohk_changed) {
      Save({"player_damage_taken", "enemy_damage_taken", "one_hit_kills"});
    }
    ImGui::TextDisabled("God mode overrides damage you take. Scripted "
                        "deaths are never scaled.");

    ImGui::Separator();
    DrawEnemies();
    if (!status_.empty()) {
      ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "%s", status_.c_str());
    }
  }

  // ---------------------------------------------------------------------
  // Camera - shapes the right stick (gameplay.cpp), so it applies to every
  // camera in the game, plus the runtime's mouse-look settings.
  // ---------------------------------------------------------------------
  void DrawCamera() {
    bool photo = photo_mode::active();
    if (ImGui::Checkbox("Photo mode / free camera (F10)", &photo)) {
      photo_mode::set_active(photo);
    }
    ImGui::TextDisabled("Left stick move, right stick look, RT/LT up/down, "
                        "RB/LB fast/slow. Your character stands still; the "
                        "game keeps running.");
    if (ImGui::TreeNode("Stick diagnostics")) {
      const controller::RawPad m = controller::last_raw_pad();
      ImGui::Text("used:   LX %+.2f LY %+.2f  RX %+.2f RY %+.2f  btn %04X",
                  m.lx, m.ly, m.rx, m.ry, m.buttons);
      for (int slot = 0; slot < 4; ++slot) {
        const controller::RawPad r = controller::raw_pad(slot);
        ImGui::Text("slot %d: LX %+.2f LY %+.2f  RX %+.2f RY %+.2f  btn %04X",
                    slot, r.lx, r.ly, r.rx, r.ry, r.buttons);
      }
      ImGui::TreePop();
    }
    ImGui::Separator();
    gameplay::CameraInput input = gameplay::camera_input();
    bool changed = false;
    changed |= ImGui::Checkbox("Invert horizontal", &input.invert_x);
    changed |= ImGui::Checkbox("Invert vertical", &input.invert_y);
    float sx = static_cast<float>(input.sensitivity_x);
    float sy = static_cast<float>(input.sensitivity_y);
    ImGui::SetNextItemWidth(260);
    const bool sx_moved =
        ImGui::SliderFloat("Horizontal speed", &sx, 0.1f, 3.0f, "%.2fx");
    const bool sx_done = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(260);
    const bool sy_moved =
        ImGui::SliderFloat("Vertical speed", &sy, 0.1f, 3.0f, "%.2fx");
    const bool sy_done = ImGui::IsItemDeactivatedAfterEdit();
    float fov = static_cast<float>(input.fov_degrees);
    float dist = static_cast<float>(input.distance_scale);
    ImGui::SetNextItemWidth(260);
    const bool fov_moved =
        ImGui::SliderFloat("Field of view", &fov, 30.0f, 100.0f, "%.0f deg");
    const bool fov_done = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(260);
    const bool dist_moved =
        ImGui::SliderFloat("Distance", &dist, 0.5f, 2.5f, "%.2fx");
    const bool dist_done = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::TextDisabled("Field of view and distance are experimental: game "
                        "default is 48 deg and 1.00x.");
    if (changed || sx_moved || sy_moved || fov_moved || dist_moved) {
      input.sensitivity_x = sx;
      input.sensitivity_y = sy;
      input.fov_degrees = fov;
      input.distance_scale = dist;
      gameplay::set_camera_input(input);
    }
    if (changed || sx_done || sy_done || fov_done || dist_done) {
      SaveCamera();
    }
    if (ImGui::Button("Reset camera")) {
      gameplay::set_camera_input({});
      SaveCamera();
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Keyboard & mouse");
    const bool mnk = rex::cvar::Query<bool>("mnk_mode");
    bool mouse = rex::cvar::Query<bool>("mnk_mouse");
    ImGui::BeginDisabled(!mnk);
    if (ImGui::Checkbox("Mouse look", &mouse)) {
      rex::cvar::SetFlagByName("mnk_mouse", mouse ? "true" : "false");
      Save({"mnk_mouse"});
    }
    float mouse_speed =
        static_cast<float>(rex::cvar::Query<double>("mnk_sensitivity"));
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderFloat("Mouse speed", &mouse_speed, 0.1f, 5.0f, "%.2f")) {
      rex::cvar::SetFlagByName("mnk_sensitivity",
                               std::to_string(mouse_speed));
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      Save({"mnk_sensitivity"});
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("Mouse look pauses while this menu is open. F9 "
                        "pauses or resumes it in game.");
    if (!mnk) {
      ImGui::TextDisabled("Keyboard & mouse mode is off (started without "
                          "--mnk_mode=true).");
    }
  }

  void SaveCamera() {
    Save({"camera_invert_x", "camera_invert_y", "camera_sensitivity_x",
          "camera_sensitivity_y", "camera_fov", "camera_distance"});
  }

  // ---------------------------------------------------------------------
  // Shader - shader presets and their live settings (shader_settings.h).
  // Sliders reach the shader on the next frame; switching preset needs a
  // restart.
  // ---------------------------------------------------------------------
  void DrawShader() {
    std::vector<std::string> names;
    const std::string& loaded = shader_settings::loaded_preset();
    const std::string selected = shader_settings::selected_preset();
    auto display = [](const std::string& name) {
      return name.empty() ? std::string("Original") : name;
    };
    ImGui::SetNextItemWidth(260);
    if (ImGui::BeginCombo("Preset", display(selected).c_str())) {
      std::vector<std::string> choices = {""};
      for (const auto& name : shader_settings::preset_names()) {
        choices.push_back(name);
      }
      for (const auto& name : choices) {
        if (ImGui::Selectable(display(name).c_str(), name == selected)) {
          shader_settings::select_preset(name);
          names.push_back("shader_preset");
        }
      }
      ImGui::EndCombo();
    }
    if (selected != loaded) {
      ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                         "Running %s. Restart to switch to %s.",
                         display(loaded).c_str(), display(selected).c_str());
      if (ImGui::Button("Restart now")) {
        ImGui::OpenPopup("Restart Tenchu Z?");
      }
      if (ImGui::BeginPopupModal("Restart Tenchu Z?", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Unsaved progress will be lost.");
        if (ImGui::Button("Restart")) {
          ImGui::CloseCurrentPopup();
          Restart();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    }
    if (shader_settings::preset_names().empty()) {
      ImGui::TextWrapped(
          "No presets found. A preset is a folder of .hlsl files in "
          "shader_overrides\\ (or a mod's shaders\\ folder).");
    }
    if (!shader_settings::description().empty()) {
      ImGui::TextWrapped("%s", shader_settings::description().c_str());
    }
    ImGui::Separator();

    bool on = shader_settings::effects_enabled();
    if (ImGui::Checkbox("Effects on", &on)) {
      shader_settings::set_effects_enabled(on);
      names.push_back("shader_effects");
    }
    bool values_done = false;
    const auto& sliders = shader_settings::sliders();
    ImGui::BeginDisabled(!on);
    for (size_t i = 0; i < sliders.size(); ++i) {
      const shader_settings::Slider& s = sliders[i];
      ImGui::PushID(static_cast<int>(i));
      float value = shader_settings::value(i);
      bool changed = false;
      if (s.checkbox) {
        bool b = value != 0.0f;
        if (ImGui::Checkbox(s.label.c_str(), &b)) {
          value = b ? 1.0f : 0.0f;
          changed = values_done = true;
        }
      } else {
        ImGui::SetNextItemWidth(260);
        changed = ImGui::SliderFloat(s.label.c_str(), &value, s.min, s.max,
                                     "%.2f");
        values_done |= ImGui::IsItemDeactivatedAfterEdit();
      }
      if (!s.help.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", s.help.c_str());
      }
      if (changed) {
        shader_settings::set_value(i, value);
      }
      ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (!sliders.empty()) {
      if (ImGui::Button("Reset to defaults")) {
        shader_settings::reset_values();
        values_done = true;
      }
      ImGui::TextDisabled("Hover a setting for a description. Ctrl+click a "
                          "slider to type a value.");
    }
    if (values_done) {
      saved_note_ = shader_settings::SaveValues()
                        ? "Settings saved to shader_settings.ini"
                        : "Could not save settings (see log)";
    }
    if (!names.empty() && !exe_dir_.empty()) {
      saved_note_ = SaveSettings(exe_dir_ / "tenchuz.toml", names)
                        ? "Settings saved to tenchuz.toml"
                        : "Could not save settings (see log)";
    }
  }

  // Starts a new copy of the game with the same command line, then exits.
  void Restart() {
    wchar_t exe[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exe, MAX_PATH)) {
      saved_note_ = "Could not restart; please restart the game yourself";
      return;
    }
    std::wstring command_line = GetCommandLineW();
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process = {};
    const std::wstring dir = exe_dir_.wstring();
    // The new process waits for this one to exit (TenchuzApp::Create).
    SetEnvironmentVariableW(L"TENCHUZ_RESTART_FROM",
                            std::to_wstring(GetCurrentProcessId()).c_str());
    const BOOL started = CreateProcessW(
        exe, command_line.data(), nullptr, nullptr, FALSE, 0, nullptr,
        dir.empty() ? nullptr : dir.c_str(), &startup, &process);
    SetEnvironmentVariableW(L"TENCHUZ_RESTART_FROM", nullptr);
    if (!started) {
      saved_note_ = "Could not restart; please restart the game yourself";
      return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (exit_handler_) {
      exit_handler_();
    }
  }

  // ---------------------------------------------------------------------
  // Online - System Link over IP (net_settings.h). Play with the game's own
  // System Link menu; these only say where to find each other.
  // ---------------------------------------------------------------------
  void DrawOnline() {
    if (!online_loaded_) {
      std::snprintf(bind_ip_buf_, sizeof(bind_ip_buf_), "%s",
                    net_settings::bind_ip().c_str());
      local_addresses_ = net_settings::LocalAddresses();
      running_name_ = net_settings::player_name();
      std::snprintf(name_buf_, sizeof(name_buf_), "%s", running_name_.c_str());
      online_loaded_ = true;
    }
    std::vector<std::string> names;
    const ImVec4 green(0.4f, 1.0f, 0.4f, 1.0f);
    const ImVec4 orange(1.0f, 0.6f, 0.35f, 1.0f);
    const ImVec4 gold(1.0f, 0.85f, 0.3f, 1.0f);
    ImGui::TextWrapped(
        "Co-op uses the game's System Link mode (Mode Select > System Link). "
        "The host gives their join code to friends; friends paste it below "
        "and press Join, then Search in System Link.");
    ImGui::Spacing();

    // --- Player name ------------------------------------------------------
    ImGui::SeparatorText("Your name");
    ImGui::SetNextItemWidth(160);
    ImGui::InputTextWithHint("##name", "name other players see", name_buf_,
                             sizeof(name_buf_));
    const bool name_ok = net_settings::IsValidPlayerName(name_buf_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!name_ok || net_settings::player_name() == name_buf_);
    if (ImGui::Button("Set##name")) {
      net_settings::set_player_name(name_buf_);
      names.push_back("user_name");
    }
    ImGui::EndDisabled();
    if (!name_ok) {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                         "1-15 letters, numbers and spaces.");
    }
    if (net_settings::player_name() != running_name_) {
      ImGui::TextColored(orange, "Restart the game to play as %s.",
                         net_settings::player_name().c_str());
      ImGui::SameLine();
      if (ImGui::SmallButton("Restart now##name")) {
        ImGui::OpenPopup("Restart for new name?");
      }
      if (ImGui::BeginPopupModal("Restart for new name?", nullptr,
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Unsaved progress will be lost.");
        if (ImGui::Button("Restart")) {
          ImGui::CloseCurrentPopup();
          Restart();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
      }
    } else {
      ImGui::TextDisabled("Shown to other System Link players. Your saves "
                          "stay the same whatever the name.");
    }

    // --- Host: this PC's join code ----------------------------------------
    ImGui::SeparatorText("Host a game");
    const upnp::Status router = upnp::status();
    const std::string bind_ip = net_settings::bind_ip();
    // A virtual LAN (or test) address is the one friends reach us at;
    // otherwise the internet address from the router or STUN.
    const std::string code_ip = !bind_ip.empty() ? bind_ip : router.public_ip;
    if (!code_ip.empty()) {
      std::snprintf(code_buf_, sizeof(code_buf_), "%s",
                    net_settings::JoinCode(code_ip).c_str());
      ImGui::TextUnformatted("Your join code:");
      ImGui::SameLine();
      ImGui::PushStyleColor(ImGuiCol_Text, gold);
      ImGui::SetNextItemWidth(110);
      // Read-only text field, so the code can also be selected and copied
      // by hand.
      ImGui::InputText("##code", code_buf_, sizeof(code_buf_),
                       ImGuiInputTextFlags_ReadOnly);
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::Button("Copy")) {
        ImGui::SetClipboardText(code_buf_);
        saved_note_ = "Join code copied - send it to your friends";
      }
      if (!bind_ip.empty()) {
        ImGui::TextDisabled(
            "Code for %s (This PC's address for System Link, under Advanced).",
            bind_ip.c_str());
      } else if (router.ports_open) {
        ImGui::TextColored(green, "Ready: ports open on your router.");
      }
      ImGui::TextDisabled(
          "Send it to the friends you play with (it contains your IP "
          "address). Then host in System Link.");
    } else if (router.state != upnp::State::kWorking) {
      if (ImGui::Button("Get my join code")) {
        upnp::set_enabled(true);
        names.push_back("net_upnp");
        upnp::Start();
      }
      ImGui::TextDisabled(
          "Opens the game's ports on your router (UPnP) and finds your "
          "internet address. Done again automatically each time the game "
          "starts; the ports close when it exits.");
    }
    if (router.state == upnp::State::kWorking) {
      ImGui::TextUnformatted(router.message.c_str());
    } else if (router.state == upnp::State::kFailed && bind_ip.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text, orange);
      ImGui::TextWrapped("%s", router.message.c_str());
      ImGui::PopStyleColor();
      if (ImGui::Button("Try again")) {
        upnp::Start();
      }
    }

    // --- Join: a friend's code --------------------------------------------
    ImGui::SeparatorText("Join a friend");
    ImGui::SetNextItemWidth(160);
    const bool enter = ImGui::InputTextWithHint(
        "##joincode", "friend's join code", join_ip_buf_, sizeof(join_ip_buf_),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ImGui::Button("Paste")) {
      if (const char* text = ImGui::GetClipboardText()) {
        std::snprintf(join_ip_buf_, sizeof(join_ip_buf_), "%s", text);
      }
    }
    std::string resolved;
    const bool join_ok = net_settings::ResolveHosts(join_ip_buf_, &resolved) &&
                         !resolved.empty();
    ImGui::SameLine();
    ImGui::BeginDisabled(!join_ok);
    if (ImGui::Button("Join") || (enter && join_ok)) {
      JoinHost(join_ip_buf_, resolved, &names);
    }
    ImGui::EndDisabled();
    if (join_ip_buf_[0] && !join_ok) {
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                         "Not a valid join code - check for typos.");
    }

    const std::string current = net_settings::join_ip();
    if (!current.empty()) {
      const std::string current_code = net_settings::JoinCode(current);
      ImGui::TextColored(green, "Joining %s.",
                         current_code.empty() ? current.c_str()
                                              : current_code.c_str());
      ImGui::SameLine();
      ImGui::TextUnformatted("Now open System Link and Search.");
      ImGui::SameLine();
      if (ImGui::SmallButton("Stop")) {
        net_settings::set_join_ip("");
        names.push_back("net_join_ip");
      }
    } else {
      ImGui::TextDisabled(
          "No code set: System Link search only finds games on your own "
          "network.");
    }

    const std::vector<net_settings::RecentHost> recent =
        net_settings::recent_hosts();
    if (!recent.empty() &&
        ImGui::BeginTable("recent_hosts", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit)) {
      ImGui::TableSetupColumn("Recent", ImGuiTableColumnFlags_WidthFixed, 160);
      ImGui::TableSetupColumn("Last used", ImGuiTableColumnFlags_WidthFixed, 90);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 170);
      ImGui::TableHeadersRow();
      for (const auto& host : recent) {
        ImGui::PushID(host.host.c_str());
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(host.host.c_str());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(host.date.c_str());
        ImGui::TableNextColumn();
        std::string address;
        if (ImGui::SmallButton("Join") &&
            net_settings::ResolveHosts(host.host, &address)) {
          std::snprintf(join_ip_buf_, sizeof(join_ip_buf_), "%s",
                        host.host.c_str());
          JoinHost(host.host, address, &names);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) {
          ImGui::SetClipboardText(host.host.c_str());
          saved_note_ = "Copied";
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
          net_settings::RemoveRecentHost(host.host);
          names.push_back("net_recent_hosts");
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }

    // --- Advanced: router option, local network, virtual LAN --------------
    ImGui::Spacing();
    if (ImGui::CollapsingHeader("Advanced")) {
      bool upnp_on = upnp::enabled();
      if (ImGui::Checkbox("Open the game's ports on my router at start-up (UPnP)",
                          &upnp_on)) {
        upnp::set_enabled(upnp_on);
        if (upnp_on) {
          upnp::Start();
        } else {
          upnp::Stop();
        }
        names.push_back("net_upnp");
      }
      if (router.state != upnp::State::kOff &&
          router.state != upnp::State::kWorking) {
        ImGui::TextDisabled("Router: %s", router.message.c_str());
      }

      ImGui::TextUnformatted("This PC's local addresses:");
      if (local_addresses_.empty()) {
        ImGui::TextDisabled("  none found");
      }
      for (const auto& address : local_addresses_) {
        ImGui::PushID(address.c_str());
        ImGui::BulletText("%s", address.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy")) {
          ImGui::SetClipboardText(address.c_str());
          saved_note_ = "Address copied";
        }
        ImGui::PopID();
      }

      ImGui::TextUnformatted("This PC's address for System Link");
      ImGui::SetNextItemWidth(200);
      ImGui::InputTextWithHint("##bindip", "empty = automatic", bind_ip_buf_,
                               sizeof(bind_ip_buf_));
      const bool bind_ok = net_settings::IsAddressList(bind_ip_buf_) &&
                           std::string(bind_ip_buf_).find(';') == std::string::npos;
      ImGui::SameLine();
      ImGui::BeginDisabled(!bind_ok);
      if (ImGui::Button("Set##bind")) {
        net_settings::set_bind_ip(bind_ip_buf_);
        names.push_back("net_bind_ip");
      }
      ImGui::EndDisabled();
      ImGui::TextDisabled(
          "Only for virtual LANs (ZeroTier, Radmin VPN, Tailscale...): your "
          "address on that network. Your join code then uses it. Takes effect "
          "the next time you open System Link.");
      ImGui::TextDisabled(
          "Join codes also accept plain IP addresses, several separated by ';'.");
    }

    if (!names.empty() && !exe_dir_.empty()) {
      saved_note_ = SaveSettings(exe_dir_ / "tenchuz.toml", names)
                        ? "Settings saved to tenchuz.toml"
                        : "Could not save settings (see log)";
    }
  }

  // Search the given host from now on, and remember it.
  void JoinHost(const std::string& typed, const std::string& addresses,
                std::vector<std::string>* names) {
    net_settings::set_join_ip(addresses);
    net_settings::AddRecentHost(typed);
    names->push_back("net_join_ip");
    names->push_back("net_recent_hosts");
  }

  void DrawGame() {
    bool skip = gameplay::skip_intro_movies();
    if (ImGui::Checkbox("Skip intro movies (logos and opening)", &skip)) {
      gameplay::set_skip_intro_movies(skip);
      Save({"skip_intro_movies"});
    }
    ImGui::TextDisabled("Takes effect the next time the game starts. Story "
                        "cutscenes still play.");
    bool mute = ui_settings::mute_when_unfocused();
    if (ImGui::Checkbox("Mute when the window is not focused", &mute)) {
      ui_settings::set_mute_when_unfocused(mute);
      Save({"mute_when_unfocused"});
    }
  }

  // ---------------------------------------------------------------------
  // Enemies - characters from the actor table (gameplay::SummarizeEnemies).
  // ---------------------------------------------------------------------
  void DrawEnemies() {
    const gameplay::EnemySummary e = gameplay::SummarizeEnemies(memory_);
    ImGui::Text("Enemies alive: %d", e.alive);
    if (e.have_nearest) {
      ImGui::Text("Nearest: %.0f / %.0f HP, %.0f away", e.nearest.hp,
                  e.nearest.max_hp, e.nearest.distance);
    }
    if (e.have_last_hit) {
      ImGui::Text("Last hit: %.0f / %.0f HP", e.last_hit.hp,
                  e.last_hit.max_hp);
    }
  }

  // ---------------------------------------------------------------------
  // Speedrun - mission timer, splits, bests, practice tools.
  // ---------------------------------------------------------------------
  void DrawSpeedrun() {
    const auto& run = speedrun_;
    if (run.in_mission()) {
      ImGui::Text("Mission M-%02d   %s", run.mission(),
                  Speedrun::Format(run.mission_seconds()).c_str());
      auto best = run.bests().find(run.mission());
      if (best != run.bests().end()) {
        ImGui::SameLine();
        ImGui::TextDisabled("(best %s)",
                            Speedrun::Format(best->second).c_str());
      }
    } else {
      ImGui::TextDisabled("Not in a mission");
    }
    if (run.run_active()) {
      ImGui::Text("Run: %s in game   %s real",
                  Speedrun::Format(run.run_igt_seconds()).c_str(),
                  Speedrun::Format(run.run_real_seconds()).c_str());
    } else {
      ImGui::TextDisabled("No run (starts at the next mission)");
    }
    if (ImGui::Button("Start new run")) {
      speedrun_.StartRun();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset run")) {
      speedrun_.ResetRun();
    }

    if (!run.splits().empty() &&
        ImGui::BeginTable("splits", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 150))) {
      ImGui::TableSetupColumn("Mission");
      ImGui::TableSetupColumn("Time");
      ImGui::TableSetupColumn("");
      ImGui::TableHeadersRow();
      for (const auto& split : run.splits()) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("M-%02d", split.mission);
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(Speedrun::Format(split.seconds).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(split.died ? "died"
                               : split.personal_best ? "PB" : "");
      }
      ImGui::EndTable();
    }
    ImGui::TextDisabled("Missions are timed from when your character appears "
                        "until it leaves, so loads are excluded. Every "
                        "mission is logged to speedrun_log.txt; bests are in "
                        "speedrun_bests.txt.");

    ImGui::Separator();
    ImGui::TextUnformatted("Practice");
    if (ImGui::Button("Save position (F1)")) {
      SavePosition();
    }
    ImGui::SameLine();
    if (ImGui::Button("Load position (F2)")) {
      LoadPosition();
    }
    if (have_saved_position_) {
      ImGui::SameLine();
      ImGui::TextDisabled("%.0f, %.0f, %.0f", saved_position_[0],
                          saved_position_[1], saved_position_[2]);
    }
    ImGui::TextDisabled("Experimental: works within the same area.");
    if (!status_.empty()) {
      ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "%s", status_.c_str());
    }
  }

  // ---------------------------------------------------------------------
  // Controller - remap, deadzones, rumble (controller.h).
  // ---------------------------------------------------------------------
  void DrawController() {
    ImGui::TextUnformatted("Button remapping (game button <- your button)");
    controller::Remap map = controller::remap();
    bool changed = false;
    if (ImGui::BeginTable("remap", 4, ImGuiTableFlags_SizingFixedFit)) {
      for (int c = 0; c < controller::kControlCount; ++c) {
        ImGui::TableNextColumn();
        ImGui::PushID(c);
        ImGui::SetNextItemWidth(70);
        if (ImGui::BeginCombo(controller::ControlName(c),
                              controller::ControlName(map[c]))) {
          for (int src = 0; src < controller::kControlCount; ++src) {
            if (ImGui::Selectable(controller::ControlName(src),
                                  map[c] == src)) {
              map[c] = src;
              changed = true;
            }
          }
          ImGui::EndCombo();
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (ImGui::Button("Default layout")) {
      map = controller::IdentityRemap();
      changed = true;
    }
    if (changed) {
      controller::set_remap(map);
      Save({"button_map"});
    }

    ImGui::Separator();
    float left = static_cast<float>(controller::left_deadzone() * 100.0);
    float right = static_cast<float>(controller::right_deadzone() * 100.0);
    ImGui::SetNextItemWidth(260);
    bool moved = ImGui::SliderFloat("Left stick deadzone", &left, 0.0f,
                                    60.0f, "%.0f%%");
    bool done = ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SetNextItemWidth(260);
    moved |= ImGui::SliderFloat("Right stick deadzone", &right, 0.0f, 60.0f,
                                "%.0f%%");
    done |= ImGui::IsItemDeactivatedAfterEdit();
    if (moved) {
      controller::set_deadzones(left / 100.0, right / 100.0);
    }
    if (ImGui::Button("Default deadzones")) {
      controller::set_deadzones(controller::kGameLeftDeadzone,
                                controller::kGameRightDeadzone);
      done = true;
    }
    if (done) {
      Save({"left_stick_deadzone", "right_stick_deadzone"});
    }
    ImGui::TextDisabled("Game default: 24%% left, 27%% right.");

    ImGui::Separator();
    float rumble = static_cast<float>(controller::rumble_strength() * 100.0);
    ImGui::SetNextItemWidth(260);
    if (ImGui::SliderFloat("Rumble", &rumble, 0.0f, 200.0f, "%.0f%%")) {
      controller::set_rumble_strength(rumble / 100.0);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      Save({"rumble_strength"});
    }
    ImGui::TextDisabled("0%% turns rumble off.");
  }

  // ---------------------------------------------------------------------
  // HUD - the always-on panel (ui_settings.h).
  // ---------------------------------------------------------------------
  void DrawHudSettings() {
    ui_settings::Hud h = ui_settings::hud();
    bool changed = false;
    changed |= ImGui::Checkbox("Health", &h.health);
    changed |= ImGui::Checkbox("Enemies (alive, nearest, last hit)",
                               &h.enemies);
    changed |= ImGui::Checkbox("Speedrun timer", &h.timer);
    changed |= ImGui::Checkbox("Frame rate", &h.fps);
    changed |= ImGui::Checkbox("Position", &h.position);
    const char* corners[] = {"Top left", "Top right", "Bottom left",
                             "Bottom right"};
    ImGui::SetNextItemWidth(160);
    changed |= ImGui::Combo("Corner", &h.corner, corners, 4);
    if (changed) {
      ui_settings::set_hud(h);
      Save({"hud_health", "hud_enemies", "hud_timer", "hud_fps",
            "hud_position", "hud_corner"});
    }
  }

  // Drawn every frame, menu open or not.
  // For the first seconds after start, until the menu is first opened: a
  // note that F6 is where all the port's features are.
  void DrawStartHint() {
    if (visible_) {
      start_hint_done_ = true;
    }
    if (start_hint_done_) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (start_hint_since_ == std::chrono::steady_clock::time_point{}) {
      start_hint_since_ = now;
    }
    if (now - start_hint_since_ > std::chrono::seconds(30)) {
      start_hint_done_ = true;
      return;
    }
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f, viewport->WorkPos.y + 16.0f),
        ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.75f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (ImGui::Begin("##tenchuz_start_hint", nullptr, flags)) {
      // Readable on a TV or a high-resolution window too.
      ImGui::SetWindowFontScale(std::max(1.0f, viewport->WorkSize.y / 540.0f));
      ImGui::TextUnformatted("Press F6 for the Tenchu Z menu");
      ImGui::TextDisabled("Online play, graphics, controls, cheats and more");
    }
    ImGui::End();
  }

  void DrawOverlay() {
    const ui_settings::Hud h = ui_settings::hud();
    if (!(h.health || h.enemies || h.timer || h.fps || h.position)) {
      return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const float pad = 10.0f;
    const bool right = h.corner == 1 || h.corner == 3;
    const bool bottom = h.corner >= 2;
    // The runtime draws its own FPS counter in the top-right corner.
    const float top = right ? 34.0f : pad;
    ImGui::SetNextWindowPos(
        ImVec2(right ? io.DisplaySize.x - pad : pad,
               bottom ? io.DisplaySize.y - pad : top),
        ImGuiCond_Always, ImVec2(right ? 1.0f : 0.0f, bottom ? 1.0f : 0.0f));
    ImGui::SetNextWindowBgAlpha(0.45f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    if (!ImGui::Begin("##tenchuz_overlay", nullptr, flags)) {
      ImGui::End();
      return;
    }
    if (h.health) {
      gameplay::PlayerHealth player;
      if (gameplay::FindLocalPlayer(memory_, &player)) {
        ImGui::Text("HP %.0f / %.0f", player.hp, player.max_hp);
      }
    }
    if (h.enemies) {
      DrawEnemies();
    }
    if (h.timer && speedrun_.run_active()) {
      if (speedrun_.in_mission()) {
        ImGui::Text("M-%02d  %s", speedrun_.mission(),
                    Speedrun::Format(speedrun_.mission_seconds()).c_str());
      }
      ImGui::Text("Run   %s",
                  Speedrun::Format(speedrun_.run_igt_seconds()).c_str());
    }
    if (h.fps) {
      // Game frames per second, from the presented-frame counter.
      const auto now = std::chrono::steady_clock::now();
      const uint64_t frames = gameplay::presented_frames();
      const double dt =
          std::chrono::duration<double>(now - fps_sample_time_).count();
      if (dt >= 0.5) {
        game_fps_ = (frames - fps_sample_frames_) / dt;
        fps_sample_frames_ = frames;
        fps_sample_time_ = now;
      }
      ImGui::Text("Game %.0f fps   Display %.0f fps (%.1f ms)", game_fps_,
                  io.Framerate, 1000.0f / std::max(io.Framerate, 1.0f));
    }
    if (h.position) {
      float xyz[3];
      if (gameplay::GetPlayerPosition(memory_, xyz)) {
        ImGui::Text("Pos %.0f, %.0f, %.0f", xyz[0], xyz[1], xyz[2]);
      }
    }
    ImGui::End();
  }

  void DrawMods() {
    if (mods_.empty()) {
      ImGui::TextUnformatted("No mods loaded.");
    } else {
      ImGui::Text("%zu mod(s) loaded (highest priority first):", mods_.size());
      for (const std::string& mod : mods_) {
        ImGui::BulletText("%s", mod.c_str());
      }
    }
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Put each mod in its own folder in mods\\ next to tenchuz.exe, "
        "mirroring the game folder (mods\\MyMod\\DATA\\...). Mods are read "
        "when the game starts. Start a folder name with _ to disable it.");
    if (!exe_dir_.empty() && ImGui::Button("Open mods folder")) {
      const std::filesystem::path dir = exe_dir_ / "mods";
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      ShellExecuteW(nullptr, L"open", dir.wstring().c_str(), nullptr, nullptr,
                    SW_SHOWNORMAL);
    }
  }

  void DrawTools() {
    if (ImGui::CollapsingHeader("Camera probe (developer)")) {
      ImGui::TextWrapped(
          "Finds the in-mission camera's data. Stand still in a mission, "
          "press Run, and leave the controller alone for about 15 seconds "
          "while the camera turns by itself. The report is written to "
          "camera_probe_<date>_<time>.txt next to tenchuz.exe.");
      if (probe_.running()) {
        if (ImGui::Button("Cancel")) {
          probe_.Cancel();
        }
      } else if (ImGui::Button("Run camera probe")) {
        probe_.Start(exe_dir_ / "camera_probe.txt");
      }
      if (!probe_.status().empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "%s",
                           probe_.status().c_str());
      }
    }
    if (ImGui::CollapsingHeader("Memory scanner (developer)")) {
      if (scanner_.pin_active()) {
        ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1), "PIN ACTIVE at %08X",
                           scanner_.pinned_address());
        ImGui::SameLine();
        if (ImGui::Button("Unpin")) {
          scanner_.Unpin();
          status_ = "pin cleared";
        }
      }
      DrawWatchList();
      ImGui::Separator();
      DrawScanner();
    }
  }

  // ---------------------------------------------------------------------
  // Watch list - the primary interface.
  // ---------------------------------------------------------------------
  void DrawWatchList() {
    ImGui::TextUnformatted("Watch list");
    ImGui::SameLine();
    ImGui::TextDisabled("(live value, no timing needed)");

    ImGui::SetNextItemWidth(120);
    ImGui::InputTextWithHint("##watchaddr", "address (hex)", watch_addr_buf_,
                             sizeof(watch_addr_buf_));
    ImGui::SameLine();
    if (ImGui::Button("Add watch")) {
      uint32_t addr = 0;
      if (ParseHex(watch_addr_buf_, &addr)) {
        scanner_.AddWatch(addr);
        std::snprintf(status_buf_, sizeof(status_buf_), "watching %08X", addr);
        status_ = status_buf_;
      } else {
        status_ = "bad address (try 40001A10)";
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear list")) {
      scanner_.ClearWatches();
      status_ = "watch list cleared";
    }

    const auto& watches = scanner_.watches();
    if (watches.empty()) {
      ImGui::TextDisabled("no addresses watched yet");
      return;
    }

    if (ImGui::BeginTable("watches", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 150))) {
      ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 90);
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 110);
      ImGui::TableSetupColumn("Pin", ImGuiTableColumnFlags_WidthFixed, 60);
      ImGui::TableSetupColumn("Del", ImGuiTableColumnFlags_WidthFixed, 50);
      ImGui::TableHeadersRow();

      for (size_t i = 0; i < watches.size(); ++i) {
        const auto& w = watches[i];
        ImGui::PushID(static_cast<int>(1000 + i));
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        if (w.readable) {
          ImGui::Text("%08X", w.address);
        } else {
          ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%08X", w.address);
        }

        ImGui::TableSetColumnIndex(1);
        if (!w.readable) {
          ImGui::TextDisabled("unmapped");
        } else if (scanner_.type() == ValueType::kFloat) {
          ImGui::Text("%.4f", w.as_float);
        } else {
          ImGui::Text("%u  (0x%X)", w.value, w.value);
        }

        ImGui::TableSetColumnIndex(2);
        if (ImGui::SmallButton("Pin")) {
          scanner_.Pin(w.address, w.value, w.as_float);
          std::snprintf(status_buf_, sizeof(status_buf_), "pinned %08X = %u",
                        w.address, scanner_.pinned_value());
          status_ = status_buf_;
        }

        ImGui::TableSetColumnIndex(3);
        if (ImGui::SmallButton("X")) {
          scanner_.RemoveWatch(i);
          ImGui::PopID();
          break;  // vector shifted under us; redraw next frame
        }

        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::TextDisabled("value type below also controls how watched addresses are read");
  }

  // ---------------------------------------------------------------------
  // Differential scanner - still the way to *find* an address.
  // ---------------------------------------------------------------------
  void DrawScanner() {
    ImGui::TextUnformatted("Scanner");
    ImGui::SameLine();
    ImGui::TextDisabled("(find an address; read the HUD for the target value)");

    if (!scanner_.ready()) {
      ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "guest memory not available");
      return;
    }

    const ValueType types[] = {ValueType::kU32, ValueType::kU16, ValueType::kU8,
                               ValueType::kFloat};
    for (int i = 0; i < 4; ++i) {
      if (i) {
        ImGui::SameLine();
      }
      const bool selected = (scanner_.type() == types[i]);
      if (selected) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.85f, 1));
      }
      if (ImGui::Button(TypeName(types[i]))) {
        scanner_.SetType(types[i]);
        status_.clear();
      }
      if (selected) {
        ImGui::PopStyleColor();
      }
    }

    ImGui::SetNextItemWidth(140);
    ImGui::InputTextWithHint("##exact", "exact value (optional)", exact_buf_,
                             sizeof(exact_buf_));
    ImGui::SameLine();
    if (ImGui::Button("First pass")) {
      DoFirstPass();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
      scanner_.Reset();
      status_ = "reset";
    }

    ImGui::Text("pass: %d    candidates: %zu", scanner_.pass(),
                scanner_.candidate_count());
    ImGui::TextDisabled("scanned %.1f MB   skipped %.1f MB",
                        static_cast<double>(scanner_.scanned_bytes()) / 1048576.0,
                        static_cast<double>(scanner_.skipped_bytes()) / 1048576.0);

    const bool have_candidates = scanner_.candidate_count() > 0;
    ImGui::BeginDisabled(!have_candidates);
    if (ImGui::Button("Decreased")) {
      RefineWith(Filter::kDecreased);
    }
    ImGui::SameLine();
    if (ImGui::Button("Increased")) {
      RefineWith(Filter::kIncreased);
    }
    ImGui::SameLine();
    if (ImGui::Button("Changed")) {
      RefineWith(Filter::kChanged);
    }
    ImGui::SameLine();
    if (ImGui::Button("Unchanged")) {
      RefineWith(Filter::kUnchanged);
    }
    ImGui::EndDisabled();

    // Undo exists because a mis-aimed filter otherwise destroys a good candidate
    // set irrecoverably. This was the single most frustrating failure of the
    // first version of this menu.
    ImGui::SameLine();
    ImGui::BeginDisabled(!scanner_.can_undo());
    if (ImGui::Button("Undo")) {
      if (scanner_.UndoRefine()) {
        std::snprintf(status_buf_, sizeof(status_buf_),
                      "undo -> %zu candidate(s)", scanner_.candidate_count());
        status_ = status_buf_;
      } else {
        status_ = "nothing to undo";
      }
    }
    ImGui::EndDisabled();

    // Push the whole surviving set into the watch list. Once a scan is down to
    // a few hits this is the fastest way to identify the target: the watch list
    // shows every candidate's live value side by side, with no timing window.
    ImGui::SameLine();
    ImGui::BeginDisabled(!have_candidates);
    if (ImGui::Button("Watch all")) {
      const size_t added = scanner_.WatchAllCandidates();
      std::snprintf(status_buf_, sizeof(status_buf_),
                    "watching %zu candidate(s) - take a hit and compare",
                    added);
      status_ = status_buf_;
    }
    ImGui::EndDisabled();

    if (!status_.empty()) {
      ImGui::TextColored(ImVec4(0.6f, 0.9f, 0.6f, 1), "%s", status_.c_str());
    }

    DrawCandidates();
  }

  void DrawCandidates() {
    scanner_.RefreshValues();

    const auto& list = scanner_.candidates();
    if (list.empty()) {
      ImGui::TextUnformatted("no candidates - run a first pass");
      return;
    }

    // Cap what we draw: a first pass can return tens of thousands of hits and
    // rendering them all would stall the UI. Refinement is what reduces the set.
    constexpr size_t kMaxDraw = 400;
    const size_t n = list.size() < kMaxDraw ? list.size() : kMaxDraw;

    ImGui::Text("%zu candidate(s)%s", list.size(),
                list.size() > kMaxDraw ? " (showing first 400)" : "");

    if (ImGui::BeginTable("cands", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, 180))) {
      ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 90);
      ImGui::TableSetupColumn("Value");
      ImGui::TableSetupColumn("Watch", ImGuiTableColumnFlags_WidthFixed, 65);
      ImGui::TableSetupColumn("Pin", ImGuiTableColumnFlags_WidthFixed, 55);
      ImGui::TableHeadersRow();

      for (size_t i = 0; i < n; ++i) {
        const Candidate& c = list[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%08X", c.address);

        ImGui::TableSetColumnIndex(1);
        if (scanner_.type() == ValueType::kFloat) {
          ImGui::Text("%.4f", c.as_float);
        } else {
          ImGui::Text("%u  (0x%X)", c.value, c.value);
        }

        ImGui::TableSetColumnIndex(2);
        if (ImGui::SmallButton("Watch")) {
          scanner_.AddWatch(c.address);
          std::snprintf(status_buf_, sizeof(status_buf_), "watching %08X",
                        c.address);
          status_ = status_buf_;
        }

        ImGui::TableSetColumnIndex(3);
        if (ImGui::SmallButton("Pin")) {
          // Pin the value as currently observed, so the bar stays put.
          scanner_.Pin(c.address, c.value, c.as_float);
          std::snprintf(status_buf_, sizeof(status_buf_), "pinned %08X",
                        c.address);
          status_ = status_buf_;
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
  }

  void DoFirstPass() {
    if (!scanner_.ready()) {
      status_ = "guest memory not available";
      return;
    }
    const bool have_exact = ParseExact();
    status_.clear();

    const bool ok = have_exact
                        ? scanner_.FirstPass(true, exact_value_, exact_float_)
                        : scanner_.FirstPass();
    if (!ok) {
      status_ = "first pass failed: " + scanner_.last_error();
      return;
    }

    std::snprintf(status_buf_, sizeof(status_buf_),
                  "first pass%s: %zu candidate(s)",
                  have_exact ? " (exact)" : "", scanner_.candidate_count());
    status_ = status_buf_;
  }

  bool ParseExact() {
    if (exact_buf_[0] == '\0') {
      return false;
    }
    // strtof/strtoul rather than sscanf: the CRT deprecates sscanf and this
    // avoids pulling in <cstdio> formatting just to parse one scalar.
    char* end = nullptr;
    if (scanner_.type() == ValueType::kFloat) {
      const float f = std::strtof(exact_buf_, &end);
      if (end != exact_buf_ && end && *end == '\0') {
        exact_float_ = f;
        exact_value_ = 0;
        return true;
      }
      return false;
    }
    const unsigned long v = std::strtoul(exact_buf_, &end, 0);  // 0 -> auto base
    if (end != exact_buf_ && end && *end == '\0') {
      exact_value_ = static_cast<uint32_t>(v);
      exact_float_ = static_cast<float>(exact_value_);
      return true;
    }
    return false;
  }

  void RefineWith(Filter f) {
    const size_t before = scanner_.candidate_count();
    if (!scanner_.Refine(f)) {
      status_ = "refine failed: " + scanner_.last_error();
      return;
    }
    // Report what the filter actually did. "0 removed" means the question was
    // answered "no change" - not that the candidates were wrong - which is
    // exactly the confusion the old UI created.
    if (scanner_.last_removed() == 0) {
      std::snprintf(status_buf_, sizeof(status_buf_),
                    "%s: nothing changed (%zu kept) - try a different filter",
                    FilterName(f), before);
    } else {
      std::snprintf(status_buf_, sizeof(status_buf_),
                    "%s: %zu -> %zu candidate(s)", FilterName(f),
                    scanner_.last_before(), scanner_.candidate_count());
    }
    status_ = status_buf_;
  }

  static bool ParseHex(const char* text, uint32_t* out) {
    if (!text || text[0] == '\0') {
      return false;
    }
    char* end = nullptr;
    const unsigned long v = std::strtoul(text, &end, 16);
    if (end == text || !end || *end != '\0') {
      return false;
    }
    *out = static_cast<uint32_t>(v);
    return true;
  }

  rex::memory::Memory* memory_ = nullptr;
  std::filesystem::path exe_dir_;
  std::vector<std::string> mods_;
  std::string saved_note_;
  bool mouse_look_paused_ = false;
  Speedrun speedrun_;
  int money_input_ = 100000;
  bool have_saved_position_ = false;
  float saved_position_[3] = {};
  double game_fps_ = 0;
  uint64_t fps_sample_frames_ = 0;
  std::chrono::steady_clock::time_point fps_sample_time_ =
      std::chrono::steady_clock::now();
  std::function<void()> exit_handler_;
  CameraProbe probe_;
  MemoryScanner scanner_;
  std::function<void()> frame_hook_;
  std::string status_;
  bool visible_ = false;
  char exact_buf_[32] = {0};
  char watch_addr_buf_[32] = {0};
  bool online_loaded_ = false;
  char join_ip_buf_[128] = {0};
  char bind_ip_buf_[32] = {0};
  bool start_hint_done_ = false;
  std::chrono::steady_clock::time_point start_hint_since_{};
  char code_buf_[16] = {0};
  char name_buf_[16] = {0};
  std::string running_name_;
  std::vector<std::string> local_addresses_;
  char status_buf_[160] = {0};
  uint32_t exact_value_ = 0;
  float exact_float_ = 0.0f;
};

}  // namespace tenchuz
