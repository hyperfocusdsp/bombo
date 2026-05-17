#include "FaceplatePanel.h"

#include "BalanceFader.h"
#include "BombShape.h"
#include "BpmDisplay.h"
#include "Colours.h"
#include "DiceButton.h"
#include "Fonts.h"
#include "SampleSlotWidget.h"
#include "WaveBuffer.h"
#include "../Parameters.h"

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

// Bands inside the chassis rect portion.
constexpr int kHeaderH      = 50;
constexpr int kScopeH       = 100;
constexpr int kMacroH       = 90;
constexpr int kRackPadX     = 2;        // padding between chassis edge and column 0/N-1
constexpr int kRackTopGap   = 4;
constexpr int kRackBotGap   = 4;

// FX columns.
constexpr int kColGap       = 0;       // colors butt up; the per-FX accent IS the separator (2026-05-17)
constexpr int kColTitleH    = 18;      // was 20
constexpr int kColAccentH   = 3;
constexpr int kModuleIdH    = 12;      // was 14
constexpr int kInnerPadX    = 1;       // was 3 — eliminates inter-column gap
constexpr int kRowH         = 58;      // was 72 — ~20% knob shrink
constexpr int kKnobLabelH   = 13;
constexpr int kNCols        = 7;

constexpr int kMacroKnobSize = 38;     // was 46 — ~20% shrink, matches kRowH
} // namespace

// ────────────────────────────────────────────────────────────────────
//  Construction
// ────────────────────────────────────────────────────────────────────

FaceplatePanel::~FaceplatePanel() = default;

FaceplatePanel::FaceplatePanel(juce::AudioProcessorValueTreeState& apvts,
                               const WaveBuffer* waveBuffer,
                               SampleSlotCallbacks sampleSlotCb,
                               HostBpmFn hostBpmFn,
                               RandomizeFn randomizeCb)
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
        addKnob(s, pid::delayDrift,    "DRIFT", s.labelOnBg);
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
    macro_[0] = makePlaceholderKnob("PITCH",  col::boneDim());
    macro_[1] = makePlaceholderKnob("DECAY",  col::boneDim());
    macro_[2] = makePlaceholderKnob("PUNCH",  col::boneDim());
    macro_[3] = makePlaceholderKnob("WEIGHT", col::boneDim());
    macro_[4] = makePlaceholderKnob("MOOD",   col::boneDim());
    macro_[5] = makePlaceholderKnob("SPACE",  col::boneDim());
    macro_[6] = makeBoundKnob(pid::masterOut, "OUT", col::accentAmber(), col::boneDim());
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
    c->sampleSlot->onBrowsePick  = sampleSlotCb_.onBrowsePick;
    c->sampleSlot->onIndexChange = sampleSlotCb_.onIndexChange;
    c->sampleSlot->onClear       = sampleSlotCb_.onClear;
    c->sampleSlot->getNames      = sampleSlotCb_.getNames;
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
    paintBackground(g);
    // Cap + fins drawn BEHIND body so the body's outline hides the seam
    // where they attach. Matches tools/bombshape_gen.py rendering order.
    paintCapAndFins(g);
    paintChassis(g);
    paintRedRegion(g);
    paintBand(g);
    paintHeader(g, headerBounds_);
    paintScopeFrame(g);    // red U-border around scope, drawn under scope component
    for (const auto& s : sections_) paintSection(g, s);
}

void FaceplatePanel::paintScopeFrame(juce::Graphics& g)
{
    if (scopeBounds_.isEmpty()) return;

    // VAULT red — matches the fin + nose color so the scope reads as
    // attached to the fin assembly. U-shape: top + left + right borders
    // ONLY (no bottom — the bottom opens into the macro row, completing
    // the "rear cap continues into the chassis" visual).
    constexpr juce::uint32 kVaultRed = 0xFFB43F32;
    constexpr int kBorder = 3;
    g.setColour(juce::Colour(kVaultRed));

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

void FaceplatePanel::paintBackground(juce::Graphics& g)
{
    // Clear to transparent. paintChassis() fills the bomb-shaped chassisPath_
    // with the graphite gradient, covering everything inside the V. The two
    // corner wedges outside the path stay alpha=0 so the OS compositor shows
    // through them — giving the window a genuine bomb silhouette in standalone.
    // In plugin mode the host container background shows instead (usually dark).
    g.fillAll(juce::Colours::transparentBlack);
}

void FaceplatePanel::paintChassis(juce::Graphics& g)
{
    if (chassisPath_.isEmpty()) return;

    // VAULT palette (hardcoded ahead of proper Phase 2e theme integration —
    // user needed the visual NOW to inform proportion decisions, see
    // memory project_bombo_silhouette_locked_r4b_classic.md). Olive body
    // with a subtle vertical gradient for raised-panel feel.
    constexpr juce::uint32 kVaultBodyHi = 0xFF6E8052;  // brighter olive
    constexpr juce::uint32 kVaultBodyLo = 0xFF4A5638;  // shaded olive

    juce::ColourGradient grad(juce::Colour(kVaultBodyHi),
                              static_cast<float>(getWidth()) * 0.5f,
                              static_cast<float>(chassisRectArea_.getY()),
                              juce::Colour(kVaultBodyLo),
                              static_cast<float>(getWidth()) * 0.5f,
                              static_cast<float>(chassisApexY_),
                              false);
    grad.addColour(0.78, juce::Colour(kVaultBodyLo));
    g.setGradientFill(grad);
    g.fillPath(chassisPath_);
}

void FaceplatePanel::paintCapAndFins(juce::Graphics& g)
{
    if (capPath_.isEmpty()) return;
    // VAULT palette — cap is a dark shaded olive (looks like the recessed
    // rear-cap behind the body); fins are red (same as nose, per Mini-Nuke
    // convention).
    constexpr juce::uint32 kVaultCap  = 0xFF323A26;  // very dark olive
    constexpr juce::uint32 kVaultRed  = 0xFFB43F32;  // Mini-Nuke red
    g.setColour(juce::Colour(kVaultCap));
    g.fillPath(capPath_);
    g.setColour(juce::Colour(kVaultRed));
    g.fillPath(finPathL_);
    g.fillPath(finPathR_);
}

void FaceplatePanel::paintRedRegion(juce::Graphics& g)
{
    if (chassisPath_.isEmpty()) return;
    // Red paint region clipped to the body silhouette — gives the
    // chunky-bomb red nose without ANY seam in the outline. The ring
    // line at y=redRegionTopY_ is a designed marker (not an outline).
    g.saveState();
    g.reduceClipRegion(chassisPath_);

    // VAULT palette (hardcoded — proper Phase 2e theme JSON pending).
    constexpr juce::uint32 kVaultRed = 0xFFB43F32;
    g.setColour(juce::Colour(kVaultRed));
    g.fillRect(juce::Rectangle<float>(0.0f,
                                      redRegionTopY_,
                                      static_cast<float>(getWidth()),
                                      static_cast<float>(getHeight()) - redRegionTopY_));

    // Boundary ring line — thin dark stroke right at the paint split.
    // Inside the silhouette only (we're still clipped to chassisPath_).
    g.setColour(col::ink().withAlpha(0.6f));
    g.drawLine(0.0f, redRegionTopY_,
               static_cast<float>(getWidth()), redRegionTopY_, 1.5f);

    g.restoreState();
}

void FaceplatePanel::paintBand(juce::Graphics& g)
{
    if (bandRect_.isEmpty()) return;
    // VAULT palette hazard band (hardcoded — proper Phase 2e theme JSON pending).
    constexpr juce::uint32 kVaultYellow = 0xFFE8B528;
    g.saveState();
    g.reduceClipRegion(chassisPath_);
    g.setColour(juce::Colour(kVaultYellow));
    g.fillRect(bandRect_);
    // Cartouche text — BOMBO-TEC + PEACE EDITION serial. Stencil-aware
    // monospace from bombo::fonts::value. Two lines, centered in the band.
    const float bx = bandRect_.getX();
    const float by = bandRect_.getY();
    const float bw = bandRect_.getWidth();
    const float bh = bandRect_.getHeight();
    g.setColour(col::ink());
    g.setFont(bombo::fonts::value(bh * 0.32f));
    g.drawText("BOMBO-TEC",
               juce::Rectangle<float>(bx, by + bh * 0.10f, bw, bh * 0.42f),
               juce::Justification::centred, false);
    g.setFont(bombo::fonts::value(bh * 0.18f));
    g.drawText("PEACE EDITION · 1992 · FOSS",
               juce::Rectangle<float>(bx, by + bh * 0.55f, bw, bh * 0.40f),
               juce::Justification::centred, false);
    g.restoreState();
}

void FaceplatePanel::paintHeader(juce::Graphics& g, juce::Rectangle<int> area)
{
    // Header band — slightly lighter strip at the chassis top. Clipped to
    // the chassis path so the rounded corners stay crisp.
    g.saveState();
    g.reduceClipRegion(chassisPath_);
    g.setColour(col::graphiteHi());
    g.fillRect(area);
    g.restoreState();

    // BOMBO logo.
    g.setColour(col::bone());
    g.setFont(fonts::title(26.0f));
    g.drawText("BOMBO",
               area.withTrimmedLeft(20).removeFromLeft(160),
               juce::Justification::centredLeft);

    // Subtitle.
    g.setColour(col::boneDim());
    g.setFont(fonts::label(10.5f));
    g.drawText("half kick  \xE2\x80\xA2  half BBS",
               area.withTrimmedLeft(180).withWidth(280),
               juce::Justification::centredLeft);

    // Hairline at bottom edge.
    g.setColour(col::ink().withAlpha(0.7f));
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    // BPM display + Loop toggle are real components; they paint themselves.

    // SYNTH / INSERT FX tab.
    {
        const auto sR = synthTabBounds_.toFloat();
        g.setColour(col::accentAmber());
        g.fillRoundedRectangle(sR, 4.0f);
        g.setColour(col::ink());
        g.setFont(fonts::label(10.0f));
        g.drawText("SYNTH", synthTabBounds_, juce::Justification::centred);

        const auto fR = insertFxTabBounds_.toFloat();
        g.setColour(col::graphite());
        g.fillRoundedRectangle(fR, 4.0f);
        g.setColour(col::boneDim());
        g.drawRoundedRectangle(fR.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(col::boneDim());
        g.drawText("INSERT FX", insertFxTabBounds_, juce::Justification::centred);
    }
}

void FaceplatePanel::paintSection(juce::Graphics& g, const Section& s)
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
    const auto bodyColour = muted
        ? s.accent.withSaturation(0.20f).withBrightness(s.accent.getBrightness() * 0.55f)
        : s.accent;
    g.setColour(bodyColour);
    g.fillRect(rb);

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
    capPath_       = bombo::BombShape::buildCapPath(boundsF);
    finPathL_      = bombo::BombShape::buildFinPath(boundsF, -1);
    finPathR_      = bombo::BombShape::buildFinPath(boundsF, +1);
    bandRect_      = bombo::BombShape::bandRect(boundsF);
    redRegionTopY_ = bombo::BombShape::redRegionTopYInBounds(boundsF);

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
    headerBounds_ = { chassisL, chassisT, chassisR - chassisL, kHeaderH };

    // Scope strip — width matches the fin tip outer extent so the scope
    // + fins read as one continuous "rear assembly" unit. Inset by the
    // scope's red U-frame border (paintScopeFrame draws it).
    constexpr int kScopeBorder = 3;
    const int scopeTop = chassisT + kHeaderH;
    {
        const bombo::BombShape::Params bp{};
        const float finTipTotalRefW = bp.bodyBulgeW + 2.0f * bp.finOutX;
        const float scaleX = static_cast<float>(w) / bombo::BombShape::kRefW;
        const int scopeFrameW = static_cast<int>(finTipTotalRefW * scaleX);
        const int scopeFrameX = (w - scopeFrameW) / 2;
        scopeBounds_ = { scopeFrameX + kScopeBorder,
                         scopeTop + 8,
                         scopeFrameW - 2 * kScopeBorder,
                         kScopeH - 16 };
    }
    scope_.setBounds(scopeBounds_);

    const int macroTop = scopeTop + kScopeH;
    layoutMacros({ chassisL, macroTop, chassisR - chassisL, kMacroH });

    // ── Rack columns ────────────────────────────────────────────────
    // Explicit column width (locked 2026-05-17 per user feedback): each
    // section is ~44 px wide so the rack reads as a compact strip with
    // body olive showing through on the sides — NOT a wall-to-wall rack
    // chewing the full chassis width. The total rack is centered
    // horizontally inside the chassis interior.
    constexpr int kColW      = 44;
    const int totalGap       = (kNCols - 1) * kColGap;
    const int totalUsed      = kColW * kNCols + totalGap;
    const int colW           = kColW;
    const int leftMargin     = chassisL + (chassisR - chassisL - totalUsed) / 2;

    const int rectTop = macroTop + kMacroH + kRackTopGap;

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
        s.rectBounds = { x, rectTop, colW, rectH };
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

    // Balance fader now lives in the header bar (relocated 2026-05-17 per
    // user feedback — moved out of the inter-column space). layoutHeader
    // owns its bounds.

    layoutHeader(headerBounds_);
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

    int y = inner.getY();
    for (auto& cp : s.controls)
    {
        auto& c = *cp;
        const auto cell = juce::Rectangle<int>(inner.getX(), y,
                                               inner.getWidth(), rowH);

        if (c.kind == CtlKind::Knob)
        {
            const int labelH = juce::jmin(kKnobLabelH, rowH / 4);
            const int knobH = cell.getHeight() - labelH - 2;
            const int knobW = juce::jmin(cell.getWidth(), knobH);
            const int knobX = cell.getX() + (cell.getWidth() - knobW) / 2;
            c.slider->setBounds(knobX, cell.getY(), knobW, knobH);
            c.label->setBounds(cell.getX(), cell.getY() + knobH,
                               cell.getWidth(), labelH);
        }
        else if (c.kind == CtlKind::SampleSlot)
        {
            // The slot is a knob now (see SampleSlotWidget). Lay it out
            // identically to a knob cell: square widget + label below.
            const int labelH = juce::jmin(kKnobLabelH, rowH / 4);
            const int knobH = cell.getHeight() - labelH - 2;
            const int knobW = juce::jmin(cell.getWidth(), knobH);
            const int knobX = cell.getX() + (cell.getWidth() - knobW) / 2;
            c.sampleSlot->setBounds(knobX, cell.getY(), knobW, knobH);
            c.label->setBounds(cell.getX(), cell.getY() + knobH,
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

void FaceplatePanel::layoutHeader(juce::Rectangle<int> area)
{
    constexpr int kPillH  = 22;
    constexpr int kLimW   = 50;
    constexpr int kLoopW  = 60;
    constexpr int kBpmW   = 78;
    constexpr int kDiceW  = 22; // square
    constexpr int kBalW   = 40; // square (A↔B balance knob, relocated 2026-05-17 — bigger for legibility)
    constexpr int kTabW   = 64;
    constexpr int kPad    = 8;
    constexpr int kPadSmall = 4;

    const int y = (area.getHeight() - kPillH) / 2 + area.getY();
    int xCursor = area.getRight() - 14;

    // Right-to-left: tabs, BPM, BALANCE, loop, LIM, DICE.
    // BALANCE sits immediately LEFT of BPM — most prominent center-header
    // position. Bigger than the pills so the A↔B disc reads at a glance.
    insertFxTabBounds_ = { xCursor - kTabW, y, kTabW, kPillH };
    xCursor -= kTabW + 1;
    synthTabBounds_ = { xCursor - kTabW, y, kTabW, kPillH };
    xCursor -= kTabW + kPad;
    if (bpmDisplay_) bpmDisplay_->setBounds(xCursor - kBpmW, y, kBpmW, kPillH);
    xCursor -= kBpmW + kPad;
    if (balanceFader_)
    {
        const int balY = area.getY() + (area.getHeight() - kBalW) / 2;
        balanceFader_->setBounds(xCursor - kBalW, balY, kBalW, kBalW);
        xCursor -= kBalW + kPad;
    }
    if (loopBtn_)    loopBtn_   ->setBounds(xCursor - kLoopW, y, kLoopW, kPillH);
    xCursor -= kLoopW + kPad;
    if (limPill_)    limPill_   ->setBounds(xCursor - kLimW,  y, kLimW,  kPillH);
    xCursor -= kLimW + kPadSmall;
    if (diceButton_) diceButton_->setBounds(xCursor - kDiceW, y, kDiceW, kPillH);
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

void FaceplatePanel::layoutMacros(juce::Rectangle<int> area)
{
    // Aligns 1:1 with the FX columns below (matching kColW). Both rows
    // share the same total-rack width and centering math.
    constexpr int kColW      = 44;
    const int totalGap       = (kNCols - 1) * kColGap;
    const int totalUsed      = kColW * kNCols + totalGap;
    const int colW           = kColW;
    const int leftMargin     = area.getX() + (area.getWidth() - totalUsed) / 2;

    const int knobH = kMacroKnobSize;
    const int labelH = kKnobLabelH;
    const int bandH = knobH + labelH + 4;
    const int yTop = area.getY() + (area.getHeight() - bandH) / 2;

    for (int i = 0; i < kNCols; ++i)
    {
        auto& m = macro_[i];
        if (!m) continue;
        const int cx = leftMargin + i * (colW + kColGap) + colW / 2;
        const int knobX = cx - knobH / 2;
        if (m->slider) m->slider->setBounds(knobX, yTop, knobH, knobH);
        if (m->label)  m->label ->setBounds(leftMargin + i * (colW + kColGap),
                                            yTop + knobH + 2,
                                            colW, labelH);
    }
}

} // namespace bombo
