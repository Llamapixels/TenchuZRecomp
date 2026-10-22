#!/usr/bin/env python3
"""Dump raw bytes at a PE RVA (minimal PE parser, no dependencies).

Usage:
    python tools/pe_dump.py <exe> 0x5FBC00 0x5FBE78 --len 32
"""
from __future__ import annotations

import argparse
import struct


def parse_sections(data: bytes):
    e_lfanew = struct.unpack_from("<I", data, 0x3C)[0]
    assert data[e_lfanew : e_lfanew + 4] == b"PE\0\0"
    coff = e_lfanew + 4
    nsec = struct.unpack_from("<H", data, coff + 2)[0]
    opt_size = struct.unpack_from("<H", data, coff + 16)[0]
    opt = coff + 20
    magic = struct.unpack_from("<H", data, opt)[0]
    if magic == 0x20B:  # PE32+
        image_base = struct.unpack_from("<Q", data, opt + 24)[0]
    else:
        image_base = struct.unpack_from("<I", data, opt + 28)[0]
    sect = opt + opt_size
    out = []
    for i in range(nsec):
        o = sect + i * 40
        name = data[o : o + 8].rstrip(b"\0").decode("ascii", "replace")
        vsize, vaddr, rawsize, rawptr = struct.unpack_from("<IIII", data, o + 8)
        out.append((name, vaddr, vsize, rawptr, rawsize))
    return image_base, out


def rva_to_off(sections, rva: int):
    for name, vaddr, vsize, rawptr, rawsize in sections:
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            return name, rawptr + (rva - vaddr)
    return None, None


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("rvas", nargs="+")
    ap.add_argument("--len", type=int, default=32)
    args = ap.parse_args()

    data = open(args.exe, "rb").read()
    base, sections = parse_sections(data)
    print(f"image base 0x{base:X}")
    for name, vaddr, vsize, rawptr, rawsize in sections:
        print(f"  {name:<10} rva 0x{vaddr:08X} vsize 0x{vsize:X} raw 0x{rawptr:X}/{rawsize:X}")
    for r in args.rvas:
        rva = int(r, 16)
        sec, off = rva_to_off(sections, rva)
        if off is None:
            print(f"rva 0x{rva:08X}: <unmapped>")
            continue
        raw = data[off : off + args.len]
        hexs = " ".join(f"{b:02X}" for b in raw)
        print(f"rva 0x{rva:08X} ({sec}) off 0x{off:X}: {hexs}")


if __name__ == "__main__":
    main()
