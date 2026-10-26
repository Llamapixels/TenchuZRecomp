# Display and graphics profiles

F6 > **Display** contains renderer selection (moved from Game), render scale,
window dimensions, monitor, windowed/borderless mode, experimental ultrawide,
graphics profiles, and menu/overlay scaling. The launcher offers the same
display choices. Existing framerate controls and the 60 fps patch are unchanged.

## Display

- **Windowed** uses the configured window dimensions. **Borderless fullscreen**
  uses the selected monitor's desktop resolution; it does not change the
  monitor's refresh rate or provide exclusive fullscreen.
- **Monitor** stores the Windows device name, rather than an enumeration index.
  A disconnected monitor falls back to the default. Monitor and window-size
  changes require a restart. The legacy SDK `monitor` setting remains supported;
  a named monitor selected here takes precedence at startup.
- **UI scale**, 0.75–2.0, changes the F6 menu and the port's overlays immediately.
  It does not resize the original game's HUD or menus. The menu can scroll when
  its content exceeds the available window height.
- The launcher is resizable: its content scales uniformly with width, with
  vertical scrolling for shorter windows. WPF also follows Windows DPI scaling.
- Output presets include 1080p (1920x1080). Selecting it uses at least 2x
  internal rendering (2560x1440), downsampled to the output. Internal render
  scale remains integer-based; this is not a fractional 1.5x render mode.
- Mouse sensitivity is adjustable from 0.01x to 100x in the launcher and
  F6 > Camera. The mouse still drives the game's right-stick camera, so a
  higher value reaches maximum stick input with less mouse movement.

## Experimental ultrawide

**Expand 3D view to window** adjusts the main camera's horizontal projection to
the output aspect ratio while retaining the vertical FOV. It automatically
disables presentation letterboxing; turning it off restores letterboxing.
This follows the window, including 21:9 and 32:9, and does not replace the
separate FOV or follow-distance settings in F6 > Camera.

The game still renders its original 1280×720 frame multiplied by render scale.
Projection correction compensates for stretching that frame to the output.
Gameplay HUD proportions are corrected after the scene's final screen pass,
including the mission-selection panel. **Game HUD size**, **horizontal position**
and **vertical position** move/resize the HUD as a group; Reset restores it.
These controls do not move individual elements independently. Large offsets
can clip the HUD. The HUD correction requires SDK patch 0010.

Titles, movies and scripted camera classes retain original framing. The native
video mode stays independent of window dimensions with SDK patch 0011, including
when explicitly configured dimensions happen to equal the SDK defaults.
Gameplay and title framing have been inspected in the live build, including
a 32:9 Vulkan window. Extensive cutscene, culling, photo-mode, effect and
multiple-monitor coverage remains unverified; the option stays experimental.

## Named graphics profiles

Save/load profiles from F6 > Display or the launcher's Graphics section.
Files are shared in `graphics_profiles/<name>.toml` beside `tenchuz.exe`.
Names allow 1–48 ASCII letters, digits, hyphens and underscores; Windows
reserved names are rejected.

Profiles save renderer, render scale, window dimensions, fullscreen, named
monitor, letterboxing/ultrawide, UI scale, camera FOV/distance, shader preset
selection, shader effects enabled, shader overrides enabled and game HUD layout. Individual
shader slider values continue to use the existing `shader_settings.ini`;
they are not independent snapshots inside each graphics profile.

Profiles exclude framerate, refresh, vsync, tearing, controls, cheats, mods,
network settings, saves, and file paths. Loading applies live settings and
saves the selected graphics values to `tenchuz.toml`; restart for the renderer,
render scale and startup window/monitor settings. Load through F6 while the
game is running; the launcher asks you to close the game before loading.
Hand-edited profiles are restricted to the same key allowlist and checked for
types/ranges. Unsupported keys cannot overwrite unrelated settings.

## Safe launch

The launcher **Safe launch** button starts a temporary recovery run with
Direct3D 12, 1× scale, a 1280×720 window on the default monitor, original aspect,
default camera FOV/distance and UI scale, and mods/shader overrides/effects off.
It does not save the launcher's edits or rename any mod folders.

**All existing frame timing settings are retained**, including the 60 fps
patch, refresh, vsync and tearing. Safe launch therefore does not diagnose
problems caused by those values.

F6 changes are temporary in recovery; profile save/load and shader-slider
persistence are blocked. The SDK's F4 Save uses a separate recovery file under
`logs/`, never the normal config. Close the game and launch normally to return
to the saved setup. Command-line use:

```powershell
powershell -ExecutionPolicy Bypass -File tools\launcher\tenchuz_launcher.ps1 -SafeLaunch
```

## Developer checks

`tools\tests\run_display_tests.bat` builds standalone profile/persistence tests
and runs the launcher's WPF/config tests against a temporary fake installation.
No game files are used and no real game process is launched by those tests.
The C++ tests are opt-in via `TENCHUZ_DISPLAY_TESTS=ON`. For launcher layout
renders, run `launcher_display_tests.ps1 -RenderDir <absolute-output-folder>`.
