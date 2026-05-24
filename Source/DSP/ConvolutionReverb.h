#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

#include "IRBank.h"
#include "SmoothScalar.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace bombo
{

// IR-based reverb with multi-algo dispatch. Six juce::dsp::Convolution
// instances are pre-loaded at prepare() time with synthesized IRs (see
// IRBank.h); process() dispatches to the currently-selected algo.
//
// Per-hit identity guarantee
// --------------------------
//  Convolution is LTI: same input + same IR + same state → bit-identical
//  output. We hard-reset the active convolution's state on every
//  killTail() (the 30 ms fade then resets, mirroring FdnReverb's flow)
//  and we suppress input writes during the fade — so each new trigger
//  starts the convolution from clean zero state. The only sources of
//  per-trigger variance (LFO, allpass modulation, accumulating FDN
//  state) that plagued the previous algorithmic engine are absent by
//  construction. This closes the parked
//  `bug_bombo_reverb_pulsing_loop_2026_05_17` regression class.
//
// Sample-by-sample API, block-buffered internally
// -----------------------------------------------
//  The chain wants per-sample process() calls (TEETH etc.). juce::dsp::
//  Convolution is block-based, so we accumulate kBlockSize input samples,
//  convolve, and drain output one-by-one. Latency = kBlockSize samples
//  (1.3 ms @ 48 kHz), inaudible against the dry voice path.
class ConvolutionReverb
{
public:
    static constexpr int   kBlockSize        = 64;
    static constexpr float kStopFadeMs       = 80.0f;
    // 6 ms killTail fade — matches the dry voice's 5 ms steal-fadeout
    // window so the wet bus doesn't audibly hang past the new kick's
    // attack. FdnReverb used 30 ms to mask FDN-tail clicks at high
    // feedback; convolution's wet content doesn't have that pressure
    // (no resonant FDN loop), so we can run the fade tight to avoid
    // the "old tail bleeds into new kick" sensation in loop mode.
    static constexpr float kKillFadeMs       = 6.0f;
    // Deferred kill (1 beat after last trig, loop-off): no new kick to
    // mask the cut, so use a smoother fade for a natural decay-out
    // instead of an audible chop.
    static constexpr float kKillFadeSoftMs   = 50.0f;
    static constexpr int   kMaxPredelaySamples = 32768; // ~683 ms @ 48 k

    explicit ConvolutionReverb (float sampleRate = 48000.0f)
        : predelay_ ((std::size_t) kMaxPredelaySamples, 0.0f),
          inBlock_  (1, kBlockSize),
          wetBlock_ (1, kBlockSize)
    {
        for (int i = 0; i < ir::kNumAlgos; ++i)
            convs_[(std::size_t) i].reset (new juce::dsp::Convolution());

        prepareInternal (sampleRate);
    }

    void setSampleRate (float sampleRate) noexcept
    {
        // Non-RT path — called from PluginProcessor::prepareToPlay only.
        // The ctor already calls prepareInternal at the default 48 kHz, so
        // when the host hands us the same rate (very common — including the
        // offline bouncer's clone) we skip the IR re-synthesis + FFT setup.
        // That's hundreds of ms of avoidable work per clone.
        if (sampleRate == sampleRate_) return;
        prepareInternal (sampleRate);
    }

    // Hard reset — instant, no fade. Called from prepareToPlay.
    void reset() noexcept
    {
        for (auto& c : convs_) c->reset();
        for (auto& s : predelay_) s = 0.0f;
        predelayPos_ = 0;
        inBlock_.clear();
        wetBlock_.clear();
        blockWp_ = 0;
        blockRp_ = 0;
        stopFadeRemaining_ = 0;
        killFadeRemaining_ = 0;
        killFadeTotal_     = 0;
        stopMuted_         = false;
        wetLpfZ_           = 0.0f;
        decayEnv_          = 0.0f;
        trigAge_           = INT32_MAX;
        triggerResetPending_ = false;
    }

    // Per-trigger smooth tail kill: linear fade of wet output down to 0
    // over kKillFadeMs ms, while suppressing convolution input writes
    // (so the new trigger's audio doesn't fold into the dying tail).
    // Conv state is reset at the bottom of the fade. Mirrors FdnReverb's
    // kill-tail behaviour 1:1 so the RumbleChain integration is a
    // drop-in.
    void killTail()     noexcept { startKillFade (kKillFadeMs); }
    void killTailSoft() noexcept { startKillFade (kKillFadeSoftMs); }

    // Called per trigger alongside killTail(). The visible reset of the
    // wet-bus decay envelope + Size-window age counter is DEFERRED to
    // when the kill-tail fade actually ends and conv state has been
    // cleared. Reasoning: if we reset decayEnv to 1.0 right at the
    // trigger moment, the OLD conv residue (which is being faded out
    // over the next 30 ms) suddenly gets multiplied by a HIGHER decay
    // env than it had a sample ago — the wet level bumps UP at the
    // trigger instant, then ramps down. That bump reads as "the
    // previous tail bleeds across the trigger" even though we're
    // actively trying to kill it. Apply the reset only AT the moment
    // conv state actually goes to zero, so the new trigger's wet
    // contribution comes in cleanly at full level on top of true
    // silence. If no fade is in flight (rare — caller would normally
    // pair killTail with onTrigger), reset immediately.
    void onTrigger() noexcept
    {
        // TAIL OFF = no per-trigger envelope reset. The wet-bus
        // decayEnv stays pinned at 1.0 (see process()) and trigAge
        // doesn't restart, so layered hits accumulate naturally
        // instead of each refreshing the per-trig envelope shape.
        if (! tailKillOn_) return;

        if (killFadeRemaining_ > 0)
        {
            triggerResetPending_ = true;
        }
        else
        {
            decayEnv_ = 1.0f;
            trigAge_  = 0;
            triggerResetPending_ = false;
        }
    }

    // Transport-stop smooth fade. Mirrors FdnReverb::stopAndMute().
    void stopAndMute() noexcept
    {
        if (! stopMuted_ && stopFadeRemaining_ == 0)
        {
            std::uint32_t f = (std::uint32_t) (kStopFadeMs * 0.001f * sampleRate_);
            if (f < 1) f = 1;
            stopFadeRemaining_ = f;
            stopFadeTotal_     = f;
        }
    }

    // ── PARAM SETTERS ──────────────────────────────────────────────────
    // Map ITU-style 0..1 knobs onto musically useful internal ranges.
    void setType (int algo) noexcept
    {
        if (algo < 0 || algo >= ir::kNumAlgos) algo = ir::Hall;
        activeAlgo_ = algo;
    }

    // 0..1 → 50 ms .. (algo length seconds, sample-count) for the
    // Size window. Tail cuts to 0 after this many samples post-trigger.
    void setSize (float n01) noexcept
    {
        n01 = juce::jlimit (0.0f, 1.0f, n01);
        const float maxLenSec = ir::kAlgoLengthSec[(std::size_t) activeAlgo_];
        const float minLenSec = 0.05f;
        const float sec = minLenSec + (maxLenSec - minLenSec) * n01;
        sizeWindowSamples_ = juce::jmax (1, (int) (sec * sampleRate_));
        sizeFadeSamples_   = juce::jmax (1, sizeWindowSamples_ / 5);
    }

    // 0..1 → exponential decay tau between 50 ms and 4 s. Resets to 1.0
    // on every onTrigger().
    //
    // 2026-05-24: route through SmoothScalar so tweaks mid-tail glide
    // toward the new decay rate over ~25 ms instead of snapping. Without
    // smoothing the user would hear the tail's decay rate jump on the
    // very next sample, which read as a glitch.
    void setDecay (float n01) noexcept
    {
        n01 = juce::jlimit (0.0f, 1.0f, n01);
        const float tauSec = 0.05f + n01 * 3.95f;
        // Per-sample coef so env *= coef → exp(-t/tau).
        decayCoef_.setTarget(std::exp (-1.0f / (tauSec * sampleRate_)));
    }

    // 0..1 → post-conv 1-pole LP cutoff between 12 kHz (open) and 200 Hz
    // (dark). Higher knob value = darker tail (matches FdnReverb semantics).
    // Also SmoothScalar-routed; otherwise a Damp knob sweep produced
    // step-changes in tail tone instead of a continuous filter sweep.
    void setDamp (float n01) noexcept
    {
        n01 = juce::jlimit (0.0f, 1.0f, n01);
        const float fc = 12000.0f * std::pow (0.0167f, n01);  // 12k → 200 Hz, exponential
        const float x  = std::exp (-6.28318530717958f * fc / sampleRate_);
        wetLpfCoef_.setTarget(1.0f - x);
    }

    void setPredelayMs (float ms) noexcept
    {
        const int s = juce::jlimit (0, kMaxPredelaySamples - 1,
                                    (int) (ms * 0.001f * sampleRate_));
        predelaySamples_ = s;
    }

    // Controls per-trigger kill + per-trigger wet-bus envelope. When
    // OFF the convolution simply rings out per IR + linearity, with no
    // wet-side attenuation envelope and no fade-and-reset on trigger.
    // In loop mode this lets every kick's IR response layer with the
    // previous beats' (LTI sum), matching the user's "ring forever"
    // expectation.
    void setTailKillOn (bool on) noexcept { tailKillOn_ = on; }

    // ── PROCESS ────────────────────────────────────────────────────────
    float process (float in) noexcept
    {
        // 1) Predelay write/read.
        const float dryIn = in;
        predelay_[(std::size_t) predelayPos_] = dryIn;
        int rPos = predelayPos_ - predelaySamples_;
        if (rPos < 0) rPos += kMaxPredelaySamples;
        const float delayed = predelay_[(std::size_t) rPos];
        if (++predelayPos_ >= kMaxPredelaySamples) predelayPos_ = 0;

        // 2) Push delayed sample into the input block — UNLESS a killTail
        // fade is in flight, in which case write zero so the new trigger's
        // audio doesn't fold into the dying tail.
        const bool suppress = (killFadeRemaining_ > 0);
        inBlock_.setSample (0, blockWp_, suppress ? 0.0f : delayed);
        ++blockWp_;

        // 3) When the input block is full, convolve it through the active
        // algo and drain into wetBlock_. Reset the read cursor.
        if (blockWp_ >= kBlockSize)
        {
            auto& conv = *convs_[(std::size_t) activeAlgo_];
            // ProcessContextNonReplacing requires distinct in/out blocks.
            juce::dsp::AudioBlock<const float> inBk  (inBlock_);
            juce::dsp::AudioBlock<float>       outBk (wetBlock_);
            juce::dsp::ProcessContextNonReplacing<float> ctx (inBk, outBk);
            conv.process (ctx);
            blockWp_ = 0;
            blockRp_ = 0;
        }

        // 4) Read one wet sample out of the wet block.
        float wet = wetBlock_.getSample (0, blockRp_);
        ++blockRp_;
        if (blockRp_ >= kBlockSize) blockRp_ = kBlockSize - 1; // clamp (next block will reset)

        // 5) Post-LP (Damp). Coefficient pulled through SmoothScalar so
        //    a Damp knob sweep glides the cutoff rather than block-step.
        const float wetLpfCoefNow = wetLpfCoef_.next();
        wetLpfZ_ += wetLpfCoefNow * (wet - wetLpfZ_);
        if (std::fpclassify (wetLpfZ_) == FP_SUBNORMAL) wetLpfZ_ = 0.0f;
        wet = wetLpfZ_;

        // 6) Per-trigger decay envelope. When TAIL OFF the wet bus is
        //    passed through at unity — the only "decay" is whatever the
        //    IR itself provides, which gives natural per-hit ringing and
        //    LTI layering across hits.
        //    decayCoef also smoothed — knob tweaks mid-tail glide the
        //    decay rate over ~25 ms instead of jumping.
        const float decayCoefNow = decayCoef_.next();
        if (tailKillOn_)
        {
            if (decayEnv_ > 1e-7f)
            {
                wet *= decayEnv_;
                decayEnv_ *= decayCoefNow;
            }
            else
            {
                wet = 0.0f;
            }

            // 7) Size window — full level until tail-fade region, then
            //    linear ramp to 0, then silence.
            if (trigAge_ >= sizeWindowSamples_)
            {
                wet = 0.0f;
            }
            else
            {
                const int tail = sizeWindowSamples_ - trigAge_;
                if (tail < sizeFadeSamples_)
                    wet *= (float) tail / (float) sizeFadeSamples_;
            }
            if (trigAge_ < INT32_MAX - 1) ++trigAge_;
        }

        // 8) Per-trigger killTail linear fade — at bottom, reset state.
        // Mirrors FdnReverb 1:1 so RumbleChain doesn't need to change.
        if (killFadeRemaining_ > 0)
        {
            const float ramp = (float) killFadeRemaining_ / (float) killFadeTotal_;
            wet *= ramp;
            --killFadeRemaining_;
            if (killFadeRemaining_ == 0)
            {
                // Reset conv state on ALL algos so any future algo switch
                // also starts clean. Cheap (just clears small overlap buffers).
                for (auto& c : convs_) c->reset();
                inBlock_.clear();
                wetBlock_.clear();
                blockWp_ = 0;
                blockRp_ = 0;
                wetLpfZ_ = 0.0f;
                // Clear predelay so stale samples don't bleed back in.
                for (auto& s : predelay_) s = 0.0f;
                predelayPos_ = 0;
                // Apply the deferred per-trigger reset NOW that the conv
                // state is genuinely zero. See onTrigger() for the
                // rationale (avoids the wet-bus level bump on trigger).
                if (triggerResetPending_)
                {
                    decayEnv_ = 1.0f;
                    trigAge_  = 0;
                    triggerResetPending_ = false;
                }
            }
        }

        // 9) Transport-stop fade — mirrors FdnReverb::stopAndMute().
        if (stopFadeRemaining_ > 0)
        {
            const float ramp = (float) stopFadeRemaining_ / (float) stopFadeTotal_;
            wet *= ramp;
            --stopFadeRemaining_;
            if (stopFadeRemaining_ == 0)
            {
                stopMuted_ = true;
                for (auto& c : convs_) c->reset();
                for (auto& s : predelay_) s = 0.0f;
                predelayPos_ = 0;
                wetLpfZ_ = 0.0f;
            }
        }
        if (stopMuted_) wet = 0.0f;

        return wet;
    }

private:
    void startKillFade (float fadeMs) noexcept
    {
        if (! tailKillOn_) return;
        stopFadeRemaining_ = 0;
        stopMuted_ = false;
        if (killFadeRemaining_ != 0) return;
        std::uint32_t fadeSamples = (std::uint32_t) (fadeMs * 0.001f * sampleRate_);
        if (fadeSamples < 1) fadeSamples = 1;
        killFadeRemaining_ = fadeSamples;
        killFadeTotal_     = fadeSamples;
    }

    void prepareInternal (float sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate       = sampleRate;
        spec.maximumBlockSize = (juce::uint32) kBlockSize;
        spec.numChannels      = 1;

        // Load IRs BEFORE prepare(). Per JUCE docs: prepare() will ensure
        // the IR from the most recent loadImpulseResponse() is fully
        // initialised and active on the first process() call. The
        // opposite order (prepare → load) leaves currentEngine null in
        // release builds and process() then silently outputs nothing.
        for (int i = 0; i < ir::kNumAlgos; ++i)
        {
            auto& c = *convs_[(std::size_t) i];
            auto irBuf = ir::synthesizeIR (i, sampleRate);
            c.loadImpulseResponse (std::move (irBuf), sampleRate,
                                   juce::dsp::Convolution::Stereo::no,
                                   juce::dsp::Convolution::Trim::no,
                                   juce::dsp::Convolution::Normalise::no);
            c.prepare (spec);
        }

        // Default state mirrors FdnReverb's "size=0.5" feel.
        sizeWindowSamples_ = (int) (1.0f * sampleRate);
        sizeFadeSamples_   = sizeWindowSamples_ / 5;

        // 25 ms one-pole ramp on the user-tweaked scalars. Snap=true on
        // load so first sample isn't a glide from zero (which would make
        // damp-at-max boot up "open then close" audibly).
        constexpr float kFxSmoothTauSec = 0.025f;
        wetLpfCoef_.prepare(sampleRate, kFxSmoothTauSec);
        decayCoef_ .prepare(sampleRate, kFxSmoothTauSec);
        setDecay (0.7f);
        setDamp  (0.45f);
        decayCoef_ .snap(decayCoef_ .target);
        wetLpfCoef_.snap(wetLpfCoef_.target);
        wetLpfZ_ = 0.0f;

        reset();
    }

    float sampleRate_ { 48000.0f };

    std::array<std::unique_ptr<juce::dsp::Convolution>, ir::kNumAlgos> convs_{};
    int activeAlgo_ { ir::Hall };

    // Predelay ring.
    std::vector<float> predelay_;
    int predelayPos_     { 0 };
    int predelaySamples_ { 0 };

    // Block-buffered conv I/O.
    juce::AudioBuffer<float> inBlock_;
    juce::AudioBuffer<float> wetBlock_;
    int blockWp_ { 0 };
    int blockRp_ { 0 };

    // Post-LP (Damp). SmoothScalar so a Damp knob sweep glides the
    // cutoff rather than stepping the coefficient block-by-block.
    SmoothScalar wetLpfCoef_;
    float        wetLpfZ_    { 0.0f };

    // Per-trigger decay env. decayCoef_ is the per-sample multiplier
    // applied to decayEnv_; smoothed so mid-tail Decay knob tweaks
    // glide the decay rate instead of jumping it.
    SmoothScalar decayCoef_;
    float        decayEnv_   { 0.0f };

    // Per-trigger size window.
    int sizeWindowSamples_ { 48000 };
    int sizeFadeSamples_   { 9600 };
    int trigAge_           { INT32_MAX };

    // Kill-tail and stop fades.
    std::uint32_t killFadeRemaining_ { 0 };
    std::uint32_t killFadeTotal_     { 0 };
    std::uint32_t stopFadeRemaining_ { 0 };
    std::uint32_t stopFadeTotal_     { 0 };
    bool          stopMuted_         { false };

    // onTrigger() defers the decayEnv/trigAge reset until kill-tail
    // fade ends, so the old wet residue keeps decaying smoothly
    // through the fade instead of getting bumped up by the reset.
    bool          triggerResetPending_ { false };

    // setTailKillOn(false) makes killTail() and onTrigger() no-ops and
    // bypasses the per-trigger wet-bus envelope/Size window — the
    // convolution rings naturally and hits layer in LTI fashion.
    bool          tailKillOn_ { true };
};

} // namespace bombo
