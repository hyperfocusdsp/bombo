// Source/GUI/BBS/Game/Entities.cpp
#include "Entities.h"
#include <algorithm>

namespace bombo::game
{
    void Player::tick() noexcept
    {
        x += vx * kTickDt;
        y += vy * kTickDt;

        // Autofire
        if (autofireOn)
        {
            ++autofireTimer;
            if (autofireTimer >= kAutoFireTicks)
            {
                autofireTimer = 0;
                shootFlag_ = true;
            }
            else
            {
                shootFlag_ = false;
            }
        }
        else
        {
            shootFlag_ = false;
        }

        // Invincibility countdown
        if (invincTimer > 0) --invincTimer;

        // Charge meter refill (linear over kTransientRefillSec)
        if (chargeMeter < 1.0f)
            chargeMeter = std::min(1.0f, chargeMeter + kTickDt / kTransientRefillSec);
    }

    void Player::takeHit() noexcept
    {
        invincTimer = kInvincTicks;
    }
}
