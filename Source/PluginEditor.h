#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "GUI/BomboLookAndFeel.h"
#include "GUI/FaceplatePanel.h"
#include "GUI/LayoutEditOverlay.h"
#include "GUI/BBS/BBSComponent.h"
#include "GUI/Theme/ThemeProvider.h"
#include "State/PersistentState.h"

class BomboProcessor;

class BomboEditor : public juce::AudioProcessorEditor
{
public:
    explicit BomboEditor(BomboProcessor&);
    ~BomboEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool keyPressed(const juce::KeyPress& key) override;
    void visibilityChanged() override;

private:
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

    // Temporary theme-switching UI (Plan A T7). Replaced by HeaderBar
    // selector in Plan B. Declaration order matters: persistentState_
    // must outlive themeSelector_'s onChange lambda, which captures
    // `this` and writes to persistentState_.
    bombo::PersistentState persistentState_;
    juce::ComboBox themeSelector_;

    // BBS hidden terminal overlay. Sibling of `faceplate`, sized to the
    // full editor bounds, invisible by default. Dev affordance: bbsButton_
    // and Ctrl+Shift+B in keyPressed() both call bbs_.show(); long-press
    // nose detonator activation is parked pending mockup.
    bombo::BBSComponent bbs_;
    juce::TextButton    bbsButton_;

    // Layout-edit overlay (ported from squelch_pro 2026-05-17). Toggled
    // by F2 or Ctrl+Shift+E in keyPressed. Sits BETWEEN faceplate and
    // bbs_ in z-order so layout-edit drags don't show through the BBS
    // overlay when both are active.
    std::unique_ptr<bombo::LayoutEditOverlay> layoutEditor_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboEditor)
};
