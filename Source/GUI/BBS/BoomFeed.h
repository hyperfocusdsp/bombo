#pragma once
#include "../../ParameterIds.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <array>
#include <vector>

namespace bombo
{

// Generates kick parameter snapshots (RANDOM or MUTATE), maintains a
// 5-entry history ring for PREV, and fires the kick trigger callback.
// Owns no audio resources — it only writes to APVTS and calls triggerCb_.
//
// Call setApvts() and setTriggerCallback() before advance() / prev().
class BoomFeed
{
public:
    enum class Mode { Random, Mutate };

    BoomFeed();

    void setApvts(juce::AudioProcessorValueTreeState* apvts) noexcept;
    void setTriggerCallback(std::function<void()> cb) noexcept;

    // Generate next kick (adds current to history first), apply to APVTS,
    // fire trigger. Call from message thread only.
    void advance(Mode mode);

    // Restore the previous snapshot from the ring buffer.
    void prev();

    juce::String currentFilename()      const;  // "KICK-XXXX-YYYY.KCK"
    juce::String currentWaveformAscii() const;  // 18-char █▇▆▅… bar string

    // Exposed for tests — generates without side effects.
    struct Snapshot
    {
        std::vector<std::pair<juce::String, float>> values;  // {pid, normalised}
    };
    static Snapshot generateRandom(juce::Random&);
    static Snapshot mutateFrom(const Snapshot& src, juce::Random&);

private:
    juce::AudioProcessorValueTreeState* apvts_  = nullptr;
    std::function<void()>               triggerCb_;

    static constexpr int kHistorySize = 5;
    std::array<Snapshot, kHistorySize> history_;
    int  historyHead_  = 0;
    int  historyCount_ = 0;

    Snapshot current_;
    juce::Random rng_;

    void        applySnapshot(const Snapshot&);
    void        pushHistory(const Snapshot&);
    juce::String snapshotToFilename(const Snapshot&) const;
    juce::String snapshotToWaveform(const Snapshot&) const;

    struct ParamBounds { const char* id; float lo; float hi; };
    static const ParamBounds kRandomParams[];
    static const int kRandomParamsCount;
};

} // namespace bombo
