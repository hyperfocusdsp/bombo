#!/usr/bin/env python3
"""Compose the FALLOUT theme art from the nanobanana-isolated parts.

Inputs (co-registered on one 3210x5343 RGBA canvas):
  parts/body.png            - olive egg body (Bombo logo embossed)
  parts/nose.png            - bottom orange/rust nose shield (knobs absent)
  parts/displayandfins.png  - top display bezel (USED for the header) + fins
                              (fins UNUSED — drawn procedurally as clean rust)

Outputs:
  Resources/Textures/fallout_chassis.png  - body + nose, full canvas, clipped to
        the procedural silhouette by the renderer (keeps nose + macros aligned).
  Resources/Textures/fallout_header.png   - the display bezel (screen + rusty
        frame + button cutouts + chamfered shoulders), trimmed. The renderer
        anchors its GREEN-SCREEN rect onto the live scope so the frame wraps the
        scope and the cutouts land around BNC / LIM / TAIL / BPM.

The green-screen rect (as fractions of the header image) is printed below and
hardcoded in FaceplatePanel as kBezScreen{X0,Y0,X1,Y1}; re-run + update if the
bezel art changes.
"""
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
PARTS = Path("/home/user/Pictures/bombo_gui_gen/parts")
TEX = ROOT / "Resources" / "Textures"
OUT = TEX / "fallout_chassis.png"
OUT_HDR = TEX / "fallout_header.png"
TARGET_H = 1600       # body+nose composite height
HDR_W = 1280          # header bezel target width (it's a wide, short strip)


def main() -> int:
    body = Image.open(PARTS / "body.png").convert("RGBA")
    nose = Image.open(PARTS / "nose.png").convert("RGBA")
    disp = Image.open(PARTS / "displayandfins.png").convert("RGBA")
    W, H = body.size
    assert nose.size == (W, H) and disp.size == (W, H), "parts not co-registered"
    TEX.mkdir(parents=True, exist_ok=True)

    # ── Body + nose composite ───────────────────────────────────────────
    canvas = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    canvas.alpha_composite(body)
    canvas.alpha_composite(nose)
    out = canvas.resize((round(W * TARGET_H / H), TARGET_H), Image.LANCZOS)
    out.save(OUT, optimize=True)
    print(f"wrote {OUT}  size={out.size}  bytes={OUT.stat().st_size}")

    # ── Header bezel = top cluster of displayandfins (above the fin gap) ─
    bez = disp.crop((0, 0, W, int(H * 0.30)))
    bez = bez.crop(bez.getbbox())
    bw, bh = bez.size
    # Detect the dark-green screen rect (opaque, green-dominant, darkish).
    bpx = bez.load()
    xs, ys = [], []
    for y in range(0, bh, 3):
        for x in range(0, bw, 3):
            r, g, b, a = bpx[x, y]
            if a > 150 and g >= r and g >= b and (r + g + b) < 340 and g > 25:
                xs.append(x); ys.append(y)
    if xs:
        sx0, sx1, sy0, sy1 = min(xs), max(xs), min(ys), max(ys)
        print("SCREEN FRACTIONS (hardcode in FaceplatePanel):")
        print(f"  kBezScreenX0={sx0/bw:.4f}f kBezScreenY0={sy0/bh:.4f}f "
              f"kBezScreenX1={sx1/bw:.4f}f kBezScreenY1={sy1/bh:.4f}f")
    hdr = bez.resize((HDR_W, round(bh * HDR_W / bw)), Image.LANCZOS)
    hdr.save(OUT_HDR, optimize=True)
    print(f"wrote {OUT_HDR}  size={hdr.size}  bytes={OUT_HDR.stat().st_size}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
