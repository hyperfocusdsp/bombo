// Source/GUI/BBS/Game/Waves.cpp
#include "Waves.h"
#include "Constants.h"
#include <algorithm>
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
                // Densities re-tuned 2026-05-25 (engagement pass) after enemy
                // body-contact damage landed (spec §6.1). The previous pass over-
                // corrected: W1/W2 were single formations (~5 enemies over a 40s
                // wave => near-empty, boring screens). Engagement (enough enemies to
                // shoot/dodge) and survivability are INDEPENDENT once contact damage
                // is i-frame-gated, so density is restored across the board and the
                // §13.3 survival curve is recovered via enemy SPEED/HP, not by
                // emptying the screen. Early waves stay forgiving because they lean on
                // slow, low-HP kinds (Mudball/Clipper), not on being sparse.
                case 1: return { { EnemyKind::Mudball }, 2 };
                case 2: return { { EnemyKind::Mudball, EnemyKind::Clipper }, 2 };
                case 3: return { { EnemyKind::Mudball, EnemyKind::Clipper, EnemyKind::DiveBomber }, 3 };
                case 4: return { { EnemyKind::Clipper, EnemyKind::Aliaser,
                                   EnemyKind::Warble, EnemyKind::DiveBomber }, 4 };
                case 5: return { { EnemyKind::Aliaser, EnemyKind::DiveBomber, EnemyKind::Hiss,
                                   EnemyKind::Limiter, EnemyKind::Crackle }, 5 };
                case 6: return { { EnemyKind::Aliaser, EnemyKind::Warble,
                                   EnemyKind::Wobble, EnemyKind::DiveBomber }, 4 };
                case 7: return { { EnemyKind::Limiter, EnemyKind::Crackle,
                                   EnemyKind::Stutter, EnemyKind::Aliaser }, 4 };
                // W8-W11: extended late-game gauntlet. Denser formations + the new
                // tankier ELITE kinds; the per-wave speed multiplier in
                // scheduleWave() ramps approach speed on top of this.
                case 8:  return { { EnemyKind::Aliaser, EnemyKind::DiveBomber, EnemyKind::Stutter,
                                    EnemyKind::Overdrive, EnemyKind::Clipper }, 5 };
                case 9:  return { { EnemyKind::Aliaser, EnemyKind::Phaser,
                                    EnemyKind::DiveBomber, EnemyKind::Wobble }, 5 };
                case 10: return { { EnemyKind::Overdrive, EnemyKind::Flanger, EnemyKind::Aliaser,
                                    EnemyKind::DiveBomber, EnemyKind::Limiter }, 6 };
                case 11: return { { EnemyKind::Resonator, EnemyKind::Flanger, EnemyKind::Phaser,
                                    EnemyKind::Aliaser, EnemyKind::DiveBomber, EnemyKind::Stutter }, 7 };
                default: return { { EnemyKind::Mudball }, 1 };
            }
        }

        // Per-kind base horizontal speed (px/s, negative = leftward).
        // Limiter gets a vertical velocity so its wall-bounce behaviour is real.
        float baseVx(EnemyKind k)
        {
            switch (k)
            {
                // Speeds eased in the 2026-05-25 engagement pass: density was
                // restored (screens are populated again) and survivability is
                // recovered here, via slower approach, rather than by emptying waves.
                // Slower movers give the player room to dodge between i-frame windows
                // even when the screen is busy -- engagement and survivability are
                // independent once contact damage is i-frame-gated (spec §6.1).
                // Eased ~35% from the prior pass: at the corrected 60 Hz tick the
                // old speeds left too many enemies escaping off-screen to ever
                // clear a wave perfectly. Slower approach = a perfect run is
                // achievable while the 12-wave length keeps it demanding.
                case EnemyKind::Mudball:     return -18.0f;
                case EnemyKind::Clipper:     return -30.0f;
                case EnemyKind::SilenceVoid: return -16.0f;
                case EnemyKind::Limiter:     return -10.0f;
                case EnemyKind::Aliaser:     return -40.0f;
                case EnemyKind::DiveBomber:  return  0.0f;   // tickDiveBomber drives its own x
                // New monsters (light) + elites (slower, tankier).
                case EnemyKind::Warble:      return -22.0f;
                case EnemyKind::Hiss:        return -20.0f;
                case EnemyKind::Crackle:     return -24.0f;
                case EnemyKind::Wobble:      return -18.0f;
                case EnemyKind::Stutter:     return -28.0f;
                case EnemyKind::Overdrive:   return -14.0f;
                case EnemyKind::Phaser:      return -16.0f;
                case EnemyKind::Flanger:     return -14.0f;
                case EnemyKind::Resonator:   return -12.0f;
                default:                     return -26.0f;
            }
        }

        void emitFormation(std::mt19937& rng, std::vector<WaveSchedule::Event>& out,
                           float t0, EnemyKind kind, Formation f, float speedMult)
        {
            std::uniform_real_distribution<float> yDist(20.0f, static_cast<float>(kFbH) - 20.0f);
            const float baseY = yDist(rng);
            const float vx = baseVx(kind) * speedMult;
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
        // Approach-speed ramp: gentle per-wave acceleration, capped so late
        // waves stay fast but fair. Reuses the same early-game kinds at higher
        // speed rather than only leaning on enemy count.
        const float speedMult = std::min(1.22f, 1.0f + 0.020f * static_cast<float>(waveIdx - 1));
        // Tighten the inter-formation gap as waves climb (denser pressure late).
        const float gapLo = std::max(2.6f, 3.0f - 0.04f * static_cast<float>(waveIdx - 1));
        const float gapHi = std::max(4.8f, 6.0f - 0.07f * static_cast<float>(waveIdx - 1));
        float t = 0.0f;
        for (int i = 0; i < spec.formations; ++i)
        {
            const auto f = static_cast<Formation>(fDist(rng));
            const auto k = spec.pool[kDist(rng)];
            emitFormation(rng, s.events, t, k, f, speedMult);
            t += std::uniform_real_distribution<float>(gapLo, gapHi)(rng);
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
