// Source/GUI/BBS/Game/Drops.h
#pragma once
#include "Entities.h"
#include <random>
#include <vector>
#include <utility>

namespace bombo::game
{
    enum class DropTier { Common, Uncommon, Rare, Legendary };
    enum class DropSource { CommonMob, MidMob, SpecialMob, BossPhase, BossFinal, Ambient };

    struct DropTable
    {
        float dropChance = 0.0f;                                   // 0..1
        std::vector<std::pair<Pickup::Kind, float>> common, uncommon, rare, legendary;
        float tierWeights[4] = { 0, 0, 0, 0 };                     // common, uncommon, rare, legendary
    };

    DropTable makeTableForSource(DropSource src);

    struct RollResult { bool dropped; DropTier tier; Pickup::Kind kind; };
    RollResult rollDrop(const DropTable& t, std::mt19937& rng);
}
