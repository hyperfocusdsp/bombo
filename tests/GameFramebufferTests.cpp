// tests/GameFramebufferTests.cpp
#include "GUI/BBS/Game/Framebuffer.h"
#include "GUI/BBS/Game/Palette.h"
#include "GUI/BBS/Game/SpriteData.h"
#include <juce_core/juce_core.h>

namespace
{
using namespace bombo::game;

static constexpr uint8_t kTinySprite[3][3] = {
    {0, 5, 0},
    {5, 3, 5},
    {0, 5, 0},
};

class FbPsetReadbackTest : public juce::UnitTest
{
public:
    FbPsetReadbackTest() : juce::UnitTest("Framebuffer: pset writes pixel at index") {}
    void runTest() override
    {
        Framebuffer fb;
        fb.clear(0);
        fb.pset(10, 20, 3);
        expectEquals((int) fb.peek(10, 20), 3);
        expectEquals((int) fb.peek(0, 0), 0);
    }
};

class FbSpriteBlitTest : public juce::UnitTest
{
public:
    FbSpriteBlitTest() : juce::UnitTest("Framebuffer: sprite blit honors zero=transparent") {}
    void runTest() override
    {
        Framebuffer fb;
        fb.clear(2);
        fb.blitSprite(&kTinySprite[0][0], 3, 3, 5, 5);
        expectEquals((int) fb.peek(5, 5), 2);   // transparent corner — keeps clear color
        expectEquals((int) fb.peek(6, 5), 5);   // top middle
        expectEquals((int) fb.peek(6, 6), 3);   // center
    }
};

class FbClipTest : public juce::UnitTest
{
public:
    FbClipTest() : juce::UnitTest("Framebuffer: out-of-bounds writes are clipped") {}
    void runTest() override
    {
        Framebuffer fb;
        fb.clear(0);
        fb.pset(-5, -5, 4);                     // must not crash
        fb.pset(kFbW + 10, kFbH + 10, 4);
        beginTest("survives out-of-bounds pset");
        expect(true);
    }
};

class FbTextTest : public juce::UnitTest
{
public:
    FbTextTest() : juce::UnitTest("Framebuffer: text draws each glyph at 4x5 stride") {}
    void runTest() override
    {
        Framebuffer fb;
        fb.clear(0);
        fb.drawText("AB", 0, 0, 5);
        // 'A' occupies x=0..3, 'B' starts at x=4 (stride=4, no gap).
        // We assert at least one pixel was written in each glyph region.
        int aHits = 0, bHits = 0;
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 4; ++x)
                if (fb.peek(x, y) == 5) ++aHits;
        for (int y = 0; y < 5; ++y)
            for (int x = 4; x < 8; ++x)
                if (fb.peek(x, y) == 5) ++bHits;
        expectGreaterThan(aHits, 0);
        expectGreaterThan(bHits, 0);
    }
};

class SpriteDataNonEmptyTest : public juce::UnitTest
{
public:
    SpriteDataNonEmptyTest() : juce::UnitTest("SpriteData: bundled tiles are non-empty") {}
    void runTest() override
    {
        using namespace bombo::game;
        beginTest("player and mudball are non-empty sprites");
        int playerHits = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 16; ++x)
                if (sprites::kPlayer[y][x] != 0) ++playerHits;
        expectGreaterThan(playerHits, 12);

        int mudHits = 0;
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 10; ++x)
                if (sprites::kMudball[y][x] != 0) ++mudHits;
        expectGreaterThan(mudHits, 12);

        beginTest("$dB token and cabinet are non-empty");
        int dbHits = 0;
        for (int y = 0; y < 6; ++y)
            for (int x = 0; x < 6; ++x)
                if (sprites::kDbSmall[y][x] != 0) ++dbHits;
        expectGreaterThan(dbHits, 10);

        int cabHits = 0;
        for (int y = 0; y < 16; ++y)
            for (int x = 0; x < 12; ++x)
                if (sprites::kCabinet[y][x] != 0) ++cabHits;
        expectGreaterThan(cabHits, 30);
    }
};

static FbPsetReadbackTest    a;
static FbSpriteBlitTest      b;
static FbClipTest            c;
static FbTextTest            d;
static SpriteDataNonEmptyTest spriteSanity;
} // namespace
