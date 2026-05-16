#pragma once

#include <array>
#include <cmath>
#include <cstdint>

namespace bombo
{

// Click transient generator. Short bandpass-filtered noise burst (~6ms)
// pre-rendered into a fixed buffer at trigger time; playback is zero-cost.
// Worst-case sized for 50 ms at 96 kHz so regenerate() never allocates.
class ClickGen
{
public:
    static constexpr int kMaxSamples = 4800;

    explicit ClickGen(float sampleRate = 48000.0f)
    {
        regenerate(sampleRate, 6.0f, 3500.0f, 1.5f);
    }

    void regenerate(float sampleRate, float decayMs, float centerHz, float bwOct) noexcept
    {
        int n = static_cast<int>((decayMs / 1000.0f) * sampleRate);
        if (n > kMaxSamples) n = kMaxSamples;
        if (n < 1) n = 1;
        len_ = n;

        // White noise via LCG.
        uint32_t rng = 0xDEADBEEFu;
        for (int i = 0; i < len_; ++i)
        {
            rng = rng * 1664525u + 1013904223u;
            buffer_[i] = (static_cast<float>(rng) / 4294967295.0f) * 2.0f - 1.0f;
        }

        // 2-pole bandpass (trapezoidal SVF).
        constexpr float pi = 3.14159265358979323846f;
        constexpr float tau_c = 6.28318530717958647692f;
        constexpr float ln2 = 0.69314718055994530942f;
        const float f0 = centerHz / sampleRate;
        const float w = tau_c * f0;
        const float qDen = 2.0f * std::sinh((ln2 / 2.0f) * bwOct * w / std::sin(w));
        float q = 1.0f / qDen;
        if (q < 0.5f) q = 0.5f;
        const float k = 1.0f / q;
        const float g = std::tan(pi * f0);
        const float a1 = 1.0f / (1.0f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;

        float ic1 = 0.0f, ic2 = 0.0f;
        for (int i = 0; i < len_; ++i)
        {
            const float v3 = buffer_[i] - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            buffer_[i] = v1;
        }

        // Exponential envelope — peak near t=0, ~-52 dB at i=len.
        constexpr float decayK = 6.0f;
        const float invLen = 1.0f / static_cast<float>(len_);
        for (int i = 0; i < len_; ++i)
        {
            const float t = static_cast<float>(i) * invLen;
            buffer_[i] *= std::exp(-decayK * t);
        }

        // Normalize peak to 1.0.
        float peak = 0.0f;
        for (int i = 0; i < len_; ++i)
        {
            const float a = std::abs(buffer_[i]);
            if (a > peak) peak = a;
        }
        if (peak > 0.0f)
        {
            const float inv = 1.0f / peak;
            for (int i = 0; i < len_; ++i) buffer_[i] *= inv;
        }

        pos_ = len_; // not playing until triggered
    }

    void trigger() noexcept { pos_ = 0; }

    float tick() noexcept
    {
        if (pos_ >= len_) return 0.0f;
        return buffer_[pos_++];
    }

    bool isActive() const noexcept { return pos_ < len_; }

private:
    std::array<float, kMaxSamples> buffer_{};
    int len_ = 0;
    int pos_ = 0;
};

} // namespace bombo
