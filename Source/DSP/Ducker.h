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
        snapCoef_ = std::exp(-1.0f / (0.05f * sampleRate)); // ~50 ms snap decay
    }

    void setSampleRate(float sr) noexcept
    {
        sampleRate_ = sr;
        setTimesMs(lastAtkMs_, lastRelMs_);
        setHoldMs(lastHoldMs_);
        snapCoef_ = std::exp(-1.0f / (0.05f * sr));
    }

    // SHAPE: envelope curve. -1 = exponential (tight/punchy), 0 = linear, +1 = log (smooth).
    void setShape(float s) noexcept { shape_ = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s); }

    // SNAP: brief gain overshoot above unity on duck release — "air rushing back in".
    void setSnap(float s) noexcept { snap_ = s < 0.0f ? 0.0f : (s > 1.0f ? 1.0f : s); }

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

    void reset() noexcept { env_ = 0.0f; holdRemaining_ = 0; snapEnv_ = 0.0f; snapArmed_ = false; }

    float process(float trigger, float wet, float depth) noexcept
    {
        const float rect = std::abs(trigger);
        if (rect > env_)
        {
            // Attack: env tracks up toward rect; restart hold window.
            env_ = rect + (env_ - rect) * atkCoef_;
            holdRemaining_ = holdSamplesTotal_;
            snapArmed_ = true; // duck is active — prime snap for release
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

        // SHAPE: curve the envelope value before computing gain reduction.
        // shape < 0 → convex/exponential (snappy, tight).
        // shape > 0 → concave/logarithmic (smooth, rounded pump).
        float envShaped = clamped;
        if (clamped > 0.0f && shape_ != 0.0f)
        {
            const float expo = shape_ >= 0.0f
                ? 1.0f - shape_ * 0.9f   // 1.0 → 0.1 (concave)
                : 1.0f + (-shape_) * 3.0f; // 1.0 → 4.0 (convex)
            envShaped = std::pow(clamped, expo);
        }

        // SNAP: brief gain overshoot above unity when the duck releases.
        // Fires once per duck event on the falling edge (clamped crosses floor).
        if (snap_ > 0.0f)
        {
            if (snapArmed_ && clamped < 0.01f)
            {
                snapEnv_ = snap_ * 0.35f; // max ~+3 dB at snap=1
                snapArmed_ = false;
            }
            snapEnv_ *= snapCoef_;
            if (std::fpclassify(snapEnv_) == FP_SUBNORMAL) snapEnv_ = 0.0f;
        }

        return wet * (1.0f - depth * envShaped + snapEnv_);
    }

private:
    float sampleRate_ = 48000.0f;
    float env_ = 0.0f;
    float atkCoef_ = 0.0f, relCoef_ = 0.0f;
    int   holdSamplesTotal_ = 0;
    int   holdRemaining_    = 0;
    float lastAtkMs_ = 2.0f, lastRelMs_ = 250.0f, lastHoldMs_ = 0.0f;
    float shape_    = 0.0f;  // -1..+1 envelope curve
    float snap_     = 0.0f;  // 0..1 post-release overshoot
    float snapEnv_  = 0.0f;  // overshoot envelope state
    float snapCoef_ = 0.0f;  // ~50 ms decay coefficient
    bool  snapArmed_ = false;
};

} // namespace bombo
