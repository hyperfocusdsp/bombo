#include "PluginEditor.h"
#include "PluginProcessor.h"

BomboEditor::BomboEditor(BomboProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processorRef(p),
      faceplate(p.apvts)
{
    setLookAndFeel(&lnf);
    addAndMakeVisible(faceplate);
    // 1-row 7-column rack fits comfortably at this size; resizable so
    // the user can dial it to their workflow.
    setSize(1280, 720);
    setResizable(true, true);
    setResizeLimits(1080, 620, 1600, 960);
    setWantsKeyboardFocus(true);
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

void BomboEditor::visibilityChanged()
{
    if (isVisible()) grabKeyboardFocus();
}

bool BomboEditor::keyPressed(const juce::KeyPress& key)
{
    // Space, T, or Return → fire a kick. Matches the Rust archive's
    // editor bridge: editor pushes count, audio thread drains at the
    // top of process().
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (key.getKeyCode() == juce::KeyPress::spaceKey
        || key.getKeyCode() == juce::KeyPress::returnKey
        || ch == 't')
    {
        processorRef.triggerFromKeyboard();
        return true;
    }
    return false;
}
