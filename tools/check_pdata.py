#!/usr/bin/env python3
"""Cross-check candidate function entries against the .pdata unwind table.

RUNTIME_FUNCTION entries in .pdata list every real function start of a 360
image. An address present there is unambiguously a function entry; one that is
absent is likely an internal branch target (a function-segmentation artifact of
the analyzer's auto-discovery).
"""
import re
import struct

BASE = 0x82000000
# Project root (this file is in tools/).
PROJECT_ROOT = __import__("pathlib").Path(__file__).resolve().parents[1]
IMAGE = str(PROJECT_ROOT) + r"\tools\tenchuz_decrypted.bin"
FNPTR = str(PROJECT_ROOT) + r"\tools\fnptr_entries.toml"
PDATA_VA, PDATA_SZ = 0x8224DE00, 0x12DA8

data = open(IMAGE, "rb").read()
starts = set()
for off in range(0, PDATA_SZ, 8):
    begin = struct.unpack_from(">I", data, PDATA_VA - BASE + off)[0]
    if begin:
        starts.add(BASE + begin)
print(".pdata function starts:", len(starts))

unresolved = [0x825FFFAC, 0x8260F5AC, 0x8260F6A4, 0x82649F64, 0x82658200,
              0x826596D8, 0x8266179C, 0x82662A30, 0x82665CC4, 0x82665CE0,
              0x82665E3C, 0x8268FB20, 0x8281BB14]
print("--- unresolved branch targets in .pdata ---")
for a in unresolved:
    print(f"  0x{a:08X}: {'YES' if a in starts else 'no'}")

targets = []
for line in open(FNPTR):
    m = re.match(r"0x([0-9A-F]{8}) = ", line)
    if m:
        targets.append(int(m.group(1), 16))
inpd = [t for t in targets if t in starts]
print(f"--- fnptr targets found in .pdata: {len(inpd)}/{len(targets)} ---")
notin = [t for t in targets if t not in starts]
print("fnptr targets NOT in .pdata (first 15):")
for t in notin[:15]:
    print(f"  0x{t:08X}")
