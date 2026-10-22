#!/usr/bin/env python3
"""Verify the bytes present at a guest address in the extracted XEX.

Reads the raw-ish image and reports the instruction words at an address, so a
runtime patch's expected-value guard can be checked against reality rather
than against hand-assembled arithmetic.
"""

import argparse
import struct
from pathlib import Path

# Xbox 360 XEX2 for this title is unpacked at load, but the .text section is
# stored compressed in the file. The recompiler already resolves the loaded
# mapping, so we instead read the *decoded* form: the extraction tool leaves a
# flat image. Fall back to scanning for the known signature if not present.
CANDIDATES = [
    "extracted/default.xex",
    "extracted/image.bin",
    "extracted/default_dec.bin",
]


def find_image() -> Path:
    for c in CANDIDATES:
        p = Path(c)
        if p.is_file():
            return p
    raise SystemExit("no extracted image found; checked: " + ", ".join(CANDIDATES))


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("address", help="guest address, e.g. 0x8229A940")
    ap.add_argument("--count", type=int, default=4)
    ap.add_argument("--load-base", default="0x82000000",
                    help="guest address the image's file offset 0 maps to")
    ap.add_argument("--file-offset", type=int, default=None,
                    help="explicit file offset to read instead of computing one")
    args = ap.parse_args()

    addr = int(args.address, 0)
    img = find_image()
    data = img.read_bytes()
    print(f"image: {img}  ({len(data)} bytes)")

    if args.file_offset is not None:
        off = args.file_offset
    else:
        off = addr - int(args.load_base, 0)
    print(f"guest 0x{addr:08X} -> file offset 0x{off:X}")

    if off < 0 or off + 4 * args.count > len(data):
        raise SystemExit("offset out of range for this image")

    for i in range(args.count):
        word = struct.unpack_from(">I", data, off + i * 4)[0]
        print(f"  0x{addr + i * 4:08X}: 0x{word:08X}")


if __name__ == "__main__":
    main()
