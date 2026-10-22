#!/usr/bin/env python3
"""Append the scanned vtable dispatch thunks to tools/entry_points_keep.toml."""
from __future__ import annotations

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
KEEP = os.path.join(HERE, "entry_points_keep.toml")

HEADER = """
# --- Vtable dispatch thunks (batch 2026-10-19) ----------------------------
# tools/scan_vtable_thunks.py finds 296 16-byte virtual-method dispatch
# thunks of the exact shape
#     lwz r12, 0(r3)  ; lwz r11, N(r12)  ; mtspr CTR,r11  ; bctr
# laid out consecutively at 16-byte boundaries.  They are materialized as
# function addresses via lis+addi (so they appear in fnptr_lis_addi.toml) but
# .pdata records each whole run as a single huge range owned by the function
# that physically precedes it (e.g. 0x822D2FB0 spans 0x822D2FB0..0x822D52F0).
# classify_entry_points.py trusts .pdata, so every thunk after the first in a
# run is labelled an interior LABEL and dropped.  At runtime the game invokes
# one such slot and dies with
#     [FATAL] Call to invalid or unregistered function at guest address 0x822D4A20
# A thunk is *not* an interior label of the preceding function: it is entered
# only via bctr from a vtable slot and always terminates in bctr itself, so it
# must be registered as its own entry point.  Verified boundaries: each run
# starts after padding / the preceding function's terminator and ends at the
# next real function prologue.  Runs:
#   0x822D4040-0x822D5230 (288), 0x82339720-0x82339760 (5), 0x826B0E48-0x826B0E68 (3)
"""


def main() -> None:
    out = subprocess.run([sys.executable, os.path.join(HERE, "scan_vtable_thunks.py")],
                         capture_output=True, text=True, check=True).stdout
    thunks = [ln.strip() for ln in out.splitlines() if ln.strip()]
    keep = open(KEEP, encoding="utf-8").read()
    new = [t for t in thunks if t not in keep]
    with open(KEEP, "a", encoding="utf-8") as f:
        f.write(HEADER)
        for t in new:
            up = t.upper().replace("0X", "0x")
            f.write(f'{up} = {{ name = "vthunk_{t[2:].upper()}" }}\n')
    print(f"thunks scanned={len(thunks)} appended={len(new)}")


if __name__ == "__main__":
    main()
