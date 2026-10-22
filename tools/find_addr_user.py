#!/usr/bin/env python3
"""Find code that references a guest address as a constant load.

The recompiled code materialises a guest pointer with the usual PPC split:

    // lis r11,-32256        (high 16 bits, as a SIGNED literal)
    ctx.r11.s64 = -2113929216;
    // addi r10,r11,3024     (low 16 bits, signed)
    ctx.r10.s64 = ctx.r11.s64 + 3024;

Together those form a 32-bit guest address. Given a target address this tool
recovers the (lis, addi) pair and reports every site, which is how a specific
global - e.g. a flickering counter at 0x40001A10 - can be tied to the function
that reads or writes it.

The high half is emitted as a signed decimal, so it is converted back here.
"""

import argparse
import re
from pathlib import Path

LIS_RE = re.compile(r"^// lis r(\d+),(-?\d+)\s*$")
ADDI_RE = re.compile(r"^// addi r(\d+),r(\d+),(-?\d+)\s*$")


def signed16(v: int) -> int:
    v &= 0xFFFF
    return v - 0x10000 if v >= 0x8000 else v


def split_addr(addr: int):
    """Return (signed_high, signed_low) for a 32-bit address."""
    hi = (addr >> 16) & 0xFFFF
    lo = addr & 0xFFFF
    return signed16(hi), signed16(lo)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("address", help="guest address, e.g. 0x40001A10")
    ap.add_argument("--dir", default="tenchuz-recomp/generated/default")
    ap.add_argument("--context", type=int, default=3,
                    help="instructions of context to print around each hit")
    args = ap.parse_args()

    target = int(args.address, 0) & 0xFFFFFFFF
    want_hi, want_lo = split_addr(target)
    print(f"target 0x{target:08X} -> lis {want_hi} / addi {want_lo}")

    root = Path(args.dir)
    files = sorted(root.glob("tenchuz_recomp.*.cpp"))

    found = 0
    for path in files:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        # Track the most recent lis per register.
        recent_lis = {}
        for i, line in enumerate(lines):
            s = line.strip()
            m = LIS_RE.match(s)
            if m:
                recent_lis[m.group(1)] = (int(m.group(2)), i)
                continue
            m = ADDI_RE.match(s)
            if not m:
                continue
            dst, src, imm = m.group(1), m.group(2), int(m.group(3))
            if src not in recent_lis:
                continue
            hi, li_hi_line = recent_lis[src]
            # Reconstruct the full address the pair produces.
            full = ((hi & 0xFFFF) << 16) + signed16(imm)
            full &= 0xFFFFFFFF
            if full != target:
                continue
            found += 1
            lo, hi_line = max(0, i - args.context), min(len(lines), i + 1 + args.context)
            print(f"\n--- {path.name}:{i + 1}  (lis at line {li_hi_line + 1}) ---")
            for j in range(lo, hi_line):
                marker = ">>" if j == i else "  "
                print(f" {marker} {lines[j].strip()}")

    if not found:
        print(f"\nno (lis/addi) construction of 0x{target:08X} found")


if __name__ == "__main__":
    main()
