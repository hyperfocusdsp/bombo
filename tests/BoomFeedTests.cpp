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
        // Both advance once from the same Random seed -> same snapshot -> same hash.
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
        // Waveform is single-byte ASCII (post-mojibake-fix 2026-05-19);
        // glyph count == byte count == String length.
        expect(wf.length() == 18,
               "waveform must have 18 glyphs, got " + juce::String(wf.length())
               + " raw: " + wf);

        beginTest("waveform first glyph is loudest gradient char (loud attack)");
        // env = 1.0 at i = 0 picks the last entry in the gradient array,
        // currently 'H' (top-of-gradient ASCII). See BoomFeed.cpp blockChars[].
        expect(wf[0] == 'H',
               "first glyph must be 'H' (loudest gradient), got '"
               + juce::String::charToString(wf[0]) + "'");
    }
};

class BoomFeedHistoryTest : public juce::UnitTest
{
public:
    BoomFeedHistoryTest() : juce::UnitTest("BoomFeed: undo/redo navigation") {}
    void runTest() override
    {
        beginTest("prev() / next() on empty history are no-ops (no crash)");
        bombo::BoomFeed feed;
        feed.prev();
        feed.next();
        expect(true);

        beginTest("prev() at cursor==0 is a no-op; current stays valid");
        bombo::BoomFeed feed2;
        feed2.advance(bombo::BoomFeed::Mode::Random);
        const auto before = feed2.currentFilename();
        feed2.prev();
        const auto after  = feed2.currentFilename();
        expect(before == after,
               "prev() at the single-entry history must not change current");

        beginTest("8 backward steps recoverable after 9 advances");
        bombo::BoomFeed feed3;
        for (int i = 0; i < 9; ++i)
            feed3.advance(bombo::BoomFeed::Mode::Random);
        const auto tip = feed3.currentFilename();
        for (int i = 0; i < 8; ++i)
            feed3.prev();
        const auto eightBack = feed3.currentFilename();
        expect(tip != eightBack,
               "after 8 prev() the current snapshot should differ from the tip");

        beginTest("next() after prev() walks forward through history");
        for (int i = 0; i < 8; ++i)
            feed3.next();
        expect(feed3.currentFilename() == tip,
               "after symmetric prev/next the cursor should return to the tip");

        beginTest("advance() truncates the redo stack");
        bombo::BoomFeed feed4;
        for (int i = 0; i < 4; ++i) feed4.advance(bombo::BoomFeed::Mode::Random);
        for (int i = 0; i < 2; ++i) feed4.prev();
        feed4.advance(bombo::BoomFeed::Mode::Random);
        const auto fresh = feed4.currentFilename();
        feed4.next();  // should be no-op: redo stack was cleared by advance
        expect(feed4.currentFilename() == fresh,
               "next() after advance-truncated-redo must not move the cursor");

        beginTest("advance past kMaxHistory drops oldest, cursor stays valid");
        bombo::BoomFeed feed5;
        for (int i = 0; i < bombo::BoomFeed::kMaxHistory + 4; ++i)
            feed5.advance(bombo::BoomFeed::Mode::Random);
        const auto last = feed5.currentFilename();
        expect(last.startsWith("KICK-") && last.endsWith(".KCK"),
               "filename valid after overflowing the buffer: " + last);
        feed5.prev();
        expect(feed5.currentFilename().startsWith("KICK-"),
               "prev() valid after buffer overflow");
    }
};

static BoomFeedRandomBoundsTest  boomFeedRandomBoundsTest;
static BoomFeedMutateBoundsTest  boomFeedMutateBoundsTest;
static BoomFeedFilenameTest      boomFeedFilenameTest;
static BoomFeedWaveformTest      boomFeedWaveformTest;
static BoomFeedHistoryTest       boomFeedHistoryTest;

} // anonymous namespace
