// Source/GUI/BBS/Game/Constants.h
#pragma once

namespace bombo::game
{
    // Framebuffer geometry
    inline constexpr int kFbW = 160;
    inline constexpr int kFbH = 112;

    // Tick rate (was 50Hz in v1, raised to 60 for smoother feel)
    inline constexpr int kTickHz = 60;
    inline constexpr float kTickDt = 1.0f / static_cast<float>(kTickHz);

    // Player
    inline constexpr int kAutoFireTicks      = 16;   // ~3.75 shots/s at 60Hz
    inline constexpr int kInvincTicks        = 150;  // 2.5s
    inline constexpr float kPlayerSpeedPxS   = 60.0f;
    inline constexpr int kPlayerStartLives   = 3;
    inline constexpr int kPlayerMaxLives     = 5;
    inline constexpr float kChargedShotSec   = 1.2f;
    inline constexpr float kTransientRefillSec = 6.0f;

    // Chain meter
    inline constexpr float kChainDrainSec    = 4.0f;
    inline constexpr int   kChainThresholds[5] = { 1, 5, 15, 30, 50 };
    inline constexpr float kChainMultipliers[5] = { 1.0f, 1.5f, 2.0f, 3.0f, 4.0f };

    // Shop
    inline constexpr int kRerollBaseCost     = 10;
    inline constexpr int kShopOfferCount     = 3;

    // Bullet
    inline constexpr float kBulletSpeedPxS   = 180.0f;

    // BPM scaling
    inline constexpr float kBpmRef           = 120.0f;
    inline constexpr float kBpmMinMult       = 0.5f;
    inline constexpr float kBpmMaxMult       = 1.8f;

    // Drops
    inline constexpr float kDropDriftPxS     = 25.0f;
    inline constexpr float kDropLifetimeSec  = 8.0f;
    inline constexpr float kAmbientDropMinSec = 8.0f;
    inline constexpr float kAmbientDropMaxSec = 15.0f;

    // Discovery (BBS invader)
    inline constexpr float kInvaderMinIntervalSec = 60.0f;
    inline constexpr float kInvaderMaxIntervalSec = 120.0f;
    inline constexpr float kInvaderDriftPxS       = 30.0f;
    inline constexpr float kInvaderOnScreenSec    = 4.0f;
}
