#include "ThemeProvider.h"

namespace bombo
{

ThemeProvider& ThemeProvider::get()
{
    static ThemeProvider instance;
    return instance;
}

const Palette& ThemeProvider::current()
{
    return get().active_;
}

ThemeProvider::ThemeProvider()
    : active_(bandwPalette()), activeName_("bandw")
{
    registerPalette("bandw", bandwPalette());
}

void ThemeProvider::registerPalette(const std::string& name, const Palette& palette)
{
    if (registry_.find(name) == registry_.end())
        order_.push_back(name);
    registry_[name] = palette;
}

void ThemeProvider::setActive(const std::string& name)
{
    if (name == activeName_)
        return;

    auto it = registry_.find(name);
    if (it == registry_.end())
        return;

    active_     = it->second;
    activeName_ = name;
    sendChangeMessage();
}

} // namespace bombo
