#!/usr/bin/env python3
"""Find the guest setjmp counterpart of a longjmp/RtlRestoreContext routine.

longjmp restores: f14..f31 from +0..136, r1 from +144, r13..r31 from
+152..296, cr from +304, lr from +308 (all relative to the env pointer in r3).
setjmp saves exactly those slots, so scan the executable image for a run of
stores hitting +144, +304 and +308 off one base register.

Usage: python tools/find_setjmp.py [--base 0x82650000] [--span 0x40000]
"""
from __future__ import annotations

import argparse
import struct

BASE = 0x82000000
# Project root (this file is in tools/).
PROJECT_ROOT = __import__("pathlib").Path(__file__).resolve().parents[1]
IMAGE = str(PROJECT_ROOT) + r"\tools\tenchuz_decrypted.bin"
TEXT_VA = 0x82270000
TEXT_SZ = 0x599DDC


def sign16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def sign14(v: int) -> int:
    v &= 0x3FFF
    return v - 0x4000 if v & 0x2000 else v


def store_target(w: int):
    """Return (base_reg, displacement) if w is a store-with-immediate, else None."""
    op = w >> 26
    rA = (w >> 16) & 31
    imm = w & 0xFFFF
    if op in (36, 37, 38, 39, 44, 45):  # stw/stwu/stb/stbu/sth/sthu
        return rA, sign16(imm)
    if op in (62, 63):  # std/stdu (DS-form)
        return rA, sign14(imm)
    if op in (50, 51):  # stfd/stfdu (DS-form)
        return rA, sign14(imm)
    return None


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--base", default="0x82270000")
    ap.add_argument("--span", default="0x599DDC")
    args = ap.parse_args()
    lo = int(args.base, 16)
    hi = lo + int(args.span, 16)
    data = open(IMAGE, "rb").read()

    hits = []
    # For each candidate window start, collect stores per base register.
    step = 4
    window = 40  # instructions
    cur = {}  # base reg -> set of displacements seen in the last `window` insns
    history = []
    va = lo
    while va < hi:
        w = struct.unpack_from(">I", data, va - BASE)[0]
        st = store_target(w)
        history.append((va, st))
        if len(history) > window:
            old_va, old_st = history.pop(0)
        # recount
        cur = {}
        for _, s in history:
            if s:
                cur.setdefault(s[0], set()).add(s[1])
        for reg, disps in cur.items():
            if {144, 296, 304, 308} <= disps:
                hits.append((va, reg, len(disps)))
        va += step

    # Collapse consecutive hits into ranges.
    print(f"scanned 0x{lo:08X}..0x{hi:08X}")
    seen = set()
    for va, reg, n in hits:
        key = (reg,)
        if any(abs(va - h[0]) <= 4 * window for h in seen if h[1] == reg):
            continue
        seen.add((va, reg))
        print(f"  window ending 0x{va:08X}: base r{reg}, {n} distinct displacements")


if __name__ == "__main__":
    main()
