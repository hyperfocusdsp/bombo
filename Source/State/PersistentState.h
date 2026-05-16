#pragma once

#include <juce_data_structures/juce_data_structures.h>

#include <memory>

namespace bombo
{

// Wraps juce::PropertiesFile for non-DAW user-global state.
// State lives at the platform user-app-data location:
//   Linux:   ~/.config/Bombo/Bombo.settings
//   macOS:   ~/Library/Application Support/Bombo/Bombo.settings
//   Windows: %APPDATA%/Bombo/Bombo.settings
//
// Keys grow over time. v1.0: theme.active only. Future:
// unlocks.themes, unlocks.presets, nightrun.last_seed, etc.
//
// THREADING: not thread-safe. Caller must serialize access from the
// message thread (matches ThemeProvider's threading contract).
class PersistentState
{
public:
    // Default constructor: uses the platform user-app-data directory.
    PersistentState();

    // Test constructor: uses an explicit directory (e.g. a temp dir).
    explicit PersistentState(const juce::File& directory);

    ~PersistentState();   // flushes via PropertiesFile::saveIfNeeded

    juce::String getActiveTheme() const;
    void         setActiveTheme(const juce::String& name);

private:
    std::unique_ptr<juce::PropertiesFile> props_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PersistentState)
};

} // namespace bombo
