#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "GUI/BomboLookAndFeel.h"
#include "GUI/FaceplatePanel.h"
#include "GUI/LayoutEditOverlay.h"
#include "GUI/BBS/BBSComponent.h"
#include "GUI/Theme/ThemeProvider.h"
#include "GUI/ThemeTileStrip.h"
#include "State/PersistentState.h"
#include "Bounce/OfflineBouncer.h"

class BomboProcessor;

class BomboEditor : public juce::AudioProcessorEditor,
                    public juce::Timer
{
public:
    explicit BomboEditor(BomboProcessor&);
    ~BomboEditor() override;

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

    bool keyPressed(const juce::KeyPress& key) override;
    void visibilityChanged() override;
    void timerCallback() override;

private:
    // Periodic UI poll, separate from the one-shot glitch Timer the editor
    // itself uses. Refreshes MIDI-learn knob badges so a CC bound on the audio
    // thread shows up on the knob within ~100 ms.
    struct LambdaTimer : juce::Timer
    {
        std::function<void()> fn;
        void timerCallback() override { if (fn) fn(); }
    };
    LambdaTimer midiBadgeTimer_;
    // Screenshot mode only: fires a one-shot kick on a slow period so the scope
    // holds a complete kick between fires (the BPM loop retriggers too fast and
    // truncates the displayed waveform). Idle unless BOMBO_SCREENSHOT is set.
    LambdaTimer screenshotKickTimer_;

    BomboProcessor& processorRef;
    bombo::BomboLookAndFeel lnf;
    // Tooltip window — picks up `setTooltip(...)` calls from any child
    // component (currently the DiceButton). 700 ms delay before show.
    juce::TooltipWindow tooltipWindow_{ this, 700 };
    bombo::FaceplatePanel faceplate;
    // True after the deferred fit-or-restore lambda has applied a width.
    // resized() suppresses its setValue until then so the design-default
    // setSize fired from the ctor can't clobber the persisted value.
    bool initialSizeApplied_ = false;

    // In-skin theme selector — replaces the temporary juce::ComboBox from
    // Plan A T7. Sits in the header band, right of the BOMBO logo. Owns its
    // own ThemeProvider listener; clicks route through the onChosen lambda
    // wired in the editor ctor so persistence stays here.
    std::unique_ptr<bombo::ThemeTileStrip> themeStrip_;

    // BBS hidden terminal overlay. Sibling of `faceplate`, sized to the
    // full editor bounds, invisible by default. Dev affordances:
    //   Ctrl+Shift+B in keyPressed() re-opens BBS (only when unlocked);
    //   Ctrl+Shift+R resets progression so the 7-tap sequence is required
    //     again (mirrors the DRIVE=0+REVERB=max+3-tap force-reset gesture).
    // The nose 7-tap sequence activates BBS via faceplate.onNoseActivated.
    bombo::BBSComponent bbs_;

    // Layout-edit overlay (ported from an earlier project 2026-05-17). Toggled
    // by F2 or Ctrl+Shift+E in keyPressed. Sits BETWEEN faceplate and
    // bbs_ in z-order so layout-edit drags don't show through the BBS
    // overlay when both are active.
    std::unique_ptr<bombo::LayoutEditOverlay> layoutEditor_;

    // Bounce flow. fileChooser_ must outlive the async launchAsync
    // callback (JUCE FileChooser docs are explicit about this). One
    // bouncer at a time — re-clicking BNC while a bounce is in-flight
    // cancels the old one in the unique_ptr reset.
    std::unique_ptr<juce::FileChooser>      bounceChooser_;
    std::unique_ptr<bombo::OfflineBouncer>  bouncer_;
    void startBounceFlow(bombo::OfflineBouncer::Format format);

    // Glitch animation state for the nose 7-tap sequence.
    enum class GlitchLevel { None, Flicker, Garble, BlackFlash, StaticNoise, RedFlash, GreenPulse };
    GlitchLevel glitchLevel_ = GlitchLevel::None;
    juce::Time  glitchStart_;

    void triggerGlitch(GlitchLevel level, int durationMs = 300);
    void paintGlitchOverlay(juce::Graphics& g);

    // Resets BBS progression so the 7-tap sequence is required again.
    // Shared by the force-reset gesture (DRIVE=0+REVERB=max+3 nose taps)
    // and the Ctrl+Shift+R dev shortcut.
    void resetBbsProgression();

    // Syncs bbs_ bounds to the current rack area + editor scale.
    // Called from resized() and from faceplate.onRackBoundsChanged so the
    // BBS overlay tracks the rack when the layout editor moves/resizes it.
    void updateBbsBounds();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboEditor)
};
