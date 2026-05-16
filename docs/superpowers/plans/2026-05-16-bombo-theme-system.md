# Bombo Theme System (Plan A of 3) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert Bombo's current fixed `bombo::col::*` colour constants into a runtime-switchable theme system, ship three bundled themes (BANDW / PHOSPHOR / NIGHTRUN), and persist the user's choice across sessions. Foundation for the BBS feature (Plans B and C).

**Architecture:** Replace the immutable constants in `Source/GUI/Colours.h` with zero-arg accessor functions that read from a `ThemeProvider` singleton. The singleton owns the active `Palette` (a POD struct mirroring every colour currently in `Colours.h`), broadcasts changes to a JUCE `ChangeBroadcaster`, and loads palettes from JSON files baked in via `juce_add_binary_data`. A new `PersistentState` wrapper around `juce::PropertiesFile` stores the active theme name across sessions.

**Tech Stack:** JUCE 8 (C++17), CMake, `juce::var` JSON parser, `juce::ChangeBroadcaster`/`ChangeListener`, `juce::PropertiesFile`, JUCE UnitTestRunner (existing `Bombo_Tests` target).

**Context decomposition (3 plans):** This is Plan A of 3 for the BBS-shell v1.0 work. Plan B adds the dual-page tab + HeaderBar. Plan C adds the BBS lobby content. Plan A must ship clean (zero visual regression, theme switcher functional) before Plan B starts.

**Spec reference:** `/home/natalia/.claude/plans/alright-let-us-carry-radiant-snowglobe.md`

---

## File Structure

**Create:**
- `Source/GUI/Theme/Palette.h` — POD struct mirroring every `bombo::col::*` field
- `Source/GUI/Theme/ThemeProvider.h` + `.cpp` — singleton, active palette, change broadcaster, named-theme registry
- `Source/GUI/Theme/ThemeLoader.h` + `.cpp` — JSON → `Palette` parser; loads from `BinaryData`
- `Source/State/PersistentState.h` + `.cpp` — `juce::PropertiesFile` wrapper for non-DAW user-global state
- `Resources/Themes/bandw.json`, `phosphor.json`, `nightrun.json` — three bundled palettes
- `tests/PaletteTests.cpp` — split-out test file included from `RunTests.cpp` (keeps DSP and theme tests separate)

**Modify:**
- `Source/GUI/Colours.h` — accessor functions instead of const constants
- `Source/GUI/BomboLookAndFeel.h` — implement `ChangeListener`, repaint dispatch on theme change
- `Source/GUI/FaceplatePanel.h` + `.cpp` — listen for theme change, mark for repaint
- `Source/GUI/ScopeComponent.cpp` — same listener pattern
- `Source/GUI/BpmDisplay.h`, `DiceButton.h`, `BalanceFader.h`, `SampleSlotWidget.h` — same listener pattern
- `Source/PluginEditor.h` + `.cpp` — load persistent theme at construction, add a temporary `juce::ComboBox` theme selector (final UI ships in Plan B)
- `CMakeLists.txt` — new source files + theme JSON files in `BomboResources` + test sources

**Convention:** every `col::graphite` callsite changes to `col::graphite()`. 103 known callsites across 7 GUI files (verified by `grep -rn "col::" Source/ | wc -l`). Mechanical sweep — `sed -i 's/col::\([a-zA-Z]\+\)\b\(\s*[^(]\)/col::\1()\2/g'` is **not safe** because some `col::xxx` appearances are inside templates / structured initializers. Do the rewrite by hand per file in Task 2.

---

## Task 1: Palette struct + ThemeProvider singleton with hard-coded BANDW default

Foundation task. Project must still compile + run identically when this lands (BANDW = current values).

**Files:**
- Create: `Source/GUI/Theme/Palette.h`
- Create: `Source/GUI/Theme/ThemeProvider.h`
- Create: `Source/GUI/Theme/ThemeProvider.cpp`
- Modify: `CMakeLists.txt` — add the three new files to `target_sources(Bombo PRIVATE ...)`

- [ ] **Step 1: Write the failing test**

Create `tests/PaletteTests.cpp` with the following content (the file will be `#include`d from `RunTests.cpp` in Step 5):

```cpp
// tests/PaletteTests.cpp — registered UnitTests for theme system.
// Included from tests/RunTests.cpp so it links into Bombo_Tests.
#include "GUI/Theme/Palette.h"
#include "GUI/Theme/ThemeProvider.h"

#include <juce_core/juce_core.h>

namespace
{

class PaletteDefaultsTest : public juce::UnitTest
{
public:
    PaletteDefaultsTest() : juce::UnitTest("Palette: BANDW default values") {}

    void runTest() override
    {
        beginTest("ThemeProvider default is BANDW");
        const auto& p = bombo::ThemeProvider::current();
        expectEquals(p.graphite.getARGB(), 0xFF141517u, "graphite matches pre-refactor constant");
        expectEquals(p.bone.getARGB(),     0xFFF4F1EAu, "bone matches pre-refactor constant");
        expectEquals(p.accentAmber.getARGB(), 0xFFFFB800u, "accentAmber matches pre-refactor constant");
    }
};

class ThemeProviderListenerTest : public juce::UnitTest
{
public:
    ThemeProviderListenerTest() : juce::UnitTest("ThemeProvider: change broadcasts to listeners") {}

    struct CountingListener : public juce::ChangeListener
    {
        int count = 0;
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
    };

    void runTest() override
    {
        beginTest("setActive(<same name>) does not broadcast");
        CountingListener l;
        bombo::ThemeProvider::get().addChangeListener(&l);

        // Pre-condition: BANDW is already active. Setting to the same name is a no-op.
        bombo::ThemeProvider::get().setActive("bandw");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        expectEquals(l.count, 0, "no broadcast when theme unchanged");

        bombo::ThemeProvider::get().removeChangeListener(&l);
    }
};

static PaletteDefaultsTest        paletteDefaultsTest;
static ThemeProviderListenerTest  themeProviderListenerTest;

} // anonymous namespace
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:
```bash
cmake --build build --target Bombo_Tests 2>&1 | tail -20
```

Expected: compilation errors — `Palette.h` and `ThemeProvider.h` do not exist yet.

- [ ] **Step 3: Implement Palette struct**

Write `Source/GUI/Theme/Palette.h`:

```cpp
#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bombo
{

// POD palette mirroring every entry in the pre-refactor Source/GUI/Colours.h.
// Add new fields here ONLY when adding a new themeable surface.
struct Palette
{
    // Chassis / panel
    juce::Colour graphite;
    juce::Colour graphiteHi;
    juce::Colour ink;
    juce::Colour bone;
    juce::Colour boneDim;

    // Section column body fills
    juce::Colour voice;
    juce::Colour drive;
    juce::Colour delayC;
    juce::Colour reverb;
    juce::Colour filterC;
    juce::Colour duck;

    // Knob
    juce::Colour knobCap;
    juce::Colour knobBevel;
    juce::Colour knobRubber;

    // Accent
    juce::Colour accentAmber;
};

// Hard-coded BANDW palette = exact values from pre-refactor Colours.h.
// Used as the fallback when no JSON theme is loaded.
inline Palette bandwPalette()
{
    Palette p;
    p.graphite   = juce::Colour { 0xFF141517u };
    p.graphiteHi = juce::Colour { 0xFF1A1C1Fu };
    p.ink        = juce::Colour { 0xFF0A0B0Du };
    p.bone       = juce::Colour { 0xFFF4F1EAu };
    p.boneDim    = juce::Colour { 0xFF8A8882u };
    p.voice      = juce::Colour { 0xFF403D38u };
    p.drive      = juce::Colour { 0xFFD27845u };
    p.delayC     = juce::Colour { 0xFF3EA49Eu };
    p.reverb     = juce::Colour { 0xFF6AAE5Au };
    p.filterC    = juce::Colour { 0xFF5C8ABBu };
    p.duck       = juce::Colour { 0xFFC8A271u };
    p.knobCap    = juce::Colour { 0xFF1B1C1Eu };
    p.knobBevel  = juce::Colour { 0xFF606066u };
    p.knobRubber = juce::Colour { 0xFF151517u };
    p.accentAmber= juce::Colour { 0xFFFFB800u };
    return p;
}

} // namespace bombo
```

- [ ] **Step 4: Implement ThemeProvider**

Write `Source/GUI/Theme/ThemeProvider.h`:

```cpp
#pragma once

#include "Palette.h"

#include <juce_events/juce_events.h>

#include <string>
#include <unordered_map>

namespace bombo
{

// Singleton owning the active palette + a registry of named palettes.
// Listeners (LookAndFeels, components) repaint when the active theme changes.
class ThemeProvider : public juce::ChangeBroadcaster
{
public:
    static ThemeProvider& get();

    // Read-only accessor used by Colours.h accessor functions.
    static const Palette& current();

    // Returns the name of the currently active theme.
    const std::string& activeName() const { return activeName_; }

    // Register a named palette. Idempotent overwrite.
    void registerPalette(const std::string& name, const Palette& palette);

    // Activate a registered palette by name. No-op if name unknown or unchanged.
    // Broadcasts on actual change.
    void setActive(const std::string& name);

    // All registered theme names, in insertion order.
    const std::vector<std::string>& registeredNames() const { return order_; }

private:
    ThemeProvider();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeProvider)

    Palette active_;
    std::string activeName_;
    std::unordered_map<std::string, Palette> registry_;
    std::vector<std::string> order_;
};

} // namespace bombo
```

Write `Source/GUI/Theme/ThemeProvider.cpp`:

```cpp
#include "ThemeProvider.h"

namespace bombo
{

ThemeProvider& ThemeProvider::get()
{
    static ThemeProvider instance;
    return instance;
}

const Palette& ThemeProvider::current()
{
    return get().active_;
}

ThemeProvider::ThemeProvider()
    : active_(bandwPalette()), activeName_("bandw")
{
    registerPalette("bandw", bandwPalette());
}

void ThemeProvider::registerPalette(const std::string& name, const Palette& palette)
{
    if (registry_.find(name) == registry_.end())
        order_.push_back(name);
    registry_[name] = palette;
}

void ThemeProvider::setActive(const std::string& name)
{
    if (name == activeName_)
        return;

    auto it = registry_.find(name);
    if (it == registry_.end())
        return;

    active_     = it->second;
    activeName_ = name;
    sendChangeMessage();
}

} // namespace bombo
```

- [ ] **Step 5: Wire test file into RunTests.cpp**

Find the end of `tests/RunTests.cpp`'s anonymous namespace (just before `int main(...)`). Add immediately after the closing `}` of the existing anonymous namespace, before `main`:

```cpp
#include "PaletteTests.cpp"
```

If the file structure uses a single anonymous namespace wrapping everything, place the include inside it. Verify by reading the bottom 30 lines of `tests/RunTests.cpp` before editing.

- [ ] **Step 6: Wire new sources into CMake**

Edit `CMakeLists.txt`. Find the `target_sources(Bombo PRIVATE ...)` block (around line 47). Add immediately before the closing `)`:

```cmake
        Source/GUI/Theme/Palette.h
        Source/GUI/Theme/ThemeProvider.h
        Source/GUI/Theme/ThemeProvider.cpp
```

Find the `target_sources(Bombo_Tests PRIVATE ...)` block (around line 133). Modify it to include the new test header path:

```cmake
    target_sources(Bombo_Tests PRIVATE tests/RunTests.cpp)
    target_include_directories(Bombo_Tests PRIVATE Source tests)
```

(Note: replace the existing `target_include_directories(Bombo_Tests PRIVATE Source)` line with the version above.)

- [ ] **Step 7: Run tests to verify they pass**

Run:
```bash
cmake --build build --target Bombo_Tests && ctest --test-dir build --output-on-failure
```

Expected: all tests pass including `Palette: BANDW default values` and `ThemeProvider: change broadcasts to listeners`.

- [ ] **Step 8: Run the standalone to confirm zero visual regression**

Run:
```bash
cmake --build build --target Bombo_Standalone && bombo-launch
```

Expected: the plugin window opens looking **identical** to before this task. ThemeProvider exists in memory but nothing reads from it yet — colours are still hard-coded constants from the unmodified `Colours.h`. Close the standalone.

- [ ] **Step 9: Commit**

```bash
cd ~/repos/bombo
git add Source/GUI/Theme/Palette.h Source/GUI/Theme/ThemeProvider.h Source/GUI/Theme/ThemeProvider.cpp \
        tests/PaletteTests.cpp tests/RunTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
add Palette struct + ThemeProvider singleton scaffold

Foundation for the theme system. ThemeProvider holds a runtime-mutable
Palette and broadcasts via juce::ChangeBroadcaster. Default palette is
hard-coded BANDW (exact pre-refactor values from Colours.h) — no
visual change yet because Colours.h still owns the const constants.

Part 1 of theme-system refactor: Plan A spec at
docs/superpowers/plans/2026-05-16-bombo-theme-system.md

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Migrate Colours.h to accessor functions

Mechanical sweep. Project must still compile + look identical after this task — only the access pattern changes (`col::graphite` → `col::graphite()`).

**Files:**
- Modify: `Source/GUI/Colours.h`
- Modify: `Source/GUI/BomboLookAndFeel.h`
- Modify: `Source/GUI/FaceplatePanel.cpp` (and `.h` if it has any `col::` use)
- Modify: `Source/GUI/ScopeComponent.cpp`
- Modify: `Source/GUI/BpmDisplay.h`
- Modify: `Source/GUI/DiceButton.h`
- Modify: `Source/GUI/BalanceFader.h`
- Modify: `Source/GUI/SampleSlotWidget.h`

- [ ] **Step 1: Replace Colours.h with accessor functions**

Overwrite `Source/GUI/Colours.h` with:

```cpp
#pragma once

#include "Theme/ThemeProvider.h"

#include <juce_graphics/juce_graphics.h>

// Theme-aware colour accessors. Every value reads from ThemeProvider,
// so swapping themes at runtime swaps every paint on the next repaint.
//
// CALL CONVENTION: every call site is `col::graphite()`, NOT `col::graphite`.
// The trailing parens are mandatory — these are functions, not constants.
namespace bombo::col
{

inline juce::Colour graphite()    { return bombo::ThemeProvider::current().graphite; }
inline juce::Colour graphiteHi()  { return bombo::ThemeProvider::current().graphiteHi; }
inline juce::Colour ink()         { return bombo::ThemeProvider::current().ink; }
inline juce::Colour bone()        { return bombo::ThemeProvider::current().bone; }
inline juce::Colour boneDim()     { return bombo::ThemeProvider::current().boneDim; }

inline juce::Colour voice()       { return bombo::ThemeProvider::current().voice; }
inline juce::Colour drive()       { return bombo::ThemeProvider::current().drive; }
inline juce::Colour delayC()      { return bombo::ThemeProvider::current().delayC; }
inline juce::Colour reverb()      { return bombo::ThemeProvider::current().reverb; }
inline juce::Colour filterC()     { return bombo::ThemeProvider::current().filterC; }
inline juce::Colour duck()        { return bombo::ThemeProvider::current().duck; }

inline juce::Colour knobCap()     { return bombo::ThemeProvider::current().knobCap; }
inline juce::Colour knobBevel()   { return bombo::ThemeProvider::current().knobBevel; }
inline juce::Colour knobRubber()  { return bombo::ThemeProvider::current().knobRubber; }

inline juce::Colour accentAmber() { return bombo::ThemeProvider::current().accentAmber; }

} // namespace bombo::col
```

- [ ] **Step 2: Build to discover every callsite as a compile error**

Run:
```bash
cmake --build build --target Bombo 2>&1 | grep -E "error:|note:" | head -80
```

Expected: many errors of the form `'graphite' is not a member of 'bombo::col'` or `cannot convert ... to 'juce::Colour'`. Each error pinpoints a `col::xxx` site that needs the trailing `()`.

- [ ] **Step 3: Update each callsite file**

For each of the 7 files listed under **Files** above, change every `bombo::col::<name>` or `col::<name>` to `col::<name>()`. Examples of patterns to watch:

```cpp
// before:
g.setColour(bombo::col::graphite);
g.fillAll(col::ink);
auto c = col::voice.withAlpha(0.5f);
juce::Colour fg = col::bone;
const juce::Colour& a = col::accentAmber;

// after:
g.setColour(bombo::col::graphite());
g.fillAll(col::ink());
auto c = col::voice().withAlpha(0.5f);
juce::Colour fg = col::bone();
juce::Colour a = col::accentAmber();   // can no longer bind by reference
```

For const-reference bindings (`const juce::Colour& a = col::xxx`), drop the reference — accessors return by value. `juce::Colour` is a trivial wrapper around a `uint32_t`; pass-by-value is free.

Verify each file with:
```bash
grep -n "col::[a-zA-Z]\+\b\(\s*[^(]\|$\)" Source/GUI/<filename>
```

Any remaining matches are unconverted callsites. Should print nothing per file once done.

- [ ] **Step 4: Build clean**

Run:
```bash
cmake --build build --target Bombo 2>&1 | tail -10
```

Expected: clean build, no errors.

- [ ] **Step 5: Run the standalone to verify zero visual regression**

Run:
```bash
bombo-launch
```

Expected: plugin window opens looking **identical** to before Task 1. Every colour still flows from the hard-coded BANDW palette, just via the new accessor path. Close the standalone.

- [ ] **Step 6: Run tests**

Run:
```bash
cmake --build build --target Bombo_Tests && ctest --test-dir build --output-on-failure
```

Expected: all tests pass (the same set as Task 1 plus existing DSP tests).

- [ ] **Step 7: Commit**

```bash
cd ~/repos/bombo
git add Source/GUI/
git commit -m "$(cat <<'EOF'
migrate col:: constants to accessor functions

Mechanical sweep across 7 GUI files (103 call sites). Every
col::xxx access becomes col::xxx() — the constants are now zero-arg
functions that read from ThemeProvider::current(). Visual output
unchanged; ThemeProvider's hard-coded BANDW default matches the
pre-refactor constants byte-for-byte.

Part 2 of theme-system refactor.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: PersistentState wrapper for non-DAW user-global state

Wraps `juce::PropertiesFile` for theme-active, future unlocks, future score history. Tested independently.

**Files:**
- Create: `Source/State/PersistentState.h`
- Create: `Source/State/PersistentState.cpp`
- Modify: `tests/PaletteTests.cpp` — add a `PersistentStateTest` class at the bottom
- Modify: `CMakeLists.txt` — add the new sources to `target_sources(Bombo PRIVATE ...)`

- [ ] **Step 1: Write failing tests**

Append to `tests/PaletteTests.cpp` (inside the anonymous namespace, after the existing test classes, before the `static <name> registration` lines):

```cpp
class PersistentStateRoundTripTest : public juce::UnitTest
{
public:
    PersistentStateRoundTripTest() : juce::UnitTest("PersistentState: round-trip write/read") {}

    void runTest() override
    {
        beginTest("setActiveTheme then getActiveTheme returns same value");
        // Use a temp directory so the test doesn't pollute the real ~/.config/Bombo.
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_test_state_" + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();

        {
            bombo::PersistentState state(tmp);
            state.setActiveTheme("phosphor");
        }   // dtor flushes

        {
            bombo::PersistentState state(tmp);
            expectEquals(state.getActiveTheme(), juce::String("phosphor"),
                         "theme name persists across PersistentState instances");
        }

        tmp.deleteRecursively();
    }
};

class PersistentStateMissingFileTest : public juce::UnitTest
{
public:
    PersistentStateMissingFileTest() : juce::UnitTest("PersistentState: missing file returns default") {}

    void runTest() override
    {
        beginTest("getActiveTheme returns \"bandw\" when no state file exists");
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_test_state_missing_" + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();

        bombo::PersistentState state(tmp);
        expectEquals(state.getActiveTheme(), juce::String("bandw"),
                     "default theme is bandw when file is absent");

        tmp.deleteRecursively();
    }
};

// Then in the registration block at the bottom:
static PersistentStateRoundTripTest    persistentStateRoundTripTest;
static PersistentStateMissingFileTest  persistentStateMissingFileTest;
```

- [ ] **Step 2: Run tests to verify they fail to compile**

Run:
```bash
cmake --build build --target Bombo_Tests 2>&1 | tail -10
```

Expected: errors — `bombo::PersistentState` does not exist.

- [ ] **Step 3: Implement PersistentState**

Write `Source/State/PersistentState.h`:

```cpp
#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <memory>

namespace bombo
{

// Wraps juce::PropertiesFile for non-DAW user-global state.
// State lives at ~/.config/Bombo/state.settings (Linux), or platform
// equivalent via JUCE PropertiesFile::Options.
//
// Keys grow over time — for now only theme.active. Future:
// unlocks.themes, unlocks.presets, nightrun.last_seed, etc.
class PersistentState
{
public:
    // Default constructor: uses the platform user-app-data directory.
    PersistentState();

    // Test constructor: uses an explicit directory (e.g. tmp).
    explicit PersistentState(const juce::File& directory);

    ~PersistentState();   // flushes

    juce::String getActiveTheme() const;
    void         setActiveTheme(const juce::String& name);

private:
    std::unique_ptr<juce::PropertiesFile> props_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PersistentState)
};

} // namespace bombo
```

Write `Source/State/PersistentState.cpp`:

```cpp
#include "PersistentState.h"

namespace bombo
{

namespace
{
juce::PropertiesFile::Options makeOptions(const juce::File& directory)
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "Bombo";
    o.filenameSuffix      = ".settings";
    o.folderName          = "Bombo";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    o.commonToAllUsers    = false;
    o.doNotSave           = false;
    o.millisecondsBeforeSaving = 1000;

    if (directory != juce::File())
    {
        // Test mode: pin the directory.
        o.commonToAllUsers = false;
        o.folderName       = directory.getFileName();
    }
    return o;
}
} // anonymous namespace

PersistentState::PersistentState()
    : props_(std::make_unique<juce::PropertiesFile>(makeOptions({}))) {}

PersistentState::PersistentState(const juce::File& directory)
{
    juce::PropertiesFile::Options o = makeOptions({});
    auto file = directory.getChildFile("state.settings");
    props_ = std::make_unique<juce::PropertiesFile>(file, o);
}

PersistentState::~PersistentState()
{
    if (props_) props_->saveIfNeeded();
}

juce::String PersistentState::getActiveTheme() const
{
    return props_->getValue("theme.active", "bandw");
}

void PersistentState::setActiveTheme(const juce::String& name)
{
    props_->setValue("theme.active", name);
    props_->saveIfNeeded();
}

} // namespace bombo
```

- [ ] **Step 4: Wire into CMake**

Edit `CMakeLists.txt`. In the `target_sources(Bombo PRIVATE ...)` block, add:

```cmake
        Source/State/PersistentState.h
        Source/State/PersistentState.cpp
```

In the `target_sources(Bombo_Tests PRIVATE ...)` block (around line 133), the test file PersistentStateTests already lives inside `tests/PaletteTests.cpp` so no new source line needed — but the test exe needs access to `juce_data_structures`. Confirm by checking `target_link_libraries(Bombo_Tests ...)`; the existing entries include `juce::juce_events`, add `juce::juce_data_structures` if absent.

Also update the test exe to compile the PersistentState .cpp. The simplest path is to add it to `target_sources(Bombo_Tests PRIVATE ...)`:

```cmake
    target_sources(Bombo_Tests PRIVATE
        tests/RunTests.cpp
        Source/State/PersistentState.cpp
        Source/GUI/Theme/ThemeProvider.cpp)
```

- [ ] **Step 5: Run tests to verify they pass**

Run:
```bash
cmake --build build --target Bombo_Tests && ctest --test-dir build --output-on-failure
```

Expected: both new tests pass alongside existing tests.

- [ ] **Step 6: Commit**

```bash
cd ~/repos/bombo
git add Source/State/ tests/PaletteTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
add PersistentState wrapper for non-DAW user-global state

Wraps juce::PropertiesFile. Default ctor uses platform user-app-data;
test ctor accepts an explicit directory. Single key for now —
theme.active. Round-trip + missing-file tests included.

Future keys: unlocks.themes, unlocks.presets, nightrun.last_seed,
nightrun.score_history (Plan B and C).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Bundled theme JSON files + loader + binary data wiring

Three palettes ship in the plugin binary. Loader parses `BinaryData` into `Palette`. Tested with embedded JSON strings.

**Files:**
- Create: `Resources/Themes/bandw.json`
- Create: `Resources/Themes/phosphor.json`
- Create: `Resources/Themes/nightrun.json`
- Create: `Source/GUI/Theme/ThemeLoader.h`
- Create: `Source/GUI/Theme/ThemeLoader.cpp`
- Modify: `tests/PaletteTests.cpp` — add `ThemeLoaderTest`
- Modify: `CMakeLists.txt` — add JSON files to `BomboResources`, add ThemeLoader sources

- [ ] **Step 1: Write the three theme JSON files**

Write `Resources/Themes/bandw.json`:

```json
{
  "name": "bandw",
  "displayName": "BANDW",
  "palette": {
    "graphite":    "#FF141517",
    "graphiteHi":  "#FF1A1C1F",
    "ink":         "#FF0A0B0D",
    "bone":        "#FFF4F1EA",
    "boneDim":     "#FF8A8882",
    "voice":       "#FF403D38",
    "drive":       "#FFD27845",
    "delayC":      "#FF3EA49E",
    "reverb":      "#FF6AAE5A",
    "filterC":     "#FF5C8ABB",
    "duck":        "#FFC8A271",
    "knobCap":     "#FF1B1C1E",
    "knobBevel":   "#FF606066",
    "knobRubber":  "#FF151517",
    "accentAmber": "#FFFFB800"
  }
}
```

Write `Resources/Themes/phosphor.json` (light theme — bone background, dim graphite chrome, amber muted):

```json
{
  "name": "phosphor",
  "displayName": "PHOSPHOR",
  "palette": {
    "graphite":    "#FFEDE8DC",
    "graphiteHi":  "#FFF4F1EA",
    "ink":         "#FFD4CFC1",
    "bone":        "#FF1A1814",
    "boneDim":     "#FF4A4234",
    "voice":       "#FFC8C0AD",
    "drive":       "#FFB8602F",
    "delayC":      "#FF2E8A82",
    "reverb":      "#FF4F9242",
    "filterC":     "#FF456FA0",
    "duck":        "#FFA8855A",
    "knobCap":     "#FFE5E0D2",
    "knobBevel":   "#FF8A8270",
    "knobRubber":  "#FFEDE8DC",
    "accentAmber": "#FFCC8A00"
  }
}
```

Write `Resources/Themes/nightrun.json` (dark cyberpunk — near-black chassis, neon amber):

```json
{
  "name": "nightrun",
  "displayName": "NIGHTRUN",
  "palette": {
    "graphite":    "#FF050606",
    "graphiteHi":  "#FF0C0E10",
    "ink":         "#FF000000",
    "bone":        "#FFFFB048",
    "boneDim":     "#FF8A6020",
    "voice":       "#FF1A1612",
    "drive":       "#FFFF5520",
    "delayC":      "#FF20D0C8",
    "reverb":      "#FF40E060",
    "filterC":     "#FF4080FF",
    "duck":        "#FFFFD060",
    "knobCap":     "#FF0A0B0D",
    "knobBevel":   "#FFFFB048",
    "knobRubber":  "#FF050505",
    "accentAmber": "#FFFFD000"
  }
}
```

- [ ] **Step 2: Write the failing ThemeLoader test**

Append to `tests/PaletteTests.cpp` (inside the anonymous namespace):

```cpp
class ThemeLoaderTest : public juce::UnitTest
{
public:
    ThemeLoaderTest() : juce::UnitTest("ThemeLoader: parses JSON to Palette") {}

    void runTest() override
    {
        beginTest("valid JSON parses every palette field");
        const char* json = R"({
            "name": "test",
            "displayName": "TEST",
            "palette": {
                "graphite":    "#FF010203",
                "graphiteHi":  "#FF040506",
                "ink":         "#FF070809",
                "bone":        "#FF0A0B0C",
                "boneDim":     "#FF0D0E0F",
                "voice":       "#FF101112",
                "drive":       "#FF131415",
                "delayC":      "#FF161718",
                "reverb":      "#FF191A1B",
                "filterC":     "#FF1C1D1E",
                "duck":        "#FF1F2021",
                "knobCap":     "#FF222324",
                "knobBevel":   "#FF252627",
                "knobRubber":  "#FF28292A",
                "accentAmber": "#FF2B2C2D"
            }
        })";

        bombo::ThemeLoader::Result r = bombo::ThemeLoader::parse(json);
        expect(r.ok, "parse succeeded");
        expectEquals(r.name, std::string("test"), "name field");
        expectEquals(r.palette.graphite.getARGB(),   0xFF010203u, "graphite");
        expectEquals(r.palette.bone.getARGB(),       0xFF0A0B0Cu, "bone");
        expectEquals(r.palette.accentAmber.getARGB(),0xFF2B2C2Du, "accentAmber");

        beginTest("invalid JSON returns ok=false");
        bombo::ThemeLoader::Result bad = bombo::ThemeLoader::parse("{not json");
        expect(! bad.ok, "parse failed");

        beginTest("missing palette field returns ok=false");
        bombo::ThemeLoader::Result missing = bombo::ThemeLoader::parse("{\"name\":\"x\"}");
        expect(! missing.ok, "parse failed when palette absent");
    }
};

// Register:
static ThemeLoaderTest themeLoaderTest;
```

And at the top of `tests/PaletteTests.cpp`, add the include:

```cpp
#include "GUI/Theme/ThemeLoader.h"
```

- [ ] **Step 3: Run tests to verify they fail to compile**

Run:
```bash
cmake --build build --target Bombo_Tests 2>&1 | tail -10
```

Expected: `bombo::ThemeLoader` not found.

- [ ] **Step 4: Implement ThemeLoader**

Write `Source/GUI/Theme/ThemeLoader.h`:

```cpp
#pragma once

#include "Palette.h"

#include <juce_core/juce_core.h>

#include <string>

namespace bombo
{

class ThemeLoader
{
public:
    struct Result
    {
        bool ok = false;
        std::string name;
        std::string displayName;
        Palette palette;
        std::string error;   // populated when ok == false
    };

    // Parse a JSON string. Caller checks Result::ok.
    static Result parse(juce::StringRef json);
};

} // namespace bombo
```

Write `Source/GUI/Theme/ThemeLoader.cpp`:

```cpp
#include "ThemeLoader.h"

namespace bombo
{

namespace
{
bool parseHexColour(const juce::String& hex, juce::Colour& out)
{
    // Expect format "#AARRGGBB" (length 9).
    if (hex.length() != 9 || hex[0] != '#') return false;
    const auto n = static_cast<uint32_t>(hex.substring(1).getHexValue64());
    out = juce::Colour(n);
    return true;
}

// Field-by-field assignment with per-field validation.
// Returns false on first missing/invalid field.
bool fillPalette(const juce::var& pal, Palette& out, std::string& err)
{
    if (! pal.isObject()) { err = "palette is not an object"; return false; }

    auto take = [&](const char* key, juce::Colour& dest) -> bool
    {
        if (! pal.hasProperty(key))
        {
            err = std::string("missing field: ") + key;
            return false;
        }
        auto s = pal[key].toString();
        if (! parseHexColour(s, dest))
        {
            err = std::string("bad colour for ") + key + " = " + s.toStdString();
            return false;
        }
        return true;
    };

    return take("graphite",    out.graphite)
        && take("graphiteHi",  out.graphiteHi)
        && take("ink",         out.ink)
        && take("bone",        out.bone)
        && take("boneDim",     out.boneDim)
        && take("voice",       out.voice)
        && take("drive",       out.drive)
        && take("delayC",      out.delayC)
        && take("reverb",      out.reverb)
        && take("filterC",     out.filterC)
        && take("duck",        out.duck)
        && take("knobCap",     out.knobCap)
        && take("knobBevel",   out.knobBevel)
        && take("knobRubber",  out.knobRubber)
        && take("accentAmber", out.accentAmber);
}
} // anonymous namespace

ThemeLoader::Result ThemeLoader::parse(juce::StringRef jsonText)
{
    Result r;
    juce::var parsed;
    auto pr = juce::JSON::parse(jsonText, parsed);
    if (pr.failed())
    {
        r.error = pr.getErrorMessage().toStdString();
        return r;
    }
    if (! parsed.isObject())
    {
        r.error = "root is not an object";
        return r;
    }
    if (! parsed.hasProperty("palette"))
    {
        r.error = "missing top-level 'palette'";
        return r;
    }

    r.name        = parsed.getProperty("name", "").toString().toStdString();
    r.displayName = parsed.getProperty("displayName", "").toString().toStdString();

    if (! fillPalette(parsed["palette"], r.palette, r.error))
        return r;

    r.ok = true;
    return r;
}

} // namespace bombo
```

- [ ] **Step 5: Wire JSON files + ThemeLoader sources into CMake**

Edit `CMakeLists.txt`. Update the `juce_add_binary_data(BomboResources ...)` block (around line 43):

```cmake
juce_add_binary_data(BomboResources
    SOURCES
        Resources/Fonts/AllertaStencil-Regular.ttf
        Resources/Themes/bandw.json
        Resources/Themes/phosphor.json
        Resources/Themes/nightrun.json)
```

In the `target_sources(Bombo PRIVATE ...)` block, add:

```cmake
        Source/GUI/Theme/ThemeLoader.h
        Source/GUI/Theme/ThemeLoader.cpp
```

In the `target_sources(Bombo_Tests PRIVATE ...)` block, add the loader .cpp:

```cmake
    target_sources(Bombo_Tests PRIVATE
        tests/RunTests.cpp
        Source/State/PersistentState.cpp
        Source/GUI/Theme/ThemeProvider.cpp
        Source/GUI/Theme/ThemeLoader.cpp)
```

- [ ] **Step 6: Run tests to verify they pass**

Run:
```bash
cmake --build build --target Bombo_Tests && ctest --test-dir build --output-on-failure
```

Expected: `ThemeLoader: parses JSON to Palette` passes alongside the previous tests.

- [ ] **Step 7: Commit**

```bash
cd ~/repos/bombo
git add Resources/Themes/ Source/GUI/Theme/ThemeLoader.h Source/GUI/Theme/ThemeLoader.cpp \
        tests/PaletteTests.cpp CMakeLists.txt
git commit -m "$(cat <<'EOF'
add 3 bundled themes (BANDW, PHOSPHOR, NIGHTRUN) + JSON loader

BANDW = canonical Hyperfocus palette (matches pre-refactor Colours.h).
PHOSPHOR = light theme (bone background, dim graphite, muted amber).
NIGHTRUN = dark cyberpunk extra (near-black chassis, neon amber).

Loader parses #AARRGGBB hex strings into juce::Colour, validates every
required field, and returns a structured Result with ok/error.

JSON files baked via juce_add_binary_data; ThemeProvider does not
load them yet — wiring happens in the next task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: ThemeProvider loads bundled themes from BinaryData at startup

ThemeProvider gains a `loadBundledThemes()` method that reads the three baked JSON blobs and registers them. Called once early in plugin construction.

**Files:**
- Modify: `Source/GUI/Theme/ThemeProvider.h` — add `loadBundledThemes()`
- Modify: `Source/GUI/Theme/ThemeProvider.cpp` — implement it, using `BinaryData`
- Modify: `tests/PaletteTests.cpp` — verify the three themes register after loadBundledThemes
- Modify: `Source/PluginProcessor.cpp` (or `PluginEditor.cpp`, see Step 5) — call `loadBundledThemes()` once

- [ ] **Step 1: Write the failing test**

Append to `tests/PaletteTests.cpp`:

```cpp
class ThemeProviderBundledTest : public juce::UnitTest
{
public:
    ThemeProviderBundledTest() : juce::UnitTest("ThemeProvider: bundled themes register") {}

    void runTest() override
    {
        beginTest("after loadBundledThemes, all three names present");
        bombo::ThemeProvider::get().loadBundledThemes();
        const auto& names = bombo::ThemeProvider::get().registeredNames();

        const bool hasBandw    = std::find(names.begin(), names.end(), std::string("bandw"))    != names.end();
        const bool hasPhosphor = std::find(names.begin(), names.end(), std::string("phosphor")) != names.end();
        const bool hasNightrun = std::find(names.begin(), names.end(), std::string("nightrun")) != names.end();

        expect(hasBandw,    "bandw registered");
        expect(hasPhosphor, "phosphor registered");
        expect(hasNightrun, "nightrun registered");

        beginTest("switching to phosphor changes accentAmber");
        bombo::ThemeProvider::get().setActive("phosphor");
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        expectEquals(bombo::ThemeProvider::current().accentAmber.getARGB(), 0xFFCC8A00u,
                     "phosphor accent matches JSON");

        // Restore for other tests.
        bombo::ThemeProvider::get().setActive("bandw");
    }
};

static ThemeProviderBundledTest themeProviderBundledTest;
```

And add the include at top of file:
```cpp
#include <algorithm>
```

- [ ] **Step 2: Run tests to verify they fail**

Run:
```bash
cmake --build build --target Bombo_Tests 2>&1 | tail -10
```

Expected: `loadBundledThemes` method does not exist.

- [ ] **Step 3: Implement loadBundledThemes**

Edit `Source/GUI/Theme/ThemeProvider.h`. Add public method:

```cpp
    // Reads the three bundled theme JSONs from BinaryData and registers them.
    // Idempotent — call multiple times is safe.
    void loadBundledThemes();
```

Edit `Source/GUI/Theme/ThemeProvider.cpp`. Add the include at the top:

```cpp
#include "ThemeLoader.h"

#include <BinaryData.h>
```

Add the implementation at the bottom of the namespace:

```cpp
namespace
{
void registerFromBlob(ThemeProvider& tp,
                      const char* data, int size,
                      const char* expectedName)
{
    if (data == nullptr || size <= 0) return;
    juce::String json (data, static_cast<size_t>(size));
    auto r = ThemeLoader::parse(json);
    if (! r.ok)
    {
        DBG("ThemeLoader failed for " << expectedName << ": " << r.error);
        return;
    }
    tp.registerPalette(r.name.empty() ? expectedName : r.name, r.palette);
}
} // anonymous namespace

void ThemeProvider::loadBundledThemes()
{
    registerFromBlob(*this, BinaryData::bandw_json,    BinaryData::bandw_jsonSize,    "bandw");
    registerFromBlob(*this, BinaryData::phosphor_json, BinaryData::phosphor_jsonSize, "phosphor");
    registerFromBlob(*this, BinaryData::nightrun_json, BinaryData::nightrun_jsonSize, "nightrun");

    // After loading, refresh active_ from the registry so a JSON-loaded BANDW
    // replaces the hard-coded constructor default. No broadcast — value is identical
    // by design (BANDW JSON matches the constructor's hard-coded values).
    auto it = registry_.find(activeName_);
    if (it != registry_.end())
        active_ = it->second;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run:
```bash
cmake --build build --target Bombo_Tests && ctest --test-dir build --output-on-failure
```

Expected: `ThemeProvider: bundled themes register` passes; existing tests still pass.

- [ ] **Step 5: Call loadBundledThemes once at plugin startup**

Open `Source/PluginEditor.cpp`. Find the `BomboAudioProcessorEditor` constructor (the only ctor). Near the top of the constructor body, before any component setup, add:

```cpp
    bombo::ThemeProvider::get().loadBundledThemes();
```

And add the include at the top of `PluginEditor.cpp`:

```cpp
#include "GUI/Theme/ThemeProvider.h"
```

- [ ] **Step 6: Build + run standalone**

Run:
```bash
cmake --build build --target Bombo_Standalone && bombo-launch
```

Expected: plugin opens, visually identical to before (BANDW still active, JSON-loaded value matches the previous hard-coded value).

- [ ] **Step 7: Commit**

```bash
cd ~/repos/bombo
git add Source/GUI/Theme/ThemeProvider.h Source/GUI/Theme/ThemeProvider.cpp \
        Source/PluginEditor.cpp tests/PaletteTests.cpp
git commit -m "$(cat <<'EOF'
ThemeProvider loads bundled themes from BinaryData at startup

PluginEditor calls ThemeProvider::loadBundledThemes() once during
construction. The three bundled themes (BANDW/PHOSPHOR/NIGHTRUN) become
registered and switchable via setActive(). BANDW remains the cold-start
default; the JSON load matches its constructor defaults byte-for-byte so
no visual change yet — switcher UI lands in the next task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Wire every visual component as a ThemeProvider listener

Every component that paints palette colours needs to repaint when the theme changes. BomboLookAndFeel is the central hub; the other components listen too so that locally-cached colours (if any) refresh.

**Files:**
- Modify: `Source/GUI/BomboLookAndFeel.h`
- Modify: `Source/GUI/FaceplatePanel.h` + `.cpp`
- Modify: `Source/GUI/ScopeComponent.h` + `.cpp`
- Modify: `Source/GUI/BpmDisplay.h`
- Modify: `Source/GUI/DiceButton.h`
- Modify: `Source/GUI/BalanceFader.h`
- Modify: `Source/GUI/SampleSlotWidget.h`

- [ ] **Step 1: Add a ThemedComponent mixin (helper to reduce boilerplate)**

Create `Source/GUI/Theme/ThemedComponent.h`:

```cpp
#pragma once

#include "ThemeProvider.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace bombo
{

// Mixin: any juce::Component subclass that paints palette colours can inherit
// from this in addition to juce::Component to get auto-repaint on theme change.
//
// Usage:
//   class MyWidget : public juce::Component, public ThemedComponent { ... };
//
// Registers in ctor, unregisters in dtor, calls repaint() on broadcast.
class ThemedComponent : public juce::ChangeListener
{
public:
    ThemedComponent()  { ThemeProvider::get().addChangeListener(this); }
    ~ThemedComponent() override { ThemeProvider::get().removeChangeListener(this); }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        if (auto* self = dynamic_cast<juce::Component*>(this))
            self->repaint();
    }
};

} // namespace bombo
```

- [ ] **Step 2: Wire CMake**

Add to `target_sources(Bombo PRIVATE ...)`:
```cmake
        Source/GUI/Theme/ThemedComponent.h
```

- [ ] **Step 3: Add ThemedComponent base to each visual component**

For each of the following classes, add `, public bombo::ThemedComponent` after the existing `juce::Component` (or equivalent) base, and add `#include "Theme/ThemedComponent.h"` near the existing includes.

Files + classes:
- `Source/GUI/FaceplatePanel.h` — `class FaceplatePanel : public juce::Component, public bombo::ThemedComponent`
- `Source/GUI/ScopeComponent.h` — `class ScopeComponent : public juce::Component, public bombo::ThemedComponent`
- `Source/GUI/BpmDisplay.h` — same pattern
- `Source/GUI/DiceButton.h` — same pattern
- `Source/GUI/BalanceFader.h` — same pattern
- `Source/GUI/SampleSlotWidget.h` — same pattern

Before each edit, read the file to find the exact class declaration line. Some classes may inherit from non-`juce::Component` bases (e.g. `juce::Button`) — `ThemedComponent` still works because the `dynamic_cast<juce::Component*>` succeeds for any Component-derived type.

- [ ] **Step 4: BomboLookAndFeel becomes a listener too**

Edit `Source/GUI/BomboLookAndFeel.h`. Add include:
```cpp
#include "Theme/ThemeProvider.h"
```

Modify the class declaration to inherit from `juce::ChangeListener`:
```cpp
class BomboLookAndFeel : public juce::LookAndFeel_V4, public juce::ChangeListener
```

Add to the ctor body (find the existing ctor):
```cpp
    bombo::ThemeProvider::get().addChangeListener(this);
```

Add a dtor (or extend existing dtor) to unregister:
```cpp
    ~BomboLookAndFeel() override
    {
        bombo::ThemeProvider::get().removeChangeListener(this);
    }
```

Add the callback:
```cpp
    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        // Top-level repaint is dispatched by every ThemedComponent;
        // this hook exists so the L&F can refresh any cached resources
        // in the future (e.g. tinted images).
    }
```

- [ ] **Step 5: Build clean**

Run:
```bash
cmake --build build --target Bombo 2>&1 | tail -10
```

Expected: clean build.

- [ ] **Step 6: Manual visual verification**

Run:
```bash
bombo-launch
```

Then in a second terminal, after the plugin is open, attach a debugger or use a temporary ImGui debug button — actually for this task, we'll verify in Task 7 when the selector lands. For now just confirm the plugin still launches and looks identical to before.

- [ ] **Step 7: Run tests**

Run:
```bash
cmake --build build --target Bombo_Tests && ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 8: Commit**

```bash
cd ~/repos/bombo
git add Source/GUI/Theme/ThemedComponent.h Source/GUI/FaceplatePanel.h \
        Source/GUI/ScopeComponent.h Source/GUI/BpmDisplay.h Source/GUI/DiceButton.h \
        Source/GUI/BalanceFader.h Source/GUI/SampleSlotWidget.h \
        Source/GUI/BomboLookAndFeel.h CMakeLists.txt
git commit -m "$(cat <<'EOF'
wire every visual component as a ThemeProvider listener

Introduce ThemedComponent mixin (auto-registers in ctor, calls
repaint() on theme change). Every palette-painting widget inherits it.
BomboLookAndFeel also listens directly to be ready for future
themed resources (tinted knob textures, etc.).

No visual change yet — selector UI lands in the next task.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Temporary theme selector ComboBox in PluginEditor + persistence wiring

A throwaway `juce::ComboBox` placed somewhere visible (top-right of plugin window for testing). Will be replaced by the proper HeaderBar selector in Plan B, but the wiring (combobox change → ThemeProvider::setActive → PersistentState::setActiveTheme) carries over.

**Files:**
- Modify: `Source/PluginEditor.h` — add `juce::ComboBox themeSelector_;` member + `PersistentState` member
- Modify: `Source/PluginEditor.cpp` — populate, wire change handler, restore persisted theme

- [ ] **Step 1: Add members to PluginEditor**

Edit `Source/PluginEditor.h`. Add includes:
```cpp
#include "GUI/Theme/ThemeProvider.h"
#include "State/PersistentState.h"
#include <juce_gui_basics/juce_gui_basics.h>
```

Inside the class private section, add:
```cpp
    bombo::PersistentState persistentState_;
    juce::ComboBox themeSelector_;
```

- [ ] **Step 2: Wire ctor (populate, restore, set listener)**

Edit `Source/PluginEditor.cpp`. In the ctor, after the existing `bombo::ThemeProvider::get().loadBundledThemes();` line (added in Task 5), add:

```cpp
    // Populate selector from registered themes.
    {
        int itemId = 1;
        for (const auto& name : bombo::ThemeProvider::get().registeredNames())
            themeSelector_.addItem(juce::String(name), itemId++);
    }

    // Restore persisted theme.
    {
        const juce::String saved = persistentState_.getActiveTheme();
        bombo::ThemeProvider::get().setActive(saved.toStdString());
        themeSelector_.setText(saved, juce::dontSendNotification);
    }

    themeSelector_.onChange = [this]
    {
        const juce::String name = themeSelector_.getText();
        bombo::ThemeProvider::get().setActive(name.toStdString());
        persistentState_.setActiveTheme(name);
    };

    addAndMakeVisible(themeSelector_);
```

- [ ] **Step 3: Position the selector in resized()**

In `Source/PluginEditor.cpp`'s `resized()` method, add at the end (after the existing `faceplate.setBounds(...)`):

```cpp
    // Temporary theme selector — will be replaced by HeaderBar in Plan B.
    themeSelector_.setBounds(getWidth() - 110, 4, 100, 22);
```

- [ ] **Step 4: Build standalone**

Run:
```bash
cmake --build build --target Bombo_Standalone 2>&1 | tail -10
```

Expected: clean build.

- [ ] **Step 5: Manual visual verification — THE BIG ONE**

Run:
```bash
bombo-launch
```

Verify, in order:

1. Plugin opens with BANDW active (matches current visual identity).
2. Top-right has a ComboBox showing "bandw" with items "bandw" / "phosphor" / "nightrun".
3. Pick **phosphor** — entire plugin chrome re-skins live: chassis becomes light, knobs invert, scope inverts, FX columns recolour, amber muted. No flicker, no missed surfaces. If any element retains the old palette, that component missed the ThemedComponent inheritance (revisit Task 6).
4. Pick **nightrun** — chassis goes near-black, accents become neon amber, knob bevels gold. Confirm scope/BPM/dice all flip.
5. Pick **bandw** — back to canonical look.
6. Close the plugin window.
7. Reopen with `bombo-launch`.
8. Plugin reopens with **last-selected theme active** (PersistentState restored from `~/.config/Bombo/`).
9. ComboBox text matches the active theme.

If step 8 fails, check: `cat ~/.config/Bombo/Bombo.settings` — the file should contain `<VALUE name="theme.active" val="..."/>`.

- [ ] **Step 6: Run tests**

Run:
```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Pseudonymity check before commit**

Run:
```bash
git config user.email
```

Expected: `30924992+Hornfisk@users.noreply.github.com`. If anything else, **stop and fix** before committing (see memory `feedback_hornfisk_email_pseudonymity_hard_rule.md`).

- [ ] **Step 8: Commit**

```bash
cd ~/repos/bombo
git add Source/PluginEditor.h Source/PluginEditor.cpp
git commit -m "$(cat <<'EOF'
wire temporary theme selector + persistence in PluginEditor

ComboBox top-right of the chassis — temporary UI, will be replaced by
the HeaderBar selector in Plan B. Selecting a theme switches it live
(every ThemedComponent repaints) and persists via PersistentState. On
reopen, the last-active theme is restored before any visible paint.

End of Plan A: theme system foundation complete. Three bundled themes
(BANDW/PHOSPHOR/NIGHTRUN) functional and persistent. Plan B (dual-page
tab + HeaderBar) starts next.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Final acceptance + tag

End-of-plan checkpoint. Run everything, confirm no regressions, tag the commit so Plan B has a clear starting point.

- [ ] **Step 1: Clean build**

Run:
```bash
rm -rf build
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBOMBO_LTO=OFF
cmake --build build -j 4 2>&1 | tail -20
```

Expected: clean build, no warnings related to theme code.

- [ ] **Step 2: Run all tests**

Run:
```bash
ctest --test-dir build --output-on-failure
```

Expected: every test passes including all new theme tests:
- `Palette: BANDW default values`
- `ThemeProvider: change broadcasts to listeners`
- `PersistentState: round-trip write/read`
- `PersistentState: missing file returns default`
- `ThemeLoader: parses JSON to Palette`
- `ThemeProvider: bundled themes register`
- All pre-existing DSP tests

- [ ] **Step 3: VST3 install + DAW smoke**

Run:
```bash
cmake --build build --target Bombo_VST3
```

Verify `~/.vst3/Bombo.vst3` symlink still resolves (per `feedback_bombo_dev_loop.md` it's a symlink to `build/Bombo_artefacts/Release/VST3/Bombo.vst3`). Open Bombo in Renoise; verify:

- Plugin loads
- Switching themes live works
- Closing/reopening the plugin UI restores last theme
- DAW project save/reload does NOT affect the theme (theme is user-global, not project-local — this is the intended split per spec)

- [ ] **Step 4: Tag the milestone**

```bash
cd ~/repos/bombo
git tag -a plan-a-theme-system-complete -m "Plan A complete: theme system foundation. 3 themes live + persistent. Ready for Plan B."
```

- [ ] **Step 5: Update Bombo project memory**

Update `~/.claude/projects/-home-natalia/memory/MEMORY.md` to add a new line under the Bombo block referencing this completion. Use `memory-search` first to find the right neighbouring entries, then `memory-index` after editing.

---

## Spec self-review

**Spec coverage:**
- Dual-page tab → not in Plan A (Plan B). ✓ deferred correctly.
- Theme system → Tasks 1, 2, 4, 5, 6, 7. ✓
- 3 bundled themes (BANDW/PHOSPHOR/NIGHTRUN) → Task 4. ✓
- PersistentState → Task 3 + Task 7 wiring. ✓
- LookAndFeel migration map (7 files) → Task 6. ✓
- 420 wink → not in Plan A (Plan C, lives in status ticker). ✓ deferred.
- BPM micro-glitch → not in Plan A (Plan C). ✓ deferred.
- NIGHTRUN game → v1.1, separate brainstorm cycle. ✓ correctly out of scope.

**Placeholder scan:** every step has concrete code or commands. No TBDs/TODOs in implementation steps. One TBD-equivalent: Task 6 Step 6 says "verify in Task 7" — that's a real cross-task verification reference, not a placeholder.

**Type consistency:**
- `Palette` fields used identically across Palette.h, ThemeLoader.cpp, bandw.json/phosphor.json/nightrun.json. ✓
- `ThemeProvider::current()` returns `const Palette&` everywhere. ✓
- `PersistentState::getActiveTheme()` / `setActiveTheme()` use `juce::String` consistently. ✓
- `ThemeLoader::Result` struct fields (`ok`, `name`, `palette`, `error`) used identically in tests and implementation. ✓

---

## Execution choice

Plan complete and saved to `docs/superpowers/plans/2026-05-16-bombo-theme-system.md`. Two execution options:

**1. Subagent-Driven (recommended)** — fresh subagent per task with two-stage review between tasks. Best fidelity, least risk of carrying state errors forward.

**2. Inline Execution** — execute tasks in this session, batching with checkpoints. Faster, slightly higher state-leakage risk.

Plan B (dual-page tab + HeaderBar) and Plan C (BBS lobby content) follow this plan and depend on it. Don't start B until A is tagged complete.
