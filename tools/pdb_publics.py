#!/usr/bin/env python3
"""Index the tenchuz PDB publics stream (name -> RVA) and resolve addresses.

The linker map emitted by CMake turned out to be unreliable (its 32-bit
`__imp_` entries sit ~0x3E0 before the real function starts). The PDB publics
stream produced by llvm-pdbutil is authoritative.

Generate the input with:
    llvm-pdbutil dump --publics <exe>.pdb > pdb_publics.txt

Usage:
    python tools/pdb_publics.py resolve 0x5fc258
    python tools/pdb_publics.py next 0x5fbfe0
    python tools/pdb_publics.py stats
"""
from __future__ import annotations

import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = os.path.join(HERE, "..", "tenchuz-recomp", "out", "build",
                   "win-amd64-release", "tenchuz.exe")
PUBLICS = os.path.join(os.path.dirname(EXE), "pdb_publics.txt")

PUB_RE = re.compile(r"`(.+?)`")
ADDR_RE = re.compile(r"addr\s*=\s*([0-9A-Fa-f]+):([0-9A-Fa-f]+)")


def _open_text(path: str):
    """Open a text file, tolerating the UTF-16LE that PowerShell's `>` emits."""
    with open(path, "rb") as f:
        head = f.read(2)
    enc = "utf-16" if head in (b"\xff\xfe", b"\xfe\xff") else "utf-8"
    return open(path, "r", errors="ignore", encoding=enc)


def text_va() -> int:
    """Virtual address of the .text section (S_PUB32 offsets are section rel)."""
    data = open(EXE, "rb").read()
    e = struct.unpack_from("<I", data, 0x3C)[0]
    nsec = struct.unpack_from("<H", data, e + 6)[0]
    opt_size = struct.unpack_from("<H", data, e + 20)[0]
    sect = e + 24 + opt_size
    for i in range(nsec):
        o = sect + i * 40
        name = data[o:o + 8].rstrip(b"\0").decode("ascii", "replace")
        if name == ".text":
            return struct.unpack_from("<I", data, o + 12)[0]
    raise SystemExit(".text section not found")


def load() -> list[tuple[int, str]]:
    """Parse `llvm-pdbutil dump --publics` output.

    Each record spans two lines:

        | S_PUB32 [size = 36] `__imp__sub_8247BDF8`
        flags = function, addr = 0001:6270464

    and the same symbol is listed twice, so duplicates are collapsed.
    """
    va = text_va()
    out = {}
    pending = None
    for line in _open_text(PUBLICS):
        if "S_PUB32" in line:
            m = PUB_RE.search(line)
            pending = m.group(1) if m else None
            continue
        if pending is None:
            continue
        m2 = ADDR_RE.search(line)
        if m2:
            if m2.group(1) == "0001":
                # llvm-pdbutil prints the section offset in decimal.
                out[int(m2.group(2), 10) + va] = pending
            pending = None
    return sorted(out.items())


def main() -> None:
    syms = load()
    if not syms:
        raise SystemExit("no publics parsed; run llvm-pdbutil dump --publics")
    cmd = sys.argv[1] if len(sys.argv) > 1 else "stats"

    if cmd == "stats":
        print(f"parsed {len(syms)} publics from {os.path.basename(PUBLICS)}")
        print(f"rva range 0x{syms[0][0]:X} .. 0x{syms[-1][0]:X}")
        return

    for arg in sys.argv[2:]:
        rva = int(arg, 16)
        lo, hi = 0, len(syms)
        while lo < hi:
            mid = (lo + hi) // 2
            if syms[mid][0] <= rva:
                lo = mid + 1
            else:
                hi = mid
        if lo == 0:
            print(f"0x{rva:X}  <before first symbol>")
            continue
        a, n = syms[lo - 1]
        nxt = syms[lo] if lo < len(syms) else (0, "<end>")
        print(f"0x{rva:X}  {n}  +0x{rva - a:X}  (start 0x{a:X}, "
              f"next {nxt[1]} at 0x{nxt[0]:X}, span 0x{nxt[0] - a:X})")


if __name__ == "__main__":
    main()
