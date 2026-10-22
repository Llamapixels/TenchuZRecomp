#!/usr/bin/env python3
"""Audit the generated recompiled functions for unbalanced guest stack frames.

A recompiled PowerPC function must keep ctx.r1 balanced: whatever the prologue
pushes (``stwu r1,-N(r1)``) has to be popped again before it returns
(``addi r1,r1,+N`` or ``lwz r1,0(r1)``).

If an entry point in the manifest points *into the middle* of another function,
the generator emits a function that never pushed the frame but still pops it (or
vice versa).  Calling such a function corrupts ctx.r1 and, once the stack has
drifted, every ``__restgprlr_*`` restores from the wrong slot -- which is exactly
the "callee-saved registers suddenly hold garbage / null" symptom we chase.

Usage:
    python tools/check_frame_balance.py [--file substring] [--limit N]
"""
from __future__ import annotations

import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "..", "tenchuz-recomp", "generated", "default")

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\(([^)]+)\)\s*\{")
PUSH_RE = re.compile(r"ea\s*=\s*-(\d+)\s*\+\s*ctx\.r1\.u32;")
ADD_RE = re.compile(r"ctx\.r1\.[su]\d+\s*=\s*ctx\.r1\.[su]\d+\s*\+\s*(\d+);")
SUB_RE = re.compile(r"ctx\.r1\.[su]\d+\s*=\s*ctx\.r1\.[su]\d+\s*-\s*(\d+);")
POPLOAD_RE = re.compile(r"ctx\.r1\.u32\s*=\s*REX_LOAD_U32\(ctx\.r1\.u32 \+ 0\);")


def scan_file(path: str, funcs: dict) -> None:
    name = None
    pushes: set[int] = set()
    pops: set[int] = set()
    popload = False
    with open(path, errors="ignore") as f:
        for line in f:
            m = FUNC_RE.match(line)
            if m:
                if name:
                    funcs[name] = (pushes, pops, popload, path)
                name = m.group(1)
                pushes, pops, popload = set(), set(), False
                continue
            if line.startswith("}"):
                if name:
                    funcs[name] = (pushes, pops, popload, path)
                    name = None
                continue
            if name is None:
                continue
            m = PUSH_RE.search(line)
            if m:
                pushes.add(int(m.group(1)))
            m = ADD_RE.search(line)
            if m:
                pops.add(int(m.group(1)))
            m = SUB_RE.search(line)
            if m:
                pushes.add(-int(m.group(1)))
            if POPLOAD_RE.search(line):
                popload = True
    if name:
        funcs[name] = (pushes, pops, popload, path)


def main() -> None:
    args = sys.argv[1:]
    only = None
    limit = 80
    if "--file" in args:
        only = args[args.index("--file") + 1]
    if "--limit" in args:
        limit = int(args[args.index("--limit") + 1])

    funcs: dict = {}
    files = sorted(glob.glob(os.path.join(GEN, "tenchuz_recomp.*.cpp")))
    if only:
        files = [f for f in files if only in os.path.basename(f)]
    for path in files:
        scan_file(path, funcs)

    print(f"functions scanned: {len(funcs)}")
    offenders = []
    for name, (pushes, pops, popload, path) in funcs.items():
        unexplained = {p for p in pops if p not in pushes}
        if unexplained and not pushes:
            offenders.append((name, pushes, pops, popload, path, "no push at all"))
        elif unexplained:
            offenders.append((name, pushes, pops, popload, path,
                              f"unmatched pop(s) {sorted(unexplained)}"))
        elif popload and not pushes:
            offenders.append((name, pushes, pops, popload, path, "pop-by-load only"))
    print(f"offenders: {len(offenders)}")
    for name, pushes, pops, popload, path, why in offenders[:limit]:
        print(f"  {name}  push={sorted(pushes)} pop={sorted(pops)} "
              f"popload={popload}  [{os.path.basename(path)}]  <- {why}")


if __name__ == "__main__":
    main()
