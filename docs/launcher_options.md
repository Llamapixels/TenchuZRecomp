# Tenchu Z — Launcher options reference

> **Update - measured:** Tenchu Z always draws a **1280x720** frame, whatever
> `video_mode_width/height` is set to. Hooking the game's Swap (`sub_8241BF50`)
> and reading the front-buffer size gives 1280x720 at both a 1280x720 and a
> 2560x1440 video mode. A larger video mode only makes the runtime stretch the
> 720p frame, so the launcher now pins the video mode to 1280x720 and offers
> `resolution_scale` 1-7 (the SDK maximum is 7; the old 8x option was silently
> clamped) as the only resolution control. Internal resolution is
> 1280x720 x scale, e.g. 3x = 3840x2160. The launcher shows the
> numbers. Sections below that describe the guest video mode as raising the
> resolution predate this measurement.


Everything here is a **runtime cvar** of the ReXGlue runtime. Keys are written
to `tenchuz.toml`, which lives next to `tenchuz.exe`
(`tenchuz-recomp\out\build\win-amd64-release\tenchuz.toml`) and is read at
startup. Every option is also accepted as a command-line flag of the same name.

The easiest way to change these is the graphical launcher:

```
powershell -ExecutionPolicy Bypass -File tools\launcher\tenchuz_launcher.ps1
```

Source of truth for each option is the ReXGlue SDK, commit `c94f5eb`.

---

## The three independent resolution concepts

This is the part that trips people up, so it is worth being explicit. There are
**three** separate sizes, and they do different things:

| Concept | What it controls | Keys |
|---|---|---|
| **Render scale** | The resolution the game actually draws into. This is the real "game rendering resolution". | `resolution_scale` (or `draw_resolution_scale_x` / `_y`) |
| **Guest video mode** | The display size the game *believes* the console is outputting. | `video_mode_width` / `video_mode_height` / `resolution` |
| **Window** | The host OS window, and therefore what the final image is scaled into. | `window_width` / `window_height` / `fullscreen` |

### Why setting the window alone does nothing for image quality

Setting only `window_width`/`window_height` makes a bigger window, but the image
inside it is still rendered at the game's internal resolution and then stretched.
That is exactly the "I can set my window to a resolution but that doesn't mean
the game is at that resolution" problem.

`resolution_scale` is the lever that changes actual rendering resolution.

### Verified behaviour of `resolution_scale`

`resolution_scale` is the ReXGlue equivalent of Xenia's
`draw_resolution_scale`. It multiplies the dimensions of the render targets the
game draws into.

- `resolution_scale = 1` — native. 720p guest mode renders 1280×720.
- `resolution_scale = 2` — 2× in each axis. 720p guest mode renders 2560×1440.
- `resolution_scale = 3` — 3× in each axis. 720p guest mode renders 3840×2160.

Valid range is **1 to 8**. It is applied per-axis; the separate
`draw_resolution_scale_x` / `draw_resolution_scale_y` keys allow non-uniform
scaling, and `resolution_scale` overrides both when set.

**Measured evidence** (Tenchu Z, RTX 4060 Ti, guest mode 1280×720, at the title
screen and the first menu):

| `resolution_scale` | Dedicated GPU memory |
|---|---|
| 1 | ~196 MB |
| 3 | ~1173 MB |

That ~6× growth confirms the scaler is genuinely allocating larger render
targets, not just resizing the output image.

### Cost and caveats

- Higher scale costs VRAM and GPU time roughly with the square of the scale
  (3× means 9× the pixels).
- Render-to-texture effects, EDRAM resolves and post-processing are the most
  likely things to break on some titles. Tenchu Z tolerates 2× and 3× cleanly in
  early testing, but test each setting in actual gameplay, not just menus.
- The launcher's *effective draw size* readout shows the resulting internal
  resolution, so you can see exactly what a choice means before launching.

---

## Rendering resolution

| Key | Type | Default | Notes |
|---|---|---|---|
| `resolution_scale` | int 1–8 | 1 | Draw-resolution scale for both axes. The main quality knob. |
| `draw_resolution_scale_x` | int 1–8 | 1 | Per-axis override for X. Ignored if `resolution_scale` is set. |
| `draw_resolution_scale_y` | int 1–8 | 1 | Per-axis override for Y. Ignored if `resolution_scale` is set. |

All three require a restart. They affect render-target allocation, so changing
them mid-session is not supported.

---

## Output resolution (guest video mode)

| Key | Type | Default | Notes |
|---|---|---|---|
| `resolution` | string | `""` | Preset name applied to **both** the guest video mode and the startup window. Names: `480p`, `540p`, `720p`, `900p`, `1080p`, `1440p`, `1800p`, `2160p`, `4k`, or explicit `WxH`. |
| `video_mode_width` | int 640–4095 | 1280 | Guest display width. Overrides the preset for the guest mode. |
| `video_mode_height` | int 480–4095 | 720 | Guest display height. Overrides the preset for the guest mode. |

Precedence for the **guest video mode** (from
`src/kernel/xboxkrnl/xboxkrnl_video.cpp`):

```
video_mode_width/height (if non-default)
  -> window_width/height (if non-default)
    -> resolution preset
      -> built-in default (1280x720)
```

### How Tenchu Z uses the guest video mode

Verified in the recompiled guest code. In `tenchuz_recomp.82.cpp`:

```asm
bl  0x828097dc        ; VdQueryVideoMode(stack+80)
lwz r11, 80(r1)       ; display_width
lwz r10, 84(r1)       ; display_height
stw r11, 19964(r29)   ; device->render_width  = display_width
stw r10, 19968(r29)   ; device->render_height = display_height
stw r11, 19972(r29)   ; device->display_width  = display_width
```

So the guest video mode **is** read by the game and used as the size of the
frame it composites into. This is why `video_mode_*` has a real effect and is a
useful companion to `resolution_scale`: it sets the logical frame size, while
`resolution_scale` multiplies the render target behind it.

The game also calls `VdQueryVideoMode` from `sub_8242E7A8` but only reads the
*flags* word from the result, not the dimensions — so not every consumer of the
video mode cares about size.

---

## Window

| Key | Type | Default | Notes |
|---|---|---|---|
| `window_width` | int 0–8192 | 0 | Startup window width in logical pixels. `0` = follow guest video mode / preset. |
| `window_height` | int 0–8192 | 0 | Startup window height in logical pixels. `0` = as above. |
| `fullscreen` | bool | false | Start fullscreen. |
| `monitor` | int | 0 | `0` = default, `1` = primary, `2` = secondary. |

Precedence for the **window** (from `src/ui/window_sdl.cpp`):

```
window_width/height (if > 0)
  -> video_mode_width/height (if non-default)
    -> resolution preset
      -> app default (1280x720)
```

Note the asymmetry: `window_*` beats `video_mode_*` for the window, but
`video_mode_*` beats `window_*` for the guest mode. If you want a large window
with a smaller guest mode, set both explicitly.

---

## Frame rate

| Key | Type | Default | Notes |
|---|---|---|---|
| `video_mode_refresh_rate` | double 24–240 | 60.0 | Guest video-mode refresh rate in Hz. **Also the game's simulation rate.** |
| `vsync` | bool | true | Selects guest vblank source (see below), and host vertical sync. |
| `d3d12_allow_variable_refresh_rate_and_tearing` | bool | true | Allow VRR / tearing present. |

### Frame rate changes game speed — this is expected

This is the single most surprising thing about this title, and it is worth being
precise about.

The runtime generates the emulated console's vblank from the configured refresh
rate. From `src/graphics/graphics_system.cpp`:

```cpp
double refresh_rate_hz = std::max(1.0, double(float(video_mode.refresh_rate)));
uint64_t vsync_interval_ticks   = guest_tick_frequency / refresh_rate_hz;
uint64_t no_vsync_interval_ticks = guest_tick_frequency / 1000;
...
uint64_t interval_ticks = REXCVAR_GET(vsync) ? vsync_interval_ticks
                                             : no_vsync_interval_ticks;
while (elapsed >= interval_ticks) { MarkVblank(); }
```

Tenchu Z drives its simulation from vblank. Therefore the vblank rate **is** the
game's clock:

| Setting | Result |
|---|---|
| 60 Hz + vsync on | Normal speed. The console's native rate. |
| 30 Hz + vsync on | Half speed (slow motion). |
| 120 Hz + vsync on | Double speed. |
| any Hz + **vsync off** | **1000 Hz timer** — the game runs extremely fast. |

**Important:** `vsync = false` does not mean "uncapped but otherwise normal". It
swaps the vblank timer to a fixed 1000 Hz. For Tenchu Z that makes the game run
far too fast to play. Keep `vsync = true` and control speed with the refresh
rate instead.

This behaviour also proves the game is **not** hard-coded to 60 Hz logic — it
genuinely follows the video mode. So the frame rate setting is a real, working
control, just not in the way "uncap the framerate" usually implies.

### What this means in practice

- **Normal gameplay:** `video_mode_refresh_rate = 60.0`, `vsync = true`.
- **Smoothness above 60 Hz:** possible only if you accept faster game speed
  (e.g. 120 Hz = 2x speed), or if a future SDK/runtime change decouples the
  guest vblank rate from game logic.
- **True high-refresh presentation at normal speed** would require either a
  runtime that separates vblank from simulation, or a guest-side patch to
  Tenchu Z's frame pacing. Neither is available today.

Both `video_mode_refresh_rate` and `vsync` require a restart.

### Host presentation

`vsync` also governs host vertical sync, so it cannot be freely turned off to
uncap presentation without also changing game speed. Leave it on.

---

## Presentation

| Key | Type | Default | Notes |
|---|---|---|---|
| `present_letterbox` | bool | true | Letterbox for non-native aspect ratios. |
| `present_safe_area_x` | int 0–100 | 90 | Horizontal safe area percentage. |
| `present_safe_area_y` | int 0–100 | 90 | Vertical safe area percentage. |
| `present_effect` | string | `bilinear` | Guest output effect: `bilinear`, `cas`, `fsr`, `fsr2`, `fsr3`. |
| `present_dither` | bool | false | Dithering in the final present pass. |

**Important:** this SDK build was compiled with
`REXGLUE_ENABLE_FIDELITYFX:BOOL=OFF`, so `present_effect` only accepts
`bilinear`. The `cas` / `fsr` / `fsr2` / `fsr3` values are compiled in only when
FidelityFX is enabled at SDK build time (`-DREXGLUE_ENABLE_FIDELITYFX=ON`,
experimental). To get sharp upscaling before then, raise `resolution_scale` and
keep the window at native.

---

## GPU options already required by this title

| Key | Value | Why |
|---|---|---|
| `use_fuzzy_alpha_epsilon` | `true` | **Required.** Without it, characters render as garbled pixels. Relaxes alpha-test comparison so alpha tests that should compare equal are not spuriously rejected. Same fix as Xenia Canary on NVIDIA. |

Do not remove this key.

---

## Logging

| Key | Type | Default | Notes |
|---|---|---|---|
| `log_level` | string | `info` | Global level: `trace`, `debug`, `info`, `warn`, `error`, `critical`, `off`. |
| `log_file` | string | (empty) | Log file path. Empty means `<exe_dir>/logs/`. |
| `log_flush_interval` | int | 0 | Periodic flush interval in seconds (0 = off). |
| `[log.levels]` | table | (empty) | Per-category level overrides, keyed by category name. |

### Tenchu Z logs ~180 errors/second without a per-category override

Tenchu Z issues roughly **180 empty resolve operations per second** in gameplay —
about three per frame at 60 Hz. Each is a `PM4_DRAW_INDX_2` with `edram_mode=6`
(the EDRAM copy/resolve path) whose rectangle, after being clamped against the
scissor rectangle, is zero-area.

The runtime handles this correctly. In `src/graphics/util/draw.cpp`,
`GetResolveInfo` computes the rectangle, clamps it, and on finding it empty logs
`"Resolve region is empty"` and returns `false`; one level up in
`src/graphics/d3d12/render_target_cache.cpp` a zero-area resolve is explicitly
treated as a no-op:

```cpp
// Nothing to copy/clear.
if (!resolve_info.coordinate_info.width_div_8 || !resolve_info.height_div_8) {
  return true;
}
```

**Nothing renders incorrectly** — the operation genuinely does nothing. The only
real cost is the log, and it is not small: the two `[error]` lines emitted per
event grow the log by about **1 MB every 10 seconds**, and the runtime rotates at
5 MB, so the flood produced ten rotation files (`runtime_test.1.log` …
`runtime_test.10.log`, ~50 MB) in a couple of hours of play.

Measured, same build and settings:

| `gpu` category level | Errors in 75 s | Log size in 75 s |
|---|---|---|
| default (`error`) | 18,908 | 4.3 MB / 105 s |
| `critical` | 0 | 1.1 KB |

### The fix: suppress the category, not the rendering

Per-category levels are read from the config file by
`ParseCategoryLevelsFromConfig` (`src/core/logging.cpp`) and applied **after**
the global `log_level`, so a CLI `--log_level=info` cannot undo them. Add:

```toml
[log.levels]
gpu = "critical"
```

The log then stays at a few hundred bytes per session. Set `gpu = "error"`
again temporarily if you need to see genuine GPU errors while diagnosing a
rendering problem.

This is a configuration change only — no SDK source patch is involved, so the
SDK tree stays clean.

---

## Command-line equivalents

Every key can be passed as a flag. The two forms below are equivalent:

```
# config file
resolution_scale = 3
video_mode_width = 1280
video_mode_height = 720
vsync = false

# command line
tenchuz.exe --resolution_scale=3 --video_mode_width=1280 --video_mode_height=720 --no-vsync
```

Boolean flags accept `--flag` / `--no-flag`. If you pass both a config file and
flags, the command line wins.

---

## Recommended settings

**Careful / low risk** — better image, no meaningful risk of breaking effects:

```toml
resolution_scale = 2
video_mode_width = 1280
video_mode_height = 720
window_width = 2560
window_height = 1440
video_mode_refresh_rate = 60.0
vsync = true
```

**High quality** — true 4K internal render from a 720p guest mode:

```toml
resolution_scale = 3
video_mode_width = 1280
video_mode_height = 720
window_width = 3840
window_height = 2160
fullscreen = true
video_mode_refresh_rate = 60.0
vsync = true
```

**Maximum** — heavier still, test in gameplay:

```toml
resolution_scale = 4
video_mode_width = 1280
video_mode_height = 720
window_width = 3840
window_height = 2160
fullscreen = true
video_mode_refresh_rate = 60.0
vsync = true
```

### Frame rate: what NOT to do

Do not set `vsync = false` to "uncap the framerate". For Tenchu Z it switches the
guest vblank to a 1000 Hz timer and the game runs far too fast to play.

Do not raise `video_mode_refresh_rate` expecting smoother graphics at normal
speed either — it multiplies game speed (120 Hz = 2x, 240 Hz = 4x).

For normal play, always use:

```toml
video_mode_refresh_rate = 60.0
vsync = true
```

---

## Launcher CLI

The launcher also works without the UI, which is useful in scripts and for
reproducible setups:

```
# write the config and exit, no window
powershell -File tools\launcher\tenchuz_launcher.ps1 -Headless

# apply a preset: <output>-<scale>x
powershell -File tools\launcher\tenchuz_launcher.ps1 -Headless -Preset "720p-3x"
powershell -File tools\launcher\tenchuz_launcher.ps1 -Headless -Preset "1080p-2x"

# point at a game root other than the one containing this tools\ folder
powershell -File tools\launcher\tenchuz_launcher.ps1 -GameRoot "D:\games\tenchu z"
```

The launcher only touches the keys it manages. Comments and any other keys in
`tenchuz.toml` — including `use_fuzzy_alpha_epsilon` — are preserved. It makes a
one-time backup at `tenchuz.toml.launcher.bak` before its first edit.
