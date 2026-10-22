// tenchuz - loose-file mod loader
//
// See mod_loader.h for the user-facing rules.

#include "mod_loader.h"

#include <algorithm>
#include <cwchar>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem/device.h>
#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/vfs.h>
#include <rex/logging.h>

REXCVAR_DEFINE_BOOL(mods_enabled, true, "Tenchu Z",
                    "Load mods from the mods folder next to tenchuz.exe");

namespace tenchuz {
namespace {

constexpr const char* kOverlayMountPath = "\\Device\\TenchuzMods";
constexpr const char* kLinkedNames[] = {"game:", "d:"};

// One node of the merged tree. Each node stands for the same relative path in
// every layer (mods in priority order, then the game folder) and forwards
// opens to the highest-priority layer that has it.
//
// A whole tree is needed rather than a device that just tries each layer in
// turn: VirtualFileSystem::OpenFile resolves a file's *parent directory* and
// then looks the name up in that directory entry's own children, so the
// directory entries themselves must already contain the mod files.
class MergedEntry : public rex::filesystem::Entry {
 public:
  MergedEntry(rex::filesystem::Device* device, MergedEntry* parent,
              const std::string_view path,
              std::vector<rex::filesystem::Entry*> sources)
      : Entry(device, parent, path), target_(sources.front()) {
    attributes_ = target_->attributes();
    size_ = target_->size();
    allocation_size_ = target_->allocation_size();
    create_timestamp_ = target_->create_timestamp();
    access_timestamp_ = target_->access_timestamp();
    write_timestamp_ = target_->write_timestamp();
    if (attributes_ & rex::filesystem::kFileAttributeDirectory) {
      MergeChildren(sources);
    }
  }

  rex::X_STATUS Open(uint32_t desired_access,
                     rex::filesystem::File** out_file) override {
    return target_->Open(desired_access, out_file);
  }
  bool can_map() const override { return target_->can_map(); }
  std::unique_ptr<rex::memory::MappedMemory> OpenMapped(
      rex::memory::MappedMemory::Mode mode, size_t offset,
      size_t length) override {
    return target_->OpenMapped(mode, offset, length);
  }

 private:
  static std::string Key(std::string_view name) {
    std::string key(name);
    for (char& c : key) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
    }
    return key;
  }

  // Unions the children of every directory source, case-insensitively, with
  // earlier (higher-priority) layers winning. A file in a higher layer hides a
  // directory of the same name below it, and vice versa.
  void MergeChildren(const std::vector<rex::filesystem::Entry*>& sources) {
    std::vector<std::string> order;
    std::unordered_map<std::string, std::vector<rex::filesystem::Entry*>> by_name;
    for (rex::filesystem::Entry* source : sources) {
      if (!(source->attributes() & rex::filesystem::kFileAttributeDirectory)) {
        continue;
      }
      for (const auto& child : source->children()) {
        auto& list = by_name[Key(child->name())];
        if (list.empty()) {
          order.push_back(Key(child->name()));
        }
        const bool child_is_dir =
            child->attributes() & rex::filesystem::kFileAttributeDirectory;
        const bool first_is_dir =
            !list.empty() &&
            (list.front()->attributes() & rex::filesystem::kFileAttributeDirectory);
        if (list.empty() || (child_is_dir && first_is_dir)) {
          list.push_back(child.get());
        }
      }
    }
    for (const std::string& key : order) {
      auto& list = by_name[key];
      const std::string& name = list.front()->name();
      const std::string child_path = path_.empty() ? name : path_ + "\\" + name;
      children_.push_back(
          std::make_unique<MergedEntry>(device_, this, child_path, list));
    }
  }

  rex::filesystem::Entry* target_;
};

class MergedDevice : public rex::filesystem::Device {
 public:
  explicit MergedDevice(
      std::vector<std::unique_ptr<rex::filesystem::Device>> layers)
      : Device(kOverlayMountPath), layers_(std::move(layers)) {}

  bool Initialize() override {
    std::vector<rex::filesystem::Entry*> roots;
    for (auto& layer : layers_) {
      roots.push_back(layer->ResolvePath(""));
    }
    root_ = std::make_unique<MergedEntry>(this, nullptr, "", roots);
    return true;
  }

  void Dump(rex::string::StringBuffer* string_buffer) override {
    root_->Dump(string_buffer, 0);
  }

  rex::filesystem::Entry* ResolvePath(const std::string_view path) override {
    return root_->ResolvePath(path);
  }

  const std::string& name() const override { return base().name(); }
  uint32_t attributes() const override { return base().attributes(); }
  uint32_t component_name_max_length() const override {
    return base().component_name_max_length();
  }
  uint32_t total_allocation_units() const override {
    return base().total_allocation_units();
  }
  uint32_t available_allocation_units() const override {
    return base().available_allocation_units();
  }
  uint32_t sectors_per_allocation_unit() const override {
    return base().sectors_per_allocation_unit();
  }
  uint32_t bytes_per_sector() const override {
    return base().bytes_per_sector();
  }

 private:
  const rex::filesystem::Device& base() const { return *layers_.back(); }

  std::vector<std::unique_ptr<rex::filesystem::Device>> layers_;
  std::unique_ptr<MergedEntry> root_;
};

bool IsEnabledModFolder(const std::filesystem::directory_entry& entry) {
  if (!entry.is_directory()) {
    return false;
  }
  const std::string name = entry.path().filename().string();
  return !name.empty() && name[0] != '_' && name[0] != '.';
}

// Counts the files a mod ships and how many of them replace a game file, so
// the log shows at a glance whether a mod's folder layout is right. The
// mod's shaders\ folder holds shader overrides, not game files; those are
// counted separately.
void CountFiles(const std::filesystem::path& mod_root,
                const std::filesystem::path& game_root, size_t* files,
                size_t* replacing, size_t* shaders) {
  std::error_code ec;
  for (auto it = std::filesystem::recursive_directory_iterator(mod_root, ec);
       !ec && it != std::filesystem::recursive_directory_iterator();
       it.increment(ec)) {
    if (it.depth() == 0 && it->is_directory() &&
        _wcsicmp(it->path().filename().c_str(), L"shaders") == 0) {
      // Includes shader presets (subfolders).
      for (const auto& f :
           std::filesystem::recursive_directory_iterator(it->path(), ec)) {
        if (f.is_regular_file() && f.path().extension() == ".hlsl") {
          ++*shaders;
        }
      }
      it.disable_recursion_pending();
      continue;
    }
    if (!it->is_regular_file()) {
      continue;
    }
    ++*files;
    const auto relative = std::filesystem::relative(it->path(), mod_root, ec);
    if (!ec && std::filesystem::exists(game_root / relative)) {
      ++*replacing;
    }
  }
}

}  // namespace

std::vector<std::string> InstallMods(rex::filesystem::VirtualFileSystem* vfs,
                const std::filesystem::path& game_root,
                const std::filesystem::path& mods_dir) {
  std::error_code ec;
  if (!REXCVAR_GET(mods_enabled)) {
    REXLOG_INFO("mods: disabled by mods_enabled = false");
    return {};
  }
  if (!vfs || game_root.empty() ||
      !std::filesystem::is_directory(mods_dir, ec)) {
    return {};
  }

  std::vector<std::filesystem::path> mod_roots;
  for (const auto& entry : std::filesystem::directory_iterator(mods_dir, ec)) {
    if (IsEnabledModFolder(entry)) {
      mod_roots.push_back(entry.path());
    }
  }
  if (mod_roots.empty()) {
    return {};
  }
  // Alphabetical, then reversed: the last mod in the alphabet is asked first.
  std::sort(mod_roots.begin(), mod_roots.end());
  std::reverse(mod_roots.begin(), mod_roots.end());

  std::vector<std::unique_ptr<rex::filesystem::Device>> layers;
  std::vector<std::string> mounted_names;
  for (const auto& root : mod_roots) {
    auto device = std::make_unique<rex::filesystem::HostPathDevice>(
        kOverlayMountPath, root, /*read_only=*/true);
    if (!device->Initialize()) {
      REXLOG_ERROR("mods: could not read {}, skipping", root.string());
      continue;
    }
    size_t files = 0;
    size_t replacing = 0;
    size_t shaders = 0;
    CountFiles(root, game_root, &files, &replacing, &shaders);
    REXLOG_INFO("mods: {} - {} file(s), {} replacing game files, {} shader(s)",
                root.filename().string(), files, replacing, shaders);
    if (files > 0 && replacing == 0) {
      REXLOG_WARN("mods: {} replaces nothing; its files should mirror the game "
                  "folder (e.g. {}\\DATA\\...)",
                  root.filename().string(), root.filename().string());
    }
    layers.push_back(std::move(device));
    mounted_names.push_back(root.filename().string());
  }
  if (layers.empty()) {
    return {};
  }

  // The merged tree is a snapshot and cannot create files, so it is only used
  // when the game folder is read-only (the default).
  if (REXCVAR_GET(allow_game_relative_writes)) {
    REXLOG_WARN("mods: disabled because allow_game_relative_writes is on");
    return {};
  }
  auto base = std::make_unique<rex::filesystem::HostPathDevice>(
      kOverlayMountPath, game_root, /*read_only=*/true);
  if (!base->Initialize()) {
    REXLOG_ERROR("mods: could not read the game folder; mods disabled");
    return {};
  }
  layers.push_back(std::move(base));

  std::string game_link_target;
  if (!vfs->FindSymbolicLink(kLinkedNames[0], game_link_target)) {
    REXLOG_ERROR("mods: game: is not mounted; mods disabled");
    return {};
  }
  auto merged = std::make_unique<MergedDevice>(std::move(layers));
  merged->Initialize();
  if (!vfs->RegisterDevice(std::move(merged))) {
    REXLOG_ERROR("mods: could not mount the mod overlay; mods disabled");
    return {};
  }
  for (const char* link : kLinkedNames) {
    vfs->UnregisterSymbolicLink(link);
    vfs->RegisterSymbolicLink(link, kOverlayMountPath);
  }
  REXLOG_INFO("mods: {} mod(s) active (game:, d: now resolve through {})",
              mounted_names.size(), kOverlayMountPath);
  return mounted_names;
}

}  // namespace tenchuz
