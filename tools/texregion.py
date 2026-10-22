#!/usr/bin/env python3
"""Region-level texture-corruption metric.

For a frame, splits the image into a grid of tiles and reports, per tile, the
local high-frequency noise plus 4x4 block-grid signature.  Garbled character
textures show up as tiles with elevated noise/blockiness compared with their
neighbours (sky/ground).

Usage: texregion.py <frame.png> [--cols 8 --rows 6]
"""
import sys

import numpy as np
from PIL import Image


def metrics(g):
    lap = (4 * g[1:-1, 1:-1] - g[:-2, 1:-1] - g[2:, 1:-1]
           - g[1:-1, :-2] - g[1:-1, 2:])
    noise = float(np.abs(lap).mean()) if lap.size else 0.0
    dx = np.abs(np.diff(g, axis=1)); dy = np.abs(np.diff(g, axis=0))
    seam = (dx[:, 3::4].mean() if dx.shape[1] >= 4 else 0) + \
           (dy[3::4, :].mean() if dy.shape[0] >= 4 else 0)
    base = dx.mean() + dy.mean() + 1e-6
    return noise, seam / base


def report(path, cols, rows):
    img = np.asarray(Image.open(path).convert("L"), dtype=np.float64)
    h, w = img.shape
    cw, ch = w // cols, h // rows
    print(f"== region {path} ({w}x{h}, {cols}x{rows}) ==")
    print("     " + " ".join(f"c{c:<6d}" for c in range(cols)))
    for r in range(rows):
        cells = []
        for c in range(cols):
            tile = img[r*ch:(r+1)*ch, c*cw:(c+1)*cw]
            n, b = metrics(tile)
            cells.append(f"{n:5.1f}/{b:4.2f}")
        print(f"  r{r}: " + " ".join(cells))
    print("  (cell = noise/blockiness; high noise + blockiness >1.4 == garble)")


def main():
    args = sys.argv[1:]
    cols, rows = 8, 6
    paths = []
    i = 0
    while i < len(args):
        if args[i] == "--cols":
            cols = int(args[i+1]); i += 2
        elif args[i] == "--rows":
            rows = int(args[i+1]); i += 2
        else:
            paths.append(args[i]); i += 1
    if not paths:
        print(__doc__); return 1
    for p in paths:
        report(p, cols, rows)
    return 0


if __name__ == "__main__":
    sys.exit(main())
