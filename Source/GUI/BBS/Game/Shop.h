// Source/GUI/BBS/Game/Shop.h
#pragma once
#include <vector>
#include <array>
#include <random>

namespace bombo::game
{
    enum class ShopItemId
    {
        FireRate, Spread, TransientBuff, SidechainShield,
        ExtraLife, LowpassDodge, ChainTimer, DbMagnet,
        // v1.1 (declared for forward-compat; not in catalogV10)
        ChargeSpeed, PhaseFlip, Duck, Oversample, Bandpass, MultitapDelay, ReverbTail,
    };

    struct ShopItem
    {
        ShopItemId  id;
        const char* shortName;
        int         cost;
        int         maxStacks;
        const char* shortDesc;
    };

    const std::vector<ShopItem>& catalogV10();   // 8 v1.0.x items

    class ShopVisit
    {
    public:
        explicit ShopVisit(uint32_t seed);
        const std::array<ShopItem, 3>& offers() const noexcept { return offers_; }
        int  rerollCost() const noexcept;
        // Checks affordability and advances rerollUses; caller is responsible for
        // deducting rerollCost() from the player's dB when this returns true.
        bool reroll(int dbAvailable);
        // playerStacks indexed by (int)ShopItemId (0..14). Returns true if purchased.
        bool buy(int slotIdx, int& dbAvailable, std::array<int, 15>& playerStacks);
    private:
        void rollOffers();
        std::mt19937 rng_;
        std::array<ShopItem, 3> offers_{};
        int rerollUses_ = 0;
    };
}
