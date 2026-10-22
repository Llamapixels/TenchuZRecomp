#!/usr/bin/env python3
"""Extract the XDVDFS game partition from an Xbox 360 ISO dump.

Handles both XDVDFS flavors:
  * "360 native":    big-endian fields, UTF-16BE names, u32 left/right,
                     volume descriptor at partition_base + 0x8000.
  * "repacked/LE":   little-endian fields, ASCII names, u16 left/right,
                     0xFF padding to 4-byte entry alignment, descriptor
                     located at data_base + 0x10000 (empirically calibrated
                     against the Tenchu Z USA/Europe dump: entry start
                     sectors resolve with data_base = descriptor - 0x10000).

Usage:
    python extract_xdvdfs.py <iso> <out_dir> [--list-only]
"""

from __future__ import annotations

import argparse
import mmap
import os
import struct
import sys

MAGIC = b"MICROSOFT*XBOX*MEDIA"
SECTOR = 2048
ATTR_DIR = 0x10


class Volume:
    def __init__(self, mm: mmap.mmap):
        self.mm = mm
        idx = mm.find(MAGIC)
        if idx < 0:
            sys.exit("XDVDFS volume descriptor magic not found")
        if mm.find(MAGIC, idx + 1) != idx + 0x7EC:
            # real descriptors carry a trailing magic 0x7EC bytes later
            print("warning: trailing descriptor magic not where expected")
        self.vd = idx

        # Try flavor/base combinations until the root directory parses
        # plausibly (first entry has a sane ASCII/UTF-16 name + known attrs).
        candidates = [
            (">", 2, idx - 0x8000, "360 native (BE/UTF-16)"),
            ("<", 2, idx - 0x10000, "repacked LE (u16 links)"),
            ("<", 4, idx - 0x10000, "repacked LE (u32 links)"),
            ("<", 2, idx - 0x8000, "repacked LE, base-0x8000"),
        ]
        for endian, linksize, data_base, label in candidates:
            if data_base < 0:
                continue
            try:
                root_sector, root_size = struct.unpack_from(
                    endian + "II", mm, idx + 0x14)
                root_off = data_base + root_sector * SECTOR
                if not (0 < root_size <= 0x1000000
                        and root_off + root_size <= len(mm)):
                    continue
                self.endian, self.linksize = endian, linksize
                self.data_base = data_base
                self.root_sector, self.root_size = root_sector, root_size
                buf = mm[root_off: root_off + root_size]
                if self._valid_entry(buf, 0):
                    self.label = label
                    print(f"flavor: {label}")
                    print(f"  descriptor @ {idx:#x}, data base {data_base:#x}")
                    print(f"  root dir: sector {root_sector} -> {root_off:#x}, "
                          f"size {root_size}")
                    return
            except struct.error:
                continue
        sys.exit("could not decode volume descriptor with any known flavor")

    # --- entry parsing ----------------------------------------------------
    def _u(self, buf: bytes, off: int) -> int:
        fmt = self.endian + ("H" if self.linksize == 2 else "I")
        return struct.unpack_from(fmt, buf, off)[0]

    def _valid_entry(self, buf: bytes, off: int) -> bool:
        try:
            left = self._u(buf, off)
            right = self._u(buf, off + self.linksize)
            start, size = struct.unpack_from(self.endian + "II", buf,
                                             off + 2 * self.linksize)
            attrs = buf[off + 2 * self.linksize + 8]
            nlen = buf[off + 2 * self.linksize + 9]
        except struct.error:
            return False
        if attrs not in (0x00, 0x10, 0x80) or nlen == 0 or nlen > 255:
            return False
        name = buf[off + 2 * self.linksize + 10:
                   off + 2 * self.linksize + 10 + nlen]
        try:
            decoded = (name.decode("utf-16-be") if self.endian == ">"
                       else name.decode("ascii"))
        except UnicodeDecodeError:
            return False
        return all(32 <= ord(c) < 127 for c in decoded)

    def read_dir(self, sector: int, size: int) -> list[tuple[str, int, int, int]]:
        """Return (name, start_sector, file_size, attrs) for the directory
        occupying [data_base + sector*2048, +size)."""
        off = self.data_base + sector * SECTOR
        buf = self.mm[off: off + size]
        out: list[tuple[str, int, int, int]] = []
        hdr = 2 * self.linksize + 10  # left/right + start + size + attrs + nlen
        visited: set[int] = set()

        def walk(entry_off: int, depth: int) -> None:
            if depth > 64 or entry_off + hdr > len(buf) or entry_off in visited:
                return
            visited.add(entry_off)
            left = self._u(buf, entry_off)
            right = self._u(buf, entry_off + self.linksize)
            start, fsize = struct.unpack_from(
                self.endian + "II", buf, entry_off + 2 * self.linksize)
            attrs = buf[entry_off + 2 * self.linksize + 8]
            nlen = buf[entry_off + 2 * self.linksize + 9]
            raw = buf[entry_off + hdr: entry_off + hdr + nlen]
            name = (raw.decode("utf-16-be", errors="replace")
                    if self.endian == ">" else raw.decode("ascii", "replace"))
            # NOTE: subtree offsets are relative to the start of the
            # directory block (not to the entry) in both flavors.
            if left:
                walk(left * 4, depth + 1)
            if (0 < nlen <= 255 and name and attrs in (0x00, 0x10, 0x80)
                    and all(32 <= ord(c) < 127 for c in name)):
                out.append((name, start, fsize, attrs))
            else:
                print(f"  WARNING: invalid dir entry at block+{entry_off:#x} "
                      f"(raw {buf[entry_off:entry_off + hdr + 8].hex()})",
                      flush=True)
            if right:
                walk(right * 4, depth + 1)

        walk(0, 0)
        return out


def extract(vol: Volume, out_dir: str) -> tuple[int, int]:
    files = 0
    total = 0

    def rec(sector: int, size: int, rel: str) -> None:
        nonlocal files, total
        entries = vol.read_dir(sector, size)
        if not entries:
            print(f"  WARNING: empty/unreadable dir block {rel!r} "
                  f"(sector {sector})", flush=True)
        for name, start, fsize, attrs in entries:
            if name in (".", "..") or not name:
                continue
            name = name.replace("/", "_").replace("\\", "_")
            # some repacked trees contain stray NUL/control bytes in names
            name = "".join(c for c in name if c >= " " and c != "\x7f").strip()
            if not name:
                print(f"  WARNING: skipping unnamed entry (sector {start}, "
                      f"size {fsize}) in {rel!r}", flush=True)
                continue
            dst = os.path.join(out_dir, rel, name)
            if attrs & ATTR_DIR:
                os.makedirs(dst, exist_ok=True)
                rec(start, fsize, os.path.join(rel, name))
            else:
                os.makedirs(os.path.dirname(dst) or out_dir, exist_ok=True)
                src = vol.data_base + start * SECTOR
                remaining = fsize
                with open(dst, "wb") as fh:
                    while remaining > 0:
                        chunk = vol.mm[src: src + min(remaining, 8 << 20)]
                        if not chunk:
                            break
                        fh.write(chunk)
                        src += len(chunk)
                        remaining -= len(chunk)
                files += 1
                total += fsize
                if files % 100 == 0:
                    print(f"  ... {files} files, {total / 1e9:.2f} GB",
                          flush=True)

    rec(vol.root_sector, vol.root_size, "")
    return files, total


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("iso")
    ap.add_argument("out_dir")
    ap.add_argument("--list-only", action="store_true")
    args = ap.parse_args()

    with open(args.iso, "rb") as fh:
        mm = mmap.mmap(fh.fileno(), 0, access=mmap.ACCESS_READ)
        vol = Volume(mm)
        if args.list_only:
            for name, start, fsize, attrs in vol.read_dir(vol.root_sector,
                                                          vol.root_size):
                kind = "DIR " if attrs & ATTR_DIR else "FILE"
                off = vol.data_base + start * SECTOR
                print(f"  {kind} {off:#012x} {fsize:>12}  {name}")
        else:
            os.makedirs(args.out_dir, exist_ok=True)
            files, total = extract(vol, args.out_dir)
            print(f"extracted {files} files, {total / 1e9:.2f} GB "
                  f"-> {args.out_dir}")
        mm.close()


if __name__ == "__main__":
    main()
