# Changelog

All notable changes to this project will be documented in this file.
The format is loosely based on [Keep a Changelog](https://keepachangelog.com/),
and the project broadly follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- **Duck routing** — Reverse-bass duck triangle now cycles `Off → A → B → AB`
  instead of being a binary toggle (`pid::duckRouting` replaces the legacy
  `pid::duckVoiceA`). `A` is the original behaviour (Voice A sub ducked by
  Voice B body, classic reverse-bass). `B` ducks Voice B body via a synthetic
  25 ms trigger pulse — using subPart as the key fails because A's sub takes
  too long to ramp up while B's body has already faded by then. `AB` runs
  both legs simultaneously. `Off` is bit-identical to the pre-feature path
  (both per-voice duckers skipped entirely). The chain-level DUCK column
  (always-on tail duck on the wet bus) is unaffected by the routing — it
  continues to duck the reverb/delay tails as before.
- **Duck triangle UI** — The letter inside the triangle now rides the
  hypotenuse (`tl→apex` diagonal) instead of being painted upright, with
  contrast-aware colour (dark on bright accents, bright on dark accents)
  so it's readable on every theme. Off state shows the full word `DUCK`
  (auto-fit to the hypotenuse); active routing shows `A`/`B`/`AB`. Click cycles
  the routing param via `juce::ParameterAttachment`, mirroring the
  `DecRoutingPill` pattern (which has worked for the DEC knob since
  `2026-05-24`).
- **State migration** — Old DAW projects and user-saved presets containing
  the legacy `duck_voice_a` bool are transparently translated to
  `duck_routing=A` (true) or `duck_routing=Off` (false) on load. The
  migration helper (`bombo::migrateDuckVoiceAToRouting` in
  `Source/State/DuckMigration.h`) is a pure ValueTree transform run inside
  `setStateInformation` before `apvts.replaceState`. No user action
  required; no broken state.
- **Factory preset Reverze 1** — User-saved reverse-bass kick added at slot
  03 of the curated factory bank (existing 02–08 bumped to 03–09 then
  swapped slot 02↔03 so Rez stays at 02 and Reverze 1 lands at 03). Pew is
  now slot 09; game's shot-sound lookup is by displayName so the in-game
  audio is unaffected. Pew's `reverb_mix=0` enforced so the in-game shot
  stays dry (was 0.06 before).

### Build & CI
- **macOS CI build time** — The universal (Apple Silicon + Intel) macOS CI
  build linked with LTO and ran an unbounded parallel build on a 3-core
  runner, so a single translation unit could take hours and a whole job ran
  for ~3 hours. CI now disables LTO on macOS/Linux, pins the macOS build to
  `-j 3`, and caps each job at 45 minutes so a runaway fails fast.

### Fixed
- **Linux user-preset location** — User presets were written to
  `~/.config/.config/Bombo/Presets` (an accidental doubled `.config`). They
  now live at the correct `~/.config/Bombo/Presets`, and any presets left at
  the old path are migrated automatically on first launch.
- **Cross-platform boss behaviour** — The RUMBLR boss's phase-2 charge used
  the C library `rand()`, whose sequence differs per platform, making the
  charge cadence (and its unit test) non-portable. It now uses a seeded
  `std::mt19937`, for identical, reproducible behaviour on every platform.
- **BBS game speed (slow motion)** — The v2 game ran in slow motion wherever
  the host can't sustain a true 60 Hz repaint — most visibly under WSLg
  software rendering, but any heavy-paint frame did it. The sim integrates on
  a **fixed `1/60` timestep with no catch-up** (`tick()` advances exactly
  `kTickDt` per call, driven by `startTimer(16)`), so a starved timer slows
  wall-clock time directly. `BBSComponent::timerCallback` now steps the sim by
  **real elapsed time** (a wall-clock accumulator, capped at 4 steps/callback
  to avoid a spiral after a hitch), so the game runs at the intended speed
  regardless of frame rate. `tick()` itself is unchanged — only how often it's
  called — so game logic and tests are unaffected. Regressed in the v2 rewrite
  (`0e8ec2d` scaffold → `cb21815` "60 Hz fix"); v1 ran at 50 Hz.
- **Duck triangle label** — Resting (Off) state shows the full word `DUCK`
  again instead of a lone `D`; active routing still shows `A`/`B`/`AB`. Text is
  full-strength bone (was 0.35 alpha → near-invisible) and auto-fits the
  hypotenuse so the longer labels don't overflow the wedge. Bold on every theme.
- **OUT hero knob indicator** — The focus-ring (logo) knob's pointer was a thin
  bone stem that vanished into bright accent rings (magenta / neon themes). It's
  now contrast-aware: a wider dark-grey stem with a thin bone keyline over a
  bright accent, bone stem over a dark accent — legible on every theme.
- **Editor crop / scope flush** — Lifted the editor so the scope's green
  U-frame sits flush under the host/title bar (crop top moved to the
  scope-strip boundary at design-y 50), removing the empty band above the
  scope while keeping the 3 px U-frame border from clipping to sub-pixel.
  Window aspect ≈ 0.586 (was ≈ 0.578).
- **Loop cache** — Fixed the audible "every other hit cut" alternation in
  LOOP + TAIL ON mode where reverb tails alternated between full,
  truncated, and tonally altered every beat. Root cause: the loop cache
  invalidation check uses `std::memcmp` on `ChainParams`, but the struct
  mixes `float`/`int`/`bool` and the compiler inserts padding bytes
  between fields. Default construction (`bombo::ChainParams p;`) left
  padding uninitialized (stack garbage), so two structurally-equal
  instances built in different stack frames could fail `memcmp` on
  padding alone. The cache invalidated every block, forcing per-beat
  re-capture with a `chain_.reset()` between each — different live FX
  state per beat = audible alternation. Fix: `std::memset(&p, 0, sizeof(p))`
  at the top of `BomboProcessor::buildChainParamsFromApvts()` zeroes
  padding deterministically; named-field assignments below override the
  zeroed bytes for actual values. Symptom went latent before because the
  exact padding pattern depended on what was on the stack, which is
  build/optimizer/runtime sensitive — recent commits shifted call stacks
  enough to expose it consistently. Bombo's existing audio-regression
  test ("AUDIO VALIDATION: reverb-on 8-beat loop") doesn't catch this
  because it constructs the processor once and runs straight through; the
  bug needs a steady stream of `processBlock` calls that each rebuild
  `ChainParams` from a fresh stack frame.
- **UI** — Eliminated the thin diagonal AA seam at the upper-left/right
  corners of the orange nose region. The chassis is now filled in a single
  `fillPath` with a sharp body→nose gradient stop at `redRegionTopY`, so
  there is no horizontal cut to misalign with the silhouette diagonal.
  Visible in every theme prior to the fix.
- **Nose macros** — The 7 macro knobs in the bomb's nose (OUT hero +
  PITCH/DECAY/PUNCH/WEIGHT/MOOD/SPACE satellites) receive clicks again.
  `NoseComponent`'s overlay had been intercepting every click in the nose
  bounds since the 7-tap activation shipped; a new `hitTest` hook now
  passes clicks through wherever a macro slider sits beneath the overlay.
  The 7-tap unlock and BBS re-open still work everywhere else.
- **Scope** — Capped `WaveBuffer::prevLength_` at 1500 samples
  (≈750 ms at 48 kHz / kDecim=24). Long reverb tails bleeding above the
  -54 dBFS silence floor no longer push the next trigger's X-axis to
  multi-second territory, so a dry kick on the following trigger fills
  the scope instead of compressing to ~10 % of its width.
- **BBS** — `ESC` now dismisses BBS regardless of where the mouse / focus
  is. The editor forwards non-modifier keys to `BBSComponent::keyPressed`
  whenever BBS is visible, so `S` / `T` / `N` / `P` / `F` / `R` / `M` /
  arrows etc. work without re-clicking into the BBS panel first.
- **BBS — T trigger** — `T` fires a one-shot trigger from any BBS screen
  (Intro / BoomFeed / MyDownloads), matching the main view's T behaviour.
  Previously the key was swallowed by BBS's catch-all.
- **BBS — preset save** — `S` (save current kick to bank) now works on
  fresh installs. `PresetBank::saveAs` creates `~/.config/Bombo/Presets/`
  (Linux) / `Bombo/Presets` (macOS/Windows app-data) before writing.
  The BBS panel also surfaces actual save failures instead of always
  reporting success.

## [v1.0-rc0] — 2026-05-18

First release candidate. Tagged at `226cc52`. JUCE rewrite from the
original nih-plug prototype: chassis bomb-tail silhouette, VAULT theme
default, R4B-CLASSIC nose, hex-ring macros, BBS terminal overlay, preset
bank CRUD, scope strip, offline WAV/AIFF bounce, sub-sample-accurate
tempo-sync delay, factory VOICE B kicks.
