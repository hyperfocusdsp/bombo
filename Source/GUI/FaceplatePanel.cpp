#include "FaceplatePanel.h"

#include "BalanceFader.h"
#include "BombShape.h"
#include "BpmDisplay.h"
#include "ChassisRenderer.h"
#include "Colours.h"
#include "DiceButton.h"
#include "Fonts.h"
#include "HeaderRenderer.h"
#include "SampleSlotWidget.h"
#include "WaveBuffer.h"
#include "../ParameterIds.h"

namespace bombo
{
namespace
{
// ── Chassis & band geometry ─────────────────────────────────────────
// The chassis is a single polygon: rounded-top rectangle with a tapered
// bottom (bomb tail). Everything outside the chassis paints `col::ink`
// so the JUCE resize-corner has a dark, unambiguous background.
// Chassis is now flush with the window on the sides + top. The only
// "outside chassis" region is the bottom-left + bottom-right wedges
// formed by the bomb-tail V — and the backdrop colour matches the
// chassis interior so those wedges are invisible at the edge. The JUCE
// resize-corner widget paints its own stripes against this backdrop in
// the bottom-right wedge.
constexpr int kChassisMarginX   = 0;
constexpr int kChassisMarginTop = 0;
constexpr int kChassisMarginBot = 4;
constexpr int kChassisCornerR   = 0;
constexpr int kTailH            = 160;

// Bands inside the chassis rect portion. The macro-row strip is gone
// 2026-05-17 — macros live in the bomb's nose (see layoutMacrosInNose).
constexpr int kHeaderH      = 50;
constexpr int kScopeH       = 100;
constexpr int kRackPadX     = 2;        // padding between chassis edge and column 0/N-1
constexpr int kRackTopGap   = 22;      // accommodates the A↔B balance fader strip above VOICE A/B
constexpr int kRackBotGap   = 4;
constexpr int kBalFaderH    = 16;      // horizontal balance fader height

// FX columns.
constexpr int kColGap       = 0;       // colors butt up; the per-FX accent IS the separator (2026-05-17)
constexpr int kColTitleH    = 18;      // was 20
constexpr int kColAccentH   = 3;
constexpr int kModuleIdH    = 12;      // was 14
constexpr int kInnerPadX    = 1;       // was 3 — eliminates inter-column gap
constexpr int kRowH         = 68;      // tight rows after squaring knobs: knob
                                       // box scales with min(kColW, rowH-label-1)
                                       // so bumping kColW + kRowH together gives
                                       // bigger rack knobs without the old gap.
constexpr int kKnobLabelH   = 13;
constexpr int kNCols        = 7;
} // namespace

// ────────────────────────────────────────────────────────────────────
//  Construction
// ────────────────────────────────────────────────────────────────────

FaceplatePanel::~FaceplatePanel() = default;

FaceplatePanel::Control::Control()  = default;
FaceplatePanel::Control::~Control() = default;

void FaceplatePanel::setPresetBank(PresetBank& bank)
{
    if (presetBar_ != nullptr) return;  // idempotent
    presetBar_ = std::make_unique<PresetBarComponent>(bank, apvts_);
    addAndMakeVisible(*presetBar_);
    if (! getBounds().isEmpty()) resized();
}

FaceplatePanel::FaceplatePanel(juce::AudioProcessorValueTreeState& apvts,
                               const WaveBuffer* waveBuffer,
                               SampleSlotCallbacks sampleSlotCb,
                               HostBpmFn hostBpmFn,
                               RandomizeFn randomizeCb,
                               BounceFn bounceWavCb,
                               BounceFn bounceAiffCb)
    : apvts_(apvts),
      sampleSlotCb_(std::move(sampleSlotCb))
{
    setOpaque(false);
    addAndMakeVisible(scope_);
    scope_.setWaveBuffer(waveBuffer);

    // ── 7 FX columns — order matches the pre-port reference ─────────
    {
        Section s;
        s.name = "VOICE A"; s.moduleId = "AMP-1"; s.mutePid = pid::voiceAMute;
        s.accent = col::voice(); s.labelOnBg = col::bone();
        addChoice(s, pid::waveform,   "WAVE",   s.labelOnBg);
        addKnob  (s, pid::pitchStart, "PITCH",  s.labelOnBg);
        addKnob  (s, pid::pitchEnd,   "END",    s.labelOnBg);
        addKnob  (s, pid::pitchDecay, "P.DEC",  s.labelOnBg);
        addKnob  (s, pid::pitchCurve, "CURVE",  s.labelOnBg);
        // SUB HPF — one-pole high-pass on the SUB layer only. Carves muddy
        // ultra-lows for tight psytrance/hard-techno kicks. 20 Hz = bypass.
        addKnob  (s, pid::subHpf,     "SUB HP", s.labelOnBg);
        sections_.push_back(std::move(s));
    }
    {
        Section s;
        s.name = "VOICE B"; s.moduleId = "OSS-1"; s.mutePid = pid::voiceBMute;
        s.accent = col::voice(); s.labelOnBg = col::bone();
        addKnob      (s, pid::ampAttack,   "ATK",    s.labelOnBg);
        addKnob      (s, pid::ampDecay,    "DEC",    s.labelOnBg);
        addKnob      (s, pid::clickAmount, "CLICK",  s.labelOnBg);
        addKnob      (s, pid::noiseAmount, "BODY",   s.labelOnBg);
        addKnob      (s, pid::noiseColor,  "COLOR",  s.labelOnBg);
        addSampleSlot(s,                   "SAMPLE", s.labelOnBg);
        sections_.push_back(std::move(s));
    }
    {
        Section s;
        s.name = "DRIVE"; s.moduleId = "SAT-A"; s.mutePid = pid::driveMute;
        s.accent = col::drive(); s.labelOnBg = col::ink();
        addKnob  (s, pid::driveAmount,   "V.AMT",  s.labelOnBg);
        addChoice(s, pid::driveMode,     "V.MODE", s.labelOnBg);
        addKnob  (s, pid::fxDriveAmount, "B.AMT",  s.labelOnBg);
        addChoice(s, pid::fxDriveMode,   "B.MODE", s.labelOnBg);
        addKnob  (s, pid::fxDriveMix,    "MIX",    s.labelOnBg);
        sections_.push_back(std::move(s));
    }
    {
        Section s;
        s.name = "DELAY"; s.moduleId = "DLY-1"; s.mutePid = pid::delayMute;
        s.accent = col::delayC(); s.labelOnBg = col::ink();
        addKnob(s, pid::delayTime,     "TIME",  s.labelOnBg);
        addKnob(s, pid::delayFeedback, "FBK",   s.labelOnBg);
        addChoice(s, pid::delayTimeMode, "SYNC", s.labelOnBg);
        addKnob(s, pid::delayMorph,    "TONE",  s.labelOnBg);
        addKnob(s, pid::delayMix,      "MIX",   s.labelOnBg);
        sections_.push_back(std::move(s));
    }
    {
        Section s;
        s.name = "REVERB"; s.moduleId = "RVB-FDN"; s.mutePid = pid::reverbMute;
        s.accent = col::reverb(); s.labelOnBg = col::ink();
        addKnob(s, pid::reverbSize,      "SIZE",  s.labelOnBg);
        addKnob(s, pid::reverbDecay,     "DECAY", s.labelOnBg);
        addKnob(s, pid::reverbDamp,      "DAMP",  s.labelOnBg);
        addKnob(s, pid::reverbDiffusion, "DIFF",  s.labelOnBg);
        addKnob(s, pid::reverbPredelay,  "PRE",   s.labelOnBg);
        addKnob(s, pid::reverbMix,       "MIX",   s.labelOnBg);
        sections_.push_back(std::move(s));
    }
    {
        Section s;
        s.name = "FILTER"; s.moduleId = "SVF-2P"; s.mutePid = pid::filterMute;
        s.accent = col::filterC(); s.labelOnBg = col::ink();
        addKnob(s, pid::filterHp,    "HP",    s.labelOnBg);
        addKnob(s, pid::filterHpQ,   "HP Q",  s.labelOnBg);
        addKnob(s, pid::filterLp,    "LP",    s.labelOnBg);
        addKnob(s, pid::filterLpQ,   "LP Q",  s.labelOnBg);
        addKnob(s, pid::filterColor, "COLOR", s.labelOnBg);
        sections_.push_back(std::move(s));
    }
    {
        Section s;
        s.name = "DUCK"; s.moduleId = "SC-CMP"; s.mutePid = pid::duckMute;
        s.accent = col::duck(); s.labelOnBg = col::ink();
        addKnob(s, pid::duckAtk,   "ATK",   s.labelOnBg);
        addKnob(s, pid::duckHold,  "HOLD",  s.labelOnBg);
        addKnob(s, pid::duckRel,   "REL",   s.labelOnBg);
        addKnob(s, pid::duckDepth, "DEPTH", s.labelOnBg);
        sections_.push_back(std::move(s));
    }

    // ── Macro row ───────────────────────────────────────────────────
    // Macros live in the red nose region — labels must sit on a saturated
    // red background, so use col::bone() (cream-white) not boneDim for
    // legibility. OUT gets the same treatment plus a fonts::label bump
    // applied in layoutMacrosInNose() so it reads as the hero.
    macro_[0] = makePlaceholderKnob("PITCH",  col::bone());
    macro_[1] = makePlaceholderKnob("DECAY",  col::bone());
    macro_[2] = makePlaceholderKnob("PUNCH",  col::bone());
    macro_[3] = makePlaceholderKnob("WEIGHT", col::bone());
    macro_[4] = makePlaceholderKnob("MOOD",   col::bone());
    macro_[5] = makePlaceholderKnob("SPACE",  col::bone());
    macro_[6] = makeBoundKnob(pid::masterOut, "OUT", col::accentAmber(), col::bone());
    for (auto& m : macro_)
    {
        if (m == nullptr) continue;
        if (m->slider) addAndMakeVisible(*m->slider);
        if (m->label)  addAndMakeVisible(*m->label);
    }
    wireMacroFanOut();

    // ── LIM pill ────────────────────────────────────────────────────
    limPill_ = std::make_unique<juce::ToggleButton>("LIM");
    limPill_->setColour(juce::ToggleButton::textColourId, col::bone());
    limPill_->setWantsKeyboardFocus(false);
    limPill_->setMouseClickGrabsKeyboardFocus(false);
    limAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts_, pid::limiterOn, *limPill_);
    addAndMakeVisible(*limPill_);

    // ── Tail-kill toggle (small "TAIL" pill) ────────────────────────
    // ON (default) = trim delay+reverb tails between trigs and on loop
    // off. OFF = let tails ring naturally past the last trig.
    tailPill_ = std::make_unique<juce::ToggleButton>("TAIL");
    tailPill_->setColour(juce::ToggleButton::textColourId, col::bone());
    tailPill_->setWantsKeyboardFocus(false);
    tailPill_->setMouseClickGrabsKeyboardFocus(false);
    tailAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts_, pid::tailKillOn, *tailPill_);
    addAndMakeVisible(*tailPill_);

    // ── Loop toggle (small "↻" pill) ────────────────────────────────
    loopBtn_ = std::make_unique<juce::ToggleButton>("LOOP");
    loopBtn_->setColour(juce::ToggleButton::textColourId, col::bone());
    loopBtn_->setWantsKeyboardFocus(false);
    loopBtn_->setMouseClickGrabsKeyboardFocus(false);
    loopAtt_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts_, pid::loopOn, *loopBtn_);
    addAndMakeVisible(*loopBtn_);

    // ── BPM display (replaces the old MNT pill slot) ───────────────
    bpmDisplay_ = std::make_unique<BpmDisplay>(
        apvts_, pid::bpm, std::move(hostBpmFn));
    addAndMakeVisible(*bpmDisplay_);

    // ── Voice A ↔ Voice B balance fader (between columns 0 and 1) ──
    balanceFader_ = std::make_unique<BalanceFader>(apvts_, pid::voiceBalance);
    addAndMakeVisible(*balanceFader_);

    // ── DICE button (full randomize) ───────────────────────────────
    diceButton_ = std::make_unique<DiceButton>();
    diceButton_->onClick = std::move(randomizeCb);
    addAndMakeVisible(*diceButton_);

    // ── BNC WAV / BNC AIF pills (offline bounce) ───────────────────
    // Momentary TextButtons (not APVTS-attached). Editor wires onClick
    // to a FileChooser + OfflineBouncer flow. Callback may be empty in
    // headless/test contexts — guard so the click is a no-op.
    bncWavPill_ = std::make_unique<juce::TextButton>("BNC WAV");
    bncWavPill_->setColour(juce::TextButton::textColourOnId,  col::bone());
    bncWavPill_->setColour(juce::TextButton::textColourOffId, col::bone());
    bncWavPill_->setTooltip("Bounce the current sound to a .wav file");
    bncWavPill_->setWantsKeyboardFocus(false);
    bncWavPill_->setMouseClickGrabsKeyboardFocus(false);
    bncWavPill_->onClick = [cb = std::move(bounceWavCb)] { if (cb) cb(); };
    addAndMakeVisible(*bncWavPill_);

    bncAifPill_ = std::make_unique<juce::TextButton>("BNC AIF");
    bncAifPill_->setColour(juce::TextButton::textColourOnId,  col::bone());
    bncAifPill_->setColour(juce::TextButton::textColourOffId, col::bone());
    bncAifPill_->setTooltip("Bounce the current sound to an .aiff file");
    bncAifPill_->setWantsKeyboardFocus(false);
    bncAifPill_->setMouseClickGrabsKeyboardFocus(false);
    bncAifPill_->onClick = [cb = std::move(bounceAiffCb)] { if (cb) cb(); };
    addAndMakeVisible(*bncAifPill_);

    // ── Nose overlay ────────────────────────────────────────────────
    // Transparent component over the nose region; handles 7-tap
    // activation sequence and glitch visuals.
    addChildComponent(noseOverlay_);
    noseOverlay_.setVisible(true);
    noseOverlay_.onActivationComplete = [this] { if (onNoseActivated) onNoseActivated(); };
    noseOverlay_.onGlitchTap = [this](int tap)
    {
        scope_.showTapWarning(tap);
        if (onNoseGlitchTap) onNoseGlitchTap(tap);
    };
    // Click-through over the macro cluster: the nose overlay covers the
    // OUT hero + 6 satellite knobs. Without this, every macro click is
    // swallowed by the overlay's 7-tap/reopen handler. Lambda receives a
    // point in noseOverlay_ local coords; we translate to FaceplatePanel
    // coords (the macro sliders' parent) before testing each slider's bounds.
    noseOverlay_.macroHitTester = [this](juce::Point<int> p) -> bool
    {
        const juce::Point<int> pFace = p + noseOverlay_.getBounds().getPosition();
        for (const auto& m : macro_)
            if (m && m->slider && m->slider->getBounds().contains(pFace))
                return true;
        return false;
    };
}

FaceplatePanel::Control*
FaceplatePanel::addKnob(Section& s, const juce::String& paramId,
                        const juce::String& displayName, juce::Colour labelColour)
{
    auto c = std::make_unique<Control>();
    c->kind = CtlKind::Knob;
    c->slider = std::make_unique<juce::Slider>();
    c->slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c->slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);
    c->slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    c->slider->setColour(juce::Slider::rotarySliderOutlineColourId, col::knobCap());
    c->slider->setWantsKeyboardFocus(false);
    c->slider->setMouseClickGrabsKeyboardFocus(false);
    c->label = std::make_unique<juce::Label>();
    c->label->setText(displayName, juce::dontSendNotification);
    c->label->setJustificationType(juce::Justification::centred);
    c->label->setFont(fonts::label(10.0f));
    c->label->setColour(juce::Label::textColourId, labelColour);
    c->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts_, paramId, *c->slider);
    addAndMakeVisible(*c->slider);
    addAndMakeVisible(*c->label);
    Control* raw = c.get();
    s.controls.push_back(std::move(c));
    return raw;
}

FaceplatePanel::Control*
FaceplatePanel::addChoice(Section& s, const juce::String& paramId,
                          const juce::String& displayName, juce::Colour labelColour)
{
    // Choice parameters render as discrete-stepped rotary knobs (integer
    // setRange + numChoices/choiceNames hints picked up by BomboLookAndFeel).
    // The SliderAttachment round-trips fine with AudioParameterChoice — the
    // parameter is already a stepped float internally.
    auto c = std::make_unique<Control>();
    c->kind = CtlKind::Knob;
    c->slider = std::make_unique<juce::Slider>();
    c->slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c->slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);
    c->slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    c->slider->setColour(juce::Slider::rotarySliderOutlineColourId, col::knobCap());
    c->slider->setWantsKeyboardFocus(false);
    c->slider->setMouseClickGrabsKeyboardFocus(false);

    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts_.getParameter(paramId)))
    {
        const int n = p->choices.size();
        if (n > 0)
        {
            c->slider->setRange(0.0, static_cast<double>(n - 1), 1.0);
            juce::Array<juce::var> arr;
            for (const auto& choice : p->choices) arr.add(choice);
            c->slider->getProperties().set("numChoices", n);
            c->slider->getProperties().set("choiceNames", arr);
        }
    }

    c->label = std::make_unique<juce::Label>();
    c->label->setText(displayName, juce::dontSendNotification);
    c->label->setJustificationType(juce::Justification::centred);
    c->label->setFont(fonts::label(10.0f));
    c->label->setColour(juce::Label::textColourId, labelColour);
    c->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts_, paramId, *c->slider);
    addAndMakeVisible(*c->slider);
    addAndMakeVisible(*c->label);
    Control* raw = c.get();
    s.controls.push_back(std::move(c));
    return raw;
}

FaceplatePanel::Control*
FaceplatePanel::addSampleSlot(Section& s, const juce::String& displayName,
                              juce::Colour labelColour)
{
    auto c = std::make_unique<Control>();
    c->kind = CtlKind::SampleSlot;
    c->sampleSlot = std::make_unique<SampleSlotWidget>();

    // Forward callbacks straight from the slot widget to whatever the
    // editor wired in. Owner is responsible for keeping the processor side
    // safe across editor open/close.
    c->sampleSlot->onBrowsePick    = sampleSlotCb_.onBrowsePick;
    c->sampleSlot->onIndexChange   = sampleSlotCb_.onIndexChange;
    c->sampleSlot->onClear         = sampleSlotCb_.onClear;
    c->sampleSlot->onLoadFactory   = sampleSlotCb_.onLoadFactory;
    c->sampleSlot->getNames        = sampleSlotCb_.getNames;
    c->sampleSlot->getCurrentIndex = sampleSlotCb_.getCurrentIdx;
    // Initial population — restore any state the processor already holds
    // (DAW session restore may have populated it before the editor opened).
    c->sampleSlot->refresh();

    c->label = std::make_unique<juce::Label>();
    c->label->setText(displayName, juce::dontSendNotification);
    c->label->setJustificationType(juce::Justification::centred);
    c->label->setFont(fonts::label(10.0f));
    c->label->setColour(juce::Label::textColourId, labelColour);
    addAndMakeVisible(*c->sampleSlot);
    addAndMakeVisible(*c->label);
    Control* raw = c.get();
    s.controls.push_back(std::move(c));
    return raw;
}

std::unique_ptr<FaceplatePanel::Control>
FaceplatePanel::makeBoundKnob(const juce::String& paramId,
                              const juce::String& displayName,
                              juce::Colour capColour,
                              juce::Colour labelColour)
{
    auto c = std::make_unique<Control>();
    c->kind = CtlKind::Knob;
    c->slider = std::make_unique<juce::Slider>();
    c->slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c->slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);
    c->slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    c->slider->setColour(juce::Slider::rotarySliderOutlineColourId, capColour);
    c->slider->setWantsKeyboardFocus(false);
    c->slider->setMouseClickGrabsKeyboardFocus(false);
    c->label = std::make_unique<juce::Label>();
    c->label->setText(displayName, juce::dontSendNotification);
    c->label->setJustificationType(juce::Justification::centred);
    c->label->setFont(fonts::label(9.5f));
    c->label->setColour(juce::Label::textColourId, labelColour);
    c->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts_, paramId, *c->slider);
    return c;
}

std::unique_ptr<FaceplatePanel::Control>
FaceplatePanel::makePlaceholderKnob(const juce::String& displayName,
                                    juce::Colour labelColour)
{
    auto c = std::make_unique<Control>();
    c->kind = CtlKind::Knob;
    c->slider = std::make_unique<juce::Slider>();
    c->slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c->slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);
    c->slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    c->slider->setColour(juce::Slider::rotarySliderOutlineColourId, col::knobCap());
    c->slider->setRange(0.0, 1.0, 0.0);
    c->slider->setValue(0.5, juce::dontSendNotification);
    c->slider->setNumDecimalPlacesToDisplay(2);
    c->slider->setWantsKeyboardFocus(false);
    c->slider->setMouseClickGrabsKeyboardFocus(false);
    c->label = std::make_unique<juce::Label>();
    c->label->setText(displayName, juce::dontSendNotification);
    c->label->setJustificationType(juce::Justification::centred);
    c->label->setFont(fonts::label(9.5f));
    c->label->setColour(juce::Label::textColourId, labelColour);
    return c;
}

// ────────────────────────────────────────────────────────────────────
//  Paint
// ────────────────────────────────────────────────────────────────────

void FaceplatePanel::paint(juce::Graphics& g)
{
    // Chassis + nose + band painted via the stateless ChassisRenderer
    // (extracted 2026-05-17). Paint order is load-bearing: cap+fins must
    // sit BEHIND the body so the body silhouette hides their attachment
    // edges (matches tools/bombshape_gen.py).
    const chassisRenderer::Ctx ctx{
        chassisPath_, capPath_, finPathL_, finPathR_,
        chassisRectArea_, chassisApexY_, redRegionTopY_,
        getWidth(), getHeight()
    };
    chassisRenderer::drawBackground(g);
    chassisRenderer::drawCapAndFins(g, ctx);
    chassisRenderer::drawChassis(g, ctx);
    chassisRenderer::drawRedRegion(g, ctx);
    // Yellow BOMBO-TEC cartouche band removed 2026-05-17 — the strip is
    // now the preset bar's home (PresetBarComponent paints itself there).

    headerRenderer::draw(g, headerBounds_, chassisPath_);
    paintScopeFrame(g);    // red U-border around scope, drawn under scope component

    // Rack painting is hard-clipped to the chassis silhouette via an
    // alpha-image mask (pre-rasterised in resized()). Path-based clips
    // were leaking at the curves — VOICE A / DUCK outer corners poked
    // through the bone outline. The image-mask form is pixel-perfect.
    // The per-column rounded corners (paintSection's roundLeft/
    // roundRight) shape the visible edge; the mask enforces it.
    juce::Graphics::ScopedSaveState save(g);
    if (chassisMask_.isValid())
        g.reduceClipRegion(chassisMask_, juce::AffineTransform());
    else
        g.reduceClipRegion(chassisPath_);
    const int lastIdx = static_cast<int>(sections_.size()) - 1;
    for (int i = 0; i < static_cast<int>(sections_.size()); ++i)
    {
        const bool roundLeft  = (i == 0);
        const bool roundRight = (i == lastIdx);
        paintSection(g, sections_[i], roundLeft, roundRight);
    }
}

void FaceplatePanel::paintScopeFrame(juce::Graphics& g)
{
    if (scopeBounds_.isEmpty()) return;

    // Match the fin/nose red so the scope reads as attached to the
    // rear-cap assembly. U-shape: top + left + right borders ONLY (no
    // bottom — the bottom opens into the macro row).
    constexpr int kBorder = 3;
    g.setColour(col::noseRed());

    const int x  = scopeBounds_.getX() - kBorder;
    const int y  = scopeBounds_.getY() - kBorder;
    const int w  = scopeBounds_.getWidth()  + 2 * kBorder;
    const int hF = scopeBounds_.getHeight() + kBorder;  // covers top + sides, not bottom

    // Top edge
    g.fillRect(x, y, w, kBorder);
    // Left edge
    g.fillRect(x, y, kBorder, hF);
    // Right edge
    g.fillRect(x + w - kBorder, y, kBorder, hF);
}

void FaceplatePanel::paintSection(juce::Graphics& g, const Section& s,
                                  bool roundLeft, bool roundRight)
{
    if (s.rectBounds.isEmpty()) return;

    const auto rb = s.rectBounds.toFloat();
    const bool muted = isMuted(s);

    // VAULT direction round 3 (2026-05-17): the FX columns get FULL
    // accent-colored bodies again — the colors themselves act as the
    // separators (no kColGap, no outline). Body olive shows on the LEFT
    // and RIGHT of the entire rack strip; the rack reads as a band of
    // industrial sticker labels glued onto the bomb. Muted columns
    // desaturate to communicate bypass without losing column identity.
    //
    // Outer columns get their outer corners rounded (2026-05-17) so
    // VOICE A's outer-left + DUCK's outer-right curves blend into the
    // bomb silhouette instead of poking square corners through it.
    const auto bodyColour = muted
        ? s.accent.withSaturation(0.20f).withBrightness(s.accent.getBrightness() * 0.55f)
        : s.accent;

    constexpr float kCorner = 24.0f;
    auto buildShape = [&](float yTop, float h)
    {
        juce::Path p;
        p.addRoundedRectangle(rb.getX(), yTop,
                              rb.getWidth(), h,
                              kCorner, kCorner,
                              roundLeft,  roundRight,   // top-left, top-right
                              roundLeft,  roundRight);  // bot-left, bot-right
        return p;
    };

    // Clip everything else (gloss + accent strip + title bar) to the
    // same rounded body shape so the outer-column rounded corners stay
    // crisp.
    juce::Graphics::ScopedSaveState save(g);
    const auto bodyPath = buildShape(rb.getY(), rb.getHeight());
    g.reduceClipRegion(bodyPath);

    g.setColour(bodyColour);
    g.fillPath(bodyPath);

    // Subtle top gloss for raised-panel feel — kept from the original
    // styling, looks good against both the body olive and the accent
    // column color.
    {
        juce::ColourGradient gloss(juce::Colours::white.withAlpha(0.07f),
                                   rb.getX(), rb.getY(),
                                   juce::Colours::transparentBlack,
                                   rb.getX(), rb.getY() + rb.getHeight() * 0.18f,
                                   false);
        g.setGradientFill(gloss);
        g.fillRect(rb.withHeight(rb.getHeight() * 0.18f));
    }

    // Title strip: 3 px accent tab + thin dark title bar with name text.
    {
        const auto accentStrip = juce::Rectangle<int>(s.rectBounds.getX(),
                                                      s.rectBounds.getY(),
                                                      s.rectBounds.getWidth(),
                                                      kColAccentH);
        const auto titleBar = juce::Rectangle<int>(s.rectBounds.getX(),
                                                   s.rectBounds.getY() + kColAccentH,
                                                   s.rectBounds.getWidth(),
                                                   kColTitleH - kColAccentH);
        // Accent strip — darker shade of the column accent (was full
        // brightness; now slightly darker so the tab + body don't merge).
        g.setColour(bodyColour.darker(0.45f));
        g.fillRect(accentStrip);
        g.setColour(muted ? col::graphite() : col::ink());
        g.fillRect(titleBar);
        g.setColour(muted ? col::boneDim() : col::bone());
        g.setFont(fonts::title(11.5f));
        g.drawText(s.name, titleBar, juce::Justification::centred);
        if (muted)
        {
            const float midY = titleBar.getCentreY();
            g.setColour(col::boneDim().withAlpha(0.55f));
            g.drawLine(titleBar.getX() + 8.0f, midY,
                       titleBar.getRight() - 8.0f, midY, 1.0f);
        }
    }
    // Module-ID strip REMOVED — saves ~12 px vertically per column.
    // No separator lines between columns either — the accent color IS
    // the separator (kColGap = 0 above).
}

// ────────────────────────────────────────────────────────────────────
//  Layout-editor element source
// ────────────────────────────────────────────────────────────────────

std::vector<LayoutElem> FaceplatePanel::getEditableElements() const
{
    // Major draggable blocks for the LayoutEditOverlay. Per-knob editing
    // is a follow-up; for now the user can drag/resize whole sections,
    // the macro row, scope strip, band, and balance fader.
    std::vector<LayoutElem> out;
    out.reserve(2 + sections_.size() + 2);

    out.push_back({ "macroRow",      macroBoundsTracked_, false, "macro_row"   });
    out.push_back({ "scopeStrip",    scopeBounds_,        false, "scope_strip" });
    out.push_back({ "band",          bandBoundsTracked_,  false, "band"        });
    if (presetBar_ != nullptr)
        out.push_back({ "presetBar", presetBarBounds_,    false, "macro_row"   });
    if (balanceFader_ != nullptr)
    {
        // Hit-box inflated vertically — the 16px tall fader is unreachable
        // in edit mode otherwise. First drag persists the inflated rect to
        // Layout.json, so the runtime fader grows to the same easier-to-grab
        // size. Horizontal is left as-is (already 88px between V.A/V.B).
        const auto raw = balanceFader_->getBounds();
        const int kInflateV = 8;  // 16 → 32 tall
        const juce::Rectangle<int> hit(raw.getX(),
                                       raw.getY() - kInflateV,
                                       raw.getWidth(),
                                       raw.getHeight() + 2 * kInflateV);
        out.push_back({ "balanceFader", hit, false, "rotary_knob" });
    }

    for (size_t i = 0; i < sections_.size(); ++i)
    {
        out.push_back({ "section." + sections_[i].name.replaceCharacter(' ', '_').toLowerCase(),
                        sections_[i].rectBounds, false, "section_column" });
    }
    return out;
}

// ────────────────────────────────────────────────────────────────────
//  Layout
// ────────────────────────────────────────────────────────────────────

void FaceplatePanel::resized()
{
    const int w = getWidth();
    const int h = getHeight();

    // Build the chassis path via the parametric Mini-Nuke silhouette
    // generator (locked R4B-CLASSIC 2026-05-17). The path is a single
    // continuous egg-shape from rear-cap area down through the bulge to
    // the rounded nose tip — no seam between body and "nose"; the red
    // region (drawn in paintRedRegion) is a clipped paint inside the
    // same silhouette. Body, cap, and fin paths are owned by FaceplatePanel
    // as members so paint() can reference them per-stage.
    const juce::Rectangle<float> boundsF(0.0f, 0.0f,
                                         static_cast<float>(w),
                                         static_cast<float>(h));
    chassisPath_   = bombo::BombShape::buildBombPath(boundsF);

    // Rasterise the chassis silhouette once into an alpha image. paint()
    // uses this as the rack clip — pixel-accurate, no curve seams.
    chassisMask_ = juce::Image(juce::Image::ARGB, juce::jmax(1, w), juce::jmax(1, h), true);
    {
        juce::Graphics maskG(chassisMask_);
        maskG.setColour(juce::Colours::white);
        maskG.fillPath(chassisPath_);
    }

    capPath_       = bombo::BombShape::buildCapPath(boundsF);
    finPathL_      = bombo::BombShape::buildFinPath(boundsF, -1);
    finPathR_      = bombo::BombShape::buildFinPath(boundsF, +1);
    bandRect_      = bombo::BombShape::bandRect(boundsF);
    redRegionTopY_ = bombo::BombShape::redRegionTopYInBounds(boundsF);

    // Band bounds — layout-override hook so the user can drag the
    // yellow cartouche around in edit mode.
    {
        const auto bandDefault = bandRect_.toNearestInt();
        const auto bandFinal   = layout_.boundsOr("band", bandDefault);
        bandRect_              = bandFinal.toFloat();
        bandBoundsTracked_     = bandFinal;
    }

    // ── Inscribed UI region — the band-bottom to red-region-top zone of
    //    the body interior. chassisRectArea_ used to be "rect above V-tail";
    //    now it's "the body interior the rack lives in." kRackBotGap of
    //    headroom above the red region so the bottom row doesn't kiss the
    //    nose split line. Width = body bulge width (the widest usable
    //    horizontal strip — narrows the rack OUTER bounds slightly so the
    //    rack columns sit inside the silhouette rather than overflowing it.
    const int chassisL = kChassisMarginX;
    const int chassisR = w - kChassisMarginX;
    const int chassisT = kChassisMarginTop;
    const int rackBotPad = 8;
    const int chassisRectBot = static_cast<int>(redRegionTopY_) - rackBotPad;
    const int chassisApexY   = static_cast<int>(redRegionTopY_) + 4;  // nose interior

    chassisRectArea_    = { chassisL, chassisT, chassisR - chassisL,
                            chassisRectBot - chassisT };
    chassisRectBottomY_ = chassisRectBot;
    chassisApexY_       = chassisApexY;

    // ── Bands inside the chassis rect ───────────────────────────────
    // Header + scope share a single width = fin-tip outer extent. This
    // gives the entire "rear assembly" (header → scope → fins) a single
    // unified outer rectangle. Olive body shows on the left + right of
    // the header → freed pixels available for future fin-area UI (user
    // direction 2026-05-17: "we can get to use the free space in the
    // fins and nose for new features").
    const bombo::BombShape::Params bp{};
    const float finTipTotalRefW = bp.bodyBulgeW + 2.0f * bp.finOutX;
    const float scaleX          = static_cast<float>(w) / bombo::BombShape::kRefW;
    const int   finTipW         = static_cast<int>(finTipTotalRefW * scaleX);
    const int   finTipX         = (w - finTipW) / 2;
    headerBounds_ = { finTipX, chassisT, finTipW, kHeaderH };

    // Scope strip inside its red U-frame, same finTipW as the header.
    constexpr int kScopeBorder = 3;
    const int scopeTop = chassisT + kHeaderH;
    {
        const juce::Rectangle<int> scopeDefault(finTipX + kScopeBorder,
                                                scopeTop + 8,
                                                finTipW - 2 * kScopeBorder,
                                                kScopeH - 16);
        scopeBounds_ = layout_.boundsOr("scopeStrip", scopeDefault);
        scope_.setBounds(scopeBounds_);
    }

    // Preset bar = the cartouche footprint, full stop. No extending
    // past the band edges into chassis-graphite territory (looked like
    // a grey overlap on top of the bomb). Buttons must fit INSIDE the
    // band rect — see PresetBarComponent::resized for the slim sizing.
    int macroTop = static_cast<int>(bandRect_.getBottom()) + 8;
    if (presetBar_ != nullptr)
    {
        const juce::Rectangle<int> barDefault = bandRect_.toNearestInt();
        presetBarBounds_ = layout_.boundsOr("presetBar", barDefault);
        presetBar_->setBounds(presetBarBounds_);
        macroTop = presetBarBounds_.getBottom() + 8;
    }
    // Macros now live in the bomb's nose (red region) rather than in a
    // horizontal strip above the rack. The nose interior runs from the
    // red-region top down to the chassis tip — NOT down to panel height
    // (which would include space below the rounded silhouette tip and
    // let the cluster's bottom satellite spill outside the bomb).
    const bombo::BombShape::Params bsParams{};
    const float tipYActual = bsParams.tipY * (static_cast<float>(h) / bombo::BombShape::kRefH);
    const int noseTopPad     = 8;
    const int noseBottomPad  = 6;
    const int noseTop = static_cast<int>(redRegionTopY_) + noseTopPad;
    const int noseBot = static_cast<int>(tipYActual) - noseBottomPad;
    const juce::Rectangle<int> noseInteriorDefault(chassisL, noseTop,
                                                   chassisR - chassisL,
                                                   juce::jmax(40, noseBot - noseTop));
    juce::Rectangle<int> noseInterior = layout_.boundsOr("macroRow", noseInteriorDefault);
    layoutMacrosInNose(noseInterior);
    noseOverlay_.setBounds(noseInterior);
    macroBoundsTracked_ = noseInterior;
    bandBoundsTracked_  = bandRect_.toNearestInt();

    // ── Rack columns ────────────────────────────────────────────────
    // Explicit column width (locked 2026-05-17 per user feedback): each
    // section is ~44 px wide so the rack reads as a compact strip with
    // body olive showing through on the sides — NOT a wall-to-wall rack
    // chewing the full chassis width. The total rack is centered
    // horizontally inside the chassis interior.
    constexpr int kColW      = 54;
    const int totalGap       = (kNCols - 1) * kColGap;
    const int totalUsed      = kColW * kNCols + totalGap;
    const int colW           = kColW;
    const int leftMargin     = chassisL + (chassisR - chassisL - totalUsed) / 2;

    // With macros gone from this strip, the rack starts directly below
    // the preset bar (with kRackTopGap above the VOICE A/B title strips
    // to keep room for the A↔B balance fader). macroTop is computed in
    // the preset-bar block above as presetBarBounds_.getBottom() + 8.
    const int rectTop = macroTop + kRackTopGap;

    // Uniform section height (reverted 2026-05-17 per user feedback after
    // b506649): all columns share the height of the tallest one for
    // visual balance. Shorter columns (DUCK = 4 knobs, V.A / DRV / DLY /
    // FLT = 5) leave empty olive-tinted slots below their last knob —
    // intentional placeholders for whichever extra knobs land per-column
    // to round everything out to 6 controls.
    constexpr int kRectBotPad = 4;
    int maxControls = 0;
    for (const auto& s : sections_)
        maxControls = juce::jmax(maxControls, static_cast<int>(s.controls.size()));
    const int rectH = kColTitleH + maxControls * kRowH + kRectBotPad;

    for (int i = 0; i < kNCols && i < static_cast<int>(sections_.size()); ++i)
    {
        auto& s = sections_[i];
        const int x = leftMargin + i * (colW + kColGap);
        // Default section bounds — overridable via LayoutManager.
        juce::Rectangle<int> defaultBounds(x, rectTop, colW, rectH);
        const juce::String secId = "section."
            + s.name.replaceCharacter(' ', '_').toLowerCase();
        s.rectBounds = layout_.boundsOr(secId, defaultBounds);
        s.titleBounds = { s.rectBounds.getX(),
                          s.rectBounds.getY(),
                          s.rectBounds.getWidth(),
                          kColTitleH };
        // moduleIdBounds kept for backward compat with the Section
        // struct + mouseDown hit-testing; paintSection omits its visual.
        s.moduleIdBounds = { s.rectBounds.getX(),
                             s.rectBounds.getBottom() - kModuleIdH,
                             s.rectBounds.getWidth(),
                             kModuleIdH };
        layoutSection(s);
    }

    // Balance fader — default position is in the kRackTopGap above
    // VOICE A + VOICE B title strips, but layout_.boundsOr lets the user
    // drag it anywhere in edit mode. Bug fix 2026-05-17: previously the
    // bounds were hard-applied, so drag-edits never moved the actual knob.
    if (balanceFader_ && sections_.size() >= 2)
    {
        const int balX = sections_[0].rectBounds.getX();
        const int balR = sections_[1].rectBounds.getRight();
        const int balY = sections_[0].rectBounds.getY() - kBalFaderH - 2;
        const juce::Rectangle<int> balDefault(balX, balY, balR - balX, kBalFaderH);
        balanceFader_->setBounds(layout_.boundsOr("balanceFader", balDefault));
    }

    layoutHeader(headerBounds_);

    if (onRackBoundsChanged) onRackBoundsChanged();
}

void FaceplatePanel::layoutSection(Section& s)
{
    if (s.rectBounds.isEmpty() || s.controls.empty()) return;

    // Module-ID strip removed — no bottom trim needed. Each section's
    // rect height is now sized exactly to its controls (see resized).
    auto inner = s.rectBounds.reduced(kInnerPadX, 0)
                              .withTrimmedTop(kColTitleH);
    if (inner.isEmpty()) return;

    // Uniform row height across all sections — every knob the same size.
    const int rowH = kRowH;

    // Per-row layout: SQUARE knob (knobW × knobW) with the label flush
    // beneath it. The 2026-05-17 layout had a 63 px-tall non-square knob
    // box; the rotary visual rendered as 44 px circle anyway, so 19 px
    // of empty colour-strip was wasted between the knob bottom and the
    // label. Squaring it up and tightening saves ~20 px per row.
    int y = inner.getY();
    for (auto& cp : s.controls)
    {
        auto& c = *cp;
        const auto cell = juce::Rectangle<int>(inner.getX(), y,
                                               inner.getWidth(), rowH);

        const int labelH  = juce::jmin(kKnobLabelH, rowH / 4);
        const int knobBox = juce::jmin(cell.getWidth(), rowH - labelH - 1);
        const int knobX   = cell.getX() + (cell.getWidth() - knobBox) / 2;

        if (c.kind == CtlKind::Knob)
        {
            c.slider->setBounds(knobX, cell.getY(), knobBox, knobBox);
            c.label->setBounds(cell.getX(), cell.getY() + knobBox,
                               cell.getWidth(), labelH);
        }
        else if (c.kind == CtlKind::SampleSlot)
        {
            c.sampleSlot->setBounds(knobX, cell.getY(), knobBox, knobBox);
            c.label->setBounds(cell.getX(), cell.getY() + knobBox,
                               cell.getWidth(), labelH);
        }
        else
        {
            const int btnH = 22;
            c.button->setBounds(cell.getX(),
                                cell.getY() + (cell.getHeight() - btnH) / 2,
                                cell.getWidth(), btnH);
        }
        y += rowH;
    }
}

void FaceplatePanel::layoutHeader(juce::Rectangle<int> /*capArea*/)
{
    // Controls are split symmetrically between the left and right fins.
    // Left fin:  BNC WAV · BNC AIF · DICE   (I/O + randomise)
    // Right fin: LIM · TAIL · LOOP           (playback toggles, row 1)
    //            BPM display                  (tempo display,   row 2)
    //
    // All toggle pills share kPillW × kPillH for visual uniformity.
    // Controls sit in the lower fin zone (y ≈ 160) — below the scope
    // strip (scope ends ≈ y 142) so there is no overlap.
    constexpr int kPillW = 60;   // unified width for all pill buttons
    constexpr int kPillH = 22;
    constexpr int kGap   = 4;    // gap between pills in a row
    constexpr int kBpmW  = 78;   // BPM display is slightly wider

    const float sx = (float)getWidth()  / bombo::BombShape::kRefW;
    const float sy = (float)getHeight() / bombo::BombShape::kRefH;

    // Fin Y range (ref: finTopY=34, finBotY=128)
    const int finTop = static_cast<int>(34.0f * sy);
    const int finBot = static_cast<int>(128.0f * sy);

    // Row 1 at 55 % of the way down the fin (below scope strip)
    const int row1Y = finTop + (finBot - finTop) * 55 / 100 - kPillH / 2;
    // Row 2 (BPM) just below row 1
    const int row2Y = row1Y + kPillH + kGap;

    // Left fin X: fin outer (ref 13) to body inner at finTopY (ref 130)
    const int leftFinCx = static_cast<int>((13.0f + 130.0f) * 0.5f * sx);
    // Right fin X: body inner right at finTopY (ref 230) to fin outer (ref 347)
    const int rightFinCx = static_cast<int>((230.0f + 347.0f) * 0.5f * sx);

    // ── Left fin: 3 pills centred on leftFinCx ────────────────────────
    const int leftTotal = 3 * kPillW + 2 * kGap;
    int lx = leftFinCx - leftTotal / 2;
    if (bncWavPill_) bncWavPill_->setBounds(lx,             row1Y, kPillW, kPillH);
    lx += kPillW + kGap;
    if (bncAifPill_) bncAifPill_->setBounds(lx,             row1Y, kPillW, kPillH);
    lx += kPillW + kGap;
    if (diceButton_) diceButton_->setBounds(lx,             row1Y, kPillW, kPillH);

    // ── Right fin: 3 toggle pills (row 1) + BPM display (row 2) ──────
    const int rightTotal = 3 * kPillW + 2 * kGap;
    int rx = rightFinCx - rightTotal / 2;
    if (limPill_)  limPill_ ->setBounds(rx,             row1Y, kPillW, kPillH);
    rx += kPillW + kGap;
    if (tailPill_) tailPill_->setBounds(rx,             row1Y, kPillW, kPillH);
    rx += kPillW + kGap;
    if (loopBtn_)  loopBtn_ ->setBounds(rx,             row1Y, kPillW, kPillH);

    if (bpmDisplay_)
        bpmDisplay_->setBounds(rightFinCx - kBpmW / 2, row2Y, kBpmW, kPillH);
}

// ────────────────────────────────────────────────────────────────────
//  Macro fan-out
// ────────────────────────────────────────────────────────────────────

void FaceplatePanel::writeParam(const juce::String& paramId, float plainValue)
{
    if (auto* p = apvts_.getParameter(paramId))
    {
        const float norm = p->convertTo0to1(plainValue);
        p->beginChangeGesture();
        p->setValueNotifyingHost(norm);
        p->endChangeGesture();
    }
}

bool FaceplatePanel::isMuted(const Section& s) const noexcept
{
    if (s.mutePid.isEmpty()) return false;
    if (auto* p = apvts_.getRawParameterValue(s.mutePid))
        return p->load() > 0.5f;
    return false;
}

void FaceplatePanel::toggleMute(const Section& s)
{
    if (s.mutePid.isEmpty()) return;
    if (auto* p = apvts_.getParameter(s.mutePid))
    {
        const float cur = p->getValue();
        p->beginChangeGesture();
        p->setValueNotifyingHost(cur > 0.5f ? 0.0f : 1.0f);
        p->endChangeGesture();
        repaint();
    }
}

// ────────────────────────────────────────────────────────────────────
//  Mouse events — section title click toggles mute
// ────────────────────────────────────────────────────────────────────

void FaceplatePanel::mouseDown(const juce::MouseEvent& e)
{
    const auto pt = e.getPosition();
    for (const auto& s : sections_)
    {
        if (s.mutePid.isEmpty()) continue;
        if (s.titleBounds.contains(pt))
        {
            toggleMute(s);
            return;
        }
    }
}

void FaceplatePanel::mouseMove(const juce::MouseEvent& e)
{
    const auto pt = e.getPosition();
    for (const auto& s : sections_)
    {
        if (s.mutePid.isEmpty()) continue;
        if (s.titleBounds.contains(pt))
        {
            setMouseCursor(juce::MouseCursor::PointingHandCursor);
            return;
        }
    }
    setMouseCursor(juce::MouseCursor::NormalCursor);
}

void FaceplatePanel::wireMacroFanOut()
{
    // Each macro is a 0..1 slider that fans out to a small set of params.
    // The mappings below were picked by ear for a kick — at v=0.5 the
    // sound sits close to a default-ish kick; the macro then sweeps the
    // target params through musically useful ranges.
    //
    // SliderAttachment isn't used for the 6 placeholder macros because no
    // APVTS param exists for them yet; instead we drive APVTS params on
    // change, and the bound underlying knobs reflect the new positions
    // automatically (their SliderAttachments listen to host changes).

    auto wire = [](juce::Slider* s, std::function<void(float)> fn)
    {
        if (!s) return;
        s->onValueChange = [s, fn = std::move(fn)] { fn(static_cast<float>(s->getValue())); };
    };

    // PITCH — sweeps the pitch envelope start/end frequencies.
    wire(macro_[0]->slider.get(), [this](float v)
    {
        writeParam(pid::pitchStart, juce::jmap(v, 60.0f, 280.0f));
        writeParam(pid::pitchEnd,   juce::jmap(v, 25.0f,  90.0f));
    });

    // DECAY — overall length: amp decay (primary) + pitch envelope decay.
    wire(macro_[1]->slider.get(), [this](float v)
    {
        writeParam(pid::ampDecay,   juce::jmap(v, 120.0f, 1500.0f));
        writeParam(pid::pitchDecay, juce::jmap(v,  30.0f,  400.0f));
    });

    // PUNCH — click amount up, amp attack down (snappier).
    wire(macro_[2]->slider.get(), [this](float v)
    {
        writeParam(pid::clickAmount, juce::jmap(v, 0.0f,  0.85f));
        writeParam(pid::ampAttack,   juce::jmap(v, 5.0f,  0.2f));   // inverse
    });

    // WEIGHT — body up, LP opens up, HP shelves down.
    wire(macro_[3]->slider.get(), [this](float v)
    {
        writeParam(pid::noiseAmount, juce::jmap(v,  0.05f, 0.6f));
        writeParam(pid::filterLp,    juce::jmap(v,  800.0f, 6000.0f));
        writeParam(pid::filterHp,    juce::jmap(v,  120.0f,  25.0f)); // inverse
    });

    // MOOD — drives both voice clip and rumble-bus saturator together.
    wire(macro_[4]->slider.get(), [this](float v)
    {
        writeParam(pid::driveAmount,   juce::jmap(v, 0.0f, 0.80f));
        writeParam(pid::fxDriveAmount, juce::jmap(v, 0.0f, 0.60f));
    });

    // SPACE — reverb + delay send levels together.
    wire(macro_[5]->slider.get(), [this](float v)
    {
        writeParam(pid::reverbMix, juce::jmap(v, 0.0f, 0.65f));
        writeParam(pid::delayMix,  juce::jmap(v, 0.0f, 0.45f));
    });

    // macro_[6] (OUT) is APVTS-bound directly — no fan-out needed.
}

void FaceplatePanel::layoutMacrosInNose(juce::Rectangle<int> noseInterior)
{
    // Macros relocated into the bomb's red nose region (2026-05-17). OUT
    // sits as a larger hero at the centre; the six satellite macros are
    // arranged in a perfect hexagonal ring around it (60° intervals,
    // starting at top).
    //
    // Macro indexing (matches construction order in the ctor):
    //   0 = PITCH   (top)            3 = WEIGHT (bottom)
    //   1 = DECAY   (top-right)      4 = MOOD   (bottom-left)
    //   2 = PUNCH   (bottom-right)   5 = SPACE  (top-left)
    //   6 = OUT     (centre, master gain APVTS-bound)
    if (noseInterior.isEmpty()) return;

    // Sizes tuned to fit inside the actual bomb silhouette (not just the
    // rectangular nose bounding box). The teardrop tapers steeply below
    // the body-bottom line, so the cluster has to be modest — but big
    // enough to read at a glance. Tuned iteratively against screenshots.
    constexpr int kSatSize   = 38;     // satellite knob diameter
    constexpr int kHeroSize  = 66;     // OUT hero diameter
    constexpr int kLabelH    = 12;
    constexpr int kLabelGap  = 2;
    constexpr int kRingGap   = 14;     // gap between OUT edge and satellite edge
                                       // (leaves room for OUT's label between
                                       // OUT and the bottom satellite).

    // Ring radius from centre of OUT to centre of each satellite. Edge-
    // to-edge gap is kRingGap so the cluster reads as a tight assembly
    // rather than a scatter.
    const int radius = (kHeroSize / 2) + kRingGap + (kSatSize / 2);

    // Cluster footprint — used for centering in the nose and for the
    // F2-editor hit-box. Width: 2*radius + satellite diameter.
    // Height: same vertically, plus the bottom-satellite's label strip.
    const int clusterW = 2 * radius + kSatSize;
    const int clusterH = 2 * radius + kSatSize + kLabelGap + kLabelH;

    const int cx = noseInterior.getCentreX();
    // Centre the cluster vertically inside the actual nose interior so
    // the top satellite stays well below the red-region boundary and
    // the bottom satellite + its label finish above the chassis tip.
    const int topSatHalf  = radius + kSatSize / 2;
    const int botSatHalf  = radius + kSatSize / 2 + kLabelGap + kLabelH;
    const int verticalSpan = topSatHalf + botSatHalf;
    const int slack        = juce::jmax(0, noseInterior.getHeight() - verticalSpan);
    const int cy = noseInterior.getY() + topSatHalf + slack / 2;

    auto setKnob = [&](int macroIdx, int knobCx, int knobCy, int knobSize, bool wantLabel)
    {
        auto& m = macro_[macroIdx];
        if (! m) return;
        const int knobX  = knobCx - knobSize / 2;
        const int knobY  = knobCy - knobSize / 2;
        const int labelW = knobSize + 22;
        if (m->slider) m->slider->setBounds(knobX, knobY, knobSize, knobSize);
        if (m->label)
        {
            m->label->setBounds(knobCx - labelW / 2,
                                knobY + knobSize + kLabelGap,
                                labelW, kLabelH);
            m->label->setVisible(wantLabel);
        }
    };

    // OUT hero at the geometric centre. Hero gets a bigger label font
    // so the master-output is unmistakably the centerpiece of the nose.
    setKnob(6, cx, cy, kHeroSize, true);
    if (auto& m6 = macro_[6]; m6 && m6->label)
        m6->label->setFont(fonts::label(11.5f).boldened());

    // 6 satellites equally spaced. Screen y increases downwards, so a
    // -π/2 angle points UP. Stepping +π/3 walks clockwise.
    constexpr float kPi = juce::MathConstants<float>::pi;
    const float startAngle = -kPi / 2.0f;
    const int satOrder[6] = { 0, 1, 2, 3, 4, 5 };  // PITCH, DECAY, PUNCH, WEIGHT, MOOD, SPACE
    for (int i = 0; i < 6; ++i)
    {
        const float a = startAngle + static_cast<float>(i) * (kPi / 3.0f);
        const int sx = cx + static_cast<int>(std::cos(a) * static_cast<float>(radius));
        const int sy = cy + static_cast<int>(std::sin(a) * static_cast<float>(radius));
        setKnob(satOrder[i], sx, sy, kSatSize, true);
    }

    (void) clusterW;
}

} // namespace bombo
