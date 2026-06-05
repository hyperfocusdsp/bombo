#!/usr/bin/env python3
"""Brand compositor for Bombo marketing assets.
Cuts the bomb silhouette out of its flat bg (flood-fill corners) and places it
on the Hyperfocus graphite+grid background with a drop shadow."""
import os
import sys
from PIL import Image, ImageDraw, ImageFilter

# ── Brand tokens (from hyperfocus global.css) ────────────────────────────
GRAPHITE = (0x0E, 0x0F, 0x12)
SLATE    = (0x2A, 0x2D, 0x33)
BONE     = (0xF4, 0xF1, 0xEA)
AMBER    = (0xFF, 0xB8, 0x00)
MUTED    = (0x8E, 0x93, 0xA0)
GRID_CELL = 32


def make_grid(w, h, cell=GRID_CELL, boost=1.0):
    """Graphite canvas with the subtle slate grid (site uses ~8% line presence)."""
    img = Image.new("RGB", (w, h), GRAPHITE)
    d = ImageDraw.Draw(img)
    # effective line ≈ 0.08*slate + 0.92*graphite, nudged up a touch for print
    f = 0.13 * boost
    line = tuple(int(GRAPHITE[i] + (SLATE[i] - GRAPHITE[i]) * f) for i in range(3))
    for x in range(0, w, cell):
        d.line([(x, 0), (x, h)], fill=line, width=1)
    for y in range(0, h, cell):
        d.line([(0, y), (w, y)], fill=line, width=1)
    return img


def cutout(path, thresh=120):
    """Flood-fill the flat background from all 4 corners -> transparent RGBA.
    Removes the solid bg AND any faint compositor bleed in the corner wedges,
    leaving only the bright chassis silhouette."""
    img = Image.open(path).convert("RGB")
    w, h = img.size
    seed_rgb = (255, 0, 255)  # sentinel
    flood = img.copy()
    for corner in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        ImageDraw.floodfill(flood, corner, seed_rgb, thresh=thresh)
    # Build alpha: sentinel pixels -> transparent, else opaque
    rgba = img.convert("RGBA")
    fpx = flood.load()
    apx = rgba.load()
    for y in range(h):
        for x in range(w):
            if fpx[x, y] == seed_rgb:
                r, g, b, _ = apx[x, y]
                apx[x, y] = (r, g, b, 0)
    return rgba


def drop_shadow(rgba, blur=24, offset=(0, 14), opacity=140):
    """Soft shadow from the silhouette's alpha."""
    w, h = rgba.size
    pad = blur * 3
    canvas = Image.new("RGBA", (w + pad * 2, h + pad * 2), (0, 0, 0, 0))
    alpha = rgba.split()[3]
    shadow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    shmask = Image.new("L", canvas.size, 0)
    shmask.paste(alpha, (pad + offset[0], pad + offset[1]))
    shmask = shmask.filter(ImageFilter.GaussianBlur(blur))
    shmask = shmask.point(lambda v: int(v * opacity / 255))
    shadow.putalpha(shmask)
    canvas = Image.alpha_composite(canvas, shadow)
    canvas.alpha_composite(rgba, (pad, pad))
    return canvas


SHOTS = os.environ.get("BOMBO_SHOTS", os.path.expanduser("~/Pictures/bombo_screenshots"))
OUT = "/tmp/bombo_review/final"
THEMES = ["vault", "bandw", "nightrun", "matrix", "cyber", "plasma", "fallout"]


def trim_to_content(rgba):
    bb = rgba.getbbox()
    return rgba.crop(bb) if bb else rgba


def build_hero(theme="vault", W=1080, H=1920):
    """Portrait hero: the chassis on the brand grid with a soft drop shadow."""
    chassis = trim_to_content(cutout(f"{SHOTS}/{theme}.png"))
    target_h = int(H * 0.80)
    s = target_h / chassis.height
    chassis = chassis.resize((int(chassis.width * s), target_h), Image.LANCZOS)
    if chassis.width > int(W * 0.86):
        s2 = int(W * 0.86) / chassis.width
        chassis = chassis.resize((int(chassis.width * s2), int(chassis.height * s2)), Image.LANCZOS)
    sh = drop_shadow(chassis, blur=30, offset=(0, 18), opacity=150)
    canvas = make_grid(W, H).convert("RGBA")
    cx = (W - sh.width) // 2
    cy = (H - sh.height) // 2
    canvas.alpha_composite(sh, (cx, cy))
    out = f"{OUT}/bombo-ui.webp"
    canvas.convert("RGB").save(out, "WEBP", quality=92)
    print(f"hero -> {out}  {canvas.size}")


def build_lineup(target_h=560, gap=-40, margin=70):
    """Row of all 7 finishes, every OTHER chassis vertically flipped (they
    tessellate — the bomb is wide-top/narrow-bottom), on the brand grid."""
    cuts = []
    for i, t in enumerate(THEMES):
        c = trim_to_content(cutout(f"{SHOTS}/{t}.png"))
        s = target_h / c.height
        c = c.resize((int(c.width * s), target_h), Image.LANCZOS)
        if i % 2 == 1:
            c = c.transpose(Image.FLIP_TOP_BOTTOM)   # vertical flip
        cuts.append(c)
    total_w = sum(c.width for c in cuts) + gap * (len(cuts) - 1) + margin * 2
    band_h = target_h + margin * 2
    canvas = make_grid(total_w, band_h).convert("RGBA")
    x = margin
    for c in cuts:
        sh = drop_shadow(c, blur=20, offset=(0, 10), opacity=120)
        # drop_shadow pads by blur*3; align the chassis origin, not the padded box
        pad = 20 * 3
        canvas.alpha_composite(sh, (x - pad, margin - pad))
        x += c.width + gap
    out = f"{OUT}/bombo-themes.webp"
    canvas.convert("RGB").save(out, "WEBP", quality=92)
    print(f"lineup -> {out}  {canvas.size}")


FONT_DIR = "/usr/share/fonts/TTF"
def _font(name, size):
    from PIL import ImageFont
    return ImageFont.truetype(f"{FONT_DIR}/{name}", size)


def build_og(theme="fallout", W=1200, H=630):
    """Social share card (OG, 1200x630): FALLOUT chassis on grid (left) +
    wordmark / tagline / formats (right), Plex type, amber accent."""
    canvas = make_grid(W, H).convert("RGBA")
    # chassis, left
    ch = trim_to_content(cutout(f"{SHOTS}/{theme}.png"))
    s = int(H * 0.86) / ch.height
    ch = ch.resize((int(ch.width * s), int(H * 0.86)), Image.LANCZOS)
    sh = drop_shadow(ch, blur=22, offset=(0, 12), opacity=140)
    pad = 22 * 3
    canvas.alpha_composite(sh, (40 - pad, (H - ch.height) // 2 - pad))
    # text block, right — anchored to the chassis top (wordmark) and nose tip (url)
    d = ImageDraw.Draw(canvas)
    tx = 40 + ch.width + 70
    ctop = (H - ch.height) // 2          # chassis top edge on canvas
    cbot = ctop + ch.height              # chassis bottom = the bomb's nose tip
    # wordmark cap-top aligned to the chassis top
    d.text((tx, ctop - 8), "BOMBO", font=_font("IBMPlexSans-Bold.ttf", 104), fill=BONE)
    # dictionary-style gloss for the uninitiated (cheap curiosity-gap, deadpan)
    d.text((tx + 5, ctop + 120), 'bombo  ·  Spanish for "bass drum"',
           font=_font("IBMPlexMono-Regular.ttf", 23), fill=MUTED)
    d.text((tx + 4, ctop + 164), "Classified ordnance for the kick.",
           font=_font("IBMPlexMono-Medium.ttf", 30), fill=BONE)
    d.rectangle([tx + 5, ctop + 220, tx + 70, ctop + 224], fill=AMBER)   # amber rule
    d.text((tx + 5, ctop + 248), "Free  ·  VST3  ·  AU  ·  CLAP  ·  Standalone",
           font=_font("IBMPlexMono-Regular.ttf", 25), fill=BONE)
    d.text((tx + 5, ctop + 288), "Linux  ·  macOS  ·  Windows",
           font=_font("IBMPlexMono-Regular.ttf", 25), fill=MUTED)
    # amber url baseline aligned to the chassis nose tip
    d.text((tx + 5, cbot - 34), "hyperfocusdsp.com",
           font=_font("IBMPlexMono-Medium.ttf", 26), fill=AMBER)
    out = f"{OUT}/bombo-og.png"
    canvas.convert("RGB").save(out)
    print(f"og -> {out}  {canvas.size}")


if __name__ == "__main__":
    import os
    os.makedirs(OUT, exist_ok=True)
    build_hero("fallout")
    build_lineup()
    build_og("fallout")
