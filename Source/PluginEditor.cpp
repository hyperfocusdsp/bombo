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
      faceplate(p.apvts, &p.waveBuffer())
{
    setLookAndFeel(&lnf);
    addAndMakeVisible(faceplate);

    // Design-size coordinates the faceplate paints in. Resizing applies
    // an AffineTransform::scale so every knob, label, column, fin, and
    // band scales together — no per-child re-layout, no overflow.
    constexpr double kDesignW    = 820.0;
    constexpr double kDesignH    = 920.0;
    constexpr double kAspect     = kDesignW / kDesignH;
    constexpr int    kMinWidth   = 600;
    constexpr int    kMaxWidth   = 1400;

    setSize(static_cast<int>(kDesignW), static_cast<int>(kDesignH));
    setResizable(true, true);
    setResizeLimits(kMinWidth, static_cast<int>(kMinWidth / kAspect),
                    kMaxWidth, static_cast<int>(kMaxWidth / kAspect));
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio(kAspect);
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
    // Same colour as the faceplate's outside-chassis backdrop so any
    // letterbox strip is invisible at the editor edge.
    g.fillAll(bombo::col::graphite);
}

void BomboEditor::resized()
{
    // Faceplate paints in fixed 820×920 design coordinates and we apply a
    // uniform scale transform so every child scales together. The
    // constrainer keeps width/height locked to design aspect, so both
    // scale factors come out equal in practice — `jmin` covers the
    // rounding-gap case. Bounds are set to `editor / scale` (rounded up)
    // so the transformed faceplate fully covers the editor, even when
    // the scale isn't an exact integer ratio — kills the thin right-edge
    // strip that previously appeared at off-design sizes.
    constexpr float kDesignW = 820.0f;
    constexpr float kDesignH = 920.0f;
    const float scale = juce::jmin(static_cast<float>(getWidth())  / kDesignW,
                                   static_cast<float>(getHeight()) / kDesignH);
    if (scale <= 0.0f) return;
    faceplate.setTransform(juce::AffineTransform::scale(scale));
    const int boundsW = static_cast<int>(std::ceil(static_cast<float>(getWidth())  / scale));
    const int boundsH = static_cast<int>(std::ceil(static_cast<float>(getHeight()) / scale));
    faceplate.setBounds(0, 0, boundsW, boundsH);
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
