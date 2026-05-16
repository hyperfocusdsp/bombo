#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_utils/juce_audio_utils.h>
// StandaloneFilterWindow.h uses AudioProcessorPlayer (juce_audio_utils)
// and MidiInput / AudioDeviceManager (juce_audio_devices); both modules
// must be transitively available before the header is parsed.
#include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>

#if JUCE_LINUX
 // X11 headers define `KeyPress`, `Status`, etc. as macros that clash with
 // juce::KeyPress and other JUCE types — so the implementation lives in
 // its own translation unit. Declare the entry point here.
 namespace bombo { bool claimCompositorSelectionOnce(); }
#endif

static bombo::FaceplatePanel::SampleSlotCallbacks makeSampleSlotCallbacks(BomboProcessor& p)
{
    bombo::FaceplatePanel::SampleSlotCallbacks cb;
    cb.onBrowsePick   = [&p](const juce::File& f) { p.setVoiceBSampleFolder(f); };
    cb.onIndexChange  = [&p](int idx)             { p.loadVoiceBSampleByIndex(idx); };
    cb.onClear        = [&p]()                    { p.clearVoiceBSample(); };
    cb.getNames       = [&p]()                    { return p.voiceBSampleNames(); };
    cb.getCurrentIdx  = [&p]()                    { return p.voiceBSampleIndex(); };
    return cb;
}

BomboEditor::BomboEditor(BomboProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processorRef(p),
      faceplate(p.apvts, &p.waveBuffer(), makeSampleSlotCallbacks(p),
                [&p]() { return p.hostBpm(); },
                [&p]() { p.randomizeBombo(); })
{
   #if JUCE_LINUX
    // Run exactly once per process. Inside a DAW host, the host's own X
    // window may not benefit, but the claim itself is harmless (idempotent
    // and only fires when no other owner exists).
    static const bool compositorClaimed = bombo::claimCompositorSelectionOnce();
    (void) compositorClaimed;
   #endif

    setOpaque(false);
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

    // First-launch fit: defer until the message loop is idle. Setting
    // size synchronously in the constructor races with StandaloneFilter-
    // Window's window-mapping negotiation (which bounces the editor
    // between our target and setResizeLimits' min for 10+ cycles before
    // settling). callAsync runs after that's done so the size sticks.
    // Picks the biggest connected display by area (laptop+external →
    // external is almost always where you work). Capped by min/max.
    // JUCE's StandalonePluginHolder persists window-X/Y on close;
    // window-W/H currently isn't persisted by JUCE, so this also runs
    // on every launch — harmless since it always lands at the same fit.
    juce::MessageManager::callAsync(
        [=, safe = juce::Component::SafePointer<BomboEditor>(this)]() mutable
        {
            if (safe == nullptr) return;

            int w = -1;
           #if JucePlugin_Build_Standalone
            // First preference: a width remembered from a previous launch.
            // JUCE's StandalonePluginHolder owns a PropertiesFile; we store
            // editor width there in BomboEditor::resized().
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
            {
                if (auto* props = holder->settings.get())
                {
                    const int saved = props->getIntValue("bombo-editor-width", -1);
                    if (saved >= kMinWidth && saved <= kMaxWidth) w = saved;
                }
            }
           #endif

            if (w <= 0)
            {
                // Fall back to fit-to-display on first launch.
                const auto& displays = juce::Desktop::getInstance().getDisplays().displays;
                const juce::Displays::Display* biggest = nullptr;
                for (const auto& d : displays)
                {
                    if (biggest == nullptr
                        || (d.userArea.getWidth() * d.userArea.getHeight())
                           > (biggest->userArea.getWidth() * biggest->userArea.getHeight()))
                        biggest = &d;
                }
                if (biggest == nullptr) return;
                const auto area = biggest->userArea;
                constexpr int kChromePad = 60;
                const double padW = juce::jmax(1, area.getWidth()  - kChromePad);
                const double padH = juce::jmax(1, area.getHeight() - kChromePad);
                const double scale = std::min(padW / kDesignW, padH / kDesignH);
                w = static_cast<int>(std::round(kDesignW * scale));
                w = juce::jlimit(kMinWidth, kMaxWidth, w);
            }
            const int h = static_cast<int>(std::round(w / kAspect));
            safe->setSize(w, h);
            safe->initialSizeApplied_ = true;

           #if JucePlugin_Build_Standalone
            // Make the DocumentWindow non-opaque exactly once, after the
            // editor is parented and the initial size has settled. JUCE
            // recreates the native peer with an ARGB visual so Hyprland
            // can composite through the transparent corner wedges.
            if (auto* top = safe->getTopLevelComponent(); top != nullptr && top != safe.getComponent())
            {
                top->setOpaque(false);
                // ResizableWindow::paint fills with backgroundColourId — opaque
                // grey by default, which would mask the transparent corners.
                // Set it transparent so only chassisPath_ pixels are drawn.
                if (auto* rw = dynamic_cast<juce::ResizableWindow*>(top))
                    rw->setBackgroundColour(juce::Colours::transparentBlack);
            }
           #endif
        });

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
    // Transparent — the faceplate's chassisPath_ fill covers everything
    // inside the bomb shape; corner wedges stay alpha=0.
    g.fillAll(juce::Colours::transparentBlack);
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

   #if JucePlugin_Build_Standalone
    // Persist the editor width so the next launch reopens at this scale.
    // Skip until initialSizeApplied_ — the ctor's design-default setSize
    // fires resized() before the deferred lambda has a chance to read the
    // persisted value, and we'd otherwise clobber it. Resize fires often
    // during interactive drags; setValue is a cheap in-memory write — the
    // PropertiesFile flushes lazily.
    if (initialSizeApplied_)
        if (auto* holder = juce::StandalonePluginHolder::getInstance())
            if (auto* props = holder->settings.get())
                props->setValue("bombo-editor-width", getWidth());
   #endif
}

void BomboEditor::visibilityChanged()
{
    if (isVisible()) grabKeyboardFocus();
}

bool BomboEditor::keyPressed(const juce::KeyPress& key)
{
    // Transport-style keybinds:
    //   Space / Return → toggle LOOP (start = first kick + auto-fire at BPM,
    //                                  stop = deferred tail kill at next beat).
    //   T              → one-shot: fire a kick + schedule tail kill one beat
    //                    later. Independent of loop state.
    const auto kc = key.getKeyCode();
    if (kc == juce::KeyPress::spaceKey || kc == juce::KeyPress::returnKey)
    {
        processorRef.toggleLoop();
        return true;
    }
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (ch == 't')
    {
        processorRef.triggerOneShot();
        return true;
    }
    return false;
}
