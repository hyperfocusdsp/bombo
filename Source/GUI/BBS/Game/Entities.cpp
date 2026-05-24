// Source/GUI/BBS/Game/Entities.cpp
#include "Entities.h"
#include <algorithm>
#include <cmath>

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

    int defaultHp(EnemyKind k) noexcept
    {
        switch (k)
        {
            case EnemyKind::Mudball:     return 3;
            case EnemyKind::Clipper:     return 2;
            case EnemyKind::SilenceVoid: return 9999;   // logically invincible; player must dodge
            case EnemyKind::Limiter:     return 4;
            case EnemyKind::Aliaser:     return 1;
            case EnemyKind::AliaserMini: return 1;
            case EnemyKind::DiveBomber:  return 1;
            case EnemyKind::Rumblr:      return 40;
        }
        return 1;
    }

    Enemy* EnemyPool::spawn(EnemyKind kind, float x, float y, float vx, float vy) noexcept
    {
        for (auto& s : slots_)
        {
            if (! s.active)
            {
                s = Enemy{};
                s.kind = kind;
                s.x = x; s.y = y; s.vx = vx; s.vy = vy;
                s.hp = s.hpMax = defaultHp(kind);
                s.active = true;
                return &s;
            }
        }
        return nullptr;
    }

    void EnemyPool::tick() noexcept
    {
        for (auto& s : slots_)
        {
            if (! s.active) continue;
            // Mudball: pure straight line. (Other kinds get behaviour in Task 11/12.)
            s.x += s.vx * kTickDt;
            s.y += s.vy * kTickDt;
            if (s.x < -16 || s.x > kFbW + 16) s.active = false;
        }
    }

    int EnemyPool::applyBulletDamage(BulletPool& bullets) noexcept
    {
        int kills = 0;
        for (auto& b : bullets.bullets())
        {
            if (! b.active) continue;
            for (auto& e : slots_)
            {
                if (! e.active) continue;
                // Hitbox: 6px Chebyshev around enemy centre.
                const float dx = std::abs(b.x - e.x);
                const float dy = std::abs(b.y - e.y);
                if (dx < 6 && dy < 6)
                {
                    e.hp -= b.damage;
                    if (b.pierceLeft > 0) --b.pierceLeft;
                    else                  b.active = false;
                    if (e.hp <= 0) { e.active = false; ++kills; }
                    break;   // this bullet is done checking enemies
                }
            }
        }
        return kills;
    }
}
