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

        // Charged-shot hold progress
        if (charging)
            chargeProgress = std::min(1.0f, chargeProgress + kTickDt / kChargedShotSec);

        // Charge meter refill (linear over kTransientRefillSec)
        if (chargeMeter < 1.0f)
            chargeMeter = std::min(1.0f, chargeMeter + kTickDt / kTransientRefillSec);
    }

    bool Player::releaseCharge() noexcept
    {
        const bool fired = (charging && chargeProgress >= 1.0f && chargeMeter >= 1.0f);
        if (fired)
        {
            chargeMeter -= 1.0f;
            if (chargeMeter < 0.0f) chargeMeter = 0.0f;
        }
        charging = false;
        chargeProgress = 0.0f;
        return fired;
    }

    void Player::takeHit() noexcept
    {
        invincTimer = kInvincTicks;
    }

    Bullet* BulletPool::spawn(float x, float y, float vx, float vy,
                              int damage, bool wide, int pierce) noexcept
    {
        for (auto& s : slots_)
        {
            if (! s.active)
            {
                s.x = x; s.y = y; s.vx = vx; s.vy = vy;
                s.damage = damage; s.wide = wide;
                s.pierceLeft = pierce; s.active = true;
                return &s;
            }
        }
        return nullptr;   // pool full
    }

    void BulletPool::tick() noexcept
    {
        for (auto& s : slots_)
        {
            if (! s.active) continue;
            s.x += s.vx * kTickDt;
            s.y += s.vy * kTickDt;
            if (s.x < -8 || s.x > kFbW + 8 || s.y < -8 || s.y > kFbH + 8)
                s.active = false;
        }
    }
}
