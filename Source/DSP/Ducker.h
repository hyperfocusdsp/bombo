#pragma once

#include <cmath>

namespace bombo
{

// Envelope-follower wet-gate. Pulls the wet bus down in time with each
// kick onset. duck_gain = 1 - depth * env. depth=0 is bypass.
//
// GROWL: low-pass filter applied to wet BEFORE the gain reduction, keyed
// to the duck envelope. Deep duck → wet goes dark (~300 Hz LP); releasing
// duck → wet brightens back to full spectrum simultaneously. Dry kick path
// is completely unaffected. For techno: reverb/delay tails emerge from
// darkness into brightness as each kick decays — the "bloom" effect.
class Ducker
{
public:
    explicit Ducker(float sampleRate = 48000.0f) : sampleRate_(sampleRate)
    {
        setTimesMs(2.0f, 250.0f);
        updateGrowlAlpha();
    }

    void setSampleRate(float sr) noexcept
    {
        sampleRate_ = sr;
        setTimesMs(lastAtkMs_, lastRelMs_);
        setHoldMs(lastHoldMs_);
        updateGrowlAlpha();
    }

    // SHAPE: envelope curve. -1 = exponential (tight/punchy), 0 = linear, +1 = log (smooth).
    void setShape(float s) noexcept { shape_ = s < -1.0f ? -1.0f : (s > 1.0f ? 1.0f : s); }

    // GROWL: LP filter depth on the wet signal during duck. 0 = clean gain
    // reduction only. 1 = wet fully darkened at peak duck, brightens with release.
    void setGrowl(float g) noexcept { growl_ = g < 0.0f ? 0.0f : (g > 1.0f ? 1.0f : g); }

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
        lpZ_ = 0.0f;
    }

    float process(float trigger, float wet, float depth) noexcept
    {
        const float rect = std::abs(trigger);
        if (rect > env_)
        {
            env_ = rect + (env_ - rect) * atkCoef_;
            holdRemaining_ = holdSamplesTotal_;
        }
        else if (holdRemaining_ > 0)
        {
            --holdRemaining_;
        }
        else
        {
            env_ = rect + (env_ - rect) * relCoef_;
        }
        if (std::fpclassify(env_) == FP_SUBNORMAL) env_ = 0.0f;
        if (depth < 0.0f) depth = 0.0f;
        if (depth > 1.0f) depth = 1.0f;
        const float clamped = env_ > 1.0f ? 1.0f : env_;

        // SHAPE: curve the envelope before computing gain reduction.
        float envShaped = clamped;
        if (clamped > 0.0f && shape_ != 0.0f)
        {
            const float expo = shape_ >= 0.0f
                ? 1.0f - shape_ * 0.9f     // 1.0 → 0.1 (concave / smooth)
                : 1.0f + (-shape_) * 3.0f;  // 1.0 → 4.0 (convex / punchy)
            envShaped = std::pow(clamped, expo);
        }

        // GROWL: LP-filter wet keyed to duck depth, applied BEFORE gain
        // reduction. Wet darkens as duck deepens; brightens as it releases.
        // lpZ_ is a one-pole LP at ~300 Hz — slow enough to pass only warmth.
        // Transient is preserved: at attack peak, gain is near 0 so the
        // darkened wet is inaudible. During release the tail blooms from dark.
        if (growl_ > 0.0f)
        {
            const float t = growl_ * envShaped;
            lpZ_ = growlAlpha_ * wet + (1.0f - growlAlpha_) * lpZ_;
            if (std::fpclassify(lpZ_) == FP_SUBNORMAL) lpZ_ = 0.0f;
            wet = wet * (1.0f - t) + lpZ_ * t;
        }

        return wet * (1.0f - depth * envShaped);
    }

private:
    void updateGrowlAlpha() noexcept
    {
        // One-pole LP coefficient for ~300 Hz.
        constexpr float kCutHz = 300.0f;
        const float wc = 6.28318530f * kCutHz;
        growlAlpha_ = wc / (sampleRate_ + wc);
    }

    float sampleRate_ = 48000.0f;
    float env_ = 0.0f;
    float atkCoef_ = 0.0f, relCoef_ = 0.0f;
    int   holdSamplesTotal_ = 0;
    int   holdRemaining_    = 0;
    float lastAtkMs_ = 2.0f, lastRelMs_ = 250.0f, lastHoldMs_ = 0.0f;
    float shape_       = 0.0f;
    float growl_       = 0.0f;
    float growlAlpha_  = 0.0f;
    float lpZ_         = 0.0f;
};

} // namespace bombo
