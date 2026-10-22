# Shader modding (pixel shader overrides)

You can replace any of the game's pixel shaders with your own HLSL. The game
still draws exactly what it drew before (same geometry, same textures, same
constants), but your code decides the colour of every pixel. Typical uses:
colour grading per material, better bloom or tone mapping, sharper texture
filtering, debug views, or "modern lighting" on surfaces whose inputs (normals,
light vectors) the original shader already receives.

Status: working and verified (title screen shader replaced with a greyscale
version; a faithful re-implementation matched the original within animation
noise). Requires the patched SDK described in `SDK-VERSION.md`.

---

## 1. Quick start

1. **Dump the shaders.** In the launcher, tick **Dump shaders for shader
   modding** and play to the scene you want to change. Every shader the game
   uses is written to `shader_dump\` next to `tenchuz.exe`. Untick it again
   afterwards; dumping slows down the first time each shader is seen.
2. **Find your shader.** Pixel shaders are the `shader_<HASH>.ps.*` files. For
   each one there is:
   - `shader_<HASH>.ucode.frag` — the game's original Xbox 360 microcode
     (readable assembly: `tfetch2D` = texture read, `mul`/`mad`/`dp3` = maths,
     `oC0` = output colour).
   - `shader_<HASH>.ps.stub.hlsl` — a starting point for your override.
   - `shader_<HASH>.ps.interface.hlsli` — everything this shader can read.
   Sort the folder by date: shaders that appear when you enter a scene are the
   ones that scene uses. A quick way to identify one is to copy its stub
   unchanged (it draws magenta or the first texture) and see what changes.
3. **Install the override.** Copy the stub into a preset folder (section
   "Presets" below), e.g. `shader_overrides\My Look\<HASH>.ps.hlsl`, and pick
   that preset in F6 > Shader. A file placed directly in
   `shader_overrides\` (or a mod's `shaders\`) is not part of any preset and
   always applies. The `shader_` prefix may be kept or dropped.
4. **Edit and restart.** Shaders are compiled when the game first uses them;
   restart the game after every change. Compile errors are written to
   `logs\tenchuz.log` and the game falls back to the original shader, so a
   broken override never crashes the game.

The log confirms what was loaded:

```
shader overrides: 1 folder(s) [...\mods\My Lighting\shaders]
shader override applied: 9567C79307ACC6F5 (modification 0000000000000003) from ...
```

One shader can be applied more than once (different *modifications*: the same
game shader used with different render states). That is normal.

### Presets

A preset is a folder of overrides that replace shaders together: one "look".
Presets live in `shader_overrides\<Preset>\`, or in an enabled mod's
`shaders\<Preset>\`. **F6 > Shader > Preset** lists every preset found plus
**Original** (the game's own shaders); the choice is saved as `shader_preset`
in `tenchuz.toml`. Switching needs a restart, and the tab has a **Restart now**
button for it.

A preset can add live sliders with a `preset.ini` next to its shaders:

```ini
description = One line shown under the selector.
slider   = key | Label | min | max | default | Tooltip
checkbox = key | Label | default | Tooltip
```

Up to 40 entries; the n-th one is `XeUser(n)` in the preset's HLSL (counting
from 0), shared by all the preset's shader files. The player's values are
saved per preset in `shader_settings.ini` next to `tenchuz.exe`. **Effects
on** (shared by all presets) shows the original image without unloading the
preset.

### Values the game passes to every shader

| `XeUser(n)` | Value |
| --- | --- |
| 0-39 | The active preset's sliders, in `preset.ini` order (0 if it has fewer). |
| 40-41 | Player's chest on screen (0..1, top left = 0, 0). |
| 42 | Player's view depth (world units in front of the camera). |
| 43 | 1 when the player values are valid (player on screen). |
| 48-50 | Camera eye, world space. |
| 51-53 | Camera forward (unit vector). |
| 54-56 | Camera right × tan(horizontal half field of view). |
| 57-59 | Camera up × tan(vertical half field of view). |
| 60 | Time in seconds (wraps every hour). |
| 61 | 1 when the camera values are valid. |
| 63 | 2 when **Effects on** is unticked, otherwise 1 when the sliders are live and 0 when there are none (no `preset.ini`, or an older build). Fall back to your own defaults when it is 0. |

With these a shader can rebuild the world position of a pixel from its
normalized device coordinates (x, y) and its view depth d (the `w` of a
projected position):

```hlsl
float3 world = eye + d * (forward + x * right + y * up);
```

The world uses +Y up, in roughly centimetre units. The Modern Look puddle
shader (`79481E88F9E67594.ps.hlsl`) is a working example; its ripples stay
fixed on the ground.

### Included presets

Both are in `extras\shader_overrides\`; copy the folders into
`shader_overrides\`. Both replace the game's final screen pass
(`822F99ECDB1B0ACD`), which mixes the frame with the game's blur, depth of
field and bloom, keep all of that, and have a split-screen compare checkbox
(original on the left).

- **Modern Look**: sharpening, clarity (local contrast), bloom built from
  the frame, a highlight roll-off, colour grading (exposure, contrast,
  saturation, vibrance, warmth), a vignette and dithering, plus depth
  effects: ambient occlusion, atmospheric haze, depth of field and light
  shafts. The final pass's `tf3` holds about k / view depth (large up close);
  the shader measures k each frame at the player, whose distance the game
  passes in (`XeUser(40..43)`). It also replaces
  the puddle water shader (`79481E88F9E67594`): animated ripples fixed in
  the world, ripple distortion of the reflection, stronger reflection at
  shallow angles, sharp glints and a depth tint, each with a slider. Rivers
  use the game's ordinary surface shaders (their ripples are an animated
  texture), so they are not affected.
- **Manga**: ink outlines (edge detection), flat tone bands, halftone
  screentone dots and paper colour. The **Colour** slider goes from black and
  white manga (0) to a colour comic (1).

---

### Shaders identified so far

| Hash | What it draws |
| --- | --- |
| `822F99ECDB1B0ACD` | Final screen pass: frame + blur, depth of field and the game's bloom. |
| `79481E88F9E67594` | Puddle water (reflection, view into the water, highlight). Vertex shader `69BE68CC048883BA` animates waves. |
| `9DE1F1A77BB889B8` | Fire and similar effect sprites. |
| `9567C79307ACC6F5` | Title screen, menus, HUD pieces (texture × colour). |
| `1E70EB9513D670C9` | Full-screen copy of the frame (affects everything). |
| `9E6DE1F938C5E159`, `CF3DDB085FFDB554`, `1EC7360D9F13ADAF` | Generic textured, vertex-lit, shadowed surfaces (not used in the river mission). |
| `0E1ADC5527A33888`, `106EF3DFCDD53BCA`, `9B8A639AAA10300F`, `CA7AE348E00E19DF`, `CD6F0EED50078733` | Blur filters (post-processing, shadows). |

The river's shader is not identified yet; it is probably drawn additively,
so colour tests on the shaders behind it mix with it. The game draws in HDR:
test colours need large values (e.g. 50) to show, and only exact 0 stays
black.

---

## 2. Writing an override

Every override starts with:

```hlsl
#include "xe_interface.hlsli"

XeOutput main(XeInput input) {
  XeOutput output;
  ...
  output.oC0 = XeOutputColor(0, color);
  return output;
}
```

`xe_interface.hlsli` is generated by the game for each shader (you do not ship
it; the `.ps.interface.hlsli` file in the dump is a copy for reading). It
matches the original shader exactly, so your shader slots into the same
pipeline.

Example — the title screen in greyscale (the original microcode was
`tfetch2D r0, r0.xy, tf0` then `mul oC0, r0, r1`):

```hlsl
#include "xe_interface.hlsli"

XeOutput main(XeInput input) {
  XeOutput output;
  float4 color = XeSample2D(0, input.r0.xy) * input.r1;
  float grey = dot(color.rgb, float3(0.299, 0.587, 0.114));
  output.oC0 = XeOutputColor(0, float4(grey, grey, grey, color.a));
  return output;
}
```

### What the interface gives you

| Name | Meaning |
| --- | --- |
| `input.r0`, `input.r1`, … | The interpolators the vertex shader passes (the original's `r0`, `r1`…: texture coordinates, colours, normals, whatever the game put there). Read the microcode to see which is which. |
| `input.xe_position` | `SV_Position` (pixel position, scaled by the render scale). |
| `input.xe_front_face` | `SV_IsFrontFace`. |
| `input.xe_point_coord` | Point sprite coordinate, only if the original used one. |
| `output.oC0` … `oC3` | Colour outputs; only the ones the original writes exist. |
| `C<n>` (e.g. `C12`) | Guest float constant `c<n>` as a `float4`, for every constant the original reads. If the original indexes constants dynamically, use `XeC(n)` instead. |
| `XeBool(n)`, `XeLoop(n)`, `XeFetchDword(fc, dword)` | Guest bool / loop constants, and dword 0-5 of fetch constant `fc` (texture size, format, etc.). |
| `XeTexture2D_fc<n>`, `XeSampler_fc<n>` | Texture and sampler bound to guest fetch constant `n` (the `tf<n>` in the microcode). Also available as `XeTexture<i>` / `XeSampler<i>`. |
| `XE_SHADER_HASH`, `XE_ORIGINAL_WRITES_DEPTH` | Information about the original shader. |
| `XeUser(0)` … `XeUser(63)` | Live values set by the game, the same for every override and every draw: the preset's sliders, the camera and time. See "Values the game passes to every shader" above. |

Helpers:

| Helper | Use |
| --- | --- |
| `XeSample2D(fc, uv)` | Samples a 2D texture like the original `tfetch2D`, including the game's sign / gamma conversion. `fc` must be a literal number. |
| `XeOutputColor(target, c)` | **Pass every colour you write through this.** Applies the game's alpha test, the render target's exponent bias and gamma conversion, exactly as the original did. Skipping it gives wrong brightness or broken cut-outs (foliage, fences, text). |
| `XePWLGammaToLinear`, `XeLinearToPWLGamma` | The Xbox 360 piecewise-linear gamma curve. |
| `XeApplyTextureSigns(fc, v)` | The conversion `XeSample2D` applies, for when you sample a texture yourself (e.g. `SampleLevel`, `Gather`). |
| `XeAlphaTest(a)` | The alpha test on its own (already done by `XeOutputColor(0, …)`). |

You can also `#include` your own files; paths are relative to the folder the
override is in, so a mod can share code between shaders.

Overrides are compiled as **Shader Model 5.1** (`ps_5_1`) with full
optimisation by Windows' own `d3dcompiler_47.dll`. Any HLSL that works in
SM 5.1 works here.

---

## 3. Rules and limitations

- **Pixel shaders only.** Vertex shaders cannot be replaced (yet), so an
  override can only use the data the original vertex shader already passes.
  For example, a surface whose shader gets no normal cannot be given
  per-pixel lighting.
- **Restart to reload.** Editing a file needs a restart; values passed
  through `XeUser(n)` change live.
- **Mods must be enabled.** `mods\<Mod>\shaders\` is only read when mods are
  on (launcher **Load mods**, `mods_enabled = true`). `shader_overrides\` is
  always read. When the same shader is overridden twice, the active preset's
  folders win over loose files, `shader_overrides\` wins over mods, and among
  mods the one later in the alphabet wins (the same order as file mods).
- **Turn overrides off** with `shader_overrides_enabled = false` in
  `tenchuz.toml` (no folders are read and the game runs as before).
- **Resource binding modes.** Overrides work in both of the renderer's
  modes; the generated interface matches whichever is active. The game keeps
  the default (bindless) mode: the other one (`d3d12_bindless = false`)
  breaks some of this game's textures, such as purple floors in mission 1.
- **Depth output and alpha-to-coverage** are not replicated. The handful of
  original shaders that write depth (`XE_ORIGINAL_WRITES_DEPTH 1`) should be
  left alone or rewritten to not need it.
- **Render scale.** `xe_position` is in scaled pixels (e.g. ×3 at 3x render
  scale). Texture coordinates are not affected.
- **Signed textures.** `XeSample2D` applies the game's biased (`*2-1`) and
  gamma channel conversions like the original, but always reads the unsigned
  view of the texture. A texture the game reads as signed would come out
  wrong; none has been seen so far, but compare against the original if a
  surface looks off.

---

## 4. Cost

An override costs exactly what its HLSL costs. A shader that does the same
work as the original runs at the same speed; the compile happens once, when
the shader is first seen (a few milliseconds, visible at most as a single
hitch). Heavy effects (many texture reads per pixel, loops) cost GPU time like
in any other game, multiplied by the render scale's pixel count.

---

## 5. How it works (for SDK maintainers)

Implemented in the SDK, `src/graphics/d3d12/shader_override.{h,cpp}`, hooked
into `D3D12PipelineCache` right after a pixel shader is translated
(`pipeline_cache.cpp`). The full change is saved as
`tenchuz-recomp\sdk_patches\0001-shader-overrides.patch`.

- The cvar `shader_override_dir` holds a `;`-separated list of folders. The
  game (`tenchuz_app.h`, `SetUpShaderOverrides`, and `shader_settings.cpp`)
  fills it at startup with the active preset's folder in `shader_overrides\`
  and each enabled mod's `shaders\`, then those roots themselves, keeping
  only folders that contain `.hlsl` files. If none do, the runtime is left
  untouched.
- For each translated pixel shader the SDK looks for `<HASH>.ps.hlsl` or
  `shader_<HASH>.ps.hlsl` (hash = the guest microcode hash, the same one in the
  dump file names). If found it generates `xe_interface.hlsli` from the
  translator's metadata (interpolator mask, constant usage, texture and
  sampler bindings, system constant layout), compiles the file with
  `D3DCompile` and swaps the translation's DXBC via
  `Shader::Translation::ReplaceTranslatedBinary`.
- The root signature is unchanged; the override must declare only resources
  the original used, which the interface guarantees.
- With `dump_shaders` set, `DumpShaderOverrideStub` writes the stub and a copy
  of the interface next to the usual dump files.
