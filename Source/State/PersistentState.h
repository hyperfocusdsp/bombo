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

    // BBS hidden-terminal state. unlocked = user has triggered the BBS
    // detonator at least once. lastScreen = the screen-state enum value
    // they were on when they last dismissed it, so re-entry lands where
    // they left off. Defaults are: not unlocked, last screen = 0 (login).
    bool getBbsUnlocked() const;
    void setBbsUnlocked(bool unlocked);

    int  getBbsLastScreen() const;
    void setBbsLastScreen(int screenEnum);

    // Progression state — saves counter drives level-up; level gates
    // SYSOP unlocks. unlockedSysops is a comma-separated list of
    // integer indices into SysopContent::kSysops (e.g. "0,1,2").
    int          getBbsSavesCount() const;
    void         setBbsSavesCount(int count);
    int          getBbsLevel() const;
    void         setBbsLevel(int level);
    juce::String getBbsUnlockedSysops() const;   // default: "0,1,2"
    void         setBbsUnlockedSysops(const juce::String& csv);

    // Last directory the user bounced to. Defaults to the platform user
    // music directory (~/Music on Linux+macOS, %USERPROFILE%/Music on
    // Windows). Updated on every successful bounce so the next FileChooser
    // opens where they left off. Returns the default if the persisted
    // path no longer exists (drive unmounted, folder deleted, etc.).
    juce::File getLastBounceDir() const;
    void       setLastBounceDir(const juce::File& dir);

private:
    std::unique_ptr<juce::PropertiesFile> props_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PersistentState)
};

} // namespace bombo
