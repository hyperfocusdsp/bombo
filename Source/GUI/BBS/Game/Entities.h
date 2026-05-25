// Source/GUI/BBS/Game/Entities.h
#pragma once
#include "Constants.h"
#include <array>
#include <cstdint>
#include <vector>

namespace bombo::game
{
    struct Player
    {
        float x = 30.0f, y = 56.0f;
        float vx = 0.0f, vy = 0.0f;
        int   autofireTimer = 0;
        bool  autofireOn = true;
        int   invincTimer = 0;
        float chargeMeter = 1.0f;        // 0..1, charged-shot ammo
        float chargeProgress = 0.0f;     // 0..1, current hold-down progress
        bool  charging = false;

        void tick() noexcept;
        bool wantsShootThisTick() const noexcept { return shootFlag_; }
        void clearShootFlag() noexcept { shootFlag_ = false; }
        void takeHit() noexcept;
        bool isInvincible() const noexcept { return invincTimer > 0; }
        void beginCharge() noexcept { if (chargeMeter > 0.0f) charging = true; }
        bool releaseCharge() noexcept;   // returns true if a charged shot fires

    private:
        bool shootFlag_ = false;
    };

    struct Bullet
    {
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        int   damage = 1;
        bool  wide = false;
        bool  active = false;
        int   pierceLeft = 0;
    };

    class BulletPool
    {
    public:
        static constexpr int kMax = 64;
        Bullet* spawn(float x, float y, float vx, float vy,
                      int damage = 1, bool wide = false, int pierce = 0) noexcept;
        void    tick() noexcept;
        const std::array<Bullet, kMax>& bullets() const noexcept { return slots_; }
        std::array<Bullet, kMax>&        bullets() noexcept       { return slots_; }
    private:
        std::array<Bullet, kMax> slots_{};
    };

    enum class EnemyKind : uint8_t
    {
        Mudball, Clipper, SilenceVoid, Limiter,
        Aliaser, AliaserMini, DiveBomber, Rumblr,
        // Extra straight-mover kinds (sprites from the sheet's spare cells).
        // Monsters: light/fast fodder. Elites: tankier, higher score.
        Warble, Hiss, Crackle, Wobble, Stutter,
        Overdrive, Phaser, Flanger, Resonator,
        // Boss-class encounters (2 mini-bosses + 2 extra act bosses; RUMBLR
        // above is boss 1). All report true from isBoss() so cull / separation /
        // wave-clear / AutoPlayer logic treats them like RUMBLR.
        MiniBoss1, MiniBoss2, Boss2, Boss3
    };

    // Boss-class kinds: screen-confined, never culled, gate the wave/victory flow.
    bool isBoss(EnemyKind k) noexcept;

    struct Enemy
    {
        EnemyKind kind = EnemyKind::Mudball;
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        int   hp = 1;
        int   hpMax = 1;
        bool  active = false;
        float phase = 0.0f;     // sine / movement-timer state
        float fireTimer = 0.0f; // enemy-projectile cooldown (shooter kinds, late waves)
        int   subState = 0;
    };

    // Centralised default HP per enemy kind.
    int defaultHp(EnemyKind k) noexcept;

    // Boss helpers — exposed for unit tests.
    int  rumblrPhase(const Enemy& r) noexcept;
    void tickRumblr(Enemy& r, BulletPool* enemyShots) noexcept;
    void tickMiniBoss(Enemy& b, const Player* player, BulletPool* enemyShots) noexcept;
    void tickBoss2(Enemy& b, const Player* player, BulletPool* enemyShots) noexcept;
    void tickBoss3(Enemy& b, const Player* player, BulletPool* enemyShots) noexcept;

    class EnemyPool
    {
    public:
        static constexpr int kMax = 48;
        Enemy* spawn(EnemyKind kind, float x, float y, float vx, float vy) noexcept;
        // NG+ HP bonus added to every spawn's base HP (0 = base game). Set once
        // per run by Game from the NG+ tier; SilenceVoid (already 9999) is unaffected.
        void   setHpBonus(int n) noexcept { hpBonus_ = n; }
        // enemyShots: optional pool for RUMBLR shockwave projectiles + late-wave
        // shooter-kind fire. waveIdx gates regular-enemy fire to late waves only
        // (>= kEnemyFireMinWave); 0 disables it. All params default so existing
        // ep.tick() / ep.tick(&player) callers (and unit tests) compile unchanged.
        void   tick(const Player* player = nullptr, BulletPool* enemyShots = nullptr,
                    int waveIdx = 0) noexcept;

        // Per-kill report for scoring + drop rolls. Reports the PARENT kill only
        // (AliaserMini spawns are not reported; the existing split logic is unchanged).
        struct KillInfo { EnemyKind kind; float x, y; };
        // Returns number of enemies killed this call. If `out` is non-null, a KillInfo
        // is pushed for each kill. Default-null keeps existing callers/tests compiling.
        int    applyBulletDamage(BulletPool& bullets,
                                 std::vector<KillInfo>* out = nullptr) noexcept;
        const std::array<Enemy, kMax>& enemies() const noexcept { return slots_; }
        std::array<Enemy, kMax>&        enemies() noexcept       { return slots_; }
    private:
        std::array<Enemy, kMax> slots_{};
        int hpBonus_ = 0;   // NG+ additive HP (see setHpBonus)
    };

    struct Pickup
    {
        enum class Kind : uint8_t
        {
            DbSmall, DbMed, DbBig, OneUp, TransientBurst, Compression,
            EqFilter, ChainBank, DbCluster, DoubleShot,
            TimeFreeze, SidechainPulse, Mute, PhaseLock,
            Mystery
        };
        Kind  kind = Kind::DbSmall;
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        float ttl = kDropLifetimeSec;
        bool  active = false;
    };

    class PickupPool
    {
    public:
        static constexpr int kMax = 32;
        // Spawns a pickup that drifts left at kDropDriftPxS by default.
        Pickup* spawn(Pickup::Kind kind, float x, float y) noexcept;
        // playerX/Y + magnetActive used to pull currency pickups toward the player.
        void    tick(float playerX, float playerY, bool magnetActive) noexcept;
        const std::array<Pickup, kMax>& pickups() const noexcept { return slots_; }
        std::array<Pickup, kMax>&        pickups() noexcept       { return slots_; }
    private:
        std::array<Pickup, kMax> slots_{};
    };

    // True for currency-type pickups (affected by the dB Magnet).
    bool isCurrency(Pickup::Kind k) noexcept;
}
