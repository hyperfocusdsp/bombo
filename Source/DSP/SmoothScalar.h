#pragma once

#include <cmath>

namespace bombo
{

// Sample-rate, one-pole linear smoother for an audio-thread scalar.
//
// Why this exists: every FX parameter setter (decayCoef, delay tap,
// feedback gain, LP coef …) used to write its value directly into the
// member consumed by process(). A knob tweak mid-tail therefore caused
// an instantaneous DSP state jump that the ear heard as a glitch. Wrapping
// the consumed value in a SmoothScalar lets the setter post a *target*,
// while process() pulls a per-sample one-pole-ramped value — perceived
// as a smooth glide regardless of how fast the user spins the knob.
//
// Lighter than juce::SmoothedValue (no per-instance ramp counter or
// blockSize state, just two floats + a coefficient). Suitable for any
// scalar where exponential-toward-target is musically acceptable —
// gains, filter coefficients, decay rates, fractional sample taps.
//
// Usage:
//   SmoothScalar fb_;
//   // in prepareToPlay / setSampleRate:
//   fb_.prepare(sr, 0.025f);    // 25 ms time constant
//   fb_.snap(0.0f);             // initial value, no glide on startup
//   // in setter:
//   fb_.setTarget(newValue);
//   // in process() per sample:
//   const float fb = fb_.next();
struct SmoothScalar
{
    float target  = 0.0f;
    float current = 0.0f;
    float coef    = 1.0f;  // 1.0 = snap immediately, 0.0 = never moves

    void prepare(float sampleRate, float tauSec) noexcept
    {
        // One-pole step: cur += coef * (target - cur). With coef as below,
        // current reaches ~63% of target after `tauSec` seconds and ~99%
        // after ~5*tauSec. 25 ms is the sweet spot for FX params: short
        // enough to feel like the knob is "live", long enough to mask
        // zipper noise + topology pops.
        if (tauSec <= 0.0f || sampleRate <= 0.0f) { coef = 1.0f; return; }
        coef = 1.0f - std::exp(-1.0f / (tauSec * sampleRate));
    }

    // Force current == target, skipping the ramp. Use on reset / prepareToPlay
    // so the FX doesn't audibly glide up from 0 at plugin load.
    void snap(float v) noexcept { target = current = v; }

    void setTarget(float v) noexcept { target = v; }

    // Step the smoother one sample and return the current value.
    float next() noexcept
    {
        current += coef * (target - current);
        // Denormal guard — FX chains run continuously; a stuck-near-zero
        // current would otherwise compound CPU cost over long sessions.
        if (std::fpclassify(current) == FP_SUBNORMAL) current = 0.0f;
        return current;
    }

    // Read without stepping — for places that need the same value twice
    // in the same sample (rare, but cheap to support).
    float peek() const noexcept { return current; }
};

} // namespace bombo
