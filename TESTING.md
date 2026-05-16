# Testing Bombo (JUCE port)

This is the hand-off checklist for sanity-checking the JUCE rewrite against the Rust archive at `~/repos/bombo-rust-archive/`.

## Build & install

```bash
cd ~/repos/bombo
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./tools/install-linux.sh           # Linux:   → ~/.vst3/Bombo.vst3
# ./tools/install-macos.sh         # macOS:   → ~/Library/Audio/Plug-Ins/{VST3,Components}
# tools\install-windows.ps1        # Windows: → C:\Program Files\Common Files\VST3\
```

Standalone binary: `./build/Bombo_artefacts/Release/Standalone/Bombo` (or just `bombo-launch` if you have `~/.local/bin/` on PATH).

### First-run setup

**Enable MIDI input in the standalone.** JUCE Standalone ships with no MIDI inputs enabled by default:

1. Click **Options** in the top-left of the standalone window
2. **Audio/MIDI Settings…**
3. Tick the box next to your controller (e.g. **Arturia BeatStep**)
4. Close — the setting persists in `~/.config/Bombo/Bombo.settings`

**Triggering without MIDI:** press **Space**, **T**, or **Enter** in the editor window — fires a kick at the buffer head. Works in both standalone and inside a DAW (as long as the plugin editor has keyboard focus).

**Hyprland users:** the chassis is fixed-aspect, so it tiles badly. The rule below makes it float at native size; it's already in `~/.config/hypr/hyprland.conf`:

```
windowrule {
    name = bombo-float
    match:class = ^Bombo$
    float = true
    center = true
    size = 1320 880
}
```

## Run the unit tests

```bash
cmake --build build --target Bombo_Tests
ctest --test-dir build --output-on-failure
# or run the binary directly for per-test output:
./build/Bombo_Tests_artefacts/Release/Bombo_Tests
```

33 k+ assertions across VoiceClip / MasterBus / AmpEnvelope / BiquadFilter / Oscillator / BombVoice / FdnReverb. CI runs the same on Linux + macOS + Windows.

## Sanity checks in a DAW

Load `Bombo.vst3` (or `Bombo.component` on macOS) on an instrument track in Bitwig, Reaper, or Renoise. Send it a MIDI note and check:

| What to verify | How |
|---|---|
| Plugin scans cleanly | DAW plugin browser shows Bombo under Hyperfocus DSP / Drum |
| Audio fires on note-on | C-2 / C-1 → audible kick. Hits at the buffer head should land sample-accurate. Also: Space / T / Enter in the editor → same trigger. |
| Voice stealing | Play a 16th-note flam (≥4 hits inside ~20 ms). The first 4 voices crossfade with 5 ms fadeout; further hits drop without crackle. |
| FX chain audible | Adjust `Reverb Mix` and `Delay Mix` — the wet bus should grow. `Duck Depth` should pull the wet down on each kick. |
| Master limiter holds | Push `Master Out` to +6 dB. The output peak should stay at ≈-0.45 dBFS (no clipping). |
| Bypass character | `Drive Mode = Off` and `FX Drive = 0` should make the drive stage a passthrough. |
| State round-trip | Save DAW project → close → reopen. All 42 params restore. |
| Editor renders | 1320×950 chassis, 8 mil-rice sections, AllertaStencil title, bomb-tail fin band visible at the bottom. |

## A/B against the Rust archive

The original nih-plug build still exists read-only at `~/repos/bombo-rust-archive/`. Build it the old way for side-by-side:

```bash
cd ~/repos/bombo-rust-archive
cargo xtask bundle bombo --release
# install to a different name so they don't collide:
rm -rf ~/.vst3/Bombo-Rust.vst3
cp -r target/bundled/Bombo.vst3 ~/.vst3/Bombo-Rust.vst3
# Renoise/Bitwig should now see both Bombo and Bombo-Rust.
```

Load both on identical tracks, send the same MIDI, A/B by ear. The DSP was ported as JUCE-idiomatic where it fit (juce::dsp where applicable) and verbatim where the sound character matters — most notably the **FDN reverb's Hadamard matrix, FDN lengths `[743, 1093, 1361, 1697]`, irrational LFO rates `[0.39, 0.51, 0.73, 1.07] Hz`, and `0.18` in-loop LP damping coefficient** are bit-identical to the Rust source. The voice itself (polyBLEP osc + pitch/amp envelopes + click + body + voice-clip) is mathematically identical.

Expected drift: tiny — within ear-test tolerance on transients, possibly a few cents of micro-pitch wobble on the reverb tail due to LFO phase init differences. Sub fundamental, click character, and overall punch should read as the same instrument.

## Phase status

| Phase | Status | Notes |
|---|---|---|
| 0 — JUCE scaffold | ✅ | `CMakeLists.txt`, VST3+AU+Standalone, Hyperfocus DSP branding |
| 1a — Voice path | ✅ | osc / envelopes / click / noise / drift / voice_clip / BombVoice; 4-voice pool with 5 ms fade-on-steal; 8-slot pending-hit ring |
| 1b — FX chain | ✅ | BiquadFilter / Delay / FdnReverb / Ducker / MultibandLimiter / MasterBus / RumbleChain |
| 2 — APVTS + state | ✅ | 42 params, raw-pointer cache, XML round-trip via host |
| 3 — Faceplate | ✅ MVP | Mil-rice palette, 8 sections, AllertaStencil header. Blender-baked KnobRenderer + ScopeComponent deferred. |
| 4 — Drag-reorder + fin | ⚠️ partial | Bomb-tail fin band shipped. FX-rack drag-reorder + transparent host BG + LIM `E` overlay parked. |
| 5 — Presets / MIDI Learn / Layout editor | ⏳ deferred | DAW state save covers the immediate need. PresetManager + MIDI Learn UI + LayoutEditOverlay are real but post-KVRDC. |
| 6 — Packaging / CI | ✅ | Install scripts for all 3 OSes. GitHub Actions matrix builds + tests on Linux / macOS / Windows. macOS is ad-hoc signed (no Apple Dev). |
| 7 — Tests | ✅ | 7 JUCE UnitTestRunner suites, 33 k+ assertions, run via ctest. |

## Known limitations

- **No FX drag-reorder yet** — the rack visually groups the 7 sections but they don't swap. The DSP chain is fixed serial: DRY → DRIVE → FILTER → DELAY/REVERB(parallel) → DUCK → MULTIBAND → MASTER.
- **No Blender-baked knobs yet** — rotary draws are clean section-colored ticks + flat caps. The full SquelchPro-style photoreal pipeline lands in a polish pass.
- **No scope** — the hero band's real-time wave display is parked. Voice-active state still drives correctly; just no visual.
- **No factory presets** — the Rust archive's `assets/factory_presets/` was empty, so nothing to port. DAW state save covers user-side persistence.
- **macOS signing is ad-hoc** — Gatekeeper will warn on first load. Right-click → Open. Notarization is blocked on Apple Dev enrollment (see memory: `feedback_bombo_plan_b_plus_mac_strategy`).
