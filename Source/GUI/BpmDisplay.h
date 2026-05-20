#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

#include "Colours.h"
#include "Fonts.h"
#include "Theme/ThemedComponent.h"

namespace bombo
{

// Small "BPM 120" pill that doubles as a click-drag editor.
//
// When the host reports a BPM (DAW context), we poll it via the
// hostBpmFn callback and display that value — drag is disabled, the
// readout shows the host's tempo so user doesn't think Bombo is out
// of sync. When the host returns 0 (standalone, or DAW that doesn't
// expose BPM), the SliderAttachment-bound value is shown and drag
// edits the param.
class BpmDisplay : public juce::Component,
                   public bombo::ThemedComponent,
                   private juce::Timer
{
public:
    BpmDisplay(juce::AudioProcessorValueTreeState& apvts,
               const juce::String& paramId,
               std::function<float()> hostBpmFn)
        : hostBpmFn_(std::move(hostBpmFn))
    {
        slider_.setRange(60.0, 300.0, 1.0);
        slider_.setSliderStyle(juce::Slider::LinearBarVertical);
        // Hidden — we forward mouse events to it ourselves and paint the
        // value with our own pill drawing. Track value changes so the
        // pill repaints when the param (or the SliderAttachment) updates.
        slider_.setVisible(false);
        addChildComponent(slider_);
        slider_.onValueChange = [this] { repaint(); };
        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, slider_);

        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        startTimerHz(10); // poll host BPM
    }

    void paint(juce::Graphics& g) override
    {
        const auto r = getLocalBounds().toFloat();
        // Same corner radius as the pill buttons in BomboLookAndFeel.
        constexpr float kR = 4.0f;
        const bool hostLocked = hostBpmDisplayed_ > 0.5f;
        const bool on = hostLocked;

        // Background + border — mirrors BomboLookAndFeel pill style.
        g.setColour(on ? col::accentAmber().withAlpha(0.40f)
                       : col::graphite().withAlpha(0.88f));
        g.fillRoundedRectangle(r, kR);
        // Amber border always (dim when unlocked) — same as LoopButton / toggle pills.
        g.setColour(on ? col::accentAmber()
                       : col::accentAmber().withAlpha(0.50f));
        g.drawRoundedRectangle(r.reduced(0.5f), kR, 1.0f);

        // "149 BPM" centred — same monospaced font as the other fin pills.
        const int displayed = static_cast<int>(std::round(
            hostLocked ? hostBpmDisplayed_ : static_cast<float>(slider_.getValue())));
        g.setColour(hostLocked ? col::accentAmber() : col::bone());
        g.setFont(fonts::value(9.0f));
        g.drawText(juce::String(displayed) + " BPM", r,
                   juce::Justification::centred, false);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (hostBpmDisplayed_ > 0.5f) return; // host-locked → read-only
        dragStartY_ = e.position.y;
        dragStartVal_ = static_cast<float>(slider_.getValue());
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (hostBpmDisplayed_ > 0.5f) return;
        const float dy = dragStartY_ - e.position.y;
        const float speed = e.mods.isShiftDown() ? 0.2f : 0.5f; // shift = fine
        const float v = juce::jlimit(60.0f, 300.0f, dragStartVal_ + dy * speed);
        slider_.setValue(v, juce::sendNotificationSync);
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        if (hostBpmDisplayed_ > 0.5f) return;
        const float dir = w.deltaY > 0 ? 1.0f : (w.deltaY < 0 ? -1.0f : 0.0f);
        if (dir == 0.0f) return;
        const float v = juce::jlimit(60.0f, 300.0f,
                                     static_cast<float>(slider_.getValue()) + dir);
        slider_.setValue(v, juce::sendNotificationSync);
    }

    void mouseDoubleClick(const juce::MouseEvent&) override
    {
        if (hostBpmDisplayed_ > 0.5f) return;
        openEditor();
    }

    void resized() override
    {
        if (editor_ != nullptr) editor_->setBounds(getLocalBounds());
    }

private:
    void timerCallback() override
    {
        if (! hostBpmFn_) return;
        const float h = hostBpmFn_();
        if (std::abs(h - hostBpmDisplayed_) > 0.05f)
        {
            hostBpmDisplayed_ = h;
            repaint();
        }
    }

    void openEditor()
    {
        editor_ = std::make_unique<juce::TextEditor>();
        editor_->setBounds(getLocalBounds());
        editor_->setBorder({ 0, 0, 0, 0 });
        editor_->setIndents(4, 0);
        editor_->setJustification(juce::Justification::centred);
        editor_->setColour(juce::TextEditor::backgroundColourId, col::graphite());
        editor_->setColour(juce::TextEditor::textColourId,       col::bone());
        editor_->setColour(juce::TextEditor::outlineColourId,    col::accentAmber());
        editor_->setColour(juce::TextEditor::focusedOutlineColourId, col::accentAmber());
        editor_->setColour(juce::TextEditor::highlightColourId,  col::accentAmber().withAlpha(0.35f));
        editor_->setFont(fonts::value(10.5f));
        editor_->setInputRestrictions(3, "0123456789"); // 3 digits, integer
        editor_->setText(juce::String(static_cast<int>(std::round(slider_.getValue()))),
                         juce::dontSendNotification);
        editor_->selectAll();

        editor_->onReturnKey   = [this] { commitEditor(); };
        editor_->onEscapeKey   = [this] { dismissEditor(); };
        editor_->onFocusLost   = [this] { commitEditor(); };

        addAndMakeVisible(*editor_);
        editor_->grabKeyboardFocus();
    }

    void commitEditor()
    {
        if (editor_ == nullptr) return;
        const float typed = editor_->getText().getFloatValue();
        if (typed >= 60.0f && typed <= 300.0f)
            slider_.setValue(typed, juce::sendNotificationSync);
        dismissEditor();
    }

    void dismissEditor()
    {
        if (editor_ != nullptr)
        {
            editor_.reset();
            repaint();
        }
    }

    juce::Slider slider_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
    std::unique_ptr<juce::TextEditor> editor_;
    std::function<float()> hostBpmFn_;
    float hostBpmDisplayed_ = 0.0f;
    float dragStartY_ = 0.0f;
    float dragStartVal_ = 120.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BpmDisplay)
};

} // namespace bombo
