// tests/PaletteTests.cpp -- registered UnitTests for theme system.
// Compiled as its own translation unit (see CMakeLists.txt). JUCE finds
// the tests via static UnitTest registration in the anonymous namespace
// below -- the linker keeps the static instances alive across TUs.
#include "GUI/Theme/Palette.h"
#include "GUI/Theme/ThemeLoader.h"
#include "GUI/Theme/ThemeProvider.h"
#include "State/PersistentState.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>

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
        expect(p.graphite    == juce::Colour { 0xFF141517u }, "graphite matches pre-refactor constant");
        expect(p.bone        == juce::Colour { 0xFFF4F1EAu }, "bone matches pre-refactor constant");
        expect(p.accentAmber == juce::Colour { 0xFFFFB800u }, "accentAmber matches pre-refactor constant");
    }
};

class ThemeProviderListenerTest : public juce::UnitTest
{
public:
    ThemeProviderListenerTest() : juce::UnitTest("ThemeProvider: setActive early-returns do not broadcast") {}

    struct CountingListener : public juce::ChangeListener
    {
        int count = 0;
        void changeListenerCallback(juce::ChangeBroadcaster*) override { ++count; }
    };

    void runTest() override
    {
        beginTest("setActive(<current name>) is a no-op -- no broadcast dispatched");

        CountingListener l;
        bombo::ThemeProvider::get().addChangeListener(&l);

        // Pre-condition: BANDW is already active.
        bombo::ThemeProvider::get().setActive("bandw");
        // ChangeBroadcaster::dispatchPendingMessages flushes any pending
        // async update synchronously via AsyncUpdater::handleUpdateNowIfNeeded
        // -- no MessageManager dispatch loop required (which would need
        // JUCE_MODAL_LOOPS_PERMITTED, off by default). If setActive had
        // broadcasted (e.g. someone deleted the same-name early-return),
        // this delivers the callback to `l`.
        bombo::ThemeProvider::get().dispatchPendingMessages();
        expectEquals(l.count, 0, "no broadcast when active theme is reapplied");

        beginTest("setActive(<unknown name>) is a no-op -- no broadcast dispatched");
        bombo::ThemeProvider::get().setActive("totally-not-a-registered-theme");
        bombo::ThemeProvider::get().dispatchPendingMessages();
        expectEquals(l.count, 0, "no broadcast for unregistered theme name");
        expectEquals(juce::String(bombo::ThemeProvider::get().activeName()),
                     juce::String("bandw"),
                     "active theme unchanged when unknown name passed");

        bombo::ThemeProvider::get().removeChangeListener(&l);
    }
};

class PersistentStateRoundTripTest : public juce::UnitTest
{
public:
    PersistentStateRoundTripTest() : juce::UnitTest("PersistentState: round-trip write/read") {}

    void runTest() override
    {
        beginTest("setActiveTheme then getActiveTheme returns same value");
        // Use a temp directory so the test doesn't pollute the real
        // ~/.config/Bombo state.
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
        beginTest("getActiveTheme returns \"vault\" when no state file exists");
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_test_state_missing_" + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();

        bombo::PersistentState state(tmp);
        expectEquals(state.getActiveTheme(), juce::String("vault"),
                     "default theme is vault when file is absent");

        tmp.deleteRecursively();
    }
};

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
        expectEquals(juce::String(r.name), juce::String("test"), "name field");
        expect(r.palette.graphite    == juce::Colour { 0xFF010203u }, "graphite");
        expect(r.palette.bone        == juce::Colour { 0xFF0A0B0Cu }, "bone");
        expect(r.palette.accentAmber == juce::Colour { 0xFF2B2C2Du }, "accentAmber");

        beginTest("invalid JSON returns ok=false");
        bombo::ThemeLoader::Result bad = bombo::ThemeLoader::parse("{not json");
        expect(! bad.ok, "parse failed");

        beginTest("missing palette field returns ok=false");
        bombo::ThemeLoader::Result missing = bombo::ThemeLoader::parse("{\"name\":\"x\"}");
        expect(! missing.ok, "parse failed when palette absent");
    }
};

class ThemeProviderBundledTest : public juce::UnitTest
{
public:
    ThemeProviderBundledTest() : juce::UnitTest("ThemeProvider: bundled themes register") {}

    void runTest() override
    {
        beginTest("after loadBundledThemes, all four names present");
        bombo::ThemeProvider::get().loadBundledThemes();
        const auto& names = bombo::ThemeProvider::get().registeredNames();

        const bool hasBandw    = std::find(names.begin(), names.end(), std::string("bandw"))    != names.end();
        const bool hasPhosphor = std::find(names.begin(), names.end(), std::string("phosphor")) != names.end();
        const bool hasNightrun = std::find(names.begin(), names.end(), std::string("nightrun")) != names.end();
        const bool hasVault    = std::find(names.begin(), names.end(), std::string("vault"))    != names.end();

        expect(hasBandw,    "bandw registered");
        expect(hasPhosphor, "phosphor registered");
        expect(hasNightrun, "nightrun registered");
        expect(hasVault,    "vault registered");

        beginTest("switching to phosphor changes accentAmber");
        bombo::ThemeProvider::get().setActive("phosphor");
        bombo::ThemeProvider::get().dispatchPendingMessages();
        expect(bombo::ThemeProvider::current().accentAmber == juce::Colour { 0xFFCC8A00u },
               "phosphor accent matches JSON");

        beginTest("vault palette carries the Mini-Nuke chassis fields");
        bombo::ThemeProvider::get().setActive("vault");
        bombo::ThemeProvider::get().dispatchPendingMessages();
        const auto& vp = bombo::ThemeProvider::current();
        expect(vp.bodyHi     == juce::Colour { 0xFF6E8052u }, "vault bodyHi");
        expect(vp.bodyLo     == juce::Colour { 0xFF4A5638u }, "vault bodyLo");
        expect(vp.cap        == juce::Colour { 0xFF323A26u }, "vault cap");
        expect(vp.noseRed    == juce::Colour { 0xFFB43F32u }, "vault noseRed");
        expect(vp.bandYellow == juce::Colour { 0xFFE8B528u }, "vault bandYellow");

        beginTest("legacy theme without chassis fields falls back via ThemeLoader");
        // bandw.json predates Phase 2e and omits the 5 chassis fields.
        // ThemeLoader fills them from graphiteHi/graphite/ink/drive/accentAmber.
        bombo::ThemeProvider::get().setActive("bandw");
        bombo::ThemeProvider::get().dispatchPendingMessages();
        const auto& bp = bombo::ThemeProvider::current();
        expect(bp.bodyHi  == bp.graphiteHi,  "bandw bodyHi falls back to graphiteHi");
        expect(bp.bodyLo  == bp.graphite,    "bandw bodyLo falls back to graphite");
        expect(bp.cap     == bp.ink,         "bandw cap falls back to ink");
        expect(bp.noseRed == bp.drive,       "bandw noseRed falls back to drive");
        expect(bp.bandYellow == bp.accentAmber, "bandw bandYellow falls back to accentAmber");
    }
};

static PaletteDefaultsTest             paletteDefaultsTest;
static ThemeProviderListenerTest       themeProviderListenerTest;
static PersistentStateRoundTripTest    persistentStateRoundTripTest;
static PersistentStateMissingFileTest  persistentStateMissingFileTest;
static ThemeLoaderTest                 themeLoaderTest;
static ThemeProviderBundledTest        themeProviderBundledTest;

} // anonymous namespace
