#pragma once
#include "../../ParameterIds.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <array>
#include <vector>

namespace bombo
{

// Generates kick parameter snapshots (RANDOM or MUTATE), maintains an
// undo/redo navigation history (kPrevDepth backward + current + kNextDepth
// forward = 17 snapshots), and fires the kick trigger callback. Owns no
// audio resources — it only writes to APVTS and calls triggerCb_.
//
// Call setApvts() and setTriggerCallback() before advance() / prev() / next().
class BoomFeed
{
public:
    enum class Mode { Random, Mutate };

    BoomFeed();

    void setApvts(juce::AudioProcessorValueTreeState* apvts) noexcept;
    void setTriggerCallback(std::function<void()> cb) noexcept;

    // Generate next kick. Standard undo/redo semantics: drops everything
    // past the cursor (the "future" you'd P'd back through), appends the new
    // snapshot, advances the cursor. Caps total at kMaxHistory; the oldest
    // entry is dropped when the cap is exceeded. Message thread only.
    void advance(Mode mode);

    // Move the cursor backward through history. No-op when already at the
    // oldest in-memory entry.
    void prev();

    // Move the cursor forward through history. No-op when already at the
    // latest written entry. Use this to recover after pressing P too far.
    void next();

    juce::String currentFilename()      const;  // "KICK-XXXX-YYYY.KCK"
    juce::String currentWaveformAscii() const;  // 18-char █▇▆▅… bar string

    // Exposed for tests — generates without side effects.
    struct Snapshot
    {
        std::vector<std::pair<juce::String, float>> values;  // {pid, normalised}
    };
    static Snapshot generateRandom(juce::Random&);
    static Snapshot mutateFrom(const Snapshot& src, juce::Random&);

    // Capacity tunables. Public so tests and BBS hints can reference them.
    static constexpr int kPrevDepth  = 8;
    static constexpr int kNextDepth  = 8;
    static constexpr int kMaxHistory = kPrevDepth + 1 + kNextDepth;  // 17
    // Kept as kHistorySize for back-compat with prior call sites.
    static constexpr int kHistorySize = kMaxHistory;

private:
    juce::AudioProcessorValueTreeState* apvts_  = nullptr;
    std::function<void()>               triggerCb_;

    // Undo/redo navigation: snapshots in chronological order; cursor_ points
    // at the current one. cursor_ == -1 means no history yet. Capped at
    // kMaxHistory (declared in public section).
    std::vector<Snapshot> history_;
    int  cursor_ = -1;

    Snapshot current_;
    juce::Random rng_;

    void        applySnapshot(const Snapshot&);
    juce::String snapshotToFilename(const Snapshot&) const;
    juce::String snapshotToWaveform(const Snapshot&) const;

    struct ParamBounds { const char* id; float lo; float hi; };
    static const ParamBounds kRandomParams[];
    static const int kRandomParamsCount;
};

} // namespace bombo
