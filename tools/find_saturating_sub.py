#!/usr/bin/env python3
"""Find every saturating-subtract function in the recompiled image.

A 'damage' routine has a very specific shape:

    lwz  rD,off(rA)      ; load a resource from an object field
    subf rD,rB,rD        ; subtract an amount   (rD = rD - rB)
    cmplw/cmpw cr6,rD,v  ; compare against a bound
    bgt/ble ...          ; branch if already above zero
    li   rD,0            ; clamp to zero
    stw  rD,off(rA)      ; store back

The previous tool (`find_damage_pattern`) only reported sites where the load
and the store used the *same* base register and offset, which misses cases
where the object pointer is copied into another register first - very common,
because a call clobbers r3.

This variant instead scans whole *functions* and reports any that contain a
subtract whose result is compared and then conditionally zeroed, regardless of
which registers are used. Each hit is printed with the function address so it
can be disassembled and probed individually.
"""

import argparse
import re
from pathlib import Path

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
LOAD_RE = re.compile(r"^// lwz r(\d+),(-?\d+)\(r(\d+)\)\s*$")
SUBF_RE = re.compile(r"^// subf r(\d+),r(\d+),r(\d+)\s*$")
CMPWI_ZERO_RE = re.compile(r"^// cmpwi cr\d+,r(\d+),0\s*$")
CMPLW_RE = re.compile(r"^// cmplw cr\d+,r(\d+),r(\d+)\s*$")
LI_ZERO_RE = re.compile(r"^// li r(\d+),0\s*$")
STORES = (
    re.compile(r"^// stw r(\d+),(-?\d+)\(r(\d+)\)\s*$"),
    re.compile(r"^// stb r(\d+),(-?\d+)\(r(\d+)\)\s*$"),
    re.compile(r"^// sth r(\d+),(-?\d+)\(r(\d+)\)\s*$"),
)


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="tenchuz-recomp/generated/default")
    ap.add_argument("--limit", type=int, default=80)
    args = ap.parse_args()

    root = Path(args.dir)
    files = sorted(root.glob("tenchuz_recomp.*.cpp"))

    hits = []

    for path in files:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        cur_func = None
        cur_func_line = 0
        # instruction window of the current function
        buf = []

        def flush():
            if not cur_func:
                return
            # Does the function contain a subtract followed by a conditional
            # zeroing of the same register?
            for k, s in enumerate(buf):
                sm = SUBF_RE.match(s)
                if not sm:
                    continue
                dst = sm.group(1)
                # look ahead for zeroing dst or a comparison of dst
                zeroed = False
                cmp_at = None
                for j in range(k + 1, min(k + 12, len(buf))):
                    if LI_ZERO_RE.match(buf[j]) and \
                            LI_ZERO_RE.match(buf[j]).group(1) == dst:
                        zeroed = True
                        break
                    cm = CMPWI_ZERO_RE.match(buf[j])
                    if cm and cm.group(1) == dst:
                        cmp_at = j
                if zeroed:
                    # which field was subtracted from?
                    field = None
                    for j in range(k - 1, max(-1, k - 6), -1):
                        lm = LOAD_RE.match(buf[j])
                        if lm and lm.group(1) == dst:
                            field = f"{lm.group(2)}(r{lm.group(3)})"
                            break
                    hits.append((path.name, cur_func_line, cur_func,
                                 sm.group(0), field, list(buf[max(0, k - 3):k + 6])))
            buf.clear()

        for i, line in enumerate(lines):
            s = line.strip()
            fm = FUNC_RE.match(s)
            if fm:
                flush()
                cur_func = fm.group(1)
                cur_func_line = i + 1
                continue
            if s.startswith("// ") or s.startswith("loc_"):
                buf.append(s)

        flush()

    print(f"=== {len(hits)} function(s) containing a saturating subtract ===\n")
    for name, line_no, func, sub, field, seq in hits[:args.limit]:
        print(f"{func}  [{name}:{line_no}]  field {field}   ({sub})")
    if len(hits) > args.limit:
        print(f"\n... {len(hits) - args.limit} more (raise --limit)")


if __name__ == "__main__":
    main()
