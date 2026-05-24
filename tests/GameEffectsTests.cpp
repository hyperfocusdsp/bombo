#include "GUI/BBS/Game/Effects.h"
#include "GUI/BBS/Game/Entities.h"
#include "GUI/BBS/Game/Drops.h"
#include <juce_core/juce_core.h>

namespace
{
using namespace bombo::game;

class ResolveCurrencyPickupsTest : public juce::UnitTest
{
public:
    ResolveCurrencyPickupsTest() : juce::UnitTest("Effects: currency pickups give dB") {}
    void runTest() override
    {
        beginTest("DbSmall/Med/Big yield 1/5/20 dB, nothing else");
        expectEquals(resolvePickup(Pickup::Kind::DbSmall).currencyDelta, 1);
        expectEquals(resolvePickup(Pickup::Kind::DbMed).currencyDelta,   5);
        expectEquals(resolvePickup(Pickup::Kind::DbBig).currencyDelta,   20);
        expect(! resolvePickup(Pickup::Kind::DbSmall).clearEnemyBullets);
    }
};

class ResolveActionPickupsTest : public juce::UnitTest
{
public:
    ResolveActionPickupsTest() : juce::UnitTest("Effects: action pickups set the right flags") {}
    void runTest() override
    {
        beginTest("each non-currency pickup maps to its documented outcome");
        expectEquals(resolvePickup(Pickup::Kind::OneUp).lifeDelta, 1);
        expect(resolvePickup(Pickup::Kind::TransientBurst).refillCharge);
        expect(resolvePickup(Pickup::Kind::Compression).magnetiseCurrency);
        expectWithinAbsoluteError(resolvePickup(Pickup::Kind::EqFilter).setSpreadTimer, 8.0f, 0.001f);
        expectEquals(resolvePickup(Pickup::Kind::ChainBank).chainBankAdd, 10);
        expectEquals(resolvePickup(Pickup::Kind::DbCluster).spawnCurrencyCluster, 5);
        expectWithinAbsoluteError(resolvePickup(Pickup::Kind::TimeFreeze).setSlowMoTimer, 3.0f, 0.001f);
        expect(resolvePickup(Pickup::Kind::SidechainPulse).clearEnemyBullets);
        expectWithinAbsoluteError(resolvePickup(Pickup::Kind::Mute).setMuteTimer, 4.0f, 0.001f);
        expect(resolvePickup(Pickup::Kind::PhaseLock).grantPhaseLock);
        expect(resolvePickup(Pickup::Kind::Mystery).grantRandomShopItem);
    }
};

class EffectStateTimersTest : public juce::UnitTest
{
public:
    EffectStateTimersTest() : juce::UnitTest("Effects: EffectState timers count down and gate") {}
    void runTest() override
    {
        beginTest("spread/slowmo/mute timers active until they elapse");
        EffectState es;
        es.applyOutcome(resolvePickup(Pickup::Kind::EqFilter));
        expect(es.spreadActive());
        es.tick(7.99f);
        expect(es.spreadActive());
        es.tick(0.02f);
        expect(! es.spreadActive());

        es.applyOutcome(resolvePickup(Pickup::Kind::TimeFreeze));
        expect(es.slowMoActive());
        es.tick(3.01f);
        expect(! es.slowMoActive());
    }
};

class DropTierOfTest : public juce::UnitTest
{
public:
    DropTierOfTest() : juce::UnitTest("Effects: dropTierOf categorises each kind") {}
    void runTest() override
    {
        beginTest("kinds map to their rarity tier");
        expect(dropTierOf(Pickup::Kind::DbSmall)    == DropTier::Common);
        expect(dropTierOf(Pickup::Kind::EqFilter)   == DropTier::Uncommon);
        expect(dropTierOf(Pickup::Kind::TimeFreeze) == DropTier::Rare);
        expect(dropTierOf(Pickup::Kind::Mystery)    == DropTier::Legendary);
    }
};

class SparkleDecisionTest : public juce::UnitTest
{
public:
    SparkleDecisionTest() : juce::UnitTest("Effects: sparkle/legendary-cycle decisions") {}
    void runTest() override
    {
        beginTest("rare sparkles on alternating 8-tick frames; common never sparkles");
        expect(! shouldSparkle(DropTier::Common, 0));
        expect(! shouldSparkle(DropTier::Uncommon, 0));
        const bool s0 = shouldSparkle(DropTier::Rare, 0);
        const bool s8 = shouldSparkle(DropTier::Rare, 8);
        expect(s0 != s8);
        beginTest("legendary colour index cycles through 1..5");
        expect(legendaryColorIndex(0)  >= 1 && legendaryColorIndex(0)  <= 5);
        expect(legendaryColorIndex(20) >= 1 && legendaryColorIndex(20) <= 5);
    }
};

static ResolveCurrencyPickupsTest a;
static ResolveActionPickupsTest   b;
static EffectStateTimersTest      c;
static DropTierOfTest             d;
static SparkleDecisionTest        e;
}
