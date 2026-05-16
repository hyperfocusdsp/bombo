#include "PluginEditor.h"
#include "PluginProcessor.h"

BomboEditor::BomboEditor(BomboProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processorRef(p),
      faceplate(p.apvts)
{
    setLookAndFeel(&lnf);
    addAndMakeVisible(faceplate);
    setSize(1320, 950);
    setResizable(false, false);
}

BomboEditor::~BomboEditor()
{
    setLookAndFeel(nullptr);
}

void BomboEditor::paint(juce::Graphics& g)
{
    g.fillAll(bombo::col::graphite);
}

void BomboEditor::resized()
{
    faceplate.setBounds(getLocalBounds());
}
