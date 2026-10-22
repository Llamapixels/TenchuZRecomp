# SDK Version Record — Tenchu Z Recompilation

## Pinned SDK

- **Repository**: https://github.com/rexglue/rexglue-sdk
- **Local clone**: `rexglue-sdk` next to this project's folder (or set `REXGLUE_SDK`)
- **Tag**: `Release v0.10.0` (`v0.10.0-2-gc94f5eb`)
- **Commit**: `c94f5eb` ("Release v0.10.0"), recursive submodules
- **CLI version string**: `0.10.0.2-dev.gc94f5eb`
- **Build**: `win-amd64` configure preset + `win-amd64-release` build preset,
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
