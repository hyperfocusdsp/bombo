#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"

namespace bombo
{

// Mil-rice LookAndFeel — Bombo's chassis aesthetic. Rotary sliders draw
// a graphite recess + section-color tick ring + bone indicator line.
// Labels use the LookAndFeel-cached fonts so any control in the editor
// picks up the palette without per-component plumbing.
class BomboLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BomboLookAndFeel()
    {
        setColour(juce::Slider::backgroundColourId,        col::graphite);
        setColour(juce::Slider::rotarySliderFillColourId,  col::bone);
        setColour(juce::Slider::rotarySliderOutlineColourId, col::ink);
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
        setColour(juce::PopupMenu::highlightedBackgroundColourId, col::voice);
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
                                                   static_cast<float>(height)).reduced(2.0f);
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto cx = bounds.getCentreX();
        const auto cy = bounds.getCentreY();
        const float radius = diameter * 0.5f;

        // Section colour — passed via the slider's custom outline colour
        // (one per FX column). Falls back to bone if none set.
        const auto tint = slider.findColour(juce::Slider::rotarySliderOutlineColourId);

        // Outer recess ring.
        g.setColour(col::ink);
        g.fillEllipse(cx - radius, cy - radius, diameter, diameter);

        // Tick ring — 24 ticks every 15°, brighter ones at min/mid/max.
        constexpr int kNumTicks = 24;
        const float tickInner = radius * 0.86f;
        const float tickOuter = radius * 0.98f;
        for (int i = 0; i < kNumTicks; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kNumTicks - 1);
            const float a = juce::jmap(t, rotaryStartAngle, rotaryEndAngle);
            const float c = std::cos(a - juce::MathConstants<float>::halfPi);
            const float s = std::sin(a - juce::MathConstants<float>::halfPi);
            const bool major = (i == 0 || i == kNumTicks - 1 || i == kNumTicks / 2);
            g.setColour(major ? tint : tint.withAlpha(0.35f));
            g.drawLine(cx + c * tickInner, cy + s * tickInner,
                       cx + c * tickOuter, cy + s * tickOuter,
                       major ? 1.5f : 0.8f);
        }

        // Knob cap — flat graphite disc with subtle inner highlight.
        const float capRadius = radius * 0.78f;
        g.setColour(col::graphiteHi);
        g.fillEllipse(cx - capRadius, cy - capRadius, capRadius * 2.0f, capRadius * 2.0f);
        g.setColour(col::ink.withAlpha(0.45f));
        g.drawEllipse(cx - capRadius, cy - capRadius, capRadius * 2.0f, capRadius * 2.0f, 1.0f);

        // Indicator line — short bone stroke from inner cap to outer edge.
        const float a = juce::jmap(sliderPos, rotaryStartAngle, rotaryEndAngle);
        const float ic = std::cos(a - juce::MathConstants<float>::halfPi);
        const float is = std::sin(a - juce::MathConstants<float>::halfPi);
        const float inR = capRadius * 0.20f;
        const float outR = capRadius * 0.94f;
        g.setColour(col::bone);
        g.drawLine(cx + ic * inR, cy + is * inR,
                   cx + ic * outR, cy + is * outR, 2.0f);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        // Labels render in slim bone — kept terse so the chassis breathes.
        g.fillAll(label.findColour(juce::Label::backgroundColourId));
        if (!label.isBeingEdited())
        {
            const auto colour = label.findColour(juce::Label::textColourId);
            g.setColour(colour);
            g.setFont(label.getFont());
            g.drawFittedText(label.getText(),
                             label.getLocalBounds(),
                             label.getJustificationType(),
                             1, 0.9f);
        }
    }

    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboLookAndFeel)
};

} // namespace bombo
