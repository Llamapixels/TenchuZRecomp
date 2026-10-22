#!/usr/bin/env python3
"""Find every guest instruction that accesses a given small offset (X(rA)).

Used to narrow down a null-base access fault: if the guest faults reading
`0x1A4`, the culprit is one of the `lwz/stw/lbz/... rD, 0x1A4(rA)` sites.

Usage:
    python tools/scan_offset_access.py 0x1A4 [--image ...]
"""
from __future__ import annotations

import argparse
import struct

BASE = 0x82000000
# Project root (this file is in tools/).
PROJECT_ROOT = __import__("pathlib").Path(__file__).resolve().parents[1]
IMAGE = str(PROJECT_ROOT) + r"\tools\tenchuz_decrypted.bin"

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

# opcode -> (mnemonic, signed?)
DLOAD = {32: "lwz", 34: "lbz", 40: "lhz", 42: "lha", 48: "lfs", 50: "lfd", 58: "ld"}
DSTORE = {36: "stw", 38: "stb", 44: "sth", 52: "stfs", 54: "stfd", 62: "std"}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("offset", help="offset, e.g. 0x1A4")
    ap.add_argument("--image", default=IMAGE)
    args = ap.parse_args()
    want = int(args.offset, 16)

    data = open(args.image, "rb").read()
    hits = 0
    for name, va, size in EXEC_SECTIONS:
        for off in range(0, size, 4):
            w = struct.unpack_from(">I", data, va - BASE + off)[0]
            op = w >> 26
            if op not in DLOAD and op not in DSTORE:
                continue
            imm = w & 0xFFFF
            if imm != want:
                continue
            rD = (w >> 21) & 31
            rA = (w >> 16) & 31
            mn = DLOAD.get(op) or DSTORE.get(op)
            hits += 1
            print(f"  {name} 0x{va + off:08X}  {mn} r{rD}, 0x{want:X}(r{rA})")
    print(f"total {hits}")


if __name__ == "__main__":
    main()
