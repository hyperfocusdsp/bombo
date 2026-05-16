# Bombo JUCE — bug & feature tracker

Status as of `430f410`. The DSP + APVTS + audio path are working; the
UI is functional-but-rough. Next session focuses on closing the visual
gap to the Rust archive at `~/repos/bombo-rust-archive/`.

## What works ✓

- All 7 DSP modules + voice pool + pending-hit ring + FX chain (FDN
  reverb verbatim). Kick triggers, FX chain processes.
- 42 APVTS params, raw-pointer cache, DAW state round-trip.
- 1-row 7-column rack (VOICE A · VOICE B · DRIVE · FILTER · DELAY ·
  REVERB · DUCK) with uniform knob slot height.
- Section-color knob caps + in-cap value readout via the param's
  `stringFromValueFunction`.
- Bomb-tail fin V band at the chassis bottom.
- Space / T / Enter trigger from the editor (clicks on knobs no
  longer steal focus).
- Standalone auto-enables every available MIDI input on launch
  (BeatStep / any controller works without Options → MIDI dance).
- ~33 k unit-test assertions across 7 DSP suites, ctest green.
- Linux install script, GH Actions matrix builds, Hyprland float rule.
- Editor is resizable (1080×620 .. 1600×960).

## Open bugs 🐛

### B1. Loop on Space doesn't fire
The Rust archive's editor binds Space to **toggle `loop_on`**, not to
trigger one-shot kicks (the user's mental model). In JUCE port I wired
Space to a one-shot trigger and left `loop_on` un-toggled. Either:
- (a) make Space toggle the `loop_on` APVTS BoolParam, OR
- (b) add a footer LOOP pill button that user clicks, keep Space as
  one-shot (simpler, but doesn't match the Rust UX).

Recommend (a) — `BomboEditor::keyPressed` flips the param via
`apvts.getParameter("loop_on")->beginChangeGesture()...`. Plus the
audio path already wires `loop_on` through the chain via
`buildChainParamsFromApvts` — verify that the loop logic in the
Rust processor actually fires triggers when `loop_on` is true. (See
`bombo-rust-archive/src/plugin.rs:489-543` — the loop scheduler
pushes pending hits at `samples_per_beat` intervals; that loop
scheduler **was not ported** in Phase 1b. So toggling the param
won't actually fire kicks until the scheduler is ported.)

**Files to touch:**
- `Source/PluginProcessor.cpp` — port the loop scheduler from
  `plugin.rs:489-543`. Needs `loop_samples_left`, `loop_was_on`,
  rising-edge detection, BPM source select (host transport vs
  knob), and `pushPending(sample_offset)` per beat tick. Wire
  `chain_.silenceTail()` on falling edge.
- `Source/PluginEditor.cpp` — Space toggles `loop_on` parameter.
- `Source/Parameters.h` — already has `loop_on / loop_bpm /
  loop_host_sync`? Check — may need to add.
- `Source/DSP/RumbleChain.h` — add a `silenceTail()` method that
  fires `delay_.stopFade()` + `reverb_.stopFade()`.

### B2. Chassis is too wide and too short
Currently 1280×720. Rust archive geometry:
- `WINDOW_W = FX_RACK_W + SECTION_COL_LMARGIN` ≈ 488 logical points
- `WINDOW_H = HEADER + HERO + RACK + FIN ≈ 939` logical points
- Scaled ~2× for the screenshot the user shared = ~976×1878 visually,
  but the **aspect ratio** is the point: narrower + taller, not
  wider + shorter.

Realistic JUCE target: ~**980×1100**. That fits a 1440p screen,
keeps the bomb-silhouette proportions, and still has room for a
proper scope + macro row.

**Files to touch:**
- `Source/PluginEditor.cpp` — `setSize(980, 1100)`, update resize
  limits.
- `Source/GUI/FaceplatePanel.cpp` — column widths, gaps, knob slot
  sizes proportional.
- `~/.config/hypr/hyprland.conf` — `size = 980 1100` in the rule.

### B3. Knob value text is too small
Cap font scales by 0.55 × cap radius. At current 60-px knobs the
readout is ~9 pt. Hard to read. Bump to 0.65-0.70 once chassis is
the right size (bigger cap radius will make this self-correct).

## Open features 🛠

### F1. Bomb silhouette chassis
The Rust archive paints a **12-vertex bomb-shape polygon** as the
chassis itself, with the body widening at the head and tapering at
the tail. Outside the silhouette = transparent (host BG shows
through). Reference: `bombo-rust-archive/src/ui/editor.rs:629-700`
(`paint_bomb_silhouette` and its callers).

JUCE plan: paint the silhouette in `BomboEditor::paint` over the
graphite chassis. Knob columns inside the silhouette, fin V at the
bottom. Transparent corners require `setOpaque(false)` on the
top-level editor + per-pixel alpha support from the host (works in
Bitwig / Reaper / Renoise on Linux; Windows hosts vary).

### F2. Waveform scope
The Rust archive's hero band has a 320×105-px live scope showing the
last ~250 ms of audio output. Driven by a lock-free ring buffer
pushed from the audio thread and pulled by a 30 Hz Timer in the
editor.

JUCE plan:
- Add a `bombo::WaveBuffer` (lock-free SPSC ring, `juce::AbstractFifo`
  + 2 ks float buffer). Owned by the processor, shared with the editor
  via `Arc`-style `std::shared_ptr`.
- Audio thread: at the end of each per-sample loop, push the master-bus
  output into the FIFO.
- `Source/GUI/ScopeComponent.h` — `juce::Component + juce::Timer`,
  pulls the latest N samples at 30 Hz, normalises, paints a polyline.
- Place it in the header or as a dedicated hero band between header
  and rack.

### F3. Macro knob row
Rust archive's hero band has 7 macro knobs aligned 1:1 with the FX
columns below (PITCH, DECAY, PUNCH, WEIGHT, MOOD, SPACE, plus a
seventh). Each macro fans out to several underlying params via a
visual position in `[-1, 1]`.

JUCE plan: defer until F1+F2 land. The macro fan-out is a separate
DSP-side decision (where each macro routes). For visual parity, add
a 7-slot row above the FX rack with a placeholder slider per slot.

### F4. Clickable / editable labels
Knob labels should:
- left-click → pop a tiny text editor for direct value entry
- right-click → context menu (MIDI Learn, reset to default, copy, paste)
- shift-click → reset to default

JUCE has `juce::Slider::onMenuShown` hooks and `juce::Label::onTextChange`.
Wire each `Control::label` to its slider for left-click→edit, plus
add `juce::Slider::TextEntryBoxPosition::TextBoxBelow` temporarily
on focus. Right-click menu via `MouseListener` on each Slider.

### F5. Section MUTE pills
Rust archive has small MUTE toggle pills on DRIVE / FILTER / DELAY /
REVERB / DUCK headers. Already have `drive_mute / filter_mute /
delay_mute / reverb_mute / duck_mute` BoolParams in the Rust spec —
**need to add them to APVTS** in the JUCE port. Then a `juce::
TextButton` styled as a 38×12 px pill at the right end of each
section title bar.

### F6. FX drag-reorder
DRIVE / FILTER / DELAY / REVERB columns are draggable in the Rust UI
— drop one onto another and the signal chain rewires. Phase 4 of the
original plan; parked because it needs a DSP chain refactor (current
chain is hardcoded order).

Needs:
- `FxOrder` permutation index (already an APVTS persisted property
  in the Rust archive — port the schema).
- `RumbleChain::process` to consume the order at runtime instead of
  hardcoded `dry → drive → filter → delay/reverb → ducker → multiband`.
- `juce::DragAndDropContainer` + `juce::DragAndDropTarget` on each
  FX column header. Ghost image, drop highlight, ease-out
  reflow animation.

### F7. Bomb-silhouette transparent corners
Once F1 ships, set `setOpaque(false)` on the editor + draw only the
silhouette. Host BG shows through the corners. Test in Renoise,
Bitwig, Reaper.

### F8. Macro fan-out wiring
Each macro knob routes to multiple underlying params via configurable
mappings. Rust archive does this via 7 macro positions × 8-param-each
matrix. Defer until F3 lands.

### F9. Factory presets
The Rust archive's `assets/factory_presets/` was empty (the user
never shipped presets). Either:
- Pick 4-6 starter patches by ear in the JUCE port, save them as
  APVTS XML, embed via BinaryData. Ship as "Init", "Hard 909",
  "Long Rumble", "Sub Drop", "Click Snap".
- Skip — DAW state save covers user-side persistence.

### F10. MIDI Learn UI
SquelchPro template has the full pattern (try-lock + async callback).
Right-click any knob → "MIDI Learn" → twist a CC → done. Settings
persist in `Resources/MidiMap.json`.

### F11. Photoreal knob via Blender
The current rotary draws a flat-color cap with a bone indicator —
good enough but not photoreal. SquelchPro's `KnobRenderer` ships
PNG body + normal map → CPU Blinn-Phong. Pipeline at
`~/repos/squelch_pro/tools/blender/`. Retune the JSON preset for
Bombo's mil-rice palette and run `./render_knob.sh`. Drop the
output PNGs into `Resources/Knobs/` + add to BinaryData. Mostly a
visual upgrade, not a functional one.

## Out-of-scope for next session

- HYPERFOCUS BBS mode (procedural kick generator + swipe UX)
- Wavetable / sample extension
- macOS notarization (still blocked on Apple Dev enrollment per
  `feedback_bombo_plan_b_plus_mac_strategy`)
- KVRDC submission itself (manual upload step)

## Priority order for next session

1. **B1** — port the loop scheduler. Without this, "loop on Space"
   never works. (DSP-side; mostly mechanical port from Rust.)
2. **B2** — resize chassis to ~980×1100 (narrower + taller).
3. **F2** — waveform scope. The user's been asking; visible win.
4. **F1** — bomb silhouette outline (paint, not transparent yet).
5. **F4** — clickable labels for direct value entry.
6. **F5** — section MUTE pills (small effort, real UX value).
7. **F3** — macro knob row (visual placeholder + fan-out wiring later).
8. **F6** — FX drag-reorder (the headline polish — needs DSP refactor).
9. **F7** — transparent corners (after F1 silhouette lands).
10. **F11** — photoreal knob via Blender.
11. **F9** — factory presets.
12. **F10** — MIDI Learn UI.
