#!/usr/bin/env bash
# capture_screenshots.sh — headless multi-theme screenshot capture for Bombo
# Requires: grim, imagemagick (convert/montage), python3
# Usage: ./tools/capture_screenshots.sh
set -euo pipefail

BOMBO="$(dirname "$(dirname "$(realpath "$0")")")/build/Bombo_artefacts/Release/Standalone/Bombo"
OUTDIR=~/Pictures/bombo_screenshots
mkdir -p "$OUTDIR"

THEMES=(vault bandw nightrun matrix cyber plasma fallout)

echo "=== Bombo screenshot capture ==="
echo "Binary : $BOMBO"
echo "Output : $OUTDIR"
echo ""

# Prevent focus steal from interrupting other windows. Still appears on
# current workspace (required for grim to capture it on the active output).
hyprctl keyword windowrulev2 "noinitialfocus,class:^(Bombo)$" >/dev/null 2>&1

for THEME in "${THEMES[@]}"; do
    echo "→ $THEME"

    BOMBO_SCREENSHOT=1 BOMBO_FORCE_THEME="$THEME" "$BOMBO" &
    BGPID=$!

    # Poll for window (up to 15s)
    GEOM=""
    for i in $(seq 1 30); do
        sleep 0.5
        GEOM=$(hyprctl clients -j 2>/dev/null | python3 -c "
import json, sys
for c in json.load(sys.stdin):
    if c.get('class','') == 'Bombo':
        x,y=c['at']; w,h=c['size']
        print(f'{x},{y} {w}x{h}'); sys.exit(0)
sys.exit(1)" 2>/dev/null) && break || true
    done

    if [ -z "$GEOM" ]; then
        echo "  SKIP: window did not appear"
        kill "$BGPID" 2>/dev/null || true; wait "$BGPID" 2>/dev/null || true
        continue
    fi

    echo "  window @ $GEOM"

    # Wait for: audio init + first loop cycle + scope to fill with waveform
    sleep 4

    grim -g "$GEOM" "$OUTDIR/${THEME}_raw.png"
    echo "  captured → ${THEME}_raw.png"

    kill "$BGPID" 2>/dev/null || true
    wait "$BGPID" 2>/dev/null || true
    sleep 0.3
done

hyprctl keyword windowrulev2 "unset,class:^(Bombo)$" >/dev/null 2>&1

echo ""
echo "=== Post-processing ==="

# Check for a JUCE title-bar offset. JUCE's StandaloneFilterWindow on Wayland
# renders its own thin title bar at the top of the content area. Probe the
# first raw image: scan downward from y=0 until we hit a non-background row.
FIRST_RAW="$OUTDIR/vault_raw.png"
TITLEBAR_H=0
if [ -f "$FIRST_RAW" ]; then
    TITLEBAR_H=$(python3 - "$FIRST_RAW" << 'EOF'
import sys
from PIL import Image
img = Image.open(sys.argv[1]).convert("RGB")
bg = (0x14, 0x16, 0x1B)   # BOMBO_SOLID_BG fill colour
tol = 12
w, h = img.size
for y in range(min(60, h)):
    row_pixels = [img.getpixel((x, y)) for x in range(w)]
    matches = sum(1 for p in row_pixels if all(abs(p[i]-bg[i]) <= tol for i in range(3)))
    # If 90 % of the row matches the background colour, it's the title area
    if matches / w >= 0.90:
        print(y + 1)
        break
else:
    print(0)
EOF
    )
    echo "  Detected title-bar height: ${TITLEBAR_H}px"
fi

cd "$OUTDIR"

for THEME in "${THEMES[@]}"; do
    RAW="${THEME}_raw.png"
    OUT="${THEME}.png"
    if [ ! -f "$RAW" ]; then continue; fi

    if [ "$TITLEBAR_H" -gt 0 ]; then
        # Chop the JUCE title bar from the top
        convert "$RAW" -chop "0x${TITLEBAR_H}" "$OUT"
    else
        cp "$RAW" "$OUT"
    fi
    echo "  processed: $OUT"
done

echo ""
echo "=== Compositing lineup ==="

# All 7 themes, equal-height resize to 800px, separated by a 4px dark gap
LINEUP_PARTS=()
for THEME in "${THEMES[@]}"; do
    if [ -f "${THEME}.png" ]; then
        LINEUP_PARTS+=("${THEME}.png")
    fi
done

if [ "${#LINEUP_PARTS[@]}" -gt 0 ]; then
    # Resize each to 800px tall, then stitch horizontally with a 4px gap
    RESIZED=()
    for img in "${LINEUP_PARTS[@]}"; do
        STEM="${img%.png}"
        convert "$img" -resize x800 "${STEM}_r800.png"
        RESIZED+=("${STEM}_r800.png")
    done

    # montage with no background tile (just the images, no padding titles)
    montage "${RESIZED[@]}" \
        -tile "${#RESIZED[@]}x1" \
        -geometry +4+0 \
        -background '#14161B' \
        bombo_themes_lineup.png

    echo "  lineup: bombo_themes_lineup.png"

    # Convert to webp for the site
    convert bombo_themes_lineup.png \
        -quality 90 \
        bombo_themes_lineup.webp
    echo "  webp:   bombo_themes_lineup.webp"
else
    echo "  No theme images found — skipping lineup"
fi

echo ""
echo "=== Done ==="
echo "  Individual shots: $OUTDIR/<theme>.png"
echo "  7-theme lineup:   $OUTDIR/bombo_themes_lineup.png"
echo "  Site asset:       $OUTDIR/bombo_themes_lineup.webp"
