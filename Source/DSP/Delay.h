#pragma once

#include <vector>
#include <cmath>
#include <cstdint>

#include "SmoothScalar.h"

namespace bombo
{

// Echo with feedback-loop LP damping, LFO read-tap drift, and a
// post-stage LP→BP→HP tone-morph SVF at 300 Hz. Port of dsp/delay.rs.
// Buffers pre-allocated; process() never heap-allocates.
class Delay
{
public:
    static constexpr int kMaxSamples = 200000;
    // kKillFadeMs: width of the V-shaped tail-kill window. Trade-off
    // between click suppression and audible chop tightness:
    //   - first half = output ramps OLD echo down to 0 (audible bleed
    //     window — the longer this is, the more old content you hear
    //     overlapping the new kick attack)
    //   - midpoint  = hard buffer flush (silent at output)
    //   - second half = output ramps back up (silent because buffer is
    //     empty)
    // History: 5 ms originally; bumped to 30 ms 2026-05-17 to mask
    // buffer-flush clicks at high feedback. But 30 ms = 15 ms of
    // audible OLD-tail bleed at the start of every chopped beat —
    // user reports this as "tail bleeds onto next trig" in loop mode
    // 2026-05-23. Tightened to 6 ms (matches ConvolutionReverb fade +
    // dry-voice 5 ms steal): bleed window is now ~3 ms (below kick-
    // attack-mask threshold) while V-ramp still reaches ~0.7% before
    // the flush, low enough that the flush stays click-free at the
    // 0.95 feedback cap.
    static constexpr float kKillFadeMs = 6.0f;
    // Soft kill — used by the deferred tail-kill timer in
    // BomboProcessor (one beat after the last trigger) AND by loop-off
    // transitions. No kick attack masks the cut here, so we want the
    // fade long enough to land click-free + sound like a natural
    // decay-out rather than a chop. 50 ms V-fade = 25 ms slope =
    // ~-32 dB at midpoint with sub-audible per-sample slope.
    static constexpr float kKillFadeSoftMs = 50.0f;
    static constexpr float kStopFadeMs = 80.0f;

    explicit Delay(float sampleRate = 48000.0f)
        : buffer_(static_cast<size_t>(kMaxSamples), 0.0f)
    {
        setSampleRate(sampleRate);
        delaySamples_.snap(0.25f * sampleRate);   // 250 ms default
        feedback_   .snap(0.45f);
        fbDampCoef_ .snap(0.25f);
    }

    void setSampleRate(float sr) noexcept
    {
        sampleRate_ = sr;
        // SVF cutoff fixed at 300 Hz, Q = 0.707 — morph picks the output.
        setSvfCoefs(300.0f, 0.707f);

        // 25 ms one-pole ramp on the user-tweaked scalars. Without this,
        // knob tweaks mid-tail produced a tap-scrub blip (delaySamples_
        // jump), a wet-level step (feedback_), or a tone-step on the
        // feedback-loop LP (fbDampCoef_). The smoother glides the
        // consumed value so process() never sees a discontinuity.
        constexpr float kFxSmoothTauSec = 0.025f;
        delaySamples_.prepare(sr, kFxSmoothTauSec);
        feedback_    .prepare(sr, kFxSmoothTauSec);
        fbDampCoef_  .prepare(sr, kFxSmoothTauSec);
    }

    // Stores the tap as a float so tempo-sync delays don't quantize to
    // integer samples. At 44.1 kHz with irrational BPM × note-value
    // ratios the int truncation drift used to compound against the
    // LOOP scheduler (which round-to-nearest), producing audible "delay
    // creeping" over long loops. The process() read tap already
    // interpolates linearly between idx0/idx1 so sub-sample taps cost
    // nothing extra at runtime.
    // Routed through SmoothScalar (since 2026-05-24) so the read tap
    // glides between values rather than jumping — a fast TIME-knob sweep
    // mid-tail now sounds like a continuous tape-warble pitch slide
    // instead of discrete sample-position blips.
    void setTimeMs(float ms) noexcept
    {
        const float n      = (ms < 1.0f ? 1.0f : ms) * 0.001f * sampleRate_;
        const float maxF   = static_cast<float>(kMaxSamples - 1);
        delaySamples_.setTarget(n < 1.0f ? 1.0f : (n > maxF ? maxF : n));
    }

    void setFeedback(float fb) noexcept
    {
        if (fb < 0.0f) fb = 0.0f;
        if (fb > 0.95f) fb = 0.95f;
        feedback_.setTarget(fb);
    }

    // SMEAR: tape-warble LFO on the read tap. 0 = static, 1 = ~90 cents wobble.
    void setDrift(float drift) noexcept
    {
        if (drift < 0.0f) drift = 0.0f;
        if (drift > 1.0f) drift = 1.0f;
        // 0.02 * sr = 960 samples at 48 kHz → up to ~90 cents at 0.7 Hz.
        driftDepthSamples_ = drift * (0.02f * sampleRate_);
    }

    void setDamp(float damp) noexcept
    {
        if (damp < 0.0f) damp = 0.0f;
        if (damp > 1.0f) damp = 1.0f;
        fbDampCoef_.setTarget(damp * 0.5f);
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

    // Controls whether killTail() does anything. TAIL OFF = no-op,
    // letting the delay buffer + feedback ring naturally across trigs
    // (the user-facing "let tails ring forever" semantics).
    void setTailKillOn(bool on) noexcept { tailKillOn_ = on; }

    // Per-trigger kill-fade — output V-ramp + buffer flush at midpoint.
    // Fast (6 ms) so the OLD tail dies before the NEW kick attack
    // builds; the brief discontinuity at the buffer flush is masked
    // by the simultaneous kick attack.
    void killTail() noexcept { startKillFade(kKillFadeMs); }

    // Deferred kill (post-last-trig, transport-stop, etc.) — gentler
    // fade so the natural-feeling tail decay isn't audibly chopped.
    // No kick attack is firing alongside, so the kill needs to be
    // smooth enough to be click-free on its own.
    void killTailSoft() noexcept { startKillFade(kKillFadeSoftMs); }

    float process(float input) noexcept
    {
        if (stopMuted_) return 0.0f;

        constexpr float tau_c = 6.28318530717958647692f;
        lfoPhase_ += driftRateHz_ / sampleRate_ * tau_c;
        if (lfoPhase_ >= tau_c) lfoPhase_ -= tau_c;
        const float driftOffset = std::sin(lfoPhase_) * driftDepthSamples_;

        // Smoothed scalars — step once per sample. delaySamples in
        // particular: smoothing the read-tap target combined with the
        // existing linear-interp tap (idx0/idx1+frac) gives a smooth
        // tape-warble feel under TIME-knob sweeps mid-tail.
        const float delaySamplesNow = delaySamples_.next();
        const float fbDampCoefNow   = fbDampCoef_.next();
        const float feedbackNow     = feedback_.next();

        const int bufLen = static_cast<int>(buffer_.size());
        const float bufLenF = static_cast<float>(bufLen);
        float readPosF = static_cast<float>(writePos_)
                       - delaySamplesNow
                       + driftOffset
                       + bufLenF;
        // rem_euclid for floats — keep in [0, bufLen).
        readPosF -= std::floor(readPosF / bufLenF) * bufLenF;
        const int idx0 = static_cast<int>(readPosF) % bufLen;
        const int idx1 = (idx0 + 1) % bufLen;
        const float frac = readPosF - std::floor(readPosF);
        const float wet = buffer_[idx0] * (1.0f - frac) + buffer_[idx1] * frac;

        // Feedback-loop LP damping.
        fbDampZ_ = wet * (1.0f - fbDampCoefNow) + fbDampZ_ * fbDampCoefNow;
        if (std::fpclassify(fbDampZ_) == FP_SUBNORMAL) fbDampZ_ = 0.0f;
        // Bug fix 2026-05-17: during the kill-fade window, write ZERO to
        // the buffer — not input + fb. Otherwise any audio arriving during
        // the fade (the voice's 5 ms startFadeout tail, or pre-flush wet
        // looping through fbDampZ_) gets captured into the delay buffer,
        // then plays back at the delay-cycle period AFTER the fade ends.
        // With max feedback (≈1.0), that capture rings out indefinitely
        // — user reported this as a faint, never-dying delay tail.
        const float fb = fbDampZ_ * feedbackNow;
        buffer_[writePos_] = (killFadeRemaining_ > 0) ? 0.0f : input + fb;
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
    void startKillFade(float fadeMs) noexcept
    {
        if (! tailKillOn_) return;
        lfoPhase_ = 0.0f;
        stopFadeRemaining_ = 0;
        stopMuted_ = false;
        if (killFadeRemaining_ != 0) return;
        uint32_t fadeSamples = static_cast<uint32_t>(fadeMs * 0.001f * sampleRate_);
        if (fadeSamples < 1) fadeSamples = 1;
        killFadeRemaining_ = fadeSamples;
        killFadeTotal_ = fadeSamples;
    }

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
    // delaySamples_ / feedback_ / fbDampCoef_ wrapped in SmoothScalar
    // 2026-05-24 so APVTS-driven knob updates glide rather than snap.
    // Default values are applied via .snap() in the ctor; setSampleRate()
    // configures the 25 ms ramp.
    SmoothScalar delaySamples_;
    SmoothScalar feedback_;
    float        fbDampZ_ = 0.0f;
    SmoothScalar fbDampCoef_;
    float lfoPhase_ = 0.0f;
    float driftDepthSamples_ = 0.0f;
    float driftRateHz_ = 0.7f;
    uint32_t killFadeRemaining_ = 0;
    uint32_t killFadeTotal_ = 1;
    uint32_t stopFadeRemaining_ = 0;
    uint32_t stopFadeTotal_ = 1;
    bool stopMuted_ = false;
    // setTailKillOn(false) suppresses per-trigger killTail so tails
    // ring naturally across hits.
    bool tailKillOn_ = true;

    // Wet-bus morph SVF (LP/BP/HP at fixed 300 Hz, crossfaded).
    float svfIc1_ = 0.0f, svfIc2_ = 0.0f;
    float svfA1_ = 0.0f, svfA2_ = 0.0f, svfA3_ = 0.0f, svfK_ = 0.0f;
    float filterMorph_ = 0.5f;
};

} // namespace bombo
