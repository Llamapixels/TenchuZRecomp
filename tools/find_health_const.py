#!/usr/bin/env python3
"""Find candidate 'health = 70' initialisation sites in the recompiled code.

The generated code keeps each original PowerPC instruction as a `// mnemonic`
comment, so a store of an immediate can be recognised textually:

    // li r11,70
    ctx.r11.s64 = 70;
    // stw r11,68(r31)
    REX_STORE_U32(ctx.r31.u32 + 68, ctx.r11.u32);

That sequence - load the literal 70 into a register, then store that register
into a struct field - is the standard signature of "initialise this field to
70" and is what a max/starting health assignment looks like.

This is a *search* aid, not a conclusion: 70 is a common small integer (offsets,
counts, array sizes all collide), so the output is a shortlist to eyeball or
probe, not a list of confirmed hits.
"""

import argparse
import re
from pathlib import Path

LI_RE = re.compile(r"^// li r(\d+),(\d+)\s*$")
STORE_RE = re.compile(r"^// stw r(\d+),(-?\d+)\(r(\d+)\)\s*$")
STWX_RE = re.compile(r"^// stw r(\d+),\d+\(r\d+\)\s*$")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="tenchuz-recomp/generated/default")
    ap.add_argument("--value", type=int, default=70,
                    help="immediate to look for (default 70)")
    ap.add_argument("--window", type=int, default=4,
                    help="how many instructions after the load to look for a store")
    ap.add_argument(
        "--loads",
        action="store_true",
        help="also report li sites that are NOT followed by a store nearby",
    )
    args = ap.parse_args()

    root = Path(args.dir)
    if not root.is_dir():
        raise SystemExit(f"not a directory: {root}")

    files = sorted(root.glob("tenchuz_recomp.*.cpp"))

    hits = []          # (file, line_no, reg, store_desc, context)
    loads_only = []    # (file, line_no, context)

    for path in files:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        for i, line in enumerate(lines):
            stripped = line.strip()
            m = LI_RE.match(stripped)
            if not m:
                continue
            reg = m.group(1)
            val = int(m.group(2))
            if val != args.value:
                continue

            # Look ahead for a store of the same register.
            shared = None
            store_desc = None
            for j in range(i + 1, min(i + 1 + args.window, len(lines))):
                nxt = lines[j].strip()
                sm = STORE_RE.match(nxt)
                if sm and sm.group(1) == reg:
                    store_desc = nxt
                    shared = lines[i:j + 1]
                    break
                # Any store form (stwx/stb/sth) of the same register also counts.
                if f"// st" in nxt and f" r{reg}," in nxt:
                    store_desc = nxt
                    shared = lines[i:j + 1]
                    break

            if shared:
                hits.append((path.name, i + 1, f"r{reg}", store_desc, shared))
            else:
                loads_only.append((path.name, i + 1, [lines[i].strip()]))

    print(f"=== {len(hits)} '{args.value}' load(s) stored within "
          f"{args.window} instruction(s) ===")
    for name, line_no, reg, desc, ctx in hits:
        print(f"\n{name}:{line_no}  ({reg} -> {desc})")
        for c in ctx:
            print(f"    {c}")

    if args.loads:
        print(f"\n=== {len(loads_only)} other '{args.value}' load(s) "
              f"(no nearby store) ===")
        for name, line_no, ctx in loads_only:
            print(f"{name}:{line_no}  {ctx[0]}")


if __name__ == "__main__":
    main()
