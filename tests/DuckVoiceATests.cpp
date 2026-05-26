// tests/DuckVoiceATests.cpp -- reverse-bass duck on Voice A (BombVoice).
//
// The "D" toggle routes the DUCK envelope onto Voice A (sub), keyed by Voice
// B's punch, on top of the normal post-FX bus duck. The load-bearing safety
// property: with the toggle OFF the sub path is untouched no matter what the
// duck params hold -- so enabling the feature can never regress existing
// presets. Also pins depth-0 passthrough and that ducking actually lowers
// Voice A energy.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include "../Source/DSP/BombVoice.h"
#include "../Source/State/DuckMigration.h"

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

        beginTest("VoiceTrigger has duckRouting field, defaults to 0 (Off)");
        {
            bombo::VoiceTrigger t;
            expectEquals(t.duckRouting, 0, "default duckRouting must be Off (0)");
        }

        beginTest("operator== respects duckRouting field");
        {
            bombo::VoiceTrigger a, b;
            a.duckRouting = 0;  b.duckRouting = 0;
            expect(a == b, "identical Off triggers must compare equal");
            b.duckRouting = 1;
            expect(a != b, "differing duckRouting must compare unequal");
        }

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
            on.duckRouting = 1;  on.duckDepth = 0.0f;  on.duckGrowl = 0.0f;

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
            ducked.duckRouting = 1;  ducked.duckDepth = 0.9f;
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

        beginTest("duckRouting=B (2) ducks Voice B body via synthetic trigger pulse");
        {
            constexpr int N = 3000;
            // driveMute=true linearises the post-voice path so the energy
            // delta from body-only ducking is observable (otherwise the
            // diode clipper saturates and absorbs the change).
            bombo::VoiceTrigger off;   off.duckRouting = 0;  off.duckDepth = 0.0f; off.driveMute = true;
            bombo::VoiceTrigger bMode; bMode.driveMute = true;
            bMode.duckRouting = 2;
            bMode.duckDepth   = 0.9f;
            bMode.duckAtkMs   = 2.0f;
            bMode.duckRelMs   = 200.0f;

            const auto baseline = renderVoice(off,   N);
            const auto ducked   = renderVoice(bMode, N);

            double eBase = 0.0, eDuck = 0.0;
            for (int i = 200; i < N; ++i)
            {
                eBase += baseline[(size_t) i] * baseline[(size_t) i];
                eDuck += ducked  [(size_t) i] * ducked  [(size_t) i];
            }
            expect(eDuck < eBase,
                   "B-mode duck must reduce total energy vs Off baseline (linear path)");
        }

        beginTest("duckRouting=AB (3) ducks both voices");
        {
            constexpr int N = 3000;
            bombo::VoiceTrigger off;    off.duckRouting = 0;  off.duckDepth = 0.0f; off.driveMute = true;
            bombo::VoiceTrigger aMode;  aMode.driveMute = true;
            aMode.duckRouting = 1;  aMode.duckDepth = 0.9f; aMode.duckAtkMs = 2.0f; aMode.duckRelMs = 200.0f;
            bombo::VoiceTrigger abMode; abMode.driveMute = true;
            abMode.duckRouting = 3;
            abMode.duckDepth   = 0.9f;
            abMode.duckAtkMs   = 2.0f;
            abMode.duckRelMs   = 200.0f;

            const auto baseline = renderVoice(off,    N);
            const auto aDucked  = renderVoice(aMode,  N);
            const auto abDucked = renderVoice(abMode, N);

            double eBase = 0.0, eA = 0.0, eAB = 0.0;
            for (int i = 200; i < N; ++i)
            {
                eBase += baseline[(size_t) i] * baseline[(size_t) i];
                eA    += aDucked [(size_t) i] * aDucked [(size_t) i];
                eAB   += abDucked[(size_t) i] * abDucked[(size_t) i];
            }
            expect(eAB < eBase, "AB-mode must reduce energy vs Off baseline");
            expect(eAB < eA,    "AB-mode must reduce energy more than A-only (B-mode adds on top)");
        }

        beginTest("duckRouting=Off (0) keeps duck params inert regardless of values");
        {
            constexpr int N = 3000;
            bombo::VoiceTrigger a;   // default — duckRouting=0
            bombo::VoiceTrigger b;   // also Off, but with duck params dialled
            b.duckDepth = 0.9f;  b.duckGrowl = 0.7f;  b.duckShape = -0.5f;
            const auto va = renderVoice(a, N);
            const auto vb = renderVoice(b, N);
            int mism = 0;
            for (int i = 0; i < N; ++i) if (va[(size_t) i] != vb[(size_t) i]) ++mism;
            expectEquals(mism, 0, "Off must skip both per-voice duckers entirely");
        }

        beginTest("legacy duck_voice_a=true migrates to duck_routing=A on state load");
        {
            juce::ValueTree state("PARAMETERS");
            juce::ValueTree p1("PARAM");
            p1.setProperty("id", "duck_voice_a", nullptr);
            p1.setProperty("value", 1.0f, nullptr);
            state.appendChild(p1, nullptr);

            auto migrated = bombo::migrateDuckVoiceAToRouting(state);

            auto routing = migrated.getChildWithProperty("id", "duck_routing");
            expect(routing.isValid(), "migration must insert duck_routing PARAM");
            expectWithinAbsoluteError((float) routing.getProperty("value"), 1.0f / 3.0f, 1.0e-4f,
                "duck_routing value should be index 1 (A) normalized to 1/3");

            auto legacy = migrated.getChildWithProperty("id", "duck_voice_a");
            expect(! legacy.isValid(),
                "legacy duck_voice_a PARAM must be removed after migration");
        }

        beginTest("legacy duck_voice_a=false migrates to duck_routing=Off");
        {
            juce::ValueTree state("PARAMETERS");
            juce::ValueTree p1("PARAM");
            p1.setProperty("id", "duck_voice_a", nullptr);
            p1.setProperty("value", 0.0f, nullptr);
            state.appendChild(p1, nullptr);

            auto migrated = bombo::migrateDuckVoiceAToRouting(state);
            auto routing = migrated.getChildWithProperty("id", "duck_routing");
            expect(routing.isValid(), "migration must insert duck_routing PARAM");
            expectWithinAbsoluteError((float) routing.getProperty("value"), 0.0f, 1.0e-4f,
                "duck_routing value should be index 0 (Off)");
        }

        beginTest("migration is no-op when duck_voice_a absent");
        {
            juce::ValueTree state("PARAMETERS");
            juce::ValueTree p1("PARAM");
            p1.setProperty("id", "duck_routing", nullptr);
            p1.setProperty("value", 0.6667f, nullptr);   // pre-set to AB-ish
            state.appendChild(p1, nullptr);

            auto migrated = bombo::migrateDuckVoiceAToRouting(state);
            auto routing = migrated.getChildWithProperty("id", "duck_routing");
            expect(routing.isValid(), "duck_routing must still be present");
            expectWithinAbsoluteError((float) routing.getProperty("value"), 0.6667f, 1.0e-4f,
                "duck_routing value must be preserved when no legacy field exists");
        }
    }
};

static DuckVoiceATests duckVoiceATests;

} // namespace
