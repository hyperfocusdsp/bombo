// Source/GUI/BBS/Game/Drops.cpp
#include "Drops.h"

namespace bombo::game
{
    DropTable makeTableForSource(DropSource src)
    {
        DropTable t;
        t.common = { {Pickup::Kind::DbSmall, 4.0f}, {Pickup::Kind::DbMed, 2.0f},
                     {Pickup::Kind::DbBig, 1.0f}, {Pickup::Kind::OneUp, 0.4f},
                     {Pickup::Kind::TransientBurst, 1.2f}, {Pickup::Kind::Compression, 0.8f} };
        t.uncommon = { {Pickup::Kind::EqFilter, 1.0f}, {Pickup::Kind::ChainBank, 1.0f},
                       {Pickup::Kind::DbCluster, 1.0f}, {Pickup::Kind::DoubleShot, 1.0f} };
        t.rare = { {Pickup::Kind::TimeFreeze, 1.0f}, {Pickup::Kind::SidechainPulse, 1.0f},
                   {Pickup::Kind::Mute, 1.0f}, {Pickup::Kind::PhaseLock, 1.0f} };
        t.legendary = { {Pickup::Kind::Mystery, 1.0f} };

        switch (src)
        {
            case DropSource::CommonMob:  t.dropChance = 0.06f; t.tierWeights[0]=70; t.tierWeights[1]=25; t.tierWeights[2]=5;   t.tierWeights[3]=0;  break;
            case DropSource::MidMob:     t.dropChance = 0.12f; t.tierWeights[0]=50; t.tierWeights[1]=40; t.tierWeights[2]=10;  t.tierWeights[3]=0;  break;
            case DropSource::SpecialMob: t.dropChance = 0.25f; t.tierWeights[0]=20; t.tierWeights[1]=50; t.tierWeights[2]=28;  t.tierWeights[3]=2;  break;
            case DropSource::BossPhase:  t.dropChance = 1.0f;  t.tierWeights[0]=0;  t.tierWeights[1]=0;  t.tierWeights[2]=100; t.tierWeights[3]=0;  break;
            case DropSource::BossFinal:  t.dropChance = 1.0f;  t.tierWeights[0]=0;  t.tierWeights[1]=0;  t.tierWeights[2]=70;  t.tierWeights[3]=30; break;
            case DropSource::Ambient:    t.dropChance = 1.0f;  t.tierWeights[0]=60; t.tierWeights[1]=30; t.tierWeights[2]=9;   t.tierWeights[3]=1;  break;
        }
        return t;
    }

    static Pickup::Kind pickWeighted(const std::vector<std::pair<Pickup::Kind, float>>& v, std::mt19937& rng)
    {
        float total = 0.0f;
        for (const auto& p : v) total += p.second;
        std::uniform_real_distribution<float> d(0.0f, total);
        float r = d(rng);
        for (const auto& p : v) { r -= p.second; if (r <= 0.0f) return p.first; }
        return v.front().first;
    }

    RollResult rollDrop(const DropTable& t, std::mt19937& rng)
    {
        std::uniform_real_distribution<float> dchance(0.0f, 1.0f);
        if (dchance(rng) > t.dropChance)
            return { false, DropTier::Common, Pickup::Kind::DbSmall };

        const float tw = t.tierWeights[0] + t.tierWeights[1] + t.tierWeights[2] + t.tierWeights[3];
        std::uniform_real_distribution<float> dt(0.0f, tw);
        float r = dt(rng);
        DropTier tier;
        if      (r < t.tierWeights[0]) tier = DropTier::Common;
        else if (r < t.tierWeights[0] + t.tierWeights[1]) tier = DropTier::Uncommon;
        else if (r < t.tierWeights[0] + t.tierWeights[1] + t.tierWeights[2]) tier = DropTier::Rare;
        else tier = DropTier::Legendary;

        Pickup::Kind k;
        switch (tier)
        {
            case DropTier::Common:    k = pickWeighted(t.common, rng); break;
            case DropTier::Uncommon:  k = pickWeighted(t.uncommon, rng); break;
            case DropTier::Rare:      k = pickWeighted(t.rare, rng); break;
            case DropTier::Legendary: k = pickWeighted(t.legendary, rng); break;
        }
        return { true, tier, k };
    }
}
