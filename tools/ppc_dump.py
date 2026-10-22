#!/usr/bin/env python3
"""Tiny PowerPC (Xenon) instruction dumper for the decrypted guest image.

Decodes the handful of opcodes needed to reason about function-pointer
materialization (addis/addi/ori, branches, mtctr/bctrl) and prints a window of
instructions around one or more addresses.

Usage:
    python tools/ppc_dump.py 0x824805B0 [0x...] [--back 8] [--fwd 12]
"""
from __future__ import annotations

import argparse
import struct

BASE = 0x82000000
IMAGE = r"C:\Emulation\360\recomps\tenchu z\tools\tenchuz_decrypted.bin"

EXEC_SECTIONS = [
    (".text", 0x82270000, 0x599DDC),
    (".embsec_a", 0x82809E00, 0x99AC),
    (".embsec_b", 0x82813800, 0xA4B8),
    (".embsec_c", 0x8281DE00, 0x1D0C),
    (".embsec_d", 0x8281FC00, 0x5C80),
    (".embsec_e", 0x82825A00, 0x1350),
    (".embsec_f", 0x82826E00, 0xCC8),
    (".embsec_g", 0x82827C00, 0x21B8),
]


def sign16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def section_of(addr: int) -> str:
    for name, va, sz in EXEC_SECTIONS:
        if va <= addr < va + sz:
            return name
    return "?"


def decode(w: int) -> str:
    op = w >> 26
    rD = (w >> 21) & 31
    rA = (w >> 16) & 31
    rB = (w >> 11) & 31
    imm = w & 0xFFFF
    if w == 0x60000000:
        return "nop"
    if w == 0x4E800020:
        return "blr"
    if w == 0x4E800420:
        return "bctr"
    if w == 0x4E800421:
        return "bctrl"
    if w == 0x4E800421:
        return "bctrl"
    if (w >> 16) == 0x7C08 and (w & 0xFFFF) == 0x03A6:
        return f"mtlr r{rD}"
    if (w >> 16) == 0x7C09 and (w & 0xFFFF) == 0x03A6:
        return f"mtctr r{rD}"
    if op == 14:  # addi
        return f"addi r{rD}, r{rA}, {sign16(imm)}"
    if op == 15:  # addis
        return f"addis r{rD}, r{rA}, 0x{imm:04X}"
    if op == 24:  # ori
        return f"ori r{rA}, r{rD}, 0x{imm:04X}"
    if op == 25:  # oris
        return f"oris r{rA}, r{rD}, 0x{imm:04X}"
    if op == 18:  # b
        off = w & 0x03FFFFFC
        if off & 0x02000000:
            off -= 0x04000000
        return f"b 0x{w & 2 and 900:04X}" if False else f"b {off:+d} (lk={w & 1})"
    if op == 16:  # bc
        return f"bc bo=0x{(w >> 21) & 0x1F:02X} bi=0x{(w >> 16) & 0x1F:02X} lk={w & 1}"
    if op == 32:  # lwz
        return f"lwz r{rD}, {sign16(imm)}(r{rA})"
    if op == 34:  # lbz
        return f"lbz r{rD}, {sign16(imm)}(r{rA})"
    if op == 40:  # lhz
        return f"lhz r{rD}, {sign16(imm)}(r{rA})"
    if op == 36:  # stw
        return f"stw r{rD}, {sign16(imm)}(r{rA})"
    if op == 44:  # sth
        return f"sth r{rD}, {sign16(imm)}(r{rA})"
    if op == 38:  # stb
        return f"stb r{rD}, {sign16(imm)}(r{rA})"
    if op == 21:  # rlwinm
        return f"rlwinm r{rA}, r{rD}, ..."
    if op == 37:  # stwu
        return f"stwu r{rD}, {sign16(imm)}(r{rA})"
    if op == 31:
        xo = (w >> 1) & 0x3FF
        names = {266: "add", 40: "subf", 444: "or", 316: "xor", 28: "and",
                 792: "sraw", 467: "mtspr", 339: "mfspr", 23: "lwzx",
                 151: "stwx", 535: "lfsx", 599: "lfsux"}
        return f"X31.{names.get(xo, xo)} r{rD}, r{rA}, r{rB}"
    return f"op{op} raw=0x{w:08X}"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("addrs", nargs="+")
    ap.add_argument("--back", type=int, default=8)
    ap.add_argument("--fwd", type=int, default=12)
    args = ap.parse_args()

    data = open(IMAGE, "rb").read()
    for a in args.addrs:
        addr = int(a, 16)
        print(f"=== around 0x{addr:08X} ({section_of(addr)}) ===")
        start = addr - args.back * 4
        for i in range(-args.back, args.fwd + 1):
            va = addr + i * 4
            w = struct.unpack_from(">I", data, va - BASE)[0]
            mark = " <== target" if i == 0 else ""
            print(f"  0x{va:08X}  0x{w:08X}  {decode(w)}{mark}")


if __name__ == "__main__":
    main()
