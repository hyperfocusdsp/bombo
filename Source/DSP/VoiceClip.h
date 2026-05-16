#pragma once

#include <cmath>

namespace bombo
{

// Per-voice soft-clip stage. Sits between layer mix (SUB+MID+click+noise)
// and the final amp-env multiplication so the shaper sees raw oscillator
// sum at full level — adds harmonics that survive into the tail.
//
// Distinct-by-construction palette (no two modes are the same family):
//   0 OFF   — pass-through, zero cost
//   1 TANH  — symmetric, odd harmonics
//   2 DIODE — asymmetric (pos compresses faster than neg) — even-rich,
//             2nd-harmonic dominant; matches TR-909 signature
//   3 CUBIC — x - x³/3 fast soft-clip, 3rd harmonic only
// All return identity at drive=0 — every preset's harmonics survive a
// drive automation pass through zero.
constexpr int VC_OFF   = 0;
constexpr int VC_TANH  = 1;
constexpr int VC_DIODE = 2;
constexpr int VC_CUBIC = 3;

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
        default:
            return x;
    }
}

} // namespace bombo
