#!/usr/bin/env python3
"""Validate that manifest entry points are real function starts.

For each address in tools/fnptr_entries.toml and tools/extra_entries.toml,
report whether it is the *start* of a .pdata function, lies *inside* one
(a label, not a function -> splitting it would corrupt codegen), or has no
.pdata entry at all (leaf function, or not a function).

Usage:
    python tools/check_entry_points.py [--only extra|fnptr|all]
"""
from __future__ import annotations

import argparse
import os
import re
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = os.path.join(HERE, "tenchuz_decrypted.bin")
BASE = 0x82000000
PDATA_VA = 0x8224DE00
PDATA_SIZE = 0x12DA8
TEXT_LO, TEXT_HI = 0x82270000, 0x82829DB8

ENTRY_RE = re.compile(r"^\s*0x([0-9A-Fa-f]{8})\s*=")


def pdata_ranges() -> list[tuple[int, int]]:
    data = open(IMAGE, "rb").read()
    off = PDATA_VA - BASE
    begins = []
    for i in range(PDATA_SIZE // 8):
        b = struct.unpack_from(">I", data, off + i * 8)[0]
        begins.append(b)
    begins = sorted(set(begins))
    out = []
    for i, b in enumerate(begins):
        end = begins[i + 1] if i + 1 < len(begins) else TEXT_HI
        if TEXT_LO <= b < TEXT_HI:
            out.append((b, end))
    return out


def load_addrs(path: str) -> list[int]:
    out = []
    for line in open(path, errors="ignore"):
        m = ENTRY_RE.match(line)
        if m:
            out.append(int(m.group(1), 16))
    return out


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", choices=["extra", "fnptr", "all"], default="all")
    args = ap.parse_args()

    ranges = pdata_ranges()
    starts = {b: e for b, e in ranges}
    print(f".pdata functions: {len(ranges)}")
    print(f"text range: {TEXT_LO:#x}..{TEXT_HI:#x}")

    def classify(a: int) -> str:
        if a in starts:
            return f"pdata-start (end {starts[a]:#x})"
        for b, e in ranges:
            if b < a < e:
                return f"MID-FUNCTION of {b:#010x} (+{a - b:#x})"
        return "no pdata entry (leaf / not a function)"

    for label, path in (("extra", "extra_entries.toml"),
                        ("fnptr", "fnptr_entries.toml")):
        if args.only not in (label, "all"):
            continue
        addrs = load_addrs(os.path.join(HERE, path))
        counts = {"start": 0, "mid": 0, "none": 0, "oob": 0}
        print(f"\n=== {label}: {len(addrs)} entries ({path}) ===")
        for a in sorted(addrs):
            c = classify(a)
            if a < TEXT_LO or a >= TEXT_HI:
                counts["oob"] += 1
                tag = "OUT-OF-CODE"
            elif c.startswith("pdata-start"):
                counts["start"] += 1
                if label == "extra":
                    print(f"  {a:#010x}  {c}")
                continue
            elif c.startswith("MID"):
                counts["mid"] += 1
                tag = "MID"
            else:
                counts["none"] += 1
                tag = "NO-PDATA"
            print(f"  {a:#010x}  [{tag}] {c}")
        print(f"  summary: {counts}")


if __name__ == "__main__":
    main()
