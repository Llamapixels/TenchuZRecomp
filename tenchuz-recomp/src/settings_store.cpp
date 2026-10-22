// tenchuz - saving in-game settings back to tenchuz.toml

#include "settings_store.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace tenchuz {
namespace {

std::string FormatValue(std::string_view name) {
  const rex::cvar::FlagEntry* info = rex::cvar::GetFlagInfo(name);
  std::string value = rex::cvar::GetFlagByName(name);
  if (info && info->type == rex::cvar::FlagType::String) {
    std::string quoted = "\"";
    for (char c : value) {
      if (c == '"' || c == '\\') {
        quoted += '\\';
      }
      quoted += c;
    }
    return quoted + "\"";
  }
  return value;
}

// The key a TOML line assigns at the root, or empty.
std::string_view LineKey(std::string_view line) {
  size_t i = line.find_first_not_of(" \t");
  if (i == std::string_view::npos) {
    return {};
  }
  size_t end = i;
  while (end < line.size() &&
         (std::isalnum(static_cast<unsigned char>(line[end])) ||
          line[end] == '_')) {
    ++end;
  }
  if (end == i) {
    return {};
  }
  size_t eq = line.find_first_not_of(" \t", end);
  if (eq == std::string_view::npos || line[eq] != '=') {
    return {};
  }
  return line.substr(i, end - i);
}

bool IsTableHeader(std::string_view line) {
  size_t i = line.find_first_not_of(" \t");
  return i != std::string_view::npos && line[i] == '[';
}

bool IsComment(std::string_view line) {
  size_t i = line.find_first_not_of(" \t");
  return i != std::string_view::npos && line[i] == '#';
}

}  // namespace

bool SaveSettings(const std::filesystem::path& config_path,
                  const std::vector<std::string>& cvar_names) {
  std::unordered_map<std::string, std::string> pending;
  std::vector<std::string> order;
  for (const std::string& name : cvar_names) {
    if (!rex::cvar::GetFlagInfo(name)) {
      REXLOG_ERROR("settings: unknown setting {}", name);
      continue;
    }
    pending.emplace(std::string(name), FormatValue(name));
    order.push_back(name);
  }

  std::vector<std::string> lines;
  {
    std::ifstream in(config_path, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      lines.push_back(std::move(line));
    }
  }
  // A UTF-8 byte-order mark (written by older launcher versions) would make
  // the first key unrecognisable.
  if (!lines.empty() && lines[0].starts_with("\xEF\xBB\xBF")) {
    lines[0].erase(0, 3);
  }

  std::vector<std::string> out;
  out.reserve(lines.size() + order.size() + 2);
  static const char kHeader[] = "# --- Tenchu Z in-game menu settings ---";
  bool in_root = true;
  size_t insert_at = std::string::npos;
  size_t header_at = std::string::npos;  // our own header, if already written
  for (std::string& line : lines) {
    if (in_root && header_at == std::string::npos && line == kHeader) {
      header_at = out.size();
    }
    if (in_root && IsTableHeader(line)) {
      in_root = false;
      // Insert above the comment block that introduces the table.
      insert_at = out.size();
      while (insert_at > 0 && IsComment(out[insert_at - 1])) {
        --insert_at;
      }
    }
    if (in_root) {
      auto it = pending.find(std::string(LineKey(line)));
      if (it != pending.end()) {
        out.push_back(it->first + " = " + it->second);
        pending.erase(it);
        continue;
      }
    }
    out.push_back(std::move(line));
  }

  std::vector<std::string> missing;
  for (const std::string& name : order) {
    auto it = pending.find(name);
    if (it != pending.end()) {
      missing.push_back(it->first + " = " + it->second);
    }
  }
  if (!missing.empty() && header_at != std::string::npos) {
    // Add to the end of the existing block of menu settings.
    size_t end = header_at + 1;
    while (end < out.size() && !out[end].empty() && !IsTableHeader(out[end])) {
      ++end;
    }
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(end), missing.begin(),
               missing.end());
  } else if (!missing.empty()) {
    missing.insert(missing.begin(), kHeader);
    missing.push_back("");
    if (insert_at == std::string::npos) {
      if (!out.empty() && !out.back().empty()) {
        out.push_back("");
      }
      insert_at = out.size();
    }
    out.insert(out.begin() + static_cast<std::ptrdiff_t>(insert_at),
               missing.begin(), missing.end());
  }

  // Write to a temporary file and swap it in, so a crash mid-write cannot
  // leave a truncated config behind.
  std::filesystem::path temp = config_path;
  temp += ".tmp";
  {
    std::ofstream file(temp, std::ios::binary | std::ios::trunc);
    if (!file) {
      REXLOG_ERROR("settings: cannot write {}", temp.string());
      return false;
    }
    for (const std::string& line : out) {
      file << line << "\r\n";
    }
  }
  std::error_code ec;
  std::filesystem::rename(temp, config_path, ec);
  if (ec) {
    REXLOG_ERROR("settings: cannot replace {}: {}", config_path.string(),
                 ec.message());
    return false;
  }
  return true;
}

}  // namespace tenchuz
