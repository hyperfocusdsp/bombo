#pragma once

#include <cmath>
#include <algorithm>

namespace bombo
{

// Exponential pitch envelope: sweeps from f_start DOWN to f_end.
//   f(t) = f_end + (f_start - f_end) * (1 - (t / sweep_dur)^curve)
// If f_start < f_end, they are swapped (kick always sweeps down).
class PitchEnvelope
{
public:
    explicit PitchEnvelope(float sampleRate = 48000.0f) { setSampleRate(sampleRate); }

    void setSampleRate(float sr) noexcept { dt_ = 1.0f / sr; }

    void trigger(float fStart, float fEnd, float sweepDurS, float curve) noexcept
    {
        if (fStart >= fEnd) { fStart_ = fStart; fEnd_ = fEnd; }
        else                { fStart_ = fEnd;   fEnd_ = fStart; }
        sweepDur_ = std::max(sweepDurS, 0.001f);
        curve_ = std::max(curve, 0.1f);
        t_ = 0.0f;
    }

    float tick() noexcept
    {
        if (t_ >= sweepDur_) return fEnd_;
        const float x = t_ / sweepDur_;
        const float shape = std::pow(x, curve_);
        const float freq = fEnd_ + (fStart_ - fEnd_) * (1.0f - shape);
        t_ += dt_;
        return freq;
    }

private:
    float fStart_ = 150.0f, fEnd_ = 45.0f;
    float sweepDur_ = 0.06f, curve_ = 3.0f;
    float t_ = 0.0f;
    float dt_ = 1.0f / 48000.0f;
};

// Quantize an envelope time-constant to ~16 levels per decade, lerped in
// by drift_amount. Approximates the cap-array stepping of analog envelope
// chips. drift_amount=0 returns tau unchanged.
inline float analogQuantizeTau(float tau, float driftAmount) noexcept
{
    if (driftAmount <= 0.0f || tau <= 0.0f) return tau;
    const float logTau = std::log10(tau);
    const float steppedLog = std::round(logTau * 16.0f) / 16.0f;
    const float stepped = std::pow(10.0f, steppedLog);
    return tau + (stepped - tau) * driftAmount;
}

// Linear-attack + exponential-decay amp envelope.
// gain(t) = attack_ramp(t) * exp(-(t - attack_time) / tau)
// where tau = decay_ms / 6.9078 so decay_ms reaches ~-60 dB.
class AmpEnvelope
{
public:
    explicit AmpEnvelope(float sampleRate = 48000.0f) { setSampleRate(sampleRate); }

    void setSampleRate(float sr) noexcept
    {
        sampleRate_ = sr;
        dt_ = 1.0f / sr;
        attackSamples_ = static_cast<int>(0.001f * sr);
        if (attackSamples_ < 1) attackSamples_ = 1;
    }

    void triggerFull(float decayMs, float attackMs, float driftAmount) noexcept
    {
        const float decayS = decayMs / 1000.0f;
        const float rawTau = std::max(decayS / 6.9078f, 0.0001f);
        tau_ = analogQuantizeTau(rawTau, driftAmount);
        const float minAtkMs = std::max(attackMs, 0.05f);
        int n = static_cast<int>((minAtkMs / 1000.0f) * sampleRate_);
        attackSamples_ = (n < 1) ? 1 : n;
        t_ = 0.0f;
        attackCounter_ = 0;
        active_ = true;
    }

    void trigger(float decayMs, float driftAmount) noexcept
    {
        triggerFull(decayMs, 1.0f, driftAmount);
    }

    float tick() noexcept
    {
        if (!active_) return 0.0f;
        float attackGain;
        if (attackCounter_ < attackSamples_)
        {
            attackGain = (static_cast<float>(attackCounter_) + 1.0f)
                       / static_cast<float>(attackSamples_);
            ++attackCounter_;
        }
        else
        {
            attackGain = 1.0f;
        }
        const float decayGain = std::exp(-t_ / tau_);
        t_ += dt_;
        const float g = attackGain * decayGain;
        if (g < 0.0001f) { active_ = false; return 0.0f; }
        return g;
    }

    bool isActive() const noexcept { return active_; }

private:
    float tau_ = 0.06f;
    float t_ = 0.0f;
    float dt_ = 1.0f / 48000.0f;
    float sampleRate_ = 48000.0f;
    int attackSamples_ = 48;
    int attackCounter_ = 0;
    bool active_ = false;
};

} // namespace bombo
