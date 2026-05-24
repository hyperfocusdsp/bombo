// Source/GUI/BBS/Game/Entities.cpp
#include "Entities.h"
#include <algorithm>
#include <cmath>

namespace
{
    using namespace bombo::game;

    void tickMudball(Enemy& e) noexcept
    {
        e.x += e.vx * kTickDt;
        e.y += e.vy * kTickDt;
    }

    void tickClipper(Enemy& e) noexcept
    {
        // 1s burst (vx = -100), 1s pause (vx = 0), repeating.
        e.phase += kTickDt;
        if (e.phase < 1.0f)      e.vx = -100.0f;
        else if (e.phase < 2.0f) e.vx = 0.0f;
        else                   { e.phase = 0.0f; e.vx = -100.0f; }   // restart burst immediately
        e.x += e.vx * kTickDt;
    }

    void tickSilenceVoid(Enemy& e) noexcept
    {
        // Slow horizontal drift + sine vertical sway.
        e.phase += kTickDt;
        e.x += e.vx * kTickDt;
        e.y += std::sin(e.phase * 1.5f) * 20.0f * kTickDt;
    }

    void tickLimiter(Enemy& e) noexcept
    {
        // Vertical wall: vy drives, vx slow drift; bounce off top/bottom.
        e.x += e.vx * kTickDt;
        e.y += e.vy * kTickDt;
        if (e.y < 20.0f)         e.vy =  std::abs(e.vy);
        if (e.y > kFbH - 20.0f)  e.vy = -std::abs(e.vy);
    }
    void tickAliaser(Enemy& e) noexcept
    {
        e.x += -90.0f * kTickDt;
    }

    void tickAliaserMini(Enemy& e) noexcept
    {
        e.x += e.vx * kTickDt;
        e.y += e.vy * kTickDt;
    }

    void tickDiveBomber(Enemy& e, const Player* player) noexcept
    {
        e.phase += kTickDt;
        if (e.phase < 1.0f)
        {
            // Lead-in: drift left at a steady pace, no homing yet.
            e.x += -30.0f * kTickDt;
        }
        else
        {
            if (player != nullptr)
            {
                const float dy = player->y - e.y;
                // Clamp vy to ±80 px/s using std::max/min (<algorithm> already included).
                e.vy = std::max(-80.0f, std::min(80.0f, dy * 3.0f));
            }
            e.x += -110.0f * kTickDt;
            e.y += e.vy * kTickDt;
        }
    }
} // anonymous namespace

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

    void EnemyPool::tick(const Player* player) noexcept
    {
        for (auto& s : slots_)
        {
            if (! s.active) continue;
            switch (s.kind)
            {
                case EnemyKind::Mudball:     tickMudball(s);            break;
                case EnemyKind::Clipper:     tickClipper(s);            break;
                case EnemyKind::SilenceVoid: tickSilenceVoid(s);        break;
                case EnemyKind::Limiter:     tickLimiter(s);            break;
                case EnemyKind::Aliaser:     tickAliaser(s);            break;
                case EnemyKind::AliaserMini: tickAliaserMini(s);        break;
                case EnemyKind::DiveBomber:  tickDiveBomber(s, player); break;
                default:                     tickMudball(s);            break;  // Rumblr: Task 19
            }
            // Cull on all four edges (Y-cull added per Task 10 review — vertical movers).
            if (s.x < -16.0f || s.x > kFbW + 16.0f ||
                s.y < -16.0f || s.y > kFbH + 16.0f)
                s.active = false;
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
                    if (e.kind == EnemyKind::SilenceVoid)
                    {
                        b.active = false;   // absorbed, no damage
                        break;
                    }
                    e.hp -= b.damage;
                    if (b.pierceLeft > 0) --b.pierceLeft;
                    else                  b.active = false;
                    if (e.hp <= 0)
                    {
                        if (e.kind == EnemyKind::Aliaser)
                        {
                            // Capture spawn coords before deactivating. spawn() iterates
                            // slots_ to find an inactive slot — fixed std::array, no
                            // reallocation, so the reference 'e' stays valid. The inner
                            // enemy loop breaks immediately after this hit, so the two
                            // freshly-spawned minis will NOT be re-processed by this
                            // bullet. A second active bullet in the OUTER loop could hit
                            // a mini the same tick — that's intentional game behaviour.
                            const float sx = e.x, sy = e.y, svx = e.vx;
                            e.active = false;
                            ++kills;
                            spawn(EnemyKind::AliaserMini, sx, sy - 6.0f, svx * 0.7f, -40.0f);
                            spawn(EnemyKind::AliaserMini, sx, sy + 6.0f, svx * 0.7f,  40.0f);
                        }
                        else
                        {
                            e.active = false;
                            ++kills;
                        }
                    }
                    // Pierce: bullet stays active but hits at most one enemy per tick;
                    // the next enemy is reached within 1-2 ticks at typical bullet speed.
                    break;
                }
            }
        }
        return kills;
    }
}
