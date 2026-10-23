// tenchuz - play single-player levels in multiplayer (F6 > Online)
//
// The multiplayer lobby keeps the chosen level in a heap block whose address
// changes every run. Layout (big-endian u32s), from a community Cheat Engine
// table for Xenia and checked against it:
//
//   +0x00  level id (single-player missions are 5..49, see kLevels)
//   +0x04  small value
//   +0x08  small value (0x32 in the lobby the table was made in)
//   +0x0C  small value
//   +0x10  0xFFFFFFFF
//   +0x14  0
//   +0xA9  "cam_riki" (the table: "LEVEL id -A6 from cam_riki", -A6 from the
//          id's low byte at +0x03)
//
// The block starts 4 bytes into a 16-byte line (the table's byte address
// always ends in 7). A background thread finds it, then keeps the chosen level
// written there while the option is on.

#pragma once

#include <cstdint>
#include <string>

namespace rex::memory {
class Memory;
}

namespace tenchuz::mp_level {

struct Level {
  uint32_t id;
  const char* name;
};

// Single-player missions, ids from the cheat table.
inline constexpr Level kLevels[] = {
    {5, "Punish The Greedy Merchant"},
    {6, "Steal The Matchlock Gun Designs"},
    {12, "Steal The Ronin List"},
    {13, "Punish The Evil Merchant"},
    {14, "Infiltrate The Inner Mansion"},
    {15, "Pursuit Of Echigoya"},
    {16, "Daylight Battle On Deck"},
    {23, "Expose The False Landlady"},
    {24, "Assassination Of Vassal"},
    {33, "Kill The Agent"},
    {35, "Punish The Extremist"},
    {42, "Punish The Corrupt Administration"},
    {43, "Assassination Of Vassal (2)"},
    {49, "Divine Punishment Tenchu"},
};

void SetMemory(rex::memory::Memory* memory);

// While enabled, the chosen level is written into the lobby block (found and
// re-found in the background).
void SetEnabled(bool enabled);
bool enabled();
void SetLevel(uint32_t id);
uint32_t level();

// What the finder last saw, for the menu.
struct Status {
  uint32_t address = 0;       // guest address of the block, 0 = not found
  uint32_t lobby_level = 0;   // level id the lobby held before we wrote ours
  uint32_t current_level = 0; // level id there now
  bool searching = false;
  int candidates = 0;         // blocks matching the layout in the last search
};
Status GetStatus();

}  // namespace tenchuz::mp_level
