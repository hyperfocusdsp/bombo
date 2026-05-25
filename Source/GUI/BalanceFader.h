#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"
#include "Fonts.h"
#include "Theme/ThemedComponent.h"

namespace bombo
{

// Horizontal VOICE A ↔ VOICE B balance fader. Lives in the strip above the
// two voice columns and fills its full layout box so it aligns with them.
// A on the left, B on the right, sliding accent thumb, center detent at 0.5.
// Click/drag to position, double-click → 0.5, wheel to nudge.
//
// Styled as a dark recessed pill to match the BNC/LIM/TAIL pill chrome.
class BalanceFader : public juce::Component, public bombo::ThemedComponent
{
public:
    BalanceFader(juce::AudioProcessorValueTreeState& apvts,
                 const juce::String& paramId)
    {
        slider_.setRange(0.0, 1.0, 0.0);
        slider_.setVisible(false);
        addChildComponent(slider_);
        slider_.onValueChange = [this] { repaint(); };
        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, slider_);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced(1.0f);
        if (b.getWidth() < 16.0f || b.getHeight() < 8.0f) return;

        const float r        = b.getHeight() * 0.5f;
        const bool  hot      = isMouseOver(true);
        const auto  accent   = col::accentAmber();

        // Recessed pill housing.
        g.setColour(juce::Colours::black.withAlpha(0.35f));
        g.fillRoundedRectangle(b.translated(0.0f, 1.0f), r);
        g.setColour(col::knobRubber().darker(0.15f));
        g.fillRoundedRectangle(b, r);
        // Inner shadow line at the top of the groove (recess cue).
        g.setColour(juce::Colours::black.withAlpha(0.30f));
        g.fillRoundedRectangle(b.withHeight(b.getHeight() * 0.5f).reduced(2.0f, 1.0f), r * 0.6f);
        // Rim.
        g.setColour((hot ? accent : col::bone()).withAlpha(hot ? 0.85f : 0.40f));
        g.drawRoundedRectangle(b, r, 1.0f);

        const auto track = trackRect();

        // Center detent tick.
        g.setColour(col::bone().withAlpha(0.28f));
        g.fillRect(track.getCentreX() - 0.5f, b.getY() + 3.0f, 1.0f, b.getHeight() - 6.0f);

        // End labels.
        g.setColour(col::bone().withAlpha(0.92f));
        g.setFont(fonts::value(juce::jlimit(9.0f, 12.0f, b.getHeight() * 0.72f)));
        g.drawText("A", juce::Rectangle<float>(b.getX() + 3.0f, b.getY(),
                                               r, b.getHeight()),
                   juce::Justification::centredLeft);
        g.drawText("B", juce::Rectangle<float>(b.getRight() - r - 3.0f, b.getY(),
                                               r, b.getHeight()),
                   juce::Justification::centredRight);

        // Thumb.
        const float v   = static_cast<float>(slider_.getValue());
        const float th  = b.getHeight() - 6.0f;          // thumb height
        const float tw  = juce::jmax(8.0f, th * 0.62f);  // thumb width
        const float tx  = juce::jmap(v, 0.0f, 1.0f,
                                     track.getX() + tw * 0.5f,
                                     track.getRight() - tw * 0.5f);
        const juce::Rectangle<float> thumb(tx - tw * 0.5f, b.getCentreY() - th * 0.5f, tw, th);
        g.setColour(juce::Colours::black.withAlpha(0.40f));
        g.fillRoundedRectangle(thumb.translated(0.0f, 1.0f), tw * 0.35f);
        g.setGradientFill(juce::ColourGradient(accent.brighter(0.20f), thumb.getX(), thumb.getY(),
                                               accent.darker(0.25f), thumb.getX(), thumb.getBottom(),
                                               false));
        g.fillRoundedRectangle(thumb, tw * 0.35f);
        g.setColour(col::bone().withAlpha(0.55f));
        g.drawRoundedRectangle(thumb, tw * 0.35f, 1.0f);
    }

    void mouseDown(const juce::MouseEvent& e) override { setFromMouse(e); }
    void mouseDrag(const juce::MouseEvent& e) override { setFromMouse(e); }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        slider_.setValue(0.5, juce::sendNotificationSync);
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const float step = (w.deltaY > 0 ? 1.0f : (w.deltaY < 0 ? -1.0f : 0.0f)) * 0.02f;
        if (step == 0.0f) return;
        slider_.setValue(juce::jlimit(0.0f, 1.0f,
                                      static_cast<float>(slider_.getValue()) + step),
                         juce::sendNotificationSync);
    }

private:
    juce::Rectangle<float> trackRect() const
    {
        const auto b = getLocalBounds().toFloat().reduced(1.0f);
        const float r = b.getHeight() * 0.5f;
        // Track inset leaves room for the A/B end labels.
        return b.reduced(r + 6.0f, b.getHeight() * 0.5f - 1.0f);
    }

    void setFromMouse(const juce::MouseEvent& e)
    {
        const auto track = trackRect();
        if (track.getWidth() <= 1.0f) return;
        const float fine = e.mods.isShiftDown() ? 0.35f : 1.0f;
        const float raw  = juce::jmap(e.position.x, track.getX(), track.getRight(), 0.0f, 1.0f);
        const float cur  = static_cast<float>(slider_.getValue());
        const float v    = juce::jlimit(0.0f, 1.0f, cur + (raw - cur) * fine);
        slider_.setValue(v, juce::sendNotificationSync);
    }

    juce::Slider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BalanceFader)
};

} // namespace bombo
