#pragma once

#include <cmath>

namespace bombo
{

// Waveform selectors. Saw and Pulse are band-limited via polyBLEP at the
// discontinuities; Tri is naive (1/n² rolloff makes aliasing inaudible at
// kick-range fundamentals); Sine has no discontinuities. Port of
// dsp/oscillator.rs.
constexpr int WAVE_SINE  = 0;
constexpr int WAVE_TRI   = 1;
constexpr int WAVE_SAW   = 2;
constexpr int WAVE_PULSE = 3;

class SineOsc
{
public:
    explicit SineOsc(float sampleRate = 48000.0f) : sampleRate_(sampleRate) {}

    void setSampleRate(float sr) noexcept { sampleRate_ = sr; }

    // Reset phase to the given offset. π/2 = cosine start = max amplitude.
    void trigger(float phaseOffset) noexcept { phase_ = phaseOffset; }

    // Sine-only legacy entry.
    float tick(float freq) noexcept
    {
        const float out = std::sin(phase_);
        advance(freq);
        return out;
    }

    // Selectable waveform with polyBLEP for saw/pulse.
    float tickWave(float freq, int waveform) noexcept
    {
        const float tau = 6.28318530717958647692f;
        const float t = phase_ / tau;
        float dt = std::abs(freq / sampleRate_);
        if (dt < 1e-6f) dt = 1e-6f;
        if (dt > 0.5f)  dt = 0.5f;

        float out;
        switch (waveform)
        {
            case WAVE_SINE:  out = std::sin(phase_);    break;
            case WAVE_TRI:   out = triangle(phase_);    break;
            case WAVE_SAW:   out = sawBlep(t, dt);      break;
            case WAVE_PULSE: out = pulseBlep(t, dt, 0.5f); break;
            default:         out = std::sin(phase_);    break;
        }
        advance(freq);
        return out;
    }

private:
    void advance(float freq) noexcept
    {
        constexpr float tau = 6.28318530717958647692f;
        phase_ += freq / sampleRate_ * tau;
        if (phase_ >= tau) phase_ -= tau;
    }

    static float triangle(float phase) noexcept
    {
        constexpr float tau = 6.28318530717958647692f;
        const float p = phase / tau;
        return p < 0.5f ? (4.0f * p - 1.0f) : (3.0f - 4.0f * p);
    }

    // PolyBLEP residual (Välimäki) — quadratic correction within one sample
    // of each discontinuity. Smooths the jump into a 2-sample ramp.
    static float polyBlep(float t, float dt) noexcept
    {
        if (t < dt)
        {
            const float x = t / dt;
            return x + x - x * x - 1.0f;
        }
        if (t > 1.0f - dt)
        {
            const float x = (t - 1.0f) / dt;
            return x * x + x + x + 1.0f;
        }
        return 0.0f;
    }

    static float sawBlep(float t, float dt) noexcept
    {
        // Descending saw: +1 at t=0, → -1 at t=1, jump UP back to +1.
        // PolyBLEP is added (vs subtracted for rising saw) at the upward jump.
        return (1.0f - 2.0f * t) + polyBlep(t, dt);
    }

    static float pulseBlep(float t, float dt, float duty) noexcept
    {
        const float naive = (t < duty) ? 1.0f : -1.0f;
        const float pRise = polyBlep(t, dt);
        float tFall = t + 1.0f - duty;
        tFall -= std::floor(tFall);
        const float pFall = polyBlep(tFall, dt);
        return naive + pRise - pFall;
    }

    float phase_ = 0.0f;
    float sampleRate_ = 48000.0f;
};

} // namespace bombo
