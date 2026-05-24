// Source/GUI/BBS/Game/Audio.h
//
// GameAudioBus — a tiny lock-free procedural sound generator for the
// Kick Impact mini-game. ZERO asset files: every blip / jingle / arpeggio
// is synthesised from a fixed pool of short decaying tone voices.
//
// Threading contract:
//   - The trigger* methods run on the MESSAGE thread (game tick / input).
//     They hand off requests lock-free via a small SPSC ring of atomics.
//   - renderInto() runs on the AUDIO thread. It drains pending requests,
//     allocates tone voices from a fixed pool, and mixes them additively
//     into the supplied buffer. NO locks, NO heap allocation, NO file IO.
//
#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cstdint>

#include "Entities.h"   // EnemyKind
#include "Drops.h"      // DropTier

namespace bombo::game
{
    class GameAudioBus
    {
    public:
        GameAudioBus() noexcept;

        // Store the sample rate for phase-increment + envelope math. Safe to
        // call from prepareToPlay (audio thread is not running concurrently).
        void prepare(double sampleRate) noexcept;

        // ── Triggers (message thread) — lock-free handoff to the audio thread.
        void triggerEnemyHit(EnemyKind k) noexcept;
        void triggerBossTelegraph() noexcept;
        void triggerWaveClearJingle() noexcept;
        void triggerGameOverJingle(bool victory) noexcept;
        void triggerPickupArpeggio(DropTier tier) noexcept;

        // ── Audio thread: mix all active tone voices additively into buffer.
        // Mono-summed onto every channel. RT-safe.
        void renderInto(juce::AudioBuffer<float>& buffer) noexcept;

       #if defined(BOMBO_GAME_TEST_HOOKS)
        // Test-only: how many tone voices are currently sounding.
        int testActiveVoiceCount() const noexcept;
       #endif

    private:
        // ── Tone voice: a short decaying sine OR square blip with optional
        // start delay (for staggered arpeggio notes). All state lives on the
        // audio thread; never touched from the message thread.
        enum class Wave : uint8_t { Sine, Square, Triangle };

        struct Voice
        {
            bool   active = false;
            Wave   wave   = Wave::Sine;
            float  freq   = 0.0f;     // Hz
            float  phase  = 0.0f;     // 0..1
            float  amp    = 0.0f;     // current envelope value
            float  decay  = 0.0f;     // per-sample multiplicative decay
            float  gain   = 0.15f;    // peak amplitude
            int    delaySamples = 0;  // remaining start delay before sounding
            float  noiseMix = 0.0f;   // 0..1 blend of white noise (boss rumble)
        };

        static constexpr int kNumVoices = 8;

        // ── Lock-free SPSC request ring. The message thread packs a request
        // into a uint32 slot and bumps the write index; the audio thread
        // reads up to the write index and bumps the read index. Power-of-two
        // size for cheap masking.
        static constexpr int kRingSize = 32;
        static constexpr int kRingMask = kRingSize - 1;

        // Request opcodes packed into the low byte; payload in the next byte.
        enum class Op : uint8_t
        {
            EnemyHit, BossTelegraph, WaveClear, GameOverWin, GameOverLose, Pickup
        };

        static uint32_t pack(Op op, uint8_t payload) noexcept
        {
            return static_cast<uint32_t>(op) | (static_cast<uint32_t>(payload) << 8);
        }

        void pushRequest(Op op, uint8_t payload) noexcept;   // message thread
        void drainRequests() noexcept;                       // audio thread
        void dispatch(Op op, uint8_t payload) noexcept;      // audio thread

        // Allocate a voice from the pool (steals the quietest if all busy).
        Voice* allocVoice() noexcept;
        // Schedule a single note (audio thread, from dispatch()).
        void   note(float freq, float durSec, Wave wave, float gain,
                    int delaySamples = 0, float noiseMix = 0.0f) noexcept;

        std::array<Voice, kNumVoices> voices_{};
        std::array<std::atomic<uint32_t>, kRingSize> ring_{};
        std::atomic<int> writeIdx_{ 0 };
        int              readIdx_ = 0;     // audio-thread only

        float sampleRate_ = 48000.0f;
        uint32_t noiseState_ = 0x1234567u; // xorshift PRNG (audio thread only)

        float nextNoise() noexcept;
    };
}
