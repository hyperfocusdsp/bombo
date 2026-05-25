// Source/GUI/BBS/Game/Effects.cpp
#include "Effects.h"
#include <algorithm>

namespace bombo::game
{
    PickupOutcome resolvePickup(Pickup::Kind k) noexcept
    {
        PickupOutcome o;
        using K = Pickup::Kind;
        switch (k)
        {
            case K::DbSmall:        o.currencyDelta = 1;  break;
            case K::DbMed:          o.currencyDelta = 5;  break;
            case K::DbBig:          o.currencyDelta = 20; break;
            case K::OneUp:          o.lifeDelta = 1;      break;
            case K::TransientBurst: o.refillCharge = true; break;
            case K::Compression:    o.magnetiseCurrency = true; break;
            case K::EqFilter:       o.setSpreadTimer = 8.0f; break;
            case K::ChainBank:      o.chainBankAdd = 10; break;
            case K::DbCluster:      o.spawnCurrencyCluster = 5; break;
            case K::DoubleShot:     o.grantDoubleShot = true; break;
            case K::TimeFreeze:     o.setSlowMoTimer = 3.0f; break;
            case K::SidechainPulse: o.clearEnemyBullets = true; break;
            case K::Mute:           o.setMuteTimer = 4.0f; break;
            case K::PhaseLock:      o.grantPhaseLock = true; break;
            case K::Mystery:        o.grantRandomShopItem = true; break;
        }
        return o;
    }

    void EffectState::applyOutcome(const PickupOutcome& o) noexcept
    {
        if (o.setSpreadTimer > 0.0f) spread_ = o.setSpreadTimer;
        if (o.setSlowMoTimer > 0.0f) slowMo_ = o.setSlowMoTimer;
        if (o.setMuteTimer   > 0.0f) mute_   = o.setMuteTimer;
    }

    void EffectState::tick(float dt) noexcept
    {
        if (spread_ > 0.0f) spread_ = std::max(0.0f, spread_ - dt);
        if (slowMo_ > 0.0f) slowMo_ = std::max(0.0f, slowMo_ - dt);
        if (mute_   > 0.0f) mute_   = std::max(0.0f, mute_   - dt);
    }

    DropTier dropTierOf(Pickup::Kind k) noexcept
    {
        using K = Pickup::Kind;
        switch (k)
        {
            case K::DbSmall: case K::DbMed: case K::DbBig:
            case K::OneUp: case K::TransientBurst: case K::Compression:
                return DropTier::Common;
            case K::EqFilter: case K::ChainBank: case K::DbCluster: case K::DoubleShot:
                return DropTier::Uncommon;
            case K::TimeFreeze: case K::SidechainPulse: case K::Mute: case K::PhaseLock:
                return DropTier::Rare;
            case K::Mystery:
                return DropTier::Legendary;
        }
        return DropTier::Common;
    }

    bool shouldSparkle(DropTier tier, int tickCounter) noexcept
    {
        if (tier != DropTier::Rare && tier != DropTier::Legendary) return false;
        return ((tickCounter / 8) % 2) == 0;
    }

    int legendaryColorIndex(int tickCounter) noexcept
    {
        return 1 + ((tickCounter / 4) % 5);
    }
}
