#pragma once

#include "Oscillators.h"
#include "Envelopes.h"
#include "ClickGen.h"
#include "NoiseGen.h"
#include "Drift.h"
#include "VoiceClip.h"

namespace bombo
{

// Per-trigger snapshot of all knob state. Captured once at note-on so the
// per-sample tick() reads stable values even if params update mid-decay.
// SUB layer = fundamental (sustained); MID layer = body punch (short).
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
    // Jitter
    float driftAmount     = 0.0f;
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
        driftSample_ = drift_.sampleEnvelope(t.driftAmount);
        pitchJitter_ = drift_.pitchJitter(t.driftAmount);
        fadeoutGain_ = 1.0f;
        fadeoutStep_ = 0.0f;

        constexpr float halfPi = 1.57079632679489661923f;
        osc_.trigger(halfPi);
        midOsc_.trigger(halfPi);

        const float f0 = t.pitchStartHz * pitchJitter_;
        const float f1 = t.pitchEndHz < 1.0f ? 1.0f : t.pitchEndHz;
        pitchEnv_.trigger(f0, f1, t.pitchEnvDecayMs / 1000.0f, t.pitchCurve);

        const float m0 = t.midPitchStartHz * pitchJitter_;
        const float m1 = t.midPitchEndHz < 1.0f ? 1.0f : t.midPitchEndHz;
        midPitchEnv_.trigger(m0, m1, t.midDecayMs / 1000.0f, t.pitchCurve);

        const float decay = t.ampDecayMs * driftSample_.decayScale;
        ampEnv_.triggerFull(decay, t.ampAttackMs, t.driftAmount);

        const float midDecay = t.midDecayMs * driftSample_.decayScale;
        midAmpEnv_.triggerFull(midDecay, t.ampAttackMs, t.driftAmount);

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

        const float raw = sub + mid + clickOut + noiseOut;
        const float shaped = voiceClipApply(trig_.driveMode, trig_.driveAmount, raw);

        const float out = shaped * driftSample_.ampScale * fadeoutGain_;

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
    Drift          drift_{};

    VoiceTrigger   trig_{};
    DriftSample    driftSample_{};
    float          pitchJitter_ = 1.0f;
    float          sampleRate_ = 48000.0f;
    float          lastClickCenterHz_ = -1.0f;
    float          fadeoutGain_ = 1.0f;
    float          fadeoutStep_ = 0.0f;
};

} // namespace bombo
