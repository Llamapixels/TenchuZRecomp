#!/usr/bin/env python3
"""List direct `bl` callers of guest addresses in the decrypted image.

Usage: python tools/find_callers.py 0x82657B00 0x826578C0
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


def sign_extend(v: int, bits: int = 24) -> int:
    m = 1 << (bits - 1)
    return (v ^ m) - m


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("targets", nargs="+")
    args = ap.parse_args()
    targets = {int(t, 16) for t in args.targets}

    data = open(IMAGE, "rb").read()
    callers: dict[int, list[int]] = {t: [] for t in targets}

    for name, va, sz in EXEC_SECTIONS:
        for off in range(0, sz, 4):
            addr = va + off
            w = struct.unpack_from(">I", data, addr - BASE)[0]
            op = w >> 26
            if op != 18:  # b / bl
                continue
            if not (w & 1):  # LK == 0 -> plain branch
                continue
            disp = sign_extend(w & 0x03FFFFFC)
            target = (addr + disp) & 0xFFFFFFFF
            if target in callers:
                callers[target].append(addr)

    for t in sorted(callers):
        addrs = callers[t]
        print(f"0x{t:08X}: {len(addrs)} direct bl caller(s)")
        for a in addrs[:40]:
            print(f"    caller 0x{a:08X}")
        if len(addrs) > 40:
            print(f"    ... {len(addrs) - 40} more")


if __name__ == "__main__":
    main()
