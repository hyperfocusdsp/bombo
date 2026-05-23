#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "../DSP/FxOrder.h"

namespace bombo
{

// Factory + user preset bank (Phase 3 + MVP CRUD, 2026-05-17).
//
// Factory presets live in BinaryData (read-only, ship with the binary).
// User presets live on disk in userPresetsDir() and are mutable: save /
// rename / delete. Both kinds appear in the same combined list — factory
// first (NN_ filename order), then user (alphabetical by display name).
//
// JSON shape (both factory + user):
//   {
//     "name":        "PULSE",
//     "displayName": "Pulse",      // optional; falls back to name
//     "params": { "pitch_start": 145.0, "amp_decay": 380.0, ... }
//   }
//
// applyByIndex / applyDefaults skip these param IDs regardless of JSON:
//   master_out, bpm, loop_on, limiter_on, *_mute.
class PresetBank
{
public:
    enum class Source { Factory, User };

    struct Preset
    {
        Source                                     source = Source::Factory;
        std::string                                name;         // canonical (any case)
        std::string                                displayName;  // pretty
        std::vector<std::pair<std::string, float>> params;
        juce::File                                 filePath;     // empty for factory
        // Per-preset FX chain order. Absent on legacy presets that pre-date
        // the feature — apply path falls back to the chain's current order
        // in that case (no-op), preserving the user's last manual choice.
        std::optional<FxOrder>                     fxOrder;
    };

    // Fires at the end of every successful applyByIndex with the just-
    // applied preset. The editor wires this to push preset.fxOrder into
    // both the processor (DSP) and the faceplate (UI).
    std::function<void(const Preset&)> onPresetApplied;

    // Queried at save-time to capture the current FX chain order into the
    // resulting JSON. The editor sets this to a closure returning
    // BomboProcessor::getFxOrder(). Returns std::nullopt to omit the field.
    std::function<std::optional<FxOrder>()> fxOrderProvider;

    PresetBank();

    int  size() const noexcept             { return static_cast<int>(presets_.size()); }
    bool empty() const noexcept            { return presets_.empty(); }
    const Preset& at(int idx) const        { return presets_[(size_t) idx]; }

    int  currentIndex() const noexcept     { return current_; }
    bool isCurrentUserPreset() const noexcept;
    const std::string& currentName() const;
    const std::string& currentDisplayName() const;

    // Re-scan userPresetsDir() and rebuild the user portion of the list.
    // Preserves current_ by name (snaps to nearest valid index if the
    // currently-selected preset has gone away).
    void refreshUserPresets();

    // Apply preset[idx] to apvts. Sets current_. No-op if idx out of range.
    void applyByIndex(int idx, juce::AudioProcessorValueTreeState& apvts);
    void next(juce::AudioProcessorValueTreeState& apvts);
    void prev(juce::AudioProcessorValueTreeState& apvts);

    // Resets every (non-excluded) APVTS param to its declared default.
    static void applyDefaults(juce::AudioProcessorValueTreeState& apvts);

    // Snapshot the current APVTS state as a new user preset under displayName.
    // Sanitizes displayName to a filename (lowercase, alnum + `_`, length
    // capped). Returns the new index in the combined list, or -1 on failure
    // (empty name / disk error / dup). Marks the new preset as current.
    int  saveAs(const juce::String& displayName,
                juce::AudioProcessorValueTreeState& apvts);

    // Overwrite the currently-selected USER preset with the current APVTS
    // state. No-op + returns false if current is factory.
    bool overwriteCurrent(juce::AudioProcessorValueTreeState& apvts);

    // Rename the user preset at idx. No-op + returns false if idx points
    // to a factory preset, the new name sanitizes to empty, or the new
    // file already exists.
    bool renameAt(int idx, const juce::String& newDisplayName);

    // Delete the user preset at idx. No-op + returns false for factory.
    // current_ snaps to idx-1 (or 0) after deletion.
    bool deleteAt(int idx);

    // Cross-platform location for user-written presets. Created lazily.
    static juce::File userPresetsDir();
    static juce::String sanitizeFilename(const juce::String& displayName);

private:
    void loadFactoryFromBinaryData();
    int  findByDisplayName(const juce::String& name) const;

    std::vector<Preset> presets_;
    int                 current_ = -1;
};

} // namespace bombo
