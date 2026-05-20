# Changelog

All notable changes to this project will be documented in this file.
The format is loosely based on [Keep a Changelog](https://keepachangelog.com/),
and the project broadly follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Fixed
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
