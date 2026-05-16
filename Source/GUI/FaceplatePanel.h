#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "ScopeComponent.h"

namespace bombo
{

class WaveBuffer;

// Bombo faceplate. Four bands stacked top-to-bottom:
//   1. Header  — BOMBO logo, subtitle, LIM/MNT pills, SYNTH/FX tab
//   2. Scope   — recessed post-master oscilloscope strip (30 Hz redraw)
//   3. Macro   — 7 small knobs aligned 1:1 with the FX columns below
//   4. Rack    — 7 colour-coded FX columns. Each column paints as a
//                5-vertex polygon: rectangle on top, taper-to-a-point fin
//                at the bottom. Gaps between fins show the graphite chassis
//                through, forming the bomb-tail silhouette.
class FaceplatePanel : public juce::Component
{
public:
    FaceplatePanel(juce::AudioProcessorValueTreeState& apvts,
                   const WaveBuffer* waveBuffer);
    ~FaceplatePanel() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

private:
    enum class CtlKind { Knob, Choice, Toggle };

    struct Control
    {
        CtlKind kind = CtlKind::Knob;
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
        juce::String         name;
        juce::String         moduleId;        // e.g. "AMP-1" — printed below the rack rect
        juce::String         mutePid;         // APVTS param ID for the mute toggle ("" = not muteable)
        juce::Colour         accent;          // column body fill
        juce::Colour         labelOnBg;       // ink for saturated bg, bone for dark bg
        juce::Rectangle<int> rectBounds;      // column rectangle — no per-column fin
        juce::Rectangle<int> titleBounds;     // clickable title strip at the top of rectBounds
        juce::Rectangle<int> moduleIdBounds;  // narrow strip at the bottom of rectBounds
        std::vector<std::unique_ptr<Control>> controls;
    };

    Control* addKnob   (Section& s, const juce::String& paramId, const juce::String& displayName,
                        juce::Colour labelColour);
    Control* addChoice (Section& s, const juce::String& paramId, const juce::String& displayName,
                        juce::Colour labelColour);

    // Standalone control creators (not bound to a Section).
    std::unique_ptr<Control> makeBoundKnob(const juce::String& paramId,
                                           const juce::String& displayName,
                                           juce::Colour capColour,
                                           juce::Colour labelColour);
    std::unique_ptr<Control> makePlaceholderKnob(const juce::String& displayName,
                                                 juce::Colour labelColour);

    void layoutSection(Section& s);
    void layoutHeader (juce::Rectangle<int> area);
    void layoutMacros (juce::Rectangle<int> area);

    // Macro fan-out — each macro slider's onValueChange routes the
    // current normalised position into a curated set of underlying
    // APVTS params. Called once from the constructor after the macro
    // controls are constructed.
    void wireMacroFanOut();

    // Convenience writer: takes a plain (un-normalised) value and routes
    // it through the param's convertTo0to1 with proper change gestures.
    void writeParam(const juce::String& paramId, float plainValue);

    // Returns true when the section's mute BoolParam is currently on.
    bool isMuted(const Section& s) const noexcept;
    void toggleMute(const Section& s);

    void paintBackground (juce::Graphics& g);
    void paintChassis    (juce::Graphics& g);
    void paintChassisTail(juce::Graphics& g);
    void paintHeader     (juce::Graphics& g, juce::Rectangle<int> area);
    void paintSection    (juce::Graphics& g, const Section& s);

    juce::AudioProcessorValueTreeState& apvts_;

    // FX rack — 7 columns left to right.
    std::vector<Section> sections_;

    // Single chassis shape: rounded-top rectangle that tapers to a
    // bottom-centre apex (the bomb-tail silhouette). Outside this path
    // the panel paints a darker backdrop so the JUCE resize-corner sits
    // on visible empty space in the bottom-right.
    juce::Path           chassisPath_;
    juce::Rectangle<int> chassisRectArea_;   // top rectangular portion (excludes tail)
    int                  chassisRectBottomY_ = 0;
    int                  chassisApexY_       = 0;

    // Header pills.
    std::unique_ptr<juce::ToggleButton> limPill_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limAtt_;
    juce::Rectangle<int> mntPillBounds_;       // painted, not a real component
    juce::Rectangle<int> synthTabBounds_;
    juce::Rectangle<int> insertFxTabBounds_;
    juce::Rectangle<int> headerBounds_;

    // Scope.
    ScopeComponent scope_;
    juce::Rectangle<int> scopeBounds_;

    // Macro row — 7 controls, aligned 1:1 with FX columns. The seventh
    // (rightmost) is bound to master OUT and tinted amber as the hero.
    // The other six are visual placeholders until macro fan-out is wired
    // (see TODO.md F3/F8). Each lives in `macro_[i]` so column i's macro
    // can be re-aligned without a list shuffle.
    std::array<std::unique_ptr<Control>, 7> macro_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaceplatePanel)
};

} // namespace bombo
