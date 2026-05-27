# Bombo

> Half kick instrument, half 1992 BBS. One plugin.

Bombo is a free, open-source **kick-drum synthesizer and rumble-FX plugin** for
techno, house, drum & bass, trap, and ambient. It pairs a two-voice kick engine
with a reorderable effects chain and reverse-bass ducking, wrapped in a bomb-shaped
chassis that hides a working 1992-style BBS terminal.

Built by [Hyperfocus DSP](https://hyperfocusdsp.com). GPL-3.0-or-later.

## Features

### Sound engine
- **Two-voice kick** — Voice A is the sub/body (sine core with a tunable pitch
  envelope: start, end, decay, curve, plus a sub high-pass); Voice B is the
  mid/punch layer and can be a **synth voice or a loaded sample**. A balance
  control crossfades the two.
- **Transient + noise** — adjustable click (amount + center) and a noise layer
  with a color control, for snap and air on top of the body.
- **Drive** — per-voice saturation (amount / mode / bias) and a separate
  FX-bus drive stage with its own mode and mix.

### Effects chain (reorderable)
- **Filter** — high-pass and low-pass with resonance, plus `color` and `teeth`
  voicing controls.
- **Delay** — tempo-synced with sub-sample-accurate timing, feedback, `morph`,
  `smear`, and mix.
- **Reverb** — multiple algorithms with size, decay, damping, diffusion,
  pre-delay, and mix.
- The Drive → Filter → Delay → Reverb order is **rearrangeable per preset**.

### Ducking
- **DUCK routing** — reverse-bass sidechain ducking with `Off → A → B → AB`
  routing, so you can duck the sub, the body, or both. A separate always-on
  wet-bus tail duck keeps reverb/delay tails out of the way of each hit.

### Workflow
- **Macros** — six one-knob shapers on the bomb's nose (PITCH, DECAY, PUNCH,
  WEIGHT, MOOD, SPACE) plus the OUT hero knob.
- **Scope** — a live waveform strip across the top of the UI.
- **Preset bank** — 10 curated factory presets plus full user preset
  save / rename / delete.
- **Six themes** — VAULT (default), BANDW, CYBER, MATRIX, NIGHTRUN, PLASMA.
- **Offline bounce** — render the current kick straight to WAV or AIFF.
- **Optional key-tracking** so the kick follows incoming MIDI note pitch.

### HYPERFOCUS BBS
Tap into the nose to dial up a 1992-style bulletin-board terminal baked into the
plugin — complete with a BoomFeed and a playable Space-Impact-style mini-game.
A flourish, not a dependency: it touches none of the audio path.

## Formats & platforms

| | |
|---|---|
| **Formats** | VST3, AU, CLAP, Standalone |
| **Linux** | x86_64 |
| **macOS** | universal (Apple Silicon + Intel) |
| **Windows** | x64 |

## Build

Requires CMake ≥ 3.22 and a C++17 compiler. JUCE 8 and the CLAP extensions are
pulled automatically via CMake `FetchContent`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run the standalone
./build/Bombo_artefacts/Release/Standalone/Bombo
```

LTO is on by default for release builds; pass `-DBOMBO_LTO=OFF` on
memory-limited machines or for faster CI builds.

## Install

```bash
./tools/install-linux.sh        # Linux
./tools/install-macos.sh        # macOS (ad-hoc signed; no Apple Developer ID needed)
tools\install-windows.ps1       # Windows (PowerShell)
```

User presets live under the per-user config directory:

| Platform | Location |
|---|---|
| Linux | `~/.config/Bombo/Presets` |
| macOS | `~/Library/Bombo/Presets` |
| Windows | `%APPDATA%\Bombo\Presets` |

## Test

```bash
cmake --build build --target Bombo_Tests
ctest --test-dir build --output-on-failure
```

See [`TESTING.md`](TESTING.md) for the manual sanity-check checklist.

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE).
