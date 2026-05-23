#pragma once

#include "BiquadFilter.h"
#include "Delay.h"
#include "ConvolutionReverb.h"
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
    // TEETH: LP cutoff tracks pitch envelope. -1..+1; 0 = no tracking.
    float filterTeeth = 0.0f;
    // Pitch envelope decay (ms) — fed from VoiceTrigger so TEETH can
    // mirror the pitch sweep duration. Only used when filterTeeth != 0.
    float pitchDecayMs = 80.0f;
    // DELAY
    float delayMs       = 250.0f;
    float delayFeedback = 0.0f;
    float delaySmear    = 0.0f;
    // delayTimeMode: 0 = Free, otherwise index into the note-value list
    // declared in createParameterLayout (1=1/2, 2=1/2., 3=1/2T, etc).
    // hostBpm is the effective BPM (host's value if available, else the
    // BPM param) — fed in from PluginProcessor::buildChainParamsFromApvts.
    int   delayTimeMode = 0;
    float hostBpm       = 120.0f;
    float delayMorph    = 0.5f;
    float delayMix      = 0.0f;
    // REVERB
    // reverbType: index into IRBank algos (0=Room, 1=Plate, 2=Hall,
    // 3=Spring, 4=Chamber, 5=Bunker). Replaces the FdnReverb's single
    // engine — see ConvolutionReverb.h for the per-hit identity story.
    int   reverbType       = 2;   // Hall
    float reverbSize       = 0.5f;
    float reverbDecay      = 0.6f;
    float reverbDamp       = 0.3f;
    float reverbDiffusion  = 0.5f; // hidden — kept for preset back-compat
    float reverbPredelayMs = 20.0f;
    float reverbMix        = 0.0f;
    // DUCKER
    float duckAttackMs  = 2.0f;
    float duckHoldMs    = 0.0f;
    float duckReleaseMs = 250.0f;
    float duckDepth     = 0.0f;
    float duckShape     = 0.0f;
    float duckGrowl     = 0.0f;
    // LIMITER
    bool  limiterOn     = true;
    float limiterAmount = 0.5f;
    // TAIL KILL: governs per-trigger chop semantics across the FX bus.
    // ON  = chop tails between trigs in any mode (delay buffer flush,
    //       reverb conv reset, fresh per-trig wet identity).
    // OFF = let tails ring naturally in any mode (no flush, layered
    //       hits, no per-trig wet-bus envelope).
    bool  tailKillOn    = true;
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
        // Reverb resynthesizes its IR bank at the new sample rate.
        // Non-audio-thread path (prepareToPlay only).
        reverb_.setSampleRate(sampleRate);
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

    // Called at each kick trigger alongside killTail(). Starts the TEETH
    // pitch-tracking envelope so the LP cutoff sweeps with the pitch,
    // and resets the convolution reverb's per-trigger decay envelope so
    // every hit starts the wet bus from identical state.
    void onTrigger(float pitchDecayMs) noexcept
    {
        teethEnv_ = 1.0f;
        const float decayS = (pitchDecayMs < 1.0f ? 1.0f : pitchDecayMs) * 0.001f;
        teethEnvCoef_ = std::exp(-1.0f / (decayS * sampleRate_));
        reverb_.onTrigger();
    }

    void update(const ChainParams& p) noexcept
    {
        // Filter coefs — only recompute when params actually moved.
        if (std::abs(p.hpHz - lastHpHz_) > 0.5f || std::abs(p.hpQ - lastHpQ_) > 0.001f)
        {
            hpFilter_.setHpf(sampleRate_, p.hpHz, p.hpQ);
            lastHpHz_ = p.hpHz; lastHpQ_ = p.hpQ;
        }
        // TEETH: modulate LP cutoff with the pitch-tracking envelope.
        // teethEnv_ is advanced per-sample in process(); update() reads the
        // current value once per block (block-rate LP update, inaudible lag).
        float effectiveLpHz = p.lpHz;
        if (std::abs(p.filterTeeth) > 0.001f && teethEnv_ > 0.001f)
        {
            const float semitones = p.filterTeeth * 24.0f * teethEnv_;
            effectiveLpHz = p.lpHz * std::pow(2.0f, semitones / 12.0f);
            const float maxHz = sampleRate_ * 0.49f;
            effectiveLpHz = effectiveLpHz < 20.0f ? 20.0f
                          : (effectiveLpHz > maxHz ? maxHz : effectiveLpHz);
        }
        if (std::abs(effectiveLpHz - lastLpHz_) > 0.5f || std::abs(p.lpQ - lastLpQ_) > 0.001f)
        {
            lpFilter_.setLpf(sampleRate_, effectiveLpHz, p.lpQ);
            lastLpHz_ = effectiveLpHz; lastLpQ_ = p.lpQ;
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
        delay_.setDrift(p.delaySmear);
        delay_.setFilterMorph(p.delayMorph);

        reverb_.setType(p.reverbType);
        reverb_.setSize(p.reverbSize);
        reverb_.setDecay(p.reverbDecay);
        reverb_.setDamp(p.reverbDamp);
        reverb_.setPredelayMs(p.reverbPredelayMs);
        reverb_.setTailKillOn(p.tailKillOn);
        delay_.setTailKillOn(p.tailKillOn);
        // p.reverbDiffusion intentionally ignored — param kept for
        // preset back-compat but the IR has built-in diffusion.

        ducker_.setTimesMs(p.duckAttackMs, p.duckReleaseMs);
        ducker_.setHoldMs(p.duckHoldMs);
        ducker_.setShape(p.duckShape);
        ducker_.setGrowl(p.duckGrowl);

        params_ = p;
    }

    // Process one sample. dry = current voice-pool sum.
    float process(float dry) noexcept
    {
        // TEETH: advance pitch-tracking envelope every sample so update()
        // picks up the current value at block rate for LP modulation.
        if (teethEnv_ > 0.0001f)
        {
            teethEnv_ *= teethEnvCoef_;
            if (std::fpclassify(teethEnv_) == FP_SUBNORMAL) teethEnv_ = 0.0f;
        }

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
    ConvolutionReverb reverb_;
    Ducker ducker_;
    MultibandLimiter multiband_;
    MasterBus masterBus_;
    ChainParams params_{};
    float lastHpHz_ = 30.0f, lastHpQ_ = 0.707f;
    float lastLpHz_ = 4500.0f, lastLpQ_ = 0.707f;
    // TEETH pitch-tracking envelope state
    float teethEnv_     = 0.0f;
    float teethEnvCoef_ = 1.0f;
};

} // namespace bombo
