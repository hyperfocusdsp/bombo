#pragma once

#include <cmath>

namespace bombo
{

// Per-voice soft-clip stage. Sits between layer mix (SUB+MID+click+noise)
// and the final amp-env multiplication so the shaper sees raw oscillator
// sum at full level - adds harmonics that survive into the tail.
//
// Distinct-by-construction palette (no two modes are the same family):
//   0 OFF   - pass-through, zero cost
//   1 TANH  - symmetric, odd harmonics
//   2 DIODE - asymmetric (pos compresses faster than neg) - even-rich,
//             2nd-harmonic dominant; matches TR-909 signature
//   3 CUBIC - x - x³/3 fast soft-clip, 3rd harmonic only
//   4 FUZZ  - hard clip: all harmonics equally, brutal transient crush
//   5 FOLD  - bipolar wave folder: metallic/industrial, Buchla-style
//   6 PHASE - power-law bend: compresses near zero-crossing, CZ-like resonant character
//   7 RECT  - full-wave rectify: octave doubler (DC filtered downstream by HPF)
//   8 CRUSH - bit-depth reduction: quantisation grit, lo-fi staircase
// All return identity at drive=0 - every preset's harmonics survive a
// drive automation pass through zero.
constexpr int VC_OFF   = 0;
constexpr int VC_TANH  = 1;
constexpr int VC_DIODE = 2;
constexpr int VC_CUBIC = 3;
constexpr int VC_FUZZ  = 4;
constexpr int VC_FOLD  = 5;
constexpr int VC_PHASE = 6;
constexpr int VC_RECT  = 7;
constexpr int VC_CRUSH = 8;

// Bipolar triangle wave folder - reflects x back at ±1 boundaries.
static inline float wfold(float x) noexcept
{
    x = std::fmod(x + 1.0f, 4.0f);
    if (x < 0.0f) x += 4.0f;
    return x < 2.0f ? x - 1.0f : 3.0f - x;
}

inline float voiceClipApply(int mode, float drive, float x) noexcept
{
    if (drive <= 0.0f || mode == VC_OFF) return x;
    if (drive > 1.0f) drive = 1.0f;

    switch (mode)
    {
        case VC_TANH:
        {
            const float g = 1.0f + drive * 15.0f;
            return std::tanh(x * g);
        }
        case VC_DIODE:
        {
            const float g = 1.0f + drive * 15.0f;
            return x >= 0.0f ? std::tanh(x * g)
                             : std::tanh(x * g * 0.6f);
        }
        case VC_CUBIC:
        {
            const float g = 1.0f + drive * 11.0f;
            float xg = x * g;
            if (xg < -1.0f) xg = -1.0f;
            if (xg >  1.0f) xg =  1.0f;
            return xg - (xg * xg * xg) / 3.0f;
        }
        case VC_FUZZ:
        {
            const float g = 1.0f + drive * 15.0f;
            const float xg = x * g;
            return xg < -1.0f ? -1.0f : xg > 1.0f ? 1.0f : xg;
        }
        case VC_FOLD:
        {
            const float g = 1.0f + drive * 5.0f;
            return wfold(x * g);
        }
        case VC_PHASE:
        {
            // Power-law bend: compresses near zero, stretches peaks
            // (CZ phase-distortion character applied as a transfer function)
            const float pw = 1.0f / (1.0f + drive * 2.5f);
            const float ax = std::abs(x);
            const float sign = x >= 0.0f ? 1.0f : -1.0f;
            return ax < 1e-6f ? x : sign * std::pow(ax, pw);
        }
        case VC_RECT:
        {
            // Full-wave rectify - creates DC + strong 2nd harmonic (octave doubler).
            // HPF downstream removes DC; use at low mix to layer an octave-up texture.
            const float g = 1.0f + drive * 8.0f;
            const float xg = x * g;
            const float clipped = xg < -1.0f ? 1.0f : xg > 1.0f ? 1.0f : std::abs(xg);
            return clipped;
        }
        case VC_CRUSH:
        {
            // Bit-depth reduction: drive=0.1 → ~10-bit subtle grit, drive=1 → ~3-bit lo-fi
            const float bits = std::round(3.0f + (1.0f - drive) * 7.0f);
            const float steps = std::pow(2.0f, bits);
            return std::round(x * steps) / steps;
        }
        default:
            return x;
    }
}

} // namespace bombo
