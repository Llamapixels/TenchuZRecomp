# Changelog

## Unreleased

- **Direct mouse camera**: mouse look now turns the camera by exactly the distance moved, in the same frame. It used
  to act as a right stick, so every movement went through the game's 200 ms camera speed ramp, its stick deadzone and
  top speed, and the camera's easing, which felt like input delay at any sensitivity. The mouse turn now goes through
  the game's own yaw and pitch steps (so every state's limits still apply) and turns the eased view with it; movement
  is eased as before. On by default: F6 > Camera > Direct mouse camera and Mouse speed, also in the launcher. Off
  gives the old stick behaviour. Smoothest with 60 fps (experimental) on. Needs SDK patch 0013.

## 0.1.4-alpha

- Mouse sensitivity now supports 0.01–100x in F6 > Camera and the launcher.
  High-sensitivity deltas saturate safely instead of overflowing.
- Launcher output-resolution presets include 1920x1080. The 1080p preset uses
  2x internal rendering (2560x1440) downsampled to the selected output size.
- Download includes the built-in shader presets, with no user-installed mods.

- F6 Display tab, shared graphics profiles, menu scaling, resizable launcher,
  named monitor selection, windowed/borderless modes and temporary Safe launch.
- Experimental ultrawide camera correction with original HUD proportions,
  group HUD size/position controls, and original framing for titles, movies
  and scripted camera classes. Explicit native video dimensions now remain
  independent of the host window even when equal to the SDK defaults.
- Existing framerate controls and the 60 fps patch are unchanged.
- Vulkan's mission-1 walkway corruption is still unresolved; a clean D3D12
  comparison must not be mistaken for a Vulkan fix.

## 0.1.3-alpha

- **Single-player levels in multiplayer**: the System Link host can pick any of the 14 single-player missions in
  F6 > Online and tick "Use this level in multiplayer" before starting. Level list from a community Xenia cheat table.
- **Black menu fix option**: F6 > Game > "Sync GPU-written memory every frame". Turn it off if menus show up black
  (the same as Xenia's `d3d12_clear_memory_page_state = false`). On by default; applies at once.
- Performance settings Xenia players use for this game are now on in the shipped `tenchuz.toml`:
  `readback_memexport = false` and `occlusion_query_enable = false`. Set either back to `true` if something looks
  wrong.

## 0.1.2-alpha

- **Vulkan renderer (work in progress)**: choose Direct3D 12 or Vulkan in the launcher or in F6 > Game > Renderer. Try Vulkan if the
  game runs slowly or does not start with Direct3D 12. If Vulkan cannot start, the game uses Direct3D 12. Shader
  presets need Direct3D 12.
- Modern Look no longer replaces the puddle water shader: puddles look as on the Xbox again, and the seven
  puddle sliders are gone from F6 > Shader.
- **Hide the game's HUD** with F11 or F6 > HUD, for screenshots and videos. In missions this also hides
  subtitles and the pause menu while it is on.
- Fixed a 20-40 second black screen at start-up on some PCs, caused by Windows waiting on a device through
  DirectInput. DirectInput is now off; F6 > Controller > Older USB controllers turns it back on for old generic pads.
- The game keeps a little more CPU priority while its window is in the background, so it should not slow down in
  missions when you click away.
- **60 fps (experimental)**: F6 > Game or the launcher. Missions run at 60 frames per second while you control your
  character; menus stay at 30. Game speed is unchanged.
- The frame rate readout (F6 > HUD) now shows the game's real frame rate as "FPS" and the window's repaint rate
  separately, and the launcher explains that the refresh setting is the game's clock (60 Hz = normal speed, 30 fps),
  not a frame rate target.
- Known issue: with Vulkan on NVIDIA, the wooden walkway under the eaves in mission 1 looks purple or speckled red
  and green. Direct3D 12 (the default) draws it correctly.

## 0.1.1-alpha

- **Keyboard and mouse rebinding** in F6 > Controller: press Set, then the key or mouse button to use. Shift, Ctrl
  and Alt work on their own and as part of a combination; + adds a second key for an input.
- A key no longer stops working while Shift, Ctrl or Alt is held for another action.

## 0.1.0-alpha

First public test release.

- **Native Windows build** of Tenchu Z (Xbox 360), recompiled with the ReXGlue SDK. Setup extracts the game files
  from your own disc image (USA/Europe release) and checks the version.
- **Graphics:** render scale 1x to 7x, window size and fullscreen, two shader presets ("Modern Look" with sharpening,
  bloom, ambient occlusion, haze, depth of field and better puddles; "Manga") with live sliders, custom shaders.
- **Controls:** any controller with button remapping, deadzones and rumble strength; keyboard and mouse with mouse
  look.
- **Online:** the game's System Link co-op over the internet: join codes, automatic router setup (UPnP), player names,
  recent hosts.
- **Extras (F6 menu):** cheats and damage multipliers, camera field of view and distance, photo mode, speedrun timer
  with splits and personal bests, info overlay, mods folder.
- **Fixes over the plain recompilation:** striped partner cards in co-op (a recompiler bug in vector pack
  instructions), menu copy and paste, the game slowing down in the background, and more (see FEATURES.txt).
