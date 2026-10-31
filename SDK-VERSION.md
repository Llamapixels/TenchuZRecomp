# SDK Version Record — Tenchu Z Recompilation

## Additional display patches (2026-10-25)

Apply `0010-overlay-layout.patch` and `0011-explicit-video-mode.patch` after
patches 1–9, rebuild/install the SDK, then relink the game to restage both DLLs.
Patch 10 adds `rex_set_overlay_layout` and transforms HUD draws after the final
screen pass on D3D12/Vulkan. NDC and scissors use the visible frame center,
including when the backend creates an oversized synthetic viewport for 2D.
Patch 11 honors explicit `video_mode_width/height` values equal to their defaults,
instead of silently substituting window dimensions. Neither changes timing.
No additional Vulkan shader workaround is shipped: the floor remains unresolved.

Patch `0012-mouse-sensitivity.patch` extends `mnk_sensitivity` to 0.01–100x
and clamps mouse deltas before integer conversion to avoid overflow at high
sensitivity. F6 and the launcher expose the same range. No timing code changes.

## System Link and window patches (2026-10-28)

Apply after patches 1–12, rebuild/install the SDK, then relink the game.

Patch `0013-system-link-session-teardown.patch` (`xam_net.cpp`): on a console,
`XNetUnregisterKey` drops the connections made with that session key. The
runtime only logged it, so after a mission both players waited on a black screen
for the other's TCP connection to close. The title's TCP sockets (from
`connect`/`accept`) are now shut down when the key is unregistered (cvar
`net_unregister_drops_tcp`, default on). `net_trace` also logs `closesocket`,
`shutdown`, connection closes and real `recv` errors.

Patch `0014-message-box-input-and-resize.patch` (`xam_ui.cpp`, `xam_input.cpp`,
`xam_module.cpp`, `window_sdl.cpp/.h`):
- System message boxes ("Cut off from a session") can be answered with
  Enter/Space or controller A, and Left/Right or the d-pad change the
  selection. While one is open, the title gets no controller input, as on a
  console.
- After a window resize, Windows could show the picture offset up and left by
  the window frame until the window moved, so the overlay no longer lined up
  with the mouse. The window is now nudged 1 px and back once the resize ends
  (cvar `window_reposition_after_resize`, default on).

## Pinned SDK

- **Repository**: https://github.com/rexglue/rexglue-sdk
- **Local clone**: `rexglue-sdk` next to this project's folder (or set `REXGLUE_SDK`)
- **Tag**: `Release v0.10.0` (`v0.10.0-2-gc94f5eb`)
- **Commit**: `c94f5eb` ("Release v0.10.0"), recursive submodules
- **CLI version string**: `0.10.0.2-dev.gc94f5eb`
- **Build**: `win-amd64` configure preset with `-DREXGLUE_USE_VULKAN=ON` (Direct3D 12 and Vulkan in one
  `rexgpu-xenos.dll`; the game picks one with `gpu_backend`) + `win-amd64-release` build preset,
  installed to `rexglue-sdk\out\install\win-amd64` (registers in CMake user package registry)

## SDK patches applied

**1. Pixel shader overrides (2026-10-21).** Lets HLSL files replace translated
guest pixel shaders. Saved as `tenchuz-recomp/sdk_patches/0001-shader-overrides.patch`.
To apply it: run `git apply` in the SDK clone, then `tools\build_sdk.bat`, then
copy `rexgpu-xenos.dll` and `rexruntime.dll` from `rexglue-sdk\out\install\win-amd64\bin`
next to `tenchuz.exe`. (The game build only restages those DLLs when the exe relinks.)

| File | Change |
| --- | --- |
| `src/graphics/d3d12/shader_override.{h,cpp}` (new) | `shader_override_dir` cvar; generates `xe_interface.hlsli` from translator metadata (bindless: textures and samplers from the descriptor heaps by the per-draw descriptor indices; bindful: by register); compiles overrides with `d3dcompiler_47.dll` (`ps_5_1`, unbounded descriptor tables enabled); writes override stubs when dumping. |
| `src/graphics/d3d12/pipeline_cache.cpp` | Calls `ApplyShaderOverride` after a pixel shader is translated, and `DumpShaderOverrideStub` when `dump_shaders` is set. |
| `include/rex/graphics/pipeline/shader/shader.h` | `Shader::Translation::ReplaceTranslatedBinary()`. |
| `src/graphics/CMakeLists.txt` | Adds `shader_override.cpp`. |
| `include/rex/graphics/pipeline/shader/dxbc_translator.h`, `src/graphics/pipeline/shader/dxbc_translator.cpp` | Appends `override_user[16][4]` (64 floats) to the system constants, described as four `float4[4]` rdef entries (`xe_override_user0..3`). Translated shaders never read it. |
| `src/graphics/d3d12/command_processor.cpp` | Copies the 16 live values into the system constants on every draw. |
| `src/graphics/plugin_main.cpp` | Exports `rex_shader_override_set_values(const float*, uint32_t)`, which the game finds with `GetProcAddress` (F6 > Shader). |

Inactive unless `shader_override_dir` is set. The game sets it only when an
override folder has `.hlsl` files, and leaves the renderer in its default
bindless mode (bindful mode breaks some of this game's textures). See
`docs/shader_modding.md`.

Everything else game-specific lives in the game project: hooks in
`tenchuz-recomp/src/` and declarative `[entrypoint.functions]` entries in
`tenchuz-recomp/tenchuz_manifest.toml`.

(For reference: the SDK's documented Windows build gotcha — 18 symlinks under
`thirdparty/libmspack/cabextract/mspack/` had to be materialized as real files
before building on Windows. That is a checkout fix, not a source patch.)

## Project generator version

Projects scaffolded by `rexglue init` record `sdk_version = "0.10.0"` in
`<name>_manifest.toml`. Codegen warns if the CLI is newer than the recorded
version (benign).

## Known SDK issues relevant to this title

1. **Function-discovery gap**: functions reachable only
   via data function-pointer tables are never registered (no MSVC RTTI ⇒ vtable
   pass finds nothing) ⇒ runtime fatal "Call to invalid or unregistered function
   at guest address". Declarative fix: `[entrypoint.functions]` entries, derived
   with `tools/derive_fnptr_targets.py` from a decrypted guest image dump.
2. **UnresolvedCall validation errors** for `b`-only branch targets — fixed the
   same way (4 addresses for Tenchu Z).
3. **Over-merged function warning**: function `0x8248D898` is 5.3 MB
   (`max_file_size_bytes` = 1 MiB). May compile slowly or hit compiler limits.

## SDK patch 2: System Link over IP (2026-10-21)

Saved as `tenchuz-recomp/sdk_patches/0002-system-link.patch` (apply after
patch 1, same steps). Changes `xam_net.cpp`, `xsocket.cpp`, `xgi_app.cpp`,
`xlivebase_app.cpp`, `xam_user.cpp` and `user_profile.{h,cpp}`: real XNet
addresses and keys, host session info, `WSAEventSelect`, join by IP
(`net_join_ip`, `net_bind_ip`), a per-PC player id, a network trace
(`net_trace`), and fixes to socket byte order, `WSAWaitForMultipleEvents`
and error codes. Details in `docs/multiplayer.md`.

Later on 2026-10-21 (same patch file, regenerated): `net_public_ip` cvar.
`XNetGetTitleXnAddr` reports it in `inaOnline`. `XNetXnAddrToInAddr` picks
`inaOnline` for machines on other networks, and falls back to the
`net_join_ip` host when a host reports only a private address. The game sets
`net_public_ip` after opening the router ports with UPnP
(`tenchuz-recomp/src/upnp.cpp`).

## SDK patch 3: menu modifier keys (2026-10-21)

Saved as `tenchuz-recomp/sdk_patches/0003-menu-modifier-keys.patch`.
`src/ui/imgui_drawer.cpp` passes Ctrl, Shift, Alt and Win to ImGui as
`ImGuiMod_*` key events. It used to set `io.KeyCtrl` and the like directly,
which ImGui 1.92 overwrites, so Ctrl+V / Ctrl+C / Ctrl+A did nothing in
menu text fields.

Also in patch 2 (2026-10-21): `src/system/xam/content_manager.cpp` keeps the
per-title profile folder at `profile/User` whatever `user_name` is, so a
player name no longer points the game at an empty folder (which crashed it).

## SDK patch 4: vector pack fix (2026-10-22)

Patch 4, `tenchuz-recomp/sdk_patches/0004-vector-pack-aliasing.patch`, fixes
the recompiler (`src/codegen/builders/vector.cpp`): `vpkuwus` and `vpkuhus`
were emitted lane by lane straight into the destination register, so when the
destination is also a source (`vpkuwus v0,v0,v0`) later lanes read already
overwritten input. Tenchu Z builds its multiplayer HUD card sprite sizes this
way, which turned them into 65535 (striped partner cards). They now pack into
`vTemp` and store once.

A recompiler fix only reaches the game after the game code is regenerated.
The game build does not notice a new recompiler, so run, after
`tools\build_sdk.bat`:

```
rexglue codegen --ignore-stamp tenchuz_manifest.toml
```

in `tenchuz-recomp` (rexglue.exe is in `rexglue-sdk\out\install\win-amd64\bin`),
then `tools\build_tenchuz.bat`.

## SDK patch 5: keyboard modifier keys (2026-10-22)

Saved as `tenchuz-recomp/sdk_patches/0005-keyboard-modifiers.patch`
(`src/input/mnk/mnk_input_driver.cpp`). Key binds used to require the held
modifiers to match exactly, so any bind stopped working while Shift, Control
or Alt was held, and a bare modifier could not be bound. Now a bare modifier
("Shift") is a key of its own, and a bare key only gives way to a bind that
spells out the held modifiers with it (the D-pad's "Shift+Up" still wins over
"Up" while Shift is held).

## SDK patch 6: Vulkan texture fixes (2026-10-22)

Saved as `tenchuz-recomp/sdk_patches/0006-vulkan-textures.patch`. Two bugs in the
Vulkan (SPIR-V) shader translator that the Direct3D 12 (DXBC) path does not have:

| File | Change |
| --- | --- |
| `src/graphics/pipeline/shader/spirv_translator_rb.cpp` | The alpha test ignored `use_fuzzy_alpha_epsilon`. On NVIDIA, alpha-tested surfaces came out as noisy pixels (characters). It now compares with the same 1e-3 epsilon as the DXBC path when the setting is on. |
| `src/graphics/pipeline/shader/spirv_translator_fetch.cpp` | The texture result exponent bias was read from bits 13:18 of fetch constant word 4, which is the LOD bias, instead of word 3. Every texture with a LOD bias was scaled by a bogus power of two, usually to near black with alpha 0, so roofs, wooden beams, carts and trees disappeared or looked see-through. It now reads word 3 like the DXBC path. |

## SDK patch 7: DirectInput off by default (2026-10-22)

Saved as `tenchuz-recomp/sdk_patches/0007-directinput-off.patch`
(`src/input/sdl/sdl_input_driver.cpp`). SDL's DirectInput joystick back-end
waits on any device that stops answering (in steps of 20 s), which held start-up
on a black screen for 40 s on one PC. Measured with SDL alone: 40.3 s with
DirectInput, 6 ms without, and the Xbox controller was found either way. New
cvar `input_directinput` (default false) sets `SDL_HINT_JOYSTICK_DIRECTINPUT`
just before the gamepad subsystem starts; F6 > Controller has a checkbox for
older generic pads that need it.

## SDK patch 8: hide the game's 2D overlay (2026-10-22)

Saved as `tenchuz-recomp/sdk_patches/0008-hide-overlay.patch`
(`src/graphics/command_processor.cpp`, `src/graphics/plugin_main.cpp`). New
plugin export `rex_hide_draws_after_shader(uint64_t pixel_shader_hash)`: while
non-zero, the shared command processor skips every ordinary draw after the
first draw with that pixel shader in a frame, until the swap. EDRAM copies
(resolves) still run, so the frame reaches the screen. The game passes its final
screen pass (`822F99ECDB1B0ACD`) to hide the HUD; works on Direct3D 12 and
Vulkan.

## SDK patch 9: Vulkan denormals in Shader Model 3 multiplies (2026-10-22)

Saved as `tenchuz-recomp/sdk_patches/0009-vulkan-denormals.patch`
(`include/rex/graphics/pipeline/shader/spirv_translator.h`,
`src/graphics/pipeline/shader/spirv_translator_alu.cpp`). The Xbox 360 treats
"+0 or a denormal times anything" as 0. The SPIR-V translator tested this with
`min(|a|, |b|) == 0`, which only catches denormals when the host flushes them,
and NVIDIA's Vulkan driver reports `shaderDenormFlushToZeroFloat32 = 0`. New
helper `IsSm3MultiplicandZero` compares with `FLT_MIN` instead when the device
cannot flush denormals; unchanged elsewhere. Used by the vector mul/mad and by
`ZeroIfAnyOperandIsZero` (dot products, scalar multiplies).

Known Vulkan issue (not fixed by this): on NVIDIA the mission 1 walkway shader
`ACFA07437578EA9C` comes out speckled or flat purple. Its inputs were verified
with live probes; the bad value appears between ucode instructions 47 and 67,
and changes with unrelated shader code, so it looks like undefined behaviour in
the driver's compiler. Direct3D 12 bindless (the default) is correct.
