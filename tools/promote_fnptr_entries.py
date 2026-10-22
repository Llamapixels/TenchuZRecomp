#!/usr/bin/env python3
"""Classify dropped fnptr entries by the 'cannot fall in' rule.

For every address dropped as a LABEL, decide whether it is really a function
start:

  * predecessor is a function terminator (blr / bctr / b / bc-always /
    bcctrl / bclrl), so control cannot fall into it, AND
  * no conditional branch targets it.

Report the survivors (candidates to promote) and the rejects with reasons.

With ``--emit`` it writes logs/fnptr_promoted.md? no -- writes the survivor
list as a manifest-source TOML (logs/fnptr_promoted.toml) plus a plain-text
audit list (logs/promote_candidates.txt).
"""
from __future__ import annotations

import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import classify_entry_points as c  # noqa: E402
import check_branch_targets as cbt  # noqa: E402

LOG = os.path.join(HERE, "..", "logs", "manifest_dropped_labels.txt")
ENTRY_RE = re.compile(r"^(0x[0-9A-Fa-f]{8})")

TERMINATORS = {"blr", "bctr", "b", "bc(always)", "bcl(always)",
               "bcctrl", "bclrl"}


def cond_targets(data: bytes) -> set[int]:
    out: set[int] = set()
    for va, sz in cbt.EXEC_SECTIONS:
        for a in range(va, va + sz, 4):
            off = a - c.BASE
            w = int.from_bytes(data[off:off + 4], "big")
            if (w >> 26) != 16:
                continue
            bd = w & 0xFFFC
            if bd & 0x8000:
                bd -= 0x10000
            out.add((a + bd) & 0xFFFFFFFF)
    return out


def main() -> None:
    data = c.load_image()
    cond = cond_targets(data)
    promote: list[tuple[int, str]] = []
    reject: list[tuple[int, str]] = []
    for line in open(LOG, encoding="utf-8"):
        m = ENTRY_RE.match(line.strip())
        if not m:
            continue
        a = int(m.group(1), 16)
        prev = c.word_at(data, a - 4)
        d = c.describe_insn(prev)
        if a in cond:
            reject.append((a, f"conditional-branch target (prev={d})"))
        elif d in TERMINATORS:
            promote.append((a, d))
        else:
            reject.append((a, f"predecessor {d} (fallthrough)"))
    print(f"PROMOTE {len(promote)}:")
    for a, d in promote[:80]:
        print(f"  {a:#010x}  prev={d}")
    print(f"\nREJECT {len(reject)}")
    from collections import Counter
    print(Counter(r[1].split(" (")[0] for r in reject))
    with open(os.path.join(HERE, "..", "logs", "promote_candidates.txt"),
              "w", encoding="utf-8") as f:
        f.write("\n".join(f"{a:#010X}" for a, _ in promote))


if __name__ == "__main__":
    main()
