# Modding Tenchu Z (ReXGlue native port)

**Answer: yes.** The game reads its content as loose files from
`extracted\DATA\`, and the ReXGlue VFS mounts that folder directly on the host
filesystem. There is no archive, no signature, and no hash check. Mods are
installed into a separate `mods\` folder (section 6), so the original files are
never edited.

This page records what was verified, what the file formats are, and what is
still unsolved.

---

## 1. Why it works: no validation anywhere in the path

Verified by reading the SDK source:

| Layer | File | Behaviour |
| --- | --- | --- |
| VFS mount | `src/system/runtime.cpp` `Runtime::SetupVfs` (~line 294) | Creates a `HostPathDevice` at `\Device\Harddisk0\Partition1`; registers symlinks `game:` and `d:`. `game_data_root_` becomes the host root. |
| File open | `src/kernel/xboxkrnl/xboxkrnl_io.cpp` (~line 103) | Guest `NtCreateFile` / `NtOpenFile` forwarded to the VFS. |
| File read | `src/filesystem/devices/host_path_file.cpp` | `ReadSync` reads straight from the host file. |

There is **no** signature verification, **no** content hash, **no** archive to
repack, and no allow-list. The only requirement is that the replacement file
sits at the same path with the same name.

---

## 2. Verified: a modified file is honoured

This was tested end-to-end, not inferred.

**Test 1 — file is actually read.**
Modified `extracted\DATA\TEXTURE\ENGLISH\wait.tga` (the loading screen).
Recorded the file's NTFS last-access timestamp before launch, then launched the
game and let it run 90 s:

```
before : Tuesday, October 20, 2026 1:18:08 AM
after  : Tuesday, October 20, 2026 1:19:59 AM
RESULT : FILE WAS READ (access time advanced)
```

(Prerequisite: NTFS last-access updates enabled. Confirmed with
`fsutil behavior query DisableLastAccess` → `DisableLastAccess = 2`
(System Managed, updates enabled). On a machine with them disabled this
technique will not work.)

**Test 2 — modification visibly changes the game (corrupt).**
Corrupted `extracted\DATA\TEXTURE\ENGLISH\title.xpr` — kept the first `0x140`
bytes (header, index, name table) and overwrote everything after with `0xA5`.
Launched and captured the title screen:

| Run | Distinct colours | Dominant | Result |
| --- | --- | --- | --- |
| Original | 5,189 | white + varied content | healthy title screen |
| Corrupted | 65 | uniform grey `16,16,16` | **title screen black** |

The user confirmed it independently: *"title screen broke. its all black."*

**Test 3 — restoring the file restores the game.**
Copied the byte-identical backup back:

```
title.xpr      : D38111B7AD77AC27EE522C28B7FAB40B26FFD1315936CF835B62FF4F83D4BB04
title.xpr.orig : D38111B7AD77AC27EE522C28B7FAB40B26FFD1315936CF835B62FF4F83D4BB04
IDENTICAL - original restored correctly
```

Relaunched: **5,189 distinct colours** again. Fully recovered.

Conclusion: a byte change in `extracted\DATA\` produces a corresponding change
in the running game. The manual modding workflow is therefore functional.

---

## 3. What content is moddable

`extracted\DATA\` is loose files, not packed:

```
DATA\
  CUTSCENE\   EFFECT\   FONT\   MODEL\   MOTION\   MOVIE\
  SET\        SOUND\    TEXTURE\
  *.FXO                 (Xenos shader containers, magic A3 D7 01 41)
  cut.xsb  def.xsb      (audio banks)
  xten.xgs
```

### Textures — `DATA\TEXTURE\`

643 files: **636 `.xpr`** (680 MB), plus `.tga` and `.png` for a few UI items.

The `.tga` and `.png` files are **plain, standard formats** and can be edited
with any image editor:

| File | Format |
| --- | --- |
| `ENGLISH\wait.tga` (+ FR/GE/IT/SP) | 512×256, 32-bit BGRA, uncompressed (TGA type 2) |
| `banner.tga` | 384×128, 32-bit BGRA, uncompressed |
| `saveicn.png` | standard PNG |

Note the TGA files use alpha for compositing — `wait.tga` is mostly alpha `0`
with real content in the alpha `255` regions. Preserve the alpha channel when
editing, or the image will disappear.

The `.xpr` files are **Xbox 360 texture archives** and need tooling (below).

---

## 4. The `.xpr` format (reverse-engineered)

`.xpr` is `XPR2` — an Xbox 360 texture container. It is **big-endian
throughout** (Xbox 360 native byte order). `BitConverter` in .NET is
little-endian and must not be used on this data; every field here must be read
big-endian.

Verified byte-for-byte against `ENGLISH\title.xpr` and `ENGLISH\ames.xpr`:

```
Header (0x10 bytes)
  +0x00  "XPR2"
  +0x04  u32be  length of the (index + name) section in bytes, e.g. 0x800.
                This is NOT an absolute file offset.
  +0x08  u32be  version
  +0x0C  u32be  flags

Index — begins at 0x10, 16 bytes per entry. The true end is where the "TX2D"
magic stops; in practice count = sectionLength / 16.
  +0x00  "TX2D"
  +0x04  u32be  per-texture descriptor size
  +0x08  u32be  payload offset, relative to the index section start (0x34 seen)
  +0x0C  u32be  texture id

Name table — immediately after the index, NUL-separated ASCII, one name per
index entry, in the same order.

Payload — follows the index section.
```

Listing `ENGLISH\title.xpr` with `tools\xpr_tool.ps1 -List` gives:

```
file      : title.xpr (6686732 bytes)
version   : 0x00660000  flags=0x0000000E  indexLen=0x800
entries   : 14  names=14  nameTable@0xF0

[  0] ttl              id=0x00E8
[  1] ttl_anm          id=0x00EC
[  2] ttl_bg           id=0x00F4
[  3] ttl_btn          id=0x00FB
[  4] chmk01           id=0x0103
[  5] kyo_00           id=0x010A
[  6] mode1            id=0x0111   ... mode5
[ 11] mode            id=0x012F
[ 12] tran            id=0x0134
[ 13] tyuui           id=0x0139
```

Contrast with `ENGLISH\ames.xpr`, which has a single `TX2D` chunk — the layout
is consistent across files, the entry count simply varies.

### What is *not* solved

The **pixel payload format inside a `.xpr` is not decoded**. Specifically:

- The `+0x04` "descriptor size" values in the index grow by exactly `0x34` per
  entry (320, 372, 424, 476…), which means they describe a metadata block, not
  the pixel data. Summing them gives 9,212 bytes out of a 6.7 MB file, so they
  are not the real payload sizes.
- The real per-texture descriptor appears to carry the constant `00 00 0D 10`
  (`0x0D10` = 3344), which occurs **exactly 14 times** in `title.xpr` — one per
  texture. Its role is not confirmed.
- Whether the pixels are plain DDS, raw swizzled/tiled Xbox 360 GPU layout, or
  a custom block format is **undetermined**. The SDK has zero `.xpr` support
  (no `XPR2`, `TX2D`, or Xbox texture codec in `src/` or `include/`), so the
  guest parses this format itself and there is no host-side decoder to consult.

Replacing a `.xpr` therefore currently means **supplying a whole known-good
`.xpr`**, not authoring edited pixels. Editing an individual texture inside one
still requires finishing this decode.

---

## 5. Tools

Both are plain PowerShell, no dependencies.

### `tools\xpr_tool.ps1`

Lists or extracts the textures in an `.xpr` archive.

```powershell
# List entries (names, ids, sizes)
powershell -ExecutionPolicy Bypass -File tools\xpr_tool.ps1 `
    -Path "extracted\DATA\TEXTURE\ENGLISH\title.xpr" -List

# Extract payloads
powershell -ExecutionPolicy Bypass -File tools\xpr_tool.ps1 `
    -Path "extracted\DATA\TEXTURE\ENGLISH\title.xpr" -Extract -OutDir out\title
```

Index and name parsing are verified correct. Payload extraction boundaries are
provisional pending the format decode above.

### `tools\xpr_chunks.ps1`

Walks the chunk chain and dumps header words — the diagnostic that produced the
layout above. Useful if you continue the format work.

---

## 6. Installing mods: the `mods\` folder

Mods go in a `mods` folder next to `tenchuz.exe`. Each mod is one folder that
mirrors the game folder:

```
mods\
  HD Title\
    DATA\TEXTURE\ENGLISH\title.xpr   <- replaces extracted\DATA\TEXTURE\ENGLISH\title.xpr
  20_Other Mod\
    DATA\...
```

- The game files in `extracted\` are never modified. Delete or rename a mod
  folder to remove it.
- Mods load in alphabetical order; when two mods replace the same file, the one
  later in the alphabet wins. Prefix names with numbers to set the order.
- A folder starting with `_` or `.` is ignored: rename `HD Title` to
  `_HD Title` to disable it.
- Mods can replace files and add new ones (names are case-insensitive).
- The mod list is read when the game starts. Restart after changing mods.
- The launcher has a **Load mods** checkbox and an **Open mods folder** button.
  The setting is `mods_enabled` in `tenchuz.toml`.
- A mod's `shaders\` folder is not part of the game folder: it holds pixel
  shader overrides (`<HASH>.ps.hlsl`), usually in preset subfolders that show
  up in F6 > Shader > Preset. See `docs/shader_modding.md`.

The log (`logs\tenchuz.log`) lists each mod with how many files it replaces:

```
mods: HD Title - 1 file(s), 1 replacing game files, 0 shader(s)
mods: 1 mod(s) active (game:, d: now resolve through \Device\TenchuzMods)
```

A mod that replaces nothing gets a warning, which almost always means its
folders do not start at `DATA\`.

### How it works

`tenchuz-recomp\src\mod_loader.cpp`, game-side, no SDK change. Before the game
starts, it mounts a second device whose directory tree merges every mod with the
game folder, and repoints the `game:` / `d:` links at it. A merged *tree* is
required rather than a device that tries each folder in turn, because the VFS
opens a file by resolving its parent directory and then looking the name up in
that directory's own children. Verified with a harness that mounts the real game
folder exactly as `Runtime::SetupVfs` does: modded files open from `mods\`,
untouched files from `extracted\`, new files are found, directory listings are
merged, and `_`-prefixed folders are skipped.

---

## 8. Summary

| Question | Answer |
| --- | --- |
| Can you mod the game? | Yes — loose files, no validation, no repacking. |
| Verified? | Yes — last-access timestamps, a visibly broken title screen, and a clean byte-identical restore. |
| Easy for UI textures? | Yes — `.tga` and `.png` are unobfuscated standard formats. |
| Easy for `.xpr` textures? | Not yet — the container is parsed but the pixel payload format is undecoded. |
| Non-destructive mod layer? | Yes - the `mods\` folder (section 6). |
