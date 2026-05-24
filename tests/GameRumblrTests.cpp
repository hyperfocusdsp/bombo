// tests/GameRumblrTests.cpp
#include "GUI/BBS/Game/Entities.h"
#include <juce_core/juce_core.h>

namespace
{
using namespace bombo::game;

class RumblrPhaseThresholdsTest : public juce::UnitTest
{
public:
    RumblrPhaseThresholdsTest() : juce::UnitTest("Rumblr: phase by HP thresholds") {}
    void runTest() override
    {
        beginTest("phase 1 above 25 HP, phase 2 at <=25, phase 3 floor at <=10");
        Enemy r; r.kind = EnemyKind::Rumblr; r.hpMax = 40;
        r.hp = 40; expectEquals(rumblrPhase(r), 1);
        r.hp = 26; expectEquals(rumblrPhase(r), 1);
        r.hp = 25; expectEquals(rumblrPhase(r), 2);
        r.hp = 11; expectEquals(rumblrPhase(r), 2);
        r.hp = 10; expectEquals(rumblrPhase(r), 3);
        r.hp = 1;  expectEquals(rumblrPhase(r), 3);
    }
};

class RumblrFiresShockwavesTest : public juce::UnitTest
{
public:
    RumblrFiresShockwavesTest() : juce::UnitTest("Rumblr: phase 1 fires shockwaves every 2.5s") {}
    void runTest() override
    {
        beginTest("after ~3 cycles, several enemy shots exist");
        EnemyPool pool;
        BulletPool enemyShots;
        Enemy* r = pool.spawn(EnemyKind::Rumblr, 140, 56, 0, 0);
        expect(r != nullptr);
        r->hp = 40;   // phase 1
        const int ticks = static_cast<int>(2.5f * kTickHz);
        // Tick through 3 full shockwave cycles. Use the pool tick with the enemyShots pool.
        for (int i = 0; i < ticks * 3 + 5; ++i)
            pool.tick(/*player=*/nullptr, &enemyShots);
        int activeShots = 0;
        for (const auto& b : enemyShots.bullets()) if (b.active) ++activeShots;
        // 3 bursts x 3 projectiles = 9, minus any culled offscreen. Expect at least 3 still alive.
        expectGreaterThan(activeShots, 2);
    }
};

class RumblrPhase2ChargesTest : public juce::UnitTest
{
public:
    RumblrPhase2ChargesTest() : juce::UnitTest("Rumblr: phase 2 charges across screen") {}
    void runTest() override
    {
        beginTest("in phase 2, RUMBLR's x eventually moves left of its home position");
        EnemyPool pool;
        BulletPool enemyShots;
        Enemy* r = pool.spawn(EnemyKind::Rumblr, 140, 56, 0, 0);
        r->hp = 20;   // phase 2
        const float homeX = r->x;
        bool movedLeft = false;
        // Seed std::rand so the charge branch triggers quickly in tests.
        // Use srand(1) — with rand()%3==0 probability ~1/3, expected ~3 fire cycles before charge.
        std::srand(1);
        for (int i = 0; i < kTickHz * 12; ++i)   // up to 12s — enough for a telegraph+charge cycle
        {
            pool.tick(nullptr, &enemyShots);
            if (r->x < homeX - 20.0f) { movedLeft = true; break; }
        }
        expect(movedLeft);
    }
};

static RumblrPhaseThresholdsTest a;
static RumblrFiresShockwavesTest b;
static RumblrPhase2ChargesTest   c;
}
