#!/usr/bin/env python3
"""Re-examine the labels generate_manifest.py dropped, looking for real functions.

Why
----
`classify_entry_points.classify()` short-circuits on `.pdata`: any address
whose nearest preceding `.pdata` start is someone else is called a LABEL.
That is usually right, but `.pdata` only covers functions that have unwind
info -- leaf functions and thunks have none, so they get attributed to the
*previous* function and dropped. The runtime then dies with

    [FATAL] Call to invalid or unregistered function at guest address 0x...

Two real cases seen in this title:
  * 0x823B8720  `lis r10,0x833B; li r11,1; stw r11,-7984(r10); blr`
                -- a leaf setter passed as a callback argument,
  * 0x8251FC20  `addi r3,r3,-16; b -356`
                -- an MSVC adjustor thunk in a vtable (a whole run of them,
                   each 8 bytes apart).

The danger of relaxing the rule is re-registering a *jump-table case label*.
Doing so makes `classifyTarget` turn the enclosing function's branches into
tail calls and the computed-goto into an indirect call: the loud FATAL turns
into silent mis-execution (r1 drift). So this tool only reports a candidate
when it is structurally a function:

  1. predecessor is a hard terminator (blr / bctr / bc-always / b) -- control
     cannot fall into it, so it can only be entered by an explicit branch,
  2. the body is self-contained: an unconditional terminator is reached within
     SELF_CONTAINED instructions, with no conditional branch escaping the
     region (a switch case almost always has one),
  3. a cross-check on jump tables: a case body's `b` lands on a *shared*
     epilogue targeted by many branches, while a thunk jumps to a handler
     that is not a common exit.

Output is a report; nothing is modified.

Usage: python tools/reclassify_leaf.py [--top N] [--emit]
"""
from __future__ import annotations

import argparse
import os
import re
import struct
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
import classify_entry_points as c  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DROPPED = os.path.join(HERE, "..", "logs", "manifest_dropped_labels.txt")
EXEC_SECTIONS = [
    (0x82270000, 0x599DDC), (0x82809E00, 0x99AC), (0x82813800, 0xA4B8),
    (0x8281DE00, 0x1D0C), (0x8281FC00, 0x5C80), (0x82825A00, 0x1350),
    (0x82826E00, 0xCC8), (0x82827C00, 0x21B8),
]
SELF_CONTAINED = 16

ENTRY_RE = re.compile(r"^(0x[0-9A-Fa-f]{8})\s*=")


def s26(v: int) -> int:
    return v - 0x4000000 if v & 0x2000000 else v


def u32(data: bytes, va: int) -> int:
    off = va - c.BASE
    return int.from_bytes(data[off:off + 4], "big")


def hard_terminator(w: int) -> bool:
    op = w >> 26
    if op == 19:                      # blr / bctr / bclrl / bcctrl
        return (w & 1) == 0
    if op == 18:                      # b / bl
        return (w & 1) == 0
    if op == 16:                      # bc / bcl
        return (w & 1) == 0 and ((w >> 21) & 0x1F & 0x14) == 0x14
    return False


def self_contained(data: bytes, addr: int) -> tuple[bool, int | None]:
    """Walk forward; True when an unconditional terminator ends the region
    before any conditional branch escapes it."""
    end_addr = None
    for k in range(SELF_CONTAINED):
        a = addr + 4 * k
        w = u32(data, a)
        op = w >> 26
        if op == 19 and (w & 1) == 0:
            end_addr = a
            break                       # blr / bctr
        if op == 18 and (w & 1) == 0:
            end_addr = a
            break                       # unconditional b
        if op == 16 and (w & 1) == 0:
            bo = (w >> 21) & 0x1F
            if (bo & 0x14) == 0x14:
                end_addr = a
                break                   # bc-always
            t = (a + s26((w & 0xFFFC) << 16 >> 16)) & 0xFFFFFFFF
            if not (addr <= t <= addr + SELF_CONTAINED * 4):
                return False, None      # conditional branch escapes the region
    return end_addr is not None, end_addr


def branch_target_counts(data: bytes) -> dict[int, int]:
    """How many b/bc sites target each address (for the shared-epilogue test)."""
    counts: dict[int, int] = {}
    for va, sz in EXEC_SECTIONS:
        for a in range(va, va + sz, 4):
            w = u32(data, a)
            op = w >> 26
            if op == 18:
                t = (a + s26(w & 0x03FFFFFC)) & 0xFFFFFFFF
                counts[t] = counts.get(t, 0) + 1
            elif op == 16 and (w & 1) == 0:
                t = (a + s26((w & 0xFFFC) << 16 >> 16)) & 0xFFFFFFFF
                counts[t] = counts.get(t, 0) + 1
    return counts


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--top", type=int, default=0, help="only show the first N")
    ap.add_argument("--emit", action="store_true", help="print TOML for the passers")
    args = ap.parse_args()

    data = c.load_image()
    pdata = set(c.pdata_starts(data))
    targets = branch_target_counts(data)

    dropped = []
    for line in open(DROPPED, encoding="utf-8"):
        m = ENTRY_RE.match(line.strip())
        if m:
            dropped.append(int(m.group(1), 16))

    rows = []
    for a in dropped:
        prev = u32(data, a - 4)
        if not hard_terminator(prev):
            continue
        ok, end = self_contained(data, a)
        if not ok:
            continue
        # shared-epilogue test: a switch case's `b` lands somewhere many
        # branches converge; a thunk's target is a normal handler.
        tail = None
        endw = u32(data, end)
        if (endw >> 26) == 18 and (endw & 1) == 0:
            tail = (end + s26(endw & 0x03FFFFFC)) & 0xFFFFFFFF
        shared = targets.get(tail, 0) if tail is not None else 0
        rows.append((a, end, tail, shared))

    # a shared epilogue is targeted by a lot of branches; thunks jump to
    # handlers that are not common exits
    rows.sort(key=lambda r: (r[3], r[0]))
    risky = [r for r in rows if r[3] >= 12]
    clean = [r for r in rows if r[3] < 12]

    if args.emit:
        for a, _, _, _ in clean:
            print(f"0x{a:08X} = {{ name = \"extra_{a:08X}\" }}")
        return

    print(f"dropped labels scanned            : {len(dropped)}")
    print(f"predecessor is a hard terminator : {len(rows)}")
    print(f"  self-contained body            : {len(rows)}")
    print(f"  jumps to a shared epilogue     : {len(risky)}  (likely switch cases -> keep dropped)")
    print(f"  clean function candidates      : {len(clean)}")
    print()
    show = clean if args.top == 0 else clean[:args.top]
    for a, end, tail, shared in show:
        t = f"tail->{tail:08X}" if tail is not None else "leaf (blr/bctr)"
        print(f"  0x{a:08X}  body {a:08X}..{end:08X}  {t} ({shared} branches)")
    if risky:
        print()
        print("  (rejected, shared epilogue):")
        for a, end, tail, shared in risky[:12]:
            print(f"  0x{a:08X}  tail->{tail:08X} ({shared} branches)")


if __name__ == "__main__":
    main()
