#pragma once

#include <cmath>
#include <cstdint>

namespace bombo
{

// White noise via LCG, then a 1-pole LP controlled by `color`:
//   color = 0 → dark (sub rumble, ~60 Hz cutoff)
//   color = 1 → bright (up to ~6 kHz)
// Deterministic across triggers — RNG resets to a fixed seed each trigger()
// so noise content is bit-identical hit-to-hit (no "ghost attack" jitter).
class NoiseGen
{
public:
    static constexpr uint32_t kSeed = 0x12345678u;

    explicit NoiseGen(float sampleRate = 48000.0f) : sampleRate_(sampleRate) {}

    void setSampleRate(float sr) noexcept { sampleRate_ = sr; }

    void trigger() noexcept
    {
        state_ = kSeed;
        lpZ_ = 0.0f;
    }

    float tick(float color) noexcept
    {
        state_ = state_ * 1664525u + 1013904223u;
        const float white = (static_cast<float>(state_) / 4294967295.0f) * 2.0f - 1.0f;

        // 60 Hz to 6 kHz log sweep on color [0,1]. Capped at 0.49·sr.
        constexpr float tau_c = 6.28318530717958647692f;
        float cutoffHz = 60.0f * std::pow(100.0f, color);
        const float maxHz = sampleRate_ * 0.49f;
        if (cutoffHz > maxHz) cutoffHz = maxHz;
        const float rc = 1.0f / (tau_c * cutoffHz);
        const float dt = 1.0f / sampleRate_;
        const float alpha = dt / (rc + dt);

        lpZ_ += alpha * (white - lpZ_);
        // Flush subnormals.
        if (std::fpclassify(lpZ_) == FP_SUBNORMAL) lpZ_ = 0.0f;
        return lpZ_;
    }

private:
    uint32_t state_ = kSeed;
    float lpZ_ = 0.0f;
    float sampleRate_ = 48000.0f;
};

} // namespace bombo
