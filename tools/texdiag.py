#!/usr/bin/env python3
"""Texture-artifact diagnostic for captured frames (read-only).

Characterizes garbled-looking regions without needing eyes:
  * per-channel means / alpha coverage
  * 4x4 and 8x8 block-periodicity (DXT block decode signature)
  * channel decorrelation (B/R swap signature)
  * high-frequency noise energy vs. clean reference frames (menus)

Usage: texdiag.py <frame.png> [<frame2.png> ...]
"""
import sys

import numpy as np
from PIL import Image


def blockiness(g, bs):
    """Mean absolute difference between adjacent pixels inside block seams vs
    interior.  Values >> 1 mean data repeats/changes on a `bs` grid."""
    dx = np.abs(np.diff(g, axis=1))
    dy = np.abs(np.diff(g, axis=0))
    seam_x = dx[:, bs - 1::bs].mean()
    seam_y = dy[bs - 1::bs, :].mean()
    all_x, all_y = dx.mean(), dy.mean()
    return (seam_x + seam_y) / max(all_x + all_y, 1e-6)


def noise_energy(g):
    """High-frequency energy: mean |Laplacian|."""
    lap = (4 * g[1:-1, 1:-1] - g[:-2, 1:-1] - g[2:, 1:-1]
           - g[1:-1, :-2] - g[1:-1, 2:])
    return float(np.abs(lap).mean())


def channel_corr(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    def c(a, bb):
        a = a.astype(np.float64).ravel()
        bb = bb.astype(np.float64).ravel()
        if a.std() < 1e-6 or bb.std() < 1e-6:
            return float("nan")
        return float(np.corrcoef(a, bb)[0, 1])
    return c(r, g), c(g, b), c(r, b)


def report(path):
    img = np.asarray(Image.open(path).convert("RGBA"), dtype=np.int16)
    rgb = img[..., :3].astype(np.float64)
    a = img[..., 3]
    lum = (0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2])
    # ignore near-black letterbox / near-white overlays for noise stats
    mask = (lum > 16) & (lum < 240) & (a > 0)
    gm = lum[mask] if mask.sum() > 1024 else lum.ravel()
    print(f"== {path} ==")
    print(f"  size={img.shape[1]}x{img.shape[0]} "
          f"rgb_mean=({rgb[...,0].mean():.1f},{rgb[...,1].mean():.1f},"
          f"{rgb[...,2].mean():.1f}) alpha_mean={a.mean():.1f} "
          f"alpha<255={100.0*(a<255).mean():.1f}%")
    rg, gb, rb = channel_corr(rgb)
    print(f"  channel corr R-G={rg:.3f} G-B={gb:.3f} R-B={rb:.3f} "
          f"(corrupt data -> low; normal scene -> >0.5)")
    for bs in (4, 8, 16):
        print(f"  blockiness {bs}x{bs}: {blockiness(lum, bs):.3f} "
              f"(~1.0 = no grid, >1.5 = block-grid signature)")
    print(f"  noise energy (|laplacian|)={noise_energy(lum):.2f} "
          f"(clean UI/scene ~2-8, garbage ~15+)")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    for p in sys.argv[1:]:
        report(p)
    return 0


if __name__ == "__main__":
    sys.exit(main())
