// tests/GameShopTests.cpp
#include "GUI/BBS/Game/Shop.h"
#include <juce_core/juce_core.h>
#include <array>

namespace
{
using namespace bombo::game;

class ShopV10CatalogTest : public juce::UnitTest
{
public:
    ShopV10CatalogTest() : juce::UnitTest("Shop: v1.0.x catalog has 8 items") {}
    void runTest() override
    {
        beginTest("catalogV10 returns exactly 8 items with sane fields");
        const auto& all = catalogV10();
        expectEquals((int) all.size(), 8);
        for (const auto& it : all)
        {
            expect(it.cost > 0);
            expect(it.maxStacks >= 1);
            expect(it.shortName != nullptr && juce::String(it.shortName).isNotEmpty());
        }
    }
};

class ShopOffersThreeDistinctTest : public juce::UnitTest
{
public:
    ShopOffersThreeDistinctTest() : juce::UnitTest("Shop: a visit offers 3 distinct items") {}
    void runTest() override
    {
        beginTest("offers() returns 3 items, all different ids");
        ShopVisit v(1u);
        const auto& items = v.offers();
        expectEquals((int) items.size(), 3);
        expect(items[0].id != items[1].id);
        expect(items[1].id != items[2].id);
        expect(items[0].id != items[2].id);
    }
};

class ShopRerollDoublesCostTest : public juce::UnitTest
{
public:
    ShopRerollDoublesCostTest() : juce::UnitTest("Shop: reroll cost doubles per use") {}
    void runTest() override
    {
        beginTest("reroll cost 10 -> 20 -> 40 within a visit");
        ShopVisit v(9u);
        expectEquals(v.rerollCost(), 10);
        expect(v.reroll(/*dbAvailable=*/1000));
        expectEquals(v.rerollCost(), 20);
        expect(v.reroll(1000));
        expectEquals(v.rerollCost(), 40);
    }
};

class ShopRerollDeniedWhenBrokeTest : public juce::UnitTest
{
public:
    ShopRerollDeniedWhenBrokeTest() : juce::UnitTest("Shop: reroll denied when too poor") {}
    void runTest() override
    {
        beginTest("reroll returns false and cost unchanged when dbAvailable < cost");
        ShopVisit v(3u);
        expect(! v.reroll(/*dbAvailable=*/5));   // cost is 10
        expectEquals(v.rerollCost(), 10);
    }
};

class ShopBuyTest : public juce::UnitTest
{
public:
    ShopBuyTest() : juce::UnitTest("Shop: buy deducts dB, increments stack, respects maxStacks + affordability") {}
    void runTest() override
    {
        beginTest("buy slot 0 deducts cost and bumps the item's stack");
        ShopVisit v(7u);
        std::array<int, 15> stacks{};
        int db = 1000;
        const auto item0 = v.offers()[0];
        expect(v.buy(0, db, stacks));
        expectEquals(db, 1000 - item0.cost);
        expectEquals(stacks[(int) item0.id], 1);

        beginTest("buy denied when insufficient dB");
        int poor = 0;
        expect(! v.buy(1, poor, stacks));
        expectEquals(poor, 0);

        beginTest("buy denied past maxStacks");
        // Buy slot 0 up to its maxStacks, then one more should fail.
        int rich = 100000;
        // reset: already 1 stack of item0 bought above
        int already = stacks[(int) item0.id];
        for (int i = already; i < item0.maxStacks; ++i) expect(v.buy(0, rich, stacks));
        expect(! v.buy(0, rich, stacks));   // exceeds maxStacks
        expectEquals(stacks[(int) item0.id], item0.maxStacks);
    }
};

static ShopV10CatalogTest         a;
static ShopOffersThreeDistinctTest b;
static ShopRerollDoublesCostTest  c;
static ShopRerollDeniedWhenBrokeTest d;
static ShopBuyTest                e;
}
