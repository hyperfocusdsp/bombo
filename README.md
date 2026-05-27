# Bombo

> Half kick instrument, half 1992 BBS. One plugin.

Bombo is a free, open-source **kick-drum synthesizer and rumble-FX plugin** for
techno, house, drum & bass, trap, and ambient. A two-voice kick engine (sub +
synth/sample body) feeds a reorderable drive → filter → delay → reverb chain
with reverse-bass sidechain ducking, six one-knob macros, a live scope, and a
preset bank — all wrapped in a bomb-shaped chassis that hides a working
1992-style BBS terminal, complete with a playable mini-game.

![Bombo](docs/bombo.webp?v=1.0.0)

<p align="center">
  <a href="https://hyperfocusdsp.com"><img src="https://img.shields.io/badge/Hyperfocus_DSP-hyperfocusdsp.com-f59e0b?style=for-the-badge" alt="Hyperfocus DSP" /></a>
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-GPL--3.0--or--later-2d3748?style=for-the-badge" alt="License: GPL-3.0-or-later" /></a>
</p>

**Formats:** VST3, AU, CLAP, Standalone
**Platforms:** Linux (x86_64), macOS (Apple Silicon + Intel), Windows (x64)
**License:** GPL-3.0-or-later

## Quick start

Download the latest build for your platform from the
[Releases](https://github.com/hyperfocusdsp/bombo/releases/latest) page,
extract, and run the installer:

```bash
./tools/install-linux.sh        # Linux
./tools/install-macos.sh        # macOS (ad-hoc signed; no Apple Developer ID)
tools\install-windows.ps1       # Windows (PowerShell)
```

Rescan plugins in your DAW. Bombo appears as **Hyperfocus DSP / Bombo**.

## Features

### Signal chain

```
Voice A (sub)  +  Voice B (synth or sample, mid/punch)
  → per-voice drive
  → DUCK (reverse-bass: Off / A / B / AB)
  → FX bus: Drive → Filter → Delay → Reverb   (reorderable per preset)
  → limiter → OUT
```

### Sound engine
- **Two-voice kick** — Voice A is the sub/body: a sine core with a tunable
  pitch envelope (start, end, decay, curve) and a sub high-pass. Voice B is the
  mid/punch layer and can be a **synth voice or a loaded sample**. A balance
  control crossfades the two.
- **Transient + noise** — adjustable click (amount + center frequency) and a
  noise layer with a color control, for snap and air on top of the body.
- **Drive** — per-voice saturation (amount / mode / bias) plus a separate
  FX-bus drive stage with its own mode and mix.

### Effects chain (reorderable)
- **Filter** — high-pass and low-pass with resonance, plus `color` and `teeth`
  voicing.
- **Delay** — tempo-synced with sub-sample-accurate timing, feedback, `morph`,
  `smear`, and mix.
- **Reverb** — multiple algorithms with size, decay, damping, diffusion,
  pre-delay, and mix.
- Drag to reorder the **Drive → Filter → Delay → Reverb** stages; the order is
  saved per preset.

### Ducking
- **DUCK routing** — reverse-bass sidechain ducking that cycles
  `Off → A → B → AB`, so you can duck the sub, the body, or both for classic
  reverse-bass movement. A separate always-on wet-bus tail duck keeps
  reverb/delay tails out of the way of each hit.

### Workflow
- **Macros** — six one-knob shapers on the bomb's nose (PITCH, DECAY, PUNCH,
  WEIGHT, MOOD, SPACE) plus the OUT hero knob.
- **Scope** — a live output-waveform strip across the top of the UI.
- **Preset bank** — 10 curated factory presets plus full user preset
  save / rename / delete.
- **Six themes** — VAULT (default), BANDW, CYBER, MATRIX, NIGHTRUN, PLASMA.
- **Offline bounce** — render the current kick straight to WAV or AIFF; runs
  offline against a fresh DSP instance, so live audio keeps flowing and the
  result is reproducible.
- **Optional key-tracking** so the kick follows incoming MIDI note pitch.

### HYPERFOCUS BBS
Tap into the bomb's nose to dial up a 1992-style bulletin-board terminal baked
into the plugin — a BoomFeed and a playable Space-Impact-style mini-game (try
the Konami code). It's a flourish, not a dependency: it touches none of the
audio path.

## Controls

| Input | Action |
|---|---|
| `T` key | Fire a test kick |
| MIDI NoteOn | Trigger the engine (with optional key-tracking) |
| Click + vertical drag | Adjust any knob |
| Shift + drag | Fine control |
| Ctrl + click / double-click (knob) | Reset to default |
| Tap the nose (7×) | Open / close the HYPERFOCUS BBS |
| `Esc` | Dismiss the BBS from anywhere |

## Preset locations

User presets are stored under the per-user config directory:

| Platform | Location |
|---|---|
| Linux | `~/.config/Bombo/Presets` |
| macOS | `~/Library/Bombo/Presets` |
| Windows | `%APPDATA%\Bombo\Presets` |

## Building from source

Requires **CMake ≥ 3.22** and a **C++17** compiler. JUCE 8 and the CLAP
extensions are fetched automatically via CMake `FetchContent`.

<details>
<summary>Linux system dependencies (Debian / Ubuntu / Pop!_OS)</summary>

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build \
    libasound2-dev libjack-jackd2-dev \
    libfreetype6-dev libfontconfig1-dev \
    libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
    libxinerama-dev libxrandr-dev libxrender-dev \
    libwebkit2gtk-4.1-dev libglu1-mesa-dev libcurl4-openssl-dev mesa-common-dev
```
</details>

<details>
<summary>Linux system dependencies (Arch / Manjaro)</summary>

```bash
sudo pacman -S --needed base-devel cmake ninja \
    alsa-lib jack2 freetype2 fontconfig \
    libx11 libxcomposite libxcursor libxext libxinerama libxrandr libxrender \
    webkit2gtk-4.1 glu mesa curl
```
</details>

```bash
git clone https://github.com/hyperfocusdsp/bombo.git
cd bombo

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# Run the standalone
./build/Bombo_artefacts/Release/Standalone/Bombo
```

LTO is on by default for release builds; pass `-DBOMBO_LTO=OFF` on
memory-limited machines or for faster CI builds.

### Tests

```bash
cmake --build build --target Bombo_Tests
ctest --test-dir build --output-on-failure
```

See [`TESTING.md`](TESTING.md) for the manual sanity-check checklist.

## Support and feedback

- **Email:** [feedback@hyperfocusdsp.com](mailto:feedback@hyperfocusdsp.com)
- **GitHub Issues:** [open an issue](https://github.com/hyperfocusdsp/bombo/issues/new/choose)

Your DAW + OS line and specific repro steps help most.

## License

GPL-3.0-or-later. See [`LICENSE`](LICENSE) for the full text.
