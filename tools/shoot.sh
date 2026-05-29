#!/usr/bin/env bash
# Launch the Bombo standalone for visual review — WITHOUT stealing focus.
#   tools/shoot.sh [THEME]
#
# Uses Hyprland windowrules to send the window to a silent workspace with no
# initial focus, so it never yanks you off what you're doing. Capture is left
# to YOU (grim a region, or just alt-tab over when you're ready) — this script
# deliberately does NOT switch workspaces or focus windows.
set -euo pipefail
THEME="${1:-fallout}"
BIN="$HOME/repos/bombo/build/Bombo_artefacts/Release/Standalone/Bombo"

# Send Bombo to workspace 9, silent, no focus grab.
hyprctl keyword windowrulev2 "workspace 9 silent,class:(Bombo)" >/dev/null 2>&1 || true
hyprctl keyword windowrulev2 "noinitialfocus,class:(Bombo)"     >/dev/null 2>&1 || true

pkill -x Bombo 2>/dev/null || true
sleep 1.0
BOMBO_SOLID_BG=1 BOMBO_FORCE_THEME="$THEME" setsid "$BIN" >/tmp/bombo_${THEME}.log 2>&1 </dev/null &
disown
echo "launched theme=$THEME on workspace 9 (silent). It did NOT take focus."
echo "To view: switch to ws 9 yourself, or screenshot when convenient."
