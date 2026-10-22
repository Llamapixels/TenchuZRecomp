# Tenchu Z — ReShade post-processing

ReShade **works** with this recompilation. This document records what was
verified, what is not possible and why, so the investigation is not repeated.

Verified against ReShade **6.8.0.2158** (64-bit) on 2026-10-19.

---

## Why ReShade is the supported route for visual effects

ReXGlue has a built-in guest-output post-processing pass
(`src/ui/d3d12/d3d12_presenter.cpp`, `GuestOutputPaintEffect`), but in this SDK
build (`REXGLUE_ENABLE_FIDELITYFX=OFF`) it only offers `bilinear`. Extending it
with custom effects would require patching SDK sources and rebuilding the SDK —
which this project has deliberately avoided.

ReShade hooks the swap chain instead and needs no SDK change at all, so it is
the intended way to add colour post-processing to this title.

---

## Install

1. Run the ReShade installer and point it at:

   ```
   tenchuz-recomp\out\build\win-amd64-release\tenchuz.exe
   ```

2. When asked for the rendering API, choose **Direct3D 12**.

   This is the only correct choice. ReXGlue presents through D3D12
   (`GPU_PLUGINS = xenos`), and the installer deploys a DXGI proxy
   (`dxgi.dll`) either way.

3. Install the effect packages you want.

The installer writes these into the game's executable folder:

| File / folder | Purpose |
|---|---|
| `dxgi.dll` | The ReShade proxy — this *is* the install |
| `reshade-shaders\` | Effect sources and textures |
| `ReShade.ini` | ReShade configuration |
| `ReShadePreset.ini` | Active preset |
| `ReShade.log` | Diagnostic log |

## Uninstall

Delete or rename **`dxgi.dll`** in that folder. That is the entire removal —
ReShade writes nothing to the registry or anywhere else on the system. Renaming
is often preferable, since it disables ReShade without losing your preset and
shader packages.

---

## What was verified

ReShade attaches successfully to the D3D12 swap chain. From `ReShade.log`:

- Hooks installed on `C:\WINDOWS\system32\d3d12.dll`.
- `D3D12CreateDevice` redirected successfully.
- `IDXGIFactory2::CreateSwapChainForHwnd` redirected successfully.
- Swap chain description detected as **1920×1080**, `DXGI_FORMAT_B8G8R8A8_UNORM`,
  3 buffers — matching the launcher's configured window size.
- Effects compile and run.
- **Confirmed working by the user.**

## What does not work: depth-based effects

**Depth effects — depth of field (DoF), SSAO, MXAO — do not work.** They are not
expected to, and there is no setting that fixes this.

ReShade's depth support works by heuristically identifying the game's depth
buffer through the D3D12 resource bindings. That identification fails here, for
two reasons specific to a recompiler:

1. **Guest-managed resources.** The depth target is a resource the *guest* code
   allocates and formats, not one the host API creates in a way ReShade
   recognises.
2. **Unusual formats.** This title uses float24 depth and R16G16B16A16 gamma
   render targets, which are not among the formats ReShade's heuristics scan
   for.

`ReShade.log` contains **no depth-buffer detection lines at all** — ReShade never
found a candidate, so every depth-reading shader has nothing to sample.

Depth-based shaders are therefore permanently unavailable on this stack without
substantial work (exporting a real depth resource in a format ReShade accepts),
which is out of scope.

---

## Effect support summary

| Effect type | Status |
|---|---|
| Colour grading, curves, levels | Works |
| Bloom | Works |
| Sharpening (CAS, LumaSharpen) | Works |
| Blur (non-depth) | Works |
| Film grain, vignette, chromatic aberration | Works |
| SMAA / FXAA / TAA-style AA | Works |
| Ambient occlusion (MXAO, SSAO) | **No** — needs depth |
| Depth of field | **No** — needs depth |
| Ray-traced / screen-space reflections | **No** — needs depth |

## Interaction with the built-in overlays

ReXGlue draws its own ImGui overlays into the same swap chain:

- **F3** — debug overlay (shows `Guest: X.X FPS (X.XX ms)`)
- **`** (backtick) — console overlay

These coexist with ReShade, but both overlays draw into the same swap chain and
both want mouse input, so ReShade's UI (**Home** key) may overlap the F3 overlay
visually. This is cosmetic.

Note that either overlay being open changes what ReShade sees if its shaders are
applied before the overlay is composited — the overlay is *not* part of the
guest image. With overlays closed, only the game image is affected.

---

## Cross-references

- `docs/launcher_options.md` — launcher and runtime option reference.
- Rendering resolution is **not** something ReShade controls. Use
  `resolution_scale` in the launcher for real internal rendering resolution.
