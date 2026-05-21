#pragma once

#include "BiquadFilter.h"
#include "Delay.h"
#include "FdnReverb.h"
#include "Ducker.h"
#include "MultibandLimiter.h"
#include "MasterBus.h"
#include "VoiceClip.h"

namespace bombo
{

// Lightweight Phase-1b chain: serial routing through DRIVE → FILTER →
// DELAY → REVERB, with the dry voice ducked by itself before mixing in
// wet, then the multiband limiter and master bus on the sum.
//
// The full param surface (RumbleParams with serial/parallel toggle and
// FxOrder permutation) lands in Phase 2 along with APVTS wiring. For now
// the chain holds musical defaults so a kick + rumble can be auditioned.
struct ChainParams
{
    // DRIVE
    float driveAmount = 0.0f;
    int   driveMode   = VC_TANH;
    float driveMix    = 1.0f;
    // FILTER
    float hpHz = 30.0f;   float hpQ = 0.707f;
    float lpHz = 4500.0f; float lpQ = 0.707f;
    float filterColor = 0.0f; // 0..1 — drive on LP feedback
    // DELAY
    float delayMs       = 250.0f;
    float delayFeedback = 0.0f;
    float delayCrumble  = 0.0f;
    // delayTimeMode: 0 = Free, otherwise index into the note-value list
    // declared in createParameterLayout (1=1/2, 2=1/2., 3=1/2T, etc).
    // hostBpm is the effective BPM (host's value if available, else the
    // BPM param) — fed in from PluginProcessor::buildChainParamsFromApvts.
    int   delayTimeMode = 0;
    float hostBpm       = 120.0f;
    float delayMorph    = 0.5f;
    float delayMix      = 0.0f;
    // REVERB
    float reverbSize       = 0.5f;
    float reverbDecay      = 0.6f;
    float reverbDamp       = 0.3f;
    float reverbDiffusion  = 0.5f;
    float reverbPredelayMs = 20.0f;
    float reverbMix        = 0.0f;
    // DUCKER
    float duckAttackMs  = 2.0f;
    float duckHoldMs    = 0.0f;
    float duckReleaseMs = 250.0f;
    float duckDepth     = 0.0f;
    float duckShape     = 0.0f;
    float duckSnap      = 0.0f;
    // LIMITER
    bool  limiterOn     = true;
    float limiterAmount = 0.5f;
    // SECTION MUTES — when set, the named stage passes its input through
    // unmodified (filter) or contributes zero (delay/reverb), so the
    // user can A/B with that effect bypassed.
    bool  driveMute  = false;
    bool  filterMute = false;
    bool  delayMute  = false;
    bool  reverbMute = false;
    bool  duckMute   = false;
};

class RumbleChain
{
public:
    explicit RumbleChain(float sampleRate = 48000.0f)
        : sampleRate_(sampleRate),
          delay_(sampleRate),
          reverb_(sampleRate),
          ducker_(sampleRate),
          multiband_(sampleRate),
          masterBus_(sampleRate)
    {
        hpFilter_.setHpf(sampleRate, 30.0f, 0.707f);
        lpFilter_.setLpf(sampleRate, 4500.0f, 0.707f);
    }

    void setSampleRate(float sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        delay_.setSampleRate(sampleRate);
        // Reverb has a single-shot constructor; rebuild only if user changes
        // SR mid-session — acceptable for now (not an audio-thread path).
        reverb_ = FdnReverb(sampleRate);
        ducker_.setSampleRate(sampleRate);
        multiband_.setSampleRate(sampleRate);
        masterBus_.setSampleRate(sampleRate);
        hpFilter_.setHpf(sampleRate, lastHpHz_, lastHpQ_);
        lpFilter_.setLpf(sampleRate, lastLpHz_, lastLpQ_);
    }

    void reset() noexcept
    {
        hpFilter_.reset();
        lpFilter_.reset();
        delay_.reset();
        reverb_.reset();
        ducker_.reset();
        multiband_.reset();
        masterBus_.reset();
    }

    // Called from the trigger handler so each kick is its own discrete
    // reverb + delay event (Maddix-Rumbler behaviour).
    void killTail() noexcept
    {
        delay_.killTail();
        reverb_.killTail();
    }

    void update(const ChainParams& p) noexcept
    {
        // Filter coefs — only recompute when params actually moved.
        if (std::abs(p.hpHz - lastHpHz_) > 0.5f || std::abs(p.hpQ - lastHpQ_) > 0.001f)
        {
            hpFilter_.setHpf(sampleRate_, p.hpHz, p.hpQ);
            lastHpHz_ = p.hpHz; lastHpQ_ = p.hpQ;
        }
        if (std::abs(p.lpHz - lastLpHz_) > 0.5f || std::abs(p.lpQ - lastLpQ_) > 0.001f)
        {
            lpFilter_.setLpf(sampleRate_, p.lpHz, p.lpQ);
            lastLpHz_ = p.lpHz; lastLpQ_ = p.lpQ;
        }
        lpFilter_.setDrive(p.filterColor);

        // Tempo-sync mode: when delayTimeMode != 0 the effective delay
        // length is computed from host BPM × note-value, overriding the
        // user-set TIME knob. Index 0 means free-running on the TIME
        // knob value (ms). Note-value beat ratios match the StringArray
        // declared in Parameters.h (1/2, 1/2., 1/2T, 1/4, 1/4., 1/4T,
        // 1/8, 1/8., 1/8T, 1/16, 1/16., 1/16T, 1/32).
        const float effectiveMs = [&]() noexcept -> float
        {
            if (p.delayTimeMode <= 0 || p.hostBpm < 1.0f) return p.delayMs;
            constexpr float beats[] = {
                /*idx 1*/  2.0f,  3.0f,  4.0f / 3.0f,        // 1/2, 1/2., 1/2T
                /*idx 4*/  1.0f,  1.5f,  2.0f / 3.0f,        // 1/4, 1/4., 1/4T
                /*idx 7*/  0.5f,  0.75f, 1.0f / 3.0f,        // 1/8, 1/8., 1/8T
                /*idx 10*/ 0.25f, 0.375f, 1.0f / 6.0f,       // 1/16, 1/16., 1/16T
                /*idx 13*/ 0.125f                            // 1/32
            };
            const int   i  = juce::jlimit(0, 12, p.delayTimeMode - 1);
            const float ms = beats[i] * (60000.0f / p.hostBpm);
            return juce::jlimit(1.0f, 5000.0f, ms);
        }();
        delay_.setTimeMs(effectiveMs);
        delay_.setFeedback(p.delayFeedback);
        delay_.setDrift(0.0f);   // drift retired — kept as no-op for now
        delay_.setFilterMorph(p.delayMorph);
        delay_.setCrumble(p.delayCrumble);

        reverb_.setParams(p.reverbDecay, p.reverbDamp);
        reverb_.setSize(p.reverbSize);
        reverb_.setDiffusion(p.reverbDiffusion);
        reverb_.setPredelayMs(p.reverbPredelayMs);

        ducker_.setTimesMs(p.duckAttackMs, p.duckReleaseMs);
        ducker_.setHoldMs(p.duckHoldMs);
        ducker_.setShape(p.duckShape);
        ducker_.setSnap(p.duckSnap);

        params_ = p;
    }

    // Process one sample. dry = current voice-pool sum.
    float process(float dry) noexcept
    {
        // DRIVE on the dry path (voice-clip family — same shaper palette).
        float driven = dry;
        if (!params_.driveMute
            && params_.driveAmount > 0.0f
            && params_.driveMode != VC_OFF)
        {
            const float shaped = voiceClipApply(params_.driveMode,
                                                params_.driveAmount, dry);
            driven = dry * (1.0f - params_.driveMix) + shaped * params_.driveMix;
        }

        // FILTER (HP then LP — kicks want the rumble bus AC-coupled and
        // top-trimmed before it enters the wet stages). Mute bypasses to
        // raw input — but we still process the filters silently so their
        // state stays warm and re-enabling doesn't pop.
        float filtered = driven;
        if (!params_.filterMute)
            filtered = lpFilter_.process(hpFilter_.process(driven));

        // DELAY and REVERB run from the same filtered source, summed wet.
        // Muted stages contribute zero to the wet sum so the dry kick
        // passes through cleanly.
        const float dWet = params_.delayMute  ? 0.0f : delay_.process(filtered);
        const float rWet = params_.reverbMute ? 0.0f : reverb_.process(filtered);
        float wet = dWet * params_.delayMix + rWet * params_.reverbMix;

        // Sidechain duck the wet off the dry (filtered) signal.
        if (!params_.duckMute)
            wet = ducker_.process(filtered, wet, params_.duckDepth);

        const float sum = filtered + wet;
        const float limited = multiband_.process(sum, params_.limiterOn, params_.limiterAmount);
        return masterBus_.process(limited);
    }

private:
    float sampleRate_;
    BiquadFilter hpFilter_;
    BiquadFilter lpFilter_;
    Delay delay_;
    FdnReverb reverb_;
    Ducker ducker_;
    MultibandLimiter multiband_;
    MasterBus masterBus_;
    ChainParams params_{};
    float lastHpHz_ = 30.0f, lastHpQ_ = 0.707f;
    float lastLpHz_ = 4500.0f, lastLpQ_ = 0.707f;
};

} // namespace bombo
