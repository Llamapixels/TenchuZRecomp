#!/usr/bin/env python3
"""List linker-map symbols around an address (debugging aid).

Usage:
    python tools/map_probe.py 0x1405fbe78 [--count 8]
"""
from __future__ import annotations

import argparse
import bisect
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
MAP = os.path.normpath(os.path.join(
    HERE, "..", "tenchuz-recomp", "out", "build", "win-amd64-release", "tenchuz.map"))

LINE_RE = re.compile(
    r"^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\b")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("addr")
    ap.add_argument("--count", type=int, default=8)
    args = ap.parse_args()

    target = int(args.addr, 16)
    syms: list[tuple[int, str]] = []
    for line in open(MAP, errors="ignore"):
        m = LINE_RE.match(line)
        if m:
            syms.append((int(m.group(2), 16), m.group(1)))
    syms.sort()
    addrs = [a for a, _ in syms]
    i = bisect.bisect_right(addrs, target)
    print(f"map lines: {len(syms)}")
    lo = max(0, i - args.count)
    hi = min(len(syms), i + args.count)
    for j in range(lo, hi):
        a, n = syms[j]
        mark = "  <== nearest before" if j == i - 1 else ""
        print(f"  0x{a:016X}  {n}{mark}")
    print(f"target 0x{target:016X}")


if __name__ == "__main__":
    main()
