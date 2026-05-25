#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"

namespace bombo
{

// Small downward triangle toggle for the reverse-bass duck on Voice A
// ("pull down" = duck). Lives at the bottom of the VOICE A column. Filled
// accent when ON, faint accent outline when OFF; brightens on hover.
class DuckTriangleButton : public juce::ToggleButton
{
public:
    DuckTriangleButton() : juce::ToggleButton(juce::String()) {}

    void paintButton(juce::Graphics& g, bool, bool) override
    {
        const auto r   = getLocalBounds().toFloat().reduced(1.0f);
        const bool on  = getToggleState();
        const bool hot = isMouseOverOrDragging();

        juce::Path tri;  // apex points down
        tri.addTriangle(r.getX(),        r.getY(),
                        r.getRight(),    r.getY(),
                        r.getCentreX(),  r.getBottom());

        const auto accent = col::accentAmber();
        if (on)
        {
            g.setColour(accent.withAlpha(hot ? 1.0f : 0.92f));
            g.fillPath(tri);
        }
        else
        {
            g.setColour(accent.withAlpha(hot ? 0.75f : 0.45f));
            g.strokePath(tri, juce::PathStrokeType(1.2f));
        }
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuckTriangleButton)
};

} // namespace bombo
