#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bombo
{

// juce::Slider subclass that ports SquelchPro's "perfect knob action" polish
// while keeping BomboLookAndFeel's drawRotarySlider visual treatment:
//
//   1. Hover state — mouseEnter / mouseExit repaint; L&F reads
//      isMouseOverOrDragging() to brighten the cap on hover.
//   2. Cursor-hidden drag — hides the cursor on mouseDown and restores its
//      position on mouseUp, so dragging feels like turning a physical knob
//      (no cursor wandering off-screen).
//   3. Double-click + Ctrl-click reset — both gestures snap the slider to
//      its default value, matching industry-standard DAW knob behaviour
//      (Bitwig/Ableton/Studio One). Text entry moved to right-click menu
//      (JUCE's built-in popup auto-includes the "Type a value..." item).
//   4. Calibrated drag sensitivity — pixels-per-100% tuned so default-drag
//      feels appropriate at small knob sizes; modifier keys (Shift) yield
//      finer adjustment through JUCE's standard modifier-fine path.
//
// All knobs in Bombo (FX, macros, sample-slot, etc.) go through this so the
// interaction is uniform; the L&F still owns the visual.
//
// NOTE: enabling double-click reset requires the construction site to call
// setDoubleClickReturnValue(true, defaultValue) AFTER attaching the slider
// to its APVTS param — see FaceplatePanel::addKnob (2026-05-24 follow-up).
class BomboKnob : public juce::Slider
{
public:
    BomboKnob()
    {
        // Don't yank the value to wherever the user clicks — let the drag
        // start from the current value, like a physical knob.
        setSliderSnapsToMousePosition(false);

        // Default-double-click resets to the value set via
        // setDoubleClickReturnValue(true, X) by the construction site.
        // The flag gets enabled once that helper is called (we keep it
        // false here so an un-wired slider stays inert rather than
        // resetting to whatever stale value lingered).
        setDoubleClickReturnValue(false, 0.0);

        // JUCE's default popup menu (right-click) already includes a
        // "Reset to default value" entry once the default is configured
        // via setDoubleClickReturnValue. Enable it so users have a menu
        // path alongside the gesture shortcuts.
        setPopupMenuEnabled(true);

        // 240 px ≈ a comfortable full-range sweep at default sensitivity.
        // Shift modifier-fine drops automatically via JUCE.
        setMouseDragSensitivity(240);
    }

    void mouseEnter(const juce::MouseEvent& e) override
    {
        juce::Slider::mouseEnter(e);
        repaint();
    }

    void mouseExit(const juce::MouseEvent& e) override
    {
        juce::Slider::mouseExit(e);
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        // Ctrl+click (or Cmd+click on macOS) resets the knob to its default
        // value — same outcome as a double-click, just a single-gesture
        // shortcut for users who'd rather not double-tap. Industry-standard
        // pattern (Bitwig, Ableton, Studio One). We do NOT enter drag mode
        // when the modifier is held so a stray drag after reset doesn't
        // immediately move the slider off its just-reset value.
        if (e.mods.isCtrlDown() || e.mods.isCommandDown())
        {
            if (isDoubleClickReturnEnabled())
            {
                setValue(getDoubleClickReturnValue(), juce::sendNotificationSync);
                return;  // skip base mouseDown — no drag should follow
            }
        }
        // Hide the cursor for the duration of the drag so it feels like
        // turning a physical knob instead of dragging a pointer up a screen.
        dragStartScreenPos_ = e.getScreenPosition();
        if (! e.mods.isRightButtonDown() && ! e.mods.isPopupMenu())
        {
            setMouseCursor(juce::MouseCursor::NoCursor);
            cursorHidden_ = true;
        }
        juce::Slider::mouseDown(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        juce::Slider::mouseUp(e);
        if (cursorHidden_)
        {
            // Snap the cursor back to where the drag started so the user
            // doesn't lose their pointer halfway up the screen.
            juce::Desktop::getInstance().getMainMouseSource()
                .setScreenPosition(dragStartScreenPos_.toFloat());
            setMouseCursor(juce::MouseCursor::NormalCursor);
            cursorHidden_ = false;
        }
    }

    // mouseDoubleClick is intentionally NOT overridden — we want JUCE's
    // built-in behaviour, which resets the slider to its configured
    // default value (see setDoubleClickReturnValue call site in
    // FaceplatePanel::addKnob). Text entry stays accessible via the
    // right-click popup menu.

private:
    juce::Point<int> dragStartScreenPos_;
    bool             cursorHidden_ = false;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboKnob)
};

} // namespace bombo
