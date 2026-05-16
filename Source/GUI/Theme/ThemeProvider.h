#pragma once

#include "Palette.h"

#include <juce_events/juce_events.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace bombo
{

// Singleton owning the active palette + a registry of named palettes.
// Listeners (LookAndFeels, components) repaint when the active theme changes.
class ThemeProvider : public juce::ChangeBroadcaster
{
public:
    static ThemeProvider& get();

    // Read-only accessor used by Colours.h accessor functions.
    static const Palette& current();

    // Returns the name of the currently active theme.
    const std::string& activeName() const { return activeName_; }

    // Register a named palette. Idempotent overwrite.
    void registerPalette(const std::string& name, const Palette& palette);

    // Activate a registered palette by name. No-op if name unknown or unchanged.
    // Broadcasts on actual change.
    void setActive(const std::string& name);

    // All registered theme names, in insertion order.
    const std::vector<std::string>& registeredNames() const { return order_; }

private:
    ThemeProvider();
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeProvider)

    Palette active_;
    std::string activeName_;
    std::unordered_map<std::string, Palette> registry_;
    std::vector<std::string> order_;
};

} // namespace bombo
