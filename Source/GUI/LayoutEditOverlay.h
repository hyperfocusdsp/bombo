#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <set>
#include <functional>
#include "LayoutManager.h"      // LayoutElem lives here

namespace bombo
{

class FaceplatePanel;

/** In-plugin "layout edit mode" overlay. Ported from squelch_pro 2026-05-17.

    Activation: BomboEditor toggles via F2 or Ctrl+Shift+E.

    Mouse:
      Click select · Ctrl+click toggle · Esc clear · drag to move ·
      Shift+drag or bottom-right handle resize (Shift unlocks aspect lock) ·
      Alt disables snap · right-click for per-element lock menu

    Keys:
      Arrows nudge 1px (Shift = 10px) · L toggle lock · Ctrl+L lock all ·
      Shift+L unlock all · Ctrl+Z undo · Ctrl+Shift+Z redo · Delete/Backspace
      hide selection (0×0) · Ctrl+Shift+D dump all to Layout.json
*/
class LayoutEditOverlay : public juce::Component
{
public:
    explicit LayoutEditOverlay (FaceplatePanel& face);

    void setEditMode (bool shouldBeActive);
    bool isEditMode() const noexcept { return editMode; }

    bool handleKey (const juce::KeyPress& key);

    void paint (juce::Graphics& g) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp   (const juce::MouseEvent& e) override;

    void toggleLockOnSelection();
    void setLockAll (bool locked);

    void refreshElements();

private:
    int  hitTest (juce::Point<int> p) const;

    struct SnapResult
    {
        int delta = 0;
        int guideCoord = 0;
        bool active = false;
    };

    static SnapResult bestSnap (const std::vector<int>& movingEdges,
                                const std::vector<int>& staticEdges,
                                int threshold);

    FaceplatePanel& faceplate;
    std::vector<LayoutElem> elements;

    bool editMode = false;

    std::set<int> selection;
    int  draggingIndex = -1;
    bool resizing = false;
    juce::Point<int> dragAnchor;
    juce::Rectangle<int> startBounds;
    std::vector<juce::Rectangle<int>> moveStartAll;

    int  gridSize = 4;
    int  snapThresholdPx = 4;

    std::vector<juce::Line<int>> activeGuides;

    std::vector<std::vector<juce::Rectangle<int>>> undoStack;
    std::vector<std::vector<juce::Rectangle<int>>> redoStack;
    static constexpr int kUndoCap = 100;

    void pushUndoSnapshot();
    bool popUndo();
    bool popRedo();
    std::vector<juce::Rectangle<int>> currentBoundsSnapshot() const;
    void applyBoundsToAll (const std::vector<juce::Rectangle<int>>& snap);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LayoutEditOverlay)
};

} // namespace bombo
