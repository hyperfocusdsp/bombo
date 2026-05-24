// Source/GUI/BBS/Game/Waves.cpp
#include "Waves.h"
#include "Constants.h"
#include <random>
#include <cmath>

namespace bombo::game
{
    namespace
    {
        struct WaveSpec { std::vector<EnemyKind> pool; int formations; };

        WaveSpec specFor(int waveIdx)
        {
            switch (waveIdx)
            {
                // Densities re-tuned 2026-05-25 after enemy body-contact damage
                // landed (spec §6.1). Contact attrition is now the dominant threat,
                // so formation counts came DOWN substantially to keep the §13.3
                // calibration curve (forgiving W1-W3, W5-W6 skill gate, boss reachable).
                case 1: return { { EnemyKind::Mudball }, 1 };
                case 2: return { { EnemyKind::Mudball, EnemyKind::Clipper }, 1 };
                case 3: return { { EnemyKind::Mudball, EnemyKind::Clipper, EnemyKind::DiveBomber }, 2 };
                case 4: return { { EnemyKind::Clipper, EnemyKind::Aliaser, EnemyKind::DiveBomber }, 3 };
                case 5: return { { EnemyKind::Aliaser, EnemyKind::DiveBomber, EnemyKind::Clipper,
                                   EnemyKind::Limiter }, 6 };
                case 6: return { { EnemyKind::Aliaser, EnemyKind::DiveBomber }, 5 };
                case 7: return { { EnemyKind::Limiter, EnemyKind::Clipper, EnemyKind::Aliaser }, 4 };
                default: return { { EnemyKind::Mudball }, 1 };
            }
        }

        // Per-kind base horizontal speed (px/s, negative = leftward).
        // Limiter gets a vertical velocity so its wall-bounce behaviour is real.
        float baseVx(EnemyKind k)
        {
            switch (k)
            {
                case EnemyKind::Mudball:     return -40.0f;
                case EnemyKind::Clipper:     return -45.0f;
                case EnemyKind::SilenceVoid: return -25.0f;
                case EnemyKind::Limiter:     return -15.0f;
                case EnemyKind::Aliaser:     return -90.0f;
                case EnemyKind::DiveBomber:  return  0.0f;   // tickDiveBomber drives its own x
                default:                     return -40.0f;
            }
        }

        void emitFormation(std::mt19937& rng, std::vector<WaveSchedule::Event>& out,
                           float t0, EnemyKind kind, Formation f)
        {
            std::uniform_real_distribution<float> yDist(20.0f, static_cast<float>(kFbH) - 20.0f);
            const float baseY = yDist(rng);
            const float vx = baseVx(kind);
            // Limiter gets vy so its bounce/wall behaviour fires correctly.
            const float vy = (kind == EnemyKind::Limiter) ? 40.0f : 0.0f;

            switch (f)
            {
                case Formation::Stream:
                    for (int i = 0; i < 5; ++i)
                        out.push_back({ t0 + i * 0.4f, kind, static_cast<float>(kFbW) + 8.0f, baseY, vx, vy });
                    break;

                case Formation::Arc:
                    for (int i = 0; i < 5; ++i)
                        out.push_back({ t0 + i * 0.3f, kind, static_cast<float>(kFbW) + 8.0f,
                                        20.0f + i * 8.0f, vx, vy });
                    break;

                case Formation::Sine:
                    for (int i = 0; i < 5; ++i)
                        out.push_back({ t0 + i * 0.35f, kind, static_cast<float>(kFbW) + 8.0f, baseY, vx, vy });
                    break;

                case Formation::VFormation:
                    for (int i = 0; i < 5; ++i)
                    {
                        const float dy = static_cast<float>(std::abs(i - 2)) * 6.0f;
                        out.push_back({ t0 + i * 0.15f, kind, static_cast<float>(kFbW) + 8.0f,
                                        baseY + dy, vx, vy });
                    }
                    break;

                case Formation::SwarmRush:
                    for (int i = 0; i < 6; ++i)
                    {
                        std::uniform_real_distribution<float> jit(-4.0f, 4.0f);
                        out.push_back({ t0 + i * 0.08f, kind, static_cast<float>(kFbW) + 8.0f,
                                        baseY + jit(rng), vx * 1.3f, vy });
                    }
                    break;

                case Formation::Pincer:
                    for (int i = 0; i < 3; ++i)
                    {
                        out.push_back({ t0 + i * 0.3f, kind, static_cast<float>(kFbW) + 8.0f,
                                        20.0f,                               vx,  4.0f });
                        out.push_back({ t0 + i * 0.3f, kind, static_cast<float>(kFbW) + 8.0f,
                                        static_cast<float>(kFbH) - 20.0f,   vx, -4.0f });
                    }
                    break;
            }
        }
    } // namespace

    WaveSchedule scheduleWave(uint32_t seed, int waveIdx)
    {
        WaveSchedule s;
        const WaveSpec spec = specFor(waveIdx);
        std::mt19937 rng(seed ^ static_cast<uint32_t>(waveIdx * 31));
        std::uniform_int_distribution<int>    fDist(0, 5);
        std::uniform_int_distribution<size_t> kDist(0, spec.pool.size() - 1);
        float t = 0.0f;
        for (int i = 0; i < spec.formations; ++i)
        {
            const auto f = static_cast<Formation>(fDist(rng));
            const auto k = spec.pool[kDist(rng)];
            emitFormation(rng, s.events, t, k, f);
            t += std::uniform_real_distribution<float>(3.0f, 6.0f)(rng);
        }
        return s;
    }

    void runWave(uint32_t seed, int waveIdx, EnemyPool& pool)
    {
        // Spawn all events immediately (ignores timestamps) — deterministic test helper.
        const auto s = scheduleWave(seed, waveIdx);
        for (const auto& e : s.events)
            pool.spawn(e.kind, e.x, e.y, e.vx, e.vy);
    }

    void WaveSchedule::tick(EnemyPool& pool, float dt)
    {
        elapsed += dt;
        while (nextIdx < events.size() && events[nextIdx].t <= elapsed)
        {
            const auto& ev = events[nextIdx++];
            pool.spawn(ev.kind, ev.x, ev.y, ev.vx, ev.vy);
        }
    }
} // namespace bombo::game
