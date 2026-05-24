// Source/GUI/BBS/Game/Entities.h
#pragma once
#include "Constants.h"
#include <array>
#include <cstdint>

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
        Aliaser, AliaserMini, DiveBomber, Rumblr
    };

    struct Enemy
    {
        EnemyKind kind = EnemyKind::Mudball;
        float x = 0.0f, y = 0.0f, vx = 0.0f, vy = 0.0f;
        int   hp = 1;
        int   hpMax = 1;
        bool  active = false;
        float phase = 0.0f;     // sine / timer state
        int   subState = 0;
    };

    // Centralised default HP per enemy kind.
    int defaultHp(EnemyKind k) noexcept;

    class EnemyPool
    {
    public:
        static constexpr int kMax = 48;
        Enemy* spawn(EnemyKind kind, float x, float y, float vx, float vy) noexcept;
        void   tick() noexcept;
        // Returns number of enemies killed this call.
        int    applyBulletDamage(BulletPool& bullets) noexcept;
        const std::array<Enemy, kMax>& enemies() const noexcept { return slots_; }
        std::array<Enemy, kMax>&        enemies() noexcept       { return slots_; }
    private:
        std::array<Enemy, kMax> slots_{};
    };
}
