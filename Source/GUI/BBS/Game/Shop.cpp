// Source/GUI/BBS/Game/Shop.cpp
#include "Shop.h"
#include "Constants.h"
#include <algorithm>

namespace bombo::game
{
    static const std::vector<ShopItem> kV10 = {
        { ShopItemId::FireRate,        "FIRE RATE +1",     25, 3, "autofire +12%/stk" },
        { ShopItemId::Spread,          "SPREAD",           45, 2, "extra bullet +/-15deg" },
        { ShopItemId::TransientBuff,   "TRANSIENT BUFF",   30, 2, "+1 damage/stk" },
        { ShopItemId::SidechainShield, "SIDECHAIN SHIELD", 60, 1, "absorbs 1 hit" },
        { ShopItemId::ExtraLife,       "EXTRA LIFE",       75, 1, "+1 life (max 5)" },
        { ShopItemId::LowpassDodge,    "LOWPASS DODGE",    40, 1, "0.3s invuln on L/R" },
        { ShopItemId::ChainTimer,      "CHAIN TIMER +1s",  40, 2, "+1s chain drain/stk" },
        { ShopItemId::DbMagnet,        "dB MAGNET",        35, 1, "currency auto-pull" },
    };

    const std::vector<ShopItem>& catalogV10() { return kV10; }

    ShopVisit::ShopVisit(uint32_t seed) : rng_(seed) { rollOffers(); }

    void ShopVisit::rollOffers()
    {
        const auto& cat = catalogV10();
        std::vector<int> idx(cat.size());
        for (int i = 0; i < (int) cat.size(); ++i) idx[i] = i;
        std::shuffle(idx.begin(), idx.end(), rng_);
        offers_[0] = cat[(size_t) idx[0]];
        offers_[1] = cat[(size_t) idx[1]];
        offers_[2] = cat[(size_t) idx[2]];
    }

    int ShopVisit::rerollCost() const noexcept
    {
        return kRerollBaseCost * (1 << rerollUses_);   // 10, 20, 40, 80...
    }

    bool ShopVisit::reroll(int dbAvailable)
    {
        const int cost = rerollCost();
        if (dbAvailable < cost) return false;
        ++rerollUses_;
        rollOffers();
        return true;
    }

    bool ShopVisit::buy(int slotIdx, int& dbAvailable, std::array<int, 15>& stacks)
    {
        if (slotIdx < 0 || slotIdx > 2) return false;
        const ShopItem& it = offers_[(size_t) slotIdx];
        if (dbAvailable < it.cost) return false;
        const int stackIdx = (int) it.id;
        if (stackIdx < 0 || stackIdx >= 15) return false;
        if (stacks[(size_t) stackIdx] >= it.maxStacks) return false;
        dbAvailable -= it.cost;
        ++stacks[(size_t) stackIdx];
        return true;
    }
}
