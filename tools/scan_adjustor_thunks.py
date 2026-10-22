#!/usr/bin/env python3
"""Find every MSVC adjustor thunk in the image and verify it for keep-listing.

An adjustor thunk is a two-instruction function:

    addi r3, r3, N      ; adjust `this`
    b    target          ; tail-jump into the real method

MSVC gives these no `.pdata` unwind record, so tools/classify_entry_points.py
attributes them to the *previous* function and generate_manifest.py drops them.
A virtual call through a vtable that lands on one then kills the runtime with

    [FATAL] Call to invalid or unregistered function at guest address 0x...

(exactly 0x8251FC20, a run of such thunks at 0x8251FBC0..0x8251FC28).

A thunk is only safe to register when:

  1. it really is the 2-instruction pattern addi r3,r3,N / b target,
     with a *hard terminator* before it (blr / b / bctr / bclr / branch-always)
     -- a fall-through predecessor means it is mid-function, e.g. the epilogue
     `addi r1,r1,128; b __restgprlr` at 0x8251FCEC, which must stay dropped;
  2. no conditional branch targets it (tools/check_branch_targets.py rule --
     `classifyTarget` would turn `bc` into a conditional tail call, r1 drift);
  3. its tail target is a real function start: in `.pdata`, already registered,
     an XEX import record (valid tail target but NOT registerable -- see
     is_import_record), or preceded by a hard terminator. Otherwise the FATAL
     would just move to the target; such thunks are reported for manual review
     instead.

Import records: the tail of .text holds the kernel import thunk table -- each
record is `[type/ordinal word][type/ordinal word][mtctr r11][bctr]`. The
ordinal words are loader-replaced (opcode 0/1 placeholders), so the record is
data in the raw dump. Codegen registers those addresses from the XEX import
symbols as `__imp__<name>`; registering one in the manifest renames it to a
function that gets a 0-block node and no DEFINE_REX_FUNC body, which fails the
link (observed: `thunktgt_8280989C`, actually `__imp__KeResetEvent`).

Output: keep-list lines for tools/entry_points_keep.toml (also written to
logs/adjustor_thunks_keep.toml) plus a human-readable report.

Usage:
    python tools/scan_adjustor_thunks.py           # report + emit
    python tools/scan_adjustor_thunks.py --dry     # report only
"""
from __future__ import annotations

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import classify_entry_points as c  # noqa: E402
from check_branch_targets import EXEC_SECTIONS, s26  # noqa: E402

HARD_TERM_PAD = 0x00000000  # linker zero-padding: `tdi` traps, cannot fall through


def word(data: bytes, addr: int) -> int:
    off = addr - c.BASE
    return int.from_bytes(data[off:off + 4], "big")


def is_hard_terminator(data: bytes, addr: int) -> bool:
    """True when control flow CANNOT fall through `addr` into `addr+4`.

    Accepted: unconditional `b`, `blr`/`bctr`, `bclr`/`bcctr` with an
    always-taken BO (20..23), `bc` with an always-taken BO, and the linker's
    zero padding (executing 0x00000000 = tdi traps, so nothing continues).

    Rejected: `bl`/`bctrl` (call return points fall through), *conditional*
    `bc`/`bclr` (e.g. `beqlr` falls through when the condition is false --
    accepting those would misclassify mid-function code such as the
    0x8265956C -> 0x82659570 chain as a callable thunk).
    """
    w = word(data, addr)
    if w == HARD_TERM_PAD:
        return True
    if w in (0x4E800020, 0x4E800420):        # blr / bctr
        return True
    op = w >> 26
    if op == 18 and not (w & 1):             # unconditional b (LK=0)
        return True
    if op in (16, 19):                       # bc / bclr / bcctr:
        bo = (w >> 21) & 31                  # only always-taken counts
        if (bo & 0x1C) == 0x14:              # BO 0b101xx (canonical 20)
            return True
    return False


def is_import_record(data: bytes, addr: int) -> bool:
    """True for an XEX kernel-import thunk record (data, not code).

    Layout (16 bytes): [ordinal placeholder][ordinal placeholder]
    [mtctr r11 (0x7D6903A6)][bctr (0x4E800420)]. The two leading words are
    opcode 0/1 placeholders the XEX loader replaces at load time, so the raw
    dump shows invalid instructions there. Codegen registers such addresses
    from the XEX import symbols as `__imp__<name>` (verified:
    0x8280989C -> __imp__KeResetEvent) and emits no body for them -- they
    must never be manifest-registered as functions.
    """
    off = addr - c.BASE
    if off < 0 or off + 16 > len(data):
        return False
    if (word(data, addr) >> 26) not in (0, 1):
        return False
    return (word(data, addr + 8) == 0x7D6903A6
            and word(data, addr + 12) == 0x4E800420)


def branch_targets(data: bytes, target: int) -> list[tuple[int, str]]:
    """All direct b/bl/bc/bcl sites whose target == `target`.

    BD/LI sign-extension done explicitly (BC's 16-bit BD from bit 15), and
    AA=1 absolute targets handled -- ignoring either under-counts sites.
    """
    out: list[tuple[int, str]] = []
    for va, sz in EXEC_SECTIONS:
        for a in range(va, va + sz, 4):
            w = word(data, a)
            op = w >> 26
            if op == 18:
                li = w & 0x03FFFFFC
                if li & 0x02000000:
                    li -= 0x04000000
                t = (li if w & 2 else a + li) & 0xFFFFFFFF
                if t == target:
                    out.append((a, "bl" if w & 1 else "b"))
            elif op == 16:
                bd = w & 0xFFFC
                if bd & 0x8000:              # sign-extend BD from bit 15 --
                    bd -= 0x10000             # the bug that hid backward bcs
                t = (bd if w & 2 else a + bd) & 0xFFFFFFFF
                if t == target:
                    out.append((a, "bcl" if w & 1 else "bc"))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry", action="store_true", help="report only, no emit")
    args = ap.parse_args()

    data = c.load_image()
    pdata = c.pdata_starts(data)

    man_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "tenchuz-recomp", "tenchuz_manifest.toml")
    registered: set[int] = set()
    if os.path.exists(man_path):
        import re
        txt = open(man_path, encoding="utf-8").read()
        registered = {int(m, 16) for m in re.findall(r"0x([0-9A-Fa-f]{8})\s*=", txt)}

    # 1. pattern scan over every executable section
    candidates: list[tuple[int, int]] = []   # (thunk, target)
    fallthrough: list[int] = []              # addi r3,r3 / b but mid-function
    for va, sz in EXEC_SECTIONS:
        end = va + sz - 8
        a = va
        while a <= end:
            i1 = word(data, a)
            # addi rD,rA,SIMM with rD==rA==r3  (0x38 -> addi, not 0x3C addis)
            if (i1 & 0xFFE00000) == 0x38600000 and ((i1 >> 16) & 31) == 3:
                i2 = word(data, a + 4)
                if (i2 >> 26) == 18 and not (i2 & 1):      # unconditional b
                    li = i2 & 0x03FFFFFC
                    if li & 0x02000000:
                        li -= 0x04000000
                    tgt = (a + 4 + li) & 0xFFFFFFFF
                    if is_hard_terminator(data, a - 4):
                        candidates.append((a, tgt))
                    else:
                        fallthrough.append(a)
                    a += 8                                 # 2-insn body
                    continue
            a += 4

    # 2. verification gates
    keep: list[tuple[int, int]] = []
    rejected_cond: list[tuple[int, list]] = []
    rejected_target: list[tuple[int, int, str]] = []
    import_tgts: set[int] = set()
    for thunk, tgt in candidates:
        sites = branch_targets(data, thunk)
        cond = [s for s in sites if s[1] in ("bc", "bcl")]
        if cond:
            rejected_cond.append((thunk, cond))
            continue
        # target must be a real function start
        if is_import_record(data, tgt):
            # valid tail target (codegen emits an __imp__ call to it), but
            # NEVER manifest-registerable -- checked before `registered` so a
            # stale manifest entry from an earlier run cannot re-add it.
            import_tgts.add(tgt)
            keep.append((thunk, tgt))
            continue
        if tgt in pdata:
            why = ".pdata"
        elif tgt in registered:
            why = "manifest"
        elif is_hard_terminator(data, tgt - 4):
            why = "hard-term"
        else:
            rejected_target.append((thunk, tgt, "target not a function start"))
            continue
        keep.append((thunk, tgt))

    # 3. report
    thunk_addrs = {a for a, _ in keep}
    print(f"2-insn addi r3,r3,N / b patterns : {len(candidates)}")
    print(f"  mid-function (fall-through)    : {len(fallthrough)}  (epilogues etc -- correctly excluded)")
    print(f"  conditional-branch targeted    : {len(rejected_cond)}")
    print(f"  target not a function start    : {len(rejected_target)}")
    print(f"  SAFE to keep-list (thunks)     : {len(keep)}")
    # NOTE: no `not in registered` filter here -- this file is one of
    # generate_manifest.py's *sources*; re-emitting every non-pdata target is
    # what keeps the output idempotent. Filtering on the current manifest
    # instead wiped all thunktgt_* entries on the second run.
    needs = sorted({t for _, t in keep if t not in pdata
                    and t not in import_tgts and t not in thunk_addrs})
    print(f"  import-record tail targets     : {len(import_tgts)}  (kept, registered by codegen as __imp__*)")
    print(f"  non-pdata targets needing entry: {len(needs)}")
    for t in needs:
        print(f"    thunktgt 0x{t:08X}")

    for a in fallthrough:
        print(f"    excluded 0x{a:08X} (falls through -- interior code)")
    for a, cond in rejected_cond[:10]:
        print(f"    RISKY    0x{a:08X} ({len(cond)} conditional: "
              f"0x{cond[0][0]:08X})")
    for a, t, why in rejected_target[:10]:
        print(f"    REVIEW   thunk 0x{a:08X} -> 0x{t:08X} ({why})")

    if args.dry:
        return 0

    out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "logs", "adjustor_thunks_keep.toml")
    lines = [
        "# Auto-generated by tools/scan_adjustor_thunks.py -- do not edit.",
        "# MSVC adjustor thunks (.pdata-less) verified: hard-terminator",
        "# predecessor, no conditional branch targeting, real function target.",
        "",
    ]
    for a, _ in sorted(keep):
        lines.append(f'0x{a:08X} = {{ name = "thunk_{a:08X}" }}')
    for a in needs:
        lines.append(f'0x{a:08X} = {{ name = "thunktgt_{a:08X}" }}  # thunk target, no .pdata')
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"\nwrote {out_path}  ({len(keep)} thunks + {len(needs)} targets)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
