// tenchuz - loose-file mod loader
//
// Mods live in <exe_dir>\mods\<ModName>\ and mirror the game folder, e.g.
//
//   mods\HD Title\DATA\TEXTURE\ENGLISH\title.xpr
//
// replaces extracted\DATA\TEXTURE\ENGLISH\title.xpr without touching it.
//
// How it works: the runtime mounts the game folder as
// \Device\Harddisk0\Partition1 and points the game:\ and d:\ links at it
// (Runtime::SetupVfs). Before the game starts, this loader mounts a device
// whose directory tree merges every mod with the game folder, and repoints the
// links at it. The original device stays registered, so nothing that already
// holds one of its entries is invalidated.
//
// Rules:
//   * Mods are applied in alphabetical order of folder name; when two mods
//     replace the same file the later one wins. Prefix folder names with
//     numbers (10_, 20_) to control the order.
//   * A folder whose name starts with '_' or '.' is ignored, so renaming a mod
//     to _ModName disables it.
//   * Mods can replace files and add new ones. The file list is read once at
//     startup, so mods are picked up when the game is launched.

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rex::filesystem {
class VirtualFileSystem;
}

namespace tenchuz {

// Installs the overlay when at least one enabled mod is present. Returns the
// names of the mods mounted, highest priority first; an empty list leaves the
// file system exactly as the runtime set it up.
std::vector<std::string> InstallMods(rex::filesystem::VirtualFileSystem* vfs,
                const std::filesystem::path& game_root,
                const std::filesystem::path& mods_dir);

}  // namespace tenchuz
