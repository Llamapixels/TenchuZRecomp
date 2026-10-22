# Tenchu Z (Xbox 360) → Native Windows x64 Recompilation

This project uses ReXGlue SDK v0.10.0.2-dev (commit `c94f5eb`) to recompile Tenchu Z's `default.xex` into a native Windows x64 executable (`tenchuz.exe`) with runtime support DLLs.

## Key Findings / Workarounds

- Profile settings crash (title screen): Three title-specific binary profile settings (`0x63E83FFF`, `0x63E83FFE`, `0x63E83FFD`) are dereferenced unconditionally. The SDK HLE returns unset title-specific settings with null pointer → access violation on guest 0. Workaround: seed user profile blobs so settings are set with valid pointers.
- Seed script: `tools/seed_profile.ps1` writes the three files to `<user_data_root>/<title_id>/profile/User/` with default contents (sizes 20/80/4 bytes). Default root: `C:\Users\<User>\Documents\tenchuz`; title id: `465307D7`.

## Quick Start

1. Seed profile: `powershell -ExecutionPolicy Bypass -File tools\seed_profile.ps1`
2. Launch with the GUI (recommended): double-click `tools\launcher\Tenchu Z Launcher.cmd`
3. Run (navigate to gameplay): `tools\play_probe.ps1 -Tag test -Plan "2:focus,52:ret,56:ret,60:ret,65:down,67:ret,68:ret,70:ret,72:ret,74:ret,76:ret,78:ret,80:ret,82:ret,84:ret,86:ret,88:ret,89:cap"`

## Graphics & Performance

The launcher (`tools\launcher\tenchuz_launcher.ps1`) is a small GUI for the
runtime configuration. It edits `tenchuz.toml` next to `tenchuz.exe` and starts
the game. It also runs headless for scripts:
`-Headless -Preset "720p-3x"`.

Three independent resolution layers, all documented in
`docs/launcher_options.md`:

| Layer | Key | Effect |
|---|---|---|
| **Render scale** | `resolution_scale` (1-8) | True internal rendering resolution. 3x renders 720p guest mode at 3840x2160 natively. |
| Guest video mode | `video_mode_width/height` | Frame size the game composites into. |
| Window | `window_width/height`, `fullscreen` | Host window only. |

Measured: at guest 720p, dedicated GPU memory is ~196 MB at 1x and ~1173 MB at
3x, confirming the scaler allocates genuinely larger render targets.

### Frame rate changes game speed

The runtime builds the guest vblank from `video_mode_refresh_rate` when `vsync`
is on, and from a 1000 Hz timer when `vsync` is off
(`src/graphics/graphics_system.cpp`). Tenchu Z runs its simulation off vblank, so
the vblank rate **is** the game clock:

| Setting | Game speed |
|---|---|
| 60 Hz, vsync on | Normal (console native) |
| 30 Hz, vsync on | Half speed |
| 120 Hz, vsync on | Double speed |
| any Hz, **vsync off** | ~1000 Hz, unplayably fast |

For normal play use `video_mode_refresh_rate = 60.0` and `vsync = true`.

### Post-processing (ReShade)

ReShade works with this recompilation — install it against `tenchuz.exe` and
choose **Direct3D 12**. Colour effects (grading, bloom, sharpening, AA, grain)
all work. **Depth-based effects (DoF, SSAO/MXAO) do not**, because ReShade
cannot identify the guest-managed float24 depth buffer; there is no setting that
fixes this.

Removing `dxgi.dll` from the game folder is a complete uninstall. Full details,
including the verified swap-chain detection and per-effect support table, are in
`docs/reshade.md`.

For changes ReShade cannot make, the game's own pixel shaders can be replaced
with HLSL files (`shader_overrides\` or a mod's `shaders\` folder). See
`docs/shader_modding.md`.

### Modding

Game content is **loose files on disk** under `extracted\DATA\` — no archive, no
signature, no hash check. The ReXGlue VFS mounts the folder directly, so
replacing a file changes what the game loads. This was verified end-to-end: a
deliberately corrupted `title.xpr` produced a black title screen, and restoring
the byte-identical backup brought it back.

UI textures (`.tga`, `.png`) are unobfuscated standard formats and can be edited
with any image editor. The 636 `.xpr` texture archives (680 MB) are parsed by
`tools\xpr_tool.ps1`, but their pixel payload format is not yet decoded.

**Installing mods:** put each mod in its own folder under `mods\` next to
`tenchuz.exe`, mirroring the game folder (e.g.
`mods\MyMod\DATA\TEXTURE\ENGLISH\title.xpr`). The originals in `extracted\`
are never touched; rename a mod folder to start with `_` to disable it.
Full rules, the reverse-engineered `.xpr` layout and tools are in
`docs/modding.md`.

## Features

The full, up-to-date list of features, hotkeys and their status is in
**`FEATURES.txt`**. In short: an in-game menu (F6) with cheats, damage
multipliers, camera options (invert, speed, FOV, distance, photo mode),
controller remapping / deadzones / rumble, a speedrun timer with splits and
personal bests, an on-screen info panel, shader presets with live sliders,
mod listing, custom pixel shaders, and System Link multiplayer over IP
(`docs/multiplayer.md`).

Every hook and the guest data it relies on is documented in
`tenchuz-recomp/src/gameplay.h` (gameplay, camera, input), `postfx.h`,
`controller.h`, `photo_mode.h` and `speedrun.h`.

Avoid the runtime's own F4 settings screen's **Save to config** button: it
rewrites `tenchuz.toml` with only the changed values, deleting the comments and
the `[log.levels]` fix. Use the launcher or the F6 menu instead.

## Evidence

- No crash after seeding; game boots, reaches title, and renders in-game frames (verified via screenshot captures and frame diffs).
- Runtime logs show continuous rendering; no `FATAL`/guest access violations in tested runs.
- `tools/play_probe.ps1` supports WASD and focus+input timing.

Note: Original ISO/XEX are not modified. All work is on copies.
