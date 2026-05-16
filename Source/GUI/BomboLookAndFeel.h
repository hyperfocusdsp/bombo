#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"

namespace bombo
{

// Mil-rice LookAndFeel. Knobs get a layered look — section-color outer
// ring → graphite cap with radial highlight → bone indicator with soft
// shadow. The cap shading sells the depth without needing a Blender
// pipeline (which is parked).
class BomboLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BomboLookAndFeel()
    {
        setColour(juce::Slider::backgroundColourId,        col::graphite);
        setColour(juce::Slider::rotarySliderFillColourId,  col::bone);
        setColour(juce::Slider::rotarySliderOutlineColourId, col::bone);
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
                                                   static_cast<float>(height)).reduced(3.0f);
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto cx = bounds.getCentreX();
        const auto cy = bounds.getCentreY();
        const float radius = diameter * 0.5f;

        // Tint = section colour, passed via the outline colour ID.
        const auto tint = slider.findColour(juce::Slider::rotarySliderOutlineColourId);

        // 1. Drop shadow under the knob.
        g.setColour(juce::Colour(0xFF000000).withAlpha(0.35f));
        g.fillEllipse(cx - radius + 1.0f, cy - radius + 2.0f, diameter, diameter);

        // 2. Outer ring — tint colour with a vertical gradient (highlight
        //    at top, shadow at bottom) so the knob reads as 3D.
        {
            juce::ColourGradient ringGrad(tint.brighter(0.30f),
                                          cx, cy - radius,
                                          tint.darker(0.50f),
                                          cx, cy + radius, false);
            g.setGradientFill(ringGrad);
            g.fillEllipse(cx - radius, cy - radius, diameter, diameter);
        }

        // 3. Tick ring — 17 ticks across the 270° rotary sweep.
        constexpr int kNumTicks = 17;
        const float tickInner = radius * 0.84f;
        const float tickOuter = radius * 0.96f;
        const auto tickC = tint.darker(0.7f);
        for (int i = 0; i < kNumTicks; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(kNumTicks - 1);
            const float a = juce::jmap(t, rotaryStartAngle, rotaryEndAngle);
            const float c = std::cos(a - juce::MathConstants<float>::halfPi);
            const float s = std::sin(a - juce::MathConstants<float>::halfPi);
            const bool major = (i == 0 || i == kNumTicks - 1 || i == kNumTicks / 2);
            g.setColour(major ? col::bone.withAlpha(0.55f) : tickC);
            g.drawLine(cx + c * tickInner, cy + s * tickInner,
                       cx + c * tickOuter, cy + s * tickOuter,
                       major ? 1.3f : 0.7f);
        }

        // 4. Cap recess — slightly smaller circle in ink so the cap reads
        //    seated inside the ring.
        const float recessR = radius * 0.78f;
        g.setColour(col::ink);
        g.fillEllipse(cx - recessR, cy - recessR, recessR * 2.0f, recessR * 2.0f);

        // 5. Cap face — graphite with a radial-ish highlight from top-left
        //    via two stacked ellipses (cheap fake-Phong).
        const float capR = radius * 0.74f;
        {
            juce::ColourGradient capGrad(col::graphiteHi.brighter(0.15f),
                                         cx - capR * 0.4f, cy - capR * 0.5f,
                                         col::ink, cx + capR * 0.5f, cy + capR * 0.5f,
                                         true);
            g.setGradientFill(capGrad);
            g.fillEllipse(cx - capR, cy - capR, capR * 2.0f, capR * 2.0f);
        }
        // Specular crescent — tiny bright sliver at upper-left.
        {
            const float sR = capR * 0.55f;
            juce::ColourGradient specGrad(col::bone.withAlpha(0.20f),
                                          cx - capR * 0.45f, cy - capR * 0.45f,
                                          col::bone.withAlpha(0.0f),
                                          cx - capR * 0.10f, cy - capR * 0.05f, true);
            g.setGradientFill(specGrad);
            g.fillEllipse(cx - capR * 0.7f, cy - capR * 0.7f, sR * 1.8f, sR * 1.4f);
        }

        // 6. Indicator line — short bone stroke with a subtle shadow.
        const float a = juce::jmap(sliderPos, rotaryStartAngle, rotaryEndAngle);
        const float ic = std::cos(a - juce::MathConstants<float>::halfPi);
        const float is = std::sin(a - juce::MathConstants<float>::halfPi);
        const float inR = capR * 0.25f;
        const float outR = capR * 0.92f;
        // Shadow.
        g.setColour(col::ink.withAlpha(0.7f));
        g.drawLine(cx + ic * inR + 1.0f, cy + is * inR + 1.0f,
                   cx + ic * outR + 1.0f, cy + is * outR + 1.0f, 2.6f);
        // Indicator.
        g.setColour(col::bone);
        g.drawLine(cx + ic * inR, cy + is * inR,
                   cx + ic * outR, cy + is * outR, 2.4f);
        // Tip cap dot for definition.
        g.fillEllipse(cx + ic * outR - 1.6f, cy + is * outR - 1.6f, 3.2f, 3.2f);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
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
