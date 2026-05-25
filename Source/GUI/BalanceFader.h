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
        const auto r = getLocalBounds().toFloat();
        if (r.getWidth() < 16.0f || r.getHeight() < 6.0f) return;

        const bool  hot    = isMouseOver(true);
        const auto  accent = col::accentAmber();
        constexpr float kRad = 4.0f;  // match the Voice B env pill (SynthToggle)

        // Housing — identical pill chrome to the env pill: graphite fill +
        // amber rounded-rect border, 4px corners.
        g.setColour(col::graphite().withAlpha(0.88f));
        g.fillRoundedRectangle(r, kRad);
        g.setColour(accent.withAlpha(hot ? 0.85f : 0.50f));
        g.drawRoundedRectangle(r.reduced(0.5f), kRad, hot ? 1.5f : 1.0f);

        const auto track = trackRect();

        // Center detent tick.
        g.setColour(col::bone().withAlpha(0.28f));
        g.fillRect(track.getCentreX() - 0.5f, r.getY() + 3.0f, 1.0f,
                   juce::jmax(2.0f, r.getHeight() - 6.0f));

        // Thumb.
        const float v   = static_cast<float>(slider_.getValue());
        const float th  = juce::jmax(4.0f, r.getHeight() - 5.0f);
        const float tw  = juce::jlimit(5.0f, 9.0f, r.getHeight() * 0.6f);
        const float tx  = juce::jmap(v, 0.0f, 1.0f,
                                     track.getX() + tw * 0.5f,
                                     track.getRight() - tw * 0.5f);
        const juce::Rectangle<float> thumb(tx - tw * 0.5f, r.getCentreY() - th * 0.5f, tw, th);
        g.setColour(juce::Colours::black.withAlpha(0.40f));
        g.fillRoundedRectangle(thumb.translated(0.0f, 1.0f), 2.5f);
        g.setGradientFill(juce::ColourGradient(accent.brighter(0.20f), thumb.getX(), thumb.getY(),
                                               accent.darker(0.25f), thumb.getX(), thumb.getBottom(),
                                               false));
        g.fillRoundedRectangle(thumb, 2.5f);
        g.setColour(col::bone().withAlpha(0.55f));
        g.drawRoundedRectangle(thumb, 2.5f, 1.0f);
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
        const auto r = getLocalBounds().toFloat();
        // Inset enough to keep the thumb travel inside the rounded corners.
        return r.reduced(6.0f, r.getHeight() * 0.5f - 1.0f);
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
