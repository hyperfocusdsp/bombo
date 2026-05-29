#!/usr/bin/env python3
"""Slice a rack render into N vertical column strips at pixel-precise seams.

The genai rack has N distinctly-COLORED columns. Rather than luminance minima
(which snap to dark baked-in labels), we detect column boundaries as peaks in
the horizontal COLOR gradient of a tall mid-band: a column's average hue is
dominated by its fill, so label text barely moves it, while a column-to-column
transition is a large, sharp color jump. The N-1 biggest, well-separated peaks
are the internal seams; the outer cuts are the image edges (so the end strips
keep the rack's chamfered frame). Writes strips + a red-line cut preview.

Usage:
  python3 tools/rack_slice.py [SRC.png] [OUT_DIR] [PREVIEW.png]
Defaults target the original rack.png.
"""

import os
import sys
import numpy as np
from PIL import Image

DEF_SRC = os.path.expanduser("~/Pictures/bombo_gui_gen/parts/rack.png")
DEF_OUTD = os.path.expanduser("~/Pictures/bombo_gui_gen/parts/rack_strips")
DEF_PREVIEW = os.path.expanduser("~/Pictures/bombo_gui_gen/parts/rack_cuts_preview.png")
NCOL = 7


def smooth1(v, k):
    return np.convolve(v, np.ones(k) / k, mode="same")


def slice_rack(src, outd, preview):
    os.makedirs(outd, exist_ok=True)
    img = Image.open(src).convert("RGBA")
    a = np.asarray(img).astype(np.float64)
    H, W, _ = a.shape

    band = a[int(H * 0.12):int(H * 0.88), :, :3].mean(axis=0)  # (W,3) avg colour per x
    sm = np.stack([smooth1(band[:, c], 21) for c in range(3)], axis=1)
    d = np.zeros(W)
    d[1:] = np.sqrt(((sm[1:] - sm[:-1]) ** 2).sum(axis=1))  # colour gradient
    d = smooth1(d, 9)

    margin = int(W * 0.07)
    sep = (W / NCOL) * 0.55
    seams = []
    for x in (int(i) for i in np.argsort(d)[::-1]):
        if margin < x < W - margin and all(abs(x - s) > sep for s in seams):
            seams.append(x)
        if len(seams) == NCOL - 1:
            break
    seams.sort()
    cuts = [0] + seams + [W]

    print(f"{os.path.basename(src)}  ({W}x{H})")
    for i in range(NCOL):
        x0, x1 = cuts[i], cuts[i + 1]
        img.crop((x0, 0, x1, H)).save(os.path.join(outd, f"strip_{i + 1}.png"))
        print(f"  strip_{i + 1}.png  x[{x0:4d}..{x1:4d}]  w={x1 - x0:4d}px")

    arr = np.asarray(img).copy()
    for cx in cuts:
        for dx in (-1, 0, 1):
            x = min(max(cx + dx, 0), W - 1)
            arr[:, x, :3] = (255, 0, 0)
            arr[:, x, 3] = 255
    Image.fromarray(arr).save(preview)
    print(f"  cuts(x) = {cuts}")
    print(f"  -> {outd}\n  -> {preview}")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else DEF_SRC
    outd = sys.argv[2] if len(sys.argv) > 2 else DEF_OUTD
    preview = sys.argv[3] if len(sys.argv) > 3 else DEF_PREVIEW
    slice_rack(src, outd, preview)


if __name__ == "__main__":
    main()
