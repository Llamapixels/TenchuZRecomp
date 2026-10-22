#!/usr/bin/env python3
"""Resolve a host RIP (or RVA) to a recompiled function using tenchuz.map.

The CMake build emits tenchuz.map (MSVC /MAP). Recompiled guest functions are
named `sub_XXXXXXXX` (auto-discovered) or `fnptr_XXXXXXXX` / `extra_XXXXXXXX`
(manifest entry points), where XXXXXXXX is the guest address -- so a crash RIP
can be mapped straight back to the guest code that faulted.

Usage:
    python tools/resolve_rip.py 0x1401234abc [more rip values...]
    python tools/resolve_rip.py --rva 0x1234abc
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


FUNC_RE = re.compile(r"^(?:__imp_)?_?(?:sub|fnptr|extra|thunk)_[0-9A-Fa-f]{8}$")


def load_map(path: str, funcs_only: bool = True) -> tuple[list[int], list[str], int]:
    addrs: list[int] = []
    names: list[str] = []
    preferred = 0x140000000
    m = re.search(r"Preferred load address is ([0-9a-fA-F]+)", open(path).read(), re.M)
    if m:
        preferred = int(m.group(1), 16)
    for line in open(path, errors="ignore"):
        mm = LINE_RE.match(line)
        if not mm:
            continue
        name = mm.group(1)
        if funcs_only and not FUNC_RE.match(name):
            continue
        addr = int(mm.group(2), 16)
        addrs.append(addr)
        names.append(name)
    order = sorted(range(len(addrs)), key=lambda i: addrs[i])
    addrs = [addrs[i] for i in order]
    names = [names[i] for i in order]
    return addrs, names, preferred


def guess_guest(name: str) -> str:
    m = re.search(r"(?:sub|fnptr|extra|thunk)_([0-9A-Fa-f]{8})", name)
    if m:
        return f"0x{int(m.group(1), 16):08X}"
    return "?"


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("values", nargs="*", help="host RIPs as 0x...")
    ap.add_argument("--rva", nargs="*", default=[], help="module-relative VAs")
    ap.add_argument("--base", default=None, help="override module base")
    args = ap.parse_args()

    if not os.path.exists(MAP):
        raise SystemExit(f"map file not found: {MAP}")
    addrs, names, preferred = load_map(MAP)
    base = int(args.base, 16) if args.base else preferred
    print(f"map symbols: {len(addrs)}  preferred base: 0x{preferred:016X}")

    targets = [int(v, 16) for v in args.values]
    targets += [base + int(v, 16) for v in args.rva]
    for rip in targets:
        i = bisect.bisect_right(addrs, rip) - 1
        if i < 0:
            print(f"0x{rip:016X}  <before first symbol>")
            continue
        off = rip - addrs[i]
        print(f"0x{rip:016X}  rva 0x{rip - base:08X}  +0x{off:X}  {names[i]}"
              f"  -> guest {guess_guest(names[i])}")


if __name__ == "__main__":
    main()
