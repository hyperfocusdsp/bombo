#pragma once

#include <cmath>

namespace bombo
{

// Envelope-follower wet-gate. Pulls the wet bus down in time with each
// kick onset. duck_gain = 1 - depth * env. depth=0 is bypass.
//
// HOLD: after each attack peak the env is pinned at the new peak for
// `holdMs` before release coefficient takes over. Lets the user dial in
// a rhythmic "pump" that stays committed past the natural release —
// essential for groove ducking when the trigger transient is shorter
// than the desired duck duration.
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
        setHoldMs(lastHoldMs_);
    }

    void setTimesMs(float attackMs, float releaseMs) noexcept
    {
        lastAtkMs_ = attackMs; lastRelMs_ = releaseMs;
        const float atkS = (attackMs < 0.1f ? 0.1f : attackMs) / 1000.0f;
        const float relS = (releaseMs < 1.0f ? 1.0f : releaseMs) / 1000.0f;
        atkCoef_ = std::exp(-1.0f / (atkS * sampleRate_));
        relCoef_ = std::exp(-1.0f / (relS * sampleRate_));
    }

    void setHoldMs(float holdMs) noexcept
    {
        lastHoldMs_ = holdMs;
        if (holdMs < 0.0f) holdMs = 0.0f;
        holdSamplesTotal_ = static_cast<int>(holdMs * 0.001f * sampleRate_);
    }

    void reset() noexcept { env_ = 0.0f; holdRemaining_ = 0; }

    float process(float trigger, float wet, float depth) noexcept
    {
        const float rect = std::abs(trigger);
        if (rect > env_)
        {
            // Attack: env tracks up toward rect; restart hold window.
            env_ = rect + (env_ - rect) * atkCoef_;
            holdRemaining_ = holdSamplesTotal_;
        }
        else if (holdRemaining_ > 0)
        {
            // Hold: freeze env at its current (post-attack) value.
            --holdRemaining_;
        }
        else
        {
            // Release: standard one-pole decay.
            env_ = rect + (env_ - rect) * relCoef_;
        }
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
    int   holdSamplesTotal_ = 0;
    int   holdRemaining_    = 0;
    float lastAtkMs_ = 2.0f, lastRelMs_ = 250.0f, lastHoldMs_ = 0.0f;
};

} // namespace bombo
