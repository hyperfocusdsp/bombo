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
    o.folderName              = "Bombo";
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
    return props_->getValue("theme.active", "bandw");
}

void PersistentState::setActiveTheme(const juce::String& name)
{
    props_->setValue("theme.active", name);
    props_->saveIfNeeded();
}

} // namespace bombo
