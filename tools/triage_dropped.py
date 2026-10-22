#!/usr/bin/env python3
"""Triage the 237 fnptr addresses dropped as intra-function labels.

For each dropped address, report its predecessor instruction class so we can
separate *real* function starts (predecessor is a terminator: blr/bctr/b/
bc-always -- control cannot fall into them) from genuine mid-function labels
(predecessor is straight-line code).
"""
from __future__ import annotations

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import classify_entry_points as c  # noqa: E402

LOG = os.path.join(HERE, "..", "logs", "manifest_dropped_labels.txt")
ENTRY_RE = re.compile(r"^(0x[0-9A-Fa-f]{8})")


def main() -> None:
    data = c.load_image()
    starts = c.pdata_starts(data)
    buckets: dict[str, list[int]] = {}
    for line in open(LOG, encoding="utf-8"):
        m = ENTRY_RE.match(line.strip())
        if not m:
            continue
        a = int(m.group(1), 16)
        prev = c.word_at(data, a - 4)
        d = c.describe_insn(prev)
        # Only addresses that are NOT inside a .pdata range can be real starts
        # via the predecessor rule; but for triage, bucket by predecessor.
        kind, why = c.classify(data, a, starts)
        buckets.setdefault(d, []).append(a)
    for d in sorted(buckets, key=lambda k: -len(buckets[k])):
        print(f"{d:14s} {len(buckets[d]):4d}   e.g. " +
              ", ".join(f"{x:#010x}" for x in buckets[d][:4]))
    print(f"\ntotal {sum(len(v) for v in buckets.values())}")


if __name__ == "__main__":
    main()
