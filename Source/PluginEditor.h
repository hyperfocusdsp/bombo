#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

class BomboProcessor;

class BomboEditor : public juce::AudioProcessorEditor
{
public:
    explicit BomboEditor(BomboProcessor&);
    ~BomboEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    BomboProcessor& processorRef;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboEditor)
};
