// tenchuz - keyboard and mouse bindings. See key_bindings.h.

#include "key_bindings.h"

#include <rex/cvar.h>
#include <rex/ui/keybinds.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace tenchuz::key_bindings {

const std::vector<Bind>& binds() {
  // Defaults as defined by the runtime (src/input/mnk/mnk_input_driver.cpp).
  static const std::vector<Bind> list = {
      {"keybind_a", "A", "Semicolon,Space"},
      {"keybind_b", "B", "Quote,Backspace"},
      {"keybind_x", "X", "L"},
      {"keybind_y", "Y", "P"},
      {"keybind_left_shoulder", "LB", "1"},
      {"keybind_right_shoulder", "RB", "3"},
      {"keybind_left_trigger", "LT", "Q,I"},
      {"keybind_right_trigger", "RT", "E,O"},
      {"keybind_start", "Start", "X,Return"},
      {"keybind_back", "Back", "Z,Tab"},
      {"keybind_lstick_up", "Move up", "W"},
      {"keybind_lstick_down", "Move down", "S"},
      {"keybind_lstick_left", "Move left", "A"},
      {"keybind_lstick_right", "Move right", "D"},
      {"keybind_lstick_press", "Left stick press", "F"},
      {"keybind_rstick_up", "Camera up", "Up"},
      {"keybind_rstick_down", "Camera down", "Down"},
      {"keybind_rstick_left", "Camera left", "Left"},
      {"keybind_rstick_right", "Camera right", "Right"},
      {"keybind_rstick_press", "Right stick press", "K"},
      {"keybind_dpad_up", "D-pad up", "Shift+Up"},
      {"keybind_dpad_down", "D-pad down", "Shift+Down"},
      {"keybind_dpad_left", "D-pad left", "Shift+Left"},
      {"keybind_dpad_right", "D-pad right", "Shift+Right"},
  };
  return list;
}

std::vector<std::string> keys(const Bind& bind) {
  std::vector<std::string> out;
  const std::string value = rex::cvar::Query<std::string>(bind.cvar);
  size_t start = 0;
  while (start < value.size()) {
    size_t end = value.find(',', start);
    if (end == std::string::npos) end = value.size();
    std::string key = value.substr(start, end - start);
    key.erase(0, key.find_first_not_of(' '));
    key.erase(key.find_last_not_of(' ') + 1);
    if (!key.empty()) out.push_back(key);
    start = end + 1;
  }
  return out;
}

void set_keys(const Bind& bind, const std::vector<std::string>& keys) {
  std::string value;
  for (const std::string& key : keys) {
    if (!value.empty()) value += ',';
    value += key;
  }
  rex::cvar::SetFlagByName(bind.cvar, value);
}

namespace {

bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

bool IsModifier(int vk) { return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU; }

// Keys the capture looks at: everything the runtime has a name for, except
// the left/right copies of the modifiers (VK_SHIFT etc. cover them).
bool Watched(int vk) {
  if (vk >= VK_LSHIFT && vk <= VK_RMENU) return false;
  return !rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk)).empty();
}

std::string Name(int vk) {
  // Enter and numpad Enter share a key code; the runtime's name list gives
  // "NumpadEnter" for both.
  if (vk == VK_RETURN) return "Return";
  return rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk));
}

}  // namespace

void Capture::Start() {
  state_ = State::kWaitRelease;
  modifier_down_ = 0;
}

bool Capture::Poll(std::string* key) {
  if (state_ == State::kWaitRelease) {
    for (int vk = 1; vk < 255; ++vk) {
      if (Watched(vk) && Down(vk)) return false;
    }
    state_ = State::kListen;
    return false;
  }
  if (state_ != State::kListen) return false;

  if (Down(VK_ESCAPE)) {
    key->clear();
    state_ = State::kIdle;
    return true;
  }
  for (int vk = 1; vk < 255; ++vk) {
    // F6 opens and closes this menu; Escape cancels.
    if (!Watched(vk) || IsModifier(vk) || vk == VK_F6 || vk == VK_ESCAPE || !Down(vk)) {
      continue;
    }
    std::string chord;
    if (Down(VK_SHIFT)) chord += "Shift+";
    if (Down(VK_CONTROL)) chord += "Control+";
    if (Down(VK_MENU)) chord += "Alt+";
    *key = chord + Name(vk);
    state_ = State::kIdle;
    return true;
  }
  // A modifier on its own: taken when it is released with nothing else
  // pressed in between.
  if (modifier_down_ != 0) {
    if (!Down(modifier_down_)) {
      *key = Name(modifier_down_);
      state_ = State::kIdle;
      return true;
    }
  } else {
    for (int vk : {VK_SHIFT, VK_CONTROL, VK_MENU}) {
      if (Down(vk)) {
        modifier_down_ = vk;
        break;
      }
    }
  }
  return false;
}

}  // namespace tenchuz::key_bindings
