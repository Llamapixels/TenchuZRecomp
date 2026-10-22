#!/usr/bin/env python3
"""Validate the .pdata parse: what fraction of codegen-known functions appear?"""
import re
import struct

BASE = 0x82000000
# Project root (this file is in tools/).
PROJECT_ROOT = __import__("pathlib").Path(__file__).resolve().parents[1]
IMAGE = str(PROJECT_ROOT) + r"\tools\tenchuz_decrypted.bin"
INIT = str(PROJECT_ROOT) + r"\tenchuz-recomp\generated\default\tenchuz_init.cpp"
PDATA_VA, PDATA_SZ = 0x8224DE00, 0x12DA8

data = open(IMAGE, "rb").read()
starts = set()
raw_count = 0
for off in range(0, PDATA_SZ, 8):
    begin = struct.unpack_from(">I", data, PDATA_VA - BASE + off)[0]
    if begin:
        raw_count += 1
        starts.add(BASE + begin)
print("non-zero .pdata entries:", raw_count)

known = set()
for line in open(INIT, errors="ignore"):
    m = re.match(r"\s*\{\s*0x([0-9A-Fa-f]{8})\s*,", line)
    if m:
        known.add(int(m.group(1), 16))
print("codegen-known functions:", len(known))
inter = known & starts
print(f"known functions present in .pdata: {len(inter)} ({100*len(inter)/max(1,len(known)):.1f}%)")
if inter:
    sample = sorted(inter)[:5]
    print("sample:", [f"0x{a:08X}" for a in sample])
# is the image base for .pdata RVAs really 0x82000000?
print("sample .pdata begins (absolute):",
      [f"0x{BASE + struct.unpack_from('>I', data, PDATA_VA - BASE + i*8)[0]:08X}" for i in range(5)])
