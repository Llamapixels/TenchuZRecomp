# Multiplayer (System Link over IP)

Tenchu Z's co-op works through the game's own **System Link** mode. Xbox
Live is gone, but System Link only needs the players' PCs to reach each other
over IP, which the port's runtime now provides: on one network, or over the
internet with a virtual LAN or port forwarding. Everyone needs their own copy
of the game and this port.

Status: verified with two copies on one PC (host in the village, second
player joined and both visible). Join codes verified the same way (code
copied into the other copy, search found the host, join worked). Automatic
router setup (UPnP) verified against a real router: the ports open at
start-up and close at exit. Not yet
tested between two PCs or over the internet.

---

## Playing

1. Everyone starts the game and opens **Mode Select > System Link**.
2. One player **hosts**.
3. The others **search**; the host's game appears in the list. Join it.

### On the same home network

Nothing to set up: search finds the host on its own.

### Over the internet: join code (no extra program)

**Host:**

1. Open **F6 > Online** and press **Get my join code**. The game opens its
   ports on your router (UPnP) and shows **Your join code**, e.g.
   `K3F9-2QX7`. It does this again by itself every time the game starts,
   and closes the ports when the game exits.
2. Press **Copy** and send the code to your friends (Discord, text...).
3. Host in System Link.

**Friends:**

1. Open **F6 > Online**, paste the code under **Join a friend** (**Paste**
   button or Ctrl+V) and press **Join**.
2. Open System Link and Search: the host's game appears.

The code is the host's internet address in short form, with a check letter
that catches typos. Anyone who has it has your IP address, so share it only
with people you play with. The code stays the same until your internet
provider changes your address.

The last 5 codes you joined are listed under **Recent**: **Join** uses one
again, **Copy** copies it, **Remove** forgets it. **Stop** (next to
"Joining ...") goes back to searching your own network.

If the router won't open the ports, the game still finds your internet
address (from a public STUN server: Google's, or Cloudflare's) and shows a
code, with a warning. Friends can only join if you then forward the ports on
your router by hand (table below) or use a virtual LAN.

| Message | Meaning |
| --- | --- |
| No router answered | UPnP is off in the router's settings (turn it on there), or Windows Firewall blocked the answer. |
| The router would not open ... error 718 | Another PC on your network already has these ports. |
| ... error 606 | The router allows UPnP queries but not changes. |
| ... behind another router ... (CGNAT) | Your router is behind another router, or your provider shares one address between customers. UPnP can't help; use a virtual LAN. |

### Over the internet: virtual LAN

If the join code doesn't work for you, use a **virtual LAN**: a free program
that puts the PCs on one private network, as if they were in the same house.
ZeroTier, Radmin VPN and Tailscale all work.

1. Everyone installs the same virtual LAN program and joins the same network.
2. Each player opens **F6 > Online > Advanced** and puts their address on the
   virtual LAN (the program shows it; usually starts `10.`, `100.` or `26.`)
   in **This PC's address for System Link**, then presses **Set**.
3. The host's **Your join code** now stands for that address. Friends join
   with it as above. A plain address also works in the join box.

Joining makes search ask that PC directly instead of broadcasting to the
local network, so it works even with virtual LANs that do not pass
broadcasts (Tailscale). Several hosts can be listed, separated by `;`.

Without UPnP or a virtual LAN, the host can forward these ports on their
router to their PC by hand. Their join code (or their public IP) then works
for friends:

| Port | Protocol | Use |
| --- | --- | --- |
| 1000 | UDP | game traffic |
| 1001 | UDP | voice / data |
| 1003 | TCP | joining (player details) |
| 1004 | UDP | System Link search |

Windows Firewall may ask to allow `tenchuz.exe` the first time: allow it on
private networks (and public ones if your virtual LAN adapter is marked
public).

### Your name

**F6 > Online > Your name**: up to 15 letters, numbers and spaces. Press
**Set**, then **Restart now** (the name is read when the game starts). Your
saves and settings don't change with the name.

### Settings

All saved in `tenchuz.toml`:

| Setting | F6 > Online | Meaning |
| --- | --- | --- |
| `net_join_ip` | Join a friend | Host address(es) the search is sent to. Empty = broadcast to the local network. |
| `net_bind_ip` | Advanced > This PC's address | The address this PC uses for System Link. Empty = automatic (the network adapter that reaches the internet). |
| `net_upnp` | Get my join code / Advanced | Forward the ports with UPnP at start-up; close them at exit. Off until you first press Get my join code. |
| `net_recent_hosts` | Recent | The last 5 codes or addresses joined, with dates. |
| `net_public_ip` | - | This PC's internet address, given to players on other networks. Set while the game runs (from the router, or STUN). |
| `user_name` | Your name | The name other players see (default "User"). |
| `net_trace` | - | Logs every network call to `logs\tenchuz.log` (for troubleshooting). |

---

## Known limits

- **Smoothness depends on the host's frame rate**: each game sends its
  updates (enemies included) once per frame, so if the host's game slows
  down, enemies stutter for everyone else. The game keeps full speed when its
  window is in the background (it turns off Windows' background throttling).
- **Voice chat** is not supported.
- **Xbox Live** mode does not work; use System Link.

---

## Testing on one PC

`tools\systemlink_test.ps1` starts two copies on one PC: copy A on
127.0.0.1 (the normal build folder) and copy B on 127.0.0.2 (a second folder
with its own settings and a copy of your saves). Either can host: copy the
host's join code (F6 > Online) into the other copy and press Join. Add
`-PresetJoin` to skip the codes. Both log all network traffic.

```
powershell -ExecutionPolicy Bypass -File tools\systemlink_test.ps1
```

Add `-Refresh` to copy a new build and your saves into the second folder.
Both copies run at 1x render scale unless you pass `-RenderScale` (0 = each
copy's saved setting). At higher scales the two copies compete for the GPU:
the window you are not playing in drops to a few frames a second, and since
each game sends its updates once per frame, enemies stutter on the other
screen. This only happens with two copies on one PC.
Your controller may drive both windows; click one and use the keyboard.

---

## How it works (for maintainers)

The game-side code is `tenchuz-recomp/src/upnp.cpp` (router discovery with
SSDP, then SOAP `AddPortMapping`, `GetExternalIPAddress` and
`DeletePortMapping` on the router's WANIPConnection or WANPPPConnection
service) and `net_settings.cpp` (join codes: Crockford base32 of the address
plus a check byte; recent hosts). The ports are closed in
`OnWindowCloseRequested` because the runtime hard-exits after the window
closes.

The SDK patch `tenchuz-recomp\sdk_patches\0002-system-link.patch`
(SDK-VERSION.md) fills in the Xbox network layer the runtime had stubbed:

- **Addresses**: `XNetGetTitleXnAddr` reports the real (or `net_bind_ip`)
  address and an Ethernet id made from it; `XNetXnAddrToInAddr` /
  `XNetInAddrToXnAddr` map System Link addresses to IPs directly.
- **Keys**: `XNetCreateKey` / `XNetRegisterKey` create and accept System Link
  keys (no encryption).
- **Sessions**: a hosted session gets real session info (id, host address,
  key) and a nonce.
- **Join by IP**: with `net_join_ip` set, a broadcast `sendto` goes to those
  hosts instead; `net_bind_ip` replaces "any address" in `bind`.
- **Search answers**: the host answers a System Link search with a
  broadcast, which never reaches a player on another network. Without
  `net_join_ip`, a broadcast still goes to the local network, and also to
  each address that sent to that socket in the last 10 seconds and is not
  on this network (same first three numbers). That's how the searcher gets
  the answer. This also happens with `net_join_ip` set (a host left with a
  join address from an earlier game), and nothing is ever sent to this PC's
  own address.
- **Internet addresses**: `XNetGetTitleXnAddr` puts `net_public_ip` in the
  XNADDR's `inaOnline`. `XNetXnAddrToInAddr` uses `inaOnline` for a machine
  on another network: one whose `inaOnline` is in `net_join_ip`, or differs
  from our own `net_public_ip`. On the same network it uses `ina`. If
  `net_join_ip` names one host and the address given is a private one that
  isn't ours, the `net_join_ip` host is used (a host behind NAT that does
  not know its internet address). An `ina` listed in `net_join_ip` always
  wins (virtual LANs). With `net_bind_ip` set the game does not look up an
  internet address at all.
- **Player id**: the id the game sees is unique per PC and save folder (saves
  stay under the old fixed id).
- **Player name**: `user_name` is the gamertag. The per-title profile folder
  (`<user_data_root>/465307D7/profile/User`, which holds the settings files
  Tenchu Z needs) no longer follows the name (`content_manager.cpp`).
- **Bug fixes in the runtime**: socket addresses and ports were byte-swapped
  in `sendto` / `recvfrom`; `WSAWaitForMultipleEvents` treated its millisecond
  timeout as an absolute kernel time and always reported "event 0", so the
  game read unconnected TCP sockets and closed them; `recv`, `send` and
  `accept` did not set the error code (WSAEWOULDBLOCK), so the game dropped
  TCP connections; `WSAEventSelect` was a stub; the "cable connected" check
  always said no; `XUserMuteListQuery` failed.
- **Menu copy/paste**: the runtime passed Ctrl, Shift and Alt to the menus
  the pre-1.87 ImGui way, which ImGui 1.92 ignores, so Ctrl+V did nothing.
  Fixed by `sdk_patches/0003-menu-modifier-keys.patch`.

Tenchu Z's System Link protocol, from the trace: search is a 16-byte UDP
broadcast to port 1004 (type 0x01) answered by a 112-byte session
description (0x02); joining is a request/reply on UDP 1000 (0x11), the
player's id and name (0x03), the session's player list (0x04), then a TCP
connection to port 1003 carrying the player's details. In game, players
exchange state (0x0A) and pings (0x16) on UDP 1000.
