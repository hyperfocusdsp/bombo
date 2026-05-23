#include "FaceplatePanel.h"

#include "BalanceFader.h"
#include "BombShape.h"
#include "BpmDisplay.h"
#include "ChassisRenderer.h"
#include "Colours.h"
#include "DiceButton.h"
#include "LoopButton.h"
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

void FaceplatePanel::changeListenerCallback(juce::ChangeBroadcaster* bc)
{
    ThemedComponent::changeListenerCallback(bc); // calls repaint()

    // Refresh section label colors for the new theme. VOICE sections use
    // bone (accent text on dark voice bg); FX sections use ink. Identified
    // by mutePid because accent color captured at build time is stale.
    for (auto& s : sections_)
    {
        const bool isVoice = (s.mutePid == pid::voiceAMute
                           || s.mutePid == pid::voiceBMute);
        const juce::Colour newLabel = isVoice ? col::bone() : col::ink();
        s.labelOnBg = newLabel;
        for (auto& c : s.controls)
        {
            if (c && c->label)
                c->label->setColour(juce::Label::textColourId, newLabel);
        }
    }
}

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
        s.reorderable = true; s.fxId = FxId::Drive;
        addKnob  (s, pid::driveAmount,   "V.AMT",  s.labelOnBg);
        addChoice(s, pid::driveMode,     "V.MODE", s.labelOnBg);
        addKnob  (s, pid::driveBias,     "BIAS",   s.labelOnBg);
        addKnob  (s, pid::fxDriveAmount, "B.AMT",  s.labelOnBg);
        addChoice(s, pid::fxDriveMode,   "B.MODE", s.labelOnBg);
        addKnob  (s, pid::fxDriveMix,    "MIX",    s.labelOnBg);
        sections_.push_back(std::move(s));
    }
    {
        Section s;
        s.name = "DELAY"; s.moduleId = "DLY-1"; s.mutePid = pid::delayMute;
        s.accent = col::delayC(); s.labelOnBg = col::ink();
        s.reorderable = true; s.fxId = FxId::Delay;
        addKnob(s, pid::delayTime,     "TIME",  s.labelOnBg);
        addKnob(s, pid::delayFeedback, "FBK",   s.labelOnBg);
        addChoice(s, pid::delayTimeMode, "SYNC", s.labelOnBg);
        addKnob(s, pid::delayMorph,    "TONE",    s.labelOnBg);
        addKnob(s, pid::delaySmear,    "SMEAR",   s.labelOnBg);
        addKnob(s, pid::delayMix,      "MIX",     s.labelOnBg);
        sections_.push_back(std::move(s));
    }
    {
        Section s;
        s.name = "REVERB"; s.moduleId = "RVB-CONV"; s.mutePid = pid::reverbMute;
        s.accent = col::reverb(); s.labelOnBg = col::ink();
        s.reorderable = true; s.fxId = FxId::Reverb;
        // TYPE (Choice) replaces the deprecated DIFFUSION knob — the
        // convolution engine carries diffusion in the IR itself.
        addChoice(s, pid::reverbType,    "TYPE",  s.labelOnBg);
        addKnob  (s, pid::reverbSize,    "SIZE",  s.labelOnBg);
        addKnob  (s, pid::reverbDecay,   "DECAY", s.labelOnBg);
        addKnob  (s, pid::reverbDamp,    "DAMP",  s.labelOnBg);
        addKnob  (s, pid::reverbPredelay,"PRE",   s.labelOnBg);
        addKnob  (s, pid::reverbMix,     "MIX",   s.labelOnBg);
        sections_.push_back(std::move(s));
    }
    {
        Section s;
        s.name = "FILTER"; s.moduleId = "SVF-2P"; s.mutePid = pid::filterMute;
        s.accent = col::filterC(); s.labelOnBg = col::ink();
        s.reorderable = true; s.fxId = FxId::Filter;
        addKnob(s, pid::filterHp,    "HP",    s.labelOnBg);
        addKnob(s, pid::filterHpQ,   "HP Q",  s.labelOnBg);
        addKnob(s, pid::filterLp,    "LP",    s.labelOnBg);
        addKnob(s, pid::filterLpQ,   "LP Q",  s.labelOnBg);
        addKnob(s, pid::filterColor, "COLOR", s.labelOnBg);
        addKnob(s, pid::filterTeeth, "TEETH", s.labelOnBg);
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
        addKnob(s, pid::duckShape, "SHAPE", s.labelOnBg);
        addKnob(s, pid::duckGrowl,   "GROWL",   s.labelOnBg);
        sections_.push_back(std::move(s));
    }

    // ── Visual-order permutation ────────────────────────────────────
    // Default: identity — column N renders sections_[N]. The four FX in
    // the middle (indices 2..5) get reshuffled by drag; slots 0, 1, and
    // 6 stay pinned to VOICE A, VOICE B, DUCK forever.
    visualOrder_.resize(sections_.size());
    for (std::size_t i = 0; i < sections_.size(); ++i)
        visualOrder_[i] = static_cast<int>(i);

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

    // ── Loop toggle — icon-only circular-arrow button ──────────────
    loopBtn_ = std::make_unique<LoopButton>();
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

    // ── BNC pill — single bounce button with format popup ──────────
    // Click shows WAV / AIFF choice; ticks the last-used format.
    // Callbacks stored as members so the popup lambda can reach them.
    bounceWavCb_  = std::move(bounceWavCb);
    bounceAiffCb_ = std::move(bounceAiffCb);

    bncPill_ = std::make_unique<juce::TextButton>("BNC");
    bncPill_->setTooltip("Bounce to WAV or AIFF — click to choose format");
    bncPill_->setWantsKeyboardFocus(false);
    bncPill_->setMouseClickGrabsKeyboardFocus(false);
    bncPill_->onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem(1, "Bounce WAV",  true, !lastBounceIsAiff_);
        m.addItem(2, "Bounce AIFF", true,  lastBounceIsAiff_);
        m.showMenuAsync(
            juce::PopupMenu::Options().withTargetComponent(bncPill_.get()),
            [this](int result)
            {
                if (result == 1) { lastBounceIsAiff_ = false; if (bounceWavCb_)  bounceWavCb_();  }
                if (result == 2) { lastBounceIsAiff_ = true;  if (bounceAiffCb_) bounceAiffCb_(); }
            });
    };
    addAndMakeVisible(*bncPill_);

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
    const int lastCol = static_cast<int>(visualOrder_.size()) - 1;
    for (int col = 0; col < static_cast<int>(visualOrder_.size()); ++col)
    {
        const bool roundLeft  = (col == 0);
        const bool roundRight = (col == lastCol);
        const int  sIdx       = visualOrder_[(std::size_t) col];
        if (sIdx < 0 || sIdx >= static_cast<int>(sections_.size())) continue;
        paintSection(g, sections_[(std::size_t) sIdx], roundLeft, roundRight);
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

    // Detect whether THIS section is the one currently being dragged so
    // we can give it a "lifted" treatment — brighter accent + tinted
    // title bar — without moving its bounds (live-reflow handles the
    // positional feedback; this provides the visual identity cue).
    const bool isDragLifted = (dragState_ == DragState::Dragging)
        && (dragOriginSection_ >= 0)
        && (dragOriginSection_ < static_cast<int>(sections_.size()))
        && (&s == &sections_[(std::size_t) dragOriginSection_]);

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
        // Lifted state brightens to full saturation so the moving section
        // reads as the active one even mid-reflow.
        g.setColour(isDragLifted
                    ? s.accent.brighter(0.25f)
                    : bodyColour.darker(0.45f));
        g.fillRect(accentStrip);
        g.setColour(isDragLifted ? s.accent.darker(0.30f)
                                 : (muted ? col::graphite() : col::ink()));
        g.fillRect(titleBar);
        g.setColour(isDragLifted ? col::bone()
                                 : (muted ? col::boneDim() : col::bone()));
        g.setFont(fonts::title(11.5f));
        g.drawText(s.name, titleBar, juce::Justification::centred);
        if (muted && ! isDragLifted)
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
    // SingleChannel (not ARGB) so reduceClipRegion works on X11 — ARGB
    // image masks silently no-op on transparent windows under X11/XCB.
    chassisMask_ = juce::Image(juce::Image::SingleChannel, juce::jmax(1, w), juce::jmax(1, h), true);
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

    for (int col = 0;
         col < kNCols && col < static_cast<int>(visualOrder_.size());
         ++col)
    {
        const int sIdx = visualOrder_[(std::size_t) col];
        if (sIdx < 0 || sIdx >= static_cast<int>(sections_.size())) continue;
        auto& s = sections_[(std::size_t) sIdx];
        const int x = leftMargin + col * (colW + kColGap);
        // Default section bounds — overridable via LayoutManager. The
        // layout-editor key stays tied to the section *identity*
        // (s.name), not the column position, so a section dragged
        // visually keeps any bespoke layout overrides that were saved
        // for it under its name.
        juce::Rectangle<int> defaultBounds(x, rectTop, colW, rectH);
        const juce::String secId = "section."
            + s.name.replaceCharacter(' ', '_').toLowerCase();
        const auto layoutOverride = layout_.boundsOr(secId, defaultBounds);
        // If the user dragged this section to a new column, its saved
        // override (if any) holds the OLD x — we want the column position
        // to track visualOrder_, so always force x + width from the
        // default while preserving any user-edited y/h.
        s.rectBounds = juce::Rectangle<int>(x, layoutOverride.getY(),
                                            colW, layoutOverride.getHeight());
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
    // Left  fin row1: BNC (pill) | DICE (square icon)   — one row only
    // Right fin row1: LIM (pill) | TAIL (pill)
    //         row2:  LOOP (square icon) | BPM display
    //
    // kPillW = 62 px (fits "BNC" and "LIM"/"TAIL" with comfortable margins).
    // kSq    = 22 px — square side used for DICE and LOOP icons.
    constexpr int kPillW = 62;
    constexpr int kPillH = 22;
    constexpr int kSq    = 22;   // square icon size
    constexpr int kGap   = 4;
    constexpr int kBpmW  = 76;

    const float sx = (float)getWidth() / bombo::BombShape::kRefW;

    const int row1Y = kHeaderH + kScopeH + 8;   // ≈ 158 design px
    const int row2Y = row1Y + kPillH + kGap;     // ≈ 184 design px

    const int leftFinCx  = static_cast<int>((13.0f  + 130.0f) * 0.5f * sx);
    const int rightFinCx = static_cast<int>((230.0f + 347.0f) * 0.5f * sx);

    // ── Left fin: BNC pill + DICE square, single row ─────────────────
    {
        const int totalW = kPillW + kGap + kSq;
        int lx = leftFinCx - totalW / 2;
        if (bncPill_)    bncPill_   ->setBounds(lx,           row1Y, kPillW, kPillH);
        lx += kPillW + kGap;
        // DICE is square — centre it vertically in the pill height
        const int diceY = row1Y + (kPillH - kSq) / 2;
        if (diceButton_) diceButton_->setBounds(lx,           diceY, kSq,    kSq);
    }

    // ── Right fin: row1 LIM+TAIL, row2 LOOP-icon+BPM ─────────────────
    {
        const int pairW = 2 * kPillW + kGap;
        int rx = rightFinCx - pairW / 2;
        if (limPill_)  limPill_ ->setBounds(rx, row1Y, kPillW, kPillH);
        rx += kPillW + kGap;
        if (tailPill_) tailPill_->setBounds(rx, row1Y, kPillW, kPillH);

        // Row 2: LOOP icon (square) + BPM display, centred on rightFinCx
        const int row2W = kSq + kGap + kBpmW;
        int rx2 = rightFinCx - row2W / 2;
        const int loopY = row2Y + (kPillH - kSq) / 2;
        if (loopBtn_)    loopBtn_   ->setBounds(rx2, loopY, kSq,   kSq);
        rx2 += kSq + kGap;
        if (bpmDisplay_) bpmDisplay_->setBounds(rx2, row2Y, kBpmW, kPillH);
    }
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
//  FX chain order — UI ↔ DSP bridge
// ────────────────────────────────────────────────────────────────────

FxOrder FaceplatePanel::currentFxOrder() const noexcept
{
    FxOrder out = kDefaultFxOrder;
    std::size_t emit = 0;
    for (int sIdx : visualOrder_)
    {
        if (sIdx < 0 || sIdx >= static_cast<int>(sections_.size())) continue;
        const auto& s = sections_[(std::size_t) sIdx];
        if (! s.reorderable) continue;
        if (emit < out.size()) out[emit++] = s.fxId;
    }
    return out;
}

void FaceplatePanel::applyFxOrder(FxOrder o)
{
    if (! isValidFxOrder(o)) return;
    if (o == currentFxOrder()) return;

    // Build new visualOrder_: keep fixed-section slots untouched, fill the
    // reorderable slots in the sequence that produces `o`.
    std::vector<int> next = visualOrder_;

    // Index lookup: fxId → section index.
    std::array<int, 4> sectionForFx{ -1, -1, -1, -1 };
    for (int sIdx = 0; sIdx < static_cast<int>(sections_.size()); ++sIdx)
    {
        const auto& s = sections_[(std::size_t) sIdx];
        if (s.reorderable)
            sectionForFx[(std::size_t) s.fxId] = sIdx;
    }
    // Bail if the model is malformed (every FxId must be backed by exactly
    // one Section). Should never happen given the constructor wiring.
    for (int idx : sectionForFx) if (idx < 0) return;

    // Walk the reorderable column slots in order and assign per `o`.
    std::size_t emit = 0;
    for (std::size_t col = 0; col < next.size(); ++col)
    {
        const int sIdx = next[col];
        if (sIdx < 0 || sIdx >= static_cast<int>(sections_.size())) continue;
        if (! sections_[(std::size_t) sIdx].reorderable) continue;
        next[col] = sectionForFx[(std::size_t) o[emit++]];
        if (emit >= o.size()) break;
    }

    visualOrder_ = std::move(next);
    resized();
    repaint();
}

// ────────────────────────────────────────────────────────────────────
//  Mouse events — section title click toggles mute OR begins reorder drag
// ────────────────────────────────────────────────────────────────────

namespace
{
constexpr int kDragThresholdPx = 4;
constexpr std::size_t kOrderUndoStackCap = 32;
}

int FaceplatePanel::hitTestReorderableTitle(juce::Point<int> pt) const noexcept
{
    for (int i = 0; i < static_cast<int>(sections_.size()); ++i)
    {
        const auto& s = sections_[(std::size_t) i];
        if (! s.reorderable) continue;
        if (s.titleBounds.contains(pt)) return i;
    }
    return -1;
}

int FaceplatePanel::insertionColumnForX(int mouseX) const noexcept
{
    // Determine the range of reorderable column slots — first and last
    // slots whose currently-occupying section is reorderable.
    int firstReorderableCol = -1, lastReorderableCol = -1;
    for (int col = 0; col < static_cast<int>(visualOrder_.size()); ++col)
    {
        const int sIdx = visualOrder_[(std::size_t) col];
        if (sIdx < 0 || sIdx >= static_cast<int>(sections_.size())) continue;
        if (! sections_[(std::size_t) sIdx].reorderable) continue;
        if (firstReorderableCol < 0) firstReorderableCol = col;
        lastReorderableCol = col;
    }
    if (firstReorderableCol < 0) return -1;

    // Find the column whose center is closest to mouseX. Midpoint rule
    // for crisp snap behavior — once the cursor crosses a neighbor's
    // midpoint the swap commits.
    int bestCol = firstReorderableCol;
    int bestDist = std::numeric_limits<int>::max();
    for (int col = firstReorderableCol; col <= lastReorderableCol; ++col)
    {
        const int sIdx = visualOrder_[(std::size_t) col];
        const auto& s = sections_[(std::size_t) sIdx];
        const int center = s.rectBounds.getCentreX();
        const int dist = std::abs(mouseX - center);
        if (dist < bestDist) { bestDist = dist; bestCol = col; }
    }
    return bestCol;
}

void FaceplatePanel::swapReorderableColumns(int fromCol, int toCol)
{
    if (fromCol == toCol) return;
    if (fromCol < 0 || toCol < 0) return;
    if (fromCol >= static_cast<int>(visualOrder_.size())) return;
    if (toCol   >= static_cast<int>(visualOrder_.size())) return;

    // Validate both endpoints are reorderable slots — the constraint comes
    // from insertionColumnForX, but belt-and-suspenders is cheap.
    auto isReorderableCol = [&](int col) noexcept {
        const int sIdx = visualOrder_[(std::size_t) col];
        return sIdx >= 0
            && sIdx < static_cast<int>(sections_.size())
            && sections_[(std::size_t) sIdx].reorderable;
    };
    if (! isReorderableCol(fromCol) || ! isReorderableCol(toCol)) return;

    // Shift slice — pull the dragged section into toCol, sliding the
    // intervening reorderable sections by one position the other way.
    // This produces the iOS "rest of the list slides under your finger"
    // behavior rather than a hard 2-element swap.
    const int moved = visualOrder_[(std::size_t) fromCol];
    if (toCol > fromCol)
    {
        for (int c = fromCol; c < toCol; ++c)
            visualOrder_[(std::size_t) c] =
                visualOrder_[(std::size_t)(c + 1)];
    }
    else
    {
        for (int c = fromCol; c > toCol; --c)
            visualOrder_[(std::size_t) c] =
                visualOrder_[(std::size_t)(c - 1)];
    }
    visualOrder_[(std::size_t) toCol] = moved;

    resized();
    repaint();
}

void FaceplatePanel::pushOrderUndoSnapshot()
{
    orderUndoStack_.push_back(currentFxOrder());
    if (orderUndoStack_.size() > kOrderUndoStackCap)
        orderUndoStack_.erase(orderUndoStack_.begin());
}

void FaceplatePanel::mouseDown(const juce::MouseEvent& e)
{
    const auto pt = e.getPosition();

    // Reorderable header — arm both intents (mute on tap, drag on move).
    const int hitReorderable = hitTestReorderableTitle(pt);
    if (hitReorderable >= 0)
    {
        dragState_         = DragState::Armed;
        dragOriginSection_ = hitReorderable;
        dragStartPos_      = pt;
        dragCurrentPos_    = pt;
        // Find the current column for this section so a drag without an
        // initial swap is still a valid no-op.
        dragOriginColumn_  = -1;
        for (int col = 0; col < static_cast<int>(visualOrder_.size()); ++col)
            if (visualOrder_[(std::size_t) col] == hitReorderable)
                { dragOriginColumn_ = col; break; }
        return;
    }

    // Fixed (non-reorderable) header — single-purpose mute toggle.
    for (const auto& s : sections_)
    {
        if (s.reorderable) continue;
        if (s.mutePid.isEmpty()) continue;
        if (s.titleBounds.contains(pt))
        {
            toggleMute(s);
            return;
        }
    }
}

void FaceplatePanel::mouseDrag(const juce::MouseEvent& e)
{
    if (dragState_ == DragState::Idle) return;

    const auto pt = e.getPosition();
    dragCurrentPos_ = pt;

    // Promote Armed → Dragging when the cursor escapes the dead-zone.
    if (dragState_ == DragState::Armed)
    {
        const int dx = std::abs(pt.getX() - dragStartPos_.getX());
        const int dy = std::abs(pt.getY() - dragStartPos_.getY());
        if (std::max(dx, dy) < kDragThresholdPx) return;

        // Crossing the threshold commits to drag — cancel the mute intent.
        dragState_ = DragState::Dragging;
        pushOrderUndoSnapshot();
        setMouseCursor(juce::MouseCursor::DraggingHandCursor);
    }

    // Live reflow: find the column under the cursor and swap if it changed.
    int currentCol = -1;
    for (int col = 0; col < static_cast<int>(visualOrder_.size()); ++col)
        if (visualOrder_[(std::size_t) col] == dragOriginSection_)
            { currentCol = col; break; }
    if (currentCol < 0) return;

    const int targetCol = insertionColumnForX(pt.getX());
    if (targetCol >= 0 && targetCol != currentCol)
        swapReorderableColumns(currentCol, targetCol);
}

void FaceplatePanel::mouseUp(const juce::MouseEvent& e)
{
    const auto state = dragState_;
    dragState_ = DragState::Idle;
    setMouseCursor(juce::MouseCursor::NormalCursor);

    if (state == DragState::Armed)
    {
        // Tap without drag — fire mute on the originally-pressed section.
        if (dragOriginSection_ >= 0
            && dragOriginSection_ < static_cast<int>(sections_.size()))
            toggleMute(sections_[(std::size_t) dragOriginSection_]);
    }
    else if (state == DragState::Dragging)
    {
        const FxOrder o = currentFxOrder();
        if (onFxOrderChanged) onFxOrderChanged(o);
    }

    dragOriginSection_ = -1;
    dragOriginColumn_  = -1;
    juce::ignoreUnused(e);
}

bool FaceplatePanel::keyPressed(const juce::KeyPress& key)
{
    // Cmd/Ctrl+Z — pop the last fx-order snapshot.
    const bool isUndo =
        (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')
        && (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        && ! key.getModifiers().isShiftDown();
    if (isUndo && ! orderUndoStack_.empty())
    {
        const FxOrder prev = orderUndoStack_.back();
        orderUndoStack_.pop_back();
        applyFxOrder(prev);
        if (onFxOrderChanged) onFxOrderChanged(prev);
        return true;
    }
    return false;
}

void FaceplatePanel::mouseMove(const juce::MouseEvent& e)
{
    const auto pt = e.getPosition();
    // Reorderable headers get the grab cursor as a discoverability hint —
    // the affordance is the cursor, not chrome on the header.
    for (const auto& s : sections_)
    {
        if (! s.reorderable) continue;
        if (s.titleBounds.contains(pt))
        {
            setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            return;
        }
    }
    // Fixed mute-only headers get the standard hand pointer.
    for (const auto& s : sections_)
    {
        if (s.reorderable) continue;
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
