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

private:
    BomboProcessor& processorRef;
    bombo::BomboLookAndFeel lnf;
    bombo::FaceplatePanel faceplate;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboEditor)
};
