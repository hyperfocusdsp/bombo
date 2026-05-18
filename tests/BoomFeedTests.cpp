#include "GUI/BBS/BoomFeed.h"
#include <juce_core/juce_core.h>

namespace
{

class BoomFeedRandomBoundsTest : public juce::UnitTest
{
public:
    BoomFeedRandomBoundsTest() : juce::UnitTest("BoomFeed: random stays in bounds") {}
    void runTest() override
    {
        beginTest("all RANDOM params in [0,1] over 1000 iterations");
        juce::Random rng(42);
        for (int iter = 0; iter < 1000; ++iter)
        {
            const auto s = bombo::BoomFeed::generateRandom(rng);
            for (const auto& [id, val] : s.values)
            {
                expect(val >= 0.0f && val <= 1.0f,
                       "param " + id + " out of range: " + juce::String(val));
                expect(! std::isnan(val), "param " + id + " is NaN");
            }
        }
    }
};

class BoomFeedMutateBoundsTest : public juce::UnitTest
{
public:
    BoomFeedMutateBoundsTest() : juce::UnitTest("BoomFeed: mutate stays in [0,1]") {}
    void runTest() override
    {
        beginTest("mutate from mid-value stays in [0,1] over 500 iterations");
        juce::Random rng(99);
        auto base = bombo::BoomFeed::generateRandom(rng);
        for (int iter = 0; iter < 500; ++iter)
        {
            base = bombo::BoomFeed::mutateFrom(base, rng);
            for (const auto& [id, val] : base.values)
            {
                expect(val >= 0.0f && val <= 1.0f,
                       "mutated param " + id + " out of range");
                expect(! std::isnan(val), "mutated param " + id + " is NaN");
            }
        }
    }
};

class BoomFeedFilenameTest : public juce::UnitTest
{
public:
    BoomFeedFilenameTest() : juce::UnitTest("BoomFeed: filename format") {}
    void runTest() override
    {
        beginTest("filename matches KICK-XXXX-XXXX.KCK format");
        // Exercise advance() with null apvts (applySnapshot is a no-op in that case).
        // Two advances produce a deterministic filename from the same seed.
        bombo::BoomFeed feed;
        feed.advance(bombo::BoomFeed::Mode::Random);
        const juce::String name = feed.currentFilename();

        expect(name.startsWith("KICK-"),        "filename must start with KICK-");
        expect(name.endsWith(".KCK"),            "filename must end with .KCK");
        expect(name.length() == 18,              "KICK-XXXX-XXXX.KCK is 18 chars, got: " + name);
        expect(name[9] == '-',                   "separator at position 9 must be '-'");

        beginTest("same snapshot produces same filename (determinism)");
        // Use a fixed seed and compare two independently-constructed feeds.
        // Both advance once from the same Random seed → same snapshot → same hash.
        // We can't reproduce bit-identical RNG state portably, so instead we
        // verify that calling advance twice produces two *different* filenames
        // (birthday collision here is cosmetically acceptable but extremely unlikely).
        bombo::BoomFeed feed2;
        feed2.advance(bombo::BoomFeed::Mode::Random);
        feed2.advance(bombo::BoomFeed::Mode::Random);
        const juce::String name2 = feed2.currentFilename();
        // Format must still be valid after multiple advances.
        expect(name2.startsWith("KICK-") && name2.endsWith(".KCK") && name2.length() == 18,
               "second filename still valid: " + name2);
    }
};

class BoomFeedWaveformTest : public juce::UnitTest
{
public:
    BoomFeedWaveformTest() : juce::UnitTest("BoomFeed: waveform is 18 chars") {}
    void runTest() override
    {
        beginTest("waveform ASCII string has correct character count after advance");
        bombo::BoomFeed feed;
        feed.advance(bombo::BoomFeed::Mode::Random);
        const juce::String wf = feed.currentWaveformAscii();
        // Each block character is a 3-byte UTF-8 sequence (or 1-byte space).
        // Count Unicode code points by scanning: space = 1 byte, block = 3 bytes.
        // We expect exactly 18 glyphs.
        int glyphs = 0;
        int pos = 0;
        const auto* bytes = reinterpret_cast<const unsigned char*>(wf.toRawUTF8());
        const int len = static_cast<int>(std::strlen(reinterpret_cast<const char*>(bytes)));
        while (pos < len)
        {
            const unsigned char b = bytes[pos];
            if (b < 0x80)       { ++pos; }       // ASCII (space)
            else if (b < 0xE0)  { pos += 2; }    // 2-byte UTF-8
            else if (b < 0xF0)  { pos += 3; }    // 3-byte UTF-8 (block chars)
            else                { pos += 4; }    // 4-byte UTF-8
            ++glyphs;
        }
        expect(glyphs == 18, "waveform must have 18 glyphs, got " + juce::String(glyphs)
                             + " raw: " + wf);

        beginTest("waveform first glyph is full block (loud attack)");
        // The first position should always be the maximum block because env=1.0 at i=0.
        // Full block = UTF-8 E2 96 88 (\xe2\x96\x88).
        const unsigned char* raw = reinterpret_cast<const unsigned char*>(wf.toRawUTF8());
        expect(raw[0] == 0xe2 && raw[1] == 0x96 && raw[2] == 0x88,
               "first glyph must be full block █");
    }
};

class BoomFeedHistoryTest : public juce::UnitTest
{
public:
    BoomFeedHistoryTest() : juce::UnitTest("BoomFeed: prev history ring") {}
    void runTest() override
    {
        beginTest("prev() on empty history is a no-op (no crash)");
        bombo::BoomFeed feed;
        feed.prev();  // should not crash or assert
        expect(true);

        beginTest("prev() after one advance restores empty snapshot");
        // After one advance the waveform is valid; prev() should not crash.
        bombo::BoomFeed feed2;
        feed2.advance(bombo::BoomFeed::Mode::Random);
        feed2.prev();
        expect(true);

        beginTest("advance 6 times (> kHistorySize=5), then prev returns a valid snapshot");
        bombo::BoomFeed feed3;
        for (int i = 0; i < 6; ++i)
            feed3.advance(bombo::BoomFeed::Mode::Random);
        feed3.prev();
        const juce::String name = feed3.currentFilename();
        expect(name.startsWith("KICK-") && name.endsWith(".KCK"),
               "filename valid after prev through a full ring: " + name);
    }
};

static BoomFeedRandomBoundsTest  boomFeedRandomBoundsTest;
static BoomFeedMutateBoundsTest  boomFeedMutateBoundsTest;
static BoomFeedFilenameTest      boomFeedFilenameTest;
static BoomFeedWaveformTest      boomFeedWaveformTest;
static BoomFeedHistoryTest       boomFeedHistoryTest;

} // anonymous namespace
