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
#include "DSP/VoiceManager.h"

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

        beginTest("ALL prior voices fade out within steal window — no stragglers");
        {
            // Regression for the user-reported 2026-05-17 "voice A continues
            // its release past the next trig" bug. With 4 voices and long
            // decays, the old stealVoice logic only faded current + next,
            // leaving 2 intermediate voices ringing through new triggers.
            // Now every new trigger must drive ALL prior voices to silence
            // within kVoiceFadeoutMs + a small safety margin.
            constexpr int N = 4;
            std::vector<bombo::BombVoice> pool;
            pool.reserve(N);
            for (int i = 0; i < N; ++i) pool.emplace_back(sr);

            const bombo::VoiceTrigger t;       // default — long 700 ms decay

            // Fire each pool voice in sequence, 100 ms apart — fills the pool.
            constexpr int gap = static_cast<int>(0.1f * 48000.0f);
            for (int i = 0; i < N; ++i)
            {
                pool[i].trigger(t);
                for (int s = 0; s < gap; ++s)
                    for (auto& v : pool) v.tick();
            }
            // All 4 should still be active (decays are way > 400 ms).
            int activeBefore = 0;
            for (const auto& v : pool) if (v.isActive()) ++activeBefore;
            expect(activeBefore == N, "all 4 voices active before steal");

            // Simulate the new stealVoice + new trigger: fade EVERY active
            // voice, then trigger pool[0] fresh.
            for (auto& v : pool) if (v.isActive()) v.startFadeout(sr);
            pool[0].trigger(t);  // fresh voice in slot 0

            // Run past the fadeout window with a small safety margin.
            const int settle = static_cast<int>(bombo::kVoiceFadeoutMs * 0.001f * sr) + 32;
            for (int i = 0; i < settle; ++i)
                for (auto& v : pool) v.tick();

            // Only the newly-triggered voice should still be active.
            int activeAfter = 0;
            for (int i = 0; i < N; ++i) if (pool[i].isActive()) ++activeAfter;
            expect(activeAfter == 1, "exactly one voice active after fadeout window");
            expect(pool[0].isActive(), "the newly-triggered voice is the active one");
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

        beginTest("VoiceManager: pushPending + tickPending fire at correct offsets");
        {
            // Construct a VoiceManager standalone — no processor, no APVTS,
            // no host. This is the win from extracting it (2026-05-17):
            // the audio-side state machine is unit-testable in isolation.
            bombo::VoiceManager vm;
            vm.prepare(sr);

            // Schedule three hits at samples 0, 5, 23 inside a hypothetical
            // 32-sample buffer. Tick the manager 32 times and count firings.
            vm.pushPending(0);
            vm.pushPending(5);
            vm.pushPending(23);

            int fired = 0;
            int firedAt0 = 0, firedAt5 = 0, firedAt23 = 0;
            for (int i = 0; i < 32; ++i)
            {
                const int n = vm.tickPending();
                fired += n;
                if (i == 0)  firedAt0  = n;
                if (i == 5)  firedAt5  = n;
                if (i == 23) firedAt23 = n;
            }
            expect(fired == 3,    "three scheduled hits all fired exactly once");
            expect(firedAt0 == 1, "sample-0 hit fires at sample 0");
            expect(firedAt5 == 1, "sample-5 hit fires at sample 5");
            expect(firedAt23 == 1, "sample-23 hit fires at sample 23");
        }

        beginTest("VoiceManager: stealAndAdvance fades active voices + rotates cursor");
        {
            bombo::VoiceManager vm;
            vm.prepare(sr);
            const int startIdx = vm.activeIndex();

            const bombo::VoiceTrigger t;
            vm.trigger(t);  // active slot now ringing
            expect(vm.anyActive(), "voice active after trigger");

            vm.stealAndAdvance();
            expect(vm.activeIndex() == (startIdx + 1) % bombo::VoiceManager::kNumVoices,
                   "cursor advanced one slot mod pool size");

            // Run the fadeout to completion; the previously-active voice
            // must hit silence within the fadeout window.
            const int settle = static_cast<int>(bombo::kVoiceFadeoutMs * 0.001f * sr) + 32;
            for (int i = 0; i < settle; ++i) (void) vm.renderSample();
            expect(! vm.anyActive(), "all voices silent after fadeout completes");
        }

        beginTest("VoiceManager: fadeoutAllActive does NOT advance the cursor");
        {
            bombo::VoiceManager vm;
            vm.prepare(sr);
            const int startIdx = vm.activeIndex();

            const bombo::VoiceTrigger t;
            vm.trigger(t);
            vm.fadeoutAllActive();
            expect(vm.activeIndex() == startIdx,
                   "fadeoutAllActive leaves cursor in place (tail-kill path)");
        }
    }
};

static VoiceStealingTests voiceStealingTests;

} // anonymous namespace
