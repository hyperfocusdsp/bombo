// tests/GameDropsTests.cpp
#include "GUI/BBS/Game/Drops.h"
#include "GUI/BBS/Game/Entities.h"
#include <juce_core/juce_core.h>
#include <random>

namespace
{
using namespace bombo::game;

class DropRateDistributionTest : public juce::UnitTest
{
public:
    DropRateDistributionTest() : juce::UnitTest("Drops: common-mob ~6% drop rate over 10000 rolls") {}
    void runTest() override
    {
        beginTest("common mob drop rate is roughly 6%, rares are rare");
        DropTable t = makeTableForSource(DropSource::CommonMob);
        std::mt19937 rng(42);
        int drops = 0, rares = 0;
        for (int i = 0; i < 10000; ++i)
        {
            auto r = rollDrop(t, rng);
            if (r.dropped)
            {
                ++drops;
                if (r.tier == DropTier::Rare || r.tier == DropTier::Legendary) ++rares;
            }
        }
        expectGreaterThan(drops, 400);    // ~600 expected, generous lower bound
        expectLessThan(drops, 800);
        expectLessThan(rares, drops / 8); // rares are a small fraction
    }
};

class BossPhaseGuaranteesRareTest : public juce::UnitTest
{
public:
    BossPhaseGuaranteesRareTest() : juce::UnitTest("Drops: boss phase clear always drops rare-tier") {}
    void runTest() override
    {
        beginTest("every boss-phase roll drops, and is rare or legendary");
        std::mt19937 rng(7);
        DropTable t = makeTableForSource(DropSource::BossPhase);
        for (int i = 0; i < 200; ++i)
        {
            auto r = rollDrop(t, rng);
            expect(r.dropped);
            expect(r.tier == DropTier::Rare || r.tier == DropTier::Legendary);
        }
    }
};

class AmbientAlwaysDropsTest : public juce::UnitTest
{
public:
    AmbientAlwaysDropsTest() : juce::UnitTest("Drops: ambient source always yields a drop") {}
    void runTest() override
    {
        beginTest("ambient world drops have dropChance 1.0");
        std::mt19937 rng(123);
        DropTable t = makeTableForSource(DropSource::Ambient);
        for (int i = 0; i < 100; ++i)
            expect(rollDrop(t, rng).dropped);
    }
};

class DropKindWithinTierTest : public juce::UnitTest
{
public:
    DropKindWithinTierTest() : juce::UnitTest("Drops: rolled kind belongs to its tier") {}
    void runTest() override
    {
        beginTest("a Legendary roll always yields the Mystery kind");
        std::mt19937 rng(55);
        DropTable t = makeTableForSource(DropSource::BossFinal);   // can roll legendary
        for (int i = 0; i < 500; ++i)
        {
            auto r = rollDrop(t, rng);
            if (r.dropped && r.tier == DropTier::Legendary)
                expect(r.kind == Pickup::Kind::Mystery);
        }
    }
};

static DropRateDistributionTest a;
static BossPhaseGuaranteesRareTest b;
static AmbientAlwaysDropsTest c;
static DropKindWithinTierTest d;
}
