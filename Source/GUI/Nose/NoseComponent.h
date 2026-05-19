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

    // Force-reset: 3 rapid taps within 800ms when the caller says conditions are met.
    std::function<bool()> isForceResetReady;  // set by FaceplatePanel via PluginEditor
    std::function<void()> onForceReset;       // set by FaceplatePanel via PluginEditor

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

    int        resetTapCount_   = 0;
    juce::Time lastResetTapTime_;

    static constexpr int kResetTaps    = 3;
    static constexpr int kResetTimeout = 800;

    // Tooltip messages per progression level (used after first BBS unlock).
    static constexpr const char* kTooltips[5] = {
        "[!]  WARNING: DO NOT TOUCH",
        "[!]  ARMED -- CLEARANCE LVL 1",
        "[!]  ARMED -- CLEARANCE LVL 2",
        "[!]  ARMED -- CLEARANCE LVL 3",
        "[!]  ARMED -- CLEARANCE LVL 4 -- SYSTEM IGNITED",
    };

    // Tap-in-progress warnings shown BEFORE first BBS unlock. Indexed by
    // tapCount_ (0..6). Resets to entry 0 after kTapTimeoutMs idle. Entry 0
    // is intentionally distinct from kTooltips[0] so the active branch is
    // visually identifiable when debugging.
    static constexpr const char* kTapTooltips[7] = {
        "[?]       UNKNOWN DEVICE -- TAP TO INSPECT",
        "[!]       ALERT 1/7 -- STAND DOWN",
        "[!!]      WARNING 2/7 -- LAST CHANCE",
        "[!!!]     DANGER 3/7 -- BREACH IMMINENT",
        "[!!!!]    CRITICAL 4/7 -- OVERLOAD",
        "[!!!!!]   ARMED 5/7 -- POINT OF NO RETURN",
        "[!!!!!!]  FINAL 6/7 -- 1 TAP TO IGNITION",
    };

    void paintCracks(juce::Graphics& g, juce::Rectangle<float> bounds, int level);
    void paintGlow  (juce::Graphics& g, juce::Rectangle<float> bounds, int level);
};

} // namespace bombo
