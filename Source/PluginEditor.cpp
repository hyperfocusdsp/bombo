#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "GUI/BombShape.h"
#include "GUI/ChassisRenderer.h"
#include "GUI/Theme/ThemeProvider.h"

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
    cb.onLoadFactory  = [&p]()                    { p.loadFactorySamples(); };
    cb.getNames       = [&p]()                    { return p.voiceBSampleNames(); };
    cb.getCurrentIdx  = [&p]()                    { return p.voiceBSampleIndex(); };
    return cb;
}

BomboEditor::BomboEditor(BomboProcessor& p)
    : juce::AudioProcessorEditor(&p),
      processorRef(p),
      faceplate(p.apvts, &p.waveBuffer(), makeSampleSlotCallbacks(p),
                [&p]() { return p.hostBpm(); },
                [&p]() { p.randomizeBombo(); },
                [this] { startBounceFlow(bombo::OfflineBouncer::Format::Wav); },
                [this] { startBounceFlow(bombo::OfflineBouncer::Format::Aiff); })
{
    // Register the three bundled themes (BANDW/PHOSPHOR/NIGHTRUN) from
    // BinaryData. Must run before any component setup so that col::xxx()
    // accessors used during construction read JSON-loaded values.
    // Idempotent across multiple plugin instances in the same process.
    bombo::ThemeProvider::get().loadBundledThemes();

    // Restore persisted theme BEFORE building the tile strip so its initial
    // active-marker paint reflects the right theme on first show.
    {
        const juce::String saved = processorRef.persistentState().getActiveTheme();
        bombo::ThemeProvider::get().setActive(saved.toStdString());
        // Marketing screenshots want a specific theme regardless of what the
        // user has been working in. BOMBO_FORCE_THEME=<name> overrides without
        // touching persistent state, so the next normal launch returns to the
        // user's last-used theme.
        if (const char* forced = std::getenv("BOMBO_FORCE_THEME"))
            bombo::ThemeProvider::get().setActive(forced);
    }

    // Build the in-skin theme tile strip. Tile click → setActive + persist.
    themeStrip_ = std::make_unique<bombo::ThemeTileStrip>(
        [this](const std::string& name)
        {
            bombo::ThemeProvider::get().setActive(name);
            processorRef.persistentState().setActiveTheme(juce::String(name));
        });

   #if JUCE_LINUX
    // Run exactly once per process. Inside a DAW host, the host's own X
    // window may not benefit, but the claim itself is harmless (idempotent
    // and only fires when no other owner exists).
    static const bool compositorClaimed = bombo::claimCompositorSelectionOnce();
    (void) compositorClaimed;
   #endif

    // Marketing-screenshot mode keeps the editor opaque so the WM doesn't
    // alpha-clip the corner wedges (Hyprland shows whatever's behind when
    // alpha=0). Normal use stays transparent for the shaped-bomb silhouette.
    setOpaque(std::getenv("BOMBO_SOLID_BG") != nullptr);
    setLookAndFeel(&lnf);
    // Wire the factory preset bank BEFORE addAndMakeVisible so the panel's
    // first resized() lays the preset bar out alongside the macros/rack.
    faceplate.setPresetBank(p.presetBank());
    addAndMakeVisible(faceplate);
    // Theme tile strip is an editor-level sibling so it sits in the
    // transparent gutter beneath the chassis tip rather than fighting any
    // on-chassis component for real estate.
    addAndMakeVisible(*themeStrip_);

    // BBS overlay — added LAST so it paints above everything (faceplate,
    // selector). Stays invisible until activation. The callbacks persist
    // the unlock state on first show and the last-seen screen on dismiss.
    // Wire setters before addChildComponent so the component is fully
    // configured before the first resized() / paint() call.
    bbs_.setProgressionManager(&p.progressionManager());
    bbs_.setApvts(&p.apvts);
    bbs_.setTriggerCallback([&p]() { p.triggerOneShot(); });
    bbs_.setPresetBank(&p.presetBank());
    addChildComponent(bbs_);
    bbs_.onShown = [this]
    {
        if (! processorRef.persistentState().getBbsUnlocked())
            processorRef.persistentState().setBbsUnlocked(true);
    };
    bbs_.onDismissed = [this]
    {
        // Last-screen tracking lives on BBSComponent once 1d ships;
        // for now the dismiss callback exists so the wiring is in place.
        (void) this;
    };
    bbs_.onPresetSaved = [this]
    {
        // Refresh the preset bar after BBS S-key saves so the count is
        // immediately correct and keyboard/button navigation includes the
        // newly saved preset.
        faceplate.refreshPresetBar();
    };

    // Wire NoseComponent callbacks through FaceplatePanel.
    faceplate.setNoseProgressionLevel(processorRef.progressionManager().currentLevel());
    faceplate.setNoseFirstEntryDone(processorRef.persistentState().getBbsUnlocked());

    faceplate.onNoseActivated = [this]
    {
        processorRef.persistentState().setBbsUnlocked(true);
        faceplate.setNoseFirstEntryDone(true);
        bbs_.show();
    };

    faceplate.onNoseGlitchTap = [this](int tap)
    {
        static constexpr GlitchLevel kGlitchSequence[] = {
            GlitchLevel::None,        // 0 unused
            GlitchLevel::Flicker,     // tap 1
            GlitchLevel::Garble,      // tap 2
            GlitchLevel::BlackFlash,  // tap 3
            GlitchLevel::StaticNoise, // tap 4
            GlitchLevel::RedFlash,    // tap 5
            GlitchLevel::GreenPulse,  // tap 6
        };
        if (tap >= 1 && tap <= 6)
            triggerGlitch(kGlitchSequence[tap], tap == 4 ? 300 : tap == 3 ? 80 : 200);
    };

    // Wire progression level-up to update nose visual state.
    processorRef.progressionManager().onLevelUp = [this](int newLevel)
    {
        faceplate.setNoseProgressionLevel(newLevel);
    };

    // FX chain order — bridge UI drag-commits into the processor, and push
    // the processor's current order (possibly restored from DAW state) into
    // the panel once at startup so the UI reflects DSP.
    faceplate.onFxOrderChanged = [this](bombo::FxOrder o)
    {
        processorRef.setFxOrder(o);
    };
    faceplate.applyFxOrder(processorRef.getFxOrder());

    // Preset bank ↔ FX order. When a preset applies, route its fxOrder (if
    // any) through both the DSP chain and the faceplate visual; presets
    // that pre-date the field leave the current order untouched. At save
    // time, the bank pulls the current order from the processor.
    processorRef.presetBank().onPresetApplied =
        [this](const bombo::PresetBank::Preset& p)
    {
        // Flush any reverb tail / delay buffer carried over from the
        // previous preset BEFORE we route FX order changes through —
        // gives the first trigger after a preset apply clean DSP state
        // ("clinical first-hit" user request 2026-05-24). Lock-free
        // atomic; consumed at the top of the next processBlock.
        processorRef.requestPresetTailReset();

        if (! p.fxOrder.has_value()) return;
        processorRef.setFxOrder(*p.fxOrder);
        faceplate.applyFxOrder(*p.fxOrder);
    };
    processorRef.presetBank().fxOrderProvider = [this]() -> std::optional<bombo::FxOrder>
    {
        return processorRef.getFxOrder();
    };

    // Force-reset: DRIVE=0 + REVERB=max + 3 rapid taps resets all BBS progression.
    faceplate.setNoseForceResetReady([this]() -> bool
    {
        const auto* driveParam  = processorRef.apvts.getParameter(bombo::pid::driveAmount);
        const auto* reverbParam = processorRef.apvts.getParameter(bombo::pid::reverbSize);
        if (driveParam == nullptr || reverbParam == nullptr) return false;
        return driveParam->getValue() < 0.01f && reverbParam->getValue() > 0.99f;
    });

    faceplate.setNoseForceResetCallback([this]() { resetBbsProgression(); });
    faceplate.onRackBoundsChanged = [this]() { updateBbsBounds(); };

    // MIDI Learn — route the panel's knob menu through to the processor, and
    // poll the map at ~10 Hz so a CC bound on the audio thread lights its badge.
    faceplate.midiLearnRequest    = [this](const juce::String& id) { processorRef.midiArmLearn(id); };
    faceplate.midiForget          = [this](const juce::String& id) { processorRef.midiForget(id); };
    faceplate.getMidiCc           = [this](const juce::String& id) { return processorRef.midiCcForParam(id); };
    faceplate.getMidiArmedParam   = [this]()                       { return processorRef.midiArmedParamId(); };
    midiBadgeTimer_.fn = [this] { faceplate.refreshMidiBadges(); };
    midiBadgeTimer_.startTimerHz(10);
    faceplate.refreshMidiBadges();

    // Layout-edit overlay — ported from an earlier project. F2 / Ctrl+Shift+E
    // toggles. Sits between faceplate and bbs_ in z-order so it doesn't
    // bleed through the BBS overlay.
    layoutEditor_ = std::make_unique<bombo::LayoutEditOverlay>(faceplate);
    addChildComponent(*layoutEditor_);

    // Design-size coordinates the faceplate paints in. Resizing applies
    // an AffineTransform::scale so every knob, label, column, fin, and
    // band scales together — no per-child re-layout, no overflow.
    // Locked 2026-05-17: 9:16 (= 0.5625) for IG-Reels native screenshotting
    // (see memory project_bombo_ig_reels_aspect_constraint.md and the
    // sprint plan at internal notes).
    constexpr double kDesignW    = 600.0;
    constexpr double kDesignH    = 1066.0;
    // Visible window: fin outer tips horizontally; vertically just above the
    // scope's green U-frame → nose tip. Crop top at design-y 50 (= chassisT 0 +
    // kHeaderH 50, the scope-strip boundary) lifts the plugin flush under the
    // title bar while keeping a ~5 px margin so the 3 px green U-frame border at
    // design-y 55 isn't clipped to sub-pixel and dropped at the window edge.
    constexpr double kNoseTipY   = 600.0 / 640.0 * kDesignH;           // ≈ 999.38
    constexpr double kBombTop    = 50.0;                               // scope-strip top
    constexpr double kBombW      = (347.0 - 13.0) / 360.0 * kDesignW;  // ≈ 556.67
    constexpr double kBombH      = kNoseTipY - kBombTop;               // ≈ 949.38
    constexpr double kAspect     = kBombW / kBombH;                    // ≈ 0.586
    constexpr int    kMinWidth   = 360;
    constexpr int    kMaxWidth   = 900;

    setSize(static_cast<int>(kBombW), static_cast<int>(kBombH));
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
            //
            // NOTE: settings key versioned -v2 after the 9:16 aspect lock
            // landed (2026-05-17) — pre-v2 widths were stored against the
            // old 820×920 (aspect 0.89) and would land the window comically
            // tall under the new constrainer. Ignore them — fall through to
            // fit-to-display on first launch post-update.
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
            {
                if (auto* props = holder->settings.get())
                {
                    const int saved = props->getIntValue("bombo-editor-width-v3", -1);
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
                const double scale = std::min(padW / kBombW, padH / kBombH);
                w = static_cast<int>(std::round(kBombW * scale));
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

            // Second MIDI-enable pass: callAsync runs after the message loop
            // is idle, guaranteeing StandalonePluginHolder is fully up.
            // visibilityChanged() fires first but may race on some platforms.
            if (auto* holder = juce::StandalonePluginHolder::getInstance())
            {
                auto& dm = holder->deviceManager;
                for (const auto& dev : juce::MidiInput::getAvailableDevices())
                    dm.setMidiInputDeviceEnabled(dev.identifier, true);
            }
           #endif
        });

}

BomboEditor::~BomboEditor()
{
    setLookAndFeel(nullptr);
}

void BomboEditor::paint(juce::Graphics& g)
{
    // Marketing-screenshot mode: when BOMBO_SOLID_BG is set, paint a solid
    // brand-dark backdrop behind the chassis so the transparent corners
    // (where the bomb silhouette tapers) capture cleanly instead of bleeding
    // through to whatever's behind the standalone window. No-op for normal
    // hosted use — plugins keep their transparent corners so DAW skins read.
    if (std::getenv("BOMBO_SOLID_BG") != nullptr)
    {
        g.fillAll(juce::Colour(0xFF14161B));
    }
    else
    {
        // Transparent — the faceplate's chassisPath_ fill covers everything
        // inside the bomb shape; corner wedges stay alpha=0.
        g.fillAll(juce::Colours::transparentBlack);
    }
    paintGlitchOverlay(g);
}

void BomboEditor::paintOverChildren(juce::Graphics& g)
{
    const float scale = faceplate.getTransform().mat00;
    if (scale <= 0.0f) return;

    // Full transform: scale then faceplate position offset (bakes in bomb crop).
    const auto fullT = faceplate.getTransform()
                           .translated ((float) faceplate.getX(),
                                        (float) faceplate.getY());

    // Outline-only export mode (BOMBO_OUTLINE_ONLY=1): paint over every child
    // and render just the full bomb silhouette (cap + fins + body union) as a
    // flat white shape on black — a clean control image for external (GenAI)
    // texture generation. No rack, knobs, text, or theme colour.
    if (std::getenv("BOMBO_OUTLINE_ONLY") != nullptr)
    {
        g.fillAll (juce::Colours::black);
        juce::Graphics::ScopedSaveState ss (g);
        g.addTransform (fullT);
        g.setColour (juce::Colours::white);
        g.fillPath (faceplate.getCapPath());
        g.fillPath (faceplate.getFinPathL());
        g.fillPath (faceplate.getFinPathR());
        g.fillPath (faceplate.getChassisPath());
        return;
    }

    // Rack and chassis bottom in editor (window) coords.
    auto rack = faceplate.getRackBounds().toFloat().transformedBy (fullT);
    const float chassisBot =
        faceplate.getChassisRectArea().toFloat().transformedBy (fullT).getBottom();
    rack = rack.withBottom (juce::jmin (rack.getBottom(), chassisBot));
    if (rack.isEmpty()) return;

    // ── Hull re-paint (design-space coords via addTransform) ──────────
    // Re-draw the bomb hull (cap, fins, chassis gradient) on top of any
    // rack children or BBS content that painted into the hull wall region
    // (inside bomb silhouette but outside the chassis interior rectangle).
    // Uses the exact same draw calls and theme colours as FaceplatePanel —
    // no hardcoded colours; no solid-black corners; works across all themes.
    {
        juce::Graphics::ScopedSaveState ss (g);

        // Don't repaint the hull over a live tooltip. tooltipWindow_ is a child
        // of the editor, so the hull re-paint below would otherwise cover any
        // tooltip that pops up over the bomb body. Exclude its bounds (window
        // coords, before the design-space transform) so the tip stays visible.
        if (tooltipWindow_.isVisible() && ! tooltipWindow_.getBounds().isEmpty())
            g.excludeClipRegion (tooltipWindow_.getBounds());

        g.addTransform (fullT); // switch to design-space coords (scale + crop offset)

        const bombo::chassisRenderer::Ctx ctx {
            faceplate.getChassisPath(),
            faceplate.getCapPath(),
            faceplate.getFinPathL(),
            faceplate.getFinPathR(),
            faceplate.getChassisRectArea(),
            faceplate.getChassisApexY(),
            faceplate.getRedRegionTopY(),
            faceplate.getWidth(),
            faceplate.getHeight()
        };

        // Clip to hull exterior: inside bomb silhouette, outside the rack
        // content area, and outside the content above the rack (scope, preset
        // bar, macro row, header). Two exclusions:
        //   1. Everything above the rack — prevents painting over scope/header
        //   2. The rack section area itself — preserves the knob content
        // What remains: side margins at rack Y, plus the nose cone.
        auto rackExclude = faceplate.getRackBounds();
        rackExclude.setBottom (juce::jmin (rackExclude.getBottom(),
                                           faceplate.getChassisRectArea().getBottom()));
        const juce::Rectangle<int> aboveRack { 0, 0,
                                               faceplate.getWidth(),
                                               rackExclude.getY() };
        g.reduceClipRegion (faceplate.getChassisPath());
        g.excludeClipRegion (aboveRack);    // #1: header/scope/preset/macro
        g.excludeClipRegion (rackExclude);  // #2: rack knob content
        // #3: nose — macro controls live below the chassis rect; don't
        // repaint drawChassis() over them or they disappear.
        {
            const int noseY = rackExclude.getBottom();
            g.excludeClipRegion ({ 0, noseY,
                                   faceplate.getWidth(),
                                   faceplate.getHeight() - noseY });
        }

        bombo::chassisRenderer::drawCapAndFins (g, ctx);
        bombo::chassisRenderer::drawChassis    (g, ctx);
    }

    // ── Frame (screen-space, clipped to bomb silhouette) ──────────────
    // Amber lip + inner bevel, hard-clipped to the bomb path.
    {
        juce::Graphics::ScopedSaveState ss (g);
        auto bombScreenPath = bombo::BombShape::buildBombPath (faceplate.getLocalBounds().toFloat());
        bombScreenPath.applyTransform (fullT);
        g.reduceClipRegion (bombScreenPath);

        // Amber inner lip — marks the edge of the cutout opening. Skipped on
        // neon themes: the accent-coloured body frame is the border there, and
        // a hardcoded amber lip clashed with the neon palette.
        if (! bombo::col::isNeon())
        {
            g.setColour (juce::Colour (0xFFFFB800).withAlpha (0.80f));
            g.drawRect (rack, 1.0f);
        }

        // Inner bevel — top shadow (far lip casts shadow down into the opening)
        const float bW = 5.0f * scale;
        {
            juce::ColourGradient cg (juce::Colours::black.withAlpha (0.48f),
                                      rack.getX(), rack.getY(),
                                      juce::Colours::transparentBlack,
                                      rack.getX(), rack.getY() + bW, false);
            g.setGradientFill (cg);
            g.fillRect (rack.withBottom (rack.getY() + bW));
        }
        // Inner bevel — left shadow
        {
            juce::ColourGradient cg (juce::Colours::black.withAlpha (0.30f),
                                      rack.getX(), rack.getY(),
                                      juce::Colours::transparentBlack,
                                      rack.getX() + bW, rack.getY(), false);
            g.setGradientFill (cg);
            g.fillRect (rack.withRight (rack.getX() + bW));
        }
        // Inner bevel — bottom highlight (ambient bounce)
        const float hW = 3.0f * scale;
        {
            juce::ColourGradient cg (juce::Colours::white.withAlpha (0.06f),
                                      rack.getX(), rack.getBottom(),
                                      juce::Colours::transparentBlack,
                                      rack.getX(), rack.getBottom() - hW, false);
            g.setGradientFill (cg);
            g.fillRect (rack.withTop (rack.getBottom() - hW));
        }
        // Inner bevel — right highlight
        {
            juce::ColourGradient cg (juce::Colours::white.withAlpha (0.04f),
                                      rack.getRight(), rack.getY(),
                                      juce::Colours::transparentBlack,
                                      rack.getRight() - hW, rack.getY(), false);
            g.setGradientFill (cg);
            g.fillRect (rack.withLeft (rack.getRight() - hW));
        }
    }
}

void BomboEditor::triggerGlitch(GlitchLevel level, int durationMs)
{
    glitchLevel_ = level;
    glitchStart_ = juce::Time::getCurrentTime();
    repaint();
    startTimer(durationMs);
}

void BomboEditor::timerCallback()
{
    stopTimer();
    glitchLevel_ = GlitchLevel::None;
    repaint();
}

void BomboEditor::paintGlitchOverlay(juce::Graphics& g)
{
    if (glitchLevel_ == GlitchLevel::None) return;
    const auto b = getLocalBounds();

    switch (glitchLevel_)
    {
        case GlitchLevel::Flicker:
            g.fillAll(juce::Colour(0xFFFFFFFFu).withAlpha(0.08f));
            break;

        case GlitchLevel::Garble:
        {
            juce::Random rng(static_cast<int64_t>(juce::Time::currentTimeMillis()));
            g.setColour(juce::Colour(0xFFC8FF8Cu).withAlpha(0.15f));
            for (int y = 0; y < b.getHeight(); y += 4)
                if (rng.nextBool())
                    g.fillRect(0, y, b.getWidth(), 2);
            break;
        }

        case GlitchLevel::BlackFlash:
            g.fillAll(juce::Colours::black.withAlpha(0.92f));
            break;

        case GlitchLevel::StaticNoise:
        {
            juce::Random rng(static_cast<int64_t>(juce::Time::currentTimeMillis() / 16));
            for (int y = 0; y < b.getHeight(); y += 2)
                for (int x = 0; x < b.getWidth(); x += 2)
                {
                    const float v = rng.nextFloat();
                    g.setColour(juce::Colour::fromHSV(0.0f, 0.0f, v, 0.85f));
                    g.fillRect(x, y, 2, 2);
                }
            break;
        }

        case GlitchLevel::RedFlash:
            g.fillAll(juce::Colour(0xFFFF2222u).withAlpha(0.55f));
            break;

        case GlitchLevel::GreenPulse:
            g.fillAll(juce::Colour(0xFFC8FF8Cu).withAlpha(0.35f));
            break;

        default: break;
    }
}

void BomboEditor::resized()
{
    // Faceplate paints in fixed 600×1066 design coordinates. The window is
    // cropped to the bomb's bounding box (fin outer tips to nose tip) so the
    // bomb fills the window edge-to-edge with no dead margins.
    // Scale is derived from the bomb bbox, not the full design canvas.
    // Faceplate is positioned with a negative offset so its bomb top-left
    // aligns with the window (0,0); faceplate.getTransform() stays as pure
    // scale so getTransform().mat00 == scale everywhere it is read.
    constexpr float kDesignW = 600.0f;
    constexpr float kDesignH = 1066.0f;
    // Bomb bounding box in design coords (R4B-CLASSIC, fin outer=13 ref,
    // cap top=22 ref, fin outer right=347 ref, nose tip=600 ref)
    constexpr float kBombL = 13.0f / 360.0f * kDesignW;    // ≈ 21.67
    constexpr float kBombT = 50.0f;                                // scope-strip top; ~5px above green U-frame so it isn't clipped
    constexpr float kBombW = (347.0f - 13.0f) / 360.0f * kDesignW; // ≈ 556.67
    constexpr float kBombH = 600.0f / 640.0f * kDesignH - kBombT;  // nose tip − crop top ≈ 949.38
    const float scale = juce::jmin(static_cast<float>(getWidth())  / kBombW,
                                   static_cast<float>(getHeight()) / kBombH);
    if (scale <= 0.0f) return;
    faceplate.setTransform(juce::AffineTransform::scale(scale));
    // Negative position shifts faceplate so bomb left/top coincides with (0,0).
    faceplate.setBounds(juce::roundToInt(-kBombL * scale),
                        juce::roundToInt(-kBombT * scale),
                        static_cast<int>(kDesignW),
                        static_cast<int>(kDesignH));

    // Theme strip — bottom-right of editor, in the transparent gutter
    // beneath the chassis tip. Scales with the editor so it stays a
    // consistent visual weight at any window size.
    if (themeStrip_)
    {
        constexpr int kTileN = 6;
        const int stripW = kTileN * bombo::ThemeTileStrip::kTileSize
                         + (kTileN - 1) * bombo::ThemeTileStrip::kTileGap;
        const int stripH = bombo::ThemeTileStrip::kTileSize;
        const int rightMargin  = 12;
        const int bottomMargin = 14;
        themeStrip_->setBounds(getWidth()  - rightMargin  - stripW,
                                getHeight() - bottomMargin - stripH,
                                stripW, stripH);
    }

    // BBS overlay covers only the "square effects section" (the FX rack
    // columns). Extracted to updateBbsBounds() so the layout editor can
    // also call it when the rack moves without a full resized() cycle.
    updateBbsBounds();

    // Layout editor overlay shares the FACEPLATE's transformed bounds so
    // its mouse events land in faceplate-design coords (same as the
    // editable element rects in getEditableElements()).
    if (layoutEditor_)
    {
        layoutEditor_->setBounds(faceplate.getBounds());
        layoutEditor_->setTransform(faceplate.getTransform());
    }

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
                props->setValue("bombo-editor-width-v2", getWidth());
   #endif
}

void BomboEditor::visibilityChanged()
{
    if (! isVisible()) return;

    // BBS must never open automatically — always start in FX/synth view.
    bbs_.hide();
    grabKeyboardFocus();

   #if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
    {
        auto& dm = holder->deviceManager;

       #if JUCE_LINUX
        // Belt-and-suspenders: if JUCE opened JACK (possible when libjack.so
        // is on the path even with JUCE_JACK=0 suppressed in some edge builds)
        // or has no device at all, force ALSA. PipeWire's JACK emulation
        // registers a device object but never fires processBlock — ALSA via
        // pipewire-alsa is reliable on all PipeWire systems.
        // Also initialise with 48 kHz: DJ controllers and many PipeWire
        // configurations reject JUCE's 44100 default, causing open failure
        // and a silent fallback. 48000 is PipeWire's universal default.
        {
            const bool noDevice = (dm.getCurrentAudioDevice() == nullptr);
            const bool jackChosen = juce::String(dm.getCurrentAudioDeviceType())
                                        .containsIgnoreCase("JACK");
            if (noDevice || jackChosen)
            {
                dm.setCurrentAudioDeviceType("ALSA", true);
                juce::AudioDeviceManager::AudioDeviceSetup setup;
                dm.getAudioDeviceSetup(setup);
                if (setup.sampleRate < 1.0)
                    setup.sampleRate = 48000.0;
                setup.outputDeviceName = {};  // let JUCE pick the system default
                dm.setAudioDeviceSetup(setup, /*treatAsChosenDevice=*/true);
            }
        }
       #endif

        // Enable every available MIDI input. Moved here from the constructor
        // so StandalonePluginHolder is fully initialised on all platforms.
        // Persists in ~/.config/Bombo/Bombo.settings — only needs to succeed once.
        for (const auto& dev : juce::MidiInput::getAvailableDevices())
            dm.setMidiInputDeviceEnabled(dev.identifier, true);
    }
   #endif
}

bool BomboEditor::keyPressed(const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();

    // ── Alt+Q close ─────────────────────────────────────────────────
    // keyPressed only fires when the editor (or a descendant) has
    // keyboard focus — so the user's "if plugin is in focus" condition
    // is implicit. Standalone: closeButtonPressed() on the DocumentWindow
    // triggers JUCE's normal quit path. VST3 in a host: there's no
    // standard "ask the host to close my window" API, so we route through
    // the native peer's user-close handler — Bitwig recognises this on
    // Linux/X11 and dismisses the plugin chrome. Outside the editor's
    // focus the keypress never reaches here, so it bubbles up to Bitwig
    // which shows its usual "Quit Bitwig Studio?" prompt.
    if (mods.isAltDown() && ! mods.isCtrlDown() && ! mods.isShiftDown()
        && (key.getKeyCode() == 'Q' || key.getTextCharacter() == 'q'))
    {
        if (auto* tlc = getTopLevelComponent())
        {
            if (auto* dw = dynamic_cast<juce::DocumentWindow*>(tlc))
            {
                dw->closeButtonPressed();
            }
            else if (auto* peer = tlc->getPeer())
            {
                peer->handleUserClosingWindow();
            }
        }
        return true;
    }

    // When BBS is visible, route all keys through its handler regardless of
    // focus. JUCE only delivers keyPressed to the focused component, so if
    // the user clicks back on the faceplate while BBS is open, ESC / S / T /
    // N-P-F etc. would otherwise bypass BBS entirely. The Ctrl+Shift+*
    // dev shortcuts below stay handled here so they remain reachable when
    // BBS is dismissed via the in-BBS ESC handler. Modifier-bearing keys
    // (Ctrl+Shift+B/R/E) fall through to the editor's own handlers — BBS
    // doesn't claim them.
    if (bbs_.isVisible() && ! mods.isAnyModifierKeyDown()
        && key.getKeyCode() != juce::KeyPress::F2Key)
    {
        if (bbs_.keyPressed(key)) return true;
    }

    // ── Layout-edit mode toggle (F2 or Ctrl+Shift+E) ────────────────
    // Ports an earlier project's UX: enter edit mode, drag/resize widgets,
    // Layout.json persists. Exit with the same key or Esc-via-overlay.
    // BBS re-open (Ctrl+Shift+B) — available once unlocked; dev affordance
    // until the HeaderBar button ships.
    if (mods.isCtrlDown() && mods.isShiftDown() && key.getKeyCode() == 'B')
    {
        if (processorRef.persistentState().getBbsUnlocked())
            bbs_.show();
        return true;
    }

    // Reset BBS progression (Ctrl+Shift+R) — wipes firstEntryDone + unlocked
    // + level, so the 7-tap nose sequence is required again. Mirrors the
    // force-reset gesture path; intended for dev / re-testing.
    if (mods.isCtrlDown() && mods.isShiftDown() && key.getKeyCode() == 'R')
    {
        resetBbsProgression();
        return true;
    }

    const bool isF2     = (key.getKeyCode() == juce::KeyPress::F2Key);
    const bool isCtrlShiftE = mods.isCtrlDown() && mods.isShiftDown()
                              && key.getKeyCode() == 'E';
    if (isF2 || isCtrlShiftE)
    {
        if (layoutEditor_)
        {
            const bool turningOn = ! layoutEditor_->isEditMode();
            layoutEditor_->setEditMode(turningOn);
            if (turningOn)
                layoutEditor_->toFront(true);
            grabKeyboardFocus();
        }
        return true;
    }

    // Forward edit-mode hotkeys (Esc/L/Ctrl+Z/arrows/etc.) to the overlay.
    if (layoutEditor_ && layoutEditor_->isEditMode())
    {
        if (layoutEditor_->handleKey(key)) return true;
    }

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
    // D rolls the DICE (full randomize). Gated on !bbs_.isVisible() like preset
    // nav below: while the BBS/game is open the preset is the in-game "Pew"
    // shot sound and randomizing it would corrupt the game audio.
    if (ch == 'd' && ! bbs_.isVisible())
    {
        faceplate.rollDice();
        return true;
    }
    // K toggles key-tracking (KBTRK). Mirrors the header toggle.
    if (ch == 'k' && ! bbs_.isVisible())
    {
        processorRef.toggleKbtrk();
        return true;
    }

    // Preset navigation — left/up = prev, right/down = next, wraps around.
    // Disabled when BBS is open (its own arrow handling takes priority) or
    // when layout-edit mode is active (arrows move widgets there).
    if (!bbs_.isVisible() && !(layoutEditor_ && layoutEditor_->isEditMode())
        && !mods.isAnyModifierKeyDown())
    {
        const bool isPrev = (kc == juce::KeyPress::leftKey  || kc == juce::KeyPress::upKey);
        const bool isNext = (kc == juce::KeyPress::rightKey || kc == juce::KeyPress::downKey);
        if (isPrev || isNext)
        {
            auto& bank = processorRef.presetBank();
            if (!bank.empty())
            {
                // Use bank.prev()/next() which correctly handle currentIndex()==-1.
                // The manual (idx + delta + n) % n formula gave n-2 instead of
                // n-1 when no preset was selected and the user pressed prev.
                if (isNext) bank.next(processorRef.apvts);
                else        bank.prev(processorRef.apvts);
                faceplate.refreshPresetBar();
                return true;
            }
        }
    }

    return false;
}

void BomboEditor::updateBbsBounds()
{
    const float scale = faceplate.getTransform().mat00;
    if (scale <= 0.0f) return;

    // Full transform: scale + faceplate position offset (bakes in the bomb crop).
    const auto fullT = faceplate.getTransform()
                           .translated ((float) faceplate.getX(),
                                        (float) faceplate.getY());

    auto b = faceplate.getRackBounds()
                 .toFloat()
                 .transformedBy (fullT)
                 .toNearestInt();

    const int chassisBot = static_cast<int>(
        faceplate.getChassisRectArea().toFloat()
            .transformedBy (fullT)
            .getBottom());
    if (b.getBottom() > chassisBot)
        b.setBottom (chassisBot);

    bbs_.setBounds (b);

    // Build chassis path in BBS-local coords so BBSComponent::paint()
    // can clip itself to the bomb silhouette (top corners of the rack
    // rect extend outside the chassis egg-shape and leak through).
    {
        auto clip = faceplate.getChassisPath();
        clip.applyTransform (fullT);   // design → editor
        clip.applyTransform (juce::AffineTransform::translation (
            -(float) b.getX(), -(float) b.getY()));  // editor → BBS-local
        bbs_.setLocalChassisClip (clip);
    }
}

void BomboEditor::resetBbsProgression()
{
    processorRef.progressionManager().forceReset();
    faceplate.setNoseProgressionLevel(0);
    faceplate.setNoseFirstEntryDone(false);
    processorRef.persistentState().setBbsUnlocked(false);
    // 600 ms so it's hard to miss as confirmation feedback. Also nudge the
    // scope to fire its "tap 6/final" overlay so the user has two
    // independent visual signals the reset actually fired.
    triggerGlitch(GlitchLevel::GreenPulse, 600);
    faceplate.flashScopeResetConfirmation();
}

void BomboEditor::startBounceFlow(bombo::OfflineBouncer::Format format)
{
    // Drop re-clicks while a bounce is in flight — reassigning bouncer_
    // would stopThread(2000) on the message thread (UI freeze up to 2 s)
    // and let the prior completion lambda fire for a file the user
    // implicitly cancelled by re-clicking.
    if (bouncer_ != nullptr) return;

    const bool isWav = (format == bombo::OfflineBouncer::Format::Wav);
    const juce::String ext      = isWav ? ".wav" : ".aiff";
    const juce::String fmtLabel = isWav ? "WAV"  : "AIFF";
    const juce::String filter   = isWav ? "*.wav" : "*.aiff;*.aif";

    const juce::File startDir = processorRef.persistentState().getLastBounceDir();
    // Timestamped default name so successive bounces don't silently
    // overwrite each other; user can rename in the dialog.
    const juce::String stamp = juce::Time::getCurrentTime().formatted("%Y%m%d_%H%M%S");
    const juce::File   defaultFile = startDir.getChildFile("Bombo_" + stamp + ext);

    // parentComponent (this) anchors the dialog to the editor's window
    // owner so the OS spawns it on the same screen — without it, multi-
    // monitor setups land the chooser on whichever screen the OS thinks
    // is "active" (often the wrong one).
    bounceChooser_ = std::make_unique<juce::FileChooser>(
        "Bounce to " + fmtLabel,
        defaultFile,
        filter,
        /*useOSNativeDialogBox=*/ true,
        /*treatFilePackagesAsDirectories=*/ false,
        /*parentComponent=*/ this);

    const int flags = juce::FileBrowserComponent::saveMode
                    | juce::FileBrowserComponent::canSelectFiles
                    | juce::FileBrowserComponent::warnAboutOverwriting;

    bounceChooser_->launchAsync(
        flags,
        [this, format, ext](const juce::FileChooser& chooser)
        {
            juce::File chosen = chooser.getResult();
            if (chosen == juce::File()) return;   // user cancelled

            // Ensure the right extension regardless of what user typed.
            if (! chosen.hasFileExtension(ext))
                chosen = chosen.withFileExtension(ext);

            processorRef.persistentState().setLastBounceDir(chosen.getParentDirectory());

            bouncer_ = bombo::OfflineBouncer::startAsync(
                processorRef,
                chosen,
                format,
                [this](bool ok, juce::String message)
                {
                    if (ok)
                    {
                        // Tooltip-style toast — the FileChooser's parent
                        // window is already gone, so we use AlertWindow's
                        // async ok dialog. Keep terse.
                        juce::NativeMessageBox::showAsync(
                            juce::MessageBoxOptions()
                                .withIconType(juce::MessageBoxIconType::InfoIcon)
                                .withTitle("Bombo")
                                .withMessage("Bounced: " + message)
                                .withButton("OK"),
                            std::function<void(int)>{});
                    }
                    else
                    {
                        juce::NativeMessageBox::showAsync(
                            juce::MessageBoxOptions()
                                .withIconType(juce::MessageBoxIconType::WarningIcon)
                                .withTitle("Bombo - bounce failed")
                                .withMessage(message)
                                .withButton("OK"),
                            std::function<void(int)>{});
                    }
                    // Release the bouncer on the message thread after the
                    // user dismisses; keeping it alive briefly avoids a
                    // race where the worker thread is still unwinding.
                    bouncer_.reset();
                });
        });
}
