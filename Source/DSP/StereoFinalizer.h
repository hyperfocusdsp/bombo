#pragma once

#include <vector>

#include "BiquadFilter.h"

namespace bombo
{

// Hidden EDM finalizer (no UI, always on).
//
// SUB-MONO: everything below kSubMonoHz is forced to mono — both
// output channels get the identical low-band signal. Industry best
// practice for kicks + 808s (Elliptical EQ / bx_solo / iZotope Imager
// all default around 120 Hz). For Bombo today this is effectively a
// no-op since the upstream chain is mono, but the architecture stays
// in place so when stereo content arrives (future stereo reverb /
// chorus / etc.), the sub will be auto-mono'd at the master.
//
// HAAS WIDENING REMOVED 2026-05-17: the previous 6 ms R-channel
// Haas created an audible left-leaning image (no-delay channel is
// perceived as the source). User flagged "delay sounds more L than
// R" — that was this. Pure mono out for now; if a future stereo
// widener is desired it must be SYMMETRIC (e.g. complementary
// all-pass decorrelation), not Haas-style asymmetric delay.
class StereoFinalizer
{
public:
    static constexpr float kSubMonoHz   = 120.0f;
    static constexpr float kCrossoverQ  = 0.707f;

    void prepare(float sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        hp_.setHpf(sampleRate_, kSubMonoHz, kCrossoverQ);
        hp_.reset();
    }

    void reset() noexcept
    {
        hp_.reset();
    }

    // Single-sample stereo finalize. Mono in, L + R out (currently
    // identical; the sub-mono crossover still rides so any future
    // stereo content above 120 Hz gets the proper Elliptical-EQ
    // treatment without revisiting this path).
    void process(float in, float& outL, float& outR) noexcept
    {
        const float upper = hp_.process(in);
        const float lower = in - upper;
        const float out   = lower + upper;
        outL = out;
        outR = out;
    }

private:
    BiquadFilter hp_;
    float sampleRate_ = 48000.0f;
};

} // namespace bombo
