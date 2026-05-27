// Source/GUI/BBS/Game/Entities.cpp
#include "Entities.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>    // std::mt19937 backs the boss phase-2 charge-trigger jitter.
                     // std::rand() is NOT portable — its sequence differs per libc
                     // (glibc vs MSVC vs Apple), so the charge fired within the test
                     // window on Linux but not on Windows/macOS, silently failing CI.
                     // mt19937 yields an identical sequence on every platform for a
                     // given seed, so the jitter stays a per-run flourish AND is
                     // reproducible across processes and platforms.

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
        // 1s burst, 1s pause (vx = 0), repeating. Burst speed eased -100 -> -70 in
        // the 2026-05-25 engagement pass so dense early waves stay dodgeable.
        e.phase += kTickDt;
        if (e.phase < 1.0f)      e.vx = -70.0f;
        else if (e.phase < 2.0f) e.vx = 0.0f;
        else                   { e.phase = 0.0f; e.vx = -70.0f; }    // restart burst immediately
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
            e.x += -25.0f * kTickDt;
        }
        else
        {
            if (player != nullptr)
            {
                const float dy = player->y - e.y;
                // Clamp vy to ±60 px/s using std::max/min (<algorithm> already included).
                // Homing speed eased (110->80 x, 80->60 vy) in the 2026-05-25
                // engagement pass: the dive is still a real threat but dodgeable in a
                // populated wave.
                e.vy = std::max(-60.0f, std::min(60.0f, dy * 2.5f));
            }
            e.x += -80.0f * kTickDt;
            e.y += e.vy * kTickDt;
        }
    }

    // ── Added movement variety (2026-05-25 engagement pass) ─────────────────
    // Before this, 10 of the 17 kinds fell through to tickMudball (pure straight
    // drift), so mid/late waves all moved identically. Each function below uses
    // the spawn-time vx for horizontal speed (so the per-wave speedMult ramp
    // still applies) and only adds a distinct VERTICAL behaviour.

    // Sine weave around the spawn row. A clear visual wiggle, but modest
    // amplitude so the enemy stays roughly on its lane and is still hittable
    // (a big weave makes it dodge the player's autofire and linger forever).
    void tickWeave(Enemy& e) noexcept
    {
        e.phase += kTickDt;
        e.x += e.vx * kTickDt;
        e.y += std::sin(e.phase * 2.6f) * 30.0f * kTickDt;
    }

    // Stutter-step: short burst / short pause, faster cadence than Clipper.
    void tickStutter(Enemy& e) noexcept
    {
        e.phase += kTickDt;
        constexpr float kBurst = -55.0f;
        if      (e.phase < 0.50f) e.vx = kBurst;
        else if (e.phase < 0.85f) e.vx = 0.0f;
        else                    { e.phase = 0.0f; e.vx = kBurst; }
        e.x += e.vx * kTickDt;
    }

    // Phaser: drifts left while periodically "jumping" its Y by a fixed step,
    // alternating direction. Reads as a glitchy teleport rather than a glide.
    void tickPhaser(Enemy& e) noexcept
    {
        e.phase += kTickDt;
        e.x += e.vx * kTickDt;
        if (e.phase >= 1.5f)
        {
            e.phase = 0.0f;
            const float step = (e.subState % 2 == 0) ? 12.0f : -12.0f;
            e.y += step;
            e.y = std::max(12.0f, std::min(static_cast<float>(kFbH) - 12.0f, e.y));
            ++e.subState;
        }
    }

    // Late-wave enemy fire. Projectiles only exist from kEnemyFireMinWave on, so
    // early waves stay a gentle on-ramp (movement variety only, no incoming fire).
    // Slow, dodgeable shots aimed loosely at the player; the boss fires via its
    // own tickRumblr path and is excluded by the caller.
    constexpr int kEnemyFireMinWave = 5;

    float enemyFireInterval(EnemyKind k) noexcept
    {
        switch (k)
        {
            case EnemyKind::Crackle:   return 4.0f;   // light shooter, introduces projectiles ~W5
            case EnemyKind::Overdrive: return 3.5f;   // elite, W8+
            case EnemyKind::Resonator: return 4.0f;   // elite, W11
            // Phaser is intentionally NOT a shooter — its vertical-jump movement is
            // its gimmick. Adding fire on top stacked too much projectile pressure
            // at W9-W11 and sank boss completion below the §13.3 band.
            default:                   return 0.0f;   // non-shooter
        }
    }

    void maybeEnemyFire(Enemy& e, const Player* player, BulletPool* enemyShots,
                        int waveIdx) noexcept
    {
        if (enemyShots == nullptr || waveIdx < kEnemyFireMinWave) return;
        const float interval = enemyFireInterval(e.kind);
        if (interval <= 0.0f) return;
        if (e.x > static_cast<float>(kFbW) - 4.0f) return;   // wait until on-screen

        e.fireTimer += kTickDt;
        if (e.fireTimer < interval) return;
        e.fireTimer = 0.0f;

        const float sx = e.x - 6.0f, sy = e.y;
        float vy = 0.0f;
        if (player != nullptr)
        {
            const float dy = player->y - e.y;
            vy = std::max(-18.0f, std::min(18.0f, dy * 0.6f));   // loose aim
        }
        constexpr float vx = -30.0f;   // slow, dodgeable
        enemyShots->spawn(sx, sy, vx, vy);
    }

    // ── Boss fire primitives (shared by the boss tick functions) ────────────
    constexpr float kBossPi = 3.14159265f;

    // One slow shot aimed loosely at the player (player sits to the left).
    void bossFireAimed(Enemy& b, const Player* player, BulletPool* shots, float speed) noexcept
    {
        if (shots == nullptr) return;
        float vy = 0.0f;
        if (player != nullptr)
        {
            const float dy = player->y - b.y;
            const float dx = std::max(1.0f, b.x - player->x);
            vy = (dy / std::sqrt(dx * dx + dy * dy)) * speed;
        }
        shots->spawn(b.x - 8.0f, b.y, -speed, std::max(-speed, std::min(speed, vy)));
    }

    // N-shot fan sweeping +/- ~57deg around straight-left.
    void bossFireRadial(Enemy& b, BulletPool* shots, int n, float speed) noexcept
    {
        if (shots == nullptr || n < 1) return;
        for (int i = 0; i < n; ++i)
        {
            const float t = (n == 1) ? 0.0f
                                     : (2.0f * static_cast<float>(i)
                                        / static_cast<float>(n - 1) - 1.0f);  // -1..1
            const float a = kBossPi + t * 1.0f;
            shots->spawn(b.x - 6.0f, b.y, speed * std::cos(a), speed * std::sin(a));
        }
    }
} // anonymous namespace

namespace bombo::game
{
    // Portable PRNG for boss charge-trigger jitter (see the <random> note above).
    // Single-threaded use only: the game ticks on the message thread, tests are
    // single-threaded — same global-state semantics std::rand() had, minus the
    // per-libc sequence divergence.
    static std::mt19937& bossRng() noexcept
    {
        static std::mt19937 rng{ 0xB0FFu };   // BOFF; arbitrary fixed default seed
        return rng;
    }

    void seedBossRng(std::uint32_t seed) noexcept { bossRng().seed(seed); }

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
                        if ((bossRng()() % 3u) == 0u) { r.subState = 1; r.phase = 0.0f; }
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

    bool isBoss(EnemyKind k) noexcept
    {
        switch (k)
        {
            case EnemyKind::Rumblr:
            case EnemyKind::MiniBoss1:
            case EnemyKind::MiniBoss2:
            case EnemyKind::Boss2:
            case EnemyKind::Boss3: return true;
            default:               return false;
        }
    }

    // Mini-boss: a tanky unit that patrols vertically (vy set at spawn, bounces
    // off the margins) and fires periodically. MiniBoss1 lobs a single aimed
    // shot; MiniBoss2 throws a 3-shot fan. Weakness in practice: it telegraphs
    // by pausing at the bounce extremes where it's easiest to line up.
    void tickMiniBoss(Enemy& b, const Player* player, BulletPool* enemyShots) noexcept
    {
        b.y += b.vy * kTickDt;
        if (b.y < 24.0f)            b.vy =  std::abs(b.vy);
        if (b.y > kFbH - 24.0f)     b.vy = -std::abs(b.vy);

        b.fireTimer += kTickDt;
        const float interval = (b.kind == EnemyKind::MiniBoss2) ? 1.8f : 2.2f;
        if (b.fireTimer >= interval)
        {
            b.fireTimer = 0.0f;
            if (b.kind == EnemyKind::MiniBoss2) bossFireRadial(b, enemyShots, 3, 30.0f);
            else                                bossFireAimed (b, player, enemyShots, 34.0f);
        }
    }

    // Boss 2 ("OVERDRIVE CORE"): near-stationary core that sways slightly and
    // pumps radial bullet fans, denser in its second HP half (+ an aimed shot).
    void tickBoss2(Enemy& b, const Player* player, BulletPool* enemyShots) noexcept
    {
        b.y += std::sin(b.phase * 0.8f) * 12.0f * kTickDt;
        b.phase += kTickDt;
        b.fireTimer += kTickDt;
        const bool enraged = b.hp <= b.hpMax / 2;
        if (b.fireTimer >= (enraged ? 1.6f : 2.4f))
        {
            b.fireTimer = 0.0f;
            bossFireRadial(b, enemyShots, enraged ? 7 : 5, 30.0f);
            if (enraged) bossFireAimed(b, player, enemyShots, 36.0f);
        }
    }

    // Boss 3 ("MASTERBUS", final): cycles radial+aimed fire and, in its lower
    // HP phases, periodically telegraphs then charges across the screen and
    // returns — combining the earlier bosses' threats. 3 HP-gated phases.
    void tickBoss3(Enemy& b, const Player* player, BulletPool* enemyShots) noexcept
    {
        b.phase += kTickDt;
        const int ph = (b.hp <= b.hpMax / 3) ? 3
                     : (b.hp <= 2 * b.hpMax / 3) ? 2 : 1;
        switch (b.subState)
        {
            case 0:  // hold + fire; maybe trigger a charge in phases 2/3
                b.fireTimer += kTickDt;
                if (b.fireTimer >= (ph >= 2 ? 1.8f : 2.4f))
                {
                    b.fireTimer = 0.0f;
                    bossFireRadial(b, enemyShots, 5 + ph, 30.0f);
                    bossFireAimed (b, player, enemyShots, 34.0f);
                    if (ph >= 2 && (bossRng()() % 3u) == 0u) { b.subState = 1; b.phase = 0.0f; }
                }
                break;
            case 1:  // telegraph
                if (b.phase >= 0.8f) { b.subState = 2; b.phase = 0.0f; }
                break;
            case 2:  // charge left
                b.x -= 220.0f * kTickDt;
                if (b.x < 20.0f) { b.subState = 3; }
                break;
            case 3:  // return home
                b.x += 60.0f * kTickDt;
                if (b.x >= static_cast<float>(kFbW) - 30.0f)
                { b.x = static_cast<float>(kFbW) - 30.0f; b.subState = 0; b.phase = 0.0f; }
                break;
            default: b.subState = 0; break;
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
            // Re-swept 2026-05-25 (engagement pass). The prior 13 was a bug: since
            // rumblrPhase() returns phase 1 only when hp > 25, an hpMax of 13 started
            // the boss in phase 2 and it NEVER entered phase 1 -- the telegraphed
            // standing-shockwave intro was dead and the fight was flat phase-2. HP is
            // raised so the boss enters phase 1 (hp > 25 for a meaningful span), then
            // phase 2 (<= 25), then phase-3-clamped-to-2 (<= 10): a real escalation
            // arc. Boss completion is gated by the now-denser pre-boss WAVE attrition,
            // not by gutting boss HP. Re-swept with the sim to keep completion in the
            // spec §13.3 15-25% band. See tests/GameBalanceSim.cpp calibration notes.
            case EnemyKind::Rumblr:      return 40;   // longer final fight; phase 1 = 40..26, phase 2 = 25..0
            // Extra monsters: light fodder. Elites: tankier.
            case EnemyKind::Warble:      return 2;
            case EnemyKind::Hiss:        return 2;
            case EnemyKind::Crackle:     return 3;
            case EnemyKind::Wobble:      return 3;
            case EnemyKind::Stutter:     return 4;
            case EnemyKind::Overdrive:   return 8;
            case EnemyKind::Phaser:      return 8;
            case EnemyKind::Flanger:     return 10;
            case EnemyKind::Resonator:   return 12;
            // Boss-class. Mini-bosses are chunky-but-quick; Boss2/Boss3 escalate;
            // Boss3 (final) is the tankiest. NG+ adds +1 HP per tier on top (see
            // Game::spawnEncounter). Rumblr (40) above keeps its phase thresholds.
            case EnemyKind::MiniBoss1:   return 18;
            case EnemyKind::MiniBoss2:   return 24;
            case EnemyKind::Boss2:       return 55;
            case EnemyKind::Boss3:       return 75;
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
                if (kind != EnemyKind::SilenceVoid)    // SilenceVoid is logically invincible
                    s.hp = s.hpMax = s.hpMax + hpBonus_;
                s.active = true;
                return &s;
            }
        }
        return nullptr;
    }

    void EnemyPool::tick(const Player* player, BulletPool* enemyShots, int waveIdx) noexcept
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
                // Distinct movement (was all tickMudball fall-through before):
                case EnemyKind::Wobble:      tickWeave(s);                   break;
                case EnemyKind::Warble:      tickWeave(s);                   break;
                case EnemyKind::Hiss:        tickWeave(s);                   break;
                case EnemyKind::Stutter:     tickStutter(s);                 break;
                case EnemyKind::Phaser:      tickPhaser(s);                  break;
                // Plain straight movers. Flanger is a 10-HP tank — kept straight
                // (a weaving tank dodges autofire and lingers, spiking late-wave
                // attrition). Crackle/Overdrive/Resonator shoot (see maybeEnemyFire).
                case EnemyKind::Flanger:     tickMudball(s);                 break;
                case EnemyKind::Crackle:     tickMudball(s);                 break;
                case EnemyKind::Overdrive:   tickMudball(s);                 break;
                case EnemyKind::Resonator:   tickMudball(s);                 break;
                // Boss-class encounters drive their own movement + fire.
                case EnemyKind::MiniBoss1:
                case EnemyKind::MiniBoss2:   tickMiniBoss(s, player, enemyShots); break;
                case EnemyKind::Boss2:       tickBoss2(s, player, enemyShots);    break;
                case EnemyKind::Boss3:       tickBoss3(s, player, enemyShots);    break;
            }
            // Late-wave shooter fire (no-op for bosses — they fire via their own
            // tick — for non-shooter kinds, and for waves below kEnemyFireMinWave).
            if (! isBoss(s.kind))
                maybeEnemyFire(s, player, enemyShots, waveIdx);

            // Cull on all four edges (Y-cull added per Task 10 review — vertical movers).
            // NEVER cull a boss — it's confined to the screen and must persist.
            if (! isBoss(s.kind) &&
                (s.x < -16.0f || s.x > kFbW + 16.0f ||
                 s.y < -16.0f || s.y > kFbH + 16.0f))
                s.active = false;
        }

        // Separation: keep active enemies from overlapping so each one stays
        // individually visible even when a wave spawns a dense cluster. Gentle
        // pairwise push toward a minimum spacing (sprites are ~10px). Excludes
        // the boss (large, screen-confined). O(n^2) over a small fixed pool.
        constexpr float kMinSep = 11.0f;
        for (auto& a : slots_)
        {
            if (! a.active || isBoss(a.kind)) continue;
            for (auto& b : slots_)
            {
                if (&b <= &a) continue;   // visit each unordered pair once
                if (! b.active || isBoss(b.kind)) continue;
                float dx = b.x - a.x, dy = b.y - a.y;
                float d2 = dx * dx + dy * dy;
                if (d2 <= 0.0001f) { dx = 0.6f; dy = 0.0f; d2 = 0.36f; }  // coincident -> nudge apart
                if (d2 >= kMinSep * kMinSep) continue;
                const float d = std::sqrt(d2);
                const float push = (kMinSep - d) * 0.5f;
                const float nx = dx / d, ny = dy / d;
                a.x -= nx * push; a.y -= ny * push;
                b.x += nx * push; b.y += ny * push;
            }
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
