#include "FaceplatePanel.h"

#include "Colours.h"
#include "Fonts.h"
#include "../Parameters.h"

namespace bombo
{
namespace
{
constexpr int kHeaderH        = 56;
constexpr int kFinBandH       = 80;
constexpr int kRackGap        = 4;
constexpr int kRackOuterPad   = 10;
constexpr int kColInnerPadX   = 6;
constexpr int kColInnerPadTop = 26;
constexpr int kColInnerPadBot = 8;
// FIXED slot height: every knob across every section uses this same
// row pitch so the rack reads as one grid, not as 7 differently-sized
// columns. Sections with fewer controls leave empty space at the bottom.
constexpr int kRowH           = 88;
constexpr int kKnobLabelH     = 14;
constexpr int kHeaderKnobSize = 40;
}

// ────────────────────────────────────────────────────────────────────
//  Construction
// ────────────────────────────────────────────────────────────────────

FaceplatePanel::FaceplatePanel(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts)
{
    // ── 7 columns, left to right ────────────────────────────────────
    // VOICE A: 5 controls — WAVE + 4 voice knobs.
    {
        Section s; s.name = "VOICE A"; s.accent = col::voice;
        addChoice(s, pid::waveform,   "WAVE");
        addKnob  (s, pid::pitchStart, "PITCH");
        addKnob  (s, pid::pitchEnd,   "END");
        addKnob  (s, pid::pitchDecay, "P.DEC");
        addKnob  (s, pid::pitchCurve, "CURVE");
        sections_.push_back(std::move(s));
    }
    // VOICE B: 6 knobs — voice-B set + the MID layer's 2 most useful knobs
    // folded in so MID doesn't need its own column.
    {
        Section s; s.name = "VOICE B"; s.accent = col::voice;
        addKnob(s, pid::ampAttack,    "ATK");
        addKnob(s, pid::ampDecay,     "DEC");
        addKnob(s, pid::clickAmount,  "CLICK");
        addKnob(s, pid::noiseAmount,  "BODY");
        addKnob(s, pid::noiseColor,   "COLOR");
        addKnob(s, pid::driftAmount,  "DRIFT");
        sections_.push_back(std::move(s));
    }
    // DRIVE: V.AMT + V.MODE + B.AMT + B.MODE + MIX = 5 controls.
    {
        Section s; s.name = "DRIVE"; s.accent = col::drive;
        addKnob  (s, pid::driveAmount,   "V.AMT");
        addChoice(s, pid::driveMode,     "V.MODE");
        addKnob  (s, pid::fxDriveAmount, "B.AMT");
        addChoice(s, pid::fxDriveMode,   "B.MODE");
        addKnob  (s, pid::fxDriveMix,    "MIX");
        sections_.push_back(std::move(s));
    }
    // FILTER: HP + HP Q + LP + LP Q + COLOR.
    {
        Section s; s.name = "FILTER"; s.accent = col::filterC;
        addKnob(s, pid::filterHp,    "HP");
        addKnob(s, pid::filterHpQ,   "HP Q");
        addKnob(s, pid::filterLp,    "LP");
        addKnob(s, pid::filterLpQ,   "LP Q");
        addKnob(s, pid::filterColor, "COLOR");
        sections_.push_back(std::move(s));
    }
    // DELAY.
    {
        Section s; s.name = "DELAY"; s.accent = col::delayC;
        addKnob(s, pid::delayTime,     "TIME");
        addKnob(s, pid::delayFeedback, "FBK");
        addKnob(s, pid::delayDrift,    "DRIFT");
        addKnob(s, pid::delayMorph,    "TONE");
        addKnob(s, pid::delayMix,      "MIX");
        sections_.push_back(std::move(s));
    }
    // REVERB.
    {
        Section s; s.name = "REVERB"; s.accent = col::reverb;
        addKnob(s, pid::reverbSize,      "SIZE");
        addKnob(s, pid::reverbDecay,     "DECAY");
        addKnob(s, pid::reverbDamp,      "DAMP");
        addKnob(s, pid::reverbDiffusion, "DIFF");
        addKnob(s, pid::reverbPredelay,  "PRE");
        addKnob(s, pid::reverbMix,       "MIX");
        sections_.push_back(std::move(s));
    }
    // DUCK.
    {
        Section s; s.name = "DUCK"; s.accent = col::duck;
        addKnob(s, pid::duckAtk,   "ATK");
        addKnob(s, pid::duckRel,   "REL");
        addKnob(s, pid::duckDepth, "DEPTH");
        sections_.push_back(std::move(s));
    }

    // ── Header band — master OUT + LIM toggle + LIM AMT ─────────────
    // master OUT
    {
        auto c = std::make_unique<Control>();
        c->kind = CtlKind::Knob;
        c->slider = std::make_unique<juce::Slider>();
        c->slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        c->slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                       juce::MathConstants<float>::pi * 2.75f, true);
        c->slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        c->slider->setColour(juce::Slider::rotarySliderOutlineColourId, col::voice);
        c->slider->setWantsKeyboardFocus(false);
        c->slider->setMouseClickGrabsKeyboardFocus(false);
        c->label = std::make_unique<juce::Label>();
        c->label->setText("OUT", juce::dontSendNotification);
        c->label->setJustificationType(juce::Justification::centred);
        c->label->setFont(fonts::label(9.5f));
        c->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts_, pid::masterOut, *c->slider);
        addAndMakeVisible(*c->slider);
        addAndMakeVisible(*c->label);
        header_.push_back(std::move(c));
    }
    // LIM toggle
    {
        auto c = std::make_unique<Control>();
        c->kind = CtlKind::Toggle;
        c->button = std::make_unique<juce::ToggleButton>("LIM");
        c->button->setColour(juce::ToggleButton::textColourId, col::boneDim);
        c->button->setWantsKeyboardFocus(false);
        c->button->setMouseClickGrabsKeyboardFocus(false);
        c->bAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts_, pid::limiterOn, *c->button);
        addAndMakeVisible(*c->button);
        header_.push_back(std::move(c));
    }
    // LIM AMT
    {
        auto c = std::make_unique<Control>();
        c->kind = CtlKind::Knob;
        c->slider = std::make_unique<juce::Slider>();
        c->slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        c->slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                       juce::MathConstants<float>::pi * 2.75f, true);
        c->slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
        c->slider->setColour(juce::Slider::rotarySliderOutlineColourId, col::accentAmber);
        c->slider->setWantsKeyboardFocus(false);
        c->slider->setMouseClickGrabsKeyboardFocus(false);
        c->label = std::make_unique<juce::Label>();
        c->label->setText("LIM", juce::dontSendNotification);
        c->label->setJustificationType(juce::Justification::centred);
        c->label->setFont(fonts::label(9.5f));
        c->sAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts_, pid::limiterAmount, *c->slider);
        addAndMakeVisible(*c->slider);
        addAndMakeVisible(*c->label);
        header_.push_back(std::move(c));
    }
}

FaceplatePanel::Control*
FaceplatePanel::addKnob(Section& s, const juce::String& paramId,
                        const juce::String& displayName)
{
    auto c = std::make_unique<Control>();
    c->kind = CtlKind::Knob;
    c->slider = std::make_unique<juce::Slider>();
    c->slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    c->slider->setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                   juce::MathConstants<float>::pi * 2.75f, true);
    c->slider->setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    c->slider->setColour(juce::Slider::rotarySliderOutlineColourId, s.accent);
    // Don't steal keyboard focus on click — Space / T / Enter must keep
    // reaching the editor's keyPressed for the trigger bridge.
    c->slider->setWantsKeyboardFocus(false);
    c->slider->setMouseClickGrabsKeyboardFocus(false);
    c->label = std::make_unique<juce::Label>();
    c->label->setText(displayName, juce::dontSendNotification);
    c->label->setJustificationType(juce::Justification::centred);
    c->label->setFont(fonts::label(9.5f));
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
                          const juce::String& displayName)
{
    auto c = std::make_unique<Control>();
    c->kind = CtlKind::Choice;
    c->combo = std::make_unique<juce::ComboBox>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts_.getParameter(paramId)))
        c->combo->addItemList(p->choices, 1);
    c->combo->setColour(juce::ComboBox::textColourId, col::bone);
    c->combo->setWantsKeyboardFocus(false);
    c->combo->setMouseClickGrabsKeyboardFocus(false);
    c->label = std::make_unique<juce::Label>();
    c->label->setText(displayName, juce::dontSendNotification);
    c->label->setJustificationType(juce::Justification::centred);
    c->label->setFont(fonts::label(9.5f));
    c->cAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts_, paramId, *c->combo);
    addAndMakeVisible(*c->combo);
    addAndMakeVisible(*c->label);
    Control* raw = c.get();
    s.controls.push_back(std::move(c));
    return raw;
}

FaceplatePanel::Control*
FaceplatePanel::addToggle(Section& s, const juce::String& paramId,
                          const juce::String& displayName)
{
    auto c = std::make_unique<Control>();
    c->kind = CtlKind::Toggle;
    c->button = std::make_unique<juce::ToggleButton>(displayName);
    c->button->setWantsKeyboardFocus(false);
    c->button->setMouseClickGrabsKeyboardFocus(false);
    c->bAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts_, paramId, *c->button);
    addAndMakeVisible(*c->button);
    Control* raw = c.get();
    s.controls.push_back(std::move(c));
    return raw;
}

// ────────────────────────────────────────────────────────────────────
//  Paint
// ────────────────────────────────────────────────────────────────────

void FaceplatePanel::paint(juce::Graphics& g)
{
    g.fillAll(col::graphite);

    const auto bounds = getLocalBounds();
    paintHeader(g, bounds.withHeight(kHeaderH));
    for (const auto& s : sections_) paintSectionFrame(g, s);
    paintFinBand(g, bounds.withTrimmedTop(getHeight() - kFinBandH));
}

void FaceplatePanel::paintHeader(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(col::graphiteHi);
    g.fillRect(area);
    g.setColour(col::bone);
    g.setFont(fonts::title(26.0f));
    g.drawText("BOMBO",
               area.reduced(18, 0).removeFromLeft(180),
               juce::Justification::centredLeft);
    g.setColour(col::boneDim);
    g.setFont(fonts::label(10.0f));
    g.drawText("HALF KICK / HALF BBS",
               juce::Rectangle<int>(140, 0, 280, kHeaderH),
               juce::Justification::centredLeft);
    // Hairline at bottom edge.
    g.setColour(col::ink);
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);
}

void FaceplatePanel::paintSectionFrame(juce::Graphics& g, const Section& s)
{
    if (s.bounds.isEmpty()) return;
    const auto fb = s.bounds.toFloat();

    // Body fill — slightly lighter than the chassis so the rack reads
    // as raised panels.
    g.setColour(col::graphiteHi);
    g.fillRoundedRectangle(fb, 5.0f);

    // Section colour edge.
    g.setColour(s.accent.withAlpha(0.55f));
    g.drawRoundedRectangle(fb.reduced(0.5f), 5.0f, 1.0f);

    // Title bar: 3 px accent strip at the very top, plus a darker
    // band beneath it for the title text so the section name has
    // a stable backdrop regardless of the accent's luminance.
    const auto strip = juce::Rectangle<float>(fb.getX(), fb.getY(),
                                              fb.getWidth(), 3.0f);
    g.setColour(s.accent);
    g.fillRect(strip);

    g.setColour(col::ink);
    g.fillRect(juce::Rectangle<float>(fb.getX(), fb.getY() + 3.0f,
                                      fb.getWidth(), 20.0f));

    g.setColour(col::bone);
    g.setFont(fonts::title(11.5f));
    const auto titleArea = s.bounds.withHeight(kColInnerPadTop)
                                   .translated(0, 3);
    g.drawText(s.name, titleArea, juce::Justification::centred);
}

void FaceplatePanel::paintFinBand(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(col::ink);
    g.fillRect(area);
    g.setColour(col::bone.withAlpha(0.18f));
    g.fillRect(area.getX(), area.getY(), area.getWidth(), 1);

    const float w = static_cast<float>(area.getWidth());
    const float topY = static_cast<float>(area.getY());
    const float apexY = static_cast<float>(area.getBottom() - 4);
    const float apexX = w * 0.5f;

    juce::Path v;
    v.startNewSubPath(0.0f, topY);
    v.lineTo(w, topY);
    v.lineTo(apexX, apexY);
    v.closeSubPath();

    juce::ColourGradient grad(col::drive.withAlpha(0.55f),  0.0f, topY,
                              col::duck.withAlpha(0.55f),   w,    topY, false);
    grad.addColour(0.25, col::filterC.withAlpha(0.55f));
    grad.addColour(0.50, col::delayC .withAlpha(0.55f));
    grad.addColour(0.75, col::reverb .withAlpha(0.55f));
    g.setGradientFill(grad);
    g.fillPath(v);

    g.setColour(col::bone.withAlpha(0.22f));
    g.drawLine(0.0f, topY, apexX, apexY, 1.0f);
    g.drawLine(w,    topY, apexX, apexY, 1.0f);
}

// ────────────────────────────────────────────────────────────────────
//  Layout
// ────────────────────────────────────────────────────────────────────

void FaceplatePanel::resized()
{
    // ── 7 FX columns weighted by knob count so wider sections (REVERB
    //    has 6, DUCK has 3) get proportional space. ──────────────────
    constexpr int n = 7;
    const float ctlCounts[n] = { 5.0f, 6.0f, 5.0f, 5.0f, 5.0f, 6.0f, 3.0f };
    // DRIVE has 2 ComboBoxes (wider than knobs) — bump its weight.
    const float driveBoost = 1.1f;
    float weights[n];
    float wSum = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        weights[i] = ctlCounts[i] * (i == 2 ? driveBoost : 1.0f);
        wSum += weights[i];
    }

    const int rackTop = kHeaderH + kRackOuterPad;
    const int rackBottom = getHeight() - kFinBandH - kRackOuterPad;
    const int rackH = rackBottom - rackTop;
    const int rackAvailableW = getWidth() - kRackOuterPad * 2 - kRackGap * (n - 1);

    int xCursor = kRackOuterPad;
    for (int i = 0; i < n && i < static_cast<int>(sections_.size()); ++i)
    {
        const int w = static_cast<int>(rackAvailableW * weights[i] / wSum);
        sections_[i].bounds = juce::Rectangle<int>(xCursor, rackTop, w, rackH);
        layoutSection(sections_[i]);
        xCursor += w + kRackGap;
    }

    // ── Header band ─────────────────────────────────────────────────
    if (header_.size() >= 3)
    {
        const int hY = (kHeaderH - kHeaderKnobSize - kKnobLabelH) / 2;
        const int hRight = getWidth() - 18;

        // LIM AMT (rightmost)
        auto& limAmt = *header_[2];
        const int limAmtX = hRight - kHeaderKnobSize;
        limAmt.slider->setBounds(limAmtX, hY, kHeaderKnobSize, kHeaderKnobSize);
        limAmt.label->setBounds(limAmtX - 4, hY + kHeaderKnobSize - 2,
                                kHeaderKnobSize + 8, kKnobLabelH);

        // OUT (next-left)
        auto& out = *header_[0];
        const int outX = limAmtX - kHeaderKnobSize - 14;
        out.slider->setBounds(outX, hY, kHeaderKnobSize, kHeaderKnobSize);
        out.label->setBounds(outX - 4, hY + kHeaderKnobSize - 2,
                             kHeaderKnobSize + 8, kKnobLabelH);

        // LIM toggle (to the left of OUT)
        auto& lim = *header_[1];
        lim.button->setBounds(outX - 64, kHeaderH / 2 - 11, 56, 22);
    }
}

void FaceplatePanel::layoutSection(Section& s)
{
    if (s.bounds.isEmpty() || s.controls.empty()) return;
    const auto inner = s.bounds.reduced(kColInnerPadX, 0)
                                .withTrimmedTop(kColInnerPadTop)
                                .withTrimmedBottom(kColInnerPadBot);
    if (inner.isEmpty()) return;

    int y = inner.getY();
    for (auto& cp : s.controls)
    {
        auto& c = *cp;
        const auto cell = juce::Rectangle<int>(inner.getX(), y,
                                               inner.getWidth(), kRowH);

        if (c.kind == CtlKind::Knob)
        {
            // Disc takes top (cell.h - labelH); label below.
            const int knobH = cell.getHeight() - kKnobLabelH - 2;
            const int knobW = juce::jmin(cell.getWidth(), knobH);
            const int knobX = cell.getX() + (cell.getWidth() - knobW) / 2;
            c.slider->setBounds(knobX, cell.getY(), knobW, knobH);
            c.label->setBounds(cell.getX(), cell.getY() + knobH,
                               cell.getWidth(), kKnobLabelH);
        }
        else if (c.kind == CtlKind::Choice)
        {
            // ComboBox + label centred in the row.
            const int comboH = 24;
            const int comboW = juce::jmin(cell.getWidth() - 6, 90);
            const int comboX = cell.getX() + (cell.getWidth() - comboW) / 2;
            const int comboY = cell.getY() + (cell.getHeight() - comboH - kKnobLabelH - 2) / 2;
            c.combo->setBounds(comboX, comboY, comboW, comboH);
            c.label->setBounds(cell.getX(), comboY + comboH + 2,
                               cell.getWidth(), kKnobLabelH);
        }
        else // Toggle
        {
            const int btnH = 22;
            c.button->setBounds(cell.getX(), cell.getY() + (cell.getHeight() - btnH) / 2,
                                cell.getWidth(), btnH);
        }
        y += kRowH;
    }
}

} // namespace bombo
