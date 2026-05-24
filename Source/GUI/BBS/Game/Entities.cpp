// Source/GUI/BBS/Game/Entities.cpp
#include "Entities.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>   // std::rand — used by tickRumblr phase-2 charge trigger (non-deterministic
                     // flourish; charge timing jitters per run, which is intentional for v1.0.x)

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
        // Use stored velocity so wave formations control speed; fall back to a
        // brisk left drift if spawned with no vx (keeps mini-split svx coupling honest).
        if (e.vx == 0.0f) e.vx = -90.0f;
        e.x += e.vx * kTickDt;
        e.y += e.vy * kTickDt;
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
    int rumblrPhase(const Enemy& r) noexcept
    {
        if (r.hp <= 10) return 3;       // v1.1 territory; tickRumblr clamps to phase-2 behaviour
        if (r.hp <= 25) return 2;
        return 1;
    }

    void tickRumblr(Enemy& r, BulletPool* enemyShots) noexcept
    {
        const int phase = rumblrPhase(r);
        r.phase += kTickDt;

        // Phase 1 AND the "hold" sub-state of phase 2 both fire the 3-projectile
        // shockwave every 2.5s. Phase 2 additionally does a telegraph->charge->return.
        auto fireShockwave = [&]()
        {
            if (enemyShots == nullptr) return;
            enemyShots->spawn(r.x - 8.0f, r.y - 10.0f, -30.0f, 0.0f);
            enemyShots->spawn(r.x - 8.0f, r.y,          -30.0f, 0.0f);
            enemyShots->spawn(r.x - 8.0f, r.y + 10.0f,  -30.0f, 0.0f);
        };

        if (phase == 1)
        {
            if (r.phase >= 2.5f) { r.phase = 0.0f; fireShockwave(); }
        }
        else // phase 2 (and phase-3 HP range clamps to phase-2 behaviour in v1.0.x)
        {
            switch (r.subState)
            {
                case 0:  // hold + fire; randomly trigger a charge
                    if (r.phase >= 2.5f)
                    {
                        r.phase = 0.0f;
                        fireShockwave();
                        if ((std::rand() % 3) == 0) { r.subState = 1; r.phase = 0.0f; }
                    }
                    break;
                case 1:  // telegraph 1s (visual cue that a charge is coming)
                    if (r.phase >= 1.0f) { r.subState = 2; r.phase = 0.0f; }
                    break;
                case 2:  // charge left across the screen at 200 px/s
                    r.x -= 200.0f * kTickDt;
                    if (r.x < 20.0f) { r.subState = 3; r.phase = 0.0f; }
                    break;
                case 3:  // return to home position (x=140) at 50 px/s
                    r.x += 50.0f * kTickDt;
                    if (r.x >= 140.0f) { r.x = 140.0f; r.subState = 0; r.phase = 0.0f; }
                    break;
                default: r.subState = 0; break;
            }
        }
    }


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
            case EnemyKind::Rumblr:      return 60;
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

    void EnemyPool::tick(const Player* player, BulletPool* enemyShots) noexcept
    {
        for (auto& s : slots_)
        {
            if (! s.active) continue;
            switch (s.kind)
            {
                case EnemyKind::Mudball:     tickMudball(s);                 break;
                case EnemyKind::Clipper:     tickClipper(s);                 break;
                case EnemyKind::SilenceVoid: tickSilenceVoid(s);             break;
                case EnemyKind::Limiter:     tickLimiter(s);                 break;
                case EnemyKind::Aliaser:     tickAliaser(s);                 break;
                case EnemyKind::AliaserMini: tickAliaserMini(s);             break;
                case EnemyKind::DiveBomber:  tickDiveBomber(s, player);      break;
                case EnemyKind::Rumblr:      tickRumblr(s, enemyShots);      break;
                default:                     tickMudball(s);                 break;
            }
            // Cull on all four edges (Y-cull added per Task 10 review — vertical movers).
            // NEVER cull the boss — it's confined to the screen and must persist.
            if (s.kind != EnemyKind::Rumblr &&
                (s.x < -16.0f || s.x > kFbW + 16.0f ||
                 s.y < -16.0f || s.y > kFbH + 16.0f))
                s.active = false;
        }
    }

    bool isCurrency(Pickup::Kind k) noexcept
    {
        return k == Pickup::Kind::DbSmall || k == Pickup::Kind::DbMed ||
               k == Pickup::Kind::DbBig   || k == Pickup::Kind::DbCluster;
    }

    Pickup* PickupPool::spawn(Pickup::Kind kind, float x, float y) noexcept
    {
        for (auto& s : slots_)
        {
            if (! s.active)
            {
                s = Pickup{};
                s.kind = kind;
                s.x = x; s.y = y;
                s.vx = -kDropDriftPxS;   // drift left
                s.vy = 0.0f;
                s.ttl = kDropLifetimeSec;
                s.active = true;
                return &s;
            }
        }
        return nullptr;
    }

    void PickupPool::tick(float playerX, float playerY, bool magnetActive) noexcept
    {
        for (auto& s : slots_)
        {
            if (! s.active) continue;

            if (magnetActive && isCurrency(s.kind))
            {
                // Pull toward the player.
                const float dx = playerX - s.x;
                const float dy = playerY - s.y;
                const float len = std::sqrt(dx*dx + dy*dy);
                if (len > 0.001f)
                {
                    const float pull = 120.0f;   // px/s
                    s.x += (dx / len) * pull * kTickDt;
                    s.y += (dy / len) * pull * kTickDt;
                }
            }
            else
            {
                s.x += s.vx * kTickDt;
                s.y += s.vy * kTickDt;
            }

            s.ttl -= kTickDt;
            if (s.ttl <= 0.0f || s.x < -16.0f || s.x > kFbW + 16.0f)
                s.active = false;
        }
    }

    int EnemyPool::applyBulletDamage(BulletPool& bullets, std::vector<KillInfo>* out) noexcept
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
                            if (out != nullptr) out->push_back({ EnemyKind::Aliaser, sx, sy });
                            spawn(EnemyKind::AliaserMini, sx, sy - 6.0f, svx * 0.7f, -40.0f);
                            spawn(EnemyKind::AliaserMini, sx, sy + 6.0f, svx * 0.7f,  40.0f);
                        }
                        else
                        {
                            if (out != nullptr) out->push_back({ e.kind, e.x, e.y });
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
