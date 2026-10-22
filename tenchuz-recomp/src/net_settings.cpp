// tenchuz - System Link over IP settings. See net_settings.h.

#include "net_settings.h"

#include <cctype>
#include <cstring>
#include <ctime>

#include <rex/cvar.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

REXCVAR_DEFINE_STRING(net_recent_hosts, "", "Tenchu Z/Online",
                      "Last hosts joined, newest first: host|date;host|date...");

namespace tenchuz::net_settings {
namespace {

constexpr char kCodeAlphabet[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

uint8_t CheckByte(uint32_t ip) {
  return static_cast<uint8_t>(((ip >> 24) * 1 + ((ip >> 16) & 0xFF) * 3 +
                               ((ip >> 8) & 0xFF) * 7 + (ip & 0xFF) * 13 + 0x5A) &
                              0xFF);
}

std::string TrimSpaces(std::string s) {
  s.erase(0, s.find_first_not_of(" \t"));
  s.erase(s.find_last_not_of(" \t") + 1);
  return s;
}

std::vector<std::string> Split(const std::string& value, char separator) {
  std::vector<std::string> parts;
  size_t start = 0;
  while (start < value.size()) {
    size_t end = value.find(separator, start);
    if (end == std::string::npos) end = value.size();
    std::string part = TrimSpaces(value.substr(start, end - start));
    if (!part.empty()) parts.push_back(part);
    start = end + 1;
  }
  return parts;
}

// A join code's address; false if `code` is not a valid code.
bool DecodeJoinCode(const std::string& code, uint32_t* ip) {
  uint64_t bits = 0;
  int digits = 0;
  for (char c : code) {
    if (c == '-' || c == ' ') continue;
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (c == 'O') c = '0';
    if (c == 'I' || c == 'L') c = '1';
    const char* at = std::strchr(kCodeAlphabet, c);
    if (!at || c == 0) return false;
    bits = (bits << 5) | static_cast<uint64_t>(at - kCodeAlphabet);
    ++digits;
  }
  if (digits != 8) return false;
  *ip = static_cast<uint32_t>(bits >> 8);
  return CheckByte(*ip) == (bits & 0xFF);
}

}  // namespace

std::vector<std::string> LocalAddresses() {
  std::vector<std::string> out;
  char host[256] = {};
  if (gethostname(host, sizeof(host)) != 0) {
    return out;
  }
  addrinfo hints = {};
  hints.ai_family = AF_INET;
  addrinfo* list = nullptr;
  if (getaddrinfo(host, nullptr, &hints, &list) != 0) {
    return out;
  }
  for (addrinfo* a = list; a; a = a->ai_next) {
    const auto* in = reinterpret_cast<const sockaddr_in*>(a->ai_addr);
    char text[INET_ADDRSTRLEN] = {};
    if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text)) &&
        (ntohl(in->sin_addr.s_addr) >> 24) != 127) {
      out.push_back(text);
    }
  }
  freeaddrinfo(list);
  return out;
}

std::string join_ip() { return rex::cvar::Query<std::string>("net_join_ip"); }
void set_join_ip(const std::string& value) { rex::cvar::SetFlagByName("net_join_ip", value); }
std::string bind_ip() { return rex::cvar::Query<std::string>("net_bind_ip"); }
void set_bind_ip(const std::string& value) { rex::cvar::SetFlagByName("net_bind_ip", value); }

std::string player_name() { return rex::cvar::Query<std::string>("user_name"); }
void set_player_name(const std::string& name) { rex::cvar::SetFlagByName("user_name", name); }

bool IsValidPlayerName(const std::string& name) {
  if (name.empty() || name.size() > 15 || name.front() == ' ' || name.back() == ' ') {
    return false;
  }
  for (unsigned char c : name) {
    if (!std::isalnum(c) && c != ' ') return false;
  }
  return true;
}

bool IsAddressList(const std::string& value) {
  size_t start = 0;
  while (start < value.size()) {
    size_t end = value.find(';', start);
    if (end == std::string::npos) end = value.size();
    std::string part = value.substr(start, end - start);
    part.erase(0, part.find_first_not_of(' '));
    part.erase(part.find_last_not_of(' ') + 1);
    in_addr a = {};
    if (!part.empty() && inet_pton(AF_INET, part.c_str(), &a) != 1) {
      return false;
    }
    start = end + 1;
  }
  return true;
}

std::string JoinCode(const std::string& ip) {
  in_addr a = {};
  if (inet_pton(AF_INET, TrimSpaces(ip).c_str(), &a) != 1) return "";
  const uint32_t v = ntohl(a.s_addr);
  const uint64_t bits = (static_cast<uint64_t>(v) << 8) | CheckByte(v);
  std::string code;
  for (int i = 7; i >= 0; --i) {
    code += kCodeAlphabet[(bits >> (i * 5)) & 31];
    if (i == 4) code += '-';
  }
  return code;
}

bool ResolveHosts(const std::string& value, std::string* addresses) {
  std::string out;
  for (const std::string& part : Split(value, ';')) {
    in_addr a = {};
    uint32_t ip = 0;
    if (inet_pton(AF_INET, part.c_str(), &a) == 1) {
      ip = ntohl(a.s_addr);
    } else if (!DecodeJoinCode(part, &ip)) {
      return false;
    }
    if (!out.empty()) out += ';';
    out += std::to_string(ip >> 24) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
           std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF);
  }
  *addresses = out;
  return true;
}

std::vector<RecentHost> recent_hosts() {
  std::vector<RecentHost> out;
  for (const std::string& entry : Split(REXCVAR_GET(net_recent_hosts), ';')) {
    const size_t bar = entry.find('|');
    RecentHost host;
    host.host = TrimSpaces(entry.substr(0, bar));
    if (bar != std::string::npos) host.date = TrimSpaces(entry.substr(bar + 1));
    if (!host.host.empty() && out.size() < kMaxRecentHosts) out.push_back(host);
  }
  return out;
}

namespace {
void SetRecentHosts(const std::vector<RecentHost>& hosts) {
  std::string value;
  for (const RecentHost& host : hosts) {
    if (!value.empty()) value += ';';
    value += host.host + "|" + host.date;
  }
  rex::cvar::SetFlagByName("net_recent_hosts", value);
}
}  // namespace

void AddRecentHost(const std::string& host) {
  std::vector<RecentHost> hosts = recent_hosts();
  std::erase_if(hosts, [&](const RecentHost& h) { return h.host == host; });
  const std::time_t now = std::time(nullptr);
  std::tm local = {};
  localtime_s(&local, &now);
  char date[16] = {};
  std::strftime(date, sizeof(date), "%Y-%m-%d", &local);
  hosts.insert(hosts.begin(), RecentHost{host, date});
  if (hosts.size() > kMaxRecentHosts) hosts.resize(kMaxRecentHosts);
  SetRecentHosts(hosts);
}

void RemoveRecentHost(const std::string& host) {
  std::vector<RecentHost> hosts = recent_hosts();
  std::erase_if(hosts, [&](const RecentHost& h) { return h.host == host; });
  SetRecentHosts(hosts);
}

}  // namespace tenchuz::net_settings
