# Tenchu Z - HoloCatt Recomp Project

**Tenchu Z - HoloCatt Recomp Project** is a work-in-progress static recompilation of *Tenchu Z* (Xbox 360) into a
native Windows game. The game's own code runs natively on your PC, with higher resolutions, modern graphics options,
mods, cheats, a speedrun timer, a photo mode and System Link co-op over the internet.

> [!TIP]
> **Press F6 in game for the Tenchu Z menu.** Everything this port adds is there: online play and join codes, your
> player name, graphics and shader presets, controller and mouse settings, camera, cheats, photo mode, the speedrun
> timer and the info overlay. A hint at the top of the screen reminds you when the game starts.

> [!WARNING]
> **This is an alpha, a preview for testers.** The hub and the first missions have been played; the rest of the game
> hasn't been played through yet on this port, so expect bugs or crashes further on. It has only been tested on one
> PC (Windows 11, NVIDIA). If something breaks, please
> [report it](../../issues/new/choose): every report shows how far the game really works.

> [!NOTE]
> **No game files are included.** You need your own copy of *Tenchu Z* for the Xbox 360 as a disc image (`.iso`)
> made from your own disc. This project does not include, and will not provide, game files, disc images or links to
> them. The setup only reads the disc image you point it to; your disc image isn't changed.

## Getting started (Alpha 0.1.3, Windows)

1. Download `TenchuZ-HoloCattRecomp-0.1.3-alpha-windows.zip` from [Releases](../../releases) and extract it to a
   folder you can write to (not Program Files). It needs about 5 GB free for the game files.
2. Put your Tenchu Z disc image (`.iso`) in that folder, next to `tenchuz.exe`.
3. Double-click **`Setup Tenchu Z.cmd`**. It checks that your disc is the supported version, copies the game files
   out of it into the `extracted` folder (a few minutes) and creates the profile the game needs. If there's no `.iso`
   in the folder, it asks you to pick one. Afterwards the `.iso` isn't needed any more.
4. Start the game with **`Tenchu Z Launcher.cmd`** (resolution, window, frame rate, then Play) or `tenchuz.exe`.
5. In game, press **F6** for the Tenchu Z menu: online play, graphics, controls, cheats and the other extras.

Windows may warn that the files come from an unknown publisher (they aren't signed): choose **More info > Run
anyway**. Windows Firewall may ask to allow `tenchuz.exe` the first time; allow it for online play.

Needs Windows 10 or 11 (64-bit) and a graphics card with DirectX 12 or Vulkan.

## What you need

- Your own *Tenchu Z* for the Xbox 360, **USA/Europe release (English, French, German, Spanish, Italian)**, as a disc
  image (`.iso`). Setup checks the version; other versions (for example the Japanese release) aren't supported.
- The files go in `extracted/` next to `tenchuz.exe`; the setup puts them there.

## Status

Estimates of where each part stands. "Tested in game" is how much of the game has been played through on this port
so far.

| | Part | Progress | What it means |
|---|---|---|---|
| ✅ | **Recompiler** | ![100%](https://progress-bar.xyz/100/?width=150&style=square&progress_color=5cb85c) | The whole game recompiles to native x64 code (ReXGlue) |
| ✅ | **Setup from your disc image** | ![100%](https://progress-bar.xyz/100/?width=150&style=square&progress_color=5cb85c) | Version check and file extraction from your own `.iso`, no extra programs |
| ✅ | **Graphics (Direct3D 12)** | ![90%](https://progress-bar.xyz/90/?width=150&style=square&progress_color=5cb85c) | Hub and missions render correctly on NVIDIA; other GPUs untested |
| 🔧 | **Graphics (Vulkan, work in progress)** | ![80%](https://progress-bar.xyz/80/?width=150&style=square&progress_color=5cb85c) | Choose it in the launcher or F6 > Game; menus and missions render like Direct3D 12 on NVIDIA except one floor in mission 1 (purple or speckled), other GPUs untested. Falls back to Direct3D 12 if Vulkan cannot start |
| ✅ | **Render scale** | ![100%](https://progress-bar.xyz/100/?width=150&style=square&progress_color=5cb85c) | 1x to 7x internal resolution (3x = 3840x2160) |
| ✅ | **Sound and music** | ![100%](https://progress-bar.xyz/100/?width=150&style=square&progress_color=5cb85c) | Including mute when the window is in the background |
| ✅ | **Controllers** | ![90%](https://progress-bar.xyz/90/?width=150&style=square&progress_color=5cb85c) | Any controller; button remapping, stick deadzones and rumble strength in F6 > Controller (remapping needs more testing) |
| ✅ | **Keyboard and mouse** | ![95%](https://progress-bar.xyz/95/?width=150&style=square&progress_color=5cb85c) | Rebind every controller input in F6 > Controller by pressing the key or mouse button; mouse look |
| ✅ | **System Link co-op over the internet** | ![85%](https://progress-bar.xyz/85/?width=150&style=square&progress_color=5cb85c) | Join codes, automatic router setup (UPnP), player names; tested with two copies on one PC, not yet between two PCs |
| ✅ | **Shader presets** | ![100%](https://progress-bar.xyz/100/?width=150&style=square&progress_color=5cb85c) | "Modern Look" (sharpening, bloom, ambient occlusion, haze, depth of field) and "Manga", with live sliders |
| ✅ | **Mods** | ![100%](https://progress-bar.xyz/100/?width=150&style=square&progress_color=5cb85c) | `mods\` folder for replacement files and custom shaders, without touching the game files |
| ✅ | **Extras** | ![100%](https://progress-bar.xyz/100/?width=150&style=square&progress_color=5cb85c) | Cheats, damage multipliers, camera FOV and distance, photo mode, speedrun timer with splits, info overlay |
| 🔧 | **Tested in game** | ![15%](https://progress-bar.xyz/15/?width=150&style=square&progress_color=d9534f) | The hub and the first missions; the rest is untested: [report what breaks](../../issues/new/choose) |
| 🔧 | **Other graphics cards** | ![0%](https://progress-bar.xyz/0/?width=150&style=square&progress_color=d9534f) | AMD and Intel GPUs untested: reports welcome |
| 🗺️ | **Voice chat** | ![0%](https://progress-bar.xyz/0/?width=150&style=square&progress_color=d9534f) | Not planned: use Discord or similar |

✅ done · 🔧 in progress · 🗺️ roadmap

## Playing together

Co-op uses the game's own **System Link** mode, over the internet. Everyone needs their own copy of the game.

- **Host:** F6 > Online > **Get my join code**. The game opens its ports on your router (UPnP) and shows a code like
  `K3F9-2QX7`. Press Copy and send it to your friends, then host in Mode Select > System Link. The code stands for your
  internet address, so only share it with people you play with.
- **Friends:** F6 > Online > Join a friend: paste the code (Paste button or Ctrl+V), press **Join**, then search in
  System Link.
- Set your name in F6 > Online > Your name.

If the host's router doesn't allow UPnP, forward UDP 1000, 1001, 1004 and TCP 1003 to the host's PC, or use a virtual
LAN (ZeroTier, Radmin VPN, Tailscale). All details: [docs/multiplayer.md](docs/multiplayer.md).

## Controls

Any controller works. Keyboard and mouse: change any key in **F6 > Controller > Keyboard & mouse keys**. Press
**Set** and then the key or mouse button you want (Shift/Ctrl/Alt + key works too, and so do Shift, Ctrl or Alt on
their own); **+** adds a second key for the same input. Mouse look is in F6 > Camera. The defaults:

| Xbox button | Keys | Xbox button | Keys |
|---|---|---|---|
| Left stick | W A S D | Right stick (camera) | Arrow keys, or the mouse (F6 > Camera > Mouse look) |
| A | Space or ; | B | Backspace or ' |
| X | L | Y | P |
| LT | Q or I | RT | E or O |
| LB | 1 | RB | 3 |
| Left stick press | F | Right stick press | K |
| D-pad | Shift + arrow keys | Start / Back | Enter or X / Tab or Z |

| Hotkey | | Hotkey | |
|---|---|---|---|
| F6 | Tenchu Z menu | F8 | God mode on/off |
| F5 | Refill health | F9 | Pause / resume mouse look |
| F10 | Photo mode (free camera) | F1 / F2 | Save / load position (practice) |
| F11 | Hide / show the game's HUD | | |

Keep the frame rate at 60 Hz with vsync on (the launcher's default): the game runs its clock off the frame rate, so
the game shows 30 fps as on the Xbox 360. For 60 fps in missions, turn on **60 fps (experimental)** in the launcher or
F6 > Game; menus stay at 30.

## Reporting bugs

[Open a bug report](../../issues/new/choose) and attach `logs\tenchuz.log` from the game folder. Say where in the
game it happened and what your PC is (Windows version, graphics card). Screenshots or a short video help a lot.

## Building from source

Everything needed is here: the game-specific code (`tenchuz-recomp/src`), the recompiler manifest, the changes to the
ReXGlue SDK (`tenchuz-recomp/sdk_patches`) and the tools. The game code itself is generated on your machine from your
own `default.xex`. Steps: [docs/building.md](docs/building.md). Pull requests are welcome.

## Respect copyright

- *Tenchu Z* is © 2007 FromSoftware, Inc. It was developed by K2 LLC and published by FromSoftware and Microsoft Game
  Studios. This is a fan project. It is not affiliated with, endorsed by or sponsored by FromSoftware, K2, Microsoft or
  any other rights holder. *Tenchu* is a trademark of FromSoftware; *Xbox* and *Xbox 360* are trademarks of Microsoft.
- This repository contains no game files and no game code: no disc images, no assets, no music, no video, no text,
  and not the C++ code the recompiler generates from the game (it is made on your machine and ignored by git).
- The release's `tenchuz.exe` is built from that recompiled code. It contains no assets and can't run without the
  files from your own disc.
- Issues or discussions that contain game files, disc images or links to copies of the game will be removed. Piracy
  isn't supported in any way.
- Follow the copyright laws of your country. Making a disc image of a game you own for personal use is legal in some
  countries and not in others; that's your responsibility.

## Credits

- **[ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)** - the Xbox 360 recompiler and runtime this project is
  built on (BSD 3-Clause), which builds on the [Xenia](https://github.com/xenia-project/xenia) emulator's work
  (BSD 3-Clause).
- Libraries, under their own licenses (texts in the release's `licenses` folder): [SDL 3](https://libsdl.org) (zlib),
  [FFmpeg](https://ffmpeg.org) (LGPL 2.1), [Dear ImGui](https://github.com/ocornut/imgui) (MIT),
  [fmt](https://github.com/fmtlib/fmt) (MIT), [spdlog](https://github.com/gabime/spdlog) (MIT),
  [toml++](https://github.com/marzer/tomlplusplus) (MIT), [xxHash](https://github.com/Cyan4973/xxHash) (BSD),
  [SIMDe](https://github.com/simd-everywhere/simde) (MIT), [libmspack](https://www.cabextract.org.uk/libmspack/)
  (LGPL 2.1) and others.
- The release includes Microsoft's Visual C++ runtime DLLs, redistributed as Microsoft permits.

## License

This project's own code and documentation are open source under the [MIT License](LICENSE): take it, change it,
make it better. The license covers this project's code only; it grants nothing for *Tenchu Z*, which you must supply
yourself. The ReXGlue SDK and the libraries keep their own licenses.

## Changes

See [CHANGELOG.md](CHANGELOG.md). Everything the port adds, with its test status: [FEATURES.txt](FEATURES.txt).
