// tenchuz - saving in-game settings back to tenchuz.toml
//
// rex::cvar::SaveConfig (used by the SDK's F4 settings screen) rewrites the
// whole file with only the modified cvars, which deletes the comments, the
// [log.levels] table and anything the launcher wrote. This store instead
// updates just the named keys, in place, using the same rules as the
// launcher: keys live at the TOML root, existing lines are replaced, missing
// keys are inserted above the first [table] header.

#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace tenchuz {

// Writes the current value of each named cvar into `config_path`. Returns
// false (and logs) if the file could not be written.
bool SaveSettings(const std::filesystem::path& config_path,
                  const std::vector<std::string>& cvar_names);
inline bool SaveSettings(const std::filesystem::path& config_path,
                         std::initializer_list<std::string_view> cvar_names) {
  return SaveSettings(config_path,
                      std::vector<std::string>(cvar_names.begin(),
                                               cvar_names.end()));
}

}  // namespace tenchuz
