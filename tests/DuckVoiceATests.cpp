// tests/DuckVoiceATests.cpp -- reverse-bass duck on Voice A (BombVoice).
//
// The "D" toggle routes the DUCK envelope onto Voice A (sub), keyed by Voice
// B's punch, on top of the normal post-FX bus duck. The load-bearing safety
// property: with the toggle OFF the sub path is untouched no matter what the
// duck params hold -- so enabling the feature can never regress existing
// presets. Also pins depth-0 passthrough and that ducking actually lowers
// Voice A energy.

#include <juce_core/juce_core.h>

#include "../Source/DSP/BombVoice.h"

#include <cmath>
#include <vector>

namespace
{

std::vector<float> renderVoice(const bombo::VoiceTrigger& t, int n)
{
    bombo::BombVoice v(48000.0f);
    v.trigger(t);
    std::vector<float> out((size_t) n);
    for (int i = 0; i < n; ++i) out[(size_t) i] = v.tick();
    return out;
}

class DuckVoiceATests : public juce::UnitTest
{
public:
    DuckVoiceATests() : juce::UnitTest("Duck Voice A (reverse-bass)") {}

    void runTest() override
    {
        constexpr int N = 3000;

        beginTest("toggle OFF is bit-identical regardless of duck-param values");
        {
            bombo::VoiceTrigger off1;            // toggle off, default duck params
            bombo::VoiceTrigger off2 = off1;     // toggle off, but duck params dialled
            off2.duckDepth = 0.9f;  off2.duckGrowl = 0.7f;  off2.duckShape = -0.5f;
            off2.duckAtkMs = 1.0f;  off2.duckRelMs = 400.0f; off2.duckHoldMs = 50.0f;

            const auto a = renderVoice(off1, N);
            const auto b = renderVoice(off2, N);
            int mism = 0;
            for (int i = 0; i < N; ++i) if (a[(size_t) i] != b[(size_t) i]) ++mism;
            expectEquals(mism, 0, "duck params are inert while the toggle is off");
        }

        beginTest("ON with depth=0 + growl=0 is a clean passthrough (== OFF)");
        {
            bombo::VoiceTrigger off;
            bombo::VoiceTrigger on = off;
            on.duckVoiceA = true;  on.duckDepth = 0.0f;  on.duckGrowl = 0.0f;

            const auto a = renderVoice(off, N);
            const auto b = renderVoice(on, N);
            int mism = 0;
            for (int i = 0; i < N; ++i)
                if (std::abs(a[(size_t) i] - b[(size_t) i]) > 1.0e-7f) ++mism;
            expectEquals(mism, 0, "depth 0 + growl 0 ducker is bypass");
        }

        beginTest("ON with depth>0 lowers Voice A energy but voice still sounds");
        {
            bombo::VoiceTrigger base;
            base.voiceBalance = 0.5f;            // both A (sub) and B (body) present
            bombo::VoiceTrigger ducked = base;
            ducked.duckVoiceA = true;  ducked.duckDepth = 0.9f;
            ducked.duckAtkMs = 1.0f;   ducked.duckRelMs = 200.0f;  ducked.duckGrowl = 0.0f;

            const auto off = renderVoice(base, N);
            const auto on  = renderVoice(ducked, N);
            double eOff = 0.0, eOn = 0.0;
            for (int i = 0; i < N; ++i)
            {
                eOff += std::abs((double) off[(size_t) i]);
                eOn  += std::abs((double) on[(size_t) i]);
            }
            expect(eOn < eOff, "ducking Voice A reduces total energy");
            expect(eOn > 0.0,  "Voice B punch is untouched, so output is non-silent");
        }
    }
};

static DuckVoiceATests duckVoiceATests;

} // namespace
