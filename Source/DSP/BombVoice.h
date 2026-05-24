#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

#include "Oscillators.h"
#include "Envelopes.h"
#include "ClickGen.h"
#include "NoiseGen.h"
#include "VoiceClip.h"

namespace bombo
{

// Per-trigger snapshot of all knob state. Captured once at note-on so the
// per-sample tick() reads stable values even if params update mid-decay.
// SUB layer = fundamental (sustained); MID layer = body punch (short);
// SAMPLE layer = user-loaded punch buffer (≤ 200 ms, linear fade baked in).
struct VoiceTrigger
{
    int   waveform        = WAVE_SINE;
    // SUB
    float pitchStartHz    = 150.0f;
    float pitchEndHz      = 45.0f;
    float pitchEnvDecayMs = 80.0f;
    float pitchCurve      = 3.0f;
    // Sub HPF — one-pole high-pass on the SUB layer only. 20 Hz = bypass.
    // Useful range 30-100 Hz to carve muddy lows in tight psytrance kicks.
    float subHpfHz        = 20.0f;
    // MID
    float midPitchStartHz = 300.0f;
    float midPitchEndHz   = 80.0f;
    float midDecayMs      = 80.0f;
    float midLevel        = 0.70f;
    // Amp env
    float ampAttackMs     = 0.5f;
    float ampDecayMs      = 700.0f;
    // Transient + body
    float clickAmount     = 0.30f;
    float clickCenterHz   = 4500.0f;
    float noiseAmount     = 0.30f;
    float noiseColor      = 0.20f;
    // Drive
    float driveAmount     = 0.30f;
    int   driveMode       = VC_DIODE;
    // BIAS: DC offset fed into the waveshaper pre-clip for asymmetric harmonics.
    // -1..+1; 0 = symmetric (no change to existing behaviour).
    float driveBias       = 0.0f;
    // Section mute snapshots — captured at note-on so they apply for the
    // voice's lifetime. driveMute also bypasses the voice clipper so the
    // whole DRIVE column quiets when toggled off, not just the rumble bus.
    bool  voiceAMute      = false;
    bool  voiceBMute      = false;
    bool  driveMute       = false;
    // Voice B synth-layer master gate. true (default) preserves the
    // historic behaviour (mid sine + click + noise + sample sum into the
    // body bus). false bypasses the synth → bodyMix becomes sampleOut
    // only, giving a true "sample player" mode for Voice B.
    bool  voiceBSynthOn   = true;
    // Sample-slot layer (VOICE B). shared_ptr<const> is copied by the audio
    // thread at trigger time — the refcount bump is allocator-free on
    // libstdc++ x86_64. Null = no sample loaded; voice just skips the mix.
    std::shared_ptr<const juce::AudioBuffer<float>> sampleBuf{};

    // VOICE A ↔ VOICE B balance (tent). 0 = A only, 0.5 = both at unity,
    // 1 = B only.
    float voiceBalance    = 0.5f;

    // Equality across every field that affects the rendered voice. Used by
    // the loop cache to detect voice-param edits and invalidate so the next
    // captured beat picks up the new trigger. sampleBuf is compared by raw
    // pointer (same loaded buffer = same audio); the shared_ptr's control
    // block is irrelevant for this purpose. Float comparison is exact —
    // even a 1-bit knob nudge should invalidate the cache.
    bool operator==(const VoiceTrigger& o) const noexcept
    {
        return waveform        == o.waveform
            && pitchStartHz    == o.pitchStartHz
            && pitchEndHz      == o.pitchEndHz
            && pitchEnvDecayMs == o.pitchEnvDecayMs
            && pitchCurve      == o.pitchCurve
            && subHpfHz        == o.subHpfHz
            && midPitchStartHz == o.midPitchStartHz
            && midPitchEndHz   == o.midPitchEndHz
            && midDecayMs      == o.midDecayMs
            && midLevel        == o.midLevel
            && ampAttackMs     == o.ampAttackMs
            && ampDecayMs      == o.ampDecayMs
            && clickAmount     == o.clickAmount
            && clickCenterHz   == o.clickCenterHz
            && noiseAmount     == o.noiseAmount
            && noiseColor      == o.noiseColor
            && driveAmount     == o.driveAmount
            && driveMode       == o.driveMode
            && driveBias       == o.driveBias
            && voiceAMute      == o.voiceAMute
            && voiceBMute      == o.voiceBMute
            && voiceBSynthOn   == o.voiceBSynthOn
            && driveMute       == o.driveMute
            && voiceBalance    == o.voiceBalance
            && sampleBuf.get() == o.sampleBuf.get();
    }
    bool operator!=(const VoiceTrigger& o) const noexcept { return !(*this == o); }
};

// 5 ms voice-steal fadeout. Long enough to avoid step discontinuity on a
// sub-bass decay, short enough that near-simultaneous hits don't bleed.
constexpr float kVoiceFadeoutMs = 5.0f;

class BombVoice
{
public:
    explicit BombVoice(float sampleRate = 48000.0f) { setSampleRate(sampleRate); }

    void setSampleRate(float sr) noexcept
    {
        sampleRate_ = sr;
        osc_.setSampleRate(sr);
        midOsc_.setSampleRate(sr);
        pitchEnv_.setSampleRate(sr);
        midPitchEnv_.setSampleRate(sr);
        ampEnv_.setSampleRate(sr);
        midAmpEnv_.setSampleRate(sr);
        noise_.setSampleRate(sr);
        // Force click buffer regen on next trigger at the new rate.
        lastClickCenterHz_ = -1.0f;
    }

    // Start a 5 ms linear fadeout (voice being stolen). If a fadeout is
    // already in progress with a slower rate, the faster (newer) rate wins.
    void startFadeout(float sampleRate) noexcept
    {
        float samples = kVoiceFadeoutMs * 0.001f * sampleRate;
        if (samples < 1.0f) samples = 1.0f;
        const float newStep = fadeoutGain_ / samples;
        if (fadeoutStep_ == 0.0f || newStep > fadeoutStep_)
            fadeoutStep_ = newStep;
    }

    void trigger(const VoiceTrigger& t) noexcept
    {
        trig_ = t;
        fadeoutGain_ = 1.0f;
        fadeoutStep_ = 0.0f;
        samplePos_   = 0;
        // Sub HPF coefficient — cached at trigger time. One-pole HPF:
        //   y[n] = a * (y[n-1] + x[n] - x[n-1])
        // where a = 1 / (1 + (2π fc) / sr) approximated. Below 22 Hz we
        // disable entirely (coef = 1.0, no filtering — output passes
        // through; default 20 Hz acts as bypass).
        if (t.subHpfHz < 22.0f)
        {
            subHpfA_ = 1.0f;       // disabled — straight passthrough
        }
        else
        {
            constexpr float tau_h = 6.28318530717958647692f;
            const float rc = 1.0f / (tau_h * t.subHpfHz);
            const float dt = 1.0f / sampleRate_;
            subHpfA_ = rc / (rc + dt);
        }
        subHpfPrevX_ = 0.0f;
        subHpfPrevY_ = 0.0f;
        dcBlockX_ = 0.0f;
        dcBlockY_ = 0.0f;
        // COLOR filter: one-pole LP applied to the full body sum (mid +
        // click + noise + sample). Cutoff sweep matches NoiseGen's range
        // direction (color=0 → dark, color=1 → bright) but starts higher
        // so the kick body doesn't disappear at color=0. Coefficient cached
        // at trigger time so the per-sample tick is just one MAC.
        {
            constexpr float tau_c = 6.28318530717958647692f;
            const float color = t.noiseColor;
            const float maxHz = sampleRate_ * 0.49f;
            const float baseHz = 200.0f * std::pow(100.0f, color);
            const float cutoffHz = baseHz < maxHz ? baseHz : maxHz;
            const float rc = 1.0f / (tau_c * cutoffHz);
            const float dt = 1.0f / sampleRate_;
            bodyColorAlpha_ = dt / (rc + dt);
            bodyColorZ_ = 0.0f;
        }

        constexpr float halfPi = 1.57079632679489661923f;
        osc_.trigger(halfPi);
        midOsc_.trigger(halfPi);

        const float f0 = t.pitchStartHz;
        const float f1 = t.pitchEndHz < 1.0f ? 1.0f : t.pitchEndHz;
        pitchEnv_.trigger(f0, f1, t.pitchEnvDecayMs / 1000.0f, t.pitchCurve);

        const float m0 = t.midPitchStartHz;
        const float m1 = t.midPitchEndHz < 1.0f ? 1.0f : t.midPitchEndHz;
        // 2026-05-24 fix: cap mid pitch-env duration at 150 ms regardless
        // of midDecayMs. Was: pitch sweep duration = midDecayMs, which
        // meant longer DEC values stretched the sweep too — the mid sine
        // spent more time at the high end before settling, so users heard
        // the perceived "peak frequency" rise as DEC went up. Capping the
        // sweep restores the natural kick contour (short pitch transient,
        // then stable low rumble for as long as DEC dictates).
        //
        // Voice A has a dedicated `pitchDecay` param for this purpose —
        // Voice B was never given the same separation. If we ever want
        // user-controlled sweep length on Voice B too, add a midPitchDecay
        // param mirroring Voice A; until then 150 ms is the musical cap.
        constexpr float kMidPitchSweepMaxSec = 0.150f;
        const float midPitchEnvSec = juce::jmin(t.midDecayMs / 1000.0f,
                                                kMidPitchSweepMaxSec);
        midPitchEnv_.trigger(m0, m1, midPitchEnvSec, t.pitchCurve);

        // Pass 0 driftAmount — envelope quantisation is a no-op at 0.
        ampEnv_.triggerFull(t.ampDecayMs, t.ampAttackMs, 0.0f);
        midAmpEnv_.triggerFull(t.midDecayMs, t.ampAttackMs, 0.0f);

        // Only regenerate the click buffer when the center freq actually
        // moves — the trapezoidal SVF pass is O(decay_ms × SR), cheap but
        // wasteful per-hit if unchanged.
        if (std::abs(t.clickCenterHz - lastClickCenterHz_) > 1.0f)
        {
            const float c = t.clickCenterHz < 20.0f ? 20.0f : t.clickCenterHz;
            click_.regenerate(sampleRate_, 6.0f, c, 1.5f);
            lastClickCenterHz_ = t.clickCenterHz;
        }
        click_.trigger();
        noise_.trigger();
    }

    float tick() noexcept
    {
        // The SAMPLE layer bypasses ampEnv (it's a raw playback), so the
        // voice has to stay alive as long as EITHER the envelope is
        // running OR the sample still has frames to play. Without this
        // check, a 3 s sample with DEC=300 ms would get cut off after
        // 300 ms because the voice was declared inactive.
        const bool sampleStillPlaying = trig_.sampleBuf
            && samplePos_ < trig_.sampleBuf->getNumSamples();
        const bool voiceFinishedNaturally =
            ! ampEnv_.isActive() && ! sampleStillPlaying;

        // Once BOTH the envelope and the sample have run out, kick off
        // the existing 5 ms fadeout so the tail bleeds to true silence
        // even if the last sample frame wasn't already at zero.
        if (voiceFinishedNaturally
            && fadeoutStep_ <= 0.0f
            && fadeoutGain_ > 0.0f)
            startFadeout(sampleRate_);

        if (fadeoutGain_ <= 0.0f) return 0.0f;

        // SUB layer
        const float subFreq = pitchEnv_.tick();
        const float subOscOut = osc_.tickWave(subFreq, trig_.waveform);
        const float subAmp = ampEnv_.tick();
        float sub = subOscOut * subAmp;
        // Sub HPF — one-pole, cached at trigger. Bypassed when subHpfA_=1.
        if (subHpfA_ < 0.9999f)
        {
            const float y = subHpfA_ * (subHpfPrevY_ + sub - subHpfPrevX_);
            subHpfPrevX_ = sub;
            subHpfPrevY_ = y;
            if (std::fpclassify(subHpfPrevY_) == FP_SUBNORMAL) subHpfPrevY_ = 0.0f;
            sub = y;
        }

        // MID layer (always sine — avoid aliasing at body frequencies).
        // We still tick midPitchEnv / midAmpEnv / midOsc / noise / click
        // EVERY sample even when voiceBSynthOn is false, so internal state
        // (phase, envelope position, noise RNG) stays time-coherent —
        // toggling the gate mid-loop must not change the next-trigger
        // determinism guarantee. The gate is applied as a multiplication
        // on the synth contribution only; sample playback below is
        // untouched and still drives bodyMix when the gate is off.
        const float midFreq = midPitchEnv_.tick();
        const float midOscOut = midOsc_.tickWave(midFreq, WAVE_SINE);
        const float midAmp = midAmpEnv_.tick();
        const float synthGate = trig_.voiceBSynthOn ? 1.0f : 0.0f;
        const float mid = midOscOut * midAmp * trig_.midLevel * synthGate;

        // Transient + body texture (noise rides the MID envelope so it
        // doesn't hang on for the full sub decay). Same synthGate applies
        // so click + noise also fall silent when voiceBSynthOn is false.
        const float clickOut = click_.tick() * trig_.clickAmount * synthGate;
        const float noiseOut = noise_.tick(trig_.noiseColor)
                             * trig_.noiseAmount
                             * midAmp
                             * synthGate;

        // SAMPLE layer — shared_ptr held in the per-voice trig_ snapshot.
        // Plays once from start to end. Sample's baked-in fade-out + amp
        // shape are preserved; we now ALSO apply the mid amp envelope on
        // top so VOICE B's ATK and DEC knobs actually shape the sample.
        //
        // Before 2026-05-24 the sample bypassed midAmp entirely, so ATK
        // did nothing to a sample-only Voice B and DEC couldn't tighten
        // it — user couldn't sculpt the sample's attack/release at all.
        // Multiplying by midAmp gives users full ATK/DEC control while
        // leaving the underlying sample data untouched. Note: midAmpEnv
        // dies after midDecayMs, so very long samples may be clipped by
        // a short DEC; this is intentional — DEC is the gate length.
        float sampleOut = 0.0f;
        if (trig_.sampleBuf)
        {
            const auto& b = *trig_.sampleBuf;
            if (samplePos_ < b.getNumSamples())
            {
                sampleOut = b.getSample(0, samplePos_) * midAmp;
                ++samplePos_;
            }
        }

        // COLOR knob: one-pole LP across the full body sum so mid + click +
        // sample all darken together with the noise. (NoiseGen still does
        // its own coloring of the noise spectrum; this filter sits on top,
        // unifying the perceived "tone" of the whole body section.)
        const float bodyMix = mid + clickOut + noiseOut + sampleOut;
        bodyColorZ_ = bodyColorZ_ + bodyColorAlpha_ * (bodyMix - bodyColorZ_);
        if (std::fpclassify(bodyColorZ_) == FP_SUBNORMAL) bodyColorZ_ = 0.0f;

        // Apply Voice A / Voice B section mutes + A↔B balance (tent gain).
        // balance 0   → A only; 0.5 → both at unity; 1   → B only.
        const float bal = trig_.voiceBalance;
        const float aGain = bal <= 0.5f ? 1.0f : 1.0f - (bal - 0.5f) * 2.0f;
        const float bGain = bal >= 0.5f ? 1.0f : bal * 2.0f;
        const float subPart  = trig_.voiceAMute ? 0.0f : (sub * aGain);
        const float bodyPart = trig_.voiceBMute
            ? 0.0f
            : (bodyColorZ_ * bGain);
        const float raw = subPart + bodyPart;

        // DRIVE column mute bypasses the per-voice clipper as well as the
        // chain's B.AMT stage (RumbleChain already honors driveMute).
        float shaped;
        if (trig_.driveMute)
        {
            shaped = raw;
        }
        else
        {
            // BIAS: DC offset into waveshaper → asymmetric clipping, even harmonics.
            const float biased = raw + trig_.driveBias * 0.4f;
            shaped = voiceClipApply(trig_.driveMode, trig_.driveAmount, biased);

            // DC blocker (one-pole HP at ~5 Hz): removes the offset that
            // asymmetric clipping introduces. y[n] = x[n] - x[n-1] + R*y[n-1].
            if (std::abs(trig_.driveBias) > 0.001f)
            {
                const float y = shaped - dcBlockX_ + 0.9997f * dcBlockY_;
                dcBlockX_ = shaped;
                dcBlockY_ = y;
                if (std::fpclassify(dcBlockY_) == FP_SUBNORMAL) dcBlockY_ = 0.0f;
                shaped = y;
            }
        }

        const float out = shaped * fadeoutGain_;

        if (fadeoutStep_ > 0.0f)
        {
            fadeoutGain_ -= fadeoutStep_;
            if (fadeoutGain_ <= 0.0f) { fadeoutGain_ = 0.0f; fadeoutStep_ = 0.0f; }
        }
        return out;
    }

    bool isActive() const noexcept
    {
        const bool sampleStillPlaying = trig_.sampleBuf
            && samplePos_ < trig_.sampleBuf->getNumSamples();
        return (ampEnv_.isActive() || sampleStillPlaying)
            && fadeoutGain_ > 0.0f;
    }

private:
    SineOsc        osc_{};
    PitchEnvelope  pitchEnv_{};
    AmpEnvelope    ampEnv_{};
    SineOsc        midOsc_{};
    PitchEnvelope  midPitchEnv_{};
    AmpEnvelope    midAmpEnv_{};
    ClickGen       click_{};
    NoiseGen       noise_{};

    VoiceTrigger   trig_{};
    int            samplePos_ = 0;
    float          sampleRate_ = 48000.0f;
    float          lastClickCenterHz_ = -1.0f;
    float          fadeoutGain_ = 1.0f;
    float          fadeoutStep_ = 0.0f;
    // COLOR (unified body LP) — cached at trigger time.
    float          bodyColorAlpha_ = 1.0f;
    float          bodyColorZ_     = 0.0f;
    // Sub HPF — one-pole, cached at trigger time. subHpfA_ = 1.0 ⇒ bypass.
    float          subHpfA_      = 1.0f;
    float          subHpfPrevX_  = 0.0f;
    float          subHpfPrevY_  = 0.0f;
    // DC blocker for BIAS — one-pole HP state. Reset at trigger.
    float          dcBlockX_     = 0.0f;
    float          dcBlockY_     = 0.0f;
};

} // namespace bombo
