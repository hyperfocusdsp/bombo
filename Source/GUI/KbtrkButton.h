#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "Colours.h"

namespace bombo
{

// Compact square toggle that paints a tiny piano-keyboard icon for KBTRK
// (key tracking). Same dark-graphite / amber-border styling as LoopButton.
class KbtrkButton : public juce::ToggleButton
{
public:
    KbtrkButton() : juce::ToggleButton(juce::String()) {}

    // Right-click opens the A / B / A+B target chooser (wired by FaceplatePanel).
    std::function<void()> onContextMenu;
    // Returns the current KBTRK target index (0=A, 1=B, 2=A+B) for the corner
    // indicator. Null → treated as 0.
    std::function<int()> getTarget;

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && onContextMenu) { onContextMenu(); return; }
        juce::ToggleButton::mouseDown(e);
    }

    void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool) override
    {
        const auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();

        // Shared fin-pill chrome (dark fill, amber border, hover brighten) so
        // KBTRK matches LIM/TAIL exactly.
        bombo::pill::paintBackground(g, r, on, shouldDrawButtonAsHighlighted);

        // Mini piano-keyboard icon: an outline box, 4 white-key dividers, and
        // two black keys.
        g.setColour(bombo::pill::fg(on));

        const float kbW = r.getWidth()  * 0.56f;
        const float kbH = r.getHeight() * 0.40f;
        const float kbX = r.getCentreX() - kbW * 0.5f;
        const float kbY = r.getCentreY() - kbH * 0.5f;
        const juce::Rectangle<float> kb(kbX, kbY, kbW, kbH);

        g.drawRoundedRectangle(kb, 1.0f, 1.0f);

        // White-key dividers: 4 evenly spaced vertical lines.
        for (int i = 1; i <= 4; ++i)
        {
            const float x = kbX + kbW * (static_cast<float>(i) / 5.0f);
            g.drawLine(x, kbY, x, kbY + kbH, 0.8f);
        }

        // Two black keys (filled), sitting in the top ~55% of the keyboard.
        const float bkW = kbW * 0.12f;
        const float bkH = kbH * 0.55f;
        const float bk1 = kbX + kbW * (1.0f / 5.0f) - bkW * 0.5f;
        const float bk2 = kbX + kbW * (3.0f / 5.0f) - bkW * 0.5f;
        g.fillRect(bk1, kbY, bkW, bkH);
        g.fillRect(bk2, kbY, bkW, bkH);

        // Corner indicator of the active target (A / B / + for A+B), shown only
        // when KBTRK is on so the right-click choice is visible at a glance.
        if (on && getTarget)
        {
            const int tgt = getTarget();
            const char* lbl = (tgt == 1) ? "B" : (tgt == 2) ? "+" : "A";
            g.setFont(juce::Font(juce::FontOptions(7.0f)).boldened());
            auto corner = r;
            g.drawText(lbl, corner.removeFromBottom(8.0f).removeFromRight(8.0f),
                       juce::Justification::centred, false);
        }
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KbtrkButton)
};

} // namespace bombo
