// tests/GameHighScoresTests.cpp -- registered UnitTests for the HighScores
// JSON persistence + daily-seed module.
//
// Compiled as its own translation unit (see CMakeLists.txt). JUCE finds
// the tests via static UnitTest registration in the anonymous namespace.

#include "GUI/BBS/Game/HighScores.h"
#include <juce_core/juce_core.h>

namespace
{
using namespace bombo::game;

class HighScoresRoundTripTest : public juce::UnitTest
{
public:
    HighScoresRoundTripTest() : juce::UnitTest("HighScores: save then load round-trips") {}
    void runTest() override
    {
        beginTest("scores written to disk reload with same values");
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_hs_" + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();
        auto path = tmp.getChildFile("HighScores.json");

        {
            HighScores hs(path);
            hs.recordRun({ "VLD", 12345, 5, "2026-05-24", false, 0 });
            hs.recordRun({ "AAA",  9999, 3, "2026-05-24", false, 0 });
            hs.save();
        }
        {
            HighScores hs2(path);
            hs2.load();
            const auto& top = hs2.topTen();
            expectEquals((int) top.size(), 2);
            expectEquals(top[0].initials, juce::String("VLD"));
            expectEquals(top[0].score, 12345);
        }
        tmp.deleteRecursively();
    }
};

class HighScoresSortedTest : public juce::UnitTest
{
public:
    HighScoresSortedTest() : juce::UnitTest("HighScores: topTen descending by score, capped at 10") {}
    void runTest() override
    {
        beginTest("entries sort high-to-low and cap at 10");
        HighScores hs(juce::File{});   // in-memory (empty path = no disk IO)
        hs.recordRun({ "A", 100, 1, "2026-05-24", false, 0 });
        hs.recordRun({ "B", 500, 4, "2026-05-24", false, 0 });
        hs.recordRun({ "C", 250, 2, "2026-05-24", false, 0 });
        const auto& t = hs.topTen();
        expectEquals(t[0].score, 500);
        expectEquals(t[1].score, 250);
        expectEquals(t[2].score, 100);

        beginTest("only top 10 retained");
        HighScores hs2(juce::File{});
        for (int i = 0; i < 15; ++i)
            hs2.recordRun({ "X", i * 10, 1, "2026-05-24", false, 0 });
        expectEquals((int) hs2.topTen().size(), 10);
        expectEquals(hs2.topTen()[0].score, 140);   // highest of 0,10,...,140
    }
};

class HighScoresQualifyTest : public juce::UnitTest
{
public:
    HighScoresQualifyTest() : juce::UnitTest("HighScores: qualifiesForTopTen") {}
    void runTest() override
    {
        beginTest("any score qualifies until 10 entries; then must beat the lowest");
        HighScores hs(juce::File{});
        expect(hs.qualifiesForTopTen(1));   // empty board
        for (int i = 0; i < 10; ++i) hs.recordRun({ "X", 100 + i, 1, "2026-05-24", false, 0 });
        // lowest is 100; 99 doesn't qualify, 200 does
        expect(! hs.qualifiesForTopTen(99));
        expect(hs.qualifiesForTopTen(200));
    }
};

class DailySeedTodayTest : public juce::UnitTest
{
public:
    DailySeedTodayTest() : juce::UnitTest("HighScores: dailySeedToday is YYYYMMDD-shaped") {}
    void runTest() override
    {
        beginTest("today's seed is a plausible 8-digit date int");
        const auto s = dailySeedToday();
        expect(s > 20000000u && s < 30000000u);
    }
};

class CabinetLitPersistTest : public juce::UnitTest
{
public:
    CabinetLitPersistTest() : juce::UnitTest("HighScores: cabinetLit flag persists") {}
    void runTest() override
    {
        beginTest("setCabinetLit(true) survives save/load");
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_cab_" + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();
        auto path = tmp.getChildFile("HighScores.json");
        {
            HighScores hs(path);
            expect(! hs.isCabinetLit());
            hs.setCabinetLit(true);   // setCabinetLit saves internally
        }
        {
            HighScores hs2(path);
            hs2.load();
            expect(hs2.isCabinetLit());
        }
        tmp.deleteRecursively();
    }
};

static HighScoresRoundTripTest a;
static HighScoresSortedTest    b;
static HighScoresQualifyTest   c;
static DailySeedTodayTest      d;
static CabinetLitPersistTest   e;
} // anonymous namespace
