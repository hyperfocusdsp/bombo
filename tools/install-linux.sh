#!/usr/bin/env bash
# Install Bombo VST3 to ~/.vst3 (Linux). Always rm -rf the target before
# cp -r — nested bundles cause hosts to silently skip scanning.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VST3_SRC="${REPO_ROOT}/build/Bombo_artefacts/Release/VST3/Bombo.vst3"
VST3_DEST="${HOME}/.vst3/Bombo.vst3"

if [[ ! -d "${VST3_SRC}" ]]; then
    echo "ERROR: ${VST3_SRC} not found. Run 'cmake --build build' first." >&2
    exit 1
fi

mkdir -p "${HOME}/.vst3"
rm -rf "${VST3_DEST}"
cp -r "${VST3_SRC}" "${VST3_DEST}"
echo "Installed: ${VST3_DEST}"
