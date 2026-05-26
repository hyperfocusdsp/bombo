#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Colours.h"

namespace bombo
{

// Compact toggle button that paints a circular-arrow (loop) icon instead of
// text. Same dark-graphite / amber-border pill styling as LIM and TAIL.
class LoopButton : public juce::ToggleButton
{
public:
    LoopButton() : juce::ToggleButton(juce::String()) {}

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool) override
    {
        const auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();

        // Shared fin-pill chrome (dark fill, amber border, hover brighten) so
        // LOOP matches LIM/TAIL exactly.
        bombo::pill::paintBackground(g, r, on, shouldDrawButtonAsHighlighted);

        // Loop-arrow icon: ~300° arc + arrowhead at start, pointing clockwise.
        g.setColour(bombo::pill::fg(on));
        const float cx  = r.getCentreX();
        const float cy  = r.getCentreY();
        const float rad = juce::jmin(r.getWidth(), r.getHeight()) * 0.28f;
        const float kPi = juce::MathConstants<float>::pi;

        // Arc: 300° clockwise, leaving a 60° gap at the top (12 o'clock).
        // In JUCE, angle 0 = 12 o'clock, increases clockwise.
        const float gapA  = kPi * (30.0f / 180.0f);  // 30° in radians
        const float startA = gapA;                      // arc starts at ~1 o'clock
        const float endA   = kPi * 2.0f - gapA;        // arc ends at ~11 o'clock

        juce::Path arc;
        arc.addArc(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f,
                   startA, endA, true);
        g.strokePath(arc, juce::PathStrokeType(1.4f,
                     juce::PathStrokeType::curved,
                     juce::PathStrokeType::rounded));

        // Arrowhead at arc start (1 o'clock), pointing clockwise (downward-right).
        // JUCE angle A → screen position: (cx + rad*sin(A), cy - rad*cos(A))
        // Clockwise tangent direction: (cos(A), sin(A)) in screen space.
        const float px  = cx + rad * std::sin(startA);
        const float py  = cy - rad * std::cos(startA);
        const float tx  = std::cos(startA);   // clockwise tangent x
        const float ty  = std::sin(startA);   // clockwise tangent y
        const float hs  = rad * 0.50f;        // arrowhead arm length
        // Two arms diverging from tip, pointing back along the arc.
        juce::Path head;
        head.startNewSubPath(px - hs * (tx * 0.7f + ty * 0.4f),
                             py - hs * (ty * 0.7f - tx * 0.4f));
        head.lineTo(px, py);
        head.lineTo(px - hs * (tx * 0.7f - ty * 0.4f),
                    py - hs * (ty * 0.7f + tx * 0.4f));
        g.strokePath(head, juce::PathStrokeType(1.4f,
                     juce::PathStrokeType::mitered,
                     juce::PathStrokeType::butt));
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoopButton)
};

} // namespace bombo
