// tests/GamePaletteTests.cpp
#include "GUI/BBS/Game/Palette.h"
#include <juce_core/juce_core.h>

namespace
{
class GamePaletteAllFourThemesTest : public juce::UnitTest
{
public:
    GamePaletteAllFourThemesTest() : juce::UnitTest("GamePalette: 4 distinct LUTs") {}
    void runTest() override
    {
        using namespace bombo::game;
        beginTest("each known theme maps to a unique palette");
        const auto v = getGamePalette("vault");
        const auto m = getGamePalette("matrix");
        const auto c = getGamePalette("cyber");
        const auto p = getGamePalette("plasma");

        expect(v.bg     != m.bg     && v.bg     != c.bg     && v.bg     != p.bg);
        expect(v.accent != m.accent && v.accent != c.accent && v.accent != p.accent);
        expect(m.hilite != c.hilite);
    }
};

class GamePaletteIndexLookupTest : public juce::UnitTest
{
public:
    GamePaletteIndexLookupTest() : juce::UnitTest("GamePalette: index 0..5 lookup") {}
    void runTest() override
    {
        using namespace bombo::game;
        beginTest("indices 0..5 return bg/dim/mid/accent/hot/hilite in order");
        auto m = getGamePalette("matrix");
        expectEquals((juce::int64) m.byIndex(0), (juce::int64) m.bg);
        expectEquals((juce::int64) m.byIndex(1), (juce::int64) m.dim);
        expectEquals((juce::int64) m.byIndex(2), (juce::int64) m.mid);
        expectEquals((juce::int64) m.byIndex(3), (juce::int64) m.accent);
        expectEquals((juce::int64) m.byIndex(4), (juce::int64) m.hot);
        expectEquals((juce::int64) m.byIndex(5), (juce::int64) m.hilite);
    }
};

class GamePaletteFallbackTest : public juce::UnitTest
{
public:
    GamePaletteFallbackTest() : juce::UnitTest("GamePalette: unknown theme name falls back to matrix") {}
    void runTest() override
    {
        using namespace bombo::game;
        beginTest("bandw / nightrun / empty / garbage all return the matrix palette");
        const auto m  = getGamePalette("matrix");
        const auto bw = getGamePalette("bandw");
        const auto nr = getGamePalette("nightrun");
        const auto eg = getGamePalette("");
        const auto xx = getGamePalette("not-a-theme");
        expectEquals((juce::int64) bw.bg, (juce::int64) m.bg);
        expectEquals((juce::int64) nr.bg, (juce::int64) m.bg);
        expectEquals((juce::int64) eg.bg, (juce::int64) m.bg);
        expectEquals((juce::int64) xx.bg, (juce::int64) m.bg);
    }
};

static GamePaletteAllFourThemesTest a;
static GamePaletteIndexLookupTest    b;
static GamePaletteFallbackTest       c;
}
