#!/usr/bin/env python3
"""Resolve host RVAs to function + source location with llvm-symbolizer.

pdb_publics.py relied on a pdb_publics.txt dump that goes stale after every
rebuild; llvm-symbolizer reads the current tenchuz.pdb directly and also gives
the generated .cpp line. Addresses must be absolute (image base 0x140000000).

Usage:
    python tools/rva_sym.py 0x5f1ab8 0x654f6 ...
"""
from __future__ import annotations

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "..", "tenchuz-recomp", "out", "build",
                     "win-amd64-release")
EXE = os.path.join(BUILD, "tenchuz.exe")
IMAGE_BASE = 0x140000000
SYM = (r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools"
       r"\VC\Tools\MSVC\14.51.36231\bin\Hostx64\x64\llvm-symbolizer.exe")


def main() -> None:
    rvas = []
    for a in sys.argv[1:]:
        rvas.append(int(a, 16) & 0xFFFFFFF)
    stdin = "\n".join(f"0x{IMAGE_BASE + r:x}" for r in rvas) + "\n"
    out = subprocess.run([SYM, f"--exe={EXE}", "--functions=linkage"],
                         input=stdin, capture_output=True, text=True)
    lines = out.stdout.splitlines()
    # llvm-symbolizer emits one blank-separated block per address:
    #   function\nfile:line:col\n
    blocks = []
    cur: list[str] = []
    for ln in lines:
        if ln.strip() == "":
            if cur:
                blocks.append(cur)
                cur = []
        else:
            cur.append(ln)
    if cur:
        blocks.append(cur)
    for r, blk in zip(rvas, blocks):
        fn = blk[0] if blk else "?"
        loc = blk[1] if len(blk) > 1 else ""
        print(f"0x{r:07X}  {fn:40s} {loc}")
    if out.stderr.strip():
        print(out.stderr, file=sys.stderr)


if __name__ == "__main__":
    main()
