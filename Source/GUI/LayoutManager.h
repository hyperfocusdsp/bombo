#pragma once
#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>

namespace bombo
{

/** Contract between FaceplatePanel (provider) and LayoutEditOverlay
    (consumer). Defined here so both can include LayoutManager.h
    without circular dependencies. */
struct LayoutElem
{
    juce::String id;
    juce::Rectangle<int> bounds;
    bool locked = false;
    juce::String type;
};

/** Runtime layout manifest loader. Ported from squelch_pro 2026-05-17.

    Loads a JSON file keyed by stable element ids ("faceplate.macroRow" →
    {x,y,w,h}) and returns bounds for any requested id, falling back to a
    caller-supplied default when the id is missing or the file wasn't found.

    Load priority:
      1. $BOMBO_LAYOUT_JSON (absolute path — dev override)
      2. <source-tree>/Resources/Layout.json (dev build convenience)
      3. nothing — every call returns its fallback

    The fallback-based API means the plugin always builds and runs correctly
    even without a JSON file; JSON only overrides positions when present.
*/
class LayoutManager
{
public:
    LayoutManager();

    void reload();

    juce::Rectangle<int> boundsOr (const juce::String& id,
                                   juce::Rectangle<int> fallback) const;

    void setBounds (const juce::String& id, juce::Rectangle<int> r,
                    const juce::String& type = {});

    bool isLocked (const juce::String& id) const;
    void setLocked (const juce::String& id, bool locked);

    bool save();

    bool isLoaded() const noexcept { return loaded; }
    juce::String getSourcePath() const { return sourcePath; }

private:
    juce::var root;
    juce::String sourcePath;
    bool loaded = false;

    juce::File resolveFile() const;
};

} // namespace bombo
