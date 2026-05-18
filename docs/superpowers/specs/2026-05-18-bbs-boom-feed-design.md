# Bombo BBS — Boom Feed Design Spec

**Date:** 2026-05-18  
**Status:** Approved — ready for implementation  
**Scope:** v1.0 (KVRDC 2026 submission, effective deadline 2026-05-30)

---

## 1. Overview

The BBS is a hidden 1992-aesthetic terminal overlay inside Bombo. It wraps the **Boom Feed** — a Tinder-style kick randomizer (RANDOM or MUTATE mode) — in demoscene/BBS cultural chrome. Users discover it by tapping the nose detonator repeatedly; the first entry is a dramatic 7-tap escalating-glitch sequence that only runs once. A **progression system** (5 levels, driven by saves) unlocks new SYSOP voices and themes, rewarding repeat use through the KVRDC voting window.

The BBS is also the foundation for a **TUI design language** (`BomboLookAndFeel`) that will extend to all plugin menus in a follow-on session.

---

## 2. Goals

- Ship a functional Boom Feed (randomize → hear → save → repeat) with BBS aesthetic
- First-time nose activation sequence that is reel-worthy and memorable
- Progression system that drives retention during the KVRDC voting window
- Lay `BomboLookAndFeel` groundwork for menus inside BBS (global extension is v1.1)
- All BBS screens fully cohesive: monospace font, dark bg, phosphor green, box-drawing borders

## 3. Non-goals (v1.1)

- CRT phosphor shader / scanlines
- Audio hum loop
- 1% random line glitch effect
- WHO'S ONLINE panel
- Perceptual hash fingerprinting
- Extending `BomboLookAndFeel` to the whole plugin's menus/popups
- NIGHTRUN rhythm minigame (separate brainstorm)

---

## 4. Architecture

### 4.1 Existing infrastructure (unchanged)

| Component | Role |
|-----------|------|
| `Source/GUI/BBS/BBSComponent.h/.cpp` | Scaffold: backdrop, Esc-to-dismiss, show/hide. Expanded — not replaced. |
| `Source/State/PersistentState.h/.cpp` | PropertiesFile wrapper. New `bbs.*` keys added here. |
| `Source/PluginEditor.h/.cpp` | Wires `bbs_`. Temp `bbsButton_` TextButton removed; nose long-press wired instead. |
| `Source/PluginProcessor` + APVTS | Parameter setting and kick trigger — BoomFeed writes here. |
| `Source/Presets/PresetManager` | Saving kicks. BoomFeed calls `saveCurrentAsUser()`. |

### 4.2 New files

| File | Purpose |
|------|---------|
| `Source/GUI/BBS/BBSScreens.h/.cpp` | Screen state machine: HIDDEN → INTRO → BOOM_FEED ↔ MY_DOWNLOADS |
| `Source/GUI/BBS/BoomFeed.h/.cpp` | RANDOM / MUTATE toggle, param randomization, kick preview trigger |
| `Source/GUI/BBS/SysopContent.h` | Static const tables: 7 SYSOP voices × MOTDs + scroller lines |
| `Source/GUI/BBS/ProgressionManager.h/.cpp` | Save counter, level thresholds, unlock state, PersistentState I/O |
| `Source/GUI/BBS/BomboLookAndFeel.h/.cpp` | Custom JUCE LookAndFeel for BBS-internal menus (foundation for global extension) |
| `Source/GUI/Nose/NoseComponent.h/.cpp` | Multi-tap gesture, glitch callbacks, tooltip, 5-level crack/glow paint |
| `tests/BBSProgressionTests.cpp` | Level thresholds, unlock logic, persistence round-trip |
| `tests/BoomFeedTests.cpp` | Random param bounds, mutate drift bounds |

### 4.3 Data flow

```
User taps nose (first time: 7-tap sequence)
  → NoseComponent::onActivationComplete()
    → PluginEditor::showBBS()
      → BBSComponent::show() → BBSScreens → INTRO state

BBS INTRO completes
  → BBSScreens → BOOM_FEED state
  → ProgressionManager::currentSysop() → loads today's SYSOP voice

User presses NEXT in Boom Feed
  → BoomFeed::advance(mode)       // RANDOM or MUTATE
    → generates param snapshot
    → apvts.getParameter(id)->beginChangeGesture()
    → apvts.getParameter(id)->setValueNotifyingHost(v)
    → apvts.getParameter(id)->endChangeGesture()
    → fires trigger param (same path as TRIGGER button)

User presses SAVE
  → PresetManager::saveCurrentAsUser("KICK-XXXX-YYYY")
  → ProgressionManager::onKickSaved()
    → increments bbs.saves_count in PersistentState
    → checks level thresholds → fires unlock if crossed
    → NoseComponent::repaint() with new level

User presses TAB
  → BBSScreens → MY_DOWNLOADS state (lists user presets)
  → TAB again → back to BOOM_FEED
```

---

## 5. Screen State Machine

### States

```
HIDDEN ──── tap nose ────► INTRO (skip: ESC) ────► BOOM_FEED
                                                      │    ▲
                                                     TAB  TAB
                                                      ▼    │
                                                    MY_DOWNLOADS
                                    (ESC from any state → HIDDEN)
```

### INTRO screen (~3 seconds, skippable with ESC)

- Black screen
- Typewriter: `ATDT 555-1992...` → `CONNECT 2400` → noise burst → ASCII BOMBO logo
- On completion (or ESC): transition to BOOM_FEED
- **Skip-on-return:** if `bbs.first_entry_done == true`, INTRO is skipped entirely on subsequent opens — single tap → instant BOOM_FEED

### BOOM_FEED screen

```
┌─────────────────────────────────────────────────┐
│ HYPERFOCUS BBS v2.3       SYSOP: FUTURE CREW    │  ← header
├─────────────────────────────────────────────────┤
│                                                 │
│  ── KICK ROM BROWSER ──────────────────────     │
│  FILENAME : KICK-7F3A-9B22.KCK                  │
│  SIZE     : 3.2 KB                              │
│  WAVEFORM : ▁▂▃▄█▇▅▃▁▂▄▆█▇▄▂▁                  │
│                                                 │
│  [ ◀ PREV ]  [ ▶ PLAY ]  [ ⟳ NEXT ]  [ ♥ SAVE ]│
│                                                 │
│  MODE: [RANDOM] / MUTATE                        │
│                                                 │
├─────────────────────────────────────────────────┤
│ MOTD: WELCOME LAMER · SECOND REALITY VIBES      │  ← per-SYSOP
├─────────────────────────────────────────────────┤
│ ▸ FUTURE CREW PRESENTS: HYPERFOCUS BBS ...      │  ← scroller
└─────────────────────────────────────────────────┘
  [ TAB = MY DOWNLOADS ]  [ ESC = EXIT ]
```

- **PREV:** restores last param snapshot (ring buffer of 5 entries)
- **PLAY:** fires kick trigger without changing params
- **NEXT:** calls `BoomFeed::advance(mode)`, auto-fires trigger after 50ms
- **SAVE:** saves current params as preset, increments progression counter
- **MODE toggle:** click or `M` key — switches between RANDOM and MUTATE

### MY_DOWNLOADS screen

```
┌─────────────────────────────────────────────────┐
│ MY DOWNLOADS                        14 FILES    │
├─────────────────────────────────────────────────┤
│  NAME                    SIZE    DATE      TIME │
│─────────────────────────────────────────────── │
│► KICK-7F3A-9B22.KCK      3.2KB   05-18    14:23│  ← selected row
│  KICK-A1B2-C3D4.KCK      3.1KB   05-18    14:25│
│  KICK-F5E6-D7C8.KCK      3.0KB   05-18    15:02│
│  ...                                            │
├─────────────────────────────────────────────────┤
│ [ ↑↓ ] NAV  [ ENTER ] LOAD+PLAY  [ DEL ] DELETE│
└─────────────────────────────────────────────────┘
  [ TAB = BOOM FEED ]  [ ESC = EXIT ]
```

- Pulls from `PresetManager::getUserPresets()`
- ENTER loads preset into APVTS + fires kick trigger
- DEL calls `PresetManager::deleteUserPreset(name)` with "ARE YOU SURE? [Y/N]" inline confirm

---

## 6. Nose Activation

### 6.1 First-time sequence (7 taps)

Stored in `PersistentState` as `bbs.first_entry_done`. Runs only once; never again once set to `true`.

| Tap | Visual effect | Tooltip / overlay text |
|-----|--------------|------------------------|
| hover | — | `⚠  WARNING: DO NOT TOUCH` |
| 1 | tiny flicker (40ms) | `UNAUTHORIZED ACCESS ATTEMPT LOGGED` |
| 2 | garbled text burst (200ms) + hairline crack appears on nose | `R̷Ë̸Ä̴D̷ ̶Ä̴C̸C̶Ë̷S̶S̵` |
| 3 | full-plugin black flash (80ms) | `BREACH DETECTED · INITIATING COUNTERMEASURES` |
| 4 | static noise covers plugin (300ms) | `▓▒░▓▒░▓▒░▓▒░▓▒░` |
| 5 | red flash | `ACCESS DENIED` → fades to `...processing exception...` |
| 6 | green pulse + nose fully ignites (preview of L4 state) | `CLEARANCE OVERRIDE ACCEPTED · STAND BY` |
| 7 | modem handshake begins | BBS opens → `bbs.first_entry_done = true` |

**Implementation:** `NoseComponent` maintains a `tapCount_` int (reset to 0 on any 2s idle). Each tap calls `PluginEditor::triggerGlitch(GlitchLevel)` — a short-lived animation flag that `PluginEditor::paint()` reads. Pure GUI thread; no audio impact.

### 6.2 Subsequent opens

Single tap → modem handshake → BBS. No sequence, no drama.

### 6.3 Force-reset (secret)

- Set DRIVE knob to 0 + REVERB SIZE to max, then tap nose 3× rapidly
- Resets `bbs.first_entry_done = false` and `bbs.level = 0` and `bbs.saves_count = 0`
- Documented once, cryptically, in the NFO file bundled with the binary release:  
  `"UNIT RESET PROCEDURE: [see CLASSIFIED section] — AUTHORIZED PERSONNEL ONLY"`
- The CLASSIFIED section of the NFO is itself the level-4 unlock easter egg text

---

## 7. Boom Feed Mechanic

### 7.1 RANDOM mode

Generates a completely fresh kick by rolling all musically relevant parameters within sane bounds:

| Param group | Bound strategy |
|-------------|---------------|
| Pitch / pitch env | Full range but weighted toward 40–120 Hz center |
| Decay | 50–700ms (avoids inaudibly short or DAW-filling long) |
| Drive | 0–0.8 normalized (avoids full clip distortion) |
| Filter HP/LP | HP 20–200Hz, LP 800–18kHz |
| Reverb | Size 0–0.6, Mix 0–0.4 (subtle by default) |
| Delay | Mix 0–0.3 (subtle by default) |

All other params: full range uniform random.

A fake "filename" is generated from a short hash of the param values: `KICK-[4hex]-[4hex].KCK`.

### 7.2 MUTATE mode

Takes the current param snapshot and adds Gaussian noise (σ = 0.12 normalized) to each param, clamped to [0, 1]. Produces a "cousin" of the current kick. Good for fine exploration after finding a direction.

### 7.3 PREV (history ring buffer)

`BoomFeed` maintains a ring buffer of 5 param snapshots (FIFO). PREV pops the last entry and restores it. Snapshots are stored as `std::array<float, N>` (normalized values, indexed by param order).

### 7.4 Waveform display

A simplified ASCII waveform: render one cycle of the kick's pitch-start frequency as a bar chart using `▁▂▃▄▅▆▇█` characters. Computed once per advance, stored as a `juce::String`. Not a real audio render — a visual approximation from params.

---

## 8. Progression System

### 8.1 Level thresholds

| Level | Saves trigger | Unlock | Nose state |
|-------|--------------|--------|------------|
| 0 | start | 3 SYSOP voices (FC, Spaceballs, TRSI) | clean |
| 1 | 5 saves | Razor 1911 SYSOP unlocks | hairline crack |
| 2 | 15 saves | Fairlight SYSOP unlocks | crack + faint glow |
| 3 | 30 saves | Triton SYSOP + NIGHTRUN BBS color scheme | cracks spread + phosphor glow |
| 4 | 50 saves | Loonies/Conspiracy SYSOP + CLASSIFIED ROM section | fully ignited + pulse |

### 8.2 SYSOP selection

From the pool of unlocked voices, daily SYSOP = `UTC_weekday % unlocked_count`. Early users see 3 voices rotating; as more unlock, the rotation enriches.

### 8.3 Unlock animation

On level-up: brief full-BBS flash → `SYSTEM MESSAGE: NEW CONTENT UNLOCKED` in the MOTD bar → nose repaints → new SYSOP greeting plays on next open.

### 8.4 CLASSIFIED section (level 4)

At level 4, a new "CLASSIFIED" entry appears in the ROM browser marked `[LOCKED]` until level-4 unlock. On unlock: reveals a set of 5 hidden factory presets (not accessible from the main preset bank) plus the easter egg text in the NFO format:
```
NFO ─────────────────────────────────────────
  HYPERFOCUS BBS · CLASSIFIED ARCHIVE
  CLEARANCE GRANTED: AGENT [username if available]
  
  "UNIT RESET PROCEDURE:
   DRIVE=0 · REVERB SIZE=MAX · TAP NOSE ×3"
  
  YOU FOUND IT. TELL NO ONE.
─────────────────────────────────────────────
```

---

## 9. SYSOP Content

### Launch (3 voices)

**Future Crew** — Second Reality era  
MOTDs (pick 1 randomly per session):
- `WELCOME LAMER · DOWNLOAD AT YOUR OWN RISK · SECOND REALITY VIBES TODAY`
- `GREETINGS FROM THE CREW · MUSIC BY PURPLE MOTION · KICKS LOADED`
- `PC DEMO SCENE IS NOT DEAD · NEITHER ARE YOUR DRUMS`

Scroller: `FUTURE CREW PRESENTS: HYPERFOCUS BBS · THE GREATEST KICK ROM ARCHIVE IN THE KNOWN GALAXY · GREETINGS TO ALL SCENE HEADS ·`

---

**Spaceballs** — Amiga legends  
MOTDs:
- `NINE FINGERS WAS HERE · HARDCORE KICKS ONLY · WHO SAID AMIGA WAS DEAD`
- `PROTRACKER FOREVER · WAREHOUSE KICKS LOADED · SPACEBALLS SALUTES YOU`
- `AMIGA 1200 OR NOTHING · THESE KICKS ARE HAND-CRAFTED`

Scroller: `SPACEBALLS · NINE FINGERS IS STILL IN THE BUILDING · THE AMIGA NEVER DIES · GREETINGS TO ALL COPPER LOVERS ·`

---

**TRSI** (Tristar Red Sector Inc.) — crack-scene voice  
MOTDs:
- `RELEASE NOTES: PURE FILTH KICKS · NO PROTECTION SCHEMES THIS RELEASE`
- `TRAINED BY TRSI · THESE KICKS REQUIRE NO SERIAL · FREE AS IN FREEDOM`
- `FIRST RELEASE OF THE WEEK · THE COMPETITION IS SLEEPING`

Scroller: `TRISTAR RED SECTOR INC · ELITE KICK DISTRIBUTION SINCE 1989 · THIS RELEASE IS UNPROTECTED · SPREAD THE WORD ·`

---

### Unlock progression (4 voices)

**Razor 1911** (L1) — warez-scene NFO  
MOTDs: `EVEN FREE STUFF NEEDS A NFO · GREETZ TO THE CRACKERS · SINCE 1985`

**Fairlight** (L2) — demoparty  
MOTDs: `WEEKEND DEMOPARTY MODE · WAREHOUSE KICKS LOADED · WE BROUGHT SNACKS`

**Triton** (L3) — Amiga tracker culture  
MOTDs: `GREETZ TO THE COMP.SYS.AMIGA HEADS · OCTAMED FOREVER · CRYSTAL DREAM ERA`

**Loonies/Conspiracy** (L4) — 4k intro purists  
MOTDs: `4 KILOBYTES OF KICK ENERGY · YOU CAN DO BETTER · ASSEMBLY DEADLINE TONIGHT`

---

## 10. BomboLookAndFeel

A new `BomboLookAndFeel : public juce::LookAndFeel_V4` overrides popup/menu rendering for **BBS-internal use only** in this spec. Global extension to the full plugin is a follow-on task.

Methods to override:

```cpp
void  drawPopupMenuBackground(Graphics&, int w, int h) override;
void  drawPopupMenuItem(Graphics&, const Rectangle<int>&, bool isSeparator,
                        bool isActive, bool isHighlighted, bool isTicked,
                        bool hasSubMenu, const String& text,
                        const String& shortcutKeyText,
                        const Drawable* icon,
                        const Colour* textColour) override;
Font  getPopupMenuFont() override;  // returns monospace
void  drawTextEditorOutline(Graphics&, int w, int h,
                            TextEditor&) override;  // box-drawing border
```

`BomboLookAndFeel` reads palette from `ThemeProvider::current()` so it stays theme-aware. The BBS overlay sets `setLookAndFeel(&bomboLAF_)` on itself at construction.

---

## 11. Persistence

New keys in `PersistentState` (`~/.config/Bombo/Bombo.settings`):

| Key | Type | Default | Meaning |
|-----|------|---------|---------|
| `bbs.first_entry_done` | bool | false | First-time 7-tap sequence has run |
| `bbs.saves_count` | int | 0 | Total kicks saved (progression trigger) |
| `bbs.level` | int | 0 | Current progression level (0–4) |
| `bbs.unlocked_sysops` | String | `"0,1,2"` | Comma-separated indices of unlocked SYSOPs |
| `bbs.last_screen` | String | `"boom_feed"` | Remembers last active screen across opens |

---

## 12. Testing

### `tests/BBSProgressionTests.cpp`

- Level thresholds fire at correct save counts (5, 15, 30, 50)
- Level never exceeds 4
- Unlock list accumulates correctly (no duplicates, correct order)
- PersistentState round-trip (write → close → read back)
- Force-reset clears all keys to defaults

### `tests/BoomFeedTests.cpp`

- RANDOM mode: all generated params within defined bounds (run 1000 iterations)
- RANDOM mode: no NaN or out-of-[0,1] values
- MUTATE mode: drift from source is bounded (|delta| ≤ 0.3 for any param)
- PREV ring buffer: correct LIFO order, wraps at 5 entries
- Filename generation: correct `KICK-XXXX-YYYY.KCK` format, reproducible from same params

### Manual verification

- 7-tap sequence fires all glitch states in order
- `bbs.first_entry_done` set after 7th tap, sequence does not repeat on reopen
- Force-reset (DRIVE=0, REVERB=max, 3× nose tap) resets to level 0
- SYSOP rotates by weekday from unlocked pool
- Level-4 CLASSIFIED section visible only after 50 saves

---

## 13. Implementation notes

- **Glitch effects** (taps 3–5) use a `GlitchState` enum + animation timer in `PluginEditor`. `paint()` overlays the effect; audio thread untouched.
- **BBS trigger→kick path:** after NEXT, `BoomFeed` sets all params then sets an atomic `pendingTrigger_` flag. `PluginEditor` fires the existing TRIGGER mechanism on the next message-thread tick (50ms debounce).
- **Waveform string:** computed from `pitchStart` param only (sine approximation), not a real audio render. 18 characters wide.
- **Scroller animation:** `juce::Timer` at 60ms tick, advances character offset in scroller string. Pauses when BBS is hidden.
- **`bbsButton_` removal:** the temp `TextButton` in `PluginEditor` is deleted. Ctrl+Shift+B dev shortcut is kept during development, removed before v1.0 tag.

---

## 14. Out of scope (follow-on)

- CRT shader / scanlines — v1.1
- Audio hum loop — v1.1  
- `BomboLookAndFeel` applied globally to all plugin menus — v1.1 (foundation is laid)
- NIGHTRUN rhythm minigame — separate brainstorm, v1.1
- Cloud leaderboards / cross-user features — never
