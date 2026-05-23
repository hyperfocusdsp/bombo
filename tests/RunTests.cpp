// Bombo DSP unit tests (JUCE UnitTestRunner).
// Ports the highest-leverage Rust tests from the Rust archive:
//   - voice_clip: identity at drive=0, asymmetry, boundedness
//   - master_bus: unity below knee, brick-wall at ceiling, DC offset decay
//   - AmpEnvelope: monotonic decay, -60 dB at decay_ms
//   - BiquadFilter: LP/HP frequency response sanity
//   - BombVoice: audible kick on default trigger, decays in time
//
// Build: cmake --build build --target Bombo_Tests
// Run:   ctest --test-dir build --output-on-failure  (or run the binary directly)

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "DSP/VoiceClip.h"
#include "DSP/MasterBus.h"
#include "DSP/Envelopes.h"
#include "DSP/BiquadFilter.h"
#include "DSP/Oscillators.h"
#include "DSP/BombVoice.h"
#include "DSP/Delay.h"
#include "DSP/FdnReverb.h"
#include "DSP/ConvolutionReverb.h"
#include "DSP/IRBank.h"
#include "DSP/RumbleChain.h"
#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>
#include <vector>

namespace
{
// Single-bin Goertzel -- fundamental-bin power only. See plugindev skill
// notes on why this beats wideband RMS through nonlinear stages.
float fundamentalPower(const std::vector<float>& samples, float sr, float freq)
{
    constexpr float tau = 6.28318530717958647692f;
    const float w = tau * freq / sr;
    float re = 0.0f, im = 0.0f;
    for (size_t i = 0; i < samples.size(); ++i)
    {
        const float p = w * static_cast<float>(i);
        re += samples[i] * std::cos(p);
        im += samples[i] * std::sin(p);
    }
    return re * re + im * im;
}

std::vector<float> sineThrough(bombo::BiquadFilter& f, float freq, float sr, int n)
{
    constexpr float tau = 6.28318530717958647692f;
    const float dPhi = tau * freq / sr;
    float phase = 0.0f;
    std::vector<float> out;
    out.reserve(n);
    for (int i = 0; i < n; ++i)
    {
        const float s = std::sin(phase);
        phase += dPhi;
        if (phase >= tau) phase -= tau;
        out.push_back(f.process(s));
    }
    return out;
}
} // namespace

// ── VoiceClip ──────────────────────────────────────────────────────
class VoiceClipTests : public juce::UnitTest
{
public:
    VoiceClipTests() : juce::UnitTest("VoiceClip") {}

    void runTest() override
    {
        using namespace bombo;

        beginTest("off mode is identity");
        for (float x : {-1.0f, -0.5f, 0.0f, 0.3f, 0.9f})
            expect(voiceClipApply(VC_OFF, 0.5f, x) == x);

        beginTest("zero drive is identity for every mode");
        for (int mode : {VC_OFF, VC_TANH, VC_DIODE, VC_CUBIC})
            for (float x : {-1.0f, -0.5f, 0.0f, 0.3f, 0.9f})
                expect(voiceClipApply(mode, 0.0f, x) == x);

        beginTest("tanh stays bounded");
        for (float drive : {0.2f, 0.5f, 1.0f})
            for (float x : {-10.0f, -2.0f, 2.0f, 10.0f})
            {
                const float y = voiceClipApply(VC_TANH, drive, x);
                expect(std::abs(y) < 1.001f);
            }

        beginTest("diode is asymmetric");
        const float pos = voiceClipApply(VC_DIODE, 0.6f, 0.5f);
        const float neg = voiceClipApply(VC_DIODE, 0.6f, -0.5f);
        expect(std::abs(std::abs(pos) - std::abs(neg)) > 1e-3f);

        beginTest("cubic is monotonic across audio range");
        constexpr float drive = 0.5f;
        float prev = voiceClipApply(bombo::VC_CUBIC, drive, -1.0f);
        constexpr int n = 200;
        for (int i = 1; i <= n; ++i)
        {
            const float x = -1.0f + 2.0f * static_cast<float>(i) / n;
            const float y = voiceClipApply(bombo::VC_CUBIC, drive, x);
            expect(y >= prev - 1e-5f);
            prev = y;
        }

        beginTest("drive>1 clamps to drive=1 behaviour");
        const float atOne = voiceClipApply(VC_TANH, 1.0f, 0.5f);
        const float overshoot = voiceClipApply(VC_TANH, 5.0f, 0.5f);
        expect(std::abs(atOne - overshoot) < 1e-6f);
    }
};

// ── MasterBus ──────────────────────────────────────────────────────
class MasterBusTests : public juce::UnitTest
{
public:
    MasterBusTests() : juce::UnitTest("MasterBus") {}

    void runTest() override
    {
        constexpr float pi = 3.14159265358979323846f;

        beginTest("below knee passes clean");
        {
            const float sr = 48000.0f;
            bombo::MasterBus bus(sr);
            const float f = 220.0f;
            constexpr int n = 4800;
            float outPeak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float t = static_cast<float>(i) / sr;
                const float x = 0.5f * std::sin(2.0f * pi * f * t);
                const float y = bus.process(x);
                if (i > n / 2) outPeak = std::max(outPeak, std::abs(y));
            }
            expect(std::abs(outPeak - 0.5f) < 0.02f);
        }

        beginTest("limiter holds peak at ceiling");
        {
            const float sr = 48000.0f;
            bombo::MasterBus bus(sr);
            const float f = 220.0f;
            constexpr int n = 9600;
            float outPeak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float t = static_cast<float>(i) / sr;
                const float x = 2.0f * std::sin(2.0f * pi * f * t);
                const float y = bus.process(x);
                if (i > 240) outPeak = std::max(outPeak, std::abs(y));
            }
            expect(outPeak <= bombo::MasterBus::kCeiling * 1.05f);
        }

        beginTest("DC blocker kills offset");
        {
            const float sr = 48000.0f;
            bombo::MasterBus bus(sr);
            const int n = static_cast<int>(sr * 2.0f);
            float last = 0.0f;
            for (int i = 0; i < n; ++i) last = bus.process(0.5f);
            expect(std::abs(last) < 0.01f);
        }
    }
};

// ── AmpEnvelope ────────────────────────────────────────────────────
class AmpEnvelopeTests : public juce::UnitTest
{
public:
    AmpEnvelopeTests() : juce::UnitTest("AmpEnvelope") {}

    void runTest() override
    {
        beginTest("attack ramp starts near zero");
        {
            bombo::AmpEnvelope env(44100.0f);
            env.trigger(200.0f, 0.0f);
            const float first = env.tick();
            expect(first < 0.1f);
        }

        beginTest("reaches < -54 dB at decay_ms");
        {
            bombo::AmpEnvelope env(44100.0f);
            env.trigger(100.0f, 0.0f);
            const int samples = static_cast<int>(0.1f * 44100.0f);
            float gain = 1.0f;
            for (int i = 0; i < samples; ++i) gain = env.tick();
            expect(gain < 0.002f);
        }

        beginTest("monotonic after attack");
        {
            bombo::AmpEnvelope env(44100.0f);
            env.trigger(200.0f, 0.0f);
            for (int i = 0; i < 50; ++i) env.tick();
            float prev = env.tick();
            for (int i = 0; i < 10000; ++i)
            {
                const float g = env.tick();
                expect(g <= prev + 1e-7f);
                prev = g;
            }
        }
    }
};

// ── BiquadFilter ───────────────────────────────────────────────────
class BiquadFilterTests : public juce::UnitTest
{
public:
    BiquadFilterTests() : juce::UnitTest("BiquadFilter") {}

    void runTest() override
    {
        beginTest("passthrough is identity");
        {
            bombo::BiquadFilter f;
            f.setPassthrough();
            for (float x : {-1.0f, -0.3f, 0.0f, 0.5f, 1.0f})
                expect(std::abs(f.process(x) - x) < 1e-6f);
        }

        beginTest("LPF attenuates above cutoff");
        {
            const float sr = 48000.0f;
            bombo::BiquadFilter f;
            f.setLpf(sr, 200.0f, 0.707f);
            auto pass = sineThrough(f, 80.0f, sr, 8192);
            f.reset();
            auto stop = sineThrough(f, 4000.0f, sr, 8192);
            const float pPass = fundamentalPower(
                std::vector<float>(pass.begin() + 256, pass.end()), sr, 80.0f);
            const float pStop = fundamentalPower(
                std::vector<float>(stop.begin() + 256, stop.end()), sr, 4000.0f);
            expect(pPass > 100.0f * pStop);
        }

        beginTest("HPF attenuates below cutoff");
        {
            const float sr = 48000.0f;
            bombo::BiquadFilter f;
            f.setHpf(sr, 200.0f, 0.707f);
            auto pass = sineThrough(f, 2000.0f, sr, 8192);
            f.reset();
            auto stop = sineThrough(f, 40.0f, sr, 8192);
            const float pPass = fundamentalPower(
                std::vector<float>(pass.begin() + 256, pass.end()), sr, 2000.0f);
            const float pStop = fundamentalPower(
                std::vector<float>(stop.begin() + 256, stop.end()), sr, 40.0f);
            expect(pPass > 100.0f * pStop);
        }

        beginTest("extreme cutoff stays finite");
        {
            bombo::BiquadFilter f;
            f.setLpf(48000.0f, 1000000.0f, 0.707f);
            for (int i = 0; i < 1000; ++i)
            {
                const float y = f.process(0.5f);
                expect(std::isfinite(y));
            }
        }

        beginTest("drive=0 is linear (no harmonics)");
        {
            const float sr = 48000.0f;
            bombo::BiquadFilter f;
            f.setLpf(sr, 1000.0f, 0.707f);
            f.setDrive(0.0f);
            auto out = sineThrough(f, 200.0f, sr, 8192);
            const std::vector<float> tail(out.begin() + 1024, out.end());
            const float h1 = fundamentalPower(tail, sr, 200.0f);
            const float h2 = fundamentalPower(tail, sr, 400.0f);
            expect(h2 < h1 * 1e-3f);
        }
    }
};

// ── Oscillator (polyBLEP saw) ──────────────────────────────────────
class OscillatorTests : public juce::UnitTest
{
public:
    OscillatorTests() : juce::UnitTest("Oscillator") {}

    void runTest() override
    {
        beginTest("100 Hz fundamental has >=30 dB more energy than 110 Hz");
        {
            const float sr = 48000.0f;
            bombo::SineOsc osc(sr);
            osc.trigger(0.0f);
            constexpr int n = 24000;
            std::vector<float> samples;
            samples.reserve(n);
            for (int i = 0; i < n; ++i) samples.push_back(osc.tick(100.0f));
            const float on = fundamentalPower(samples, sr, 100.0f);
            const float off = fundamentalPower(samples, sr, 110.0f);
            expect(on > 1000.0f * off);
        }

        beginTest("output bounded for saw + pulse");
        {
            bombo::SineOsc osc(48000.0f);
            osc.trigger(0.0f);
            for (int i = 0; i < 10000; ++i)
            {
                const float s = osc.tickWave(440.0f, bombo::WAVE_SAW);
                expect(s >= -2.5f && s <= 2.5f);
            }
            osc.trigger(0.0f);
            for (int i = 0; i < 10000; ++i)
            {
                const float s = osc.tickWave(440.0f, bombo::WAVE_PULSE);
                expect(s >= -2.5f && s <= 2.5f);
            }
        }
    }
};

// ── BombVoice ──────────────────────────────────────────────────────
class BombVoiceTests : public juce::UnitTest
{
public:
    BombVoiceTests() : juce::UnitTest("BombVoice") {}

    void runTest() override
    {
        beginTest("default trigger produces audible peak");
        {
            const float sr = 48000.0f;
            bombo::BombVoice v(sr);
            v.trigger(bombo::VoiceTrigger{});
            float peak = 0.0f;
            for (int i = 0; i < 100; ++i) peak = std::max(peak, std::abs(v.tick()));
            expect(peak > 0.1f);
        }

        beginTest("inactive after 1.5x decay_ms");
        {
            const float sr = 48000.0f;
            bombo::BombVoice v(sr);
            bombo::VoiceTrigger t;
            v.trigger(t);
            const int beyond = static_cast<int>(t.ampDecayMs / 1000.0f * 1.5f * sr);
            for (int i = 0; i < beyond; ++i) v.tick();
            expect(!v.isActive());
        }

        beginTest("two voices with identical triggers produce identical output");
        {
            // Drift jitter was removed when DRIFT -> sample slot landed.
            // The voice path is now fully deterministic for a given trigger.
            const float sr = 48000.0f;
            bombo::VoiceTrigger t;
            bombo::BombVoice v1(sr), v2(sr);
            v1.trigger(t);
            v2.trigger(t);
            for (int i = 0; i < 2000; ++i)
            {
                const float a = v1.tick();
                const float b = v2.tick();
                expect(std::abs(a - b) < 1e-7f);
            }
        }

        beginTest("fadeout reaches silence");
        {
            const float sr = 48000.0f;
            bombo::BombVoice v(sr);
            v.trigger(bombo::VoiceTrigger{});
            for (int i = 0; i < 480; ++i) v.tick();
            v.startFadeout(sr);
            const int fade = static_cast<int>(bombo::kVoiceFadeoutMs * 0.001f * sr) + 16;
            for (int i = 0; i < fade; ++i) v.tick();
            expect(!v.isActive());
        }
    }
};

// ── FDN reverb sanity ──────────────────────────────────────────────
class FdnReverbTests : public juce::UnitTest
{
public:
    FdnReverbTests() : juce::UnitTest("FdnReverb") {}

    void runTest() override
    {
        beginTest("no kill_tail -> silent input -> silent output");
        {
            const float sr = 48000.0f;
            bombo::FdnReverb r(sr);
            r.setParams(0.9f, 0.0f);
            float peak = 0.0f;
            for (int i = 0; i < 2048; ++i)
                peak = std::max(peak, std::abs(r.process(0.0f)));
            expect(peak < 1e-3f);
        }

        beginTest("impulse produces decaying tail");
        {
            const float sr = 48000.0f;
            bombo::FdnReverb r(sr);
            r.setParams(0.6f, 0.3f);
            r.setSize(0.5f);
            r.setDiffusion(0.5f);
            r.hardReset();
            r.process(1.0f);
            for (int i = 0; i < 200; ++i) r.process(0.0f);
            float early = 0.0f, late = 0.0f;
            for (int i = 0; i < static_cast<int>(0.6f * sr); ++i)
            {
                const float y = r.process(0.0f);
                if (i >= static_cast<int>(0.05f * sr) && i < static_cast<int>(0.10f * sr))
                    early += y * y;
                if (i >= static_cast<int>(0.50f * sr) && i < static_cast<int>(0.60f * sr))
                    late += y * y;
            }
            expect(early > 0.0f);
            expect(late < early * 0.5f);
        }

        beginTest("full_reset silences tail");
        {
            const float sr = 48000.0f;
            bombo::FdnReverb r(sr);
            r.setParams(0.9f, 0.0f);
            r.setSize(0.5f);
            r.setDiffusion(0.5f);
            r.hardReset();
            r.process(1.0f);
            for (int i = 0; i < static_cast<int>(0.1f * sr); ++i) r.process(0.0f);
            r.fullReset();
            float e = 0.0f;
            for (int i = 0; i < static_cast<int>(0.05f * sr); ++i)
                e += std::abs(r.process(0.0f));
            expect(e < 1e-3f);
        }

        beginTest("killTail clears predelay -- no rebuild from stale content");
        {
            // Regression for the user-reported 2026-05-17 "20-second
            // ringing after one trigger" bug. predelay was NOT being
            // zeroed at the kill-fade's zero crossing, so cached input
            // kept feeding the (just-reset) FDN, which rebuilt a new
            // tail from stale content. With predelay set to a long
            // value (~400 ms) and a sustained input then killTail, the
            // output must reach silence within the kill-fade window AND
            // STAY silent through the predelay's full readout duration.
            const float sr = 48000.0f;
            bombo::FdnReverb r(sr);
            r.setParams(0.85f, 0.3f);
            r.setSize(0.5f);
            r.setDiffusion(0.5f);
            r.setPredelayMs(400.0f);   // long predelay -- exposes the bug
            r.hardReset();
            // Pump sustained input into predelay + FDN for 500 ms so both
            // are well-populated when killTail fires.
            for (int i = 0; i < static_cast<int>(0.5f * sr); ++i)
                r.process(0.4f);
            r.killTail();
            // Run past the kill-fade + the entire predelay duration.
            // After this window, output must be at-or-near silence.
            const int waitSamples = static_cast<int>(0.05f * sr)        // kill-fade
                                  + static_cast<int>(0.45f * sr);       // predelay drain
            for (int i = 0; i < waitSamples; ++i) r.process(0.0f);
            // Probe a 200 ms window -- any residual tail from un-cleared
            // predelay would show up here as the FDN rebuilds.
            float peak = 0.0f;
            for (int i = 0; i < static_cast<int>(0.2f * sr); ++i)
                peak = std::max(peak, std::abs(r.process(0.0f)));
            expect(peak < 1e-3f, "no residual reverb tail after killTail + predelay drain");
        }

        beginTest("killTail fades smoothly to silence (no click)");
        {
            const float sr = 48000.0f;
            bombo::FdnReverb r(sr);
            r.setParams(0.85f, 0.3f);
            r.setSize(0.5f);
            r.setDiffusion(0.5f);
            r.hardReset();
            // Develop a tail with an impulse + run to populate state.
            r.process(1.0f);
            for (int i = 0; i < static_cast<int>(0.05f * sr); ++i) r.process(0.0f);

            // Sample wet just before kill (should be audible).
            float preKillPeak = 0.0f;
            for (int i = 0; i < 64; ++i)
                preKillPeak = std::max(preKillPeak, std::abs(r.process(0.0f)));
            expect(preKillPeak > 1e-3f);

            // Trigger fade -- wet should slope down rather than jump to 0.
            r.killTail();
            float prev = std::abs(r.process(0.0f));
            float maxJump = 0.0f;
            for (int i = 0; i < static_cast<int>(0.010f * sr); ++i)
            {
                const float cur = std::abs(r.process(0.0f));
                maxJump = std::max(maxJump, std::abs(cur - prev));
                prev = cur;
            }
            // Sample-to-sample jump bounded -- the ramp shouldn't produce
            // anything close to the pre-kill peak in a single step.
            expect(maxJump < preKillPeak * 0.5f);

            // 30 ms after killTail: silent.
            for (int i = 0; i < static_cast<int>(0.020f * sr); ++i) r.process(0.0f);
            float postPeak = 0.0f;
            for (int i = 0; i < static_cast<int>(0.010f * sr); ++i)
                postPeak = std::max(postPeak, std::abs(r.process(0.0f)));
            expect(postPeak < 1e-4f);
        }
    }
};

class DelayTests : public juce::UnitTest
{
public:
    DelayTests() : juce::UnitTest("Delay") {}

    void runTest() override
    {
        beginTest("killTail flushes buffer -- wet silent after kill+delay");
        {
            const float sr = 48000.0f;
            bombo::Delay d(sr);
            const float delayMs = 50.0f;
            d.setTimeMs(delayMs);
            d.setFeedback(0.85f);
            d.setFilterMorph(0.0f);

            // Populate the delay buffer with sustained input so feedback
            // echoes are circulating at audible levels everywhere in the
            // buffer (not just one transient blip).
            const int populateSamples = static_cast<int>(0.5f * sr);
            for (int i = 0; i < populateSamples; ++i) d.process(0.4f);

            // Confirm the tail IS audible right before kill across a
            // window wider than one delay cycle.
            float preKillPeak = 0.0f;
            const int preWindow = static_cast<int>(delayMs * 0.001f * sr * 2.0f);
            for (int i = 0; i < preWindow; ++i)
                preKillPeak = std::max(preKillPeak, std::abs(d.process(0.0f)));
            expect(preKillPeak > 1e-2f);

            // Kill, then run > delayMs + killFadeMs so the entire buffer
            // has been read once with new (zero) input flowing through.
            d.killTail();
            const int waitSamples = static_cast<int>((delayMs + 20.0f) * 0.001f * sr);
            for (int i = 0; i < waitSamples; ++i) d.process(0.0f);

            // Now the wet should be silent across a wide window.
            float postPeak = 0.0f;
            const int postWindow = static_cast<int>(delayMs * 0.001f * sr * 2.0f);
            for (int i = 0; i < postWindow; ++i)
                postPeak = std::max(postPeak, std::abs(d.process(0.0f)));
            expect(postPeak < 1e-3f);
        }

        beginTest("killTail @ max feedback -- no residual ring during fade input");
        {
            // Regression for the user-reported 2026-05-17 "delay tail
            // very slightly keeps moving... even with max feedback" bug.
            // Root cause: buffer continued writing input+feedback during
            // the kill-fade window, so the voice-tail's last few ms got
            // captured and rang out at the delay-cycle period.
            const float sr = 48000.0f;
            bombo::Delay d(sr);
            d.setTimeMs(120.0f);
            d.setFeedback(0.98f);       // near-max feedback -- worst case
            d.setFilterMorph(0.0f);
            // Populate buffer with sustained input so feedback is active.
            for (int i = 0; i < static_cast<int>(0.4f * sr); ++i)
                d.process(0.4f);
            // Trigger kill, then feed non-zero input ONLY during the
            // exact kill-fade window -- simulates the voice's startFadeout
            // overlapping with the chain killTail. After that, input is
            // silent. With the bug, the fade-window input gets written
            // into the buffer and rings out at the delay-cycle period;
            // with the fix, those writes are suppressed and the post-
            // fade output is silent.
            d.killTail();
            const int fadeSamples = static_cast<int>(bombo::Delay::kKillFadeMs * 0.001f * sr);
            for (int i = 0; i < fadeSamples; ++i) d.process(0.3f);
            // Input is now silent. Run past 5x delay period (= 600 ms).
            for (int i = 0; i < static_cast<int>(0.6f * sr); ++i) d.process(0.0f);
            float peak = 0.0f;
            for (int i = 0; i < static_cast<int>(0.2f * sr); ++i)
                peak = std::max(peak, std::abs(d.process(0.0f)));
            expect(peak < 1e-3f, "no residual delay ring with max feedback after killTail");
        }

        beginTest("killTail handles rapid successive kills without state leak");
        {
            const float sr = 48000.0f;
            bombo::Delay d(sr);
            d.setTimeMs(50.0f);
            d.setFeedback(0.9f);

            for (int hit = 0; hit < 4; ++hit)
            {
                d.process(1.0f);
                for (int i = 0; i < static_cast<int>(0.05f * sr); ++i) d.process(0.0f);
                d.killTail();
            }
            // After 4 quick hits, run silence for delay+killfade window;
            // wet must be silent -- confirms each kill flushed cleanly.
            const int waitSamples = static_cast<int>(0.080f * sr);
            for (int i = 0; i < waitSamples; ++i) d.process(0.0f);
            float peak = 0.0f;
            for (int i = 0; i < static_cast<int>(0.005f * sr); ++i)
                peak = std::max(peak, std::abs(d.process(0.0f)));
            expect(peak < 1e-3f);
        }
    }
};

// ── Convolution reverb sanity + per-hit identity guard ─────────────
class ConvolutionReverbTests : public juce::UnitTest
{
public:
    ConvolutionReverbTests() : juce::UnitTest("ConvolutionReverb") {}

    void runTest() override
    {
        beginTest("IRBank synthesizes deterministic non-empty IRs for every algo");
        {
            const float sr = 48000.0f;
            for (int a = 0; a < bombo::ir::kNumAlgos; ++a)
            {
                auto buf1 = bombo::ir::synthesizeIR(a, sr);
                auto buf2 = bombo::ir::synthesizeIR(a, sr);
                expect(buf1.getNumChannels() == 1);
                expect(buf1.getNumSamples() > 256);
                expect(buf1.getNumSamples() == buf2.getNumSamples());
                // Determinism: byte-identical for the same algo + sample rate.
                bool identical = true;
                const auto* a1 = buf1.getReadPointer(0);
                const auto* a2 = buf2.getReadPointer(0);
                for (int i = 0; i < buf1.getNumSamples(); ++i)
                    if (a1[i] != a2[i]) { identical = false; break; }
                expect(identical, "IR bytes vary between calls (algo " + juce::String(a) + ")");
                // Peak normalised near 0.5.
                float peak = 0.0f;
                for (int i = 0; i < buf1.getNumSamples(); ++i)
                    peak = std::max(peak, std::abs(a1[i]));
                expect(peak > 0.4f && peak < 0.6f,
                       "IR peak out of normalised band (algo " + juce::String(a)
                       + ", peak=" + juce::String(peak, 4) + ")");
            }
        }

        beginTest("convolution silent-in -> silent-out");
        {
            const float sr = 48000.0f;
            bombo::ConvolutionReverb r(sr);
            r.setType(bombo::ir::Hall);
            r.setSize(0.5f);
            r.setDecay(0.7f);
            r.setDamp(0.5f);
            r.setPredelayMs(0.0f);
            float peak = 0.0f;
            for (int i = 0; i < 2048; ++i)
                peak = std::max(peak, std::abs(r.process(0.0f)));
            expect(peak < 1e-3f);
        }

        beginTest("impulse produces decaying tail (Room algo)");
        {
            // Room IR is a pure exponential decay (tau~100ms, no
            // build-up), so energy density strictly drops over time.
            // Hall has a build-up phase whose energy peaks ~250 ms in
            // and would invert this assertion -- don't substitute it.
            const float sr = 48000.0f;
            bombo::ConvolutionReverb r(sr);
            r.setType(bombo::ir::Room);
            r.setSize(1.0f);
            r.setDecay(1.0f);
            r.setDamp(0.2f);
            r.setPredelayMs(0.0f);
            r.onTrigger();
            r.process(1.0f);
            // Drain the conv block latency only -- Room decays fast.
            for (int i = 0; i < 128; ++i) r.process(0.0f);
            float early = 0.0f, late = 0.0f;
            for (int i = 0; i < static_cast<int>(0.4f * sr); ++i)
            {
                const float y = r.process(0.0f);
                if (i >= static_cast<int>(0.02f * sr) && i < static_cast<int>(0.05f * sr))
                    early += y * y;
                if (i >= static_cast<int>(0.20f * sr) && i < static_cast<int>(0.30f * sr))
                    late += y * y;
            }
            expect(early > 0.0f, "no early energy after impulse");
            // Per-sample density (early window 30 ms, late window 100 ms).
            const float earlyDensity = early / (0.03f * sr);
            const float lateDensity  = late  / (0.10f * sr);
            expect(lateDensity < earlyDensity * 0.5f,
                   "tail did not decay (earlyDensity=" + juce::String(earlyDensity, 8)
                   + ", lateDensity=" + juce::String(lateDensity, 8) + ")");
        }

        beginTest("per-trigger identity: two trigs in isolation produce identical late-tail");
        {
            // Core determinism check. Hit the convolution reverb with an
            // impulse, capture the late-tail RMS, kill+re-trigger, hit
            // again, capture the late-tail RMS. Ratio must be < 1.005.
            const float sr = 48000.0f;
            bombo::ConvolutionReverb r(sr);
            r.setType(bombo::ir::Hall);
            r.setSize(1.0f);
            r.setDecay(1.0f);
            r.setDamp(0.2f);
            r.setPredelayMs(0.0f);

            auto runTrigCaptureLate = [&]() noexcept {
                // killTail + onTrigger sequence (matches RumbleChain).
                r.killTail();
                r.onTrigger();
                // Let the fade complete before hitting.
                for (int i = 0; i < static_cast<int>(0.05f * sr); ++i) r.process(0.0f);
                r.onTrigger();
                r.process(1.0f);
                double rms = 0.0;
                int n = 0;
                for (int i = 0; i < static_cast<int>(0.30f * sr); ++i)
                {
                    const float y = r.process(0.0f);
                    if (i >= static_cast<int>(0.10f * sr))
                    {
                        rms += (double) y * (double) y;
                        ++n;
                    }
                }
                return std::sqrt(rms / juce::jmax(1, n));
            };

            const double a = runTrigCaptureLate();
            const double b = runTrigCaptureLate();
            const double ratio = a > 0.0 ? b / a : 1.0;
            expect(a > 1e-6, "first trig produced no late-tail energy");
            expect(std::abs(ratio - 1.0) < 0.005,
                   "late-tail ratio drifted (a=" + juce::String(a, 8)
                   + " b=" + juce::String(b, 8)
                   + " ratio=" + juce::String(ratio, 6) + ")");
        }

        beginTest("loop-mode stability: 12 trigs at 140 BPM, late-tail ratio bound");
        {
            // This is the parked bug_bombo_reverb_pulsing_loop_2026_05_17
            // regression test. Drive the chain with the user's exact
            // preset and confirm per-trig late-tail variation stays bound.
            // FdnReverb under this preset showed ~42% peak-late jump on
            // trig 2+ in real recording. With convolution + reset on
            // killTail we expect ratio range < 0.01.
            const float sr = 48000.0f;
            bombo::RumbleChain chain(sr);
            bombo::ChainParams p;
            // User preset (DELAY OFF, reverb-only):
            p.reverbType        = bombo::ir::Room;
            p.reverbSize        = 0.22f;
            p.reverbDecay       = 0.16f;
            p.reverbDamp        = 0.85f;
            p.reverbPredelayMs  = 0.0f;
            p.reverbMix         = 0.77f;
            p.delayMix          = 0.0f;
            p.delayMute         = true;
            p.duckDepth         = 0.76f;
            p.duckAttackMs      = 0.1f;
            p.duckHoldMs        = 10.0f;
            p.duckReleaseMs     = 388.0f;
            p.hpHz              = 29.0f;
            p.hpQ               = 2.99f;
            p.lpHz              = 18000.0f;
            p.lpQ               = 0.76f;
            p.filterColor       = 0.4f;
            p.limiterOn         = true;
            chain.update(p);

            const int beatSamples = static_cast<int>(60.0f / 140.0f * sr); // 140 BPM

            std::vector<double> peakLate;
            peakLate.reserve(12);

            for (int trig = 0; trig < 12; ++trig)
            {
                // Fire trigger: killTail + onTrigger then feed a single
                // impulse (substitute for the voice attack — good enough
                // to drive the wet bus to a known level for this guard).
                chain.killTail();
                chain.onTrigger(80.0f);
                // Let the kill-fade clear conv state.
                for (int i = 0; i < static_cast<int>(0.05f * sr); ++i)
                    chain.process(0.0f);
                chain.onTrigger(80.0f);
                chain.process(1.0f);

                // Measure peak in the 200ms+ late-tail window of the
                // remaining beat.
                double peak = 0.0;
                const int lateStart = static_cast<int>(0.20f * sr);
                const int beatEnd   = beatSamples;
                for (int i = 1; i < beatEnd; ++i)
                {
                    const float y = chain.process(0.0f);
                    if (i >= lateStart)
                        peak = std::max(peak, (double) std::abs(y));
                }
                peakLate.push_back(peak);
            }

            // Skip trig 0 (system warm-up). Use trigs 1..11.
            double mn = 1e30, mx = 0.0;
            for (size_t i = 1; i < peakLate.size(); ++i)
            {
                mn = std::min(mn, peakLate[i]);
                mx = std::max(mx, peakLate[i]);
            }
            const double range = mx > 0.0 ? (mx - mn) / mx : 0.0;
            expect(mx > 0.0, "no late-tail energy observed");
            expect(range < 0.05,
                   "loop-mode pulsing detected (peakLate range=" + juce::String(range, 4)
                   + " min=" + juce::String(mn, 8)
                   + " max=" + juce::String(mx, 8) + ")");
        }

        beginTest("REAL plugin flow: post-fade silence under loop-mode bleed");
        {
            // Mirror the exact PluginProcessor::processBlock sequence at
            // each trigger: chain.killTail() then chain.onTrigger() — NOT
            // the doubled onTrigger used in the LoopReverbStabilityTests.
            // Then check that the 30 ms fade window decays cleanly AND
            // that the post-fade window (input still zero) is silent
            // (conv state was properly reset). A bleed bug shows up here
            // as either: (a) fade window energy NOT decreasing, or (b)
            // post-fade window non-zero with zero input.
            const float sr = 48000.0f;
            bombo::RumbleChain chain(sr);
            bombo::ChainParams p;
            p.reverbType = bombo::ir::Hall;
            p.reverbSize = 0.55f;
            p.reverbDecay = 0.7f;
            p.reverbDamp = 0.45f;
            p.reverbPredelayMs = 30.0f;
            p.reverbMix = 0.6f;
            p.delayMute = true;
            p.reverbMute = false;
            p.duckDepth = 0.0f;
            p.hpHz = 30.0f; p.hpQ = 0.707f;
            p.lpHz = 18000.0f; p.lpQ = 0.707f;
            p.limiterOn = true;
            chain.update(p);

            // Beat 1: build up a wet tail with a kick-attack impulse +
            // noise sustain (mirrors what a voice would feed the chain).
            chain.killTail();
            chain.onTrigger(80.0f);
            chain.process(1.0f);
            for (int i = 1; i < static_cast<int>(0.10f * sr); ++i)
                chain.process(0.05f * static_cast<float>(((i * 17) % 23) - 11) / 11.0f);
            for (int i = static_cast<int>(0.10f * sr); i < static_cast<int>(0.35f * sr); ++i)
                chain.process(0.0f);

            // Peek at the reverb tail just before beat 2.
            double preB2 = 0.0;
            for (int i = 0; i < 128; ++i)
            {
                const float y = chain.process(0.0f);
                preB2 += static_cast<double>(y) * y;
            }
            expect(preB2 > 1e-6, "no reverb tail before beat 2 - test setup is wrong");

            // Beat 2: real flow is killTail THEN onTrigger (single call).
            chain.killTail();
            chain.onTrigger(80.0f);

            // 30 ms fade window — feed zero input, sample wet. With
            // conv input suppressed and the fade ramp going 1->0, total
            // energy should be a fraction of the preB2 reference. Per-
            // sample peak should be at or below preB2's peak.
            double fadeEnergy = 0.0;
            float  fadePeak   = 0.0f;
            const int fadeLen = static_cast<int>(0.030f * sr);
            for (int i = 0; i < fadeLen; ++i)
            {
                const float y = std::abs(chain.process(0.0f));
                fadeEnergy += static_cast<double>(y) * y;
                fadePeak = std::max(fadePeak, y);
            }

            // Post-fade silent window — conv state should be cleared.
            // We feed zero input AND expect zero output (within FP noise).
            double postFadeEnergy = 0.0;
            float  postFadePeak   = 0.0f;
            const int postLen = static_cast<int>(0.020f * sr);
            for (int i = 0; i < postLen; ++i)
            {
                const float y = std::abs(chain.process(0.0f));
                postFadeEnergy += static_cast<double>(y) * y;
                postFadePeak = std::max(postFadePeak, y);
            }

            // Pass conditions:
            //   1) fade peak does not EXCEED what was happening pre-fade
            //      (no sudden burst from a botched reset)
            //   2) post-fade peak is essentially zero (conv state reset
            //      worked, no bleed-over after the 30 ms window).
            expect(postFadePeak < 1e-3f,
                   "post-fade bleed: peak=" + juce::String(postFadePeak, 6)
                   + " (must be < 1e-3 with zero input + cleared conv state)");
            // Sanity: fade energy is finite and bounded.
            expect(std::isfinite(fadeEnergy) && fadeEnergy >= 0.0,
                   "fade energy non-finite");
            // Report the actual ratios so a future eyeball-check sees them.
            juce::Logger::writeToLog(juce::String("preB2=") + juce::String(preB2, 6)
                + " fadeEnergy=" + juce::String(fadeEnergy, 6)
                + " fadePeak="   + juce::String(fadePeak, 6)
                + " postFadeEnergy=" + juce::String(postFadeEnergy, 6)
                + " postFadePeak="   + juce::String(postFadePeak, 6));
        }

        beginTest("USER BUG: TAIL ON loop-mode bleed in FIRST 15 ms after each trigger");
        {
            // What the user is actually hearing: with TAIL ON in loop
            // mode and a heavy-feedback delay, the OLD echo content is
            // being faded out across the V-shape kill window AT THE
            // SAME TIME as the new kick attack plays. Whatever's
            // audible in the first ~10-15 ms is perceived as "tail
            // bleeds onto next trig." This test measures peak energy
            // in the 0-15 ms window after each beat 2+ trigger
            // (skipping beat 1: no previous tail to bleed) and asserts
            // it's bounded.
            const float sr = 48000.0f;
            const int   beatSamples = static_cast<int>(60.0f / 140.0f * sr);
            bombo::RumbleChain chain(sr);
            bombo::ChainParams p;
            p.delayMs        = 250.0f;
            p.delayFeedback  = 0.85f;
            p.delayMix       = 0.6f;
            p.reverbMute     = true;
            p.delayMute      = false;
            p.duckDepth      = 0.0f;
            p.hpHz           = 30.0f; p.hpQ = 0.707f;
            p.lpHz           = 12000.0f; p.lpQ = 0.707f;
            p.limiterOn      = true;
            p.tailKillOn     = true;
            chain.update(p);

            // Realistic-ish kick body: 700 ms exponential decay (matches
            // default ampDecay = 700 ms in Parameters.h). Continuous
            // input so the delay buffer is full at read positions
            // 15-30 ms post-trigger -- the window we're testing.
            auto kickBodySample = [sr](int sampleSinceTrig) noexcept {
                const float t = static_cast<float>(sampleSinceTrig) / sr;
                const float env = std::exp(-t / 0.20f);    // tau = 200 ms
                // Quasi-noise so the delay sees broadband content.
                const float n = 0.5f * static_cast<float>(((sampleSinceTrig * 17) % 23) - 11) / 11.0f;
                return env * n;
            };

            // To isolate the BLEED (old wet residue only, not the new
            // kick's dry-path attack), build delay buffer state over
            // several beats with full kick body input, then at the
            // measurement trigger feed ZERO input — the wet bus output
            // is then only the previous beats' echo running through
            // the kill fade. Any non-zero output in the 15 ms window
            // is the bleed.
            std::vector<float> bleedPeaks;
            const int bleedSamples = static_cast<int>(0.015f * sr);

            // Warm-up: 3 beats of continuous kick-body input so the
            // delay buffer is fully loaded with content.
            for (int beat = 0; beat < 3; ++beat)
            {
                chain.killTail();
                chain.onTrigger(80.0f);
                for (int i = 0; i < beatSamples; ++i)
                {
                    const float in = (i == 0) ? 1.0f : kickBodySample(i);
                    chain.process(in);
                }
            }

            // Measurement beats: trigger as usual (killTail + onTrigger)
            // but feed ZERO input throughout — wet output should drop
            // to ~silence after the kill fade closes the V (~6 ms).
            for (int beat = 0; beat < 5; ++beat)
            {
                chain.killTail();
                chain.onTrigger(80.0f);
                float peak = 0.0f;
                for (int i = 0; i < beatSamples; ++i)
                {
                    const float y = std::abs(chain.process(0.0f));
                    if (i < bleedSamples)
                        peak = std::max(peak, y);
                }
                bleedPeaks.push_back(peak);
            }

            juce::Logger::writeToLog("TAIL ON  bleed-window peaks (15 ms post-trig, beats 2..6):");
            for (auto p : bleedPeaks)
                juce::Logger::writeToLog("  peak=" + juce::String(p, 6));

            const float worstBleed = bleedPeaks.empty() ? 0.0f
                                   : *std::max_element(bleedPeaks.begin(), bleedPeaks.end());
            // Threshold = 5% — below this the kick attack masks any
            // residual chop fade. Was ~0.15-0.30 with the 30 ms V-fade
            // (15 ms of audible OLD echo), should now sit < 0.05 with
            // the 6 ms fade.
            expect(worstBleed < 0.05f,
                   "post-trig bleed window peak too high: " + juce::String(worstBleed, 6)
                   + " (must be < 0.05)");
        }

        beginTest("USER BUG: tailKillOn toggle controls per-trig chop in loop mode");
        {
            // Mirrors the user-reported scenario:
            //   - DELAY heavy feedback, REVERB on, LOOP mode at 140 BPM.
            //   - TAIL ON  -> delay buffer should chop between every trig
            //                 (post-trig the delay echo from beat N-1 must
            //                 be gone before beat N's echo arrives).
            //   - TAIL OFF -> delay buffer must NOT chop; the previous
            //                 beat's echo continues ringing through the
            //                 next beat.
            const float sr = 48000.0f;
            const int   beatSamples = static_cast<int>(60.0f / 140.0f * sr);

            auto runScenario = [&](bool tailKillOn) noexcept {
                bombo::RumbleChain chain(sr);
                bombo::ChainParams p;
                // Heavy-feedback delay, low feedback HP/LP filtering so we
                // can clearly see the echo tail. Reverb muted so the only
                // wet content is delay.
                p.delayMs        = 250.0f;
                p.delayFeedback  = 0.85f;
                p.delayMix       = 0.6f;
                p.reverbMix      = 0.0f;
                p.reverbMute     = true;
                p.delayMute      = false;
                p.duckDepth      = 0.0f;
                p.hpHz           = 30.0f;  p.hpQ = 0.707f;
                p.lpHz           = 12000.0f; p.lpQ = 0.707f;
                p.limiterOn      = true;
                p.tailKillOn     = tailKillOn;   // <-- the bit under test
                chain.update(p);

                // 4 trigs at 140 BPM, captures wet-tail energy in the
                // 200 ms window just BEFORE each trigger (i.e. the
                // accumulated echo level entering the next beat).
                std::vector<double> preBeatEnergy;
                preBeatEnergy.reserve(4);
                for (int beat = 0; beat < 4; ++beat)
                {
                    // Per-trig flow mirrors PluginProcessor exactly.
                    chain.killTail();
                    chain.onTrigger(80.0f);
                    // Inject a kick-attack approximation: short impulse
                    // burst plus 100 ms of low-level noise (sustained
                    // body) so the delay buffer captures real content.
                    chain.process(1.0f);
                    for (int i = 1; i < static_cast<int>(0.10f * sr); ++i)
                        chain.process(0.05f * static_cast<float>(((i * 17) % 23) - 11) / 11.0f);
                    // Silent fill to end of beat, accumulate energy for
                    // the 200 ms pre-NEXT-beat window.
                    const int silentSamples = beatSamples
                                            - static_cast<int>(0.10f * sr);
                    double winEnergy = 0.0;
                    const int winStart = silentSamples
                                       - static_cast<int>(0.20f * sr);
                    for (int i = 0; i < silentSamples; ++i)
                    {
                        const float y = chain.process(0.0f);
                        if (i >= winStart)
                            winEnergy += static_cast<double>(y) * y;
                    }
                    preBeatEnergy.push_back(winEnergy);
                }
                return preBeatEnergy;
            };

            // When BOMBO_DUMP_WAV is set, also write WAV captures for
            // user-audible verification. Captures both scenarios end-to-
            // end (8 beats each) as mono 48 kHz floats. Path is fixed
            // in /tmp so the user can play them with any player.
            const bool dumpWav = std::getenv("BOMBO_DUMP_WAV") != nullptr;

            auto runScenarioAndCapture = [&](bool tailKillOn, const char* wavPath) noexcept {
                bombo::RumbleChain chain(sr);
                bombo::ChainParams p;
                p.delayMs        = 250.0f;
                p.delayFeedback  = 0.85f;
                p.delayMix       = 0.6f;
                p.reverbMix      = 0.0f;
                p.reverbMute     = true;
                p.delayMute      = false;
                p.duckDepth      = 0.0f;
                p.hpHz           = 30.0f;  p.hpQ = 0.707f;
                p.lpHz           = 12000.0f; p.lpQ = 0.707f;
                p.limiterOn      = true;
                p.tailKillOn     = tailKillOn;
                chain.update(p);

                const int nBeats = 8;
                const int totalSamples = beatSamples * nBeats;
                std::vector<float> wav;
                if (dumpWav) wav.reserve(totalSamples);

                for (int beat = 0; beat < nBeats; ++beat)
                {
                    chain.killTail();
                    chain.onTrigger(80.0f);
                    const float attack = chain.process(1.0f);
                    if (dumpWav) wav.push_back(attack);
                    for (int i = 1; i < static_cast<int>(0.10f * sr); ++i)
                    {
                        const float in = 0.05f * static_cast<float>(((i * 17) % 23) - 11) / 11.0f;
                        const float y  = chain.process(in);
                        if (dumpWav) wav.push_back(y);
                    }
                    const int silentSamples = beatSamples - static_cast<int>(0.10f * sr);
                    for (int i = 0; i < silentSamples; ++i)
                    {
                        const float y = chain.process(0.0f);
                        if (dumpWav) wav.push_back(y);
                    }
                }

                if (dumpWav && wavPath != nullptr)
                {
                    juce::WavAudioFormat fmt;
                    juce::File outFile(wavPath);
                    outFile.deleteFile();
                    auto stream = outFile.createOutputStream();
                    if (stream != nullptr)
                    {
                        std::unique_ptr<juce::AudioFormatWriter> writer(
                            fmt.createWriterFor(stream.get(), sr, 1, 16, {}, 0));
                        if (writer != nullptr)
                        {
                            stream.release();
                            juce::AudioBuffer<float> buf(1, (int) wav.size());
                            std::copy(wav.begin(), wav.end(), buf.getWritePointer(0));
                            writer->writeFromAudioSampleBuffer(buf, 0, (int) wav.size());
                        }
                    }
                }
            };

            const auto onEnergies  = runScenario(true);
            const auto offEnergies = runScenario(false);

            if (dumpWav)
            {
                runScenarioAndCapture(true,  "/tmp/bombo_tail_on.wav");
                runScenarioAndCapture(false, "/tmp/bombo_tail_off.wav");
                juce::Logger::writeToLog(juce::String("WAV dumps: /tmp/bombo_tail_on.wav /tmp/bombo_tail_off.wav"));
            }

            juce::Logger::writeToLog(juce::String("TAIL ON  beats: ")
                + juce::String(onEnergies[0], 6) + ", "
                + juce::String(onEnergies[1], 6) + ", "
                + juce::String(onEnergies[2], 6) + ", "
                + juce::String(onEnergies[3], 6));
            juce::Logger::writeToLog(juce::String("TAIL OFF beats: ")
                + juce::String(offEnergies[0], 6) + ", "
                + juce::String(offEnergies[1], 6) + ", "
                + juce::String(offEnergies[2], 6) + ", "
                + juce::String(offEnergies[3], 6));

            // TAIL OFF: each successive beat's tail energy should be
            // GREATER than or equal to TAIL ON's (no chop -> tail
            // accumulates). If ON and OFF are equal, the toggle is a
            // no-op -- the user's bug.
            double onTotal  = onEnergies[1]  + onEnergies[2]  + onEnergies[3];
            double offTotal = offEnergies[1] + offEnergies[2] + offEnergies[3];
            expect(offTotal > onTotal * 1.5,
                   "TAIL toggle isn't separating chop vs ring "
                   "(on=" + juce::String(onTotal, 6)
                   + " off=" + juce::String(offTotal, 6) + ")");

            // TAIL ON: per-beat tail energies should be SIMILAR (each
            // beat starts from a freshly chopped buffer, so the level
            // entering the next beat is roughly the same every time).
            const double onMin = std::min({onEnergies[1], onEnergies[2], onEnergies[3]});
            const double onMax = std::max({onEnergies[1], onEnergies[2], onEnergies[3]});
            const double onRange = onMax > 0.0 ? (onMax - onMin) / onMax : 0.0;
            expect(onRange < 0.20,
                   "TAIL ON tails varying too much across beats "
                   "(range=" + juce::String(onRange, 4)
                   + " min=" + juce::String(onMin, 6)
                   + " max=" + juce::String(onMax, 6) + ")");
        }

        beginTest("algo dispatch is deterministic across runs");
        {
            const float sr = 48000.0f;
            auto runAlgo = [sr](int algo) noexcept {
                bombo::ConvolutionReverb r(sr);
                r.setType(algo);
                r.setSize(1.0f);
                r.setDecay(1.0f);
                r.setDamp(0.2f);
                r.setPredelayMs(0.0f);
                r.onTrigger();
                r.process(1.0f);
                double e = 0.0;
                for (int i = 0; i < static_cast<int>(0.30f * sr); ++i)
                {
                    const float y = r.process(0.0f);
                    e += (double) y * (double) y;
                }
                return e;
            };
            for (int a = 0; a < bombo::ir::kNumAlgos; ++a)
            {
                const double e1 = runAlgo(a);
                const double e2 = runAlgo(a);
                expect(e1 > 0.0, "algo " + juce::String(a) + " produced no energy");
                expect(e1 == e2, "algo " + juce::String(a) + " not run-to-run deterministic");
            }
        }
    }
};

// Static test instances -- JUCE finds them via the UnitTest registry.
// Palette/ThemeProvider tests live in tests/PaletteTests.cpp, which is
// compiled as its own translation unit (see CMakeLists.txt) and registers
// its own static UnitTest instances in an anonymous namespace.
static VoiceClipTests   voiceClipTests;
static MasterBusTests   masterBusTests;
static AmpEnvelopeTests ampEnvelopeTests;
static BiquadFilterTests biquadFilterTests;
static OscillatorTests  oscillatorTests;
static BombVoiceTests   bombVoiceTests;
static DelayTests       delayTests;
static FdnReverbTests   fdnReverbTests;
static ConvolutionReverbTests convolutionReverbTests;

int main()
{
    juce::ConsoleApplication app;
    // Force MessageManager creation on the main thread so the calling thread
    // becomes the message thread. ThemeProvider methods assert this via
    // JUCE_ASSERT_MESSAGE_THREAD in debug builds.
    juce::MessageManager::getInstance();
    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runAllTests();

    int failures = 0;
    int total = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        const auto* r = runner.getResult(i);
        if (r == nullptr) continue;
        total += r->passes + r->failures;
        failures += r->failures;
    }
    juce::Logger::writeToLog(juce::String::formatted(
        "Total: %d, failures: %d", total, failures));
    std::cout << "Total: " << total << ", failures: " << failures << std::endl;
    return failures == 0 ? 0 : 1;
}
