#!/usr/bin/env python3
"""Find guest callbacks that are materialised in CODE and passed as arguments.

`derive_fnptr_targets.py` finds function pointers stored as DATA in .rdata.
Those are covered by `tools/fnptr_entries.toml`.

But the CRT/SDK-style registration pattern builds the callback address on the
fly and passes it in an argument register:

    lis   r5, 0x823C
    addi  r5, r5, 0x8720        ; r5 = 0x823B8720
    ...
    bl    0x822A2100            ; register(r4=0x823B8720, r5=0x823B8720, ...)

Such an address never appears in .rdata, so the data-pointer scan misses it
and the recompiler has no entry for it -> at runtime the indirect call hits:

    [FATAL] Call to invalid or unregistered function at guest address 0x.........

This tool enumerates them up front. To keep the candidate list trustworthy
every hit must satisfy ALL of:

  1. built by `lis rs,hi` + `addi/ori rd,rs,lo` -- either the same register
     (`lis r4,hi; addi r4,r4,lo`) or via a scratch register (`lis r11,hi;
     addi r4,r11,lo`, the standard MSVC argument idiom).  In the scratch form
     the scratch register must not be written between the `lis` and the
     `addi/ori`,
  2. result 4-byte aligned and inside the guest code range,
  3. the register is a PPC argument register (r3..r10),
  4. a `bl`/`bctrl` happens within CALL_WINDOW instructions, with no
     intervening write to that register (so it really is an argument),
  5. the target is preceded by a function terminator (`blr`/`bctr`/`bctrl`/
     `b`) or by padding zeros -- i.e. it sits where a function would start.

Candidate addresses that are NOT already `.pdata` starts and NOT already
registered are the ones that need to be added to the keep list.

Usage:
    python tools/scan_fnptr_args.py            # summary + missing candidates
    python tools/scan_fnptr_args.py --emit     # emit TOML for fnptr_args_entries
                                               # (every non-.pdata hit; safe to
                                               # re-run, output is stable)
"""
from __future__ import annotations

import argparse
import re
import sys

sys.path.insert(0, __file__.rsplit("\\", 1)[0])
import classify_entry_points as c  # noqa: E402

EXEC_SECTIONS = [
    (0x82270000, 0x599DDC),
    (0x82809E00, 0x99AC),
    (0x82813800, 0xA4B8),
    (0x8281DE00, 0x1D0C),
    (0x8281FC00, 0x5C80),
    (0x82825A00, 0x1350),
    (0x82826E00, 0xCC8),
    (0x82827C00, 0x21B8),
]
CODE_LO, CODE_HI = 0x82270000, 0x82829DB8
LIS_WINDOW = 40   # max distance between `lis rd` and the `addi rd,rd,lo`
CALL_WINDOW = 8   # max distance between the address being formed and the call

# Primary opcodes whose rt field is a GPR destination (heuristic; being
# over-inclusive only makes the scratch-register clobber check more
# conservative, which is the safe direction).
GPR_WRITER_OPS = frozenset(
    (3, 7, 8, 10, 11, 12, 13, 14, 15, 20, 21, 22, 23, 24, 25, 26, 27,
     28, 29, 30, 31, 32, 33, 34, 35, 40, 44))


def s16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def u32(data: bytes, va: int) -> int:
    off = va - c.BASE
    return int.from_bytes(data[off:off + 4], "big")


def registered_addresses(manifest: str) -> set[int]:
    txt = open(manifest, encoding="utf-8").read()
    return {int(m, 16) for m in re.findall(r"^(0x[0-9A-Fa-f]{8})\s*=", txt, re.M)}


def starts_like_function(data: bytes, addr: int) -> bool:
    """True when the word before `addr` looks like a function terminator."""
    prev = u32(data, addr - 4)
    op = prev >> 26
    if op == 19 and (prev & 0xFC000000) == 0x4C000000:
        return True          # blr / bctr / bctrl / bclr family
    if op == 18 and (prev & 1) == 0:
        return True          # unconditional `b` jumping away
    return prev == 0         # alignment padding


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--emit", action="store_true",
                    help="print TOML entries for missing candidates")
    args = ap.parse_args()

    data = c.load_image()
    pdata = set(c.pdata_starts(data))
    reg = registered_addresses(c.MANIFEST)

    hits: dict[int, int] = {}  # candidate addr -> first materialisation site

    for va, sz in EXEC_SECTIONS:
        for a in range(va, va + sz, 4):
            w = u32(data, a)
            op = w >> 26
            if op not in (14, 24):          # addi / ori
                continue
            rA = (w >> 16) & 31             # base (source) register
            rD = (w >> 21) & 31             # destination (argument) register
            if rA == 0:
                continue                    # `li` -- no high part involved
            if not (3 <= rD <= 10):
                continue                    # PPC argument register
            # find the `lis rA, hi` that fed the base register
            for k in range(1, LIS_WINDOW + 1):
                pa = a - 4 * k
                if pa < va:
                    break
                pw = u32(data, pa)
                if (pw >> 26) == 15 and ((pw >> 16) & 31) == 0 \
                        and ((pw >> 21) & 31) == rA:
                    # scratch idiom (`lis r11,hi; addi r4,r11,lo`): the high
                    # part must survive untouched until the addi/ori
                    if rA != rD and any(
                            (u32(data, pa + 4 * i) >> 26) in GPR_WRITER_OPS
                            and ((u32(data, pa + 4 * i) >> 21) & 31) == rA
                            for i in range(1, k)):
                        break
                    base = (pw & 0xFFFF) << 16
                    imm = w & 0xFFFF
                    val = (base + s16(imm)) if op == 14 else (base | imm)
                    val &= 0xFFFFFFFF
                    if not (CODE_LO <= val < CODE_HI) or (val & 3):
                        break
                    # require a call shortly after, register still intact
                    for j in range(1, CALL_WINDOW + 1):
                        fa = a + 4 * j
                        fw = u32(data, fa)
                        fop = fw >> 26
                        # `bl` (op18, LK=1) or `bctr`/`bctrl` (op19, LK=0/1)
                        if (fop == 18 and (fw & 1)) or \
                                (fop == 19 and (fw & 0xFC0007FF) in
                                 (0x4C000420, 0x4C000421)):
                            hits.setdefault(val, a)
                            break
                        # any write to rD before the call kills the argument
                        if fop not in (18, 19) and ((fw >> 21) & 31) == rD:
                            break
                    break

    missing = sorted(a for a in hits if a not in pdata and a not in reg)

    if args.emit:
        # Emit EVERY non-.pdata hit, not just the ones missing from the
        # current manifest: this output feeds tools/fnptr_args_entries.toml,
        # which generate_manifest.py consumes as a SOURCE. Filtering on the
        # manifest here would make a second run silently wipe the file.
        # Addresses already registered under a hand-written name from another
        # source keep that name (generate_manifest.py resolves duplicates by
        # source order, earlier sources win). Entries must look like function
        # starts (terminator/padding predecessor) -- everything else needs
        # manual review via verify_fnptr_candidates.py first.
        for a in sorted(x for x in hits
                        if x not in pdata and starts_like_function(data, x)):
            print(f'0x{a:08X} = {{ name = "extra_{a:08X}" }}   '
                  f'# site 0x{hits[a]:08X}')
        return

    print(f"code-materialised callback arguments: {len(hits)}")
    print(f"  {sum(1 for a in hits if a in pdata)} are .pdata starts (already FUNC)")
    print(f"  {sum(1 for a in hits if a in reg)} are already registered")
    print(f"  {len(missing)} MISSING")
    print()
    for a in missing:
        print(f"  0x{a:08X}   first site 0x{hits[a]:08X}   "
              f"starts_like_function={starts_like_function(data, a)}")


if __name__ == "__main__":
    main()
