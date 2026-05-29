#!/usr/bin/env bash
# Launch the Bombo JUCE Standalone. Kills any prior instance first so
# rofi / .desktop relaunches don't pile up duplicate audio devices on
# PipeWire/JACK. Set BOMBO_NO_KILL=1 to disable the pkill (useful when
# running multiple instances on purpose).
set -euo pipefail
# Resolve through symlinks so launching via ~/.local/bin/bombo-launch
# still locates the repo correctly.
SCRIPT_PATH="$(readlink -f "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "${SCRIPT_PATH}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BIN="${REPO_ROOT}/build/Bombo_artefacts/Release/Standalone/Bombo"
if [[ ! -x "${BIN}" ]]; then
    echo "Bombo standalone not built. Building now..." >&2
    cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DCMAKE_BUILD_TYPE=Release >&2
    cmake --build "${REPO_ROOT}/build" --target Bombo_Standalone -j"$(nproc)" >&2
fi

# pkill self-match: use [B] bracket trick so the running zsh -c doesn't
# match its own argv. See memory: feedback_pkill_self_match_bracket_trick.
if [[ "${BOMBO_NO_KILL:-0}" != "1" ]]; then
    pkill -f '[B]ombo_artefacts/Release/Standalone/Bombo' || true
    # Give the audio backend a moment to release the device.
    sleep 0.1
fi

# Default the rofi/.desktop launch to the FALLOUT theme so it opens in the
# genai design instead of the saved default. Overridable: set BOMBO_FORCE_THEME
# yourself (or empty it: BOMBO_FORCE_THEME= bombo-launch) to use the persisted
# theme / pick another. Clicking a theme tile in-app still persists normally.
export BOMBO_FORCE_THEME="${BOMBO_FORCE_THEME:-fallout}"

exec "${BIN}" "$@"
