#!/usr/bin/env python3
"""Verify scan_fnptr_args.py candidates before adding them as entry points.

For each candidate address this checks, in order:

  1. the word before it is a branch/terminator or padding (function-like start),
  2. the codegen does NOT already treat it as an interior label
     (`loc_<addr>:` in the generated sources) -- an interior label must NOT be
     promoted to an entry point, that would split a live function in half,
  3. the bytes look like a real function head (prologue-ish / straight-line
     code that eventually hits a terminator).

Usage: python tools/verify_fnptr_candidates.py [addr ...]
       (no args -> verify every MISSING candidate from scan_fnptr_args.py)
"""
from __future__ import annotations

import re
import subprocess
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
import classify_entry_points as c  # noqa: E402

# Project root (this file is in tools/).
PROJECT_ROOT = __import__("pathlib").Path(__file__).resolve().parents[1]

GEN_DIR = c.MANIFEST.rsplit("\\", 1)[0] + "\\generated\\default"


def u32(data: bytes, va: int) -> int:
    off = va - c.BASE
    return int.from_bytes(data[off:off + 4], "big")


def term_like(prev: int) -> str | None:
    op = prev >> 26
    if op == 19 and (prev & 0xFC000000) == 0x4C000000:
        return "bcctr-family (blr/bctr/bctrl)"
    if op == 18 and (prev & 1) == 0:
        return "b (jump away)"
    if prev == 0:
        return "padding zeros"
    return None


def is_interior_label(addr: int) -> bool:
    """True when the generated code already has a `loc_<addr>:` label."""
    pat = re.compile(r"loc_%08X\s*:" % addr)
    for fn in ("tenchuz_recomp",):
        pass
    import glob
    for path in glob.glob(GEN_DIR + r"\*.cpp"):
        try:
            with open(path, "r", errors="ignore") as fh:
                for line in fh:
                    if pat.search(line):
                        return True
        except OSError:
            continue
    return False


def main() -> None:
    if len(sys.argv) > 1:
        addrs = [int(a, 16) for a in sys.argv[1:]]
    else:
        out = subprocess.run(
            [sys.executable, r"tools\scan_fnptr_args.py"],
            capture_output=True, text=True, cwd=str(PROJECT_ROOT),
        ).stdout
        addrs = [int(m, 16) for m in re.findall(r"^\s+(0x[0-9A-F]{8})\s+first site",
                                                out, re.M)]

    data = c.load_image()
    pdata = set(c.pdata_starts(data))
    bad = 0

    for a in addrs:
        prev = u32(data, a - 4)
        t = term_like(prev)
        interior = is_interior_label(a)
        # walk forward looking for a terminator within a small window
        term = None
        for k in range(1, 129):
            w = u32(data, a + 4 * k)
            op = w >> 26
            if (op == 19 and (w & 0xFC000000) == 0x4C000000) or \
                    (op == 18 and (w & 1)):
                term = (a + 4 * k, w)
                break
        flags = []
        if t:
            flags.append(f"start-ok({t})")
        else:
            flags.append(f"BAD-PREV(prev=0x{prev:08X})")
        flags.append("INTERIOR-LABEL" if interior else "not-a-label")
        flags.append(f"next-branch 0x{term[0]:08X}" if term else "NO-BRANCH<=512B")
        if a in pdata:
            flags.append("PDATA-START")
        status = "OK " if (t and not interior) else "!! "
        if not (t and not interior):
            bad += 1
        print(f"{status}0x{a:08X}  [{'; '.join(flags)}]")

    print()
    print(f"{len(addrs)} candidates, {bad} need manual review")


if __name__ == "__main__":
    main()
