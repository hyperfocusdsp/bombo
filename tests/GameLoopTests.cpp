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
            // Keep an out-of-range enemy alive so the cadence doesn't flip to WaveClear
            // (which would reset the chain and add the wave-clear bonus) mid-assertion.
            g.testEnemies().spawn(EnemyKind::Limiter, (float) (kFbW - 8), 8.0f, 0.0f, 0.0f);
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

        beginTest("single Mudball kill scores exactly base*mult (chain=1, mult=1.0)");
        {
            // scoreBaseFor(Mudball) == 10 (Game.cpp); chain becomes 1 on the kill,
            // chainMultiplierFor(1) == 1.0 -> exact score must be 10.
            expectWithinAbsoluteError(chainMultiplierFor(1), 1.0f, 1.0e-6f);
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            const auto& pl = g.player();
            // Place a Mudball (1 HP for autofire purposes is irrelevant here — we kill
            // it directly via a player bullet) right on top of a player bullet.
            g.testEnemies().spawn(EnemyKind::Mudball, pl.x + 30.0f, pl.y, 0.0f, 0.0f);
            // Keep an out-of-range enemy alive so the wave doesn't clear mid-assertion
            // (a clear would add the wave-clear bonus and break the exact-score check).
            g.testEnemies().spawn(EnemyKind::Limiter, (float) (kFbW - 8), 8.0f, 0.0f, 0.0f);
            const int scoreBefore = g.score();
            expectEquals(scoreBefore, 0);
            // Tick until the Mudball dies from autofire; capture the score at the kill.
            // Mudball default HP is 3; autofire damage is 1/shot, so it dies after a few
            // bullet hits. We assert the exact score the first time it becomes non-zero.
            int scoreAtKill = scoreBefore;
            for (int i = 0; i < 6 * kTickHz && scoreAtKill == scoreBefore; ++i)
            {
                g.tick();
                scoreAtKill = g.score();
            }
            expectEquals(g.chain().count(), 1);
            expectEquals(scoreAtKill, 10);   // 10 (Mudball base) * 1.0 (chain mult at count=1)
        }

        beginTest("multiple overlapping enemy shots cost only ONE life per tick");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            const auto& pl = g.player();
            expect(! pl.isInvincible());
            const int livesBefore = g.lives();
            // Three enemy shots all overlapping the player in the SAME tick.
            g.testEnemyShots().spawn(pl.x, pl.y, 0.0f, 0.0f);
            g.testEnemyShots().spawn(pl.x + 1.0f, pl.y, 0.0f, 0.0f);
            g.testEnemyShots().spawn(pl.x, pl.y + 1.0f, 0.0f, 0.0f);
            g.tick();
            // Per-tick single-life drain: lose exactly one life, not three.
            expectEquals(g.lives(), livesBefore - 1);
            expect(g.player().isInvincible());
        }

        beginTest("Aliaser kill reports exactly one parent KillInfo, minis excluded");
        {
            EnemyPool ep;
            BulletPool bp;
            auto* e = ep.spawn(EnemyKind::Aliaser, 50.0f, 40.0f, 0.0f, 0.0f);
            expect(e != nullptr);
            // Aliaser has 1 HP; a single damage-1 bullet kills it and spawns 2 minis.
            bp.spawn(50.0f, 40.0f, 0.0f, 0.0f, /*damage=*/1);
            std::vector<EnemyPool::KillInfo> kills;
            const int n = ep.applyBulletDamage(bp, &kills);
            expectEquals(n, 1);
            // Exactly ONE KillInfo, and it is the Aliaser parent — minis not reported.
            expectEquals((int) kills.size(), 1);
            expect(kills[0].kind == EnemyKind::Aliaser);
            // The two minis must now be active in the pool (split happened) ...
            expectEquals(countActiveEnemies(ep, EnemyKind::AliaserMini), 2);
            // ... but they must NOT appear in the kill report.
            for (const auto& k : kills)
                expect(k.kind != EnemyKind::AliaserMini);
        }

        beginTest("a single pickup is collected only once across multiple ticks");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            const auto& pl = g.player();
            const int dbBefore = g.currencyDB();
            // One DbBig pickup sitting on the player.
            g.testPickups().spawn(Pickup::Kind::DbBig, pl.x + 1.0f, pl.y + 1.0f);
            for (int i = 0; i < 10; ++i) g.tick();
            // DbBig == +20, collected exactly once despite many ticks (not 20*ticks).
            expectEquals(g.currencyDB(), dbBefore + 20);
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

        beginTest("enemy body contact costs one life when not invincible");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            const auto& pl = g.player();
            expect(! pl.isInvincible());
            const int livesBefore = g.lives();
            // A Mudball sitting right on the player → body contact this tick.
            // Keep an out-of-range enemy alive so the wave doesn't clear.
            g.testEnemies().spawn(EnemyKind::Mudball, pl.x, pl.y, 0.0f, 0.0f);
            g.testEnemies().spawn(EnemyKind::Limiter, (float) (kFbW - 8), 8.0f, 0.0f, 0.0f);
            g.tick();
            expectEquals(g.lives(), livesBefore - 1);
            expect(g.player().isInvincible());
        }

        beginTest("multiple overlapping enemies cost only ONE life per tick");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            const auto& pl = g.player();
            expect(! pl.isInvincible());
            const int livesBefore = g.lives();
            // Three damaging enemies all overlapping the player in the SAME tick.
            g.testEnemies().spawn(EnemyKind::Mudball, pl.x,        pl.y,        0.0f, 0.0f);
            g.testEnemies().spawn(EnemyKind::Clipper, pl.x + 1.0f, pl.y,        0.0f, 0.0f);
            g.testEnemies().spawn(EnemyKind::Limiter, pl.x,        pl.y + 1.0f, 0.0f, 0.0f);
            // Out-of-range filler so the wave can't clear mid-assertion.
            g.testEnemies().spawn(EnemyKind::Mudball, (float) (kFbW - 8), 8.0f, 0.0f, 0.0f);
            g.tick();
            // Per-tick single-life drain: lose exactly one, not three.
            expectEquals(g.lives(), livesBefore - 1);
            expect(g.player().isInvincible());
        }

        beginTest("SilenceVoid contact drains chain but does NOT cost a life");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            const auto& pl = g.player();
            const int livesBefore = g.lives();
            // Keep a persistent out-of-range enemy alive for the WHOLE test so the
            // wave never clears (a clear would reset the chain mid-build).
            g.testEnemies().spawn(EnemyKind::Limiter, (float) (kFbW - 8), 8.0f, 0.0f, 0.0f);
            // Build up a chain deterministically: each tick, drop a 1-HP Mudball
            // exactly on a fresh player bullet so the player-bullet→enemy pass in
            // resolveCombat() registers a kill (chain++). Place them off the player
            // so they don't trigger the new body-contact pass.
            for (int i = 0; i < 4; ++i)
            {
                auto* e = g.testEnemies().spawn(EnemyKind::Mudball, pl.x + 30.0f, pl.y, 0.0f, 0.0f);
                e->hp = 1;
                g.testPlayerBullets().spawn(pl.x + 30.0f, pl.y, 0.0f, 0.0f, /*damage=*/1);
                g.tick();
            }
            expectGreaterThan(g.chain().count(), 0);
            const int livesMid = g.lives();
            // Now park a SilenceVoid on the player. It is invincible to bullets and
            // must NOT cost a life — it drains the chain to 0.
            g.testEnemies().spawn(EnemyKind::SilenceVoid, pl.x, pl.y, 0.0f, 0.0f);
            g.tick();
            expectEquals(g.chain().count(), 0);     // drained
            expectEquals(g.lives(), livesMid);      // life unchanged by the void
            expect(g.lives() <= livesBefore);       // (no life gained)
        }

        beginTest("contact during invincibility does no damage");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            // Make the player invincible first (mirror the takeHit i-frame state).
            g.testPlayer().takeHit();
            expect(g.player().isInvincible());
            const int livesBefore = g.lives();
            const auto& pl = g.player();
            g.testEnemies().spawn(EnemyKind::Mudball, pl.x, pl.y, 0.0f, 0.0f);
            g.testEnemies().spawn(EnemyKind::Limiter, (float) (kFbW - 8), 8.0f, 0.0f, 0.0f);
            g.tick();
            expectEquals(g.lives(), livesBefore);   // i-frames absorbed the contact
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

// ── DoubleShot firepower upgrade (resets on life loss) ──────────────────────
class DoubleShotPersistenceTest : public juce::UnitTest
{
public:
    DoubleShotPersistenceTest()
        : juce::UnitTest("DoubleShot: doubles fire, resets on life loss") {}

    static int countActiveBullets(const BulletPool& bp)
    {
        int n = 0;
        for (const auto& b : bp.bullets())
            if (b.active) ++n;
        return n;
    }

    void runTest() override
    {
        beginTest("single shot fires one bullet");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();        // no incoming mobs to interfere
            g.testSetWeaponLevel(0);
            g.fireManualShot();
            expectEquals(countActiveBullets(g.playerBullets()), 1);
        }

        beginTest("DoubleShot fires two parallel lanes");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            g.testSetWeaponLevel(1);
            g.fireManualShot();
            expectEquals(countActiveBullets(g.playerBullets()), 2);
        }

        beginTest("weapon level resets to single shot when a life is lost");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            g.testSetWeaponLevel(1);

            // Park a mob on top of the player to force a body-contact life loss.
            const auto& pl = g.player();
            g.testEnemies().spawn(EnemyKind::Mudball, pl.x, pl.y, 0.0f, 0.0f);

            const int livesBefore = g.lives();
            g.tick();                          // resolveCombat -> body contact -> life lost

            expectEquals(g.weaponLevel(), 0);
            expectEquals(g.lives(), livesBefore - 1);

            // Firing now yields a single bullet again.
            g.testPlayerBullets() = BulletPool{};
            g.fireManualShot();
            expectEquals(countActiveBullets(g.playerBullets()), 1);
        }

        beginTest("double-shot + spread do NOT stack - capped at 3-way, not 6");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            g.testSetWeaponLevel(1);                    // DoubleShot active
            g.testGrantItem(ShopItemId::Spread, 1);     // Spread also active
            g.fireManualShot();
            // Spread wins; no doubling. 3 bullets, never 2x3 = 6.
            expectEquals(countActiveBullets(g.playerBullets()), 3);
        }

        beginTest("spread alone fires 3-way");
        {
            Game g;
            g.startNewRun(false);
            g.testClearWaveSchedule();
            g.testSetWeaponLevel(0);
            g.testGrantItem(ShopItemId::Spread, 1);
            g.fireManualShot();
            expectEquals(countActiveBullets(g.playerBullets()), 3);
        }
    }
};

static DoubleShotPersistenceTest doubleShotPersistenceTest;
}
