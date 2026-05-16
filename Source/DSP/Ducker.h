#pragma once

#include <cmath>

namespace bombo
{

// Envelope-follower wet-gate. Pulls the wet bus down in time with each
// kick onset. duck_gain = 1 - depth * env. depth=0 is bypass.
class Ducker
{
public:
    explicit Ducker(float sampleRate = 48000.0f) : sampleRate_(sampleRate)
    {
        setTimesMs(2.0f, 250.0f);
    }

    void setSampleRate(float sr) noexcept
    {
        sampleRate_ = sr;
        setTimesMs(lastAtkMs_, lastRelMs_);
    }

    void setTimesMs(float attackMs, float releaseMs) noexcept
    {
        lastAtkMs_ = attackMs; lastRelMs_ = releaseMs;
        const float atkS = (attackMs < 0.1f ? 0.1f : attackMs) / 1000.0f;
        const float relS = (releaseMs < 1.0f ? 1.0f : releaseMs) / 1000.0f;
        atkCoef_ = std::exp(-1.0f / (atkS * sampleRate_));
        relCoef_ = std::exp(-1.0f / (relS * sampleRate_));
    }

    void reset() noexcept { env_ = 0.0f; }

    float process(float trigger, float wet, float depth) noexcept
    {
        const float rect = std::abs(trigger);
        const float coef = rect > env_ ? atkCoef_ : relCoef_;
        env_ = rect + (env_ - rect) * coef;
        if (std::fpclassify(env_) == FP_SUBNORMAL) env_ = 0.0f;
        if (depth < 0.0f) depth = 0.0f;
        if (depth > 1.0f) depth = 1.0f;
        const float clamped = env_ > 1.0f ? 1.0f : env_;
        return wet * (1.0f - depth * clamped);
    }

private:
    float sampleRate_ = 48000.0f;
    float env_ = 0.0f;
    float atkCoef_ = 0.0f, relCoef_ = 0.0f;
    float lastAtkMs_ = 2.0f, lastRelMs_ = 250.0f;
};

} // namespace bombo
