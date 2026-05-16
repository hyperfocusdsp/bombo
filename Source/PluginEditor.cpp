#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
// StandaloneFilterWindow.h uses AudioProcessorPlayer (juce_audio_utils)
// and MidiInput / AudioDeviceManager (juce_audio_devices); both modules
// must be transitively available before the header is parsed.
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

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

    // ── Standalone-only: enable every available MIDI input on first
    //    launch. JUCE's StandalonePluginHolder defaults MIDI inputs
    //    off — users have had to dig through Options → Audio/MIDI
    //    Settings. Auto-enabling matches the Rust archive's behaviour
    //    where MIDI just worked when you launched the standalone.
    //    The setting persists in ~/.config/Bombo/Bombo.settings.
   #if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
    {
        auto& dm = holder->deviceManager;
        for (const auto& dev : juce::MidiInput::getAvailableDevices())
            dm.setMidiInputDeviceEnabled(dev.identifier, true);
    }
   #endif
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
