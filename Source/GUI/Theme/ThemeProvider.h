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
//
// THREADING: all methods MUST be called from the JUCE message thread.
// Debug builds assert this via JUCE_ASSERT_MESSAGE_THREAD. There is no
// internal synchronization — callers off the message thread will race.
//
// LIFETIME: this is a process-wide Meyers singleton. In plugin contexts
// where the host instantiates multiple plugin instances in the same
// process, all instances share one ThemeProvider — theme changes apply
// to every loaded Bombo at once. Every listener MUST removeChangeListener
// in its dtor; ThemedComponent (Task 6) will encode this discipline.
class ThemeProvider : public juce::ChangeBroadcaster
{
public:
    // Singleton because Source/GUI/Colours.h's free-function colour
    // accessors (col::graphite(), etc., introduced in Task 2) need a
    // global anchor — they can't take a ThemeProvider parameter.
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

    // Reads the three bundled theme JSONs from BinaryData and registers them.
    // Idempotent — calling multiple times is safe.
    // After loading, active_ is refreshed from the registry so a JSON-loaded
    // BANDW replaces the hard-coded constructor default. No broadcast — value
    // is identical by design (BANDW JSON matches the constructor's hard-coded
    // bandwPalette() byte-for-byte, verified in T4).
    void loadBundledThemes();

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
