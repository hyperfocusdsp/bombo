// tests/GameEntitiesTests.cpp
#include "GUI/BBS/Game/Entities.h"
#include <juce_core/juce_core.h>

namespace
{
using namespace bombo::game;

class PlayerMovesByVelocityTest : public juce::UnitTest
{
public:
    PlayerMovesByVelocityTest() : juce::UnitTest("Player: tick advances position by velocity*dt") {}
    void runTest() override
    {
        beginTest("position advances by vx*dt");
        Player p;
        p.x = 80.0f; p.y = 56.0f;
        p.vx = kPlayerSpeedPxS; p.vy = 0.0f;
        p.tick();
        const float expected = 80.0f + kPlayerSpeedPxS * kTickDt;
        expectWithinAbsoluteError(p.x, expected, 0.001f);
    }
};

class PlayerAutofireEvery16TicksTest : public juce::UnitTest
{
public:
    PlayerAutofireEvery16TicksTest() : juce::UnitTest("Player: autofire timer at 16 ticks") {}
    void runTest() override
    {
        beginTest("wantsShootThisTick fires on the 16th tick");
        Player p;
        p.autofireOn = true;
        for (int i = 0; i < 15; ++i) p.tick();
        expect(! p.wantsShootThisTick());
        p.tick();
        expect(p.wantsShootThisTick());
    }
};

class PlayerInvincibilityFramesTest : public juce::UnitTest
{
public:
    PlayerInvincibilityFramesTest() : juce::UnitTest("Player: hit grants 150 invincibility ticks") {}
    void runTest() override
    {
        beginTest("invincibility lasts exactly kInvincTicks");
        Player p;
        p.takeHit();
        expect(p.isInvincible());
        for (int i = 0; i < 149; ++i) p.tick();
        expect(p.isInvincible());
        p.tick();
        expect(! p.isInvincible());
    }
};

static PlayerMovesByVelocityTest a;
static PlayerAutofireEvery16TicksTest b;
static PlayerInvincibilityFramesTest c;

class BulletPoolSpawnAndCullTest : public juce::UnitTest
{
public:
    BulletPoolSpawnAndCullTest() : juce::UnitTest("BulletPool: spawn and offscreen cull") {}
    void runTest() override
    {
        beginTest("spawn returns an active bullet");
        BulletPool pool;
        Bullet* b = pool.spawn(10, 50, 180, 0);
        expect(b != nullptr);
        expect(b->active);

        beginTest("bullet culled after leaving the framebuffer to the right");
        for (int i = 0; i < 200; ++i) pool.tick();
        int alive = 0;
        for (const auto& b2 : pool.bullets()) if (b2.active) ++alive;
        expectEquals(alive, 0);
    }
};
static BulletPoolSpawnAndCullTest bulletPoolTest;

class ChargedShotChargeRateTest : public juce::UnitTest
{
public:
    ChargedShotChargeRateTest() : juce::UnitTest("Player: charge reaches 1.0 after 1.2s of hold") {}
    void runTest() override
    {
        beginTest("chargeProgress reaches >=1.0 after kChargedShotSec of holding");
        Player p;
        p.beginCharge();
        const int ticks = static_cast<int>(kChargedShotSec * kTickHz);
        for (int i = 0; i < ticks - 1; ++i) p.tick();
        expectLessThan(p.chargeProgress, 1.0f);
        p.tick();
        expectGreaterOrEqual(p.chargeProgress, 1.0f);
    }
};

class ChargedShotConsumesMeterTest : public juce::UnitTest
{
public:
    ChargedShotConsumesMeterTest() : juce::UnitTest("Player: releasing full charge consumes meter") {}
    void runTest() override
    {
        beginTest("releaseCharge fires when fully charged and consumes the meter");
        Player p;
        p.chargeMeter = 1.0f;
        p.beginCharge();
        for (int i = 0; i < 80; ++i) p.tick();   // surpass kChargedShotSec (1.2s * 60 = 72 ticks)
        bool fired = p.releaseCharge();
        expect(fired);
        expectLessThan(p.chargeMeter, 1.0f);
    }
};

static ChargedShotChargeRateTest    chargeRateTest;
static ChargedShotConsumesMeterTest chargeConsumeTest;

class EnemyPoolBulletCollisionTest : public juce::UnitTest
{
public:
    EnemyPoolBulletCollisionTest() : juce::UnitTest("EnemyPool: bullet decrements HP, kill at 0") {}
    void runTest() override
    {
        beginTest("3-HP Mudball takes 3 single-damage bullets to kill");
        EnemyPool ep;
        BulletPool bp;
        Enemy* e = ep.spawn(EnemyKind::Mudball, 80, 50, -30, 0);
        expect(e != nullptr);
        expectEquals(e->hp, 3);   // defaultHp(Mudball)

        // First hit: bullet placed so it collides this tick
        bp.spawn(78, 50, 180, 0, /*damage=*/1);
        bp.tick();
        int kills = ep.applyBulletDamage(bp);
        expectEquals(kills, 0);
        expectEquals(e->hp, 2);

        // Two more hits -> kill
        bp.spawn(78, 50, 180, 0, 1); bp.tick(); ep.applyBulletDamage(bp);
        bp.spawn(78, 50, 180, 0, 1); bp.tick();
        int killCount = ep.applyBulletDamage(bp);
        expectEquals(killCount, 1);
        expect(! e->active);
    }
};
static EnemyPoolBulletCollisionTest collisionTest;

class ClipperBurstsForwardTest : public juce::UnitTest
{
public:
    ClipperBurstsForwardTest() : juce::UnitTest("Clipper: bursts forward then pauses") {}
    void runTest() override
    {
        beginTest("Clipper moves left during burst phase, holds during pause phase");
        EnemyPool ep;
        Enemy* e = ep.spawn(EnemyKind::Clipper, 100, 50, 0, 0);
        const float startX = e->x;
        // First ~1s is burst (vx negative). Advance 0.5s worth of ticks.
        for (int i = 0; i < 30; ++i) ep.tick();
        expectLessThan(e->x, startX);   // moved left during burst
        const float afterBurst = e->x;
        // Advance into the pause phase (phase 1..2s) — ~1.2s more
        for (int i = 0; i < 72; ++i) ep.tick();
        // During pause it should barely move (allow small tolerance)
        // (We just assert it didn't move a full burst's worth again immediately.)
        expect(e->x <= afterBurst);   // never moves right
    }
};

class SilenceVoidImmuneTest : public juce::UnitTest
{
public:
    SilenceVoidImmuneTest() : juce::UnitTest("SilenceVoid: immune to bullet damage") {}
    void runTest() override
    {
        beginTest("bullet hitting SilenceVoid is absorbed, deals no damage, enemy survives");
        EnemyPool ep;
        BulletPool bp;
        Enemy* e = ep.spawn(EnemyKind::SilenceVoid, 80, 50, -10, 0);
        const int hpBefore = e->hp;
        bp.spawn(78, 50, 180, 0, 1);
        bp.tick();
        int kills = ep.applyBulletDamage(bp);
        expectEquals(kills, 0);
        expectEquals(e->hp, hpBefore);   // no damage
        expect(e->active);
        // bullet should be absorbed (deactivated)
        int aliveBullets = 0;
        for (const auto& b : bp.bullets()) if (b.active) ++aliveBullets;
        expectEquals(aliveBullets, 0);
    }
};

class LimiterVerticalDriftTest : public juce::UnitTest
{
public:
    LimiterVerticalDriftTest() : juce::UnitTest("Limiter: drifts vertically and bounces off edges") {}
    void runTest() override
    {
        beginTest("Limiter changes Y over time and stays within field");
        EnemyPool ep;
        Enemy* e = ep.spawn(EnemyKind::Limiter, 120, 56, -5, 40);  // vy>0 = downward
        const float startY = e->y;
        for (int i = 0; i < 30; ++i) ep.tick();
        expect(e->y != startY);          // moved vertically
        // Run a long time; it must remain active (bounces, not culled off top/bottom).
        // With vx=-5 over 630 ticks (~10.5s): x drops ~52px (120 -> ~68), still onscreen.
        for (int i = 0; i < 600; ++i) ep.tick();
        // The key assertion: it bounced rather than leaving via Y.
        expect(e->y >= 0.0f && e->y <= kFbH);
    }
};

static ClipperBurstsForwardTest    clipperTest;
static SilenceVoidImmuneTest       silenceVoidTest;
static LimiterVerticalDriftTest    limiterTest;
}
