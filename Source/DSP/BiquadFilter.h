#pragma once

#include <cmath>

namespace bombo
{

// Zavalishin trapezoidal-integration SVF (Cytomic-style). Clean LP/BP/HP
// from one shared state; integrator feedback is a natural place to insert
// non-linearity for analog drive. Direct port of dsp/filter.rs.
class BiquadFilter
{
public:
    enum Mode { Passthrough, Lp, Hp, Peak };

    BiquadFilter() { recomputeCoefficients(); }

    void reset() noexcept { ic1eq_ = 0.0f; ic2eq_ = 0.0f; }

    void setLpf(float sr, float cutoffHz, float q) noexcept
    {
        mode_ = Lp; sampleRate_ = sr; cutoffHz_ = cutoffHz; q_ = q;
        recomputeCoefficients();
    }
    void setHpf(float sr, float cutoffHz, float q) noexcept
    {
        mode_ = Hp; sampleRate_ = sr; cutoffHz_ = cutoffHz; q_ = q;
        recomputeCoefficients();
    }
    void setPeak(float sr, float cutoffHz, float q, float gainDb) noexcept
    {
        peakGain_ = std::pow(10.0f, gainDb / 20.0f);
        mode_ = Peak; sampleRate_ = sr; cutoffHz_ = cutoffHz; q_ = q;
        recomputeCoefficients();
    }
    void setPassthrough() noexcept { mode_ = Passthrough; }
    void setDrive(float drive) noexcept
    {
        drive_ = drive < 0.0f ? 0.0f : (drive > 1.0f ? 1.0f : drive);
    }

    float process(float input) noexcept
    {
        if (mode_ == Passthrough) return input;

        const float v3 = input - ic2eq_;
        const float v1 = a1_ * ic1eq_ + a2_ * v3;
        const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;

        float v1Fb = v1;
        if (drive_ > 0.0f)
        {
            const float pre = 1.0f + drive_;
            const float sat = std::tanh(pre * v1) / pre;
            v1Fb = (1.0f - drive_) * v1 + drive_ * sat;
        }

        ic1eq_ = flushDenormal(2.0f * v1Fb - ic1eq_);
        ic2eq_ = flushDenormal(2.0f * v2 - ic2eq_);

        switch (mode_)
        {
            case Lp:   return v2;
            case Hp:   return input - k_ * v1 - v2;
            case Peak: return input + (peakGain_ - 1.0f) * k_ * v1;
            default:   return input;
        }
    }

private:
    void recomputeCoefficients() noexcept
    {
        constexpr float pi = 3.14159265358979323846f;
        float cutoff = cutoffHz_;
        const float maxHz = sampleRate_ * 0.45f;
        if (cutoff < 20.0f) cutoff = 20.0f;
        if (cutoff > maxHz) cutoff = maxHz;
        float q = q_ < 0.1f ? 0.1f : q_;
        const float g = std::tan(pi * cutoff / sampleRate_);
        const float k = 1.0f / q;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        g_ = g; k_ = k; a1_ = a1; a2_ = g * a1; a3_ = g * a2_;
    }

    static float flushDenormal(float x) noexcept
    {
        return std::fpclassify(x) == FP_SUBNORMAL ? 0.0f : x;
    }

    Mode  mode_ = Passthrough;
    float sampleRate_ = 48000.0f;
    float cutoffHz_ = 1000.0f;
    float q_ = 0.707f;
    float drive_ = 0.0f;
    float peakGain_ = 1.0f;
    float g_ = 0.0f, k_ = 0.0f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1eq_ = 0.0f, ic2eq_ = 0.0f;
};

} // namespace bombo
