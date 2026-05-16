#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

#include "Colours.h"
#include "Fonts.h"
#include "Theme/ThemedComponent.h"

namespace bombo
{

// Discrete rotary knob that doubles as a sample browser.
//
// Empty state: cap renders "LOAD"; clicking opens a native FileChooser.
// Once the user picks a file, the parent folder is scanned for siblings
// (.wav/.aif/.aiff/.flac) and the widget becomes an N-step knob — rotate
// or arrow-key through the folder to swap the loaded sample.
//
// Decoupled from the processor: the owner supplies callbacks for browsing,
// index-load, clear; and getters for the current folder list / index /
// display name. The widget reads the callbacks once per refresh().
class SampleSlotWidget : public juce::Component,
                         public bombo::ThemedComponent,
                         private juce::Timer
{
public:
    SampleSlotWidget()
    {
        setOpaque(false);
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus(true);
        setMouseClickGrabsKeyboardFocus(true);
        // 8 Hz poll: catches DAW state restore + standalone settings restore
        // landing after the editor opens, without requiring an explicit
        // refresh signal from the processor.
        startTimerHz(8);
    }

    // Wired by the owner. browseFromUserPick — called once the user has
    // picked a file via FileChooser; the owner forwards this to the
    // processor's setVoiceBSampleFolder so the folder gets scanned.
    std::function<void(const juce::File&)> onBrowsePick;
    // Called when the user rotates the knob / presses arrow keys.
    std::function<void(int newIndex)>      onIndexChange;
    // Called when the user picks "Clear" from the right-click menu.
    std::function<void()>                  onClear;
    // Owner returns the current sibling-sample list and the active index.
    std::function<juce::StringArray()>     getNames;
    std::function<int()>                   getCurrentIndex;

    // Owner calls this after a load/clear/folder-scan so we refresh
    // the cached list & repaint.
    void refresh()
    {
        if (getNames)         names_       = getNames();
        if (getCurrentIndex)  currentIdx_  = getCurrentIndex();
        repaint();
    }

    bool isLoaded() const noexcept { return currentIdx_ >= 0 && currentIdx_ < names_.size(); }

    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds().toFloat();
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        if (diameter < 12.0f) return;

        const auto cx = bounds.getCentreX();
        const auto cy = bounds.getCentreY();
        const float radius = diameter * 0.5f - 4.0f;
        if (radius < 6.0f) return;

        // Match BomboLookAndFeel knob body — dark plastic cap, no gradient
        // ring fuss; this widget intentionally renders a simpler look so it
        // reads as "browse target" rather than "parameter knob."
        const auto cap     = col::knobCap();
        const auto capTop  = cap.brighter(0.10f);
        const auto capBot  = cap.darker (0.18f);

        // Mounting recess
        g.setColour(juce::Colour(0xFF000000).withAlpha(0.35f));
        g.fillEllipse(cx - radius - 1.0f, cy - radius + 1.5f,
                      (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f);
        g.setColour(juce::Colour(0xFF050507));
        g.fillEllipse(cx - radius - 2.0f, cy - radius - 2.0f,
                      (radius + 2.0f) * 2.0f, (radius + 2.0f) * 2.0f);

        // Rubber grip
        g.setColour(col::knobRubber());
        g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);

        // Cap core gradient
        const float coreR = radius * 0.78f;
        g.setGradientFill(juce::ColourGradient(capTop, cx, cy - coreR,
                                               capBot, cx, cy + coreR, false));
        g.fillEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f);

        const bool hot = isMouseOver(true) || hasKeyboardFocus(false);
        g.setColour(hot ? col::accentAmber().withAlpha(0.85f)
                        : juce::Colour::fromRGBA(0, 0, 0, 0x55));
        g.drawEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f, 1.0f);

        // Tick ring — N marks when loaded, 1 when empty (just a hint glyph).
        const int n = names_.size();
        if (n > 1)
        {
            constexpr float kStartAngle = juce::MathConstants<float>::pi * 1.25f;
            constexpr float kEndAngle   = juce::MathConstants<float>::pi * 2.75f;
            const float tickInR  = radius + 1.5f;
            const float tickOutR = tickInR + juce::jmax(3.0f, radius * 0.18f);
            for (int i = 0; i < n; ++i)
            {
                const float t = static_cast<float>(i)
                              / static_cast<float>(n - 1);
                const float ta = juce::jmap(t, kStartAngle, kEndAngle)
                               - juce::MathConstants<float>::halfPi;
                const float cc = std::cos(ta);
                const float ss = std::sin(ta);
                g.setColour(i == currentIdx_ ? col::bone()
                                              : col::bone().withAlpha(0.45f));
                g.drawLine(cx + cc * tickInR,  cy + ss * tickInR,
                           cx + cc * tickOutR, cy + ss * tickOutR,
                           i == currentIdx_ ? 2.0f : 1.4f);
            }

            // Indicator wedge — points to the active position.
            const float aT = static_cast<float>(currentIdx_)
                           / static_cast<float>(n - 1);
            const float ang = juce::jmap(aT, kStartAngle, kEndAngle)
                            - juce::MathConstants<float>::halfPi;
            const float ic = std::cos(ang);
            const float is = std::sin(ang);
            const float stemInR  = coreR;
            const float stemOutR = radius;
            const float perpC = -is;
            const float perpS =  ic;
            constexpr float stemW = 2.6f;
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
        }

        // Cap text — "LOAD" when empty, otherwise filename + (i/N).
        const float capInner = coreR * 0.92f;
        g.setColour(col::bone().withAlpha(0.92f));
        if (isLoaded())
        {
            g.setFont(fonts::value(juce::jlimit(8.0f, 11.5f, capInner * 0.55f)));
            g.drawFittedText(names_[currentIdx_],
                             juce::Rectangle<float>(cx - capInner,
                                                    cy - capInner,
                                                    capInner * 2.0f,
                                                    capInner * 1.25f).toNearestInt(),
                             juce::Justification::centredBottom, 2, 0.85f);
            g.setColour(col::boneDim());
            g.setFont(fonts::value(juce::jmax(6.5f, capInner * 0.38f)));
            g.drawFittedText(juce::String(currentIdx_ + 1) + "/" + juce::String(names_.size()),
                             juce::Rectangle<float>(cx - capInner,
                                                    cy,
                                                    capInner * 2.0f,
                                                    capInner * 0.9f).toNearestInt(),
                             juce::Justification::centredTop, 1, 0.9f);
        }
        else
        {
            g.setFont(fonts::title(juce::jlimit(10.0f, 14.0f, capInner * 0.7f)));
            g.drawFittedText("LOAD",
                             juce::Rectangle<float>(cx - capInner,
                                                    cy - capInner,
                                                    capInner * 2.0f,
                                                    capInner * 2.0f).toNearestInt(),
                             juce::Justification::centred, 1, 0.85f);
        }
    }

    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }
    void focusGained(juce::Component::FocusChangeType) override { repaint(); }
    void focusLost  (juce::Component::FocusChangeType) override { repaint(); }

    void mouseDown(const juce::MouseEvent& e) override
    {
        grabKeyboardFocus();
        if (e.mods.isRightButtonDown() && isLoaded())
        {
            juce::PopupMenu m;
            m.addItem(1, "Browse new folder…");
            m.addItem(2, "Clear");
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
                [safe = juce::Component::SafePointer<SampleSlotWidget>(this)] (int r)
                {
                    if (safe == nullptr) return;
                    if (r == 1) safe->launchChooser();
                    else if (r == 2 && safe->onClear) { safe->onClear(); safe->refresh(); }
                });
            return;
        }
        // Left-click on empty → browse. Left-click on loaded → start drag.
        if (! isLoaded())
        {
            launchChooser();
            return;
        }
        dragStartY_ = e.position.y;
        dragStartIdx_ = currentIdx_;
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (! isLoaded()) return;
        // Vertical drag → step through the list. ~10 pixels per step.
        const int n = names_.size();
        if (n < 2) return;
        const float dy = dragStartY_ - e.position.y;
        const int delta = static_cast<int>(dy / 10.0f);
        int newIdx = juce::jlimit(0, n - 1, dragStartIdx_ + delta);
        if (newIdx != currentIdx_) commitIndex(newIdx);
    }

    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& wheel) override
    {
        const int n = names_.size();
        if (n < 2) return;
        const int dir = wheel.deltaY > 0.0f ? 1 : (wheel.deltaY < 0.0f ? -1 : 0);
        if (dir == 0) return;
        commitIndex(juce::jlimit(0, n - 1, currentIdx_ + dir));
    }

    bool keyPressed(const juce::KeyPress& key) override
    {
        const int n = names_.size();
        if (n < 2) return false;
        const auto kc = key.getKeyCode();
        if (kc == juce::KeyPress::upKey || kc == juce::KeyPress::rightKey)
        { commitIndex(juce::jlimit(0, n - 1, currentIdx_ + 1)); return true; }
        if (kc == juce::KeyPress::downKey || kc == juce::KeyPress::leftKey)
        { commitIndex(juce::jlimit(0, n - 1, currentIdx_ - 1)); return true; }
        return false;
    }

private:
    void timerCallback() override
    {
        // Lightweight watcher: if the processor's notion of the current
        // index/names differs from what we cached, refresh & repaint. This
        // covers async state restore and any external param changes.
        if (! getCurrentIndex && ! getNames) return;
        int newIdx = getCurrentIndex ? getCurrentIndex() : currentIdx_;
        juce::StringArray newNames = getNames ? getNames() : juce::StringArray();
        if (newIdx != currentIdx_ || newNames != names_)
        {
            currentIdx_ = newIdx;
            names_      = std::move(newNames);
            repaint();
        }
    }

    void commitIndex(int newIdx)
    {
        if (newIdx == currentIdx_) return;
        currentIdx_ = newIdx;
        repaint();
        if (onIndexChange) onIndexChange(newIdx);
    }

    void launchChooser()
    {
        chooser_ = std::make_unique<juce::FileChooser>(
            "Load punch sample",
            juce::File::getSpecialLocation(juce::File::userHomeDirectory),
            "*.wav;*.aif;*.aiff;*.flac");
        const int flags = juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles;
        chooser_->launchAsync(flags,
            [safe = juce::Component::SafePointer<SampleSlotWidget>(this)]
            (const juce::FileChooser& fc)
            {
                if (safe == nullptr) return;
                const auto file = fc.getResult();
                if (file == juce::File()) return;
                if (safe->onBrowsePick) safe->onBrowsePick(file);
                safe->refresh();
            });
    }

    juce::StringArray names_;
    int currentIdx_ = -1;
    float dragStartY_ = 0.0f;
    int   dragStartIdx_ = 0;
    std::unique_ptr<juce::FileChooser> chooser_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleSlotWidget)
};

} // namespace bombo
