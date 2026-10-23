# Building from source (Windows)

You need your own *Tenchu Z* disc image (USA/Europe), as for playing. The repository contains no game code: the
recompiler generates it on your machine from your own `default.xex`, into `tenchuz-recomp/generated/` (ignored by
git; never share it).

## Requirements

- Windows 10 or 11, x64.
- Visual Studio 2022 Build Tools with the C++ workload and its Clang/LLVM component (the scripts use
  `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools`; edit `VSWD` in the two `.bat` files if yours is
  elsewhere).
- Python 3 (only for some of the analysis tools in `tools/`).
- About 15 GB free disk space.

## 1. The ReXGlue SDK

1. Clone [rexglue-sdk](https://github.com/rexglue/rexglue-sdk) **next to this repository's folder** (or set the
   `REXGLUE_SDK` environment variable to its path), at the commit in [SDK-VERSION.md](../SDK-VERSION.md), with its
   submodules:
   ```
   git clone --recursive https://github.com/rexglue/rexglue-sdk
   cd rexglue-sdk
   git checkout c94f5eb
   git submodule update --init --recursive
   ```
2. Apply this project's patches, in order:
   ```
   git apply ..\TenchuZRecomp\tenchuz-recomp\sdk_patches\0001-shader-overrides.patch
   git apply ..\TenchuZRecomp\tenchuz-recomp\sdk_patches\0002-system-link.patch
   git apply ..\TenchuZRecomp\tenchuz-recomp\sdk_patches\0003-menu-modifier-keys.patch
   git apply ..\TenchuZRecomp\tenchuz-recomp\sdk_patches\0004-vector-pack-aliasing.patch
   git apply ..\TenchuZRecomp\tenchuz-recomp\sdk_patches\0005-keyboard-modifiers.patch
   git apply ..\TenchuZRecomp\tenchuz-recomp\sdk_patches\0006-vulkan-textures.patch
   git apply ..\TenchuZRecomp\tenchuz-recomp\sdk_patches\0007-directinput-off.patch
   git apply ..\TenchuZRecomp\tenchuz-recomp\sdk_patches\0008-hide-overlay.patch
   git apply ..\TenchuZRecomp\tenchuz-recomp\sdk_patches\0009-vulkan-denormals.patch
   ```
   (adjust the path to where you cloned this repository).
3. Configure once with the SDK's `win-amd64` preset and Vulkan turned on
   (`cmake --preset win-amd64 -DREXGLUE_USE_VULKAN=ON`; the SDK builds only Direct3D 12 on Windows by default), then build and install it with `tools\build_sdk.bat` from this
   repository. On Windows, the symlinks under `thirdparty/libmspack/cabextract/mspack/` must be real files before
   the first build (see SDK-VERSION.md).

## 2. Your game files

Put the extracted game in `extracted/` at the root of this repository: either run the release's
`Setup Tenchu Z.cmd` against your `.iso` and copy its `extracted` folder here, or
`python tools\extract_xdvdfs.py <your.iso> extracted`.

## 3. Generate and build

1. Generate the game code from your `default.xex` (from `tenchuz-recomp\`):
   ```
   ..\..\rexglue-sdk\out\install\win-amd64\bin\rexglue.exe codegen tenchuz_manifest.toml
   ```
   Add `--ignore-stamp` after changing the recompiler (it doesn't notice a new SDK by itself).
2. Build: `tools\build_tenchuz.bat`. The output is in `tenchuz-recomp\out\build\win-amd64-release\`. The first build
   takes a while (the generated code is large).
3. Copy `rexruntime.dll` and `rexgpu-xenos.dll` from `rexglue-sdk\out\install\win-amd64\bin` next to `tenchuz.exe`
   after rebuilding the SDK (the game build only copies them when the exe relinks).
4. Run `tenchuz-recomp\out\build\win-amd64-release\tenchuz.exe`; it finds `extracted\` at the repository root. Copy
   `extras\shader_overrides` next to it for the shader presets.

## Release zip

`powershell -ExecutionPolicy Bypass -File tools\package_release.ps1` builds `release\` and the zip (no game files;
it refuses to include any). The version comes from `tenchuz-recomp/src/version.h`.

## Where things are

- `tenchuz-recomp/src/`: the game-specific code (hooks, F6 menu, online, mods, shader presets, setup paths).
- `tenchuz-recomp/tenchuz_manifest.toml`: function boundaries and entry points for the recompiler.
- `tenchuz-recomp/sdk_patches/`: changes to the ReXGlue SDK (shader overrides, System Link over IP, menu keys,
  a recompiler fix, keyboard modifier keys, Vulkan shader fixes, DirectInput off, HUD hiding).
- `tools/`: build, release, setup and analysis scripts.
- `docs/`: guides; `docs/development-notes.md` has older development notes.
