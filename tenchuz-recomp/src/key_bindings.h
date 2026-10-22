// tenchuz - keyboard and mouse bindings (F6 > Controller)
//
// The runtime's keyboard-and-mouse driver (mnk_mode) turns keys into a
// virtual Xbox controller. Each controller input has a runtime cvar,
// keybind_<input>, holding a comma-separated list of keys: "Q,I",
// "Shift+Up", "LMB". The driver re-reads them every poll, so changes apply
// at once. This file lists them for the menu and captures a key press for
// "Set": the next key or mouse button (with any Shift/Control/Alt held), or
// a modifier pressed and released alone.

#pragma once

#include <string>
#include <vector>

namespace tenchuz::key_bindings {

struct Bind {
  const char* cvar;          // e.g. "keybind_a"
  const char* label;         // e.g. "A"
  const char* default_keys;  // the runtime's default value
};

// Every bindable controller input, in menu order.
const std::vector<Bind>& binds();

std::vector<std::string> keys(const Bind& bind);
void set_keys(const Bind& bind, const std::vector<std::string>& keys);

// Waits for one key or mouse button press (Windows key state, polled every
// frame while the F6 menu is drawn).
class Capture {
 public:
  // Starts listening once every key and button is released, so the click on
  // the menu's Set button is not taken as the new key.
  void Start();
  void Cancel() { state_ = State::kIdle; }
  bool active() const { return state_ != State::kIdle; }
  // While active: true with *key set ("K", "Shift+K", "LMB", "Shift") when a
  // key was taken, or with *key empty when Escape cancelled.
  bool Poll(std::string* key);

 private:
  enum class State { kIdle, kWaitRelease, kListen };
  State state_ = State::kIdle;
  int modifier_down_ = 0;  // a modifier held with nothing else pressed yet
};

}  // namespace tenchuz::key_bindings
