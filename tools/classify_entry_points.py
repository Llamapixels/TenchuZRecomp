#!/usr/bin/env python3
"""Classify manifest entry points as real function starts vs intra-function labels.

Why this matters
----------------
``rexglue``'s ``FunctionGraph::classifyTarget`` (src/codegen/function_graph.cpp)
checks *registered entry point* BEFORE *inside the caller's own function*:

    if (isEntryPoint(target))            return TargetKind::Function;   // case 3
    if (callerFn->containsAddress(t))    return TargetKind::InternalLabel; // case 4

So registering an address that is really an intra-function label makes every
`b` / `bc` to it a *tail call* (`if (cr) { fn(ctx, base); return; }`) instead of
`goto loc_XXXX`.  The split-off body still contains the enclosing function's
epilogue, so it pops a guest frame it never pushed: ctx.r1 drifts upwards and
every ``__restgprlr_*`` restores from the wrong slot (garbage callee-saved
registers, null `this`).

How an address is classified
----------------------------
The image's ``.pdata`` table (unwind info, {begin, unwind} big-endian pairs)
is authoritative for every function that has a stack frame: the XDK linker
emits one entry per such function. So:

  * address == a .pdata start                       -> FUNC
  * address strictly inside a .pdata range          -> LABEL
    (the range may hide an unrecorded *leaf* function start -- .pdata omits
    leaves -- but registering a mid-function label is the far more damaging
    error, so inside-range defaults to LABEL; if the runtime reports
    ``[FATAL] Call to invalid or unregistered function`` for such an address,
    add it to tools/entry_points_keep.toml)
  * address not covered by .pdata (below the first start, or a leaf-function
    gap is indistinguishable only for in-range, so this branch is rare):
    fall back to the predecessor heuristic -- a real function can only be
    *entered by a branch*, never fallen into:

      predecessor is `bl` / `bctrl` (LK=1)   -> call return continuation -> LABEL
      predecessor is a conditional `bc`       -> fall-through continues   -> LABEL
      predecessor is `b`/`bc`-always/`blr`/`bctr`/`bclr` (LK=0)          -> FUNC
      predecessor is a non-branch insn / padding -> ambiguous (mid-function
        labels often follow straight-line code, e.g. loop headers after a
        ``stw``)                                    -> LABEL

Cross-check the result with tools/find_tail_calls.py + tools/check_frame_balance.py:
after a correct manifest both should report (almost) nothing.

Usage:
    python tools/classify_entry_points.py                 # whole manifest
    python tools/classify_entry_points.py 0x82649F64 ...
    python tools/classify_entry_points.py --labels-only
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
TEXT_LO, TEXT_HI = 0x82270000, 0x82829DB8
PDATA_VA = 0x8224DE00
PDATA_SIZE = 0x12DA8

# Instructions after which control cannot fall into the next word.
TERMINATORS = {"blr", "bctr", "b", "bc(always)", "bcl(always)",
               "bcctrl", "bclrl"}

ENTRY_RE = re.compile(r"^(0x[0-9A-Fa-f]{8})\s*=\s*(\{.*\})\s*$")
NAME_RE = re.compile(r'name\s*=\s*"([^"]+)"')


def load_image() -> bytes:
    return open(IMAGE, "rb").read()


def pdata_starts(data: bytes) -> list[int]:
    """Sorted list of function begin addresses from the image's .pdata."""
    off = PDATA_VA - BASE
    starts = set()
    for i in range(PDATA_SIZE // 8):
        b = struct.unpack_from(">I", data, off + i * 8)[0]
        if TEXT_LO <= b < TEXT_HI:
            starts.add(b)
    return sorted(starts)


def pdata_owner(starts: list[int], addr: int) -> tuple[int | None, int | None]:
    """Return (owner, next_start): nearest .pdata begin at or below addr."""
    import bisect
    i = bisect.bisect_right(starts, addr)
    if i == 0:
        return None, None
    return starts[i - 1], (starts[i] if i < len(starts) else TEXT_HI)


def word_at(data: bytes, addr: int) -> int | None:
    off = addr - BASE
    if off < 0 or off + 4 > len(data):
        return None
    return struct.unpack_from(">I", data, off)[0]


def describe_insn(insn: int) -> str:
    op = (insn >> 26) & 0x3F
    if op == 18:
        return "bl" if (insn & 1) else "b"
    if op == 16:
        bo = (insn >> 21) & 0x1F
        always = (bo & 0x14) == 0x14
        lk = insn & 1
        if always and lk:
            return "bcl(always)"
        if always:
            return "bc(always)"
        return "bcl" if lk else "bc"
    if op == 19:
        xo = (insn >> 1) & 0x3FF
        lk = insn & 1
        if xo == 16:
            return "bclrl" if lk else "blr"
        return "bcctrl" if lk else "bctr"
    return f"op{op}"


def conditional_branch_targets(data: bytes) -> set[int]:
    """Every address targeted by a conditional branch (`bc`/`bcl`).

    An address a conditional branch jumps *into* cannot be a function start
    (control falls/jumps into it from inside another function), even if a data
    pointer also references it.
    """
    out: set[int] = set()
    for va, sz in ((0x82270000, 0x599DDC), (0x82809E00, 0x99AC),
                   (0x82813800, 0xA4B8), (0x8281DE00, 0x1D0C),
                   (0x8281FC00, 0x5C80), (0x82825A00, 0x1350),
                   (0x82826E00, 0xCC8), (0x82827C00, 0x21B8)):
        for a in range(va, va + sz, 4):
            w = word_at(data, a)
            if w is None or (w >> 26) != 16:   # bc / bcl only
                continue
            bd = w & 0xFFFC
            if bd & 0x8000:
                bd -= 0x10000
            out.add((a + bd) & 0xFFFFFFFF)
    return out


def classify(data: bytes, addr: int, starts: list[int] | None = None,
             is_pointer: bool = False,
             cond_targets: set[int] | None = None) -> tuple[str, str]:
    """Return (kind, why) where kind is FUNC or LABEL.

    ``.pdata`` is authoritative when available (pass the result of
    ``pdata_starts``); the predecessor heuristic only applies to addresses
    the table does not cover.

    ``is_pointer`` marks addresses that a data pointer references (a literal
    32-bit address in .rdata/.data, e.g. a vtable slot). Such an address is
    *proven callable*, so if control also cannot fall into it -- its
    predecessor is a function terminator -- it is a function start even when
    a .pdata range physically covers it. The XDK linker folds runs of small
    compiler-generated functions (vtable dispatch thunks, `this`-adjustor
    stubs, leaf getters) into one .pdata range owned by the function that
    precedes them; without this override every such function is dropped and
    the runtime dies with
    ``[FATAL] Call to invalid or unregistered function at guest address 0x...``.
    """
    if starts is None:
        starts = pdata_starts(data)
    owner, nxt = pdata_owner(starts, addr)
    if owner == addr:
        return "FUNC", ".pdata function start"
    if owner is not None:
        if is_pointer:
            prev = word_at(data, addr - 4)
            if (prev is not None and describe_insn(prev) in TERMINATORS
                    and (cond_targets is None or addr not in cond_targets)):
                return "FUNC", (f"pointer-referenced, predecessor is "
                                f"{describe_insn(prev)} (cannot fall in); "
                                f"inside .pdata function 0x{owner:08X}")
        return "LABEL", (f"inside .pdata function 0x{owner:08X} "
                         f"(+0x{addr - owner:X})")
    # Below the first .pdata entry: heuristic only.
    prev = word_at(data, addr - 4)
    if prev is None:
        return "FUNC", "no predecessor"
    if prev == 0:
        return "FUNC", "padding before function start"
    op = (prev >> 26) & 0x3F
    lk = prev & 1
    if op == 18:  # b / bl
        if lk:
            return "LABEL", "predecessor is bl (return continuation)"
        return "FUNC", "predecessor is b (tail branch)"
    if op == 16:  # bc / bcl
        bo = (prev >> 21) & 0x1F
        if lk:
            return "LABEL", "predecessor is bcl (return continuation)"
        if (bo & 0x14) == 0x14:
            return "FUNC", "predecessor is bc-always (tail branch)"
        return "LABEL", "predecessor is conditional bc (fall-through)"
    if op == 19:  # bclr / bcctr
        if lk:
            return "LABEL", "predecessor is bctrl/bclrl (call continuation)"
        return "FUNC", "predecessor is blr/bctr (terminator)"
    # Non-branch predecessor: ambiguous (mid-function labels often follow
    # straight-line code, e.g. loop headers after a store) -> LABEL.
    return "LABEL", f"predecessor is {describe_insn(prev)} (non-branch, ambiguous)"


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
    args = sys.argv[1:]
    labels_only = "--labels-only" in args
    args = [a for a in args if not a.startswith("--")]

    data = load_image()

    if args:
        addrs = [(int(a, 16), "") for a in args]
    else:
        addrs = read_entries(MANIFEST)

    source_of: dict[int, str] = {}
    for path, tag in ((BASE_TOML, "base"), (EXTRA_TOML, "extra"),
                      (FNPTR_TOML, "fnptr")):
        for a, _n in read_entries(path):
            source_of.setdefault(a, tag)

    counts = {"FUNC": 0, "LABEL": 0}
    starts = pdata_starts(data)
    for a, name in sorted(addrs):
        if not (TEXT_LO <= a < TEXT_HI):
            print(f"0x{a:08X}  OUTSIDE-CODE")
            continue
        kind, why = classify(data, a, starts)
        counts[kind] += 1
        if labels_only and kind != "LABEL":
            continue
        prev = word_at(data, a - 4)
        print(f"0x{a:08X}  {kind:5s}  [{source_of.get(a, '?'):5s}]  "
              f"prev=0x{prev:08X} {describe_insn(prev):12s}  {why}")
    if not args:
        print(f"\ntotal FUNC={counts['FUNC']} LABEL={counts['LABEL']}")


if __name__ == "__main__":
    main()
