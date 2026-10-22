#!/usr/bin/env python3
"""Statistics for the linker map: are all recompiled functions listed?

Also reports the exact symbol range that brackets a host address, including
any non `sub_` symbols, so a host RIP can be attributed with confidence.

Usage:
    python tools/map_stats.py [0x1405FBE78]
"""
from __future__ import annotations

import re
import sys

MAP = (r"C:\Emulation\360\recomps\tenchu z\tenchuz-recomp\out\build"
       r"\win-amd64-release\tenchuz.map")
INIT = (r"C:\Emulation\360\recomps\tenchu z\tenchuz-recomp\generated"
        r"\default\tenchuz_init.cpp")

LINE_RE = re.compile(
    r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\b")
FUNC_RE = re.compile(r"^(?:__imp_)?_?(?:sub|fnptr|extra|thunk)_[0-9A-Fa-f]{8}$")


def main() -> None:
    target = int(sys.argv[1], 16) if len(sys.argv) > 1 else None
    all_syms: list[tuple[int, str]] = []
    func_syms: list[tuple[int, str]] = []
    for line in open(MAP, errors="ignore"):
        m = LINE_RE.match(line)
        if not m:
            continue
        name = m.group(1)
        addr = int(m.group(2), 16)
        all_syms.append((addr, name))
        if FUNC_RE.match(name):
            func_syms.append((addr, name))
    all_syms.sort()
    func_syms.sort()

    func_da = sorted({a for a, _ in func_syms})
    all_da = sorted({a for a, _ in all_syms})
    print(f"map lines matched       : {len(all_syms)}")
    print(f"distinct all addresses  : {len(all_da)}")
    print(f"distinct func addresses : {len(func_da)}")

    known = set()
    for line in open(INIT, errors="ignore"):
        m = re.match(r"\s*\{\s*0x([0-9A-Fa-f]{8})\s*,", line)
        if m:
            known.add(int(m.group(1), 16))
    print(f"codegen init table size : {len(known)}")

    missing = sorted(known - {int(n.split('_')[-1], 16) for _, n
                              in func_syms})
    print(f"init entries w/o map sym: {len(missing)}")
    for a in missing[:10]:
        print(f"   {a:#010x}")

    gaps = [func_da[i + 1] - func_da[i] for i in range(len(func_da) - 1)]
    if gaps:
        gaps_sorted = sorted(gaps)
        print(f"host gap min/med/max    : "
              f"0x{gaps_sorted[0]:X} / 0x{gaps_sorted[len(gaps)//2]:X} / "
              f"0x{gaps_sorted[-1]:X}")

    if target is not None:
        print(f"\n--- symbols bracketing 0x{target:016X} ---")
        below = [s for s in all_syms if s[0] <= target]
        above = [s for s in all_syms if s[0] > target]
        for a, n in below[-3:]:
            print(f"  0x{a:016X}  +0x{target - a:04X}  {n}")
        for a, n in above[:3]:
            print(f"  0x{a:016X}  -0x{a - target:04X}  {n}")


if __name__ == "__main__":
    main()
