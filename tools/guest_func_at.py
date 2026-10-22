#!/usr/bin/env python3
"""Resolve guest code addresses to the owning function.

Uses the .pdata table of the decrypted image: entries are {begin, unwind}
big-endian pairs and are sorted by begin address, so the owning function of
an address is the nearest .pdata start at or below it.

Leaf functions have no unwind info, so a hit labelled `+0xNNN` inside a large
range may actually be the start of a leaf function -- treat it as a hint.

Usage:
    python tools/guest_func_at.py 0x8247BDF8 0x82410E68 ...
"""
from __future__ import annotations

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = os.path.join(HERE, "tenchuz_decrypted.bin")
BASE = 0x82000000
PDATA_VA = 0x8224DE00
PDATA_SIZE = 0x12DA8
TEXT_LO, TEXT_HI = 0x82270000, 0x82829DB8


def pdata_starts() -> list[int]:
    data = open(IMAGE, "rb").read()
    off = PDATA_VA - BASE
    starts = set()
    for i in range(PDATA_SIZE // 8):
        b = struct.unpack_from(">I", data, off + i * 8)[0]
        if TEXT_LO <= b < TEXT_HI:
            starts.add(b)
    return sorted(starts)


def main() -> None:
    starts = pdata_starts()
    print(f".pdata starts in code range: {len(starts)}")
    for arg in sys.argv[1:]:
        a = int(arg, 16)
        lo, hi = 0, len(starts)
        while lo < hi:  # bisect_right
            mid = (lo + hi) // 2
            if starts[mid] <= a:
                lo = mid + 1
            else:
                hi = mid
        if lo == 0:
            print(f"{a:#010x}  <below first .pdata function ({starts[0]:#x})>")
            continue
        owner = starts[lo - 1]
        nxt = starts[lo] if lo < len(starts) else TEXT_HI
        print(f"{a:#010x}  owner={owner:#010x} (+{a - owner:#x})  "
              f"next={nxt:#010x}  span={nxt - owner:#x}")


if __name__ == "__main__":
    main()
