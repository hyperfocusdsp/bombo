// tests/GameDiscoveryTests.cpp
#include "GUI/BBS/Game/Discovery.h"
#include <juce_core/juce_core.h>
#include <random>

namespace
{
using namespace bombo::game;

// Advance the discovery clock by `seconds` worth of fixed-dt ticks.
static void tickFor(Discovery& d, float seconds, bool bbsVisible, float dt = kTickDt)
{
    const int n = juce::roundToInt(seconds / dt);
    for (int i = 0; i < n; ++i)
        d.tick(dt, bbsVisible);
}

class SpawnWindowTest : public juce::UnitTest
{
public:
    SpawnWindowTest() : juce::UnitTest("Discovery: invader spawns within the 60-120s window") {}
    void runTest() override
    {
        beginTest("no spawn before the 60s minimum");
        std::mt19937 rng(1234);
        Discovery d(rng);
        tickFor(d, 59.0f, /*bbsVisible=*/true);
        expect(! d.hasActiveInvader(), "invader must not appear before kInvaderMinIntervalSec");

        beginTest("spawns by the 120s maximum");
        // Run out to 120s total (another 61s). It must have spawned by now;
        // it may also have already despawned (4s on-screen), so check that the
        // powered-on event fired at least once rather than the live flag.
        bool everPoweredOn = false;
        const int extra = juce::roundToInt(61.0f / kTickDt);
        for (int i = 0; i < extra; ++i)
        {
            d.tick(kTickDt, /*bbsVisible=*/true);
            if (d.consumePoweredOnEvent()) everPoweredOn = true;
        }
        expect(everPoweredOn, "invader must have spawned by kInvaderMaxIntervalSec");
    }
};

class PoweredOnOnceTest : public juce::UnitTest
{
public:
    PoweredOnOnceTest() : juce::UnitTest("Discovery: first invader fires powered-on event exactly once") {}
    void runTest() override
    {
        beginTest("powered-on event fires once on first spawn, then never again on re-arm");
        std::mt19937 rng(99);
        Discovery d(rng);

        // Drive until the first spawn fires the powered-on event.
        bool fired = false;
        for (int i = 0; i < juce::roundToInt(125.0f / kTickDt) && ! fired; ++i)
        {
            d.tick(kTickDt, /*bbsVisible=*/true);
            fired = d.consumePoweredOnEvent();
        }
        expect(fired, "first spawn must fire the powered-on event");

        // Immediately consuming again returns false.
        expect(! d.consumePoweredOnEvent(), "powered-on event must not fire twice in a row");

        // Run through a full second spawn cycle (despawn + re-arm + spawn again)
        // and confirm the powered-on event never fires a second time.
        bool firedAgain = false;
        for (int i = 0; i < juce::roundToInt(260.0f / kTickDt); ++i)
        {
            d.tick(kTickDt, /*bbsVisible=*/true);
            if (d.consumePoweredOnEvent()) firedAgain = true;
        }
        expect(! firedAgain, "powered-on event must fire only for the very first invader ever");
    }
};

class HitTestTest : public juce::UnitTest
{
public:
    HitTestTest() : juce::UnitTest("Discovery: tryHitInvader hits within range, misses outside, despawns on hit") {}
    void runTest() override
    {
        std::mt19937 rng(7);
        Discovery d(rng);

        // Drive to a live invader.
        for (int i = 0; i < juce::roundToInt(125.0f / kTickDt) && ! d.hasActiveInvader(); ++i)
            d.tick(kTickDt, /*bbsVisible=*/true);
        expect(d.hasActiveInvader(), "should have a live invader to hit-test");

        const float ix = d.invaderX();
        const float iy = d.invaderY();

        beginTest("a click far from the invader misses and leaves it active");
        expect(! d.tryHitInvader(ix + 100.0f, iy + 100.0f), "far click must miss");
        expect(d.hasActiveInvader(), "missed click must not despawn the invader");

        beginTest("a click within range hits and despawns");
        expect(d.tryHitInvader(ix + 3.0f, iy - 2.0f), "near click must hit");
        expect(! d.hasActiveInvader(), "a hit must despawn the invader");

        beginTest("hit-test on no active invader returns false");
        expect(! d.tryHitInvader(ix, iy), "no live invader -> no hit");
    }
};

class HiddenNoSpawnTest : public juce::UnitTest
{
public:
    HiddenNoSpawnTest() : juce::UnitTest("Discovery: no spawn while BBS is hidden") {}
    void runTest() override
    {
        beginTest("ticking 200s with bbsVisible=false never spawns");
        std::mt19937 rng(2024);
        Discovery d(rng);
        bool everPoweredOn = false;
        const int n = juce::roundToInt(200.0f / kTickDt);
        for (int i = 0; i < n; ++i)
        {
            d.tick(kTickDt, /*bbsVisible=*/false);
            if (d.consumePoweredOnEvent()) everPoweredOn = true;
        }
        expect(! d.hasActiveInvader(), "no invader may spawn while BBS hidden");
        expect(! everPoweredOn, "powered-on event must not fire while BBS hidden");
    }
};

static SpawnWindowTest   sw;
static PoweredOnOnceTest po;
static HitTestTest       ht;
static HiddenNoSpawnTest hn;
}
