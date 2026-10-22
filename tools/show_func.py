#!/usr/bin/env python3
"""Print a recompiled function body by name from the generated sources.

Usage:
    python tools/show_func.py sub_8247F9D0 [sub_8247F760 ...]

Searches tenchuz-recomp/generated/default/tenchuz_recomp.*.cpp for
DEFINE_REX_FUNC(<name>) and prints the whole braced body.
"""
from __future__ import annotations

import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "..", "tenchuz-recomp", "generated", "default")


def find(name: str):
    needle = f"DEFINE_REX_FUNC({name})"
    for path in sorted(glob.glob(os.path.join(GEN, "tenchuz_recomp.*.cpp"))):
        with open(path, errors="ignore") as f:
            lines = f.read().splitlines()
        for i, ln in enumerate(lines):
            if needle in ln:
                return path, i, lines
    return None, -1, []


def main() -> None:
    for name in sys.argv[1:]:
        path, start, lines = find(name)
        if start < 0:
            print(f"=== {name}: NOT FOUND ===")
            continue
        print(f"=== {name}  ({os.path.basename(path)}:{start + 1}) ===")
        brace = 0
        for j in range(start, len(lines)):
            print(f"{j + 1}\t{lines[j]}")
            brace += lines[j].count("{") - lines[j].count("}")
            if j > start and brace <= 0:
                break
        print()


if __name__ == "__main__":
    main()
