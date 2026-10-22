// tenchuz - open the System Link ports on the home router (UPnP)
//
// Players on other networks can only reach a PC whose router forwards the
// System Link ports to it. Most home routers let a program on the PC ask for
// that itself (UPnP Internet Gateway Device). This finds the router, asks it
// to forward the ports (UDP 1000, 1001, 1004 and TCP 1003) to this PC, and
// reads the router's internet address. That address goes to the runtime
// (net_public_ip, given to other players in this PC's System Link address)
// and is shown as a join code in F6 > Online.
//
// Off unless net_upnp is set (F6 > Online). Runs on its own thread; the
// ports are closed again when the game exits or the option is turned off.
// Does not work when the router has UPnP off, or when the router itself is
// behind another router or the provider's shared address (CGNAT). The
// internet address then comes from a public STUN server (Google, else
// Cloudflare), so a join code can still be shown for hand-forwarded ports.

#pragma once

#include <string>

namespace tenchuz::upnp {

enum class State { kOff, kWorking, kOpen, kFailed };

struct Status {
  State state = State::kOff;
  std::string message;    // what happened, for the menu
  std::string public_ip;  // this PC's internet address ("" if unknown)
  bool ports_open = false;  // the router forwards the ports here
  std::string local_ip;   // this PC's address the ports go to
};

bool enabled();
void set_enabled(bool on);

// Opens the ports in the background (again, if already open).
void Start();
// Closes the ports (waits up to a few seconds) and clears net_public_ip.
void Stop();
Status status();

}  // namespace tenchuz::upnp
