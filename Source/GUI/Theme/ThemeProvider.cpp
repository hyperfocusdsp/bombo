#include "ThemeProvider.h"

#include "ThemeLoader.h"

#include <BinaryData.h>

namespace bombo
{

ThemeProvider& ThemeProvider::get()
{
    static ThemeProvider instance;
    return instance;
}

const Palette& ThemeProvider::current()
{
    JUCE_ASSERT_MESSAGE_THREAD;
    return get().active_;
}

ThemeProvider::ThemeProvider()
    : active_(bandwPalette()), activeName_("bandw")
{
    registerPalette("bandw", bandwPalette());
}

void ThemeProvider::registerPalette(const std::string& name, const Palette& palette)
{
    JUCE_ASSERT_MESSAGE_THREAD;
    if (registry_.find(name) == registry_.end())
        order_.push_back(name);
    registry_[name] = palette;
}

void ThemeProvider::setActive(const std::string& name)
{
    JUCE_ASSERT_MESSAGE_THREAD;
    if (name == activeName_)
        return;

    auto it = registry_.find(name);
    if (it == registry_.end())
        return;

    active_     = it->second;
    activeName_ = name;
    sendChangeMessage();
}

namespace
{
void registerFromBlob(ThemeProvider& tp,
                      const char* data, int size,
                      const char* expectedName)
{
    if (data == nullptr || size <= 0) return;
    juce::String json (data, static_cast<size_t>(size));
    auto r = ThemeLoader::parse(json);
    if (! r.ok)
    {
        DBG("ThemeLoader failed for " << expectedName << ": " << r.error);
        return;
    }
    tp.registerPalette(r.name.empty() ? expectedName : r.name, r.palette);
}
} // anonymous namespace

void ThemeProvider::loadBundledThemes()
{
    JUCE_ASSERT_MESSAGE_THREAD;

    registerFromBlob(*this, BinaryData::bandw_json,    BinaryData::bandw_jsonSize,    "bandw");
    registerFromBlob(*this, BinaryData::phosphor_json, BinaryData::phosphor_jsonSize, "phosphor");
    registerFromBlob(*this, BinaryData::nightrun_json, BinaryData::nightrun_jsonSize, "nightrun");

    // Refresh active_ from the (now possibly newly-registered) registry so the
    // JSON-loaded BANDW replaces the hard-coded ctor default. No broadcast.
    auto it = registry_.find(activeName_);
    if (it != registry_.end())
        active_ = it->second;
}

} // namespace bombo
