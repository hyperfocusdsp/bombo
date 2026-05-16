#pragma once

#include <cmath>

#include "BiquadFilter.h"

namespace bombo
{

// 3-band techno-tuned multiband limiter. LR4 crossovers at 150 Hz and
// 3 kHz, per-band soft-knee feedback limiters. Default ON @ amount=0.5.
// Port of dsp/multiband_limiter.rs.
class MultibandLimiter
{
public:
    static constexpr float kXoverLo = 150.0f;
    static constexpr float kXoverHi = 3000.0f;
    static constexpr float kQButterworth = 0.70710677f;

    static constexpr float kLowThresh  = 0.50f; // -6 dB
    static constexpr float kMidThresh  = 0.70f; // -3 dB
    static constexpr float kHighThresh = 0.80f; // -2 dB
    static constexpr float kLowKnee  = 0.15f;
    static constexpr float kMidKnee  = 0.20f;
    static constexpr float kHighKnee = 0.10f;
    static constexpr float kLowAtkMs  = 1.0f;
    static constexpr float kMidAtkMs  = 3.0f;
    static constexpr float kHighAtkMs = 0.3f;
    static constexpr float kLowRelMs  = 80.0f;
    static constexpr float kMidRelMs  = 120.0f;
    static constexpr float kHighRelMs = 40.0f;
    static constexpr float kBypassEps = 1.0e-4f;

    explicit MultibandLimiter(float sampleRate = 48000.0f)
        : sampleRate_(sampleRate),
          low_  (sampleRate, kLowAtkMs,  kLowRelMs,  kLowThresh,  kLowKnee),
          mid_  (sampleRate, kMidAtkMs,  kMidRelMs,  kMidThresh,  kMidKnee),
          high_ (sampleRate, kHighAtkMs, kHighRelMs, kHighThresh, kHighKnee)
    {
        lowLp1_.setLpf(sampleRate, kXoverLo, kQButterworth);
        lowLp2_.setLpf(sampleRate, kXoverLo, kQButterworth);
        midhiHp1_.setHpf(sampleRate, kXoverLo, kQButterworth);
        midhiHp2_.setHpf(sampleRate, kXoverLo, kQButterworth);
        midLp1_.setLpf(sampleRate, kXoverHi, kQButterworth);
        midLp2_.setLpf(sampleRate, kXoverHi, kQButterworth);
        highHp1_.setHpf(sampleRate, kXoverHi, kQButterworth);
        highHp2_.setHpf(sampleRate, kXoverHi, kQButterworth);
    }

    void setSampleRate(float sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        lowLp1_.setLpf(sampleRate, kXoverLo, kQButterworth);
        lowLp2_.setLpf(sampleRate, kXoverLo, kQButterworth);
        midhiHp1_.setHpf(sampleRate, kXoverLo, kQButterworth);
        midhiHp2_.setHpf(sampleRate, kXoverLo, kQButterworth);
        midLp1_.setLpf(sampleRate, kXoverHi, kQButterworth);
        midLp2_.setLpf(sampleRate, kXoverHi, kQButterworth);
        highHp1_.setHpf(sampleRate, kXoverHi, kQButterworth);
        highHp2_.setHpf(sampleRate, kXoverHi, kQButterworth);
        low_.setSampleRate (sampleRate, kLowAtkMs,  kLowRelMs);
        mid_.setSampleRate (sampleRate, kMidAtkMs,  kMidRelMs);
        high_.setSampleRate(sampleRate, kHighAtkMs, kHighRelMs);
    }

    void reset() noexcept
    {
        lowLp1_.reset(); lowLp2_.reset();
        midhiHp1_.reset(); midhiHp2_.reset();
        midLp1_.reset(); midLp2_.reset();
        highHp1_.reset(); highHp2_.reset();
        low_.reset(); mid_.reset(); high_.reset();
    }

    float process(float x, bool on, float amount) noexcept
    {
        if (!on || amount < kBypassEps) return x;

        // LR4 split @ 150 Hz.
        const float low   = lowLp2_.process(lowLp1_.process(x));
        const float midhi = midhiHp2_.process(midhiHp1_.process(x));
        // LR4 split @ 3 kHz on the midhi signal.
        const float mid  = midLp2_.process(midLp1_.process(midhi));
        const float high = highHp2_.process(highHp1_.process(midhi));

        return low_.process(low,  amount)
             + mid_.process(mid,  amount)
             + high_.process(high, amount);
    }

private:
    struct BandLimiter
    {
        BandLimiter(float sr, float atkMs, float relMs, float baseThresh, float knee)
            : baseThreshold(baseThresh), knee_(knee)
        {
            setSampleRate(sr, atkMs, relMs);
        }

        void setSampleRate(float sr, float atkMs, float relMs) noexcept
        {
            if (sr < 1.0f) sr = 1.0f;
            envAtk_  = std::exp(-1.0f / (0.0002f * sr));
            envRel_  = std::exp(-1.0f / (relMs * 1.0e-3f * sr));
            gainAtk_ = std::exp(-1.0f / (atkMs * 1.0e-3f * sr));
            gainRel_ = std::exp(-1.0f / (relMs * 1.0e-3f * sr));
        }

        void reset() noexcept { env_ = 0.0f; gain_ = 1.0f; }

        float process(float x, float amount) noexcept
        {
            if (amount < 0.0f) amount = 0.0f;
            if (amount > 1.0f) amount = 1.0f;
            const float threshold = 1.0f - (1.0f - baseThreshold) * amount;
            const float absX = std::abs(x);
            const float envCoef = absX > env_ ? envAtk_ : envRel_;
            env_ = absX + envCoef * (env_ - absX);

            const float target = targetGain(env_, threshold, knee_);
            const float gCoef = target < gain_ ? gainAtk_ : gainRel_;
            gain_ = target + gCoef * (gain_ - target);
            return x * gain_;
        }

        static float targetGain(float env, float threshold, float knee) noexcept
        {
            const float kneeLo = threshold - knee;
            if (env <= kneeLo) return 1.0f;
            const float safeEnv = env > 1.0e-9f ? env : 1.0e-9f;
            if (env >= threshold) return threshold / safeEnv;
            const float t = (env - kneeLo) / knee;
            const float smoothT = t * t * (3.0f - 2.0f * t);
            const float brick = threshold / safeEnv;
            return 1.0f + smoothT * (brick - 1.0f);
        }

        float env_ = 0.0f, gain_ = 1.0f;
        float envAtk_ = 0.0f, envRel_ = 0.0f;
        float gainAtk_ = 0.0f, gainRel_ = 0.0f;
        float baseThreshold = 1.0f;
        float knee_ = 0.1f;
    };

    float sampleRate_;
    BiquadFilter lowLp1_, lowLp2_, midhiHp1_, midhiHp2_;
    BiquadFilter midLp1_, midLp2_, highHp1_, highHp2_;
    BandLimiter  low_, mid_, high_;
};

} // namespace bombo
