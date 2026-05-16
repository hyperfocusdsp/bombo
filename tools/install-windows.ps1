# Install Bombo VST3 to the system VST3 folder (Windows).
# Run from PowerShell: .\tools\install-windows.ps1
$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
$Vst3Src = Join-Path $RepoRoot "build\Bombo_artefacts\Release\VST3\Bombo.vst3"
$Vst3Dest = "C:\Program Files\Common Files\VST3\Bombo.vst3"

if (-not (Test-Path $Vst3Src)) {
    Write-Error "Source not found: $Vst3Src. Run 'cmake --build build' first."
    exit 1
}

if (Test-Path $Vst3Dest) {
    Remove-Item -Recurse -Force $Vst3Dest
}
Copy-Item -Recurse $Vst3Src $Vst3Dest
Write-Host "Installed: $Vst3Dest"
