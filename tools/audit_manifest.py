#!/usr/bin/env python3
"""Audit the merged manifest's entry points against the image's .pdata table.

`rexglue`'s codegen classifies a branch target as ``Function`` when the target
address is a registered entry point, and then emits a *conditional tail call*
(``if (cr) { fn(ctx, base); return; }``) instead of a local ``goto`` -- see
src/codegen/builders/context.cpp:emit_conditional_branch.  Registering an
address that is really an intra-function label therefore (a) splits a real
function in two and (b) turns its loops into calls into a body that pops a
frame it never pushed.  That corrupts ctx.r1 and every __restgprlr_* restore.

This script flags entries whose address is not the start of a .pdata function
(``owner != addr``).

Caveat: leaf functions carry no unwind info, so a genuine leaf entry can show
up as ``owner != addr`` here; such an entry is usually harmless because a leaf
never touches ctx.r1.  Cross-check with tools/check_frame_balance.py, which
reports the entries that actually produced an unbalanced function.

Usage:
    python tools/audit_manifest.py
"""
from __future__ import annotations

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = os.path.join(HERE, "tenchuz_decrypted.bin")
MANIFEST = os.path.join(HERE, "..", "tenchuz-recomp", "tenchuz_manifest.toml")
BASE_TOML = os.path.join(HERE, "tenchuz_manifest.base.toml")
FNPTR_TOML = os.path.join(HERE, "fnptr_entries.toml")
EXTRA_TOML = os.path.join(HERE, "extra_entries.toml")

BASE = 0x82000000
PDATA_VA = 0x8224DE00
PDATA_SIZE = 0x12DA8
TEXT_LO, TEXT_HI = 0x82270000, 0x82829DB8

ENTRY_RE = re.compile(r"^(0x[0-9A-Fa-f]{8})\s*=\s*(\{.*\})\s*$")
NAME_RE = re.compile(r'name\s*=\s*"([^"]+)"')


def pdata_starts() -> list[int]:
    data = open(IMAGE, "rb").read()
    off = PDATA_VA - BASE
    starts = set()
    for i in range(PDATA_SIZE // 8):
        b = struct.unpack_from(">I", data, off + i * 8)[0]
        if TEXT_LO <= b < TEXT_HI:
            starts.add(b)
    return sorted(starts)


def owner_of(starts: list[int], a: int) -> tuple[int, int]:
    lo, hi = 0, len(starts)
    while lo < hi:
        mid = (lo + hi) // 2
        if starts[mid] <= a:
            lo = mid + 1
        else:
            hi = mid
    if lo == 0:
        return a, 0
    o = starts[lo - 1]
    n = starts[lo] if lo < len(starts) else TEXT_HI
    return o, n - o


def read_entries(path: str) -> list[tuple[int, str]]:
    out = []
    if not os.path.exists(path):
        return out
    for line in open(path, encoding="utf-8"):
        m = ENTRY_RE.match(line.strip())
        if m:
            nm = NAME_RE.search(m.group(2))
            out.append((int(m.group(1), 16), nm.group(1) if nm else "?"))
    return out


def main() -> None:
    starts = set(pdata_starts())
    slist = sorted(starts)

    merged = read_entries(MANIFEST)
    source_of: dict[int, str] = {}
    for path, tag in ((BASE_TOML, "base"), (EXTRA_TOML, "extra"),
                      (FNPTR_TOML, "fnptr")):
        for a, _n in read_entries(path):
            source_of.setdefault(a, tag)

    print(f"manifest entries: {len(merged)}   .pdata starts: {len(slist)}")
    by_src: dict[str, list[tuple[int, int, int]]] = {}
    for a, name in merged:
        if a in starts or not (TEXT_LO <= a < TEXT_HI):
            continue
        owner, span = owner_of(slist, a)
        by_src.setdefault(source_of.get(a, "?"), []).append((a, owner, span))
    for tag in sorted(by_src):
        items = sorted(by_src[tag])
        print(f"\n[{tag}] mid-function entries: {len(items)}")
        for a, owner, span in items:
            print(f"  0x{a:08X}  owner=0x{owner:08X} (+0x{a - owner:X}) span=0x{span:X}")

    # Also report entries that are outside the code section entirely.
    for tag in ("base", "extra", "fnptr"):
        for a, _n in read_entries({"base": BASE_TOML, "extra": EXTRA_TOML,
                                   "fnptr": FNPTR_TOML}[tag]):
            if not (TEXT_LO <= a < TEXT_HI):
                print(f"\n[{tag}] outside code range: 0x{a:08X}")


if __name__ == "__main__":
    main()
