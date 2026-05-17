#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "LayoutManager.h"   // LayoutElem + LayoutManager
#include "PresetBarComponent.h"
#include "ScopeComponent.h"
#include "Theme/ThemedComponent.h"

namespace bombo
{

class WaveBuffer;
class PresetBank;
class SampleSlotWidget;
class BpmDisplay;
class BalanceFader;
class DiceButton;

// Bombo faceplate. Four bands stacked top-to-bottom:
//   1. Header  — BOMBO logo, DICE/LIM/LOOP/BPM pills (always-visible, no pages)
//   2. Scope   — recessed post-master oscilloscope strip (30 Hz redraw)
//   3. Macro   — 7 small knobs aligned 1:1 with the FX columns below
//   4. Rack    — 7 colour-coded FX columns. Each column paints as a
//                5-vertex polygon: rectangle on top, taper-to-a-point fin
//                at the bottom. Gaps between fins show the graphite chassis
//                through, forming the bomb-tail silhouette.
class FaceplatePanel : public juce::Component, public bombo::ThemedComponent
{
public:
    // Folder-browse sample-slot callbacks. The widget itself is decoupled
    // from BomboProcessor; the editor wires these through.
    struct SampleSlotCallbacks
    {
        std::function<void(const juce::File&)> onBrowsePick;   // setVoiceBSampleFolder
        std::function<void(int)>               onIndexChange;  // loadVoiceBSampleByIndex
        std::function<void()>                  onClear;        // clearVoiceBSample
        std::function<juce::StringArray()>     getNames;       // voiceBSampleNames
        std::function<int()>                   getCurrentIdx;  // voiceBSampleIndex
    };

    // Host BPM accessor for the header's BPM display. Returns 0 in
    // standalone (so the display reverts to drag-editable param mode).
    using HostBpmFn = std::function<float()>;
    // Dice button click — wired by editor to BomboProcessor::randomizeBombo.
    using RandomizeFn = std::function<void()>;

    FaceplatePanel(juce::AudioProcessorValueTreeState& apvts,
                   const WaveBuffer* waveBuffer,
                   SampleSlotCallbacks sampleSlotCb = {},
                   HostBpmFn hostBpmFn = {},
                   RandomizeFn randomizeCb = {});

    // Wire the factory preset bank. Constructs the PresetBarComponent and
    // adds it to the panel. Call once after construction; safe to call
    // before the first resized().
    void setPresetBank(PresetBank& bank);
    // Defined out-of-line in .cpp so the forward-declared SampleSlotWidget
    // is complete by the time unique_ptr destructors instantiate.
    ~FaceplatePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;

    // Layout-edit hooks (Phase 2 — ported from an earlier project 2026-05-17).
    // BomboEditor owns the LayoutEditOverlay; FaceplatePanel exposes the
    // editable element list + a reference to its LayoutManager so the
    // overlay can mutate + persist bounds.
    std::vector<LayoutElem> getEditableElements() const;
    LayoutManager& getLayoutManager() noexcept { return layout_; }

private:
    enum class CtlKind { Knob, Choice, Toggle, SampleSlot };

    struct Control
    {
        CtlKind kind = CtlKind::Knob;
        std::unique_ptr<juce::Slider>           slider;
        std::unique_ptr<juce::ComboBox>         combo;
        std::unique_ptr<juce::ToggleButton>     button;
        std::unique_ptr<juce::Label>            label;
        std::unique_ptr<SampleSlotWidget>       sampleSlot;
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

    Control* addKnob       (Section& s, const juce::String& paramId, const juce::String& displayName,
                            juce::Colour labelColour);
    Control* addChoice     (Section& s, const juce::String& paramId, const juce::String& displayName,
                            juce::Colour labelColour);
    Control* addSampleSlot (Section& s, const juce::String& displayName,
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

    // The chassis/cap/fins/red-region/band/header are rendered by stateless
    // free-function renderers in ChassisRenderer.{h,cpp} + HeaderRenderer.{h,cpp}.
    // paint() composes them in order; paintScopeFrame and paintSection stay
    // here because they reach into FaceplatePanel state (scopeBounds_, sections_).
    void paintScopeFrame (juce::Graphics& g);
    void paintSection    (juce::Graphics& g, const Section& s);

    juce::AudioProcessorValueTreeState& apvts_;

    // Sample-slot bridge callbacks. Set by the editor at construction so
    // the panel can ask the processor to load/clear/query the VOICE B
    // sample without depending on the BomboProcessor header.
    SampleSlotCallbacks sampleSlotCb_;

    // FX rack — 7 columns left to right.
    std::vector<Section> sections_;

    // Mini-Nuke chassis silhouette (R4B-CLASSIC, locked 2026-05-17).
    // chassisPath_ is the unified body egg-shape from rear-cap area to
    // the rounded nose tip — ONE continuous path (no body/nose seam).
    // cap + fin paths are separate so they can render BEHIND the body,
    // with the body's outline hiding their attachment edges.
    juce::Path             chassisPath_;
    juce::Path             capPath_;
    juce::Path             finPathL_;
    juce::Path             finPathR_;
    juce::Rectangle<float> bandRect_{};
    float                  redRegionTopY_ = 0.0f;
    juce::Rectangle<int>   chassisRectArea_;   // inscribed UI region for child widgets
    int                    chassisRectBottomY_ = 0;
    int                    chassisApexY_       = 0;

    // Layout manifest — JSON-loaded overrides for major UI block bounds.
    // resized() consults layout_.boundsOr(id, default) for each editable
    // block before applying. See Source/GUI/LayoutEditOverlay.* for the
    // drag-to-edit UI; BomboEditor owns the overlay component.
    LayoutManager layout_;

    // Macro/rack/section bounds tracked here so getEditableElements() can
    // expose them to LayoutEditOverlay without recomputing.
    juce::Rectangle<int> macroBoundsTracked_{};
    juce::Rectangle<int> bandBoundsTracked_{};

    // Header pills.
    std::unique_ptr<juce::ToggleButton> limPill_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> limAtt_;
    std::unique_ptr<juce::ToggleButton> loopBtn_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt_;
    std::unique_ptr<BpmDisplay>         bpmDisplay_;
    std::unique_ptr<BalanceFader>       balanceFader_;
    std::unique_ptr<DiceButton>         diceButton_;
    juce::Rectangle<int> headerBounds_;

    // Scope.
    ScopeComponent scope_;
    juce::Rectangle<int> scopeBounds_;

    // Factory preset bar (Phase 3). nullptr until setPresetBank is called.
    std::unique_ptr<PresetBarComponent> presetBar_;
    juce::Rectangle<int> presetBarBounds_;

    // Macro row — 7 controls, aligned 1:1 with FX columns. The seventh
    // (rightmost) is bound to master OUT and tinted amber as the hero.
    // The other six are visual placeholders until macro fan-out is wired
    // (see TODO.md F3/F8). Each lives in `macro_[i]` so column i's macro
    // can be re-aligned without a list shuffle.
    std::array<std::unique_ptr<Control>, 7> macro_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaceplatePanel)
};

} // namespace bombo
