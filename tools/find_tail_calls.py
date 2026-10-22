#!/usr/bin/env python3
"""List every generated conditional *tail call* and its target.

`rexglue`'s emit_conditional_branch turns a conditional branch into

    if (!ctx.cr6.eq) {
        <target>(ctx, base);
        return;
    }

whenever the branch target is a registered entry point (TargetKind::Function).
For a target that is really an intra-function label this is wrong twice over:
it splits the function *and* calls a body that pops a guest frame it never
pushed, drifting ctx.r1.

Usage:
    python tools/find_tail_calls.py [--limit N]
"""
from __future__ import annotations

import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "..", "tenchuz-recomp", "generated", "default")

CALL_RE = re.compile(r"^\t\t([A-Za-z_][A-Za-z0-9_]*)\(ctx, base\);$")
BRANCH_COMMENT = re.compile(r"^\t// (b\w*|b)\s")


def main() -> None:
    limit = 200
    if "--limit" in sys.argv:
        limit = int(sys.argv[sys.argv.index("--limit") + 1])

    hits: dict[str, list[tuple[str, int, str]]] = {}
    for path in sorted(glob.glob(os.path.join(GEN, "tenchuz_recomp.*.cpp"))):
        lines = open(path, errors="ignore").read().splitlines()
        for i, line in enumerate(lines):
            m = CALL_RE.match(line)
            if not m:
                continue
            if i + 2 < len(lines) and lines[i + 1] == "\t\treturn;" and \
                    lines[i + 2] == "\t}":
                # The instruction comment two lines above names the branch.
                insn = lines[i - 2].strip() if i >= 2 else ""
                hits.setdefault(m.group(1), []).append(
                    (os.path.basename(path), i + 1, insn))

    total = sum(len(v) for v in hits.values())
    print(f"conditional tail calls: {total} to {len(hits)} distinct targets")
    for name in sorted(hits, key=lambda n: -len(hits[n])):
        sites = hits[name]
        ex = "; ".join(f"{f}:{ln} [{insn}]" for f, ln, insn in sites[:3])
        print(f"  {name}  x{len(sites)}   {ex}")
    if len(hits) > limit:
        print(f"... {len(hits) - limit} more")


if __name__ == "__main__":
    main()
