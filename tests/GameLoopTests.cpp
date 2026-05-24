// tests/GameLoopTests.cpp
// Headless integration tests for the in-wave game loop (Task 7).
// Requires BOMBO_GAME_TEST_HOOKS (defined on the Bombo_Tests target) for the
// test-only mutable pool accessors.
#define BOMBO_GAME_TEST_HOOKS 1
#include "GUI/BBS/Game/Game.h"
#include "GUI/BBS/Game/Entities.h"
#include "GUI/BBS/Game/Constants.h"
#include <juce_core/juce_core.h>
#include <vector>

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

class GameLoopTest : public juce::UnitTest
{
public:
    GameLoopTest() : juce::UnitTest("GameLoop: in-wave integration") {}

    void runTest() override
    {
        beginTest("starting a run drip-feeds wave-1 Mudballs over time");
        {
            Game g;
            g.startNewRun(false);
            expect(g.state() == GameState::Playing);
            // Tick ~6s of frames; wave-1 roster is all Mudballs.
            for (int i = 0; i < 6 * kTickHz; ++i) g.tick();
            // Some Mudballs must have spawned (and possibly been culled), but with no
            // bullets fired at them yet a few should still be on screen at some point.
            // Re-run accumulating a max to be robust against culling.
            int maxSeen = 0;
            Game g2;
            g2.startNewRun(false);
            for (int i = 0; i < 6 * kTickHz; ++i)
            {
                g2.tick();
                maxSeen = std::max(maxSeen, countActiveEnemies(g2.enemies(), EnemyKind::Mudball));
            }
            expectGreaterThan(maxSeen, 0);
        }

        beginTest("player autofire kills enemies and raises score + chain");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();   // isolate from wave spawns
            // Place a stationary-ish Mudball directly in front of the player.
            const auto& pl = g.player();
            g.testEnemies().spawn(EnemyKind::Mudball, pl.x + 30.0f, pl.y, 0.0f, 0.0f);
            const int scoreBefore = g.score();
            // Tick enough frames for autofire bullets to traverse 30px and deal 3 HP.
            for (int i = 0; i < 4 * kTickHz; ++i) g.tick();
            expectGreaterThan(g.score(), scoreBefore);
            expectGreaterThan(g.chain().count(), 0);
        }

        beginTest("enemy shot hurts the player when not invincible");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            const auto& pl = g.player();
            expect(! pl.isInvincible());
            const int livesBefore = g.lives();
            // Spawn an enemy shot moving left, just to the right of the player so it
            // collides within a tick.
            g.testEnemyShots().spawn(pl.x + 3.0f, pl.y, -30.0f, 0.0f);
            g.tick();
            expectEquals(g.lives(), livesBefore - 1);
            expect(g.player().isInvincible());
        }

        beginTest("collecting a dB Big pickup raises currency by 20");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            const auto& pl = g.player();
            const int dbBefore = g.currencyDB();
            g.testPickups().spawn(Pickup::Kind::DbBig, pl.x + 1.0f, pl.y + 1.0f);
            g.tick();
            expectEquals(g.currencyDB(), dbBefore + 20);
        }

        beginTest("applyBulletDamage reports kill kind + position via out-param");
        {
            EnemyPool ep;
            BulletPool bp;
            auto* e = ep.spawn(EnemyKind::Clipper, 50.0f, 40.0f, 0.0f, 0.0f);
            expect(e != nullptr);
            // Clipper has 2 HP; fire two overlapping bullets to kill it.
            bp.spawn(50.0f, 40.0f, 0.0f, 0.0f, /*damage=*/2);
            std::vector<EnemyPool::KillInfo> kills;
            const int n = ep.applyBulletDamage(bp, &kills);
            expectEquals(n, 1);
            expectEquals((int) kills.size(), 1);
            expect(kills[0].kind == EnemyKind::Clipper);
            expectWithinAbsoluteError(kills[0].x, 50.0f, 0.5f);
            expectWithinAbsoluteError(kills[0].y, 40.0f, 0.5f);
        }

        beginTest("backward-compat: applyBulletDamage with no out-param still works");
        {
            EnemyPool ep;
            BulletPool bp;
            ep.spawn(EnemyKind::Mudball, 50.0f, 40.0f, 0.0f, 0.0f);
            bp.spawn(50.0f, 40.0f, 0.0f, 0.0f, /*damage=*/3);
            const int n = ep.applyBulletDamage(bp);   // default-null out-param
            expectEquals(n, 1);
        }

        beginTest("onShot seam fires on each player shot and is null-safe");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            int shots = 0;
            g.onShot = [&shots]() { ++shots; };
            for (int i = 0; i < 2 * kTickHz; ++i) g.tick();
            expectGreaterThan(shots, 0);
        }

        beginTest("tick() is a no-op outside PLAYING state");
        {
            Game g;   // starts in Title
            const int sc = g.score();
            for (int i = 0; i < 100; ++i) g.tick();
            expectEquals(g.score(), sc);
            expect(g.state() == GameState::Title);
        }
    }
};

static GameLoopTest gameLoopTest;
}
