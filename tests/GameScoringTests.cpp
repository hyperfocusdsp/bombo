// tests/GameScoringTests.cpp
#include "GUI/BBS/Game/Game.h"
#include "GUI/BBS/Game/Entities.h"
#include "GUI/BBS/Game/Constants.h"
#include <juce_core/juce_core.h>
#include <cmath>

namespace
{
using namespace bombo::game;

class ChainMultiplierTest : public juce::UnitTest
{
public:
    ChainMultiplierTest() : juce::UnitTest("Score: chain multiplier ramps 1.0 -> 4.0") {}
    void runTest() override
    {
        beginTest("multiplier thresholds at 1/5/15/30/50");
        expectWithinAbsoluteError(chainMultiplierFor(0),  1.0f, 0.01f);
        expectWithinAbsoluteError(chainMultiplierFor(1),  1.0f, 0.01f);
        expectWithinAbsoluteError(chainMultiplierFor(5),  1.5f, 0.01f);
        expectWithinAbsoluteError(chainMultiplierFor(15), 2.0f, 0.01f);
        expectWithinAbsoluteError(chainMultiplierFor(30), 3.0f, 0.01f);
        expectWithinAbsoluteError(chainMultiplierFor(60), 4.0f, 0.01f);
    }
};

class ChainDrainTest : public juce::UnitTest
{
public:
    ChainDrainTest() : juce::UnitTest("Score: chain meter drains after 4s of no kills") {}
    void runTest() override
    {
        beginTest("chain resets to 0 after kChainDrainSec idle");
        ChainState cs;
        cs.onKill();
        expectEquals(cs.count(), 1);
        cs.tick(kChainDrainSec - 0.01f);
        expectEquals(cs.count(), 1);
        cs.tick(0.02f);
        expectEquals(cs.count(), 0);
    }
};

class ChainPeakTest : public juce::UnitTest
{
public:
    ChainPeakTest() : juce::UnitTest("Score: chain tracks peak and resets per wave") {}
    void runTest() override
    {
        beginTest("peak retains highest count; resetForWave clears");
        ChainState cs;
        for (int i = 0; i < 7; ++i) cs.onKill();
        expectEquals(cs.count(), 7);
        expectEquals(cs.peak(), 7);
        cs.tick(kChainDrainSec + 0.1f);   // drain
        expectEquals(cs.count(), 0);
        expectEquals(cs.peak(), 7);       // peak survives drain
        cs.resetForWave();
        expectEquals(cs.peak(), 0);
    }
};

class PickupPoolMagnetTest : public juce::UnitTest
{
public:
    PickupPoolMagnetTest() : juce::UnitTest("PickupPool: spawn, drift, ttl despawn") {}
    void runTest() override
    {
        beginTest("dB Magnet pulls a currency pickup toward the player");
        {
            PickupPool mp;
            auto* c = mp.spawn(Pickup::Kind::DbSmall, 80.0f, 50.0f);
            expect(c != nullptr);
            const float y0 = c->y;
            for (int i = 0; i < 10; ++i)
                mp.tick(80.0f, 10.0f, /*magnetActive=*/true);
            // Player is directly above (y=10 < pickup y=50), so pull must move pickup upward.
            expectLessThan(c->y, y0);
        }

        beginTest("magnet pull is finite when player is exactly on the pickup");
        {
            PickupPool mp2;
            auto* d = mp2.spawn(Pickup::Kind::DbBig, 40.0f, 40.0f);
            expect(d != nullptr);
            // div-by-zero guard: len <= 0.001f skips the pull — must not crash or NaN.
            for (int i = 0; i < 5; ++i)
                mp2.tick(40.0f, 40.0f, /*magnetActive=*/true);
            expect(std::isfinite(d->x) && std::isfinite(d->y));
        }

        beginTest("spawned pickup is active; despawns after ttl elapses");
        PickupPool pool;
        auto* p = pool.spawn(Pickup::Kind::DbSmall, 80.0f, 50.0f);
        expect(p != nullptr);
        expect(p->active);
        // Drift it far and exhaust TTL
        for (int i = 0; i < static_cast<int>((kDropLifetimeSec + 1.0f) * kTickHz); ++i)
            pool.tick(/*playerX=*/-999.0f, /*playerY=*/-999.0f, /*magnetActive=*/false);
        int alive = 0; for (const auto& q : pool.pickups()) if (q.active) ++alive;
        expectEquals(alive, 0);
    }
};

static ChainMultiplierTest  a;
static ChainDrainTest       b;
static ChainPeakTest        c;
static PickupPoolMagnetTest d;
}
