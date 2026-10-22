// tenchuz - open the System Link ports on the home router (UPnP). See upnp.h.
//
// The protocol, in short: a multicast "M-SEARCH" (SSDP) finds the router,
// which answers with the address of its device description (XML). That lists
// a WANIPConnection (or WANPPPConnection) service with a control URL, which
// takes SOAP requests: GetExternalIPAddress, AddPortMapping, ...

#include "upnp.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <mutex>
#include <thread>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

REXCVAR_DEFINE_BOOL(net_upnp, false, "Tenchu Z/Online",
                    "Open the System Link ports on the router (UPnP) so players on other "
                    "networks can join");

namespace tenchuz::upnp {
namespace {

struct Port {
  uint16_t number;
  const char* protocol;
};
constexpr Port kPorts[] = {{1000, "UDP"}, {1001, "UDP"}, {1003, "TCP"}, {1004, "UDP"}};
constexpr char kDescription[] = "Tenchu Z System Link";
constexpr int kTimeoutMs = 3000;

struct Url {
  std::string host;
  uint16_t port = 80;
  std::string path = "/";
};

// The router's port forwarding service.
struct Router {
  Url control;
  std::string service;  // service type, e.g. urn:schemas-upnp-org:service:WANIPConnection:1
};

std::mutex g_mutex;
Status g_status;
std::thread g_worker;
Router g_router;                  // valid while g_opened is not empty
std::vector<const Port*> g_opened;  // ports this run forwarded
bool g_set_public_ip = false;

void SetStatus(State state, const std::string& message) {
  std::lock_guard lock(g_mutex);
  g_status.state = state;
  g_status.message = message;
  if (state == State::kFailed) {
    REXLOG_WARN("UPnP: {}", message);
  } else {
    REXLOG_INFO("UPnP: {}", message);
  }
}

class Socket {
 public:
  explicit Socket(int type) : s_(::socket(AF_INET, type, 0)) {}
  ~Socket() {
    if (s_ != INVALID_SOCKET) closesocket(s_);
  }
  Socket(const Socket&) = delete;
  Socket& operator=(const Socket&) = delete;
  SOCKET get() const { return s_; }
  bool ok() const { return s_ != INVALID_SOCKET; }

 private:
  SOCKET s_;
};

std::string Lower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return s;
}

std::string Trim(const std::string& s) {
  const size_t a = s.find_first_not_of(" \t\r\n");
  if (a == std::string::npos) return "";
  return s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}

std::string IpText(const in_addr& a) {
  char text[INET_ADDRSTRLEN] = {};
  inet_ntop(AF_INET, &a, text, sizeof(text));
  return text;
}

// Value of an HTTP header in `head` (name in lower case), or "".
std::string Header(const std::string& head, const std::string& name) {
  const std::string lower = Lower(head);
  size_t pos = 0;
  while ((pos = lower.find(name + ":", pos)) != std::string::npos) {
    if (pos == 0 || lower[pos - 1] == '\n') {
      const size_t start = pos + name.size() + 1;
      return Trim(head.substr(start, head.find('\r', start) - start));
    }
    pos += name.size();
  }
  return "";
}

// Text of the first <name> element at or after `from` (any namespace
// prefix), or "".
std::string Tag(const std::string& xml, const std::string& name, size_t from = 0) {
  size_t pos = from;
  while ((pos = xml.find(name + ">", pos)) != std::string::npos) {
    const size_t lt = xml.rfind('<', pos);
    if (lt != std::string::npos && xml[lt + 1] != '/' &&
        xml.find_first_of(" >", lt) >= pos) {
      const size_t start = pos + name.size() + 1;
      const size_t end = xml.find("</", start);
      if (end == std::string::npos) return "";
      return Trim(xml.substr(start, end - start));
    }
    pos += name.size();
  }
  return "";
}

bool ParseUrl(const std::string& text, Url* url) {
  if (Lower(text.substr(0, 7)) != "http://") return false;
  const std::string rest = text.substr(7);
  const size_t slash = rest.find('/');
  const std::string host_port = rest.substr(0, slash);
  url->path = slash == std::string::npos ? "/" : rest.substr(slash);
  const size_t colon = host_port.find(':');
  url->host = host_port.substr(0, colon);
  url->port = colon == std::string::npos
                  ? 80
                  : static_cast<uint16_t>(std::atoi(host_port.c_str() + colon + 1));
  return !url->host.empty() && url->port != 0;
}

// The address of this PC on the network that leads to the internet (the one
// the router sees). No packet is sent.
std::string DefaultRouteIp() {
  Socket s(SOCK_DGRAM);
  sockaddr_in remote = {};
  remote.sin_family = AF_INET;
  remote.sin_port = htons(53);
  remote.sin_addr.s_addr = htonl(0x08080808);
  sockaddr_in local = {};
  int length = sizeof(local);
  if (!s.ok() || connect(s.get(), reinterpret_cast<sockaddr*>(&remote), sizeof(remote)) != 0 ||
      getsockname(s.get(), reinterpret_cast<sockaddr*>(&local), &length) != 0) {
    return "";
  }
  return IpText(local.sin_addr);
}

bool ConnectWithTimeout(SOCKET s, const sockaddr_in& to) {
  u_long nonblocking = 1;
  ioctlsocket(s, FIONBIO, &nonblocking);
  connect(s, reinterpret_cast<const sockaddr*>(&to), sizeof(to));
  fd_set writable, failed;
  FD_ZERO(&writable);
  FD_ZERO(&failed);
  FD_SET(s, &writable);
  FD_SET(s, &failed);
  timeval timeout = {kTimeoutMs / 1000, 0};
  const bool connected = select(0, nullptr, &writable, &failed, &timeout) == 1 &&
                         FD_ISSET(s, &writable);
  nonblocking = 0;
  ioctlsocket(s, FIONBIO, &nonblocking);
  return connected;
}

std::string Dechunk(const std::string& body) {
  std::string out;
  size_t pos = 0;
  while (pos < body.size()) {
    const size_t line_end = body.find("\r\n", pos);
    if (line_end == std::string::npos) break;
    const size_t size = std::strtoul(body.substr(pos, line_end - pos).c_str(), nullptr, 16);
    if (size == 0) break;
    out += body.substr(line_end + 2, size);
    pos = line_end + 2 + size + 2;
  }
  return out;
}

// One HTTP request. Returns the status code (0 = no answer) and fills the
// body and this PC's address on the connection.
int Http(const Url& url, const std::string& method, const std::string& headers,
         const std::string& body, std::string* response, std::string* local_ip = nullptr) {
  sockaddr_in to = {};
  to.sin_family = AF_INET;
  to.sin_port = htons(url.port);
  if (inet_pton(AF_INET, url.host.c_str(), &to.sin_addr) != 1) {
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    addrinfo* list = nullptr;
    if (getaddrinfo(url.host.c_str(), nullptr, &hints, &list) != 0 || !list) return 0;
    to.sin_addr = reinterpret_cast<sockaddr_in*>(list->ai_addr)->sin_addr;
    freeaddrinfo(list);
  }
  Socket s(SOCK_STREAM);
  if (!s.ok() || !ConnectWithTimeout(s.get(), to)) return 0;
  const DWORD timeout = kTimeoutMs;
  setsockopt(s.get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));
  setsockopt(s.get(), SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout),
             sizeof(timeout));
  if (local_ip) {
    sockaddr_in local = {};
    int length = sizeof(local);
    getsockname(s.get(), reinterpret_cast<sockaddr*>(&local), &length);
    *local_ip = IpText(local.sin_addr);
  }

  std::string request = method + " " + url.path + " HTTP/1.1\r\nHost: " + url.host + ":" +
                        std::to_string(url.port) + "\r\nConnection: close\r\n" + headers;
  if (method == "POST") {
    request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  request += "\r\n" + body;
  if (send(s.get(), request.data(), static_cast<int>(request.size()), 0) !=
      static_cast<int>(request.size())) {
    return 0;
  }

  std::string reply;
  char buffer[4096];
  int got;
  while ((got = recv(s.get(), buffer, sizeof(buffer), 0)) > 0 && reply.size() < (1 << 20)) {
    reply.append(buffer, got);
  }
  const size_t split = reply.find("\r\n\r\n");
  if (reply.rfind("HTTP/", 0) != 0 || split == std::string::npos) return 0;
  const std::string head = reply.substr(0, split);
  *response = reply.substr(split + 4);
  if (Lower(Header(head, "transfer-encoding")) == "chunked") {
    *response = Dechunk(*response);
  }
  const size_t space = head.find(' ');
  return space == std::string::npos ? 0 : std::atoi(head.c_str() + space + 1);
}

// A SOAP action on the router. Returns 0 on success, else the UPnP error code
// (or -1 if the router did not answer properly).
int Soap(const Router& router, const std::string& action, const std::string& args,
         std::string* response) {
  const std::string body =
      "<?xml version=\"1.0\"?><s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
      "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\"><s:Body><u:" +
      action + " xmlns:u=\"" + router.service + "\">" + args + "</u:" + action +
      "></s:Body></s:Envelope>";
  const std::string headers = "Content-Type: text/xml; charset=\"utf-8\"\r\nSOAPAction: \"" +
                              router.service + "#" + action + "\"\r\n";
  const int code = Http(router.control, "POST", headers, body, response);
  if (code == 200) return 0;
  const std::string error = Tag(*response, "errorCode");
  return error.empty() ? -1 : std::atoi(error.c_str());
}

std::string SoapError(int code) {
  switch (code) {
    case -1: return "no proper answer";
    case 606: return "error 606: the router does not allow changes (UPnP is read-only)";
    case 718: return "error 718: already forwarded to another device";
    case 725: return "error 725: the router only takes permanent forwards";
    default: return "error " + std::to_string(code);
  }
}

// Asks routers on the network where their device descriptions are.
std::vector<std::string> Discover(const std::string& local_ip) {
  std::vector<std::string> locations;
  Socket s(SOCK_DGRAM);
  if (!s.ok()) return locations;
  sockaddr_in local = {};
  local.sin_family = AF_INET;
  inet_pton(AF_INET, local_ip.c_str(), &local.sin_addr);
  bind(s.get(), reinterpret_cast<sockaddr*>(&local), sizeof(local));
  setsockopt(s.get(), IPPROTO_IP, IP_MULTICAST_IF, reinterpret_cast<const char*>(&local.sin_addr),
             sizeof(local.sin_addr));
  const DWORD ttl = 2;
  setsockopt(s.get(), IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl),
             sizeof(ttl));

  sockaddr_in group = {};
  group.sin_family = AF_INET;
  group.sin_port = htons(1900);
  inet_pton(AF_INET, "239.255.255.250", &group.sin_addr);
  for (const char* target : {"urn:schemas-upnp-org:device:InternetGatewayDevice:1",
                             "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
                             "urn:schemas-upnp-org:service:WANIPConnection:1",
                             "urn:schemas-upnp-org:service:WANPPPConnection:1"}) {
    const std::string search = std::string(
                                   "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\n"
                                   "MAN: \"ssdp:discover\"\r\nMX: 2\r\nST: ") +
                               target + "\r\n\r\n";
    for (int i = 0; i < 2; ++i) {
      sendto(s.get(), search.data(), static_cast<int>(search.size()), 0,
             reinterpret_cast<sockaddr*>(&group), sizeof(group));
    }
  }

  // Collect answers for up to 3 s, or 0.5 s after the first.
  using Clock = std::chrono::steady_clock;
  auto deadline = Clock::now() + std::chrono::seconds(3);
  while (Clock::now() < deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::microseconds>(deadline - Clock::now()).count();
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(s.get(), &readable);
    timeval timeout = {static_cast<long>(left / 1000000), static_cast<long>(left % 1000000)};
    if (select(0, &readable, nullptr, nullptr, &timeout) != 1) break;
    char buffer[2048];
    const int got = recv(s.get(), buffer, sizeof(buffer) - 1, 0);
    if (got <= 0) continue;
    const std::string location = Header(std::string(buffer, got), "location");
    if (!location.empty() &&
        std::find(locations.begin(), locations.end(), location) == locations.end()) {
      locations.push_back(location);
      if (locations.size() == 1) {
        deadline = std::min(deadline, Clock::now() + std::chrono::milliseconds(500));
      }
    }
  }
  return locations;
}

// Reads a device description and finds its port forwarding service.
bool FindService(const std::string& location, Router* router) {
  Url url;
  std::string xml;
  if (!ParseUrl(location, &url) || Http(url, "GET", "", "", &xml) != 200) return false;
  Url base = url;
  const std::string url_base = Tag(xml, "URLBase");
  if (!url_base.empty()) ParseUrl(url_base, &base);

  // Prefer an IP connection over a PPP one.
  for (const char* wanted : {"WANIPConnection", "WANPPPConnection"}) {
    size_t pos = 0;
    while ((pos = xml.find("<service>", pos)) != std::string::npos) {
      const size_t end = xml.find("</service>", pos);
      const std::string block = xml.substr(pos, end - pos);
      pos += 9;
      const std::string type = Tag(block, "serviceType");
      const std::string control = Tag(block, "controlURL");
      if (type.find(wanted) == std::string::npos || control.empty()) continue;
      router->service = type;
      if (!ParseUrl(control, &router->control)) {
        router->control = base;
        router->control.path = control[0] == '/' ? control : "/" + control;
      }
      return true;
    }
  }
  return false;
}

std::string MappingArgs(const Port& port) {
  return "<NewRemoteHost></NewRemoteHost><NewExternalPort>" + std::to_string(port.number) +
         "</NewExternalPort><NewProtocol>" + port.protocol + "</NewProtocol>";
}

// Forwards one port to this PC. Returns 0 or the UPnP error code.
int OpenPort(const Router& router, const Port& port, const std::string& local_ip) {
  std::string response;
  int error = 0;
  // Permanent first; some newer routers only take forwards that expire.
  for (const char* lease : {"0", "604800"}) {
    error = Soap(router, "AddPortMapping",
                 MappingArgs(port) + "<NewInternalPort>" + std::to_string(port.number) +
                     "</NewInternalPort><NewInternalClient>" + local_ip +
                     "</NewInternalClient><NewEnabled>1</NewEnabled>"
                     "<NewPortMappingDescription>" + kDescription +
                     "</NewPortMappingDescription><NewLeaseDuration>" + lease +
                     "</NewLeaseDuration>",
                 &response);
    if (error == 0 || error == 718 || error == 606) break;
  }
  if (error == 718) {
    // Some routers refuse to replace a forward even when it is already ours.
    if (Soap(router, "GetSpecificPortMappingEntry", MappingArgs(port), &response) == 0 &&
        Tag(response, "NewInternalClient") == local_ip) {
      error = 0;
    }
  }
  return error;
}

bool IsPrivate(const std::string& ip) {
  in_addr a = {};
  if (inet_pton(AF_INET, ip.c_str(), &a) != 1) return true;
  const uint32_t v = ntohl(a.s_addr);
  return v == 0 || (v >> 24) == 10 || (v >> 24) == 127 || (v >> 20) == 0xAC1 ||
         (v >> 16) == 0xC0A8 || (v >> 22) == 0x191 || (v >> 16) == 0xA9FE;
}

// Asks a public STUN server which address this PC's traffic comes from
// (RFC 5389 binding request). "" if none answers.
std::string StunPublicIp() {
  for (const auto& [host, port] :
       {std::pair{"stun.l.google.com", "19302"}, std::pair{"stun.cloudflare.com", "3478"}}) {
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* list = nullptr;
    if (getaddrinfo(host, port, &hints, &list) != 0 || !list) continue;
    sockaddr_in server = *reinterpret_cast<sockaddr_in*>(list->ai_addr);
    freeaddrinfo(list);

    Socket s(SOCK_DGRAM);
    const DWORD timeout = 1500;
    setsockopt(s.get(), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout),
               sizeof(timeout));
    uint8_t request[20] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
    for (int i = 8; i < 20; ++i) request[i] = static_cast<uint8_t>(std::rand());
    for (int attempt = 0; attempt < 2; ++attempt) {
      sendto(s.get(), reinterpret_cast<const char*>(request), sizeof(request), 0,
             reinterpret_cast<sockaddr*>(&server), sizeof(server));
      uint8_t reply[512];
      const int got = recv(s.get(), reinterpret_cast<char*>(reply), sizeof(reply), 0);
      if (got < 20 || reply[0] != 0x01 || reply[1] != 0x01 ||
          std::memcmp(reply + 8, request + 8, 12) != 0) {
        continue;
      }
      // Attributes: XOR-MAPPED-ADDRESS (0x0020) or MAPPED-ADDRESS (0x0001).
      for (int pos = 20; pos + 4 <= got;) {
        const int type = (reply[pos] << 8) | reply[pos + 1];
        const int length = (reply[pos + 2] << 8) | reply[pos + 3];
        const uint8_t* value = reply + pos + 4;
        if ((type == 0x0020 || type == 0x0001) && length >= 8 && pos + 4 + length <= got &&
            value[1] == 0x01) {
          uint32_t ip = (uint32_t(value[4]) << 24) | (uint32_t(value[5]) << 16) |
                        (uint32_t(value[6]) << 8) | value[7];
          if (type == 0x0020) ip ^= 0x2112A442;
          in_addr a;
          a.s_addr = htonl(ip);
          return IpText(a);
        }
        pos += 4 + ((length + 3) & ~3);
      }
    }
  }
  return "";
}

// Forwards the ports to `local_ip`. Returns "" on success, else what went
// wrong; fills the router's own internet address if it gave one.
std::string OpenRouterPorts(const std::string& local_ip, std::string* router_ip) {
  Router router;
  bool found = false;
  const std::vector<std::string> locations = Discover(local_ip);
  for (const std::string& location : locations) {
    REXLOG_INFO("UPnP: device at {}", location);
    if (FindService(location, &router)) {
      found = true;
      break;
    }
  }
  if (!found) {
    return locations.empty()
               ? "No router answered (UPnP may be off in the router's settings)."
               : "Your router does not offer automatic port forwarding.";
  }
  REXLOG_INFO("UPnP: {} at {}:{}{}", router.service, router.control.host, router.control.port,
              router.control.path);

  std::string response;
  if (Soap(router, "GetExternalIPAddress", "", &response) == 0) {
    *router_ip = Tag(response, "NewExternalIPAddress");
  }

  std::vector<const Port*> opened;
  std::string failure;
  for (const Port& port : kPorts) {
    const int error = OpenPort(router, port, local_ip);
    if (error == 0) {
      opened.push_back(&port);
    } else if (failure.empty()) {
      failure = std::string("The router would not open ") + port.protocol + " port " +
                std::to_string(port.number) + " (" + SoapError(error) + ").";
    }
  }
  {
    std::lock_guard lock(g_mutex);
    g_router = router;
    g_opened = opened;
  }
  if (failure.empty() && (router_ip->empty() || IsPrivate(*router_ip))) {
    failure =
        "Ports opened, but your router is behind another router or your provider "
        "shares one address between customers (CGNAT), so players outside can't "
        "reach it.";
  }
  return failure;
}

void Work() {
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  SetStatus(State::kWorking, "Opening the game's ports on your router...");

  const std::string local_ip = DefaultRouteIp();
  const std::string bind_ip = rex::cvar::Query<std::string>("net_bind_ip");
  {
    std::lock_guard lock(g_mutex);
    g_status.local_ip = local_ip;
    g_status.public_ip.clear();
    g_status.ports_open = false;
  }
  if (local_ip.empty()) {
    SetStatus(State::kFailed, "This PC has no network connection.");
    WSACleanup();
    return;
  }

  // With a virtual LAN address set, other players reach this PC there; an
  // internet address would only send them the wrong way.
  if (!bind_ip.empty()) {
    SetStatus(State::kFailed, "Not used while \"This PC's address for System Link\" is set (" +
                                  bind_ip + ").");
    WSACleanup();
    return;
  }

  std::string router_ip;
  const std::string failure = OpenRouterPorts(local_ip, &router_ip);

  std::string public_ip = router_ip;
  if (public_ip.empty() || IsPrivate(public_ip)) {
    SetStatus(State::kWorking, "Finding this PC's internet address...");
    public_ip = StunPublicIp();
    REXLOG_INFO("UPnP: internet address from STUN: {}", public_ip.empty() ? "none" : public_ip);
  }
  if (!public_ip.empty()) {
    rex::cvar::SetFlagByName("net_public_ip", public_ip);
    std::lock_guard lock(g_mutex);
    g_status.public_ip = public_ip;
    g_status.ports_open = failure.empty();
    g_set_public_ip = true;
  }

  if (failure.empty()) {
    SetStatus(State::kOpen, "Ports open on your router (" + local_ip + " - " + public_ip +
                                "). Friends can join with your code.");
  } else if (public_ip.empty()) {
    SetStatus(State::kFailed, failure + " This PC's internet address could not be found either.");
  } else {
    SetStatus(State::kFailed,
              failure + " Friends can only join with your code if you open ports 1000, "
                        "1001, 1004 (UDP) and 1003 (TCP) on your router yourself, or use a "
                        "virtual LAN.");
  }
  WSACleanup();
}

}  // namespace

bool enabled() { return REXCVAR_GET(net_upnp); }
void set_enabled(bool on) { REXCVAR_SET(net_upnp, on); }

void Start() {
  if (g_worker.joinable()) g_worker.join();
  g_worker = std::thread(Work);
}

void Stop() {
  if (g_worker.joinable()) g_worker.join();
  std::vector<const Port*> opened;
  Router router;
  {
    std::lock_guard lock(g_mutex);
    opened.swap(g_opened);
    router = g_router;
    if (g_set_public_ip) {
      rex::cvar::SetFlagByName("net_public_ip", std::string());
      g_set_public_ip = false;
    }
    g_status = Status{};
  }
  if (opened.empty()) return;
  WSADATA wsa;
  WSAStartup(MAKEWORD(2, 2), &wsa);
  for (const Port* port : opened) {
    std::string response;
    const int error = Soap(router, "DeletePortMapping", MappingArgs(*port), &response);
    REXLOG_INFO("UPnP: closed {} port {} ({})", port->protocol, port->number,
                error == 0 ? "ok" : SoapError(error));
  }
  WSACleanup();
}

Status status() {
  std::lock_guard lock(g_mutex);
  return g_status;
}

}  // namespace tenchuz::upnp
