#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "LayoutManager.h"   // LayoutElem + LayoutManager
#include "Nose/NoseComponent.h"
#include "PresetBarComponent.h"
#include "ScopeComponent.h"
#include "Theme/ThemedComponent.h"
#include "../DSP/FxOrder.h"

namespace bombo
{

class WaveBuffer;
class PresetBank;
class SampleSlotWidget;
class BpmDisplay;
class BalanceFader;
class DiceButton;
class LoopButton;

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
        std::function<void()>                  onLoadFactory;  // loadFactorySamples
    };

    // Host BPM accessor for the header's BPM display. Returns 0 in
    // standalone (so the display reverts to drag-editable param mode).
    using HostBpmFn = std::function<float()>;
    // Dice button click — wired by editor to BomboProcessor::randomizeBombo.
    using RandomizeFn = std::function<void()>;
    // Header bounce buttons — fired by user click on WAV / AIF pills.
    // Editor wires these to OfflineBouncer + FileChooser flow.
    using BounceFn = std::function<void()>;

    FaceplatePanel(juce::AudioProcessorValueTreeState& apvts,
                   const WaveBuffer* waveBuffer,
                   SampleSlotCallbacks sampleSlotCb = {},
                   HostBpmFn hostBpmFn = {},
                   RandomizeFn randomizeCb = {},
                   BounceFn bounceWavCb = {},
                   BounceFn bounceAiffCb = {});

    // Wire the factory preset bank. Constructs the PresetBarComponent and
    // adds it to the panel. Call once after construction; safe to call
    // before the first resized().
    void setPresetBank(PresetBank& bank);

    // Attach the editor-owned theme tile strip so it lives inside the
    // faceplate's transform/scale. Non-owning pointer — caller retains
    // ownership. Positioned in layoutHeader() at the right of the BOMBO
    // logo, inside headerBounds_. Pass nullptr to detach.
    void setThemeStrip(juce::Component* strip);
    // Defined out-of-line in .cpp so the forward-declared SampleSlotWidget
    // is complete by the time unique_ptr destructors instantiate.
    ~FaceplatePanel() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp  (const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&)  override;

    // Override theme-change handler to also refresh section label colors.
    void changeListenerCallback(juce::ChangeBroadcaster* bc) override;

    // Layout-edit hooks (Phase 2 — ported from an earlier project 2026-05-17).
    // BomboEditor owns the LayoutEditOverlay; FaceplatePanel exposes the
    // editable element list + a reference to its LayoutManager so the
    // overlay can mutate + persist bounds.
    std::vector<LayoutElem> getEditableElements() const;
    LayoutManager& getLayoutManager() noexcept { return layout_; }

    // Nose activation callbacks — wired by BomboEditor.
    std::function<void()>    onNoseActivated;
    std::function<void(int)> onNoseGlitchTap;

    // Fired at the end of resized() so BomboEditor can re-sync the BBS
    // overlay bounds whenever the layout editor moves/resizes the rack.
    std::function<void()> onRackBoundsChanged;

    // Forwarders so PluginEditor can set nose state without accessing the
    // private noseOverlay_ member directly.
    void setNoseProgressionLevel(int level) { noseOverlay_.setProgressionLevel(level); }
    void setNoseFirstEntryDone(bool done)   { noseOverlay_.setFirstEntryDone(done); }
    void setNoseForceResetReady(std::function<bool()> fn) { noseOverlay_.isForceResetReady = std::move(fn); }
    void setNoseForceResetCallback(std::function<void()> fn) { noseOverlay_.onForceReset = std::move(fn); }

    // Forwards to scope_'s reset-confirmation overlay so PluginEditor doesn't
    // need direct access to the private scope_ member.
    void flashScopeResetConfirmation() { scope_.showResetConfirmation(); }
    void refreshPresetBar() { if (presetBar_) presetBar_->refresh(); }

    // Inner chassis rectangle in faceplate design-space coordinates (pre-scale).
    // BomboEditor multiplies by the active scale transform to get editor-space bounds.
    juce::Rectangle<int> getChassisRectArea() const noexcept { return chassisRectArea_; }

    // Chassis geometry accessors — used by BomboEditor::paintOverChildren() to
    // re-paint the hull on top of any children that leaked past the bomb silhouette.
    const juce::Path& getChassisPath() const noexcept { return chassisPath_; }
    const juce::Path& getCapPath()     const noexcept { return capPath_; }
    const juce::Path& getFinPathL()    const noexcept { return finPathL_; }
    const juce::Path& getFinPathR()    const noexcept { return finPathR_; }
    int   getChassisApexY()  const noexcept { return chassisApexY_; }
    float getRedRegionTopY() const noexcept { return redRegionTopY_; }

    // Union of all FX column rectBounds — the "square effects section" (rack only,
    // excluding header, scope, and nose/macro area). Pre-scale design-space coords.
    juce::Rectangle<int> getRackBounds() const noexcept
    {
        if (sections_.empty()) return {};
        auto b = sections_.front().rectBounds;
        for (const auto& s : sections_)
            b = b.getUnion(s.rectBounds);
        return b;
    }

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

        // Out-of-line ctor/dtor so unique_ptr<SampleSlotWidget> can stay
        // a forward-declaration here — libc++ instantiates default_delete's
        // sizeof check at the implicit destructor site, which broke macOS CI.
        Control();
        ~Control();
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
        // Reorderable sections are the four FX (DRIVE/FILTER/DELAY/REVERB).
        // VOICE A / VOICE B / DUCK stay pinned to their column slots.
        bool                 reorderable = false;
        // Only meaningful when reorderable == true; identifies the DSP stage
        // this section drives so visualOrder_ can be mapped to FxOrder.
        FxId                 fxId = FxId::Drive;
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
    // Place the 7 macros inside the bomb's nose region (the red-painted
    // tapered area below redRegionTopY_). OUT sits as the hero (larger)
    // at the top-center, flanked by macros 0 + 1; macros 2/3 below;
    // macros 4/5 at the narrowest tip row.
    void layoutMacrosInNose(juce::Rectangle<int> noseInterior);

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
    void paintSection    (juce::Graphics& g, const Section& s,
                          bool roundLeft, bool roundRight);

    juce::AudioProcessorValueTreeState& apvts_;

    // Sample-slot bridge callbacks. Set by the editor at construction so
    // the panel can ask the processor to load/clear/query the VOICE B
    // sample without depending on the BomboProcessor header.
    SampleSlotCallbacks sampleSlotCb_;

    // FX rack — 7 columns left to right. sections_ is the *stable* identity
    // list (insertion order: VOICE A, VOICE B, DRIVE, DELAY, REVERB, FILTER,
    // DUCK). Visual layout uses visualOrder_ as an index permutation, so
    // sections_ never needs to be re-shuffled by the drag code.
    std::vector<Section> sections_;

    // Permutation of section indices → column position. visualOrder_[c] is
    // the sections_ index drawn at column c. Slots 0, 1, and the last slot
    // are pinned to the fixed sections (VOICE A, VOICE B, DUCK); the middle
    // four slots hold the reorderable FX in user-set order. Default is the
    // identity {0,1,2,3,4,5,6}.
    std::vector<int> visualOrder_;

public:
    // Push a new FX order into the rack. Updates visualOrder_, triggers a
    // re-layout, and is idempotent if `o` already matches the current order.
    // Used both by drag-commit (UI → DSP) and by preset / DAW-state restore
    // (DSP → UI). Validation is performed in DSP::setFxOrder.
    void applyFxOrder(FxOrder o);

    // Current FX order derived from visualOrder_.
    FxOrder currentFxOrder() const noexcept;

    // Fired when the user commits a drag-reorder. PluginEditor wires this
    // to BomboProcessor::setFxOrder so DSP and persistence follow the UI.
    std::function<void(FxOrder)> onFxOrderChanged;

private:

    // Mini-Nuke chassis silhouette (R4B-CLASSIC, locked 2026-05-17).
    // chassisPath_ is the unified body egg-shape from rear-cap area to
    // the rounded nose tip — ONE continuous path (no body/nose seam).
    // cap + fin paths are separate so they can render BEHIND the body,
    // with the body's outline hiding their attachment edges.
    juce::Path             chassisPath_;
    // Pre-rasterised alpha mask of chassisPath_ — used by paint() to
    // hard-clip the rack so VOICE A / DUCK outer corners can never
    // overhang the bomb silhouette. Rebuilt in resized() each time the
    // panel changes size; reused for free per repaint. The image-mask
    // form of reduceClipRegion is pixel-perfect and immune to the
    // path-rasterisation anti-alias seams that let earlier
    // reduceClipRegion(chassisPath_) attempts leak at the curves.
    juce::Image            chassisMask_;
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
    std::unique_ptr<juce::ToggleButton> tailPill_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> tailAtt_;
    std::unique_ptr<LoopButton>         loopBtn_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopAtt_;
    std::unique_ptr<BpmDisplay>         bpmDisplay_;
    std::unique_ptr<BalanceFader>       balanceFader_;
    std::unique_ptr<DiceButton>         diceButton_;
    // Single BNC bounce pill — popup on click selects WAV or AIFF, remembers choice.
    std::unique_ptr<juce::TextButton>   bncPill_;
    BounceFn bounceWavCb_;
    BounceFn bounceAiffCb_;
    bool lastBounceIsAiff_ = false;
    juce::Rectangle<int> headerBounds_;

    // Scope.
    ScopeComponent scope_;
    juce::Rectangle<int> scopeBounds_;

    // Nose overlay — transparent component sized to the nose region,
    // handles the 7-tap activation sequence and glitch visuals.
    NoseComponent noseOverlay_;

    // Factory preset bar (Phase 3). nullptr until setPresetBank is called.
    std::unique_ptr<PresetBarComponent> presetBar_;
    juce::Rectangle<int> presetBarBounds_;

    // In-skin theme tile strip. Non-owning pointer; PluginEditor holds the
    // unique_ptr. Positioned in layoutHeader() at the right side of
    // headerBounds_ so it inherits the chassis transform/scale.
    juce::Component* themeStripPtr_ = nullptr;

    // Macro row — 7 controls, aligned 1:1 with FX columns. The seventh
    // (rightmost) is bound to master OUT and tinted amber as the hero.
    // The other six are visual placeholders until macro fan-out is wired
    // (see TODO.md F3/F8). Each lives in `macro_[i]` so column i's macro
    // can be re-aligned without a list shuffle.
    std::array<std::unique_ptr<Control>, 7> macro_{};

    // ── Header drag-to-reorder state ────────────────────────────────
    // Tap-vs-drag disambiguation: mouseDown on a draggable title arms
    // the gesture but does not toggle mute; if the cursor moves past
    // kDragThresholdPx the gesture commits to drag (mute intent
    // cancelled), otherwise mouseUp fires the mute toggle as before.
    enum class DragState : std::uint8_t { Idle, Armed, Dragging };
    DragState           dragState_         = DragState::Idle;
    int                 dragOriginSection_ = -1;   // sections_ index being dragged
    int                 dragOriginColumn_  = -1;   // column at drag start
    juce::Point<int>    dragStartPos_;             // mouse pos at mouseDown
    juce::Point<int>    dragCurrentPos_;           // live mouse pos during drag
    // Undo stack of fx-order snapshots; capped at kUndoStackCap. Pushed
    // immediately before each commit; Ctrl+Z pops and re-applies.
    std::vector<FxOrder> orderUndoStack_;

    // Returns sections_ index of the reorderable section whose title
    // strip currently contains `pt`, or -1.
    int hitTestReorderableTitle(juce::Point<int> pt) const noexcept;
    // Returns the column position (visualOrder_ index) that the dragged
    // section should occupy given a cursor X. Clamped to the reorderable
    // slot range (slots between the first and last fixed sections).
    int insertionColumnForX(int mouseX) const noexcept;
    // Commit a swap that moves the section currently at fromCol into
    // toCol; the section displaced rolls into fromCol. Both must be
    // reorderable slots.
    void swapReorderableColumns(int fromCol, int toCol);
    // Push the current FxOrder onto the undo stack (capped).
    void pushOrderUndoSnapshot();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FaceplatePanel)
};

} // namespace bombo
