#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "GUI/BomboLookAndFeel.h"
#include "GUI/FaceplatePanel.h"

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboEditor)
};
