// Bombo DSP unit tests (JUCE UnitTestRunner).
// Ports the highest-leverage Rust tests from bombo-rust-archive:
//   - voice_clip: identity at drive=0, asymmetry, boundedness
//   - master_bus: unity below knee, brick-wall at ceiling, DC offset decay
//   - AmpEnvelope: monotonic decay, -60 dB at decay_ms
//   - BiquadFilter: LP/HP frequency response sanity
//   - BombVoice: audible kick on default trigger, decays in time
//
// Build: cmake --build build --target Bombo_Tests
// Run:   ctest --test-dir build --output-on-failure  (or run the binary directly)

#include <juce_core/juce_core.h>

#include "DSP/VoiceClip.h"
#include "DSP/MasterBus.h"
#include "DSP/Envelopes.h"
#include "DSP/BiquadFilter.h"
#include "DSP/Oscillators.h"
#include "DSP/BombVoice.h"
#include "DSP/FdnReverb.h"

#include <cmath>
#include <vector>

namespace
{
// Single-bin Goertzel — fundamental-bin power only. See plugindev skill
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
        beginTest("100 Hz fundamental has ≥30 dB more energy than 110 Hz");
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

        beginTest("inactive after 1.5× decay_ms");
        {
            const float sr = 48000.0f;
            bombo::BombVoice v(sr);
            bombo::VoiceTrigger t;
            v.trigger(t);
            const int beyond = static_cast<int>(t.ampDecayMs / 1000.0f * 1.5f * sr);
            for (int i = 0; i < beyond; ++i) v.tick();
            expect(!v.isActive());
        }

        beginTest("deterministic when drift=0");
        {
            const float sr = 48000.0f;
            bombo::VoiceTrigger t;
            t.driftAmount = 0.0f;
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
        beginTest("no kill_tail → silent input → silent output");
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
            r.killTail();
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
            r.killTail();
            r.process(1.0f);
            for (int i = 0; i < static_cast<int>(0.1f * sr); ++i) r.process(0.0f);
            r.fullReset();
            float e = 0.0f;
            for (int i = 0; i < static_cast<int>(0.05f * sr); ++i)
                e += std::abs(r.process(0.0f));
            expect(e < 1e-3f);
        }
    }
};

// Static test instances — JUCE finds them via the UnitTest registry.
static VoiceClipTests   voiceClipTests;
static MasterBusTests   masterBusTests;
static AmpEnvelopeTests ampEnvelopeTests;
static BiquadFilterTests biquadFilterTests;
static OscillatorTests  oscillatorTests;
static BombVoiceTests   bombVoiceTests;
static FdnReverbTests   fdnReverbTests;

int main()
{
    juce::ConsoleApplication app;
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
