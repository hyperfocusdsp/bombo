#pragma once

#include <array>
#include <vector>
#include <cmath>
#include <cstdint>

namespace bombo
{

// Maddix-style rumble wash. Hadamard 4-line FDN with input allpass
// diffusion, per-line in-loop LP damping, post-LPF (the user-facing
// "Damp" knob), fixed 30 Hz post-HPF, predelay, transport-stop fade.
// Port of dsp/reverb.rs — math is verbatim, this is Bombo's signature.
class FdnReverb
{
public:
    static constexpr int kMaxFdnSamples = 4096;
    static constexpr int kMaxAllpassSamples = 1024;
    static constexpr int kMaxPredelaySamples = 32768;
    static constexpr float kStopFadeMs = 80.0f;
    static constexpr float kKillFadeMs = 6.0f;   // per-trigger smooth tail kill

    // FDN delay-line lengths @ 44.1 kHz — coprime primes, ~octave spread.
    static constexpr int kFdnLengths44k[4] = { 743, 1093, 1361, 1697 };
    // Input-diffuser allpass lengths @ 44.1 kHz.
    static constexpr int kDiffuserLengths44k[4] = { 225, 341, 441, 556 };
    // Per-line LFO rates (irrational + coprime to avoid mutual reinforcement).
    static constexpr float kFdnLfoRatesHz[4] = { 0.39f, 0.51f, 0.73f, 1.07f };
    // Peak read-tap deviation @ 48 kHz (scales with SR at construction).
    static constexpr float kFdnLfoDepthSamples48k = 2.0f;
    // In-loop LP damping coefficient (always on, fixed).
    static constexpr float kFdnFbLpCoef = 0.18f;

    explicit FdnReverb(float sampleRate = 48000.0f)
        : predelay_(static_cast<size_t>(kMaxPredelaySamples), 0.0f)
    {
        const float scale = sampleRate / 44100.0f;
        for (int i = 0; i < 4; ++i)
        {
            int diffuserLen = static_cast<int>(static_cast<float>(kDiffuserLengths44k[i]) * scale);
            if (diffuserLen > kMaxAllpassSamples) diffuserLen = kMaxAllpassSamples;
            diffuser_[i].init(kMaxAllpassSamples, diffuserLen);

            int fdnLen = static_cast<int>(static_cast<float>(kFdnLengths44k[i]) * scale);
            if (fdnLen > kMaxFdnSamples) fdnLen = kMaxFdnSamples;
            fdnBaseLengths_[i] = fdnLen;
            fdn_[i].init(kMaxFdnSamples, fdnLen, kFdnLfoRatesHz[i], sampleRate);
        }
        sampleRate_ = sampleRate;

        // Wet LPF default cutoff ~200 Hz, post-HPF pole at 30 Hz.
        wetLpfCoef_ = lpfCoefFromHz(200.0f, sampleRate);
        wetLpfTargetCoef_ = wetLpfCoef_;
        constexpr float tau_c = 6.28318530717958647692f;
        wetHpfR_ = 1.0f - (tau_c * 30.0f / sampleRate);
    }

    // Smooth tail kill on fresh trigger. Instead of zeroing state at the
    // moment killTail() is called (which clicks when wet level is non-zero),
    // schedule a short linear fade-down — process() applies the ramp and
    // zeroes state at the bottom. ~6 ms is short enough to feel like a hard
    // stop but long enough to mask the discontinuity.
    void killTail() noexcept
    {
        stopFadeRemaining_ = 0;
        stopMuted_ = false;
        if (killFadeRemaining_ == 0)
        {
            uint32_t fadeSamples = static_cast<uint32_t>(kKillFadeMs * 0.001f * sampleRate_);
            if (fadeSamples < 1) fadeSamples = 1;
            killFadeRemaining_ = fadeSamples;
            killFadeTotal_     = fadeSamples;
        }
    }

    // For prepareToPlay only — instant hard reset without a fade.
    void hardReset() noexcept
    {
        stopFadeRemaining_ = 0;
        stopMuted_ = false;
        killFadeRemaining_ = 0;
        killFadeTotal_     = 0;
        for (auto& ap : diffuser_) ap.reset();
        for (auto& line : fdn_) line.reset();
        wetLpfZ_ = 0.0f;
        wetHpfX1_ = 0.0f;
        wetHpfY1_ = 0.0f;
    }

    void fullReset() noexcept
    {
        hardReset();
        for (auto& s : predelay_) s = 0.0f;
        predelayPos_ = 0;
    }

    void stopFade() noexcept
    {
        if (stopMuted_ || stopFadeRemaining_ > 0) return;
        uint32_t fadeSamples = static_cast<uint32_t>(kStopFadeMs * 0.001f * sampleRate_);
        if (fadeSamples < 1) fadeSamples = 1;
        stopFadeRemaining_ = fadeSamples;
        stopFadeTotal_ = fadeSamples;
    }

    void reset() noexcept { fullReset(); }

    // decay 0..1 → fb_gain 0.85..0.985.
    // damp 0..1 → wet-LPF cutoff 800 Hz → 80 Hz exponential.
    void setParams(float decay, float damp) noexcept
    {
        if (decay < 0.0f) decay = 0.0f; if (decay > 1.0f) decay = 1.0f;
        if (damp  < 0.0f) damp  = 0.0f; if (damp  > 1.0f) damp  = 1.0f;
        fbGainTarget_ = 0.85f + decay * 0.135f;
        const float cutoffHz = 800.0f * std::pow(80.0f / 800.0f, damp);
        wetLpfTargetCoef_ = lpfCoefFromHz(cutoffHz, sampleRate_);
    }

    // size 0..1 scales FDN delay lengths around nominal (0.7×..1.3×).
    void setSize(float size) noexcept
    {
        if (size < 0.0f) size = 0.0f; if (size > 1.0f) size = 1.0f;
        const float mul = 0.7f + size * 0.6f;
        for (int i = 0; i < 4; ++i)
        {
            float target = static_cast<float>(fdnBaseLengths_[i]) * mul;
            if (target < 2.0f) target = 2.0f;
            const float maxLen = static_cast<float>(kMaxFdnSamples - 1);
            if (target > maxLen) target = maxLen;
            fdn_[i].lengthTarget = target;
        }
    }

    // diffusion 0..1 crossfades raw predelay vs fully-diffused at FDN input.
    void setDiffusion(float diffusion) noexcept
    {
        if (diffusion < 0.0f) diffusion = 0.0f; if (diffusion > 1.0f) diffusion = 1.0f;
        diffuserAmount_ = diffusion;
        for (auto& ap : diffuser_) ap.feedback = 0.62f;
    }

    void setPredelayMs(float predelayMs) noexcept
    {
        const int maxSamples = static_cast<int>(predelay_.size()) - 1;
        if (predelayMs < 0.0f) predelayMs = 0.0f;
        int n = static_cast<int>(predelayMs / 1000.0f * sampleRate_);
        if (n > maxSamples) n = maxSamples;
        predelaySamples_ = n;
    }

    float process(float input) noexcept
    {
        if (stopMuted_) return 0.0f;

        // Smooth feedback gain + LPF coef toward targets (~10 ms).
        constexpr float smooth = 0.999f;
        fbGain_ += (fbGainTarget_ - fbGain_) * (1.0f - smooth);
        wetLpfCoef_ += (wetLpfTargetCoef_ - wetLpfCoef_) * (1.0f - smooth);

        // Predelay. Bug fix 2026-05-17: during the kill-fade window, write
        // ZERO instead of `input` so the predelay buffer doesn't capture
        // any voice-fadeout tail or chain-feedback content that arrives
        // while killTail is running. Otherwise that captured content
        // emerges from the predelay readout AFTER the kill-fade closes
        // and re-spins the FDN tail — same root cause as the Delay fix.
        const int pdLen = static_cast<int>(predelay_.size());
        predelay_[predelayPos_] = (killFadeRemaining_ > 0) ? 0.0f : input;
        const int pdRead = (predelayPos_ + pdLen - predelaySamples_) % pdLen;
        const float predelayed = predelay_[pdRead];
        predelayPos_ = (predelayPos_ + 1) % pdLen;

        // Input diffuser (4 nested allpasses; FDN sees a crossfade
        // between raw predelay and full diffusion).
        float diffused = predelayed;
        for (auto& ap : diffuser_) diffused = ap.process(diffused);
        const float fdnIn = predelayed * (1.0f - diffuserAmount_)
                          + diffused  * diffuserAmount_;

        // FDN read pass.
        float reads[4];
        for (int i = 0; i < 4; ++i)
        {
            const float y = fdn_[i].read();
            reads[i] = fdn_[i].damp(y);
        }
        // Hadamard 4 — unitary mix, all eigenvalues |1|.
        constexpr float h = 0.5f;
        const float m0 = h * (reads[0] + reads[1] + reads[2] + reads[3]);
        const float m1 = h * (reads[0] - reads[1] + reads[2] - reads[3]);
        const float m2 = h * (reads[0] + reads[1] - reads[2] - reads[3]);
        const float m3 = h * (reads[0] - reads[1] - reads[2] + reads[3]);

        fdn_[0].write(fdnIn + fbGain_ * m0);
        fdn_[1].write(fdnIn + fbGain_ * m1);
        fdn_[2].write(fdnIn + fbGain_ * m2);
        fdn_[3].write(fdnIn + fbGain_ * m3);

        const float wetPre = (reads[0] + reads[1] + reads[2] + reads[3]) * 0.5f;

        // Post-LPF (user-facing Damp).
        wetLpfZ_ = wetLpfZ_ + wetLpfCoef_ * (wetPre - wetLpfZ_);
        if (std::fpclassify(wetLpfZ_) == FP_SUBNORMAL) wetLpfZ_ = 0.0f;

        // Post-HPF (fixed 30 Hz, 1-pole).
        const float hpfIn = wetLpfZ_;
        const float hpfOut = hpfIn - wetHpfX1_ + wetHpfR_ * wetHpfY1_;
        wetHpfX1_ = hpfIn;
        wetHpfY1_ = hpfOut;
        float out = hpfOut;

        // Per-trigger smooth tail kill — linear ramp down to 0 over the
        // fade window. At the bottom we zero ALL state via fullReset()
        // so the next trigger starts from silence. Click-free hard stop.
        //
        // Bug fix 2026-05-17: previously only diffuser + FDN lines + LPF
        // + HPF were zeroed, leaving the predelay buffer holding up to
        // 500 ms of stale kick audio. After the fade window closed, the
        // predelay readout kept feeding the (just-reset) FDN, which then
        // rebuilt a new "tail" from that stale content. User reported
        // hearing a ~20 s residual ring after a single trigger — that
        // was the FDN re-spinning up off the un-cleared predelay.
        if (killFadeRemaining_ > 0)
        {
            const float ramp = static_cast<float>(killFadeRemaining_)
                             / static_cast<float>(killFadeTotal_);
            out *= ramp;
            --killFadeRemaining_;
            if (killFadeRemaining_ == 0)
                fullReset();   // includes predelay zero
        }

        // Transport-stop fade.
        if (stopFadeRemaining_ > 0)
        {
            const float ramp = static_cast<float>(stopFadeRemaining_)
                             / static_cast<float>(stopFadeTotal_);
            out *= ramp;
            --stopFadeRemaining_;
            if (stopFadeRemaining_ == 0)
            {
                fullReset();
                stopMuted_ = true;
            }
        }
        return out;
    }

private:
    struct Allpass
    {
        std::vector<float> buffer;
        int pos = 0;
        float feedback = 0.5f;
        int length = 1;

        void init(int maxSamples, int len) noexcept
        {
            buffer.assign(static_cast<size_t>(maxSamples), 0.0f);
            pos = 0;
            length = len < 1 ? 1 : (len > maxSamples ? maxSamples : len);
        }
        float process(float input) noexcept
        {
            const int readPos = (pos + length - 1) % length;
            const float bufOut = buffer[readPos];
            const float out = -input + bufOut;
            buffer[pos] = input + bufOut * feedback;
            pos = (pos + 1) % length;
            return out;
        }
        void reset() noexcept
        {
            for (auto& s : buffer) s = 0.0f;
            pos = 0;
        }
    };

    struct FdnLine
    {
        std::vector<float> buffer;
        int writePos = 0;
        float length = 2.0f;
        float lengthTarget = 2.0f;
        float lengthSmooth = 0.0f;
        float lfoPhase = 0.0f;
        float lfoInc = 0.0f;
        float lfoDepth = 0.0f;
        float fbLpZ = 0.0f;

        void init(int maxSamples, int len, float lfoRateHz, float sampleRate) noexcept
        {
            buffer.assign(static_cast<size_t>(maxSamples), 0.0f);
            int safe = len > maxSamples ? maxSamples : (len < 2 ? 2 : len);
            length = static_cast<float>(safe);
            lengthTarget = length;
            lengthSmooth = 1.0f - std::exp(-1.0f / (0.120f * sampleRate));
            constexpr float tau_c = 6.28318530717958647692f;
            lfoInc = lfoRateHz / sampleRate * tau_c;
            lfoDepth = kFdnLfoDepthSamples48k * (sampleRate / 48000.0f);
            writePos = 0;
            fbLpZ = 0.0f;
            lfoPhase = 0.0f;
        }

        float read() noexcept
        {
            constexpr float tau_c = 6.28318530717958647692f;
            length += (lengthTarget - length) * lengthSmooth;
            lfoPhase += lfoInc;
            if (lfoPhase >= tau_c) lfoPhase -= tau_c;
            const float modOffset = std::sin(lfoPhase) * lfoDepth;
            const int bufLen = static_cast<int>(buffer.size());
            const float bufLenF = static_cast<float>(bufLen);
            float delay = length + modOffset;
            if (delay < 1.0f) delay = 1.0f;
            const float maxDelay = static_cast<float>(bufLen - 1);
            if (delay > maxDelay) delay = maxDelay;
            float readF = static_cast<float>(writePos) + bufLenF - delay;
            readF -= std::floor(readF / bufLenF) * bufLenF;
            const int idx0 = static_cast<int>(readF) % bufLen;
            const int idx1 = (idx0 + 1) % bufLen;
            const float frac = readF - std::floor(readF);
            return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
        }

        float damp(float x) noexcept
        {
            fbLpZ = x * (1.0f - kFdnFbLpCoef) + fbLpZ * kFdnFbLpCoef;
            if (std::fpclassify(fbLpZ) == FP_SUBNORMAL) fbLpZ = 0.0f;
            return fbLpZ;
        }

        void write(float x) noexcept
        {
            const int bufLen = static_cast<int>(buffer.size());
            buffer[writePos] = x;
            writePos = (writePos + 1) % bufLen;
        }

        void reset() noexcept
        {
            for (auto& s : buffer) s = 0.0f;
            writePos = 0;
            fbLpZ = 0.0f;
            // lfoPhase intentionally NOT reset — keep modulators decorrelated.
        }
    };

    static float lpfCoefFromHz(float cutoffHz, float sampleRate) noexcept
    {
        constexpr float tau_c = 6.28318530717958647692f;
        const float sr = sampleRate < 1.0f ? 1.0f : sampleRate;
        const float dt = 1.0f / sr;
        const float fc = cutoffHz < 1.0f ? 1.0f : cutoffHz;
        const float rc = 1.0f / (tau_c * fc);
        return dt / (rc + dt);
    }

    std::vector<float> predelay_;
    int predelayPos_ = 0;
    int predelaySamples_ = 0;

    std::array<Allpass, 4> diffuser_{};
    float diffuserAmount_ = 0.5f;

    std::array<FdnLine, 4> fdn_{};
    int fdnBaseLengths_[4]{};
    float fbGain_ = 0.93f;
    float fbGainTarget_ = 0.93f;

    float wetLpfZ_ = 0.0f;
    float wetLpfCoef_ = 0.0f;
    float wetLpfTargetCoef_ = 0.0f;

    float wetHpfX1_ = 0.0f, wetHpfY1_ = 0.0f, wetHpfR_ = 0.0f;

    float sampleRate_ = 48000.0f;

    uint32_t stopFadeRemaining_ = 0;
    uint32_t stopFadeTotal_ = 1;
    bool stopMuted_ = false;
    // Per-trigger smooth tail kill — fade output to 0 over kKillFadeMs,
    // zero state at the bottom. Prevents the click that hard-resetting
    // mid-tail would otherwise cause.
    uint32_t killFadeRemaining_ = 0;
    uint32_t killFadeTotal_ = 1;
};

} // namespace bombo
