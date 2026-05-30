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
    // Mark the selection as "not a known preset" (e.g. after a host state
    // restore — the restored params don't correspond to any preset entry).
    void markCustom() noexcept             { current_ = -1; }
    bool isCurrentUserPreset() const noexcept;
    const std::string& currentName() const;
    const std::string& currentDisplayName() const;

    // Re-scan userPresetsDir() and rebuild the user portion of the list.
    // Preserves current_ by name (snaps to nearest valid index if the
    // currently-selected preset has gone away).
    void refreshUserPresets();

    // Import a downloaded preset bank file — a single preset object OR a JSON
    // array of them — into userPresetsDir() as individual user presets, then
    // rebuild. Returns the number imported. On-disk name collisions are
    // suffixed (_2, _3, ...) rather than overwritten.
    int  importBankFile(const juce::File& file);

    // Export the current bank to one JSON array file. includeFactory=false
    // writes only user presets (the shareable set); true also includes factory.
    bool exportBankToFile(const juce::File& file, bool includeFactory);

    // Apply preset[idx] to apvts. Sets current_. No-op if idx out of range.
    void applyByIndex(int idx, juce::AudioProcessorValueTreeState& apvts);
    void next(juce::AudioProcessorValueTreeState& apvts);
    void prev(juce::AudioProcessorValueTreeState& apvts);

    // Resets every (non-excluded) APVTS param to its declared default.
    // Also clears `current_` so the preset bar stops showing the last
    // loaded preset name, and fires onPresetApplied with a sentinel
    // "INIT" preset so listeners (editor) can schedule a tail reset just
    // like any normal preset apply — otherwise the just-loaded preset's
    // reverb tail bleeds through the first trigger after init.
    // Was previously static; made an instance method to access current_
    // and onPresetApplied.
    void applyDefaults(juce::AudioProcessorValueTreeState& apvts);

    // Snapshot the current APVTS state as a new user preset under displayName.
    // Sanitizes displayName to a filename (lowercase, alnum + `_`, length
    // capped). Returns the new index in the combined list, or -1 on failure
    // (empty name / disk error / dup). Marks the new preset as current.
    int  saveAs(const juce::String& displayName,
                juce::AudioProcessorValueTreeState& apvts);

    // Overwrite the currently-selected USER preset with the current APVTS
    // state. No-op + returns false if current is factory.
    bool overwriteCurrent(juce::AudioProcessorValueTreeState& apvts);

    // Rename the preset at idx. User presets rename their on-disk file.
    // Factory presets (compiled-in, no file) are renamed for the SESSION only
    // via an in-memory override — used to design the final factory bank, which
    // is then baked into Resources/Presets/*.json and rebuilt to lock it.
    bool renameAt(int idx, const juce::String& newDisplayName);

    // Delete the preset at idx. User presets delete their on-disk file.
    // Factory presets are HIDDEN for the SESSION only (can't remove BinaryData);
    // resets on plugin reload. current_ snaps to idx-1 (or 0) after deletion.
    bool deleteAt(int idx);

    // Cross-platform location for user-written presets. Created lazily.
    static juce::File userPresetsDir();
    static juce::String sanitizeFilename(const juce::String& displayName);

    // One-shot: relocate presets saved by pre-fix Linux builds to the old
    // doubled-".config" path into userPresetsDir(). No-op elsewhere / once done.
    static void migrateLegacyUserPresets();

private:
    void loadFactoryFromBinaryData();
    void rebuildAll();   // factory (with session overrides) + user, re-anchored
    // Pre-lock authoring: a factory preset is hidden whenever a USER preset
    // carries the same canonical `name` (case-insensitive). Saving / renaming
    // a factory preset materialises a user JSON carrying the factory's
    // canonical name, so the user copy shadows the compiled-in original and
    // there's no visible duplicate. Lets the whole bank be edited as user
    // presets until it's baked back into Resources/Presets/ and re-locked.
    void applyFactoryDedup();
    int  findByDisplayName(const juce::String& name) const;

    std::vector<Preset> presets_;
    int                 current_ = -1;

    // SESSION-only factory edits (factory presets are compiled-in, so these
    // can't persist across a plugin reload). Used to design the final factory
    // bank before baking it into the source JSONs. Keyed by canonical `name`.
    std::vector<std::string>                         hiddenFactoryNames_;
    std::vector<std::pair<std::string, std::string>> renamedFactory_;
};

} // namespace bombo
