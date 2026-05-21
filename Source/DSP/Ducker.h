#pragma once

#include <cmath>

namespace bombo
{

// Envelope-follower wet-gate. Pulls the wet bus down in time with each
// kick onset. duck_gain = 1 - depth * env. depth=0 is bypass.
//
// HOLD: after each attack peak the env is pinned at the new peak for
// `holdMs` before release coefficient takes over.
class Ducker
{
public:
    static constexpr float kFlutterHz = 10.0f; // fixed tremolo rate during release

    explicit Ducker(float sampleRate = 48000.0f) : sampleRate_(sampleRate)
    {
        setTimesMs(2.0f, 250.0f);
        updateFlutterPhaseStep();
    }

    void setSampleRate(float sr) noexcept
    {
        sampleRate_ = sr;
        setTimesMs(lastAtkMs_, lastRelMs_);
        setHoldMs(lastHoldMs_);
        updateFlutterPhaseStep();
    }

    // SHAPE: envelope curve. -1 = exponential (tight/punchy), 0 = linear, +1 = log (smooth).
    void setShape(float s) noexcept { shape_ = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s); }

    // FLUTTER: tremolo depth applied during the release phase. 0 = smooth release.
    void setFlutter(float f) noexcept { flutter_ = f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f); }

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

    void reset() noexcept
    {
        env_ = 0.0f;
        holdRemaining_ = 0;
        inRelease_ = false;
        flutterPhase_ = 0.0f;
    }

    float process(float trigger, float wet, float depth) noexcept
    {
        const float rect = std::abs(trigger);
        if (rect > env_)
        {
            env_ = rect + (env_ - rect) * atkCoef_;
            holdRemaining_ = holdSamplesTotal_;
            inRelease_ = false;
        }
        else if (holdRemaining_ > 0)
        {
            --holdRemaining_;
        }
        else
        {
            // Entering release phase — reset flutter LFO on first sample.
            if (!inRelease_) { inRelease_ = true; flutterPhase_ = 0.0f; }
            env_ = rect + (env_ - rect) * relCoef_;
        }
        if (std::fpclassify(env_) == FP_SUBNORMAL) env_ = 0.0f;
        if (depth < 0.0f) depth = 0.0f;
        if (depth > 1.0f) depth = 1.0f;
        const float clamped = env_ > 1.0f ? 1.0f : env_;

        // SHAPE: curve the envelope value before computing gain reduction.
        float envShaped = clamped;
        if (clamped > 0.0f && shape_ != 0.0f)
        {
            const float expo = shape_ >= 0.0f
                ? 1.0f - shape_ * 0.9f    // 1.0 → 0.1 (concave / smooth)
                : 1.0f + (-shape_) * 3.0f; // 1.0 → 4.0 (convex / punchy)
            envShaped = std::pow(clamped, expo);
        }

        // FLUTTER: LFO tremolo during release. The oscillation ripples the
        // shaped envelope so the duck pumps rhythmically as it releases.
        if (inRelease_ && flutter_ > 0.0f && envShaped > 0.001f)
        {
            flutterPhase_ += flutterPhaseStep_;
            if (flutterPhase_ >= 6.28318530f) flutterPhase_ -= 6.28318530f;
            const float ripple = flutter_ * 0.5f * envShaped * std::sin(flutterPhase_);
            envShaped += ripple;
            if (envShaped < 0.0f) envShaped = 0.0f;
            if (envShaped > 1.0f) envShaped = 1.0f;
        }

        return wet * (1.0f - depth * envShaped);
    }

private:
    void updateFlutterPhaseStep() noexcept
    {
        flutterPhaseStep_ = kFlutterHz / sampleRate_ * 6.28318530f;
    }

    float sampleRate_ = 48000.0f;
    float env_ = 0.0f;
    float atkCoef_ = 0.0f, relCoef_ = 0.0f;
    int   holdSamplesTotal_ = 0;
    int   holdRemaining_    = 0;
    float lastAtkMs_ = 2.0f, lastRelMs_ = 250.0f, lastHoldMs_ = 0.0f;
    float shape_          = 0.0f;
    float flutter_        = 0.0f;
    float flutterPhase_    = 0.0f;
    float flutterPhaseStep_ = 0.0f;
    bool  inRelease_      = false;
};

} // namespace bombo
