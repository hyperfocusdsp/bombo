// tests/PaletteTests.cpp — registered UnitTests for theme system.
// Compiled as its own translation unit (see CMakeLists.txt). JUCE finds
// the tests via static UnitTest registration in the anonymous namespace
// below — the linker keeps the static instances alive across TUs.
#include "GUI/Theme/Palette.h"
#include "GUI/Theme/ThemeProvider.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

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
        beginTest("setActive(<current name>) is a no-op — no broadcast dispatched");

        CountingListener l;
        bombo::ThemeProvider::get().addChangeListener(&l);

        // Pre-condition: BANDW is already active.
        bombo::ThemeProvider::get().setActive("bandw");
        // ChangeBroadcaster::dispatchPendingMessages flushes any pending
        // async update synchronously via AsyncUpdater::handleUpdateNowIfNeeded
        // — no MessageManager dispatch loop required (which would need
        // JUCE_MODAL_LOOPS_PERMITTED, off by default). If setActive had
        // broadcasted (e.g. someone deleted the same-name early-return),
        // this delivers the callback to `l`.
        bombo::ThemeProvider::get().dispatchPendingMessages();
        expectEquals(l.count, 0, "no broadcast when active theme is reapplied");

        beginTest("setActive(<unknown name>) is a no-op — no broadcast dispatched");
        bombo::ThemeProvider::get().setActive("totally-not-a-registered-theme");
        bombo::ThemeProvider::get().dispatchPendingMessages();
        expectEquals(l.count, 0, "no broadcast for unregistered theme name");
        expectEquals(juce::String(bombo::ThemeProvider::get().activeName()),
                     juce::String("bandw"),
                     "active theme unchanged when unknown name passed");

        bombo::ThemeProvider::get().removeChangeListener(&l);
    }
};

static PaletteDefaultsTest        paletteDefaultsTest;
static ThemeProviderListenerTest  themeProviderListenerTest;

} // anonymous namespace
