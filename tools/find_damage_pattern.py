#!/usr/bin/env python3
"""Find 'subtract a value from a struct field, clamp to zero' patterns.

This is the shape of a health/damage application:

    lwz  r11,off(r3)        ; load current value from the player object
    subf r11,r10,r11        ; value -= damage
    cmpwi cr6,r11,0
    bgt  cr6,keep
    li   r11,0              ; clamp to 0
  keep:
    stw  r11,off(r3)        ; store it back

Unlike searching for the literal 70 (which is hopeless - small integers are
everywhere), a load / subtract / clamp / store-to-same-offset sequence is highly
specific. Damage code is one of the very few places a game clamps a
subtraction at zero on an object field.

The generated recompiled code preserves the original mnemonics as `// ...`
comments, so the sequence can be matched textually with a sliding window.
"""

import argparse
import re
from pathlib import Path

# // lwz r11,120(r3)
LOAD_RE = re.compile(r"^// lwz r(\d+),(-?\d+)\(r(\d+)\)\s*$")
# // subf r11,r10,r11   (subf rD,rA,rB => rD = rB - rA)
SUBF_RE = re.compile(r"^// subf r(\d+),r(\d+),r(\d+)\s*$")
SUBI_RE = re.compile(r"^// subi r(\d+),r(\d+),(-?\d+)\s*$")
CMPWI_RE = re.compile(r"^// cmpwi cr\d+,r(\d+),(-?\d+)\s*$")
CMPW_RE = re.compile(r"^// cmpw cr\d+,r(\d+),r(\d+)\s*$")
LI_RE = re.compile(r"^// li r(\d+),(-?\d+)\s*$")
# // stw r11,120(r3)
STORE_RE = re.compile(r"^// stw r(\d+),(-?\d+)\(r(\d+)\)\s*$")


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="tenchuz-recomp/generated/default")
    ap.add_argument("--span", type=int, default=10,
                    help="max instructions from load to store")
    ap.add_argument("--limit", type=int, default=60)
    ap.add_argument("--clamp-only", action="store_true",
                    help="only report sites that clamp the result at zero")
    args = ap.parse_args()

    root = Path(args.dir)
    files = sorted(root.glob("tenchuz_recomp.*.cpp"))

    results = []

    for path in files:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        comments = [ln.strip() for ln in lines]

        for i, s in enumerate(comments):
            lm = LOAD_RE.match(s)
            if not lm:
                continue
            val_reg, load_off, load_base = lm.group(1), lm.group(2), lm.group(3)

            # Scan forward for a subtraction touching this register, then a
            # clamp, then a store of a register back to the same base+offset.
            sub_seen = False
            clamp_seen = False
            store_at = None
            for j in range(i + 1, min(i + 1 + args.span, len(comments))):
                t = comments[j]

                sm = SUBF_RE.match(t)
                if sm and (sm.group(1) == val_reg or sm.group(3) == val_reg):
                    sub_seen = True
                    continue
                sm2 = SUBI_RE.match(t)
                if sm2 and sm2.group(1) == val_reg:
                    sub_seen = True
                    continue

                cm = CMPWI_RE.match(t)
                if cm and cm.group(1) == val_reg and int(cm.group(2)) == 0:
                    clamp_seen = True
                    continue

                lt = LI_RE.match(t)
                if lt and lt.group(1) == val_reg and int(lt.group(2)) == 0:
                    clamp_seen = True
                    continue

                stm = STORE_RE.match(t)
                if stm and stm.group(2) == load_off and stm.group(3) == load_base:
                    store_at = j
                    break

            if sub_seen and store_at is not None:
                if args.clamp_only and not clamp_seen:
                    continue
                results.append((path.name, i + 1, load_off, load_base,
                                clamp_seen, store_at,
                                [comments[k] for k in range(i, store_at + 1)]))

    print(f"=== {len(results)} load/subtract/store-to-same-offset site(s) ===\n")
    for name, line_no, off, base, clamp, store_at, seq in results[:args.limit]:
        flag = "CLAMP" if clamp else "     "
        print(f"[{flag}] {name}:{line_no}  field {off}(r{base})")
        for s in seq:
            print(f"        {s}")
        print()

    if len(results) > args.limit:
        print(f"... {len(results) - args.limit} more (raise --limit)")


if __name__ == "__main__":
    main()
