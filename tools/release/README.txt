TENCHU Z - HOLOCATT RECOMP PROJECT   (version shown in the window title)
==================================

A native Windows version of Tenchu Z (Xbox 360). This download contains no
game files: you need your own copy of the game as an Xbox 360 disc image
(.iso), USA/Europe release (En, Fr, De, Es, It). Setup checks the version.

This is an ALPHA for testers: expect bugs. Report them at
  https://github.com/Llamapixels/TenchuZRecomp/issues
with logs\tenchuz.log attached.


>>> PRESS F6 IN GAME FOR THE TENCHU Z MENU <<<
Everything this port adds is in that menu: online play and join codes,
your player name, graphics and shader presets, controls, camera, cheats,
photo mode, speedrun timer and the info overlay.


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
- Renderer: Direct3D 12 by default. Vulkan is a work in progress: if the
  game runs slowly or does not start, pick Vulkan in the launcher (or F6 > Game > Renderer, then
  restart). Shader presets need Direct3D 12.
- Keep the frame rate at 60 Hz with vsync on: the game runs its clock off
  the frame rate, so other values change the game speed. The game shows
  30 fps, as on the Xbox 360.
- 60 fps (experimental): tick "60 fps while playing" in the launcher (or
  F6 > Game). Missions run at 60 fps; menus stay at 30.
- Known issue with Vulkan on NVIDIA: one wooden floor in mission 1 looks
  purple or speckled. Direct3D 12 (the default) draws it correctly.

Keyboard defaults (change any key in F6 > Controller > Keyboard & mouse
keys: press Set, then the key or mouse button you want):
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
  F11   Hide / show the game's HUD (also hides subtitles and the pause menu
        in missions while on)


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
