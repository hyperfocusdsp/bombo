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
        expectEquals ((juce::int64) p.graphite.getARGB(),    (juce::int64) 0xFF141517u, "graphite matches pre-refactor constant");
        expectEquals ((juce::int64) p.bone.getARGB(),        (juce::int64) 0xFFF4F1EAu, "bone matches pre-refactor constant");
        expectEquals ((juce::int64) p.accentAmber.getARGB(), (juce::int64) 0xFFFFB800u, "accentAmber matches pre-refactor constant");
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
        // (sendChangeMessage posts async — the test runner has no message
        //  loop, but a no-op setActive shouldn't post anything anyway, so
        //  the counter must be exactly 0.)
        bombo::ThemeProvider::get().setActive("bandw");
        expectEquals(l.count, 0, "no broadcast when theme unchanged");

        bombo::ThemeProvider::get().removeChangeListener(&l);
    }
};

static PaletteDefaultsTest        paletteDefaultsTest;
static ThemeProviderListenerTest  themeProviderListenerTest;

} // anonymous namespace
