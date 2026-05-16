#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace bombo
{

// 7-column rack, one row. VOICE A · VOICE B · DRIVE · FILTER · DELAY ·
// REVERB · DUCK. Each knob renders its value inside the cap so the
// section column stays narrow.
class FaceplatePanel : public juce::Component
{
public:
    explicit FaceplatePanel(juce::AudioProcessorValueTreeState& apvts);
    ~FaceplatePanel() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    enum class CtlKind { Knob, Choice, Toggle };

    struct Control
    {
        CtlKind kind = CtlKind::Knob;
        // One of these is populated depending on `kind`.
        std::unique_ptr<juce::Slider>           slider;
        std::unique_ptr<juce::ComboBox>         combo;
        std::unique_ptr<juce::ToggleButton>     button;
        std::unique_ptr<juce::Label>            label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>   sAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> cAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>   bAtt;
    };

    struct Section
    {
        juce::String name;
        juce::Colour accent;
        juce::Rectangle<int> bounds;   // assigned in resized()
        std::vector<std::unique_ptr<Control>> controls;
    };

    Control* addKnob   (Section& s, const juce::String& paramId, const juce::String& displayName);
    Control* addChoice (Section& s, const juce::String& paramId, const juce::String& displayName);
    Control* addToggle (Section& s, const juce::String& paramId, const juce::String& displayName);
    void     layoutSection(Section& s);

    void paintHeader(juce::Graphics& g, juce::Rectangle<int> area);
    void paintSectionFrame(juce::Graphics& g, const Section& s);
    void paintFinBand(juce::Graphics& g, juce::Rectangle<int> area);

    juce::AudioProcessorValueTreeState& apvts_;
    std::vector<Section> sections_;
    // Header-band controls: master OUT + LIM toggle + LIM AMT.
    std::vector<std::unique_ptr<Control>> header_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaceplatePanel)
};

} // namespace bombo
