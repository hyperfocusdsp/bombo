#!/usr/bin/env python3
"""bombshape_gen.py — parametric Mini-Nuke-style bomb silhouette generator.

Outputs SVG path strings + ready-to-paste SVG groups for the visual
companion. Lets us iterate silhouettes by changing parameters instead
of hand-editing bezier points.

Coordinate system: 360 × 640 canvas, 9:16 aspect (IG Reels native).
Nose points DOWN. Rear cap + fins at the TOP. Y grows downward.
"""

from __future__ import annotations

import math
from dataclasses import dataclass, field
from typing import List


# ── Palette (locked: VAULT theme proposal) ────────────────────────────
COL_BODY     = "#5B6B43"   # olive green body
COL_BODY_HI  = "#7E9560"   # cartoon highlight
COL_BODY_LO  = "#3A4630"   # cartoon shadow
COL_NOSE     = "#B43F32"   # red rounded nose
COL_NOSE_HI  = "#D9695B"   # nose highlight
COL_FIN      = "#B43F32"   # red fins (matches nose by Mini-Nuke convention)
COL_FIN_HI   = "#D9695B"
COL_BAND     = "#E8B528"   # yellow hazard band
COL_TRIM     = "#FFE6B8"
COL_AMBER    = "#FFB800"
COL_OUTLINE  = "#15180F"


@dataclass
class BombShape:
    # Chamfer fraction for square_chamfered / trapezoid_chamfered fins —
    # what fraction of fin height the diagonal cut spans. 0 = full
    # rectangle, 1 = full chamfer (≈ triangle).
    fin_chamfer_frac: float = 0.35

    # Unified silhouette mode: when True, draw body and nose as ONE
    # continuous egg-shape path (from cap to tip), with the red "nose"
    # rendered as a clipped paint region — not a separate path. This
    # gives a truly seamless outline.
    unified_silhouette: bool = False
    # When unified_silhouette=True: y position where the red paint region
    # starts (defaults to where nose_top_y was). 0 = top, 640 = bottom.
    red_region_top_y: int = 460
    # Tip sharpness in unified mode. 0.0 = teardrop with very rounded
    # bottom, 1.0 = sharp pointed tip. Affects the curve from the
    # body-bottom widest area down to the tip.
    unified_tip_sharpness: float = 0.5

    # Canvas
    width: int = 360
    height: int = 640

    # Body (egg-shape ovoid)
    body_top_y: int = 60          # where the body starts under the cap
    body_bot_y: int = 500         # where the body ends before the nose
    body_bulge_w: int = 240       # widest point of body (around the middle)
    body_top_w: int = 110         # narrow at top (where rear cap sits)
    body_bot_w: int = 170         # how narrow before nose joins
    body_bulge_y_frac: float = 0.55  # 0.5 = mid, >0.5 = bulge lower

    # Rear cap (cylindrical assembly at top)
    cap_top_y: int = 14
    cap_bot_y: int = 64           # should slightly overlap body_top_y for seam
    cap_w: int = 100              # cap width — sits under body_top_w
    cap_inner_w: int = 70         # inner detail of cap

    # Red rounded nose (bulbous, hemispherical-ish)
    nose_top_y: int = 498         # where nose starts (overlaps body_bot_y)
    nose_bot_y: int = 600
    nose_w: int = 200             # widest point of nose (should be near body_bot_w)
    nose_curve: float = 1.15      # >1 = more bulbous, <1 = flatter

    # Side fins (rear stabilizers — sit near the TOP / rear-cap area)
    fin_top_y: int = 30
    fin_bot_y: int = 130
    fin_tip_y_frac: float = 0.5   # where the outer fin tip sits between top and bot
    fin_out_x: int = 38           # how far fins extend beyond body edge
    fin_style: str = "angular_v"  # angular_v | straight | swept

    # Yellow band (atompunk hazard cartouche)
    band_top_y: int = 86
    band_bot_y: int = 128
    band_inset_x: int = 8         # inset from body edge so band doesn't poke out

    # Highlights / detail toggles
    show_rivets: bool = True
    show_portholes: bool = True
    show_cartouche: bool = True
    show_smiley: bool = True
    show_cartoon_highlight: bool = True


def cx(s: BombShape) -> float:
    return s.width / 2.0


def unified_silhouette_path(s: BombShape) -> str:
    """Single continuous egg-shape from cap-area down to nose tip.

    Used when s.unified_silhouette=True. The "nose" is then a paint
    region inside this path, not a separate shape — outline is one
    continuous curve.
    """
    c = cx(s)
    bulge_y = s.body_top_y + (s.body_bot_y - s.body_top_y) * s.body_bulge_y_frac

    top_l = (c - s.body_top_w / 2, s.body_top_y)
    top_r = (c + s.body_top_w / 2, s.body_top_y)
    mid_l = (c - s.body_bulge_w / 2, bulge_y)
    mid_r = (c + s.body_bulge_w / 2, bulge_y)
    # Where the body would have stopped — now this is the widest-low
    # control row before the tip taper.
    low_l = (c - s.body_bot_w / 2, s.body_bot_y)
    low_r = (c + s.body_bot_w / 2, s.body_bot_y)
    tip = (c, s.nose_bot_y)

    # Tangent extensions
    pull_upper = 0.6
    upper_pull_y = s.body_top_y + (bulge_y - s.body_top_y) * pull_upper
    lower_pull_y = bulge_y + (s.body_bot_y - bulge_y) * 0.4

    # Tip taper geometry. unified_tip_sharpness controls how pointy:
    #   0.0 → control point near tip is pulled OUT (very rounded teardrop)
    #   1.0 → control point near tip pulled IN (sharp point)
    tip_round = s.body_bot_w * (0.55 - 0.45 * s.unified_tip_sharpness)
    # Distance below low_r where the side tangent transitions toward the tip
    neck_ext = (s.nose_bot_y - s.body_bot_y) * (0.55 + 0.25 * (1 - s.unified_tip_sharpness))

    return (
        f"M {top_l[0]:.1f} {top_l[1]:.1f} "
        f"L {top_r[0]:.1f} {top_r[1]:.1f} "
        # Right: top_r → mid_r (upper egg curve)
        f"C {mid_r[0]:.1f} {upper_pull_y:.1f}, "
        f"{mid_r[0]:.1f} {upper_pull_y:.1f}, "
        f"{mid_r[0]:.1f} {bulge_y:.1f} "
        # mid_r → low_r (lower egg curve)
        f"C {mid_r[0]:.1f} {lower_pull_y:.1f}, "
        f"{mid_r[0]:.1f} {lower_pull_y:.1f}, "
        f"{low_r[0]:.1f} {low_r[1]:.1f} "
        # low_r → tip (continuous taper, no kink)
        f"C {low_r[0]:.1f} {low_r[1] + neck_ext:.1f}, "
        f"{c + tip_round:.1f} {tip[1]:.1f}, "
        f"{tip[0]:.1f} {tip[1]:.1f} "
        # tip → low_l (mirror)
        f"C {c - tip_round:.1f} {tip[1]:.1f}, "
        f"{low_l[0]:.1f} {low_l[1] + neck_ext:.1f}, "
        f"{low_l[0]:.1f} {low_l[1]:.1f} "
        # low_l → mid_l
        f"C {mid_l[0]:.1f} {lower_pull_y:.1f}, "
        f"{mid_l[0]:.1f} {lower_pull_y:.1f}, "
        f"{mid_l[0]:.1f} {bulge_y:.1f} "
        # mid_l → top_l
        f"C {mid_l[0]:.1f} {upper_pull_y:.1f}, "
        f"{mid_l[0]:.1f} {upper_pull_y:.1f}, "
        f"{top_l[0]:.1f} {top_l[1]:.1f} "
        f"Z"
    )


def body_path(s: BombShape) -> str:
    """Egg-shape ovoid as a closed path with cubic beziers.

    Symmetric. Bulge biased toward `body_bulge_y_frac` of body height.
    Top width = body_top_w, bottom width = body_bot_w, mid bulge = body_bulge_w.
    """
    c = cx(s)
    bulge_y = s.body_top_y + (s.body_bot_y - s.body_top_y) * s.body_bulge_y_frac

    # Anchor points (on the silhouette outline)
    top_l = (c - s.body_top_w / 2, s.body_top_y)
    top_r = (c + s.body_top_w / 2, s.body_top_y)
    mid_l = (c - s.body_bulge_w / 2, bulge_y)
    mid_r = (c + s.body_bulge_w / 2, bulge_y)
    bot_l = (c - s.body_bot_w / 2, s.body_bot_y)
    bot_r = (c + s.body_bot_w / 2, s.body_bot_y)

    # Bezier control points — push outward to create roundness
    # Going clockwise: top_l → bulge via outer → mid_l → bulge inner → bot_l
    # Control "outer pull" makes the body more bulbous; "inner pull" keeps shoulders smooth.
    pull = 0.6  # how strongly the curve pulls toward the bulge
    upper_pull_y = s.body_top_y + (bulge_y - s.body_top_y) * pull
    lower_pull_y = bulge_y + (s.body_bot_y - bulge_y) * (1 - pull)

    # Right side: top_r down to mid_r down to bot_r
    # Using cubic beziers for smooth ovoid
    path = (
        f"M {top_l[0]:.1f} {top_l[1]:.1f} "
        f"L {top_r[0]:.1f} {top_r[1]:.1f} "
        # Right side: top_r → mid_r → bot_r via cubic
        f"C {mid_r[0]:.1f} {upper_pull_y:.1f}, "
        f"{mid_r[0]:.1f} {upper_pull_y:.1f}, "
        f"{mid_r[0]:.1f} {bulge_y:.1f} "
        f"C {mid_r[0]:.1f} {lower_pull_y:.1f}, "
        f"{mid_r[0]:.1f} {lower_pull_y:.1f}, "
        f"{bot_r[0]:.1f} {bot_r[1]:.1f} "
        f"L {bot_l[0]:.1f} {bot_l[1]:.1f} "
        # Left side: bot_l → mid_l → top_l (mirror)
        f"C {mid_l[0]:.1f} {lower_pull_y:.1f}, "
        f"{mid_l[0]:.1f} {lower_pull_y:.1f}, "
        f"{mid_l[0]:.1f} {bulge_y:.1f} "
        f"C {mid_l[0]:.1f} {upper_pull_y:.1f}, "
        f"{mid_l[0]:.1f} {upper_pull_y:.1f}, "
        f"{top_l[0]:.1f} {top_l[1]:.1f} "
        f"Z"
    )
    return path


def nose_path(s: BombShape) -> str:
    """Red nose as a SMOOTH EXTENSION of the body's curvature.

    Anchored to body_bot_l / body_bot_r with a near-vertical tangent — so
    the seam between body and nose reads as one continuous silhouette, not
    a mushroom cap. Sweeps inward to a gently-rounded tip at nose_bot_y.

    Tunable via:
      - nose_neck_frac: how long the cylindrical-neck section is before the
        curve starts pulling inward. Higher = more "body extending downward"
        feel, lower = sharper taper toward tip.
      - nose_tip_round: how flat/rounded the very tip is (radius-equivalent).
    """
    c = cx(s)
    top_l = (c - s.body_bot_w / 2, s.nose_top_y)
    top_r = (c + s.body_bot_w / 2, s.nose_top_y)
    tip = (c, s.nose_bot_y)

    h = s.nose_bot_y - s.nose_top_y  # nose height

    # Neck-tangent extension: control point projected straight DOWN from the
    # body edge. The further it projects, the more the nose top reads as
    # "body extending downward" rather than a separate cap. Capped at 80% of
    # nose height so the tip still has room to round off.
    neck_ext = min(h * 0.85, h * 0.55 * s.nose_curve)
    # Tip-tangent: control point near the tip pulled OUT laterally — defines
    # how rounded the tip is. Larger = flatter/more rounded tip.
    tip_round = s.body_bot_w * 0.42

    return (
        f"M {top_l[0]:.1f} {top_l[1]:.1f} "
        f"L {top_r[0]:.1f} {top_r[1]:.1f} "
        # Right side: top_r → tip
        # CP1 straight down from top_r (preserves body's vertical tangent)
        # CP2 near the tip, offset laterally → rounded tip tangent
        f"C {top_r[0]:.1f} {top_r[1] + neck_ext:.1f}, "
        f"{c + tip_round:.1f} {tip[1]:.1f}, "
        f"{tip[0]:.1f} {tip[1]:.1f} "
        # Left side: tip → top_l (mirror)
        f"C {c - tip_round:.1f} {tip[1]:.1f}, "
        f"{top_l[0]:.1f} {top_l[1] + neck_ext:.1f}, "
        f"{top_l[0]:.1f} {top_l[1]:.1f} "
        f"Z"
    )


def cap_path(s: BombShape) -> str:
    """Rear cap cylinder at the very top, with subtle taper into the body shoulder."""
    c = cx(s)
    # Outer cap (sits behind/under fins)
    outer_l = (c - s.cap_w / 2, s.cap_top_y)
    outer_r = (c + s.cap_w / 2, s.cap_top_y)
    # Cap meets body at body_top_w
    meet_l = (c - s.body_top_w / 2, s.cap_bot_y)
    meet_r = (c + s.body_top_w / 2, s.cap_bot_y)
    return (
        f"M {outer_l[0]:.1f} {outer_l[1]:.1f} "
        f"L {outer_r[0]:.1f} {outer_r[1]:.1f} "
        f"L {meet_r[0]:.1f} {meet_r[1]:.1f} "
        f"L {meet_l[0]:.1f} {meet_l[1]:.1f} "
        f"Z"
    )


def fin_path(s: BombShape, side: str) -> str:
    """Side fin (left or right). Sits at TOP/rear of bomb (atop body shoulder).

    Styles:
      angular_v:  pointed outer edge, like Mini-Nuke's stabilizers
      straight:   rectangular box sticking out
      swept:      angled outer edge (delta-wing-ish)
    """
    c = cx(s)
    # Inner edge of fin = body shoulder at fin_top_y..fin_bot_y interpolation
    # We need body width at those y values. Linear approximation between
    # body_top_w (at body_top_y) and body_bulge_w (at bulge_y).
    bulge_y = s.body_top_y + (s.body_bot_y - s.body_top_y) * s.body_bulge_y_frac

    def body_w_at(y: float) -> float:
        if y <= s.body_top_y:
            return s.body_top_w
        if y >= bulge_y:
            return s.body_bulge_w
        # Smooth interp (quadratic ease) — matches the body's curvature roughly
        t = (y - s.body_top_y) / max(1.0, (bulge_y - s.body_top_y))
        return s.body_top_w + (s.body_bulge_w - s.body_top_w) * (t * t * (3 - 2 * t))

    # Inner edge points (where fin meets body)
    in_top_w = body_w_at(s.fin_top_y)
    in_bot_w = body_w_at(s.fin_bot_y)
    sign = 1 if side == "right" else -1
    inner_top = (c + sign * in_top_w / 2, s.fin_top_y)
    inner_bot = (c + sign * in_bot_w / 2, s.fin_bot_y)

    # Outer edge — depends on style
    tip_y = s.fin_top_y + (s.fin_bot_y - s.fin_top_y) * s.fin_tip_y_frac
    outer_top = (c + sign * (in_top_w / 2 + s.fin_out_x * 0.5), s.fin_top_y)
    outer_bot = (c + sign * (in_bot_w / 2 + s.fin_out_x * 0.4), s.fin_bot_y)
    outer_tip = (c + sign * (s.body_bulge_w / 2 + s.fin_out_x), tip_y)

    if s.fin_style == "angular_v":
        return (
            f"M {inner_top[0]:.1f} {inner_top[1]:.1f} "
            f"L {outer_top[0]:.1f} {outer_top[1]:.1f} "
            f"L {outer_tip[0]:.1f} {outer_tip[1]:.1f} "
            f"L {outer_bot[0]:.1f} {outer_bot[1]:.1f} "
            f"L {inner_bot[0]:.1f} {inner_bot[1]:.1f} "
            f"Z"
        )
    elif s.fin_style == "straight":
        far_x = c + sign * (s.body_bulge_w / 2 + s.fin_out_x)
        return (
            f"M {inner_top[0]:.1f} {inner_top[1]:.1f} "
            f"L {far_x:.1f} {s.fin_top_y:.1f} "
            f"L {far_x:.1f} {s.fin_bot_y:.1f} "
            f"L {inner_bot[0]:.1f} {inner_bot[1]:.1f} "
            f"Z"
        )
    elif s.fin_style == "square_chamfered":
        # Rectangular fin (square top, square outer-top corner) with a
        # chamfered bottom-outer corner — small angle so the fin doesn't
        # read as pure rectangle but isn't a sharp V either.
        far_x = c + sign * (s.body_bulge_w / 2 + s.fin_out_x)
        # Chamfer length: how far the cut comes from the outer-bottom corner
        chamfer = getattr(s, "fin_chamfer_frac", 0.4) * (s.fin_bot_y - s.fin_top_y)
        chamfer = min(chamfer, s.fin_out_x * 0.85)
        return (
            f"M {inner_top[0]:.1f} {inner_top[1]:.1f} "
            f"L {far_x:.1f} {s.fin_top_y:.1f} "          # outer top
            f"L {far_x:.1f} {s.fin_bot_y - chamfer:.1f} "  # outer right, partway down
            f"L {far_x - sign * chamfer:.1f} {s.fin_bot_y:.1f} "  # chamfered diagonal
            f"L {inner_bot[0]:.1f} {inner_bot[1]:.1f} "
            f"Z"
        )
    elif s.fin_style == "trapezoid_chamfered":
        # Slightly tapered (top narrower than bottom) + chamfered bottom-
        # outer corner. Feels more "fin-y" than pure square without being
        # as pointy as angular_v.
        far_x_top = c + sign * (s.body_bulge_w / 2 + s.fin_out_x * 0.7)
        far_x_bot = c + sign * (s.body_bulge_w / 2 + s.fin_out_x)
        chamfer = getattr(s, "fin_chamfer_frac", 0.35) * (s.fin_bot_y - s.fin_top_y)
        chamfer = min(chamfer, s.fin_out_x * 0.7)
        return (
            f"M {inner_top[0]:.1f} {inner_top[1]:.1f} "
            f"L {far_x_top:.1f} {s.fin_top_y:.1f} "
            f"L {far_x_bot:.1f} {s.fin_bot_y - chamfer:.1f} "
            f"L {far_x_bot - sign * chamfer:.1f} {s.fin_bot_y:.1f} "
            f"L {inner_bot[0]:.1f} {inner_bot[1]:.1f} "
            f"Z"
        )
    elif s.fin_style == "swept":
        # Tip is at top-outer corner, then sweeps down + inward
        tip_x = c + sign * (s.body_bulge_w / 2 + s.fin_out_x)
        return (
            f"M {inner_top[0]:.1f} {inner_top[1]:.1f} "
            f"L {tip_x:.1f} {s.fin_top_y + 4:.1f} "
            f"L {(tip_x + inner_bot[0]) / 2:.1f} {s.fin_bot_y:.1f} "
            f"L {inner_bot[0]:.1f} {inner_bot[1]:.1f} "
            f"Z"
        )
    raise ValueError(f"unknown fin_style: {s.fin_style}")


def band_rect(s: BombShape) -> tuple:
    """Yellow band — inset from body edges at band_top_y / band_bot_y."""
    c = cx(s)
    bulge_y = s.body_top_y + (s.body_bot_y - s.body_top_y) * s.body_bulge_y_frac

    def body_w_at(y: float) -> float:
        if y <= s.body_top_y:
            return s.body_top_w
        if y >= bulge_y:
            return s.body_bulge_w
        t = (y - s.body_top_y) / max(1.0, (bulge_y - s.body_top_y))
        return s.body_top_w + (s.body_bulge_w - s.body_top_w) * (t * t * (3 - 2 * t))

    band_h = s.band_bot_y - s.band_top_y
    band_mid_y = (s.band_top_y + s.band_bot_y) / 2
    band_w = body_w_at(band_mid_y) - 2 * s.band_inset_x
    return (c - band_w / 2, s.band_top_y, band_w, band_h)


def render_svg(s: BombShape, label: str = "", show_ui_overlays: bool = True) -> str:
    """Render the full silhouette as an SVG string (no outer <svg> tag).

    Returns the inner content; caller wraps in <svg viewBox=...>.
    """
    bx, by, bw, bh = band_rect(s)
    c = cx(s)

    parts: List[str] = []

    # Reel-safe zones
    parts.append(f'<rect x="0" y="0" width="{s.width}" height="{int(s.height * 0.1)}" fill="{COL_AMBER}" opacity="0.06"/>')
    parts.append(f'<rect x="0" y="{int(s.height * 0.9)}" width="{s.width}" height="{int(s.height * 0.1)}" fill="{COL_AMBER}" opacity="0.06"/>')

    # Rear cap (drawn BEHIND body so the body's top edge covers the cap seam)
    parts.append(
        f'<path d="{cap_path(s)}" fill="{COL_BODY_LO}" stroke="{COL_OUTLINE}" stroke-width="2" stroke-linejoin="round"/>'
    )
    # Cap inner detail (firing-mechanism cylinder)
    cap_inner_x = c - s.cap_inner_w / 2
    parts.append(
        f'<rect x="{cap_inner_x:.1f}" y="{s.cap_top_y + 6}" width="{s.cap_inner_w}" height="{s.cap_bot_y - s.cap_top_y - 12}" '
        f'fill="{COL_OUTLINE}" stroke="{COL_OUTLINE}" stroke-width="1"/>'
    )

    # Fins (drawn BEHIND body so the body's outline overrides the fin where they meet)
    parts.append(
        f'<path d="{fin_path(s, "left")}" fill="{COL_FIN}" stroke="{COL_OUTLINE}" stroke-width="2" stroke-linejoin="round"/>'
    )
    parts.append(
        f'<path d="{fin_path(s, "right")}" fill="{COL_FIN}" stroke="{COL_OUTLINE}" stroke-width="2" stroke-linejoin="round"/>'
    )
    # Fin highlight (subtle inward stripe)
    for side, sign in [("left", -1), ("right", 1)]:
        tip_y = s.fin_top_y + (s.fin_bot_y - s.fin_top_y) * s.fin_tip_y_frac
        tx = c + sign * (s.body_bulge_w / 2 + s.fin_out_x * 0.6)
        parts.append(
            f'<line x1="{tx:.1f}" y1="{tip_y - 6:.1f}" x2="{tx:.1f}" y2="{tip_y + 6:.1f}" '
            f'stroke="{COL_FIN_HI}" stroke-width="2" opacity="0.5"/>'
        )

    # Body (drawn OVER fins/cap so it carves a clean silhouette).
    # If unified_silhouette, draw ONE path (cap-to-tip) instead of body+nose;
    # the "nose" red region becomes a clipped paint region later.
    body_d = unified_silhouette_path(s) if s.unified_silhouette else body_path(s)
    if s.unified_silhouette:
        # Define a clipPath so the red paint region can only fall inside
        # the unified silhouette outline.
        parts.append(
            f'<defs><clipPath id="body-clip"><path d="{body_d}"/></clipPath></defs>'
        )
    parts.append(
        f'<path d="{body_d}" fill="{COL_BODY}" stroke="{COL_OUTLINE}" stroke-width="2.5" stroke-linejoin="round"/>'
    )
    # Red paint region for the "nose," clipped to body silhouette.
    if s.unified_silhouette:
        parts.append(
            f'<rect x="0" y="{s.red_region_top_y}" width="{s.width}" height="{s.height - s.red_region_top_y}" '
            f'fill="{COL_NOSE}" clip-path="url(#body-clip)"/>'
        )
        # A subtle stroke right at the red-paint boundary line, inside the
        # silhouette — a designed "ring" marker (like Mini-Nuke's painted
        # divider). NOT a seam in the outline.
        c_local = cx(s)
        bulge_y_local = s.body_top_y + (s.body_bot_y - s.body_top_y) * s.body_bulge_y_frac
        # Width at the red boundary y, lerping from body_bulge_w → body_bot_w.
        if s.red_region_top_y <= bulge_y_local:
            ring_w = s.body_bulge_w
        else:
            t_red = min(1.0, (s.red_region_top_y - bulge_y_local) / max(1.0, (s.body_bot_y - bulge_y_local)))
            ring_w = s.body_bulge_w + (s.body_bot_w - s.body_bulge_w) * (t_red * t_red * (3 - 2 * t_red))
        parts.append(
            f'<line x1="{c_local - ring_w / 2 + 6:.1f}" y1="{s.red_region_top_y:.1f}" '
            f'x2="{c_local + ring_w / 2 - 6:.1f}" y2="{s.red_region_top_y:.1f}" '
            f'stroke="{COL_OUTLINE}" stroke-width="1.5" opacity="0.85" '
            f'clip-path="url(#body-clip)"/>'
        )
    # Cartoon highlight (curved stroke on upper-left of body)
    if s.show_cartoon_highlight:
        c_top_y = s.body_top_y + (s.body_bot_y - s.body_top_y) * 0.15
        parts.append(
            f'<path d="M {c - s.body_top_w / 2 + 16:.1f} {c_top_y + 20:.1f} '
            f'Q {c - s.body_bulge_w / 2 + 30:.1f} {c_top_y - 10:.1f} '
            f'{c - 20:.1f} {c_top_y - 20:.1f}" '
            f'fill="none" stroke="{COL_BODY_HI}" stroke-width="6" stroke-linecap="round" opacity="0.55"/>'
        )

    # Yellow hazard band
    parts.append(
        f'<rect x="{bx:.1f}" y="{by:.1f}" width="{bw:.1f}" height="{bh:.1f}" '
        f'fill="{COL_BAND}" stroke="{COL_OUTLINE}" stroke-width="2"/>'
    )
    # Diagonal hazard bars at band ends
    parts.append(
        f'<rect x="{bx + 4:.1f}" y="{by + 4:.1f}" width="6" height="{bh - 8:.1f}" '
        f'transform="skewX(-22)" fill="{COL_OUTLINE}"/>'
    )
    parts.append(
        f'<rect x="{bx + bw - 14:.1f}" y="{by + 4:.1f}" width="6" height="{bh - 8:.1f}" '
        f'transform="skewX(-22)" fill="{COL_OUTLINE}"/>'
    )
    # Cartouche text
    if s.show_cartouche:
        parts.append(
            f'<text x="{c:.1f}" y="{by + bh * 0.45:.1f}" text-anchor="middle" '
            f'font-family="monospace" font-weight="bold" font-size="11" letter-spacing="1.5" '
            f'fill="{COL_OUTLINE}">BOMBO-TEC</text>'
        )
        parts.append(
            f'<text x="{c:.1f}" y="{by + bh * 0.78:.1f}" text-anchor="middle" '
            f'font-family="monospace" font-size="6" letter-spacing="1.2" '
            f'fill="{COL_OUTLINE}">PEACE EDITION · 1992 · FOSS</text>'
        )

    # Rivets on body shoulders
    if s.show_rivets:
        rivet_color = COL_OUTLINE
        rivet_y_top = s.body_top_y + 24
        rivet_y_bot = s.body_bot_y - 24
        for ry in (rivet_y_top, rivet_y_bot):
            for rx in (c - s.body_bulge_w / 2 + 14, c + s.body_bulge_w / 2 - 14):
                parts.append(f'<circle cx="{rx:.1f}" cy="{ry:.1f}" r="2.4" fill="{rivet_color}"/>')

    # Portholes (one each side, just below band)
    if s.show_portholes:
        porthole_y = by + bh + 24
        for sign in (-1, 1):
            px = c + sign * (s.body_bulge_w / 2 - 30)
            parts.append(f'<circle cx="{px:.1f}" cy="{porthole_y:.1f}" r="7" fill="{COL_BODY_LO}" stroke="{COL_OUTLINE}" stroke-width="1.5"/>')
            parts.append(f'<circle cx="{px:.1f}" cy="{porthole_y:.1f}" r="3" fill="{COL_BODY_HI}"/>')

    # Smiley FOSS wink, low on body
    if s.show_smiley:
        smile_y = s.body_bot_y - 30
        parts.append(
            f'<text x="{c:.1f}" y="{smile_y:.1f}" text-anchor="middle" '
            f'font-family="monospace" font-size="13" fill="{COL_TRIM}" opacity="0.55">:-)</text>'
        )

    # UI overlay rectangles (header, macros, rack) — to test inhabitability
    if show_ui_overlays:
        bulge_y = s.body_top_y + (s.body_bot_y - s.body_top_y) * s.body_bulge_y_frac

        def body_w_at(y):
            if y <= s.body_top_y:
                return s.body_top_w
            if y >= bulge_y:
                return s.body_bulge_w
            t = (y - s.body_top_y) / max(1.0, (bulge_y - s.body_top_y))
            return s.body_top_w + (s.body_bulge_w - s.body_top_w) * (t * t * (3 - 2 * t))

        # Header strip just below band
        header_y = by + bh + 14
        header_w = body_w_at(header_y) - 28
        parts.append(
            f'<rect x="{c - header_w / 2:.1f}" y="{header_y:.1f}" width="{header_w:.1f}" height="22" '
            f'rx="3" ry="3" fill="none" stroke="{COL_AMBER}" stroke-width="1.2" stroke-dasharray="3,2"/>'
        )
        parts.append(
            f'<text x="{c:.1f}" y="{header_y + 15:.1f}" text-anchor="middle" '
            f'font-family="monospace" font-size="8" fill="{COL_AMBER}">HEADER · BPM · THEME</text>'
        )
        # Macro row in mid-body
        macro_y = header_y + 38
        macro_w = body_w_at(macro_y) - 28
        parts.append(
            f'<rect x="{c - macro_w / 2:.1f}" y="{macro_y:.1f}" width="{macro_w:.1f}" height="32" '
            f'rx="4" ry="4" fill="none" stroke="{COL_TRIM}" stroke-width="1" stroke-dasharray="3,2"/>'
        )
        parts.append(
            f'<text x="{c:.1f}" y="{macro_y + 20:.1f}" text-anchor="middle" '
            f'font-family="monospace" font-size="9" fill="{COL_TRIM}">7× MACRO</text>'
        )
        # Rack — full available height below macros, narrowing if body curves
        rack_y = macro_y + 42
        rack_h = (s.body_bot_y - 60) - rack_y
        rack_w_top = body_w_at(rack_y) - 28
        rack_w_bot = body_w_at(rack_y + rack_h) - 28
        rack_w = min(rack_w_top, rack_w_bot)
        parts.append(
            f'<rect x="{c - rack_w / 2:.1f}" y="{rack_y:.1f}" width="{rack_w:.1f}" height="{rack_h:.1f}" '
            f'rx="6" ry="6" fill="none" stroke="{COL_TRIM}" stroke-width="1" stroke-dasharray="3,2"/>'
        )
        parts.append(
            f'<text x="{c:.1f}" y="{rack_y + 22:.1f}" text-anchor="middle" '
            f'font-family="monospace" font-size="9" fill="{COL_TRIM}">7 FX COLUMNS</text>'
        )

    # Red nose — only drawn as a separate path in NON-unified mode.
    if not s.unified_silhouette:
        parts.append(
            f'<path d="{nose_path(s)}" fill="{COL_NOSE}" stroke="{COL_OUTLINE}" stroke-width="2.5" stroke-linejoin="round"/>'
        )
        # Nose highlight
        nose_hi_y = s.nose_top_y + (s.nose_bot_y - s.nose_top_y) * 0.18
        parts.append(
            f'<path d="M {c - s.nose_w / 2 + 24:.1f} {nose_hi_y + 6:.1f} '
            f'Q {c - s.nose_w / 4:.1f} {nose_hi_y - 6:.1f} '
            f'{c - 8:.1f} {nose_hi_y - 10:.1f}" '
            f'fill="none" stroke="{COL_NOSE_HI}" stroke-width="4" stroke-linecap="round" opacity="0.7"/>'
        )

    # Archie fuze in nose: central detonator + 4 antennas
    nose_mid_y = s.nose_top_y + (s.nose_bot_y - s.nose_top_y) * 0.55
    parts.append(
        f'<circle cx="{c:.1f}" cy="{nose_mid_y:.1f}" r="22" fill="none" '
        f'stroke="{COL_TRIM}" stroke-width="0.8" opacity="0.4"/>'
    )
    parts.append(
        f'<circle cx="{c:.1f}" cy="{nose_mid_y:.1f}" r="16" fill="none" '
        f'stroke="{COL_TRIM}" stroke-width="0.6" opacity="0.6"/>'
    )
    parts.append(
        f'<circle cx="{c:.1f}" cy="{nose_mid_y:.1f}" r="11" fill="{COL_AMBER}"/>'
    )
    parts.append(
        f'<circle cx="{c:.1f}" cy="{nose_mid_y:.1f}" r="6" fill="{COL_OUTLINE}"/>'
    )
    # 4 antennas (peak meter)
    for angle_deg, label in [(225, "SUB"), (315, "LOW"), (135, "MID"), (45, "HI")]:
        a = math.radians(angle_deg)
        ex = c + math.cos(a) * 36
        ey = nose_mid_y + math.sin(a) * 36
        parts.append(
            f'<line x1="{c:.1f}" y1="{nose_mid_y:.1f}" x2="{ex:.1f}" y2="{ey:.1f}" '
            f'stroke="{COL_TRIM}" stroke-width="1.8"/>'
        )
        parts.append(f'<circle cx="{ex:.1f}" cy="{ey:.1f}" r="3.5" fill="{COL_AMBER}"/>')
        # Label offset further outward
        lx = c + math.cos(a) * 46
        ly = nose_mid_y + math.sin(a) * 46 + 3
        parts.append(
            f'<text x="{lx:.1f}" y="{ly:.1f}" text-anchor="middle" '
            f'font-family="monospace" font-size="7" fill="{COL_TRIM}">{label}</text>'
        )

    # Variant label at bottom
    if label:
        parts.append(
            f'<text x="{c:.1f}" y="{s.height - 8:.1f}" text-anchor="middle" '
            f'font-family="monospace" font-size="9" letter-spacing="2" '
            f'fill="{COL_TRIM}" opacity="0.55">{label}</text>'
        )

    return "\n".join(parts)


def variant(name: str, **overrides) -> tuple:
    """Build a BombShape with overrides and return (name, BombShape, svg_inner)."""
    s = BombShape(**overrides)
    return name, s, render_svg(s, label=name)


def emit_html(variants: list, title: str, subtitle: str, out_path: str):
    cards = []
    for name, _, inner in variants:
        cards.append(f"""
<div class="option" data-choice="{name.lower()}" onclick="toggleSelect(this)" style="flex:1 1 280px;">
  <div class="letter">{name[0]}</div>
  <div class="content" style="width:100%;">
    <h3>{name}</h3>
    <svg viewBox="0 0 360 640" xmlns="http://www.w3.org/2000/svg" preserveAspectRatio="xMidYMid meet" style="width:100%;max-width:280px;display:block;margin:0 auto;">
      {inner}
    </svg>
  </div>
</div>
""")
    html = f"""<h2>{title}</h2>
<p class="subtitle">{subtitle}</p>
<div class="options" style="align-items: stretch;">
{''.join(cards)}
</div>
"""
    with open(out_path, "w") as f:
        f.write(html)


if __name__ == "__main__":
    import sys
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    which = sys.argv[2] if len(sys.argv) > 2 else "r1"

    # BALLOON body params (locked from Round 1) — reused across Round 2 variants.
    balloon_body = dict(
        body_top_y=92,
        body_bot_y=460,
        body_top_w=100,
        body_bot_w=150,
        body_bulge_w=290,
        body_bulge_y_frac=0.52,
        cap_top_y=22,
        cap_bot_y=94,
        cap_w=92,
        cap_inner_w=64,
        band_top_y=106,
        band_bot_y=150,
    )
    # New gentle-extension nose params — same across Round 2.
    gentle_nose = dict(
        nose_top_y=460,
        nose_bot_y=586,
        nose_w=150,        # = body_bot_w, no mushroom flare
        nose_curve=1.10,
    )

    # ── ROUND 1 — broad parameter space exploration ────────────────────
    round1 = [
        variant(
            "STUBBY",
            # Fat, short, exaggerated bulge — chunkiest cartoon Mini-Nuke
            body_top_y=78,
            body_bot_y=470,
            body_top_w=120,
            body_bot_w=170,
            body_bulge_w=270,
            body_bulge_y_frac=0.55,
            nose_top_y=470,
            nose_bot_y=584,
            nose_w=230,
            nose_curve=1.25,
            cap_top_y=20,
            cap_bot_y=80,
            cap_w=110,
            cap_inner_w=78,
            fin_top_y=30,
            fin_bot_y=130,
            fin_tip_y_frac=0.55,
            fin_out_x=42,
            fin_style="angular_v",
            band_top_y=92,
            band_bot_y=140,
        ),
        variant(
            "BALLOON",
            # Even MORE bulbous, near-spherical mid, smaller cap/fins
            body_top_y=92,
            body_bot_y=460,
            body_top_w=100,
            body_bot_w=150,
            body_bulge_w=290,
            body_bulge_y_frac=0.52,
            nose_top_y=460,
            nose_bot_y=590,
            nose_w=240,
            nose_curve=1.35,
            cap_top_y=22,
            cap_bot_y=94,
            cap_w=92,
            cap_inner_w=64,
            fin_top_y=34,
            fin_bot_y=128,
            fin_tip_y_frac=0.5,
            fin_out_x=46,
            fin_style="angular_v",
            band_top_y=106,
            band_bot_y=150,
        ),
        variant(
            "STRETCHED",
            # Taller, less bulge, more "torpedo" feel — UI-friendliest
            body_top_y=70,
            body_bot_y=490,
            body_top_w=130,
            body_bot_w=180,
            body_bulge_w=230,
            body_bulge_y_frac=0.5,
            nose_top_y=488,
            nose_bot_y=584,
            nose_w=200,
            nose_curve=1.05,
            cap_top_y=18,
            cap_bot_y=72,
            cap_w=118,
            cap_inner_w=82,
            fin_top_y=30,
            fin_bot_y=128,
            fin_tip_y_frac=0.5,
            fin_out_x=38,
            fin_style="angular_v",
            band_top_y=84,
            band_bot_y=130,
        ),
    ]
    if which == "r1":
        emit_html(
            round1,
            title="Round 1 — 3 silhouette directions",
            subtitle="Generated by tools/bombshape_gen.py — parametric. Each variant changes body bulge, body aspect, nose curvature, fin proportions, and cap size.",
            out_path=f"{out_dir}/bomb-r1.html",
        )
        print(f"wrote round 1 to {out_dir}/bomb-r1.html")

    # ── ROUND 2 — refine BALLOON: new gentle-extension nose + fin style A/B/C
    round2 = [
        # All three share the BALLOON body + new gentle nose. They differ
        # ONLY in fin_style + fin_chamfer_frac.
        variant(
            "R2A-SQUARE",
            **balloon_body, **gentle_nose,
            fin_top_y=34,
            fin_bot_y=128,
            fin_tip_y_frac=0.5,
            fin_out_x=46,
            fin_style="square_chamfered",
            fin_chamfer_frac=0.22,   # subtle chamfer — closest to "pure rectangle"
        ),
        variant(
            "R2B-CHAMFERED",
            **balloon_body, **gentle_nose,
            fin_top_y=34,
            fin_bot_y=128,
            fin_tip_y_frac=0.5,
            fin_out_x=46,
            fin_style="square_chamfered",
            fin_chamfer_frac=0.42,   # noticeable chamfer — matches user's sketch
        ),
        variant(
            "R2C-TRAPEZOID",
            **balloon_body, **gentle_nose,
            fin_top_y=34,
            fin_bot_y=128,
            fin_tip_y_frac=0.5,
            fin_out_x=46,
            fin_style="trapezoid_chamfered",
            fin_chamfer_frac=0.35,   # tapered top + chamfered bottom
        ),
    ]
    if which == "r2":
        emit_html(
            round2,
            title="Round 2 — BALLOON locked. Refine fins + new gentle-extension nose",
            subtitle="Body unchanged from BALLOON. NEW nose: smooth extension of body curvature (no mushroom flare) with gently-rounded tip. Fin variants differ in chamfer + taper. Pick a fin style or push another direction.",
            out_path=f"{out_dir}/bomb-r2.html",
        )
        print(f"wrote round 2 to {out_dir}/bomb-r2.html")

    # ── ROUND 3 — UNIFIED SILHOUETTE + smaller fins ─────────────────────
    # User feedback: outline should be one continuous shape (no seam at
    # nose join), fins narrower (don't exceed body so much).
    # Common base: BALLOON body + unified silhouette + smaller fins.
    r3_base = dict(
        **balloon_body,
        nose_top_y=460,        # legacy field — not used in unified mode
        nose_bot_y=596,        # tip y
        nose_w=150,
        nose_curve=1.10,
        unified_silhouette=True,
        red_region_top_y=466,  # where the red paint starts inside the body
        fin_top_y=34,
        fin_bot_y=128,
        fin_tip_y_frac=0.5,
        fin_out_x=22,          # ← REDUCED (was 46) — fins close to body
        fin_style="square_chamfered",
        fin_chamfer_frac=0.42,
    )
    round3 = [
        variant(
            "R3A-POINTY",
            **{**r3_base,
               "unified_tip_sharpness": 0.78,   # MORE pointed — follows body curve to a near-point
               "nose_bot_y": 600},
        ),
        variant(
            "R3B-TEARDROP",
            **{**r3_base,
               "unified_tip_sharpness": 0.45,   # blunt-yet-rounded teardrop
               "nose_bot_y": 590},
        ),
        variant(
            "R3C-BLUNT",
            **{**r3_base,
               "unified_tip_sharpness": 0.15,   # very rounded bottom — obround-ish
               "nose_bot_y": 576,
               "red_region_top_y": 472},
        ),
    ]
    if which == "r3":
        emit_html(
            round3,
            title="Round 3 — unified silhouette, smaller fins",
            subtitle="Outline is now ONE continuous egg-shape — no seam between body and nose. Red is a paint region (with a designed ring line) inside the same silhouette. Fins reduced to fin_out_x=22 so they barely exceed the body. Three tip variants: pointed, teardrop, very blunt.",
            out_path=f"{out_dir}/bomb-r3.html",
        )
        print(f"wrote round 3 to {out_dir}/bomb-r3.html")

    # ── ROUND 4 — final convergence: R3A-POINTY locked, vary red split ─
    # User picked R3A in Round 3. Highest-impact remaining knob is the
    # red-paint region's top y — controls how much of the bomb reads red.
    # All three variants identical except for red_region_top_y.
    r4_base = dict(
        **balloon_body,
        nose_top_y=460,
        nose_bot_y=600,
        nose_w=150,
        nose_curve=1.10,
        unified_silhouette=True,
        unified_tip_sharpness=0.78,   # R3A's pointed tip — LOCKED
        fin_top_y=34,
        fin_bot_y=128,
        fin_tip_y_frac=0.5,
        fin_out_x=22,
        fin_style="square_chamfered",
        fin_chamfer_frac=0.42,
    )
    round4 = [
        variant(
            "R4A-SUBTLE",
            **{**r4_base,
               "red_region_top_y": 510},   # red ≈ lower 15% — minimalist red tip
        ),
        variant(
            "R4B-CLASSIC",
            **{**r4_base,
               "red_region_top_y": 462},   # red ≈ lower 25% — Mini-Nuke proportions
        ),
        variant(
            "R4C-MAJOR",
            **{**r4_base,
               "red_region_top_y": 410},   # red ≈ lower 35% — dramatic split, max visual punch
        ),
    ]
    if which == "r4":
        emit_html(
            round4,
            title="Round 4 — final: R3A locked, pick the red split",
            subtitle="Body, tip, fins all unchanged from R3A-POINTY. Only the red-paint region's top edge moves. SUBTLE = red just dips below the tip (most green). CLASSIC = Mini-Nuke proportions (~25% red). MAJOR = dramatic split, biggest IG-thumbnail color-block punch.",
            out_path=f"{out_dir}/bomb-r4.html",
        )
        print(f"wrote round 4 to {out_dir}/bomb-r4.html")
