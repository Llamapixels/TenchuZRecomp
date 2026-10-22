#!/usr/bin/env python3
"""Enumerate PPC vtable dispatch thunks in the decrypted guest image.

A vtable dispatch thunk is exactly 16 bytes and has the shape::

    lwz   rA, 0(r3)           ; rA = this->vtable      (base is always r3/this)
    lwz   rB, N(rA)           ; rB = vtable[N/4]
    mtspr CTR, rB             ; ctr = rB
    bctr                      ; branch to ctr (0x4E800420)

The compiler may pick different scratch registers for A/B. Two observed
families:

    r12/r11 :  lwz r12,0(r3) ; lwz r11,N(r12) ; mtspr CTR,r11 ; bctr
    r11/r11 :  lwz r11,0(r3) ; lwz r11,N(r11) ; mtspr CTR,r11 ; bctr

Both are dispatch thunks and both are mislabelled by
classify_entry_points.py: .pdata folds each whole run into a single range
owned by the function that physically precedes it, so every thunk after the
first is called an interior LABEL and dropped. They are in fact independently
callable entry points (reached only via bctr from a vtable slot, and each one
terminates in bctr itself), so a dropped thunk produces::

    [FATAL] Call to invalid or unregistered function at guest address 0x......

This scanner recovers those addresses. The recovered list is used as a
manifest source (logs/vtable_thunks_keep.toml, wired into
tools/generate_manifest.py) so codegen emits a real function for each.

Usage:
    python tools/scan_vtable_thunks.py            # print all thunk starts
    python tools/scan_vtable_thunks.py --toml     # emit manifest-source TOML
"""
from __future__ import annotations

import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
IMAGE = os.path.join(HERE, "tenchuz_decrypted.bin")
BASE = 0x82000000

EXEC_SECTIONS = [
    (".text", 0x82270000, 0x599DDC),
    (".embsec_a", 0x82809E00, 0x99AC),
    (".embsec_b", 0x82813800, 0xA4B8),
    (".embsec_c", 0x8281DE00, 0x1D0C),
    (".embsec_d", 0x8281FC00, 0x5C80),
    (".embsec_e", 0x82825A00, 0x1350),
    (".embsec_f", 0x82826E00, 0xCC8),
    (".embsec_g", 0x82827C00, 0x21B8),
]

LWZ_R12_0_R3 = 0x81830000   # lwz r12, 0(r3)
BCTR = 0x4E800420           # bctr


def u32(data: bytes, off: int) -> int:
    return struct.unpack_from(">I", data, off)[0]


def is_lwz(w: int) -> bool:
    return (w >> 26) == 32


def lwz_rt(w: int) -> int:
    return (w >> 21) & 0x1F


def lwz_ra(w: int) -> int:
    return (w >> 16) & 0x1F


def is_lwz_from(w: int, ra: int) -> bool:
    return is_lwz(w) and lwz_ra(w) == ra


def is_mtspr_ctr(w: int, rs: int) -> bool:
    # mtspr CTR, rS: opcode 31, XO=467, spr field encodes CTR=9
    if (w >> 26) != 31:
        return False
    if ((w >> 21) & 0x1F) != rs:      # RS must be the thunk scratch register
        return False
    spr = (w >> 11) & 0x3FF
    xo = (w >> 1) & 0x3FF
    return spr == (9 << 5) and xo == 467


def is_thunk(data: bytes, va: int) -> bool:
    """True when the 16 bytes at ``va`` are a vtable dispatch thunk.

    Shape: lwz rA,0(r3) ; lwz rB,N(rA) ; mtspr CTR,rB ; bctr, with A==B or
    A==r12,B==r11. The vtable base load must read ``this`` (r3) at offset 0
    and the second load must use it as base; the CTR source of mtspr must be
    exactly the register the second load wrote.
    """
    off = va - BASE
    if off < 0 or off + 16 > len(data):
        return False
    w0, w1, w2, w3 = (u32(data, off + i * 4) for i in range(4))
    if w3 != BCTR:
        return False
    # w0: lwz rA, 0(r3)  -- base must be r3, displacement 0
    if not is_lwz(w0) or lwz_ra(w0) != 3 or (w0 & 0xFFFF) != 0:
        return False
    a = lwz_rt(w0)
    if a not in (11, 12):
        return False
    # w1: lwz rB, N(rA)  -- base must be the register w0 loaded
    if not is_lwz(w1) or lwz_ra(w1) != a:
        return False
    b = lwz_rt(w1)
    # w2: mtspr CTR, rB
    return is_mtspr_ctr(w2, b)



def conditional_branch_targets(data: bytes) -> set[int]:
    """Every address targeted by a conditional branch (`bc`/`bcl`).

    A thunk that some `bc` jumps *into* is a mid-function label here (the
    branch lands past the thunk's predecessor instructions), so registering it
    as an entry point would turn that branch into a conditional tail call and
    split a live function (r1 drift). Such addresses must be rejected.
    """
    out: set[int] = set()
    for _name, lo, size in EXEC_SECTIONS:
        for a in range(lo, lo + size, 4):
            off = a - BASE
            w = u32(data, off)
            if (w >> 26) != 16:        # bc / bcl only
                continue
            bd = w & 0xFFFC
            if bd & 0x8000:            # sign-extend BD from bit 15
                bd -= 0x10000
            out.add((a + bd) & 0xFFFFFFFF)
    return out


def main() -> None:
    data = open(IMAGE, "rb").read()
    emit_toml = "--toml" in sys.argv
    cond_targets = conditional_branch_targets(data)
    found: list[int] = []
    for _name, lo, size in EXEC_SECTIONS:
        va = lo
        while va < lo + size:
            # Real dispatch thunks start on an 8-byte boundary (the compiler
            # packs them with 8-byte alignment). A 4-byte-only match is a
            # thunk-shaped sequence embedded inside a larger function body
            # (e.g. jump-table glue); promoting it would split a live
            # function, so require 8-byte alignment.
            if va % 8 == 0 and is_thunk(data, va) and va not in cond_targets:
                found.append(va)
            va += 4
    if emit_toml:
        print("# Vtable dispatch thunks (scan_vtable_thunks.py)")
        for a in found:
            print(f'0x{a:08X} = {{ name = "vthunk_{a:08X}" }}')
    else:
        for a in found:
            print(f"{a:#010x}")
        print(f"# total {len(found)}", file=sys.stderr)


if __name__ == "__main__":
    main()
