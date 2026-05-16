#!/usr/bin/env bash
# Install Bombo VST3 + AU to ~/Library/Audio/Plug-Ins/. Ad-hoc signed
# (no Apple Dev) — Gatekeeper will require user override on first load.
# Per Bombo Plan B+ Mac strategy (memory: feedback_bombo_plan_b_plus_mac_strategy).
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VST3_SRC="${REPO_ROOT}/build/Bombo_artefacts/Release/VST3/Bombo.vst3"
AU_SRC="${REPO_ROOT}/build/Bombo_artefacts/Release/AU/Bombo.component"

VST3_DEST="${HOME}/Library/Audio/Plug-Ins/VST3/Bombo.vst3"
AU_DEST="${HOME}/Library/Audio/Plug-Ins/Components/Bombo.component"

mkdir -p "${HOME}/Library/Audio/Plug-Ins/VST3"
mkdir -p "${HOME}/Library/Audio/Plug-Ins/Components"

if [[ -d "${VST3_SRC}" ]]; then
    rm -rf "${VST3_DEST}"
    cp -R "${VST3_SRC}" "${VST3_DEST}"
    codesign --force --deep --sign - "${VST3_DEST}" || true
    echo "Installed: ${VST3_DEST}"
fi

if [[ -d "${AU_SRC}" ]]; then
    rm -rf "${AU_DEST}"
    cp -R "${AU_SRC}" "${AU_DEST}"
    codesign --force --deep --sign - "${AU_DEST}" || true
    # auval needs the AU to validate before Logic will load it.
    echo "Validating AU (this may take ~10s)..."
    auval -v aumu Bomb Hfds || echo "(auval reported issues — Logic may still load it)"
    echo "Installed: ${AU_DEST}"
fi
