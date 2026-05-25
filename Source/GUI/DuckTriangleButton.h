#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"

namespace bombo
{

// Reverse-bass duck toggle for Voice A — a right-triangle wedge that fills the
// corner under the VOICE A column: horizontal top edge (along the rack
// bottom), vertical right edge, and a hypotenuse running top-left -> bottom-
// right along the bomb body's angled side. Filled accent when ON, faint accent
// outline when OFF; brightens on hover.
class DuckTriangleButton : public juce::ToggleButton
{
public:
    DuckTriangleButton() : juce::ToggleButton(juce::String()) {}

    void paintButton(juce::Graphics& g, bool, bool) override
    {
        const auto r   = getLocalBounds().toFloat();
        const bool on  = getToggleState();
        const bool hot = isMouseOverOrDragging();

        juce::Path tri;  // right angle at top-right; hypotenuse = body side
        tri.addTriangle(r.getX(),     r.getY(),        // top-left
                        r.getRight(), r.getY(),        // top-right (right angle)
                        r.getRight(), r.getBottom());  // bottom-right

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
