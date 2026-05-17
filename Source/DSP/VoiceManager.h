#pragma once

#include <array>

#include "BombVoice.h"

namespace bombo
{

// Voice-pool plumbing extracted from BomboProcessor (2026-05-17). Owns
// the BombVoice pool, the pending-trigger ring used to schedule note-ons
// at sample-accurate offsets, and the active-voice cursor that the steal
// path advances on each trigger.
//
// Real-time safety: no allocations after construction, no locks, no
// std::string. All public methods are noexcept and called from the audio
// thread. The high-level processBlock orchestration (BPM/transport/tail
// kill scheduling) stays in BomboProcessor; this class is the audio-side
// machine the processor delegates to.
class VoiceManager
{
public:
    static constexpr int kNumVoices       = 4;
    static constexpr int kPendingRingSize = 8;

    VoiceManager();

    void prepare(float sampleRate) noexcept;

    // Schedule a NoteOn `samplesUntil` samples from now. No-op if the
    // ring is full — caller is expected to cap incoming triggers to
    // kNumVoices per buffer.
    void pushPending(int samplesUntil) noexcept;

    // Advance the per-sample countdown by one. Returns the number of
    // ring entries that fired this sample (0 or more). Call once per
    // output sample inside processBlock.
    int tickPending() noexcept;

    // Voice-steal: every active voice gets a 5 ms linear fadeout
    // (matches kVoiceFadeoutMs in BombVoice.h), then advance the
    // active-voice cursor. The caller is expected to invoke trigger()
    // immediately afterwards to fill the freshly-vacated slot.
    void stealAndAdvance() noexcept;

    // Drop a new trigger into the current active slot. Must be paired
    // with stealAndAdvance() — that order matches the legacy flow:
    // stealAll → advance → voices_[active].trigger(snapshot).
    void trigger(const VoiceTrigger& t) noexcept;

    // Tail-kill helper: fade EVERY active voice without advancing the
    // cursor. Used by the deferred tail-kill path so the audible decay
    // dies in step with chain_.killTail() on the FX bus.
    void fadeoutAllActive() noexcept;

    // Sum one sample from every voice in the pool.
    float renderSample() noexcept;

    bool anyActive() const noexcept;

    int activeIndex() const noexcept { return activeVoice_; }
    float sampleRate() const noexcept { return sampleRate_; }

private:
    struct PendingHit
    {
        int samplesUntil = 0;
        bool live = false;
    };

    std::array<BombVoice, kNumVoices>       voices_;
    std::array<PendingHit, kPendingRingSize> pending_{};
    int                                      activeVoice_ = 0;
    float                                    sampleRate_  = 48000.0f;
};

} // namespace bombo
