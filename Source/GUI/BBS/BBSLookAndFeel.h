#pragma once
#include "../BomboLookAndFeel.h"
#include "../Fonts.h"

namespace bombo
{

// TUI-aesthetic LookAndFeel for use inside the BBS overlay only.
// Extends BomboLookAndFeel (knob renderer) with monospace popup menus
// and box-drawing text editor borders.
class BBSLookAndFeel : public BomboLookAndFeel
{
public:
    BBSLookAndFeel()
    {
        // Override popup menu colours with BBS phosphor palette.
        setColour(juce::PopupMenu::backgroundColourId,
                  juce::Colour(0xFF0A0A0Au));
        setColour(juce::PopupMenu::textColourId,
                  juce::Colour(0xFFC8FF8Cu));
        setColour(juce::PopupMenu::highlightedBackgroundColourId,
                  juce::Colour(0xFF1A3A1Au));
        setColour(juce::PopupMenu::highlightedTextColourId,
                  juce::Colour(0xFFFFFFFFu));
        setColour(juce::PopupMenu::headerTextColourId,
                  juce::Colour(0xFFFFE066u));
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font(juce::FontOptions("Courier New", 12.0f, juce::Font::plain));
    }

    void drawPopupMenuBackground(juce::Graphics& g, int w, int h) override
    {
        g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
        g.setColour(juce::Colour(0xFFC8FF8Cu).withAlpha(0.4f));
        g.drawRect(0, 0, w, h, 1);
    }

    void drawPopupMenuItem(juce::Graphics& g,
                           const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive,
                           bool isHighlighted, bool /*isTicked*/,
                           bool /*hasSubMenu*/,
                           const juce::String& text,
                           const juce::String& /*shortcutKeyText*/,
                           const juce::Drawable* /*icon*/,
                           const juce::Colour* /*textColour*/) override
    {
        if (isSeparator)
        {
            g.setColour(juce::Colour(0xFF333333u));
            g.fillRect(area.getX() + 4, area.getCentreY(), area.getWidth() - 8, 1);
            return;
        }
        if (isHighlighted && isActive)
            g.fillAll(findColour(juce::PopupMenu::highlightedBackgroundColourId));

        const auto textColour = (! isActive)
            ? juce::Colour(0xFF444444u)
            : isHighlighted
                ? findColour(juce::PopupMenu::highlightedTextColourId)
                : findColour(juce::PopupMenu::textColourId);

        g.setColour(textColour);
        g.setFont(getPopupMenuFont());
        g.drawText((isHighlighted ? juce::String("> ") : juce::String("  ")) + text,
                   area.reduced(4, 0),
                   juce::Justification::centredLeft);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BBSLookAndFeel)
};

} // namespace bombo
