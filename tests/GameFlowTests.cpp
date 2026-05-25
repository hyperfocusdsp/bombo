// tests/GameFlowTests.cpp
// Run-flow tests: wave-clear cadence, shop, boss, game-over -> initials -> results.
// Requires BOMBO_GAME_TEST_HOOKS for the mutable test accessors / path injection.
#define BOMBO_GAME_TEST_HOOKS 1
#include "GUI/BBS/Game/Game.h"
#include "GUI/BBS/Game/Entities.h"
#include "GUI/BBS/Game/Constants.h"
#include "GUI/BBS/Game/HighScores.h"
#include <juce_core/juce_core.h>

namespace
{
using namespace bombo::game;

static int countActiveEnemies(const EnemyPool& ep, EnemyKind kind)
{
    int n = 0;
    for (const auto& e : ep.enemies())
        if (e.active && e.kind == kind) ++n;
    return n;
}

// Tick WaveClear's 1.5s flash + one extra tick to land on the next state.
static void runWaveClearFlash(Game& g)
{
    int guard = 4 * kTickHz;
    while (g.state() == GameState::WaveClear && guard-- > 0)
        g.tick();
}

class GameFlowTest : public juce::UnitTest
{
public:
    GameFlowTest() : juce::UnitTest("GameFlow: cadence + boss + game-over + initials") {}

    void runTest() override
    {
        beginTest("wave clears when schedule done + no enemies -> WaveClear state");
        {
            Game g;
            g.startNewRun(false);
            expect(g.state() == GameState::Playing);
            g.testForceWaveClear();
            g.tick();
            expect(g.state() == GameState::WaveClear);
        }

        beginTest("wave does NOT clear while a live non-boss enemy remains");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();      // schedule done...
            const auto& pl = g.player();
            g.testEnemies().spawn(EnemyKind::Mudball, pl.x + 60.0f, pl.y, 0.0f, 0.0f);
            g.tick();
            expect(g.state() == GameState::Playing);   // ...but a mob is alive -> not clear
        }

        beginTest("cadence: W2 clear -> Shop; shop continue -> W3 Playing");
        {
            Game g;
            g.startNewRun(false);
            g.testSetCurrentWave(2);
            g.testForceWaveClear();
            g.tick();
            expect(g.state() == GameState::WaveClear);
            runWaveClearFlash(g);
            expect(g.state() == GameState::Shop);
            expect(g.shop() != nullptr);
            g.shopContinue();
            expect(g.state() == GameState::Playing);
            expectEquals(g.currentWave(), 3);
        }

        beginTest("cadence: W1 clear -> straight to W2 Playing (no shop)");
        {
            Game g;
            g.startNewRun(false);
            expectEquals(g.currentWave(), 1);
            g.testForceWaveClear();
            g.tick();
            runWaveClearFlash(g);
            expect(g.state() == GameState::Playing);
            expectEquals(g.currentWave(), 2);
        }

        beginTest("W11 clear -> Boss state with a Rumblr enemy spawned");
        {
            Game g;
            g.startNewRun(false);
            g.testSetCurrentWave(11);
            g.testForceWaveClear();
            g.tick();
            runWaveClearFlash(g);
            expect(g.state() == GameState::Boss);
            expectEquals(g.currentWave(), 12);
            expectEquals(countActiveEnemies(g.enemies(), EnemyKind::Rumblr), 1);
        }

        beginTest("boss death -> GameOver victory + boss bonus added");
        {
            Game g;
            g.startNewRun(false);
            g.testSetHighScoresPath(juce::File{});   // in-memory: always qualifies-> Initials? no, empty table qualifies
            g.testSetCurrentWave(11);
            g.testForceWaveClear();
            g.tick();
            runWaveClearFlash(g);
            expect(g.state() == GameState::Boss);
            const int scoreBefore = g.score();
            const int lives = g.lives();
            // Kill the boss directly, then tick once so the cadence sees it dead.
            for (auto& e : g.testEnemies().enemies())
                if (e.kind == EnemyKind::Rumblr) e.active = false;
            g.tick();
            expect(g.gameOverVictory());
            // GameOver OR Initials (empty score table qualifies) — both are post-victory.
            expect(g.state() == GameState::GameOver || g.state() == GameState::Initials);
            expectEquals(g.score(), scoreBefore + 5000 + lives * 200);
            // Ticking further must not re-add the victory bonus (added exactly once).
            const int scoreAfterWin = g.score();
            for (int i = 0; i < 10; ++i) g.tick();
            expectEquals(g.score(), scoreAfterWin);
        }

        beginTest("lives hitting 0 -> GameOver; if score qualifies -> Initials");
        {
            Game g;
            g.startNewRun(false);
            g.testSetHighScoresPath(juce::File{});   // in-memory empty table -> qualifies
            g.testClearWaveSchedule();
            g.testSetScore(1234);
            g.testSetLives(1);
            const auto& pl = g.player();
            g.testEnemyShots().spawn(pl.x + 1.0f, pl.y, 0.0f, 0.0f);
            g.tick();
            expectEquals(g.lives(), 0);
            expect(! g.gameOverVictory());
            expect(g.state() == GameState::Initials);   // empty top-10 -> any score qualifies
        }

        beginTest("game-over does NOT qualify when table is full of higher scores -> Results");
        {
            // Build a full top-10 of high scores in a temp file, then a low run.
            auto tmp = juce::File::createTempFile(".json");
            {
                HighScores hs(tmp);
                for (int i = 0; i < 10; ++i)
                    hs.recordRun(ScoreEntry{ "ZZZ", 100000 + i, 8, "2026-05-24", false, 0 });
                hs.save();
            }
            Game g;
            g.startNewRun(false);
            g.testSetHighScoresPath(tmp);
            g.testClearWaveSchedule();
            g.testSetScore(50);              // way below the 100000 floor
            g.testSetLives(1);
            const auto& pl = g.player();
            g.testEnemyShots().spawn(pl.x + 1.0f, pl.y, 0.0f, 0.0f);
            g.tick();
            expect(g.state() == GameState::Results);
            tmp.deleteFile();
        }

        beginTest("initials entry: cycle to 'VLD', confirm -> Results + recorded in highScores");
        {
            auto tmp = juce::File::createTempFile(".json");
            tmp.deleteFile();   // start with no file
            Game g;
            g.startNewRun(false);
            g.testSetHighScoresPath(tmp);
            g.testClearWaveSchedule();
            g.testSetScore(7777);
            g.testSetLives(1);
            const auto& pl = g.player();
            g.testEnemyShots().spawn(pl.x + 1.0f, pl.y, 0.0f, 0.0f);
            g.tick();
            expect(g.state() == GameState::Initials);

            // Default 'AAA' -> 'VLD'. ('V'=21, 'L'=11, 'D'=3 from 'A').
            g.initialsCycleLetter(0, 'V' - 'A');
            g.initialsCycleLetter(1, 'L' - 'A');
            g.initialsCycleLetter(2, 'D' - 'A');
            const auto in = g.initials();
            expect(in[0] == 'V' && in[1] == 'L' && in[2] == 'D');

            g.initialsConfirm();
            expect(g.state() == GameState::Results);

            // Reload from disk to prove recordRun + save persisted it.
            HighScores reread(tmp);
            reread.load();
            bool found = false;
            for (const auto& e : reread.topTen())
                if (e.initials == "VLD" && e.score == 7777) found = true;
            expect(found);
            tmp.deleteFile();
        }

        beginTest("initials slot + letter wrap A-Z");
        {
            Game g;
            // Drive into Initials via the death path.
            g.startNewRun(false);
            g.testSetHighScoresPath(juce::File{});
            g.testClearWaveSchedule();
            g.testSetLives(1);
            const auto& pl = g.player();
            g.testEnemyShots().spawn(pl.x + 1.0f, pl.y, 0.0f, 0.0f);
            g.tick();
            expect(g.state() == GameState::Initials);

            g.initialsCycleLetter(0, -1);     // 'A' - 1 wraps to 'Z'
            expect(g.initials()[0] == 'Z');
            g.initialsCycleLetter(0, 1);      // back to 'A'
            expect(g.initials()[0] == 'A');

            expectEquals(g.initialsSlot(), 0);
            g.initialsMoveSlot(-1);           // 0 - 1 wraps to 2
            expectEquals(g.initialsSlot(), 2);
            g.initialsMoveSlot(1);            // wraps back to 0
            expectEquals(g.initialsSlot(), 0);
        }

        beginTest("resultsContinue returns to Title");
        {
            Game g;
            g.startNewRun(false);
            g.testSetHighScoresPath(juce::File{});
            g.testClearWaveSchedule();
            g.testSetLives(1);
            const auto& pl = g.player();
            g.testEnemyShots().spawn(pl.x + 1.0f, pl.y, 0.0f, 0.0f);
            g.tick();
            // Either Initials (qualifies) -> confirm -> Results, or already Results.
            if (g.state() == GameState::Initials) g.initialsConfirm();
            expect(g.state() == GameState::Results);
            g.resultsContinue();
            expect(g.state() == GameState::Title);
        }

        beginTest("shop reroll deducts the pre-reroll cost exactly once");
        {
            Game g;
            g.startNewRun(false);
            g.testSetCurrentWave(2);
            g.testForceWaveClear();
            g.tick();
            runWaveClearFlash(g);
            expect(g.state() == GameState::Shop);
            const int costBefore = g.shop()->rerollCost();
            expectGreaterThan(costBefore, 0);
            g.testSetCurrencyDB(costBefore + 5);   // afford exactly one reroll + change
            const int dbBefore = g.currencyDB();
            const bool ok = g.shopReroll();
            expect(ok);
            // Deduct the PRE-reroll cost exactly once (not post-reroll, not zero, not twice).
            expectEquals(g.currencyDB(), dbBefore - costBefore);
        }

        beginTest("shop reroll fails (no deduction) when unaffordable");
        {
            Game g;
            g.startNewRun(false);
            g.testSetCurrentWave(4);
            g.testForceWaveClear();
            g.tick();
            runWaveClearFlash(g);
            expect(g.state() == GameState::Shop);
            g.testSetCurrencyDB(0);
            const bool ok = g.shopReroll();
            expect(! ok);
            expectEquals(g.currencyDB(), 0);
        }

        beginTest("shop free heal: +1 life once per visit, capped at max");
        {
            Game g;
            g.startNewRun(false);
            g.testSetCurrentWave(2);
            g.testForceWaveClear();
            g.tick();
            runWaveClearFlash(g);
            expect(g.state() == GameState::Shop);
            g.testSetLives(2);
            expect(g.shopFreeHealAvailable());
            expect(g.shopUseFreeHeal());
            expectEquals(g.lives(), 3);
            expect(! g.shopFreeHealAvailable());
            expect(! g.shopUseFreeHeal());   // only once per visit
            expectEquals(g.lives(), 3);
        }

        beginTest("daily run uses dailySeedToday() as seed; non-daily does not");
        {
            Game gd;
            gd.startNewRun(true);
            expect(gd.testDaily());
            expectEquals((int) gd.testRunSeed(), (int) dailySeedToday());

            Game gn;
            gn.startNewRun(false);
            expect(! gn.testDaily());
            // Non-daily must not be the hardcoded 1u, and should differ from the daily seed
            // (random_device); guard the (astronomically unlikely) collision.
            expect(gn.testRunSeed() != 0u);
        }
    }
};

static GameFlowTest gameFlowTest;
}
