#pragma once

#include <cstdint>

namespace bombo
{

// Per-trigger analog drift. Three independent jitter axes gated by one
// drift_amount knob (0..1):
//   pitch_jitter ±0.8% (~±14 cents) — VCO tuning tolerance
//   amp_jitter   ±2.5%              — VCA / level-trim tolerance
//   decay_jitter ±5%                — envelope-chip RC tolerance
// All return exactly 1.0 at amount=0 (deterministic preserved).
struct DriftSample
{
    float ampScale = 1.0f;
    float decayScale = 1.0f;
};

class Drift
{
public:
    Drift() = default;

    // Returns pitch factor for this trigger.
    float pitchJitter(float amount) noexcept
    {
        return 1.0f + randBipolar() * amount * 0.008f;
    }

    // Sample amp + decay jitter together so the LCG sequence is
    // deterministic across refactors.
    DriftSample sampleEnvelope(float amount) noexcept
    {
        const float a = randBipolar();
        const float d = randBipolar();
        return DriftSample{ 1.0f + a * amount * 0.025f,
                            1.0f + d * amount * 0.05f };
    }

private:
    float randBipolar() noexcept
    {
        state_ = state_ * 1664525u + 1013904223u;
        return (static_cast<float>(state_) / 4294967295.0f) * 2.0f - 1.0f;
    }

    uint32_t state_ = 0xCAFEBABEu;
};

} // namespace bombo
