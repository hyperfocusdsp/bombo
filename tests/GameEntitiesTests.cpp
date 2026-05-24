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
}
