// tenchuz - System Link over IP settings (F6 > Online)
//
// The patched runtime plays System Link over ordinary IP (SDK-VERSION.md):
//   net_join_ip  host address(es) the System Link search is sent to
//                (join by IP); empty = broadcast to the local network
//   net_bind_ip  this PC's address for System Link; empty = automatic
// Both are runtime cvars saved in tenchuz.toml. net_join_ip applies to the
// next search; net_bind_ip to the next time System Link is opened.
//
// Join codes: a host's internet address as 8 letters/digits ("K3F9-2QX7"),
// easier to read out or paste than an IP. Crockford base32 of the address
// plus a check byte, so typos are caught. Anyone with the code has the IP.

#pragma once

#include <string>
#include <vector>

namespace tenchuz::net_settings {

// This PC's IPv4 addresses (all adapters, loopback excluded), for telling a
// friend which address to join.
std::vector<std::string> LocalAddresses();

std::string join_ip();
void set_join_ip(const std::string& value);
std::string bind_ip();
void set_bind_ip(const std::string& value);

// The player name other players see (runtime cvar user_name, saved in
// tenchuz.toml; used from the next start). Saves do not depend on it.
std::string player_name();
void set_player_name(const std::string& name);
// 1-15 letters, digits and spaces, like an Xbox gamertag.
bool IsValidPlayerName(const std::string& name);

// True if `value` is empty or a list of IPv4 addresses separated by ';'.
bool IsAddressList(const std::string& value);

// The join code for an IPv4 address ("" if `ip` is not one).
std::string JoinCode(const std::string& ip);
// Turns a ';' list of addresses and/or join codes into addresses. False if a
// part is neither.
bool ResolveHosts(const std::string& value, std::string* addresses);

// The last hosts joined (net_recent_hosts, saved in tenchuz.toml), newest
// first, at most kMaxRecentHosts.
struct RecentHost {
  std::string host;  // join code or address, as typed
  std::string date;  // YYYY-MM-DD
};
constexpr size_t kMaxRecentHosts = 5;
std::vector<RecentHost> recent_hosts();
void AddRecentHost(const std::string& host);
void RemoveRecentHost(const std::string& host);

}  // namespace tenchuz::net_settings
