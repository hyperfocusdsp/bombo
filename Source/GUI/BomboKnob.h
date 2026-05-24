#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace bombo
{

// juce::Slider subclass that ports SquelchPro's "perfect knob action" polish
// while keeping BomboLookAndFeel's drawRotarySlider visual treatment:
//
//   1. Hover state — mouseEnter / mouseExit repaint; L&F reads
//      isMouseOverOrDragging() to brighten the cap on hover.
//   2. Cursor-hidden drag — JUCE's enableUnboundedMouseMovement(true, true)
//      hides the cursor on mouseDown and restores its position on mouseUp,
//      so dragging feels like turning a physical knob (no cursor wandering
//      off-screen).
//   3. Double-click text entry — opens the slider's built-in editor instead
//      of the default "reset to default value" behavior.
//   4. Calibrated drag sensitivity — pixels-per-100% tuned so default-drag
//      feels appropriate at small knob sizes; modifier keys (Shift) yield
//      finer adjustment through JUCE's standard modifier-fine path.
//
// All knobs in Bombo (FX, macros, sample-slot, etc.) go through this so the
// interaction is uniform; the L&F still owns the visual.
class BomboKnob : public juce::Slider
{
public:
    BomboKnob()
    {
        // Don't yank the value to wherever the user clicks — let the drag
        // start from the current value, like a physical knob.
        setSliderSnapsToMousePosition(false);

        // Default-double-click would reset-to-default; we want the editor.
        setDoubleClickReturnValue(false, 0.0);

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

    void mouseDoubleClick(const juce::MouseEvent& /*e*/) override
    {
        // Slider::showTextBox() spawns the in-place numeric editor; preferable
        // to the default reset-to-default-value behavior for a producer-facing
        // kick designer where exact-value entry is occasionally needed.
        showTextBox();
    }

private:
    juce::Point<int> dragStartScreenPos_;
    bool             cursorHidden_ = false;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboKnob)
};

} // namespace bombo
