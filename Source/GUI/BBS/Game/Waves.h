// Source/GUI/BBS/Game/Waves.h
#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include "Entities.h"

namespace bombo::game
{
    enum class Formation { Stream, Arc, Sine, VFormation, SwarmRush, Pincer };

    // Deterministic: spawns the full wave roster into `pool` immediately.
    // Used by tests + anywhere an instant roster is wanted. Pure function of (seed, waveIdx).
    void runWave(uint32_t seed, int waveIdx, EnemyPool& pool);

    // Same wave as a time-scheduled drip feed. The Game ticks this and the pool
    // gains spawns over time.
    struct WaveSchedule
    {
        struct Event { float t; EnemyKind kind; float x, y, vx, vy; };
        std::vector<Event> events;
        float elapsed = 0.0f;
        size_t nextIdx = 0;

        void tick(EnemyPool& pool, float dt);
        bool done() const noexcept { return nextIdx >= events.size(); }
    };

    WaveSchedule scheduleWave(uint32_t seed, int waveIdx);
}
