#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

#include "Colours.h"

namespace bombo
{

// Top-level chassis. Lays out the 7 FX sections + voice rows + header on
// a fixed 1320×950 grid matching the Rust Bombo dimensions. Section
// colours from col:: drive the tick rings.
class FaceplatePanel : public juce::Component
{
public:
    explicit FaceplatePanel(juce::AudioProcessorValueTreeState& apvts);
    ~FaceplatePanel() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    struct ChoiceCtl
    {
        juce::ComboBox  combo;
        juce::Label     label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;
    };
    struct ToggleCtl
    {
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };

    struct Section
    {
        juce::String      name;
        juce::Colour      accent;
        juce::Rectangle<int> bounds;
        // Children are owned by Section so they're added/removed as a unit.
        std::vector<std::unique_ptr<Knob>>      knobs;
        std::vector<std::unique_ptr<ChoiceCtl>> choices;
        std::vector<std::unique_ptr<ToggleCtl>> toggles;
    };

    Knob*      addKnob   (Section& s, juce::AudioProcessorValueTreeState& apvts,
                          const juce::String& paramId, const juce::String& displayName);
    ChoiceCtl* addChoice (Section& s, juce::AudioProcessorValueTreeState& apvts,
                          const juce::String& paramId, const juce::String& displayName);
    ToggleCtl* addToggle (Section& s, juce::AudioProcessorValueTreeState& apvts,
                          const juce::String& paramId, const juce::String& displayName);
    void       layoutSection(Section& s);

    juce::AudioProcessorValueTreeState& apvts_;
    std::vector<Section> sections_;
    std::vector<std::unique_ptr<Knob>>      headerKnobs_;
    std::vector<std::unique_ptr<ToggleCtl>> headerToggles_;
    Knob* masterOutKnob_ = nullptr;  // hero (header right side).

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaceplatePanel)
};

} // namespace bombo
