#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace bombo
{

// Transparent overlay component sized to the nose region of FaceplatePanel.
// Handles multi-tap activation, tooltip, and 5-level crack/glow visual state.
//
// FIRST-TIME (firstEntryDone_ == false):
//   7 taps → onGlitchTap(1..6) fires per tap 1-6 → onActivationComplete fires on tap 7.
//   Tap idle > 2s resets counter without triggering anything.
//
// SUBSEQUENT OPENS (firstEntryDone_ == true):
//   Single tap → onActivationComplete fires immediately.
//
// THREADING: message thread only.
class NoseComponent : public juce::Component,
                      public juce::TooltipClient
{
public:
    NoseComponent();

    // 0 = clean, 1 = hairline crack, 2 = crack + glow, 3 = deeper + phosphor,
    // 4 = fully ignited + pulse. Triggers repaint.
    void setProgressionLevel(int level);
    void setFirstEntryDone(bool done) noexcept { firstEntryDone_ = done; }

    // Fired on tap 7 (or tap 1 if firstEntryDone_). Show BBS in this callback.
    std::function<void()>    onActivationComplete;
    // Fired on each tap 1-6 during the first-time sequence.
    std::function<void(int)> onGlitchTap;

    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

private:
    int  level_          = 0;
    int  tapCount_       = 0;
    bool firstEntryDone_ = false;
    juce::Time lastTapTime_;

    static constexpr int kRequiredTaps  = 7;
    static constexpr int kTapTimeoutMs  = 2000;

    // Tooltip messages per level.
    static constexpr const char* kTooltips[5] = {
        "\xe2\x9a\xa0  WARNING: DO NOT TOUCH",
        "\xe2\x9a\xa0  ARMED \xe2\x80\x94 CLEARANCE LVL 1",
        "\xe2\x9a\xa0  ARMED \xe2\x80\x94 CLEARANCE LVL 2",
        "\xe2\x9a\xa0  ARMED \xe2\x80\x94 CLEARANCE LVL 3",
        "\xe2\x9a\xa0  ARMED \xe2\x80\x94 CLEARANCE LVL 4 \xe2\x80\x94 SYSTEM IGNITED",
    };

    void paintCracks(juce::Graphics& g, juce::Rectangle<float> bounds, int level);
    void paintGlow  (juce::Graphics& g, juce::Rectangle<float> bounds, int level);
};

} // namespace bombo
