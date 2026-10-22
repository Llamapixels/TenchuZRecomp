#!/usr/bin/env python3
"""Investigate a guest address reported as an unregistered function.

Reports whether the address is a codegen-known function / in .pdata, and finds
every 4-byte-aligned location in the decrypted image holding that value,
labelled by section and exec/data class.
"""
import re
import struct
import sys

BASE = 0x82000000
# Project root (this file is in tools/).
PROJECT_ROOT = __import__("pathlib").Path(__file__).resolve().parents[1]
IMAGE = str(PROJECT_ROOT) + r"\tools\tenchuz_decrypted.bin"
INIT = str(PROJECT_ROOT) + r"\tenchuz-recomp\generated\default\tenchuz_init.cpp"

SECTIONS = [
    (".rdata", 0x82000600, 0x24D7C4, False),
    (".pdata", 0x8224DE00, 0x12DA8, False),
    (".text", 0x82270000, 0x599DDC, True),
    (".embsec_a", 0x82809E00, 0x99AC, True),
    (".embsec_b", 0x82813800, 0xA4B8, True),
    (".embsec_c", 0x8281DE00, 0x1D0C, True),
    (".embsec_d", 0x8281FC00, 0x5C80, True),
    (".embsec_e", 0x82825A00, 0x1350, True),
    (".embsec_f", 0x82826E00, 0xCC8, True),
    (".embsec_g", 0x82827C00, 0x21B8, True),
    (".data", 0x82830000, 0xBEB15C, False),
    (".XEXID", 0x8341B200, 0x4, False),
    (".idata", 0x83420000, 0x3D2, False),
    (".XBLD", 0x83430000, 0xE0, False),
]


def main() -> None:
    addr = int(sys.argv[1], 16)
    data = open(IMAGE, "rb").read()

    known = set()
    for line in open(INIT, errors="ignore"):
        m = re.match(r"\s*\{\s*0x([0-9A-Fa-f]{8})\s*,", line)
        if m:
            known.add(int(m.group(1), 16))
    print(f"address 0x{addr:08X}")
    print(f"  codegen-known function : {addr in known}")

    pdata = set()
    for off in range(0, SECTIONS[1][2], 8):
        b = struct.unpack_from(">I", data, SECTIONS[1][1] - BASE + off)[0]
        if b:
            pdata.add(b)
    print(f"  in .pdata              : {addr in pdata}")

    needle = struct.pack(">I", addr)
    found = []
    for name, va, size, _exec in SECTIONS:
        blob = data[va - BASE: va - BASE + size]
        start = 0
        while True:
            i = blob.find(needle, start)
            if i < 0:
                break
            if i % 4 == 0:
                found.append((name, va + i))
            start = i + 1
    print(f"  4-byte-aligned references: {len(found)}")
    for name, va in found[:40]:
        print(f"    {name} @ 0x{va:08X}")


if __name__ == "__main__":
    main()
