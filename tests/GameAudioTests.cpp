// tests/GameAudioTests.cpp
//
// Headless coverage for GameAudioBus (Task 24). We can't verify the SOUND
// QUALITY of the synthesised blips/jingles without ears — that needs a human
// listen test in the DAW (flagged in the task report). What we CAN verify
// headlessly: the bus produces non-silence after a trigger, stays silent when
// idle, and survives many triggers without crashing or unbounded growth.
#include "GUI/BBS/Game/Audio.h"
#include "GUI/BBS/Game/Entities.h"
#include "GUI/BBS/Game/Drops.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace
{
using namespace bombo::game;

// True if any sample in the buffer is meaningfully non-zero.
bool bufferHasSignal(const juce::AudioBuffer<float>& buf, float thresh = 1.0e-5f)
{
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* p = buf.getReadPointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            if (std::abs(p[i]) > thresh) return true;
    }
    return false;
}

float bufferPeak(const juce::AudioBuffer<float>& buf)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch)
    {
        const float* p = buf.getReadPointer(ch);
        for (int i = 0; i < buf.getNumSamples(); ++i)
            peak = std::max(peak, std::abs(p[i]));
    }
    return peak;
}

class GameAudioTests : public juce::UnitTest
{
public:
    GameAudioTests() : juce::UnitTest("GameAudio: procedural SFX bus") {}

    void runTest() override
    {
        beginTest("fresh bus with no triggers renders silence");
        {
            GameAudioBus bus;
            bus.prepare(48000.0);
            juce::AudioBuffer<float> buf(2, 512);
            buf.clear();
            // Many render passes: still silent without any trigger.
            for (int b = 0; b < 8; ++b)
                bus.renderInto(buf);
            expect(! bufferHasSignal(buf), "idle bus produced non-silent output");
           #if defined(BOMBO_GAME_TEST_HOOKS)
            expectEquals(bus.testActiveVoiceCount(), 0);
           #endif
        }

        beginTest("a trigger produces audible (non-silent) output");
        {
            GameAudioBus bus;
            bus.prepare(48000.0);
            bus.triggerEnemyHit(EnemyKind::Clipper);

            juce::AudioBuffer<float> buf(2, 512);
            buf.clear();
            bus.renderInto(buf);
            expect(bufferHasSignal(buf), "enemy-hit trigger rendered silence");
            // Modest level — no clipping from a single voice.
            expect(bufferPeak(buf) <= 1.0f, "single voice exceeded full scale");
        }

        beginTest("every trigger kind renders some signal");
        {
            auto rendersSignal = [this](auto fire)
            {
                GameAudioBus bus;
                bus.prepare(48000.0);
                fire(bus);
                juce::AudioBuffer<float> buf(2, 1024);
                buf.clear();
                // Several blocks so staggered/delayed notes get a chance.
                bool any = false;
                for (int b = 0; b < 16 && ! any; ++b)
                {
                    buf.clear();
                    bus.renderInto(buf);
                    any = bufferHasSignal(buf);
                }
                return any;
            };

            expect(rendersSignal([](GameAudioBus& b){ b.triggerEnemyHit(EnemyKind::Mudball); }),
                   "enemyHit silent");
            expect(rendersSignal([](GameAudioBus& b){ b.triggerBossTelegraph(); }),
                   "bossTelegraph silent");
            expect(rendersSignal([](GameAudioBus& b){ b.triggerWaveClearJingle(); }),
                   "waveClear silent");
            expect(rendersSignal([](GameAudioBus& b){ b.triggerGameOverJingle(true); }),
                   "gameOver(win) silent");
            expect(rendersSignal([](GameAudioBus& b){ b.triggerGameOverJingle(false); }),
                   "gameOver(lose) silent");
            expect(rendersSignal([](GameAudioBus& b){ b.triggerPickupArpeggio(DropTier::Legendary); }),
                   "pickup(legendary) silent");
        }

        beginTest("voices decay to silence over time");
        {
            GameAudioBus bus;
            bus.prepare(48000.0);
            bus.triggerEnemyHit(EnemyKind::Aliaser);   // ~12 ms ping
            juce::AudioBuffer<float> buf(2, 512);
            // Render ~1 second of audio; the short blip must die out.
            float peakLate = 0.0f;
            const int blocks = 48000 / 512 + 4;
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                bus.renderInto(buf);
                if (b > blocks / 2)
                    peakLate = std::max(peakLate, bufferPeak(buf));
            }
            expect(peakLate < 1.0e-4f, "blip did not decay to silence");
           #if defined(BOMBO_GAME_TEST_HOOKS)
            expectEquals(bus.testActiveVoiceCount(), 0, "voice left active after decay");
           #endif
        }

        beginTest("many triggers do not crash or grow voices unbounded");
        {
            GameAudioBus bus;
            bus.prepare(48000.0);
            juce::AudioBuffer<float> buf(2, 256);

            // Hammer the bus with far more triggers than the voice pool /
            // request ring can hold, interleaving renders. Must not crash.
            for (int i = 0; i < 2000; ++i)
            {
                bus.triggerEnemyHit(static_cast<EnemyKind>(i % 8));
                if ((i % 4) == 0) bus.triggerPickupArpeggio(DropTier::Common);
                if ((i % 16) == 0) bus.triggerWaveClearJingle();
                if ((i % 64) == 0) bus.triggerBossTelegraph();
                if ((i % 7) == 0)
                {
                    buf.clear();
                    bus.renderInto(buf);
                    expect(bufferPeak(buf) < 64.0f, "output diverged under flood");
                }
            }
            // Final drain render.
            buf.clear();
            bus.renderInto(buf);
           #if defined(BOMBO_GAME_TEST_HOOKS)
            expect(bus.testActiveVoiceCount() <= 8, "voice pool exceeded its fixed bound");
           #endif
            expect(std::isfinite(bufferPeak(buf)), "non-finite output after flood");
        }

        beginTest("prepare resets cleanly and accepts new triggers");
        {
            GameAudioBus bus;
            bus.prepare(44100.0);
            bus.triggerWaveClearJingle();
            // Re-prepare (graph restart) should drop stale state and stay valid.
            bus.prepare(96000.0);
            juce::AudioBuffer<float> buf(1, 512);
            buf.clear();
            bus.renderInto(buf);
            // After a re-prepare with no new trigger, no leftover sound.
            expect(! bufferHasSignal(buf), "stale sound survived re-prepare");

            bus.triggerEnemyHit(EnemyKind::Limiter);
            buf.clear();
            bus.renderInto(buf);
            expect(bufferHasSignal(buf), "trigger after re-prepare was silent");
        }
    }
};

static GameAudioTests gameAudioTests;
}
