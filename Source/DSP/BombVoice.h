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
    // Section mute snapshots — captured at note-on so they apply for the
    // voice's lifetime. driveMute also bypasses the voice clipper so the
    // whole DRIVE column quiets when toggled off, not just the rumble bus.
    bool  voiceAMute      = false;
    bool  voiceBMute      = false;
    bool  driveMute       = false;
    // Sample-slot layer (VOICE B). shared_ptr<const> is copied by the audio
    // thread at trigger time — the refcount bump is allocator-free on
    // libstdc++ x86_64. Null = no sample loaded; voice just skips the mix.
    std::shared_ptr<const juce::AudioBuffer<float>> sampleBuf{};

    // VOICE A ↔ VOICE B balance (tent). 0 = A only, 0.5 = both at unity,
    // 1 = B only.
    float voiceBalance    = 0.5f;
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
        midPitchEnv_.trigger(m0, m1, t.midDecayMs / 1000.0f, t.pitchCurve);

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
        if (!ampEnv_.isActive() || fadeoutGain_ <= 0.0f) return 0.0f;

        // SUB layer
        const float subFreq = pitchEnv_.tick();
        const float subOscOut = osc_.tickWave(subFreq, trig_.waveform);
        const float subAmp = ampEnv_.tick();
        const float sub = subOscOut * subAmp;

        // MID layer (always sine — avoid aliasing at body frequencies)
        const float midFreq = midPitchEnv_.tick();
        const float midOscOut = midOsc_.tickWave(midFreq, WAVE_SINE);
        const float midAmp = midAmpEnv_.tick();
        const float mid = midOscOut * midAmp * trig_.midLevel;

        // Transient + body texture (noise rides the MID envelope so it
        // doesn't hang on for the full sub decay).
        const float clickOut = click_.tick() * trig_.clickAmount;
        const float noiseOut = noise_.tick(trig_.noiseColor)
                             * trig_.noiseAmount
                             * midAmp;

        // SAMPLE layer — shared_ptr held in the per-voice trig_ snapshot.
        // Plays once from start to end; fade-out + amplitude shape are baked
        // in at load time so we just read the buffer here.
        float sampleOut = 0.0f;
        if (trig_.sampleBuf)
        {
            const auto& b = *trig_.sampleBuf;
            if (samplePos_ < b.getNumSamples())
            {
                sampleOut = b.getSample(0, samplePos_);
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
        const float shaped = trig_.driveMute
            ? raw
            : voiceClipApply(trig_.driveMode, trig_.driveAmount, raw);

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
        return ampEnv_.isActive() && fadeoutGain_ > 0.0f;
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
};

} // namespace bombo
