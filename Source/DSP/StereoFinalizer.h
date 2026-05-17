#pragma once

#include <vector>

#include "BiquadFilter.h"

namespace bombo
{

// Hidden EDM finalizer (no UI, always on). Two effects in one pass:
//
//   1. SUB-MONO: everything below kSubMonoHz is forced to mono. Both
//      output channels get the identical low-band signal. Industry
//      best practice for kicks + 808s — keeps the bass tight on club
//      systems and preserves vinyl-cut compatibility. 120 Hz is the
//      crossover most mastering houses use (Elliptical EQ, bx_solo,
//      iZotope Imager all default around there).
//
//   2. UPPER-BAND HAAS WIDTH: the >120 Hz content is L = direct,
//      R = small delay. A 6 ms Haas time gives a perceptible
//      stereo image on the wet FX (reverb/delay tails, drive
//      bite) without making the kick body feel split between
//      speakers. 6 ms is below the precedence-effect threshold
//      (~12 ms) so listeners still hear a single sound stage,
//      and the comb-filter risk when summed to mono is benign
//      (first null at 1 / (2 × 6 ms) = ~83 Hz which is below
//      the sub-mono crossover anyway).
//
// Input is mono (Bombo's wet bus is single-channel after the chain).
// Output is true stereo. The sub band stays bit-identical between
// channels — the only stereo information is in the upper band Haas.
class StereoFinalizer
{
public:
    static constexpr float kSubMonoHz   = 120.0f;   // crossover
    static constexpr float kHaasMs      = 6.0f;     // R-channel delay
    static constexpr float kCrossoverQ  = 0.707f;   // Butterworth

    void prepare(float sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        hp_.setHpf(sampleRate_, kSubMonoHz, kCrossoverQ);
        const std::size_t haasSamples = static_cast<std::size_t>(
            (kHaasMs * 0.001f * sampleRate_) + 1.0f);
        haasBuf_.assign(haasSamples + 4, 0.0f);
        haasPos_ = 0;
        haasDelaySamples_ = static_cast<int>(kHaasMs * 0.001f * sampleRate_);
        if (haasDelaySamples_ < 1) haasDelaySamples_ = 1;
        hp_.reset();
    }

    void reset() noexcept
    {
        hp_.reset();
        std::fill(haasBuf_.begin(), haasBuf_.end(), 0.0f);
        haasPos_ = 0;
    }

    // Single-sample stereo finalize. Mono in, L + R out.
    void process(float in, float& outL, float& outR) noexcept
    {
        // HP → upper band; the low band = in - upper (subtractive split,
        // phase-coherent, sums back to the input bit-exactly when no
        // Haas is applied).
        const float upper = hp_.process(in);
        const float lower = in - upper;

        // Haas delay on R-channel upper-band only. Buffer is a tiny
        // circular array, sized in prepare(). No allocation here.
        const int bufLen = static_cast<int>(haasBuf_.size());
        haasBuf_[(std::size_t) haasPos_] = upper;
        int readIdx = haasPos_ - haasDelaySamples_;
        if (readIdx < 0) readIdx += bufLen;
        const float upperDelayed = haasBuf_[(std::size_t) readIdx];
        haasPos_ = (haasPos_ + 1) % bufLen;

        outL = lower + upper;
        outR = lower + upperDelayed;
    }

private:
    BiquadFilter hp_;
    std::vector<float> haasBuf_;
    int   haasPos_           = 0;
    int   haasDelaySamples_  = 0;
    float sampleRate_        = 48000.0f;
};

} // namespace bombo
