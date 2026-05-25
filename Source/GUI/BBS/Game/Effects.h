// Source/GUI/BBS/Game/Effects.h
#pragma once
#include "Entities.h"      // Pickup::Kind
#include "Drops.h"         // DropTier

namespace bombo::game
{
    struct PickupOutcome
    {
        int   currencyDelta        = 0;
        int   lifeDelta            = 0;
        bool  refillCharge         = false;
        bool  magnetiseCurrency    = false;
        bool  clearEnemyBullets    = false;
        int   spawnCurrencyCluster = 0;
        int   chainBankAdd         = 0;
        float setSpreadTimer       = 0.0f;
        float setSlowMoTimer       = 0.0f;
        float setMuteTimer         = 0.0f;
        bool  grantPhaseLock       = false;
        bool  grantRandomShopItem  = false;
        bool  grantDoubleShot      = false;
    };

    PickupOutcome resolvePickup(Pickup::Kind k) noexcept;

    struct EffectState
    {
        void applyOutcome(const PickupOutcome& o) noexcept;
        void tick(float dt) noexcept;
        bool spreadActive() const noexcept { return spread_ > 0.0f; }
        bool slowMoActive() const noexcept { return slowMo_ > 0.0f; }
        bool muteActive()   const noexcept { return mute_   > 0.0f; }
        float spread() const noexcept { return spread_; }
        float slowMo() const noexcept { return slowMo_; }
        float mute()   const noexcept { return mute_; }
    private:
        float spread_ = 0.0f, slowMo_ = 0.0f, mute_ = 0.0f;
    };

    DropTier dropTierOf(Pickup::Kind k) noexcept;
    bool shouldSparkle(DropTier tier, int tickCounter) noexcept;
    int  legendaryColorIndex(int tickCounter) noexcept;
}
