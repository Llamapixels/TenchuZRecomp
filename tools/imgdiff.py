#!/usr/bin/env python3
"""Frame comparison helper for capture evidence.

Usage:
  imgdiff.py <a.png> <b.png> [...]      pairwise frame diff
  imgdiff.py --grid <a.png>             6x4 luminance grid + edge density

Read-only diagnostic; used to tell a static menu from an animated scene and to
prove that a key press changed the picture (movement / navigation evidence).
"""
import sys

from PIL import Image, ImageFilter

GRID_COLS = 6
GRID_ROWS = 4


def gray_small(path, size=(480, 270)):
    img = Image.open(path).convert("L")
    if img.size != size:
        img = img.resize(size, Image.BILINEAR)
    return img


def mad(a, b):
    pa, pb = list(a.getdata()), list(b.getdata())
    tot = sum(abs(x - y) for x, y in zip(pa, pb))
    big = sum(1 for x, y in zip(pa, pb) if abs(x - y) > 16)
    return tot / len(pa), 100.0 * big / len(pa)


def grid(path):
    img = gray_small(path, size=(640, 360))
    w, h = img.size
    edges = img.filter(ImageFilter.FIND_EDGES)
    cw, ch = w // GRID_COLS, h // GRID_ROWS
    print(f"== grid {path} ==")
    for r in range(GRID_ROWS):
        cells = []
        for c in range(GRID_COLS):
            box = (c * cw, r * ch, (c + 1) * cw, (r + 1) * ch)
            region = img.crop(box)
            data = list(region.getdata())
            mean = sum(data) / len(data)
            edata = list(edges.crop(box).getdata())
            edens = sum(edata) / len(edata)
            cells.append(f"{mean:6.1f}/{edens:4.1f}")
        print("  " + " ".join(cells))
    data = list(img.getdata())
    print(f"  overall mean={sum(data)/len(data):.1f}")


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    if args[0] == "--grid":
        for p in args[1:]:
            grid(p)
        return 0
    imgs = [gray_small(p) for p in args]
    print("== pairwise diff (MAD, %>16 changed) ==")
    for i in range(len(imgs) - 1):
        m, pct = mad(imgs[i], imgs[i + 1])
        print(f"  {args[i].split(chr(92))[-1]} -> {args[i+1].split(chr(92))[-1]}: "
              f"MAD={m:6.2f} changed={pct:5.1f}%")
    for p, im in zip(args, imgs):
        data = list(im.getdata())
        mean = sum(data) / len(data)
        print(f"  {p.split(chr(92))[-1]}: mean={mean:.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
