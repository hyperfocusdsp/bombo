#pragma once

#include <cmath>

namespace bombo
{

// Master output stage — DC blocker + soft-knee feedback limiter.
// Ceiling -0.45 dBFS, knee starts at -1.9 dBFS, cubic Hermite blend.
// Port of dsp/master_bus.rs.
class MasterBus
{
public:
    static constexpr float kCeiling = 0.95f;
    static constexpr float kKnee = 0.15f;

    explicit MasterBus(float sampleRate = 48000.0f) { setSampleRate(sampleRate); }

    void setSampleRate(float sampleRate) noexcept
    {
        constexpr float pi = 3.14159265358979323846f;
        const float sr = sampleRate < 1.0f ? 1.0f : sampleRate;
        dcR_ = 1.0f - 2.0f * pi * 5.0f / sr;
        envAttackCoef_  = std::exp(-1.0f / (0.0005f * sr)); // 0.5 ms
        envReleaseCoef_ = std::exp(-1.0f / (0.060f  * sr)); // 60 ms
        gainAttackCoef_  = std::exp(-1.0f / (0.001f * sr)); // 1 ms
        gainReleaseCoef_ = std::exp(-1.0f / (0.080f * sr)); // 80 ms
    }

    void reset() noexcept
    {
        dcX1_ = dcY1_ = 0.0f;
        env_ = 0.0f;
        gain_ = 1.0f;
    }

    float process(float x) noexcept
    {
        // DC blocker.
        const float y = x - dcX1_ + dcR_ * dcY1_;
        dcX1_ = x; dcY1_ = y;

        // Envelope follower.
        const float absY = std::abs(y);
        const float envCoef = absY > env_ ? envAttackCoef_ : envReleaseCoef_;
        env_ = absY + envCoef * (env_ - absY);

        // Smoothed gain toward target.
        const float target = targetGain(env_);
        const float gCoef = target < gain_ ? gainAttackCoef_ : gainReleaseCoef_;
        gain_ = target + gCoef * (gain_ - target);

        // Soft-limit the result so any transient inside the 1 ms attack
        // window stays bounded without foldback.
        return softLimit(y * gain_);
    }

private:
    static float targetGain(float env) noexcept
    {
        const float kneeLo = kCeiling - kKnee;
        if (env <= kneeLo) return 1.0f;
        if (env >= kCeiling) return kCeiling / (env > 1e-9f ? env : 1e-9f);
        const float t = (env - kneeLo) / kKnee;
        const float smoothT = t * t * (3.0f - 2.0f * t);
        const float brick = kCeiling / (env > 1e-9f ? env : 1e-9f);
        return 1.0f + smoothT * (brick - 1.0f);
    }

    static float softLimit(float x) noexcept
    {
        const float ax = std::abs(x);
        const float kneeLo = kCeiling - kKnee;
        if (ax <= kneeLo) return x;
        const float maxAmp = kCeiling - kneeLo;
        const float excess = ax - kneeLo;
        const float sat = maxAmp * std::tanh(excess / maxAmp);
        return (x < 0.0f ? -1.0f : 1.0f) * (kneeLo + sat);
    }

    float dcX1_ = 0.0f, dcY1_ = 0.0f, dcR_ = 0.0f;
    float env_ = 0.0f;
    float envAttackCoef_ = 0.0f, envReleaseCoef_ = 0.0f;
    float gain_ = 1.0f;
    float gainAttackCoef_ = 0.0f, gainReleaseCoef_ = 0.0f;
};

} // namespace bombo
