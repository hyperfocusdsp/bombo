#include "PersistentState.h"

namespace bombo
{

namespace
{
juce::PropertiesFile::Options defaultOptions()
{
    juce::PropertiesFile::Options o;
    o.applicationName         = "Bombo";
    o.filenameSuffix          = ".settings";
    // JUCE's Linux PropertiesFile path logic uses `~` (not `~/.config`)
    // as the base dir, so folderName must include `.config/` explicitly
    // to follow XDG. On macOS/Windows JUCE roots in the platform's
    // user-app-data dir anyway, so a leading `.config/` is harmless
    // there (it just adds an inner folder). Keep "Bombo" on those
    // platforms via the conditional.
   #if JUCE_LINUX || JUCE_BSD || JUCE_ANDROID
    o.folderName              = ".config/Bombo";
   #else
    o.folderName              = "Bombo";
   #endif
    o.osxLibrarySubFolder     = "Application Support";
    o.storageFormat           = juce::PropertiesFile::storeAsXML;
    o.commonToAllUsers        = false;
    o.doNotSave               = false;
    o.millisecondsBeforeSaving = 1000;
    return o;
}
} // anonymous namespace

PersistentState::PersistentState()
    : props_(std::make_unique<juce::PropertiesFile>(defaultOptions())) {}

PersistentState::PersistentState(const juce::File& directory)
{
    auto file = directory.getChildFile("Bombo.settings");
    props_ = std::make_unique<juce::PropertiesFile>(file, defaultOptions());
}

PersistentState::~PersistentState()
{
    if (props_) props_->saveIfNeeded();
}

juce::String PersistentState::getActiveTheme() const
{
    // FALLOUT is the v1.0 hero theme — first-run installs open on it. This
    // default only fires when no theme.active key exists yet; once the user
    // picks any theme from the tile strip it's persisted and restored verbatim
    // on every later launch (setActiveTheme below), so their choice always wins.
    return props_->getValue("theme.active", "fallout");
}

void PersistentState::setActiveTheme(const juce::String& name)
{
    props_->setValue("theme.active", name);
    props_->saveIfNeeded();
}

bool PersistentState::getBbsUnlocked() const
{
    return props_->getBoolValue("bbs.unlocked", false);
}

void PersistentState::setBbsUnlocked(bool unlocked)
{
    props_->setValue("bbs.unlocked", unlocked);
    props_->saveIfNeeded();
}

int PersistentState::getBbsLastScreen() const
{
    return props_->getIntValue("bbs.lastScreen", 0);
}

void PersistentState::setBbsLastScreen(int screenEnum)
{
    props_->setValue("bbs.lastScreen", screenEnum);
    props_->saveIfNeeded();
}

int PersistentState::getBbsSavesCount() const
{
    return props_->getIntValue("bbs.saves_count", 0);
}

void PersistentState::setBbsSavesCount(int count)
{
    props_->setValue("bbs.saves_count", count);
    props_->saveIfNeeded();
}

int PersistentState::getBbsLevel() const
{
    return props_->getIntValue("bbs.level", 0);
}

void PersistentState::setBbsLevel(int level)
{
    props_->setValue("bbs.level", level);
    props_->saveIfNeeded();
}

juce::String PersistentState::getBbsUnlockedSysops() const
{
    return props_->getValue("bbs.unlocked_sysops", "0,1,2");
}

void PersistentState::setBbsUnlockedSysops(const juce::String& csv)
{
    props_->setValue("bbs.unlocked_sysops", csv);
    props_->saveIfNeeded();
}

juce::File PersistentState::getLastBounceDir() const
{
    // Prefer userMusicDirectory; fall back to home if Music doesn't exist
    // (headless WSL / minimal Linux installs often lack ~/Music).
    auto def = juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    if (! def.isDirectory())
        def = juce::File::getSpecialLocation(juce::File::userHomeDirectory);

    const auto stored = props_->getValue("bounce.lastDir", juce::String());
    if (stored.isEmpty()) return def;
    const juce::File f(stored);
    return (f.isDirectory() ? f : def);
}

void PersistentState::setLastBounceDir(const juce::File& dir)
{
    props_->setValue("bounce.lastDir", dir.getFullPathName());
    props_->saveIfNeeded();
}

bool PersistentState::getGameMusicEnabled() const
{
    return props_->getBoolValue("game.musicEnabled", true);   // default ON (gated to active gameplay)
}

void PersistentState::setGameMusicEnabled(bool on)
{
    props_->setValue("game.musicEnabled", on);
    props_->saveIfNeeded();
}

} // namespace bombo
