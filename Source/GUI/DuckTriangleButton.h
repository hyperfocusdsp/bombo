#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>

#include "Colours.h"
#include "Fonts.h"

namespace bombo
{

// Reverse-bass duck toggle for Voice A — a wedge tucked into the bomb body's
// angled left side under the VOICE A column. Horizontal top edge, vertical
// right edge, and a left edge that hugs the body silhouette: FaceplatePanel
// feeds it the body edge as a local-space polyline (setLeftEdge) so the wedge
// stays parallel to — and a couple px inside — the real curved outline.
// "DUCK" reads down that angled edge. Filled accent when ON, faint outline
// when OFF; brightens on hover.
class DuckTriangleButton : public juce::ToggleButton
{
public:
    DuckTriangleButton() : juce::ToggleButton(juce::String()) {}

    // Explicit triangle in LOCAL coords. tl = top-left (on body edge),
    // tr = top-right (flat top), apex = bottom point (on body edge, lower).
    // tl -> apex is the angled edge that parallels the bomb body side; "DUCK"
    // reads down it. Call after setBounds().
    void setTriangle(juce::Point<float> tl, juce::Point<float> tr, juce::Point<float> apex)
    {
        tl_ = tl; tr_ = tr; apex_ = apex; haveTri_ = true;
        repaint();
    }

    void paintButton(juce::Graphics& g, bool, bool) override
    {
        const auto r   = getLocalBounds().toFloat();
        const bool on  = getToggleState();
        const bool hot = isMouseOverOrDragging();

        const juce::Point<float> tl   = haveTri_ ? tl_   : r.getTopLeft();
        const juce::Point<float> tr   = haveTri_ ? tr_   : juce::Point<float>(r.getRight(), r.getY());
        const juce::Point<float> apex = haveTri_ ? apex_ : juce::Point<float>(r.getRight(), r.getBottom());

        juce::Path tri;
        tri.addTriangle(tl, tr, apex);
        tri = tri.createPathWithRoundedCorners(3.5f);  // rounded corners like the pills

        // Match how the DUCK rack column reads in each theme. Classic/VAULT
        // give every section a distinct body fill, so use col::duck() (the tan
        // DUCK fill). Neon themes fill ALL sections with the same near-black and
        // distinguish them with the neon accent border — using col::duck() there
        // makes the wedge body-coloured (invisible), so use the neon accent.
        const auto accent = col::isNeon() ? col::accentAmber() : col::duck();
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

        // "DUCK" centered along the triangle's LONGEST edge (tr -> apex, the
        // diagonal hypotenuse), at the same size as the rack knob labels.
        const float dx  = apex.x - tr.x;
        const float dy  = apex.y - tr.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len > 20.0f)
        {
            float ang = std::atan2(dy, dx);
            if (std::cos(ang) < 0.0f)                      // keep the text upright
                ang += juce::MathConstants<float>::pi;

            const juce::Point<float> mid = (tr + apex) * 0.5f;
            // Inset toward the interior (third vertex tl) so the glyphs sit on
            // the wedge fill rather than straddling the edge line.
            juce::Point<float> nIn(dy / len, -dx / len);
            if ((tl - mid).getDotProduct(nIn) < 0.0f) nIn = -nIn;

            constexpr float fh = 10.0f;                    // == rack knob label size
            const juce::Point<float> c = mid + nIn * (fh * 0.70f);
            const float w = len * 0.9f, h = fh * 1.4f;

            g.saveState();
            g.addTransform(juce::AffineTransform::rotation(ang, c.x, c.y));
            g.setFont(fonts::label(fh));                   // match the rack column labels
            g.setColour(on ? juce::Colours::black.withAlpha(0.85f)
                           : accent.withAlpha(hot ? 0.95f : 0.65f));
            g.drawText("DUCK",
                       juce::Rectangle<float>(c.x - w * 0.5f, c.y - h * 0.5f, w, h),
                       juce::Justification::centred, false);
            g.restoreState();
        }
    }

private:
    juce::Point<float> tl_, tr_, apex_;
    bool haveTri_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuckTriangleButton)
};

} // namespace bombo
