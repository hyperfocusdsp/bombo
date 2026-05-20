#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "Colours.h"
#include "Theme/ThemedComponent.h"

namespace bombo
{

// The DICE — one-click full randomize. Click fires onClick (wired to
// BomboProcessor::randomizeBombo); each click also rotates the visible
// face to a random 1–6 so the user gets visual feedback.
//
// Tooltip carries the playful "may cause undesirable sonic characteristics"
// warning + an IG share CTA. Hover background tint goes amber.
class DiceButton : public juce::Component,
                   public juce::SettableTooltipClient,
                   public bombo::ThemedComponent
{
public:
    std::function<void()> onClick;

    DiceButton()
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip(
            "DICE > WARNING: this may cause undesirable sonic "
            "characteristics. Please adjust your output levels accordingly.\n\n"
            "Share your sound on IG and tag @hyperfocusdsp #hyperbombo");
        // Initial face is "random-looking" (6 dots) so it reads as a die at
        // a glance before any click.
        face_ = 6;
    }

    void paint(juce::Graphics& g) override
    {
        // Die is always square — use the smaller axis, centred in component.
        const auto full   = getLocalBounds().toFloat();
        const float side  = juce::jmin(full.getWidth(), full.getHeight()) - 3.0f;
        const auto bounds = juce::Rectangle<float>(
            full.getCentreX() - side * 0.5f, full.getCentreY() - side * 0.5f,
            side, side);
        const bool hot = isMouseOver(true);

        // Body — rounded square. Amber when hovered, dark graphite at rest
        // (action button: bone border, not amber, to distinguish from toggles).
        g.setColour(hot ? col::accentAmber() : col::graphite().withAlpha(0.88f));
        g.fillRoundedRectangle(bounds, 3.5f);
        g.setColour(hot ? col::ink() : col::boneDim().withAlpha(0.45f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 3.5f, 1.0f);

        // Pips — die face pattern. Grid spacing 22% so pips never touch walls.
        const float cx = bounds.getCentreX();
        const float cy = bounds.getCentreY();
        const float gx = side * 0.22f;
        const float gy = side * 0.22f;
        const float pipR = juce::jmax(1.0f, side * 0.065f);

        auto drawPip = [&](float x, float y)
        {
            g.fillEllipse(x - pipR, y - pipR, pipR * 2.0f, pipR * 2.0f);
        };

        g.setColour(hot ? col::ink() : col::bone().withAlpha(0.92f));
        switch (face_)
        {
            case 1:
                drawPip(cx, cy);
                break;
            case 2:
                drawPip(cx - gx, cy - gy);
                drawPip(cx + gx, cy + gy);
                break;
            case 3:
                drawPip(cx - gx, cy - gy);
                drawPip(cx,      cy);
                drawPip(cx + gx, cy + gy);
                break;
            case 4:
                drawPip(cx - gx, cy - gy);
                drawPip(cx + gx, cy - gy);
                drawPip(cx - gx, cy + gy);
                drawPip(cx + gx, cy + gy);
                break;
            case 5:
                drawPip(cx - gx, cy - gy);
                drawPip(cx + gx, cy - gy);
                drawPip(cx,      cy);
                drawPip(cx - gx, cy + gy);
                drawPip(cx + gx, cy + gy);
                break;
            case 6:
            default:
                drawPip(cx - gx, cy - gy);
                drawPip(cx + gx, cy - gy);
                drawPip(cx - gx, cy);
                drawPip(cx + gx, cy);
                drawPip(cx - gx, cy + gy);
                drawPip(cx + gx, cy + gy);
                break;
        }
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    void mouseDown(const juce::MouseEvent&) override
    {
        // Roll the face — pick a new value different from the current one
        // so users always see "something happened" on click.
        int next = face_;
        while (next == face_) next = 1 + rng_.nextInt(6);
        face_ = next;
        repaint();
        if (onClick) onClick();
    }

private:
    juce::Random rng_;
    int face_ = 6;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DiceButton)
};

} // namespace bombo
