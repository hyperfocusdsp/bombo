// Source/GUI/BBS/Game/Audio.cpp
#include "Audio.h"
#include <cmath>

namespace bombo::game
{
    namespace
    {
        constexpr float kTwoPi = 6.28318530717958647692f;

        // Equal-tempered semitone ratio helper: midiNote -> Hz.
        inline float midiToHz(float note) noexcept
        {
            return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
        }
    }

    GameAudioBus::GameAudioBus() noexcept
    {
        for (auto& slot : ring_)
            slot.store(0u, std::memory_order_relaxed);
    }

    void GameAudioBus::prepare(double sampleRate) noexcept
    {
        sampleRate_ = (sampleRate > 0.0) ? static_cast<float>(sampleRate) : 48000.0f;
        // Reset voice pool — a fresh prepare means the audio graph restarted.
        for (auto& v : voices_)
            v.active = false;
        readIdx_ = writeIdx_.load(std::memory_order_acquire);  // drop stale requests
    }

    float GameAudioBus::nextNoise() noexcept
    {
        // xorshift32 — fast, deterministic, audio-thread-only PRNG.
        uint32_t x = noiseState_;
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        noiseState_ = x;
        return static_cast<float>(x) / static_cast<float>(0xFFFFFFFFu) * 2.0f - 1.0f;
    }

    // ── Message-thread triggers ─────────────────────────────────────────────

    void GameAudioBus::pushRequest(Op op, uint8_t payload) noexcept
    {
        const int w = writeIdx_.load(std::memory_order_relaxed);
        ring_[(size_t) (w & kRingMask)].store(pack(op, payload), std::memory_order_relaxed);
        // Publish: the audio thread's acquire-load of writeIdx_ sees the slot.
        writeIdx_.store(w + 1, std::memory_order_release);
    }

    void GameAudioBus::triggerEnemyHit(EnemyKind k) noexcept
    {
        pushRequest(Op::EnemyHit, static_cast<uint8_t>(k));
    }

    void GameAudioBus::triggerBossTelegraph() noexcept
    {
        pushRequest(Op::BossTelegraph, 0);
    }

    void GameAudioBus::triggerWaveClearJingle() noexcept
    {
        pushRequest(Op::WaveClear, 0);
    }

    void GameAudioBus::triggerGameOverJingle(bool victory) noexcept
    {
        pushRequest(victory ? Op::GameOverWin : Op::GameOverLose, 0);
    }

    void GameAudioBus::triggerPickupArpeggio(DropTier tier) noexcept
    {
        pushRequest(Op::Pickup, static_cast<uint8_t>(tier));
    }

    // ── Audio-thread synthesis ──────────────────────────────────────────────

    GameAudioBus::Voice* GameAudioBus::allocVoice() noexcept
    {
        // Prefer an inactive slot; otherwise steal the quietest active voice.
        Voice* quietest = nullptr;
        for (auto& v : voices_)
        {
            if (! v.active) return &v;
            if (quietest == nullptr || v.amp < quietest->amp)
                quietest = &v;
        }
        return quietest;
    }

    void GameAudioBus::note(float freq, float durSec, Wave wave, float gain,
                            int delaySamples, float noiseMix) noexcept
    {
        Voice* v = allocVoice();
        if (v == nullptr) return;

        v->active = true;
        v->wave   = wave;
        v->freq   = freq;
        v->phase  = 0.0f;
        v->amp    = 1.0f;
        v->gain   = gain;
        v->delaySamples = delaySamples;
        v->noiseMix = noiseMix;

        // Exponential decay reaching ~-60 dB over durSec.
        const float n = juce::jmax(1.0f, durSec * sampleRate_);
        v->decay = std::pow(0.001f, 1.0f / n);
    }

    void GameAudioBus::dispatch(Op op, uint8_t payload) noexcept
    {
        switch (op)
        {
            case Op::EnemyHit:
            {
                // Short bright ping; frequency varies by enemy kind so the
                // mix reads as distinct hits. Square for an 8-bit edge.
                float f;
                switch (static_cast<EnemyKind>(payload))
                {
                    case EnemyKind::Mudball:     f = 400.0f;  break;
                    case EnemyKind::Clipper:     f = 1200.0f; break;
                    case EnemyKind::SilenceVoid: f = 300.0f;  break;
                    case EnemyKind::Limiter:     f = 800.0f;  break;
                    case EnemyKind::Aliaser:     f = 1500.0f; break;
                    case EnemyKind::AliaserMini: f = 1800.0f; break;
                    case EnemyKind::DiveBomber:  f = 600.0f;  break;
                    case EnemyKind::Rumblr:      f = 220.0f;  break;
                    default:                     f = 700.0f;  break;
                }
                note(f, 0.012f, Wave::Square, 0.13f);
                break;
            }

            case Op::BossTelegraph:
                // Low ominous rumble: ~60 Hz sine + a touch of noise, ~200 ms.
                note(60.0f, 0.20f, Wave::Sine, 0.18f, 0, 0.25f);
                break;

            case Op::WaveClear:
            {
                // Ascending major arpeggio (C E G C') — square + triangle.
                const int stagger = static_cast<int>(0.06f * sampleRate_);
                note(midiToHz(60.0f), 0.10f, Wave::Square,   0.12f, 0 * stagger);
                note(midiToHz(64.0f), 0.10f, Wave::Triangle, 0.12f, 1 * stagger);
                note(midiToHz(67.0f), 0.10f, Wave::Square,   0.12f, 2 * stagger);
                note(midiToHz(72.0f), 0.16f, Wave::Triangle, 0.13f, 3 * stagger);
                break;
            }

            case Op::GameOverWin:
            {
                // Triumphant ascending fanfare (C G C' E').
                const int stagger = static_cast<int>(0.09f * sampleRate_);
                note(midiToHz(60.0f), 0.14f, Wave::Square,   0.13f, 0 * stagger);
                note(midiToHz(67.0f), 0.14f, Wave::Triangle, 0.13f, 1 * stagger);
                note(midiToHz(72.0f), 0.14f, Wave::Square,   0.13f, 2 * stagger);
                note(midiToHz(76.0f), 0.24f, Wave::Triangle, 0.14f, 3 * stagger);
                break;
            }

            case Op::GameOverLose:
            {
                // Descending minor lament (C' Ab F C).
                const int stagger = static_cast<int>(0.11f * sampleRate_);
                note(midiToHz(72.0f), 0.16f, Wave::Square,   0.12f, 0 * stagger);
                note(midiToHz(68.0f), 0.16f, Wave::Triangle, 0.12f, 1 * stagger);
                note(midiToHz(65.0f), 0.16f, Wave::Square,   0.12f, 2 * stagger);
                note(midiToHz(60.0f), 0.28f, Wave::Triangle, 0.13f, 3 * stagger);
                break;
            }

            case Op::Pickup:
            {
                // Rising arpeggio whose length scales with rarity.
                const int stagger = static_cast<int>(0.04f * sampleRate_);
                const auto tier = static_cast<DropTier>(payload);
                int notes = 2;
                if (tier == DropTier::Rare)            notes = 3;
                else if (tier == DropTier::Legendary)  notes = 4;

                // Scale steps (C E G + octave C) — legendary adds the octave.
                static const float steps[4] = { 60.0f, 64.0f, 67.0f, 72.0f };
                for (int i = 0; i < notes; ++i)
                    note(midiToHz(steps[i]), 0.07f, Wave::Square, 0.11f, i * stagger);
                break;
            }
        }
    }

    void GameAudioBus::drainRequests() noexcept
    {
        const int w = writeIdx_.load(std::memory_order_acquire);
        while (readIdx_ != w)
        {
            const uint32_t req = ring_[(size_t) (readIdx_ & kRingMask)]
                                     .load(std::memory_order_relaxed);
            ++readIdx_;
            const Op op       = static_cast<Op>(req & 0xFFu);
            const uint8_t pay = static_cast<uint8_t>((req >> 8) & 0xFFu);
            dispatch(op, pay);
        }
    }

    void GameAudioBus::renderInto(juce::AudioBuffer<float>& buffer) noexcept
    {
        drainRequests();

        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples <= 0 || numChannels <= 0) return;

        const float phaseInc0 = 1.0f / sampleRate_;

        // Hoist channel write pointers (cheap, no alloc) for the inner loop.
        constexpr int kMaxCh = 8;
        const int nCh = juce::jmin(numChannels, kMaxCh);
        float* chPtr[kMaxCh];
        for (int ch = 0; ch < nCh; ++ch)
            chPtr[ch] = buffer.getWritePointer(ch);

        // Mix each active voice additively onto EVERY output channel (the
        // game SFX are mono-summed — same signal in L and R alongside the
        // kick that already fills the buffer).
        for (auto& v : voices_)
        {
            if (! v.active) continue;

            const float phaseInc = v.freq * phaseInc0;

            for (int i = 0; i < numSamples; ++i)
            {
                if (v.delaySamples > 0) { --v.delaySamples; continue; }

                // Waveform oscillator (phase in 0..1).
                float s;
                switch (v.wave)
                {
                    case Wave::Square:
                        s = (v.phase < 0.5f) ? 1.0f : -1.0f;
                        break;
                    case Wave::Triangle:
                        s = 4.0f * std::abs(v.phase - 0.5f) - 1.0f;
                        break;
                    case Wave::Sine:
                    default:
                        s = std::sin(v.phase * kTwoPi);
                        break;
                }

                if (v.noiseMix > 0.0f)
                    s = s * (1.0f - v.noiseMix) + nextNoise() * v.noiseMix;

                const float out = s * v.amp * v.gain;
                for (int ch = 0; ch < nCh; ++ch)
                    chPtr[ch][i] += out;

                v.phase += phaseInc;
                if (v.phase >= 1.0f) v.phase -= 1.0f;
                v.amp *= v.decay;

                if (v.amp < 1.0e-4f) { v.active = false; break; }
            }
        }
    }

   #if defined(BOMBO_GAME_TEST_HOOKS)
    int GameAudioBus::testActiveVoiceCount() const noexcept
    {
        int n = 0;
        for (const auto& v : voices_)
            if (v.active) ++n;
        return n;
    }
   #endif
}
