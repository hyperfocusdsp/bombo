# Changelog

All notable changes to this project will be documented in this file.
The format is loosely based on [Keep a Changelog](https://keepachangelog.com/),
and the project broadly follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
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

### Repo hygiene
- Pseudonymity audit — WSL local master had drifted onto pre-scrub
  history with 11 commits authored as `30924992+Hornfisk@users.noreply.github.com`.
  Hard-reset to clean `origin/master`; per-repo `user.email` was already
  the Hornfisk noreply.

## [v1.0-rc0] — 2026-05-18

First release candidate. Tagged at `226cc52`. JUCE rewrite from the
original nih-plug prototype: chassis bomb-tail silhouette, VAULT theme
default, R4B-CLASSIC nose, hex-ring macros, BBS terminal overlay, preset
bank CRUD, scope strip, offline WAV/AIFF bounce, sub-sample-accurate
tempo-sync delay, factory VOICE B kicks.
