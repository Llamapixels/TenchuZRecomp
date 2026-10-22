TENCHU Z - HOLOCATT RECOMP PROJECT   (version shown in the window title)
==================================

A native Windows version of Tenchu Z (Xbox 360). This download contains no
game files: you need your own copy of the game as an Xbox 360 disc image
(.iso), USA/Europe release (En, Fr, De, Es, It). Setup checks the version.

This is an ALPHA for testers: expect bugs. Report them at
  https://github.com/Llamapixels/TenchuZRecomp/issues
with logs\tenchuz.log attached.


SETTING UP (once)
-----------------
1. Unzip this folder anywhere (it needs about 8 GB free for the game files).
2. Put your Tenchu Z .iso in this folder (next to tenchuz.exe).
3. Double-click "Setup Tenchu Z.cmd". It copies the game files out of your
   .iso into the "extracted" folder (a few minutes) and creates the profile
   settings the game needs. If there is no .iso in the folder it asks you to
   pick one.
4. Afterwards the .iso is not needed any more; delete or move it if you like.

Windows may warn that the files come from the internet or an unknown
publisher (they are not signed). Choose "More info" > "Run anyway".


PLAYING
-------
- "Tenchu Z Launcher.cmd": settings (resolution, window, frame rate) and
  start the game.
- Or start tenchuz.exe directly.
- An Xbox controller is recommended. Keyboard and mouse also work.
- Keep the frame rate at 60 Hz with vsync on: the game runs its clock off
  the frame rate, so other values change the game speed.

Keyboard (change keys with the keybind_* lines in tenchuz.toml):
  Move W A S D            Camera arrow keys (or the mouse: F6 > Camera)
  A    Space or ;         B      Backspace or '
  X    L                  Y      P
  LT   Q or I             RT     E or O
  LB   1                  RB     3
  Left stick press F      Right stick press K
  D-pad Shift + arrows    Start Enter or X, Back Tab or Z

Hotkeys in game:
  F6    Tenchu Z menu (cheats, camera, controller, shader looks, online...)
  F8    God mode on/off           F5    Refill health
  F9    Pause / resume mouse look F10   Photo mode (free camera)
  F1/F2 Save / load position (practice)


PLAYING TOGETHER (System Link over the internet)
-----------------------------------------------
Everyone needs their own copy of the game and of this folder.

Host:
  1. F6 > Online > "Get my join code". The game opens its ports on your
     router (UPnP) and shows a code like K3F9-2QX7. Press Copy and send it
     to your friends. (The code contains your internet address; only share
     it with people you play with.)
  2. Mode Select > System Link > host a game.
Friends:
  1. F6 > Online > Join a friend: paste the code (Paste button or Ctrl+V),
     press Join.
  2. Mode Select > System Link > search. The host's game appears.

Windows Firewall may ask to allow tenchuz.exe the first time: allow it.
If the host's router refuses UPnP, the host can forward UDP 1000, 1001, 1004
and TCP 1003 to their PC by hand, or everyone can use a virtual LAN
(ZeroTier, Radmin VPN, Tailscale). Details: docs\multiplayer.md.
Set your player name in F6 > Online > Your name.


MORE
----
- FEATURES.txt   everything this port adds, with hotkeys.
- docs\          multiplayer, mods, custom shaders, launcher options, ReShade.
- mods\          put mods here (each in its own folder; see docs\modding.md).
- Settings are saved in tenchuz.toml (this folder); saves and profile in
  Documents\tenchuz.
