// tests/VoiceStealingTests.cpp — polyphonic load + tail-kill regression.
//
// Layered in 2026-05-17 before the v1.0 sprint (BBS overlay + chassis
// reshape + preset bank) touches anything around the voice pool. The
// existing BombVoiceTests in RunTests.cpp covers single-voice identity
// and decay; this file covers what happens when 8 voices fire near-
// simultaneously and what happens to the mix once everything fades out.
//
// Compiled as its own translation unit (see CMakeLists.txt).

#include "DSP/BombVoice.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

class VoiceStealingTests : public juce::UnitTest
{
public:
    VoiceStealingTests() : juce::UnitTest("VoiceStealing: rapid polyphony stays clean") {}

    void runTest() override
    {
        constexpr float sr = 48000.0f;
        constexpr int   numVoices = 8;

        std::vector<bombo::BombVoice> voices;
        voices.reserve(numVoices);
        for (int i = 0; i < numVoices; ++i) voices.emplace_back(sr);

        beginTest("8 voices fired in <1 ms: mix is finite + bounded");
        {
            const bombo::VoiceTrigger t;
            // ~6 samples between hits at 48 kHz = ~125 µs spacing,
            // so all 8 voices trigger inside ~1 ms.
            constexpr int spacing      = 6;
            constexpr int totalSamples = 57600;  // 1.2 s @ 48 kHz
            int nextHit       = 0;
            int hitsRemaining = numVoices;
            float maxAbs = 0.0f;
            bool  anyNonFinite = false;

            for (int i = 0; i < totalSamples; ++i)
            {
                if (hitsRemaining > 0 && i >= nextHit)
                {
                    voices[numVoices - hitsRemaining].trigger(t);
                    nextHit += spacing;
                    --hitsRemaining;
                }
                float sum = 0.0f;
                for (auto& v : voices) sum += v.tick();
                if (! std::isfinite(sum)) { anyNonFinite = true; break; }
                maxAbs = std::max(maxAbs, std::abs(sum));
            }
            expect(! anyNonFinite, "no NaN/INF anywhere in the polyphonic mix");
            expect(maxAbs > 0.5f,  "polyphonic mix is audible");
            // 8 unity-amplitude voices can in principle sum to 8, but the
            // pitch envs and amp envs guarantee no two voices peak at the
            // same sample. A generous ceiling of (numVoices + 1) catches
            // an unbounded-feedback regression without false-positiving on
            // legitimate constructive interference.
            expect(maxAbs < static_cast<float>(numVoices + 1),
                   "polyphonic mix bounded by voice count + 1");
        }

        beginTest("after all voices fade out, mix is silent");
        {
            // Trigger fadeouts on whatever's still ringing, then run past
            // the 5 ms fade window plus a safety margin.
            for (auto& v : voices) v.startFadeout(sr);
            const int settle = static_cast<int>(bombo::kVoiceFadeoutMs * 0.001f * sr) + 64;
            for (int i = 0; i < settle; ++i)
                for (auto& v : voices) v.tick();

            const int probe = static_cast<int>(0.01f * sr);
            float peak = 0.0f;
            for (int i = 0; i < probe; ++i)
            {
                float sum = 0.0f;
                for (auto& v : voices) sum += v.tick();
                peak = std::max(peak, std::abs(sum));
            }
            expect(peak < 1e-4f, "no audio after fadeout completes");
            for (auto& v : voices)
                expect(! v.isActive(), "voice inactive after fadeout");
        }

        beginTest("voice steal mid-decay: fadeout produces no audible click");
        {
            // A single voice, fully ringing — the 5 ms linear fadeout must
            // ramp it down smoothly. The maximum sample-to-sample step
            // during the fadeout window must be small relative to the
            // pre-fadeout signal level.
            bombo::BombVoice v(sr);
            v.trigger(bombo::VoiceTrigger{});
            for (int i = 0; i < 480; ++i) v.tick();   // 10 ms into decay

            // Probe the pre-fadeout level by averaging |s| over a short
            // window. Using one sample as the prev reference would let a
            // zero-crossing falsely satisfy the jump test.
            float preLevel = 0.0f;
            for (int i = 0; i < 48; ++i) preLevel = std::max(preLevel, std::abs(v.tick()));
            expect(preLevel > 1e-3f, "voice was audible before fadeout");

            v.startFadeout(sr);

            // Prime prev with the first post-fadeout sample so the very
            // first measured jump is from sample 0 → 1, not from 0 → near-
            // peak. (Otherwise we'd measure the difference between the
            // arbitrary init value and the first real sample, which has
            // nothing to do with smoothness.)
            float prev = v.tick();
            float maxJump = 0.0f;
            const int fade = static_cast<int>(bombo::kVoiceFadeoutMs * 0.001f * sr) + 4;
            for (int i = 1; i < fade; ++i)
            {
                const float cur = v.tick();
                maxJump = std::max(maxJump, std::abs(cur - prev));
                prev = cur;
            }
            // 5 ms linear fadeout × pre-level → per-sample step ≤ preLevel/240.
            // Allow generous headroom for the underlying oscillator's own
            // cycle (sub-bass at ~50 Hz contributes its own slope) but
            // reject anything close to preLevel itself, which would mean
            // the fadeout was effectively a hard cut.
            expect(maxJump < preLevel * 0.5f,
                   "fadeout slope bounded — no hard-cut click");
        }
    }
};

static VoiceStealingTests voiceStealingTests;

} // anonymous namespace
