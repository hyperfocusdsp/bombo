#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"
#include "Fonts.h"
#include "Theme/ThemedComponent.h"

namespace bombo
{

// Small rotary knob for VOICE A ↔ VOICE B balance. Lives floating on the
// border between the two columns, vertically centered between knob rows.
// Center detent at 0.5 (both layers at unity). Double-click → 0.5.
//
// Visually a miniature of the standard knob look — same cap gradient and
// indicator wedge style, just at a smaller size with an "A·B" cap label.
class BalanceFader : public juce::Component, public bombo::ThemedComponent
{
public:
    BalanceFader(juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& paramId)
    {
        slider_.setRange(0.0, 1.0, 0.0);
        slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        slider_.setVisible(false);
        addChildComponent(slider_);
        slider_.onValueChange = [this] { repaint(); };
        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, slider_);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        if (diameter < 8.0f) return;

        const auto cx = bounds.getCentreX();
        const auto cy = bounds.getCentreY();
        const float radius = diameter * 0.5f - 2.0f;

        // Drop shadow / recess.
        g.setColour(juce::Colour(0xFF000000).withAlpha(0.45f));
        g.fillEllipse(cx - radius - 1.0f, cy - radius + 1.0f,
                      (radius + 1.0f) * 2.0f, (radius + 1.0f) * 2.0f);

        // Rubber outer.
        g.setColour(col::knobRubber());
        g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

        // Cap core — slightly amber-tinted so the user can spot it as the
        // "global between-A-and-B" control, not a regular column knob.
        const float coreR = radius * 0.72f;
        const auto coreTop = col::knobCap().brighter(0.12f);
        const auto coreBot = col::knobCap().darker(0.22f);
        g.setGradientFill(juce::ColourGradient(coreTop, cx, cy - coreR,
                                               coreBot, cx, cy + coreR, false));
        g.fillEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f);

        const bool hot = isMouseOver(true);
        g.setColour(hot ? col::accentAmber().withAlpha(0.85f)
                        : juce::Colour::fromRGBA(0, 0, 0, 0x55));
        g.drawEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f, 1.0f);

        // Indicator wedge.
        constexpr float kStart = juce::MathConstants<float>::pi * 1.25f;
        constexpr float kEnd   = juce::MathConstants<float>::pi * 2.75f;
        const float v   = static_cast<float>(slider_.getValue());
        const float ang = juce::jmap(v, kStart, kEnd)
                        - juce::MathConstants<float>::halfPi;
        const float ic = std::cos(ang);
        const float is = std::sin(ang);
        const float perpC = -is;
        const float perpS =  ic;
        constexpr float stemW = 2.2f;
        const float stemInR  = coreR;
        const float stemOutR = radius;
        juce::Path stem;
        stem.startNewSubPath(cx + ic * stemInR  + perpC * stemW * 0.5f,
                             cy + is * stemInR  + perpS * stemW * 0.5f);
        stem.lineTo(cx + ic * stemOutR + perpC * stemW * 0.6f,
                    cy + is * stemOutR + perpS * stemW * 0.6f);
        stem.lineTo(cx + ic * stemOutR - perpC * stemW * 0.6f,
                    cy + is * stemOutR - perpS * stemW * 0.6f);
        stem.lineTo(cx + ic * stemInR  - perpC * stemW * 0.5f,
                    cy + is * stemInR  - perpS * stemW * 0.5f);
        stem.closeSubPath();
        g.setColour(col::bone());
        g.fillPath(stem);

        // "A·B" cap label.
        g.setColour(col::bone().withAlpha(0.85f));
        g.setFont(fonts::value(juce::jlimit(7.0f, 9.0f, coreR * 0.55f)));
        g.drawText("A·B",
                   juce::Rectangle<float>(cx - coreR, cy - coreR,
                                          coreR * 2.0f, coreR * 2.0f),
                   juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        dragStartY_ = e.position.y;
        dragStartVal_ = static_cast<float>(slider_.getValue());
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        // Up = more B, down = more A. Standard rotary drag feel.
        const float dy = (dragStartY_ - e.position.y);
        const float speed = e.mods.isShiftDown() ? 0.002f : 0.008f;
        const float v = juce::jlimit(0.0f, 1.0f, dragStartVal_ + dy * speed);
        slider_.setValue(v, juce::sendNotificationSync);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        slider_.setValue(0.5, juce::sendNotificationSync);
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float step = (w.deltaY > 0 ? 1.0f : (w.deltaY < 0 ? -1.0f : 0.0f)) * 0.02f;
        if (step == 0.0f) return;
        const float v = juce::jlimit(0.0f, 1.0f,
                                     static_cast<float>(slider_.getValue()) + step);
        slider_.setValue(v, juce::sendNotificationSync);
    }

private:
    juce::Slider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
    float dragStartY_ = 0.0f;
    float dragStartVal_ = 0.5f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BalanceFader)
};

} // namespace bombo
