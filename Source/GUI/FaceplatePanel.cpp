#include "FaceplatePanel.h"

#include "Fonts.h"
#include "../Parameters.h"

namespace bombo
{

namespace
{
constexpr int kChassisW = 1320;
constexpr int kChassisH = 950;
constexpr int kHeaderH = 86;
constexpr int kSectionGap = 12;
constexpr int kSectionPadTop = 30;
constexpr int kSectionPadBottom = 14;
constexpr int kSectionPadSide = 14;
constexpr int kKnobW = 84;
constexpr int kKnobH = 74;
constexpr int kKnobLabelH = 14;
}

FaceplatePanel::FaceplatePanel(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts)
{
    // Section order is the visual rack order. Voice A + Voice B + MID
    // share the bone-tone "voice" colour. DRIVE/FILTER/DELAY/REVERB/DUCK
    // form the FX rack.
    sections_.reserve(7);

    // VOICE A
    {
        Section s; s.name = "VOICE A"; s.accent = col::voice;
        addChoice(s, apvts, pid::waveform,    "WAVE");
        addKnob  (s, apvts, pid::pitchStart,  "PITCH");
        addKnob  (s, apvts, pid::pitchEnd,    "END");
        addKnob  (s, apvts, pid::pitchDecay,  "P.DEC");
        addKnob  (s, apvts, pid::pitchCurve,  "CURVE");
        sections_.push_back(std::move(s));
    }
    // VOICE B
    {
        Section s; s.name = "VOICE B"; s.accent = col::voice;
        addKnob(s, apvts, pid::ampAttack,   "ATK");
        addKnob(s, apvts, pid::ampDecay,    "DEC");
        addKnob(s, apvts, pid::clickAmount, "CLICK");
        addKnob(s, apvts, pid::clickCenter, "CLK.F");
        addKnob(s, apvts, pid::noiseAmount, "BODY");
        addKnob(s, apvts, pid::noiseColor,  "COLOR");
        sections_.push_back(std::move(s));
    }
    // MID (smaller — overlapping VOICE territory but with its own panel)
    {
        Section s; s.name = "MID"; s.accent = col::voice;
        addKnob(s, apvts, pid::midPitchStart, "PITCH");
        addKnob(s, apvts, pid::midPitchEnd,   "END");
        addKnob(s, apvts, pid::midDecay,      "DEC");
        addKnob(s, apvts, pid::midLevel,      "LVL");
        addKnob(s, apvts, pid::driftAmount,   "DRIFT");
        sections_.push_back(std::move(s));
    }
    // DRIVE (voice + bus on the same panel — voice drive on the left,
    // bus drive on the right).
    {
        Section s; s.name = "DRIVE"; s.accent = col::drive;
        addKnob  (s, apvts, pid::driveAmount,   "V.AMT");
        addChoice(s, apvts, pid::driveMode,     "V.MODE");
        addKnob  (s, apvts, pid::fxDriveAmount, "B.AMT");
        addChoice(s, apvts, pid::fxDriveMode,   "B.MODE");
        addKnob  (s, apvts, pid::fxDriveMix,    "MIX");
        sections_.push_back(std::move(s));
    }
    // FILTER
    {
        Section s; s.name = "FILTER"; s.accent = col::filterC;
        addKnob(s, apvts, pid::filterHp,    "HP");
        addKnob(s, apvts, pid::filterHpQ,   "HP Q");
        addKnob(s, apvts, pid::filterLp,    "LP");
        addKnob(s, apvts, pid::filterLpQ,   "LP Q");
        addKnob(s, apvts, pid::filterColor, "COLOR");
        sections_.push_back(std::move(s));
    }
    // DELAY
    {
        Section s; s.name = "DELAY"; s.accent = col::delayC;
        addKnob(s, apvts, pid::delayTime,     "TIME");
        addKnob(s, apvts, pid::delayFeedback, "FBK");
        addKnob(s, apvts, pid::delayDrift,    "DRIFT");
        addKnob(s, apvts, pid::delayMorph,    "TONE");
        addKnob(s, apvts, pid::delayMix,      "MIX");
        sections_.push_back(std::move(s));
    }
    // REVERB
    {
        Section s; s.name = "REVERB"; s.accent = col::reverb;
        addKnob(s, apvts, pid::reverbSize,      "SIZE");
        addKnob(s, apvts, pid::reverbDecay,     "DECAY");
        addKnob(s, apvts, pid::reverbDamp,      "DAMP");
        addKnob(s, apvts, pid::reverbDiffusion, "DIFF");
        addKnob(s, apvts, pid::reverbPredelay,  "PRE");
        addKnob(s, apvts, pid::reverbMix,       "MIX");
        sections_.push_back(std::move(s));
    }
    // DUCK
    {
        Section s; s.name = "DUCK"; s.accent = col::duck;
        addKnob(s, apvts, pid::duckAtk,   "ATK");
        addKnob(s, apvts, pid::duckRel,   "REL");
        addKnob(s, apvts, pid::duckDepth, "DEPTH");
        sections_.push_back(std::move(s));
    }

    // Header: master volume + limiter on/amount. Stored at top so they
    // hover above the rack.
    masterOutKnob_ = addKnob(sections_[0], apvts, pid::masterOut, "OUT"); // placeholder slot
    // The masterOut knob will be repositioned manually in resized(). Move
    // it out of VOICE A's child list so it doesn't get gridded.
    auto& voiceA = sections_[0];
    std::unique_ptr<Knob> hero = std::move(voiceA.knobs.back());
    voiceA.knobs.pop_back();
    headerKnobs_.push_back(std::move(hero));
    masterOutKnob_ = headerKnobs_.back().get();

    {
        auto toggle = std::make_unique<ToggleCtl>();
        toggle->button.setButtonText("LIM");
        toggle->button.setColour(juce::ToggleButton::textColourId, col::boneDim);
        toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, pid::limiterOn, toggle->button);
        addAndMakeVisible(toggle->button);
        headerToggles_.push_back(std::move(toggle));
    }
    {
        auto knob = std::make_unique<Knob>();
        knob->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        knob->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 60, 12);
        knob->slider.setColour(juce::Slider::rotarySliderOutlineColourId, col::accentAmber);
        knob->label.setText("LIM AMT", juce::dontSendNotification);
        knob->label.setJustificationType(juce::Justification::centred);
        knob->label.setFont(fonts::label(9.5f));
        knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, pid::limiterAmount, knob->slider);
        addAndMakeVisible(knob->slider);
        addAndMakeVisible(knob->label);
        headerKnobs_.push_back(std::move(knob));
    }

    setSize(kChassisW, kChassisH);
}

FaceplatePanel::Knob* FaceplatePanel::addKnob(Section& s,
                                              juce::AudioProcessorValueTreeState& apvts,
                                              const juce::String& paramId,
                                              const juce::String& displayName)
{
    auto knob = std::make_unique<Knob>();
    knob->slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle(juce::Slider::TextBoxBelow, true, 60, 12);
    knob->slider.setColour(juce::Slider::rotarySliderOutlineColourId, s.accent);
    knob->label.setText(displayName, juce::dontSendNotification);
    knob->label.setJustificationType(juce::Justification::centred);
    knob->label.setFont(fonts::label(9.5f));
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        apvts, paramId, knob->slider);
    addAndMakeVisible(knob->slider);
    addAndMakeVisible(knob->label);
    auto* ptr = knob.get();
    s.knobs.push_back(std::move(knob));
    return ptr;
}

FaceplatePanel::ChoiceCtl* FaceplatePanel::addChoice(Section& s,
                                                    juce::AudioProcessorValueTreeState& apvts,
                                                    const juce::String& paramId,
                                                    const juce::String& displayName)
{
    auto choice = std::make_unique<ChoiceCtl>();
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramId)))
        choice->combo.addItemList(p->choices, 1);
    choice->combo.setColour(juce::ComboBox::textColourId, col::bone);
    choice->label.setText(displayName, juce::dontSendNotification);
    choice->label.setJustificationType(juce::Justification::centred);
    choice->label.setFont(fonts::label(9.5f));
    choice->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, paramId, choice->combo);
    addAndMakeVisible(choice->combo);
    addAndMakeVisible(choice->label);
    auto* ptr = choice.get();
    s.choices.push_back(std::move(choice));
    return ptr;
}

FaceplatePanel::ToggleCtl* FaceplatePanel::addToggle(Section& s,
                                                    juce::AudioProcessorValueTreeState& apvts,
                                                    const juce::String& paramId,
                                                    const juce::String& displayName)
{
    auto toggle = std::make_unique<ToggleCtl>();
    toggle->button.setButtonText(displayName);
    toggle->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, paramId, toggle->button);
    addAndMakeVisible(toggle->button);
    auto* ptr = toggle.get();
    s.toggles.push_back(std::move(toggle));
    return ptr;
}

void FaceplatePanel::paint(juce::Graphics& g)
{
    g.fillAll(col::graphite);

    // Header band.
    auto headerArea = juce::Rectangle<int>(0, 0, getWidth(), kHeaderH);
    g.setColour(col::graphiteHi);
    g.fillRect(headerArea);

    // Title.
    g.setColour(col::bone);
    g.setFont(fonts::title(34.0f));
    g.drawText("BOMBO",
               headerArea.reduced(24, 0),
               juce::Justification::centredLeft);
    g.setColour(col::boneDim);
    g.setFont(fonts::label(11.5f));
    g.drawText("half kick · half BBS",
               juce::Rectangle<int>(170, 0, 280, kHeaderH),
               juce::Justification::centredLeft);

    // Section frames.
    for (auto& s : sections_)
    {
        const auto fb = s.bounds.toFloat();
        g.setColour(col::graphiteHi);
        g.fillRoundedRectangle(fb, 6.0f);
        g.setColour(s.accent.withAlpha(0.6f));
        g.drawRoundedRectangle(fb.reduced(0.5f), 6.0f, 1.0f);

        // Section title strip.
        g.setColour(s.accent);
        g.fillRect(s.bounds.removeFromTop(2).toFloat());
        g.setColour(col::bone);
        g.setFont(fonts::title(13.5f));
        g.drawText(s.name,
                   s.bounds.expanded(0, 0).removeFromTop(kSectionPadTop)
                           .translated(0, 2),
                   juce::Justification::centred);
    }
}

void FaceplatePanel::resized()
{
    // Compute section bounds. The 8 sections (VOICE A, VOICE B, MID,
    // DRIVE, FILTER, DELAY, REVERB, DUCK) are arranged in two rows:
    //   Row 1: VOICE A + VOICE B + MID (top half, 3 wide panels)
    //   Row 2: DRIVE + FILTER + DELAY + REVERB + DUCK (FX rack)
    const int contentTop = kHeaderH + kSectionGap;
    const int contentBottom = getHeight() - kSectionGap;
    const int rowH = (contentBottom - contentTop - kSectionGap) / 2;

    // Row 1 — voice (3 sections)
    const int row1Y = contentTop;
    const int row1H = rowH;
    const int row1ColW = (getWidth() - kSectionGap * 4) / 3;
    for (int i = 0; i < 3 && i < static_cast<int>(sections_.size()); ++i)
    {
        sections_[i].bounds = { kSectionGap + i * (row1ColW + kSectionGap),
                                row1Y, row1ColW, row1H };
        layoutSection(sections_[i]);
    }

    // Row 2 — FX rack (5 sections — keep DUCK narrower).
    const int row2Y = row1Y + row1H + kSectionGap;
    const int row2H = rowH;
    const int row2Available = getWidth() - kSectionGap * 6;
    // 5 columns, ratio 1.05 : 1 : 1 : 1.1 : 0.85 (REVERB + DRIVE wider, DUCK slim).
    constexpr float ratios[5] = { 1.05f, 1.0f, 1.0f, 1.1f, 0.85f };
    float ratioSum = 0.0f;
    for (float r : ratios) ratioSum += r;
    int xCursor = kSectionGap;
    for (int i = 0; i < 5 && (3 + i) < static_cast<int>(sections_.size()); ++i)
    {
        const int w = static_cast<int>(row2Available * ratios[i] / ratioSum);
        sections_[3 + i].bounds = { xCursor, row2Y, w, row2H };
        layoutSection(sections_[3 + i]);
        xCursor += w + kSectionGap;
    }

    // Header — master OUT knob + LIM toggle on the right.
    const int hkRight = getWidth() - 24;
    if (headerKnobs_.size() >= 2)
    {
        auto& lim = *headerKnobs_[1];
        lim.slider.setBounds(hkRight - kKnobW, 6, kKnobW, kKnobH);
        lim.label.setBounds(hkRight - kKnobW, 6 + kKnobH, kKnobW, kKnobLabelH);

        auto& master = *headerKnobs_[0];
        const int masterX = hkRight - kKnobW * 2 - 18;
        master.slider.setBounds(masterX, 6, kKnobW, kKnobH);
        master.label.setText("OUT", juce::dontSendNotification);
        master.label.setBounds(masterX, 6 + kKnobH, kKnobW, kKnobLabelH);

        if (!headerToggles_.empty())
        {
            auto& t = headerToggles_[0]->button;
            t.setBounds(masterX - 80, kHeaderH / 2 - 12, 72, 24);
        }
    }
}

void FaceplatePanel::layoutSection(Section& s)
{
    // Pack knobs + choices + toggles in a flow row inside the section bounds.
    const auto inner = s.bounds.reduced(kSectionPadSide, 0)
                                .withTrimmedTop(kSectionPadTop)
                                .withTrimmedBottom(kSectionPadBottom);
    const int totalCtls = static_cast<int>(s.knobs.size()
                                         + s.choices.size()
                                         + s.toggles.size());
    if (totalCtls == 0 || inner.isEmpty()) return;

    const int cellW = inner.getWidth() / totalCtls;
    const int cellH = inner.getHeight();
    int idx = 0;

    auto placeKnob = [&](Knob& k) {
        const int cx = inner.getX() + idx * cellW;
        const auto cell = juce::Rectangle<int>(cx, inner.getY(), cellW, cellH);
        const int knobW = juce::jmin(kKnobW, cell.getWidth() - 4);
        const int knobX = cell.getX() + (cell.getWidth() - knobW) / 2;
        k.slider.setBounds(knobX, cell.getY(), knobW, cell.getHeight() - kKnobLabelH - 2);
        k.label.setBounds(cell.getX(), cell.getBottom() - kKnobLabelH,
                          cell.getWidth(), kKnobLabelH);
        ++idx;
    };

    auto placeChoice = [&](ChoiceCtl& c) {
        const int cx = inner.getX() + idx * cellW;
        const auto cell = juce::Rectangle<int>(cx, inner.getY(), cellW, cellH);
        const int comboW = juce::jmin(76, cell.getWidth() - 4);
        const int comboX = cell.getX() + (cell.getWidth() - comboW) / 2;
        // Combo sits center; label below.
        const int comboH = 24;
        c.combo.setBounds(comboX, cell.getY() + (cell.getHeight() - comboH) / 2 - 6,
                           comboW, comboH);
        c.label.setBounds(cell.getX(), cell.getBottom() - kKnobLabelH,
                          cell.getWidth(), kKnobLabelH);
        ++idx;
    };

    for (auto& kp : s.knobs)    placeKnob(*kp);
    for (auto& cp : s.choices)  placeChoice(*cp);
    for (auto& tp : s.toggles)
    {
        const int cx = inner.getX() + idx * cellW;
        tp->button.setBounds(cx, inner.getY() + (cellH - 24) / 2, cellW, 24);
        ++idx;
    }
}

} // namespace bombo
