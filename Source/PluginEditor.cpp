#include "PluginEditor.h"
#include "PluginProcessor.h"

BomboEditor::BomboEditor(BomboProcessor& p)
    : juce::AudioProcessorEditor(&p), processorRef(p)
{
    // Phase 0 skeleton: 1320×950 chassis matching the Rust Bombo dimensions.
    // Full faceplate/FX rack lands in Phase 3.
    setSize(1320, 950);
    setResizable(false, false);
}

void BomboEditor::paint(juce::Graphics& g)
{
    // Mil-rice graphite background (placeholder — final palette in Phase 3).
    g.fillAll(juce::Colour::fromRGB(0x14, 0x15, 0x17));

    g.setColour(juce::Colour::fromRGB(0xF4, 0xF1, 0xEA));
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    g.drawText("BOMBO", getLocalBounds().reduced(20).removeFromTop(60),
               juce::Justification::centredLeft);

    g.setColour(juce::Colour::fromRGB(0x8A, 0x88, 0x82));
    g.setFont(juce::FontOptions(11.0f));
    g.drawText("Phase 0 — JUCE skeleton",
               getLocalBounds().reduced(20).removeFromTop(90).removeFromBottom(20),
               juce::Justification::centredLeft);
}

void BomboEditor::resized() {}
