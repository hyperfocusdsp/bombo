#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"
#include "Fonts.h"

namespace bombo
{

// Bombo knob. Section colour lives in the column body — the knob cap is
// dark plastic with a bone indicator + bone value text. Hosts that want a
// hero tint (e.g. the OUT macro) override `rotarySliderOutlineColourId`
// to amber; everyone else uses the default `col::knobCap`.
//
// Two-line value layout (number above, unit below) is preserved from the
// pre-port UI: `slider.getTextFromValue` is split on the first space so
// the param's `stringFromValueFunction` does the formatting.
class BomboLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BomboLookAndFeel()
    {
        setColour(juce::Slider::backgroundColourId,        col::graphite);
        setColour(juce::Slider::rotarySliderFillColourId,  col::bone);
        // Default cap = dark plastic. Per-knob override is supported (the
        // OUT macro sets this to col::accentAmber).
        setColour(juce::Slider::rotarySliderOutlineColourId, col::knobCap);
        setColour(juce::Slider::thumbColourId,             col::bone);
        setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0));
        setColour(juce::Slider::textBoxTextColourId,       col::boneDim);
        setColour(juce::Label::textColourId,               col::bone);
        setColour(juce::ComboBox::backgroundColourId,      col::ink);
        setColour(juce::ComboBox::textColourId,            col::bone);
        setColour(juce::ComboBox::outlineColourId,         col::graphiteHi);
        setColour(juce::ComboBox::arrowColourId,           col::boneDim);
        setColour(juce::PopupMenu::backgroundColourId,     col::graphiteHi);
        setColour(juce::PopupMenu::textColourId,           col::bone);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, col::accentAmber);
        setColour(juce::PopupMenu::highlightedTextColourId, col::ink);
        setColour(juce::ToggleButton::textColourId,        col::bone);
        setColour(juce::ToggleButton::tickColourId,        col::accentAmber);
        setColour(juce::ToggleButton::tickDisabledColourId, col::boneDim);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                                   static_cast<float>(y),
                                                   static_cast<float>(width),
                                                   static_cast<float>(height));
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto cx = bounds.getCentreX();
        const auto cy = bounds.getCentreY();
        const float radius = diameter * 0.5f - 4.0f;
        if (radius < 6.0f) return;

        // Cap colour: dark by default, amber for the OUT macro.
        const auto core = slider.findColour(juce::Slider::rotarySliderOutlineColourId);
        const bool capIsDark = core.getPerceivedBrightness() < 0.5f;
        const auto indicatorColour = capIsDark ? col::bone : col::ink;
        const auto valueColour     = capIsDark ? col::bone : col::ink;

        // 1. Mounting recess (offset drop shadow).
        g.setColour(juce::Colour(0xFF000000).withAlpha(0.35f));
        g.fillEllipse(cx - radius - 1.0f, cy - radius + 1.5f,
                      (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f);
        g.setColour(juce::Colour(0xFF050507));
        g.fillEllipse(cx - radius - 2.0f, cy - radius - 2.0f,
                      (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f);

        // 2. Rubber grip — dark with subtle top highlight.
        g.setColour(col::knobRubber);
        g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(juce::Colour(0xFF24'24'26u));
        const float hlR = radius * 0.95f;
        g.fillEllipse(cx - hlR, cy - radius - radius * 0.05f, hlR * 2.0f, hlR * 2.0f);
        g.setColour(col::knobRubber);
        g.fillEllipse(cx - radius * 0.88f, cy - radius * 0.88f,
                      radius * 0.88f * 2.0f, radius * 0.88f * 2.0f);

        // 3. Metal bevel ring between rubber and the cap core.
        const float coreOuterR = radius * 0.80f;
        g.setColour(col::knobBevel);
        g.fillEllipse(cx - coreOuterR - 1.5f, cy - coreOuterR - 1.5f,
                      (coreOuterR + 1.5f) * 2.0f, (coreOuterR + 1.5f) * 2.0f);

        // 4. Cap core. Subtle top-down gradient sells the moulded-plastic feel.
        const float coreR = coreOuterR - 1.0f;
        const auto coreTop = core.brighter(0.10f);
        const auto coreBot = core.darker (0.18f);
        g.setGradientFill(juce::ColourGradient(coreTop, cx, cy - coreR,
                                               coreBot, cx, cy + coreR, false));
        g.fillEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f);

        // 5. Inner shadow at cap edge for recessed look.
        g.setColour(juce::Colour::fromRGBA(0, 0, 0, 0x55));
        g.drawEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f, 1.0f);

        // 6. Indicator stem — wedge from cap edge out to the rubber outer.
        const float a = juce::jmap(sliderPos, rotaryStartAngle, rotaryEndAngle);
        const float ang = a - juce::MathConstants<float>::halfPi;
        const float ic = std::cos(ang);
        const float is = std::sin(ang);
        const float stemInR  = coreR;
        const float stemOutR = radius;
        const float stemW    = 2.6f;
        juce::Path stem;
        const float perpC = -is;
        const float perpS =  ic;
        stem.startNewSubPath(cx + ic * stemInR  + perpC * stemW * 0.5f,
                             cy + is * stemInR  + perpS * stemW * 0.5f);
        stem.lineTo(cx + ic * stemOutR + perpC * stemW * 0.6f,
                    cy + is * stemOutR + perpS * stemW * 0.6f);
        stem.lineTo(cx + ic * stemOutR - perpC * stemW * 0.6f,
                    cy + is * stemOutR - perpS * stemW * 0.6f);
        stem.lineTo(cx + ic * stemInR  - perpC * stemW * 0.5f,
                    cy + is * stemInR  - perpS * stemW * 0.5f);
        stem.closeSubPath();
        g.setColour(indicatorColour);
        g.fillPath(stem);

        // 7. Tick dots — 11 uniform markers on the rubber outer edge.
        const float dotR = juce::jmax(0.9f, radius * 0.045f);
        const float dotRingR = radius + 2.5f;
        for (int i = 0; i <= 10; ++i)
        {
            const float t = static_cast<float>(i) / 10.0f;
            const float ta = juce::jmap(t, rotaryStartAngle, rotaryEndAngle)
                           - juce::MathConstants<float>::halfPi;
            const float dx = cx + std::cos(ta) * dotRingR;
            const float dy = cy + std::sin(ta) * dotRingR;
            g.setColour(col::bone.withAlpha(0.60f));
            g.fillEllipse(dx - dotR, dy - dotR, dotR * 2.0f, dotR * 2.0f);
        }

        // 8. Value text inside the cap. Two-line layout when the formatter
        //    returns "<number> <unit>"; single-line otherwise.
        const auto text = slider.getTextFromValue(slider.getValue());
        const float capInner = coreR * 0.95f;
        const int spaceIdx = text.indexOfChar(' ');
        const float valueFontSize = juce::jlimit(8.0f, 13.0f, capInner * 0.62f);
        g.setColour(valueColour.withAlpha(0.92f));
        if (spaceIdx > 0)
        {
            const auto num  = text.substring(0, spaceIdx);
            const auto unit = text.substring(spaceIdx + 1);
            const float unitFontSize = juce::jmax(6.5f, valueFontSize * 0.72f);
            g.setFont(fonts::value(valueFontSize));
            g.drawText(num,
                       juce::Rectangle<float>(cx - capInner, cy - capInner,
                                              capInner * 2.0f, capInner * 1.3f),
                       juce::Justification::centredBottom);
            g.setFont(fonts::value(unitFontSize));
            g.drawText(unit,
                       juce::Rectangle<float>(cx - capInner, cy - capInner * 0.05f,
                                              capInner * 2.0f, capInner * 1.0f),
                       juce::Justification::centredTop);
        }
        else
        {
            g.setFont(fonts::value(valueFontSize));
            g.drawText(text,
                       juce::Rectangle<float>(cx - capInner, cy - capInner,
                                              capInner * 2.0f, capInner * 2.0f),
                       juce::Justification::centred);
        }
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));
        if (!label.isBeingEdited())
        {
            g.setColour(label.findColour(juce::Label::textColourId));
            g.setFont(label.getFont());
            g.drawFittedText(label.getText(), label.getLocalBounds(),
                             label.getJustificationType(), 1, 0.9f);
        }
    }

    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboLookAndFeel)
};

} // namespace bombo
