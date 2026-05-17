#pragma once

#include <vector>
#include <cmath>
#include <cstdint>

namespace bombo
{

// Echo with feedback-loop LP damping, LFO read-tap drift, and a
// post-stage LP→BP→HP tone-morph SVF at 300 Hz. Port of dsp/delay.rs.
// Buffers pre-allocated; process() never heap-allocates.
class Delay
{
public:
    static constexpr int kMaxSamples = 200000;
    static constexpr float kKillFadeMs = 5.0f;
    static constexpr float kStopFadeMs = 80.0f;

    explicit Delay(float sampleRate = 48000.0f)
        : buffer_(static_cast<size_t>(kMaxSamples), 0.0f)
    {
        setSampleRate(sampleRate);
        delaySamples_ = static_cast<int>(0.25f * sampleRate); // 250 ms default
    }

    void setSampleRate(float sr) noexcept
    {
        sampleRate_ = sr;
        // SVF cutoff fixed at 300 Hz, Q = 0.707 — morph picks the output.
        setSvfCoefs(300.0f, 0.707f);
    }

    void setTimeMs(float ms) noexcept
    {
        const int n = static_cast<int>((ms < 1.0f ? 1.0f : ms) / 1000.0f * sampleRate_);
        delaySamples_ = n < 1 ? 1 : (n > kMaxSamples - 1 ? kMaxSamples - 1 : n);
    }

    void setFeedback(float fb) noexcept
    {
        if (fb < 0.0f) fb = 0.0f;
        if (fb > 0.95f) fb = 0.95f;
        feedback_ = fb;
    }

    void setDrift(float drift) noexcept
    {
        if (drift < 0.0f) drift = 0.0f;
        if (drift > 1.0f) drift = 1.0f;
        driftDepthSamples_ = drift * (0.005f * sampleRate_);
    }

    void setDamp(float damp) noexcept
    {
        if (damp < 0.0f) damp = 0.0f;
        if (damp > 1.0f) damp = 1.0f;
        fbDampCoef_ = damp * 0.5f;
    }

    void setFilterMorph(float pos) noexcept
    {
        if (pos < 0.0f) pos = 0.0f;
        if (pos > 1.0f) pos = 1.0f;
        filterMorph_ = pos;
    }

    void reset() noexcept
    {
        for (auto& s : buffer_) s = 0.0f;
        writePos_ = 0;
        fbDampZ_ = 0.0f;
        lfoPhase_ = 0.0f;
    }

    // Hard buffer flush — for clean-retrigger mode. Also clears the
    // post-filter SVF state so it doesn't ring after the buffer is zeroed.
    void flush() noexcept
    {
        for (auto& s : buffer_) s = 0.0f;
        fbDampZ_ = 0.0f;
        svfIc1_ = 0.0f;
        svfIc2_ = 0.0f;
        stopFadeRemaining_ = 0;
        stopMuted_ = false;
    }

    // Transport-stop fade — wet drains over ~STOP_FADE_MS then mutes.
    void stopFade() noexcept
    {
        if (stopMuted_ || stopFadeRemaining_ > 0) return;
        uint32_t fadeSamples = static_cast<uint32_t>(kStopFadeMs * 0.001f * sampleRate_);
        if (fadeSamples < 1) fadeSamples = 1;
        stopFadeRemaining_ = fadeSamples;
        stopFadeTotal_ = fadeSamples;
    }

    // Per-trigger kill-fade — output V-ramp only, buffer keeps writing.
    void killTail() noexcept
    {
        stopFadeRemaining_ = 0;
        stopMuted_ = false;
        if (killFadeRemaining_ == 0)
        {
            uint32_t fadeSamples = static_cast<uint32_t>(kKillFadeMs * 0.001f * sampleRate_);
            if (fadeSamples < 1) fadeSamples = 1;
            killFadeRemaining_ = fadeSamples;
            killFadeTotal_ = fadeSamples;
        }
    }

    float process(float input) noexcept
    {
        if (stopMuted_) return 0.0f;

        constexpr float tau_c = 6.28318530717958647692f;
        lfoPhase_ += driftRateHz_ / sampleRate_ * tau_c;
        if (lfoPhase_ >= tau_c) lfoPhase_ -= tau_c;
        const float driftOffset = std::sin(lfoPhase_) * driftDepthSamples_;

        const int bufLen = static_cast<int>(buffer_.size());
        const float bufLenF = static_cast<float>(bufLen);
        float readPosF = static_cast<float>(writePos_)
                       - static_cast<float>(delaySamples_)
                       + driftOffset
                       + bufLenF;
        // rem_euclid for floats — keep in [0, bufLen).
        readPosF -= std::floor(readPosF / bufLenF) * bufLenF;
        const int idx0 = static_cast<int>(readPosF) % bufLen;
        const int idx1 = (idx0 + 1) % bufLen;
        const float frac = readPosF - std::floor(readPosF);
        const float wet = buffer_[idx0] * (1.0f - frac) + buffer_[idx1] * frac;

        // Feedback-loop LP damping.
        fbDampZ_ = wet * (1.0f - fbDampCoef_) + fbDampZ_ * fbDampCoef_;
        if (std::fpclassify(fbDampZ_) == FP_SUBNORMAL) fbDampZ_ = 0.0f;
        // Bug fix 2026-05-17: during the kill-fade window, write ZERO to
        // the buffer — not input + fb. Otherwise any audio arriving during
        // the fade (the voice's 5 ms startFadeout tail, or pre-flush wet
        // looping through fbDampZ_) gets captured into the delay buffer,
        // then plays back at the delay-cycle period AFTER the fade ends.
        // With max feedback (≈1.0), that capture rings out indefinitely
        // — user reported this as a faint, never-dying delay tail.
        buffer_[writePos_] = (killFadeRemaining_ > 0)
                                ? 0.0f
                                : input + fbDampZ_ * feedback_;
        writePos_ = (writePos_ + 1) % bufLen;

        // Kill-fade V-ramp. The ramp fades the output down to 0 over the
        // first half of kKillFadeMs, then back to 1 over the second half.
        // At the zero crossing we flush the circular buffer so the second
        // half plays back fresh-written input only — no residual tail.
        // The V-ramp masks the buffer-zero click in silence.
        float out = wet;
        if (killFadeRemaining_ > 0)
        {
            const float total = static_cast<float>(killFadeTotal_);
            const float pos = total - static_cast<float>(killFadeRemaining_);
            const float half = total * 0.5f;
            float ramp = std::abs((pos - half) / half);
            if (ramp > 1.0f) ramp = 1.0f;
            out = wet * ramp;
            // At the V's bottom, hard-flush the buffer + filter state.
            // Guard fires exactly once per kill cycle (when killFadeRemaining_
            // crosses the midpoint).
            const uint32_t mid = killFadeTotal_ / 2u;
            if (killFadeRemaining_ == mid + 1u || (mid == 0u && killFadeRemaining_ == 1u))
            {
                for (auto& s : buffer_) s = 0.0f;
                fbDampZ_ = 0.0f;
                svfIc1_ = 0.0f;
                svfIc2_ = 0.0f;
            }
            --killFadeRemaining_;
        }

        // Tone morph (LP→BP→HP at 300 Hz).
        const float filtered = applyFilterMorph(out);
        return applyStopFade(filtered);
    }

private:
    void setSvfCoefs(float cutoffHz, float q) noexcept
    {
        constexpr float pi = 3.14159265358979323846f;
        const float g = std::tan(pi * cutoffHz / sampleRate_);
        const float qSafe = q < 0.1f ? 0.1f : q;
        const float k = 1.0f / qSafe;
        const float a1 = 1.0f / (1.0f + g * (g + k));
        svfA1_ = a1;
        svfA2_ = g * a1;
        svfA3_ = g * svfA2_;
        svfK_ = k;
    }

    float applyFilterMorph(float wet) noexcept
    {
        const float v3 = wet - svfIc2_;
        const float v1 = svfA1_ * svfIc1_ + svfA2_ * v3;
        const float v2 = svfIc2_ + svfA2_ * svfIc1_ + svfA3_ * v3;
        svfIc1_ = 2.0f * v1 - svfIc1_;
        svfIc2_ = 2.0f * v2 - svfIc2_;
        if (std::fpclassify(svfIc1_) == FP_SUBNORMAL) svfIc1_ = 0.0f;
        if (std::fpclassify(svfIc2_) == FP_SUBNORMAL) svfIc2_ = 0.0f;
        const float lp = v2;
        const float bp = v1;
        const float hp = wet - svfK_ * v1 - v2;
        if (filterMorph_ <= 0.5f)
        {
            const float t = filterMorph_ * 2.0f;
            return lp * (1.0f - t) + bp * t;
        }
        const float t = (filterMorph_ - 0.5f) * 2.0f;
        return bp * (1.0f - t) + hp * t;
    }

    float applyStopFade(float wet) noexcept
    {
        if (stopFadeRemaining_ == 0) return wet;
        const float ramp = static_cast<float>(stopFadeRemaining_)
                         / static_cast<float>(stopFadeTotal_);
        const float out = wet * ramp;
        --stopFadeRemaining_;
        if (stopFadeRemaining_ == 0)
        {
            for (auto& s : buffer_) s = 0.0f;
            fbDampZ_ = 0.0f;
            stopMuted_ = true;
        }
        return out;
    }

    std::vector<float> buffer_;
    int writePos_ = 0;
    float sampleRate_ = 48000.0f;
    int delaySamples_ = 12000;
    float feedback_ = 0.45f;
    float fbDampZ_ = 0.0f;
    float fbDampCoef_ = 0.25f;
    float lfoPhase_ = 0.0f;
    float driftDepthSamples_ = 0.0f;
    float driftRateHz_ = 0.7f;
    uint32_t killFadeRemaining_ = 0;
    uint32_t killFadeTotal_ = 1;
    uint32_t stopFadeRemaining_ = 0;
    uint32_t stopFadeTotal_ = 1;
    bool stopMuted_ = false;

    // Wet-bus morph SVF (LP/BP/HP at fixed 300 Hz, crossfaded).
    float svfIc1_ = 0.0f, svfIc2_ = 0.0f;
    float svfA1_ = 0.0f, svfA2_ = 0.0f, svfA3_ = 0.0f, svfK_ = 0.0f;
    float filterMorph_ = 0.5f;
};

} // namespace bombo
