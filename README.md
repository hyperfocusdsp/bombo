# Bombo

> Half kick instrument, half 1992 BBS. One plugin.

A FOSS kick synth + rumble FX combo plugin for techno / house / DnB / trap / ambient producers — with a planned HYPERFOCUS BBS mode that lets you swipe through procedurally-generated kicks like Tinder.

Built by [Hyperfocus DSP](https://hyperfocusdsp.com).

## Status

KVRDC 2026 entry, target submission **July 5, 2026**. Active rewrite from nih-plug/Rust to JUCE/C++17 (Rust archive at `~/repos/bombo-rust-archive/`). See `~/.claude/plans/you-very-succeasfully-rewrote-linear-engelbart.md` for the rewrite plan.

## Stack

- [JUCE 8](https://juce.com/) (C++17) — VST3 + AU + Standalone (CLAP planned)
- CMake build, FetchContent for JUCE
- GPL-3.0-or-later

## Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Install
./tools/install-linux.sh        # Linux
# ./tools/install-macos.sh      # macOS (ad-hoc signed, no Apple Dev)
# tools\install-windows.ps1     # Windows

# Standalone
./build/Bombo_artefacts/Release/Standalone/Bombo
```

## Test

```bash
cmake --build build --target Bombo_Tests
ctest --test-dir build --output-on-failure
```

See `TESTING.md` for the full sanity-check checklist + how to A/B against the Rust archive.
