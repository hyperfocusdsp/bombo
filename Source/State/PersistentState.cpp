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
    // VAULT is the designed-for-R4B-CLASSIC default (Phase 2e, 2026-05-17).
    // Existing installs that previously persisted "bandw" stay on bandw —
    // this default only fires when no theme.active key exists yet.
    return props_->getValue("theme.active", "vault");
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

} // namespace bombo
