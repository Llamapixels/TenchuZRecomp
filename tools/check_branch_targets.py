#!/usr/bin/env python3
"""Check whether any address is targeted by a conditional branch.

`generate_manifest.py` refuses to register addresses whose predecessor is a
fall-through, because `FunctionGraph::classifyTarget` tests "is a registered
entry point" *before* "is inside the caller's function": every `b`/`bc` to a
registered label then becomes a conditional *tail call* instead of a
`goto loc_XXXX`, and the split-off body pops a guest frame it never pushed
(r1 drift).

A keep-listed address is only safe when nothing branches *into* it. This tool
reports, for each address, every `b`/`bl`/`bc`/`bcl`/`bctr`/`bctrl` site that
targets it, so the risky ones (conditional-branch targets) can be rejected.

Usage: python tools/check_branch_targets.py [addr ...]
"""
from __future__ import annotations

import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
import classify_entry_points as c  # noqa: E402

EXEC_SECTIONS = [
    (0x82270000, 0x599DDC), (0x82809E00, 0x99AC), (0x82813800, 0xA4B8),
    (0x8281DE00, 0x1D0C), (0x8281FC00, 0x5C80), (0x82825A00, 0x1350),
    (0x82826E00, 0xCC8), (0x82827C00, 0x21B8),
]


def s26(v: int) -> int:
    return v - 0x4000000 if v & 0x2000000 else v


def main() -> None:
    addrs = [int(a, 16) for a in sys.argv[1:]]
    if not addrs:
        print("usage: python tools/check_branch_targets.py 0x........ ...")
        return
    data = c.load_image()
    targets = set(addrs)
    found: dict[int, list[tuple[int, str]]] = {a: [] for a in targets}

    for va, sz in EXEC_SECTIONS:
        for a in range(va, va + sz, 4):
            off = a - c.BASE
            w = int.from_bytes(data[off:off + 4], "big")
            op = w >> 26
            if op == 18:            # b / bl
                li = w & 0x03FFFFFC
                if li & 0x02000000:
                    li -= 0x04000000
                t = ((li if w & 2 else a + li)) & 0xFFFFFFFF
                if t in targets:
                    found[t].append((a, "bl" if w & 1 else "b"))
            elif op == 16:          # bc / bcl (conditional)
                # BD is the low 16 bits (bits 2..15), sign-extended from bit 15.
                # s26() never fires here (bit 25 is 0), so backward branches
                # (BD >= 0x8000) must be sign-extended explicitly -- otherwise
                # every backward bc target is silently missed.
                bd = w & 0xFFFC
                if bd & 0x8000:
                    bd -= 0x10000
                t = (a + bd) & 0xFFFFFFFF
                if t in targets:
                    found[t].append((a, "bcl" if w & 1 else "bc"))
            elif op == 19 and (w & 0x3FF00000) in (0x38000000, 0x38000020):
                pass  # bctr/bctrl: target is CTR, not an address

    risky = 0
    for a in addrs:
        sites = found[a]
        cond = [s for s in sites if s[1] in ("bc", "bcl")]
        if not sites:
            note = "no branch targets it  -> safe to keep-list"
        elif cond:
            note = f"RISKY: {len(cond)} conditional-branch target(s)"
            risky += 1
        else:
            note = f"direct branches only ({len(sites)}) -> tail call is correct"
        print(f"  0x{a:08X}  {note}")
        for saddr, kind in sites[:6]:
            print(f"        {kind} at 0x{saddr:08X}")
    print()
    print(f"{len(addrs)} addresses, {risky} risky")


if __name__ == "__main__":
    main()
