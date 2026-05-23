#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <cstdint>

namespace bombo
{
namespace ir
{

enum Algo : int
{
    Room    = 0,
    Plate   = 1,
    Hall    = 2,
    Spring  = 3,
    Chamber = 4,
    Bunker  = 5,
    kNumAlgos
};

inline constexpr std::array<const char*, kNumAlgos> kAlgoNames = {
    "Room", "Plate", "Hall", "Spring", "Chamber", "Bunker"
};

// Per-algo nominal tail length in seconds. The synthesized buffer is
// exactly round(seconds * sampleRate) samples long.
inline constexpr std::array<float, kNumAlgos> kAlgoLengthSec = {
    0.40f,   // Room
    1.00f,   // Plate
    2.00f,   // Hall
    0.80f,   // Spring
    1.20f,   // Chamber
    0.60f    // Bunker
};

// Per-algo PRNG seeds — different seeds per algo so each tail has its
// own grain. Bit-identical across runs because the PRNG is seeded and
// the synthesis is purely arithmetic.
inline constexpr std::array<std::uint32_t, kNumAlgos> kAlgoSeeds = {
    0x6B6F6D62u, // Room   "komb"
    0x506C6174u, // Plate  "Plat"
    0x48616C6Cu, // Hall   "Hall"
    0x53707274u, // Spring "Sprt"
    0x4368616Du, // Chamber"Cham"
    0x42756E6Bu  // Bunker "Bunk"
};

// Tiny PRNG — xorshift32. Deterministic, allocation-free, used at
// synthesis time only (not in the audio path). Returns a uniform float
// in [-1, 1].
struct XorShift32
{
    std::uint32_t state;

    explicit XorShift32 (std::uint32_t seed) noexcept : state (seed == 0 ? 0xDEADBEEFu : seed) {}

    float next() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        // Map to [-1, 1] via 24-bit cast.
        const std::uint32_t m = state & 0xFFFFFFu;
        return (static_cast<float> (m) / 8388607.5f) - 1.0f;
    }
};

// One-pole LP, used at synthesis time to shape spectral character.
struct LP1
{
    float a = 0.5f;
    float z = 0.0f;
    void reset() noexcept { z = 0.0f; }
    void setHz (float cutHz, float sr) noexcept
    {
        const float x = std::exp (-6.28318530717958f * cutHz / sr);
        a = 1.0f - x;
    }
    float process (float x) noexcept
    {
        z += a * (x - z);
        return z;
    }
};

// One-pole HP — for plate / spring brightness.
struct HP1
{
    float a = 0.5f;
    float zx = 0.0f, zy = 0.0f;
    void reset() noexcept { zx = zy = 0.0f; }
    void setHz (float cutHz, float sr) noexcept
    {
        const float x = std::exp (-6.28318530717958f * cutHz / sr);
        a = x;
    }
    float process (float x) noexcept
    {
        const float y = a * (zy + x - zx);
        zx = x; zy = y;
        return y;
    }
};

// Synthesize an IR for the given algo at the given sample rate. The
// returned buffer is single-channel (mono), exactly the canonical length
// for the algo. Peak is normalised to 0.5 so downstream convolution
// produces a sane wet level for a hot kick input. Determinism: identical
// output bytes across runs and machines given the same algo + sample
// rate (seeded xorshift, no time or RNG library calls).
inline juce::AudioBuffer<float> synthesizeIR (int algo, float sampleRate) noexcept
{
    if (algo < 0 || algo >= kNumAlgos) algo = Hall;

    const float sec = kAlgoLengthSec[(std::size_t) algo];
    const int   N   = juce::jmax (16, (int) std::round (sec * sampleRate));

    juce::AudioBuffer<float> buf (1, N);
    buf.clear();
    auto* w = buf.getWritePointer (0);

    XorShift32 rng { kAlgoSeeds[(std::size_t) algo] };

    // Pre-build the algo-specific spectral shaper before the loop so the
    // hot path is just a few MACs per sample.
    LP1 lpShape; LP1 lpExtra;
    HP1 hpShape;

    switch (algo)
    {
        case Room:
        {
            // Dense early reflections + short exponential decay. Top-heavy
            // so the kick's low rumble doesn't muddy.
            const float tau = 0.10f;
            lpShape.setHz (8000.0f, sampleRate);
            // Sprinkle 6 discrete early reflections in the first 20 ms,
            // each ~3-9 dB below dry.
            constexpr int kERCount = 6;
            const int erSamples[kERCount] = {
                (int) (0.0025f * sampleRate),
                (int) (0.0048f * sampleRate),
                (int) (0.0073f * sampleRate),
                (int) (0.0105f * sampleRate),
                (int) (0.0142f * sampleRate),
                (int) (0.0188f * sampleRate)
            };
            const float erGains[kERCount] = { 0.85f, 0.62f, 0.55f, 0.42f, 0.36f, 0.28f };

            for (int i = 0; i < N; ++i)
            {
                const float t = (float) i / sampleRate;
                const float env = std::exp (-t / tau);
                float s = rng.next() * env;
                s = lpShape.process (s);
                w[i] = s;
            }
            // Layer the early reflections on top (alternating polarity).
            for (int k = 0; k < kERCount; ++k)
            {
                const int idx = erSamples[k];
                if (idx >= 0 && idx < N)
                    w[idx] += (k & 1 ? -1.0f : 1.0f) * erGains[k];
            }
            break;
        }

        case Plate:
        {
            // Smooth, bright, exponentially decaying noise with HP cut so
            // the tail sits above the kick fundamental.
            const float tau = 0.40f;
            hpShape.setHz (200.0f, sampleRate);
            lpShape.setHz (9000.0f, sampleRate);
            // Plate "shimmer" — 3 ms attack ramp at the head so the tail
            // doesn't slam in with the convolution impulse.
            const int attackSamples = (int) (0.003f * sampleRate);
            for (int i = 0; i < N; ++i)
            {
                const float t = (float) i / sampleRate;
                const float env = std::exp (-t / tau);
                float s = rng.next() * env;
                s = hpShape.process (s);
                s = lpShape.process (s);
                if (i < attackSamples)
                    s *= (float) i / (float) attackSamples;
                w[i] = s;
            }
            break;
        }

        case Hall:
        {
            // Slow build-up + long exponential, gentle LP. The signature
            // "big room" sound.
            const float tau = 0.70f;
            const float buildTau = 0.05f;
            lpShape.setHz (6500.0f, sampleRate);
            for (int i = 0; i < N; ++i)
            {
                const float t = (float) i / sampleRate;
                const float build = 1.0f - std::exp (-t / buildTau);
                const float decay = std::exp (-t / tau);
                float s = rng.next() * build * decay;
                s = lpShape.process (s);
                w[i] = s;
            }
            break;
        }

        case Spring:
        {
            // Chirped exponential + flutter — the characteristic spring
            // "boing". Chirp goes from ~3 kHz down to ~400 Hz over 200 ms.
            const float tau = 0.30f;
            const float f0  = 3000.0f;
            const float f1  = 400.0f;
            const float chirpDur = 0.20f;
            lpShape.setHz (5500.0f, sampleRate);
            for (int i = 0; i < N; ++i)
            {
                const float t = (float) i / sampleRate;
                const float decay = std::exp (-t / tau);
                const float u = juce::jlimit (0.0f, 1.0f, t / chirpDur);
                const float instFreq = f0 + (f1 - f0) * u;
                // Quadratic-phase chirp ∫f(t)dt = f0*t + (f1-f0)*t^2/(2*chirpDur)
                const float phase = 6.28318530717958f * (f0 * t + (f1 - f0) * t * t * 0.5f / chirpDur);
                const float chirp = std::sin (phase) * 0.65f;
                const float noise = rng.next() * 0.35f;
                float s = (chirp + noise) * decay;
                s = lpShape.process (s);
                w[i] = s;
            }
            break;
        }

        case Chamber:
        {
            // Plate-like but darker, slower decay — like a hardware
            // chamber unit with felt walls.
            const float tau = 0.50f;
            hpShape.setHz (120.0f, sampleRate);
            lpShape.setHz (3500.0f, sampleRate);
            const int attackSamples = (int) (0.005f * sampleRate);
            for (int i = 0; i < N; ++i)
            {
                const float t = (float) i / sampleRate;
                const float env = std::exp (-t / tau);
                float s = rng.next() * env;
                s = hpShape.process (s);
                s = lpShape.process (s);
                if (i < attackSamples)
                    s *= (float) i / (float) attackSamples;
                w[i] = s;
            }
            break;
        }

        case Bunker:
        {
            // Dark, slap-heavy, mil-spec character — concrete cube. Strong
            // discrete slaps at 20/50/110 ms, dark filtered noise underneath.
            const float tau = 0.15f;
            lpShape.setHz (1800.0f, sampleRate);
            lpExtra.setHz (1800.0f, sampleRate);
            for (int i = 0; i < N; ++i)
            {
                const float t = (float) i / sampleRate;
                const float env = std::exp (-t / tau);
                float s = rng.next() * env;
                // Two-stage LP for steeper rolloff.
                s = lpShape.process (s);
                s = lpExtra.process (s);
                w[i] = s;
            }
            // Layer slaps — fairly hot, alternating polarity.
            constexpr int kSlapCount = 3;
            const int slapSamples[kSlapCount] = {
                (int) (0.020f * sampleRate),
                (int) (0.050f * sampleRate),
                (int) (0.110f * sampleRate)
            };
            const float slapGains[kSlapCount] = { 0.95f, 0.60f, 0.38f };
            for (int k = 0; k < kSlapCount; ++k)
            {
                const int idx = slapSamples[k];
                if (idx >= 0 && idx < N)
                    w[idx] += (k & 1 ? -1.0f : 1.0f) * slapGains[k];
            }
            break;
        }

        default: break;
    }

    // Peak-normalise to 0.5. With this fixed target, switching algos
    // doesn't slam the wet bus louder/quieter — the user-facing MIX
    // knob is the only level control. Avoids "Hall is way louder than
    // Bunker" surprises.
    float peak = 0.0f;
    for (int i = 0; i < N; ++i) peak = juce::jmax (peak, std::abs (w[i]));
    if (peak > 1e-6f)
    {
        const float g = 0.5f / peak;
        for (int i = 0; i < N; ++i) w[i] *= g;
    }

    return buf;
}

} // namespace ir
} // namespace bombo
