#include "LayoutEditOverlay.h"
#include "LayoutManager.h"
#include "FaceplatePanel.h"

namespace bombo
{

static constexpr int kResizeHandle = 8;

LayoutEditOverlay::LayoutEditOverlay (FaceplatePanel& face)
    : faceplate (face)
{
    setInterceptsMouseClicks (false, false);
    setVisible (false);
}

void LayoutEditOverlay::setEditMode (bool shouldBeActive)
{
    editMode = shouldBeActive;
    setVisible (shouldBeActive);
    setInterceptsMouseClicks (shouldBeActive, false);
    if (shouldBeActive)
        refreshElements();
    else
    {
        selection.clear();
        activeGuides.clear();
        draggingIndex = -1;
    }
    repaint();
}

bool LayoutEditOverlay::handleKey (const juce::KeyPress& key)
{
    if (! editMode) return false;

    if (key == juce::KeyPress::escapeKey)
    {
        selection.clear();
        repaint();
        return true;
    }

    if (key.getModifiers().isCtrlDown() && ! key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'Z')
    {
        if (popUndo()) return true;
    }
    if (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'Z')
    {
        if (popRedo()) return true;
    }

    if (key.getKeyCode() == 'L')
    {
        const auto& m = key.getModifiers();
        if (m.isCtrlDown() && ! m.isShiftDown()) { setLockAll (true);  return true; }
        if (m.isShiftDown() && ! m.isCtrlDown()) { setLockAll (false); return true; }
        if (! m.isCtrlDown() && ! m.isShiftDown())
        {
            toggleLockOnSelection();
            return true;
        }
    }

    if (key.getModifiers().isCtrlDown() && key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'D')
    {
        auto& lm = faceplate.getLayoutManager();
        for (const auto& el : elements)
        {
            lm.setBounds (el.id, el.bounds, el.type);
            if (el.locked) lm.setLocked (el.id, true);
        }
        lm.save();
        return true;
    }

    if (! selection.empty()
        && (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey))
    {
        pushUndoSnapshot();
        auto& lm = faceplate.getLayoutManager();
        for (int idx : selection)
        {
            if (elements[(size_t) idx].locked) continue;
            juce::Rectangle<int> empty;
            elements[(size_t) idx].bounds = empty;
            lm.setBounds (elements[(size_t) idx].id, empty, elements[(size_t) idx].type);
        }
        lm.save();
        faceplate.resized();
        faceplate.repaint();
        selection.clear();
        refreshElements();
        repaint();
        return true;
    }

    if (key.getModifiers().isCtrlDown() && ! key.getModifiers().isShiftDown()
        && key.getKeyCode() == 'A')
    {
        for (int i = 0; i < (int) elements.size(); ++i)
            if (! elements[(size_t) i].locked)
                selection.insert (i);
        repaint();
        return true;
    }

    const int kc = key.getKeyCode();
    const bool isArrow = (kc == juce::KeyPress::leftKey  || kc == juce::KeyPress::rightKey
                       || kc == juce::KeyPress::upKey    || kc == juce::KeyPress::downKey);
    if (isArrow && ! selection.empty())
    {
        const int step = key.getModifiers().isShiftDown() ? 10 : 1;
        int dx = 0, dy = 0;
        if (kc == juce::KeyPress::leftKey)  dx = -step;
        if (kc == juce::KeyPress::rightKey) dx =  step;
        if (kc == juce::KeyPress::upKey)    dy = -step;
        if (kc == juce::KeyPress::downKey)  dy =  step;

        pushUndoSnapshot();

        auto& lm = faceplate.getLayoutManager();
        for (int idx : selection)
        {
            if (elements[(size_t) idx].locked) continue;
            auto nb = elements[(size_t) idx].bounds.translated (dx, dy);
            elements[(size_t) idx].bounds = nb;
            lm.setBounds (elements[(size_t) idx].id, nb, elements[(size_t) idx].type);
        }
        lm.save();
        faceplate.resized();
        faceplate.repaint();
        repaint();
        return true;
    }

    return false;
}

std::vector<juce::Rectangle<int>> LayoutEditOverlay::currentBoundsSnapshot() const
{
    std::vector<juce::Rectangle<int>> snap;
    snap.reserve (elements.size());
    for (auto& e : elements) snap.push_back (e.bounds);
    return snap;
}

void LayoutEditOverlay::pushUndoSnapshot()
{
    undoStack.push_back (currentBoundsSnapshot());
    if ((int) undoStack.size() > kUndoCap)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void LayoutEditOverlay::applyBoundsToAll (const std::vector<juce::Rectangle<int>>& snap)
{
    if (snap.size() != elements.size()) return;
    auto& lm = faceplate.getLayoutManager();
    for (size_t i = 0; i < elements.size(); ++i)
    {
        elements[i].bounds = snap[i];
        lm.setBounds (elements[i].id, snap[i], elements[i].type);
    }
    lm.save();
    faceplate.resized();
    faceplate.repaint();
    repaint();
}

bool LayoutEditOverlay::popUndo()
{
    if (undoStack.empty()) return false;
    redoStack.push_back (currentBoundsSnapshot());
    if ((int) redoStack.size() > kUndoCap)
        redoStack.erase (redoStack.begin());
    auto snap = std::move (undoStack.back());
    undoStack.pop_back();
    applyBoundsToAll (snap);
    return true;
}

bool LayoutEditOverlay::popRedo()
{
    if (redoStack.empty()) return false;
    undoStack.push_back (currentBoundsSnapshot());
    if ((int) undoStack.size() > kUndoCap)
        undoStack.erase (undoStack.begin());
    auto snap = std::move (redoStack.back());
    redoStack.pop_back();
    applyBoundsToAll (snap);
    return true;
}

void LayoutEditOverlay::refreshElements()
{
    elements = faceplate.getEditableElements();
    moveStartAll.assign (elements.size(), {});
}

int LayoutEditOverlay::hitTest (juce::Point<int> p) const
{
    for (int i = (int) elements.size() - 1; i >= 0; --i)
    {
        const auto& el = elements[(size_t) i];
        if (el.locked) continue;
        if (el.bounds.contains (p)) return i;
    }
    return -1;
}

void LayoutEditOverlay::toggleLockOnSelection()
{
    if (selection.empty()) return;
    auto& lm = faceplate.getLayoutManager();
    bool anyUnlocked = false;
    for (int idx : selection)
        if (! elements[(size_t) idx].locked) { anyUnlocked = true; break; }
    const bool newState = anyUnlocked;
    for (int idx : selection)
    {
        elements[(size_t) idx].locked = newState;
        lm.setLocked (elements[(size_t) idx].id, newState);
    }
    lm.save();
    if (newState) selection.clear();
    repaint();
}

void LayoutEditOverlay::setLockAll (bool locked)
{
    auto& lm = faceplate.getLayoutManager();
    for (auto& el : elements)
    {
        el.locked = locked;
        lm.setLocked (el.id, locked);
    }
    lm.save();
    if (locked) selection.clear();
    repaint();
}

void LayoutEditOverlay::paint (juce::Graphics& g)
{
    if (! editMode) return;

    g.fillAll (juce::Colours::black.withAlpha (0.18f));

    const float scale = std::max (0.25f, std::abs (getTransform().mat00));
    const float px    = 1.0f / scale;

    juce::Font font (juce::FontOptions()
                         .withName (juce::Font::getDefaultMonospacedFontName())
                         .withHeight (9.0f)
                         .withStyle ("Bold"));
    g.setFont (font);

    for (size_t i = 0; i < elements.size(); ++i)
    {
        const auto& elem = elements[i];
        auto r = elem.bounds.toFloat();

        const bool isSelected = selection.count ((int) i) > 0;
        const bool isDragging = draggingIndex == (int) i;
        const bool isLocked   = elem.locked;

        juce::Colour col = juce::Colours::cyan;
        if (isLocked)        col = juce::Colours::grey;
        else if (isDragging) col = juce::Colours::orange;
        else if (isSelected) col = juce::Colours::yellow;

        g.setColour (col.withAlpha (isSelected ? 0.10f : 0.05f));
        g.fillRect (r);

        g.setColour (col.withAlpha (0.75f));
        float dashes[] = { 1.0f * px, 5.0f * px };
        juce::Path p;
        p.addRectangle (r);
        juce::Path dashed;
        juce::PathStrokeType (px).createDashedStroke (dashed, p, dashes, 2);
        g.strokePath (dashed, juce::PathStrokeType (px));

        if (isLocked)
        {
            const float pw = 6.0f, ph = 5.0f;
            const float px0 = r.getX() + 2.0f;
            const float py0 = r.getY() + 2.0f;
            g.setColour (juce::Colours::black.withAlpha (0.8f));
            g.fillRect (px0 - 0.5f, py0 - 0.5f, pw + 1.0f, ph + 1.0f);
            g.setColour (juce::Colours::lightgrey);
            g.fillRect (px0, py0, pw, ph);
            g.setColour (juce::Colours::black);
            g.drawEllipse (px0 + 1.0f, py0 - 2.5f, pw - 2.0f, 4.0f, px);
        }
        else
        {
            auto hnd = juce::Rectangle<float> (r.getRight() - kResizeHandle,
                                               r.getBottom() - kResizeHandle,
                                               kResizeHandle, kResizeHandle);
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.fillRect (hnd);
        }

        auto shortId = elem.id.fromLastOccurrenceOf (".", false, false);
        auto labelArea = elem.bounds.withSizeKeepingCentre (
            juce::jmax (40, elem.bounds.getWidth()), 10);
        int labelY = elem.bounds.getY() - 11;
        if (labelY < 0) labelY = elem.bounds.getY() + 2;
        labelArea.setY (labelY);

        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (labelArea.reduced (-2, -1));
        g.setColour (juce::Colours::white);
        g.drawText (shortId, labelArea, juce::Justification::centred, false);
    }

    g.setColour (juce::Colours::magenta.withAlpha (0.9f));
    for (auto& ln : activeGuides)
        g.drawLine ((float) ln.getStartX(), (float) ln.getStartY(),
                    (float) ln.getEndX(),   (float) ln.getEndY(), px);

    if (rubberBanding && (rubberBand.getWidth() > 0 || rubberBand.getHeight() > 0))
    {
        g.setColour (juce::Colours::white.withAlpha (0.12f));
        g.fillRect (rubberBand.toFloat());
        float rbDashes[] = { 2.0f * px, 4.0f * px };
        juce::Path rbPath;
        rbPath.addRectangle (rubberBand.toFloat());
        juce::Path rbDashed;
        juce::PathStrokeType (px).createDashedStroke (rbDashed, rbPath, rbDashes, 2);
        g.setColour (juce::Colours::white.withAlpha (0.75f));
        g.strokePath (rbDashed, juce::PathStrokeType (px));
    }

    const int nSel = (int) selection.size();
    auto status = juce::String ("LAYOUT EDIT  --  F2 exits  drag=marquee  Ctrl+A=all")
                      + "   sel:" + juce::String (nSel)
                      + "  undo:" + juce::String ((int) undoStack.size())
                      + "  redo:" + juce::String ((int) redoStack.size());
    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRect (0, 0, getWidth(), 18);
    g.setColour (juce::Colours::white);
    g.drawText (status, juce::Rectangle<int> (4, 0, getWidth() - 8, 18),
                juce::Justification::centredLeft, false);
}

LayoutEditOverlay::SnapResult LayoutEditOverlay::bestSnap (const std::vector<int>& movingEdges,
                                                           const std::vector<int>& staticEdges,
                                                           int threshold)
{
    SnapResult best;
    int bestAbs = threshold + 1;
    for (int mv : movingEdges)
        for (int st : staticEdges)
        {
            int diff = st - mv;
            int a = std::abs (diff);
            if (a <= threshold && a < bestAbs)
            {
                bestAbs = a;
                best.delta = diff;
                best.guideCoord = st;
                best.active = true;
            }
        }
    return best;
}

void LayoutEditOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! editMode) return;
    auto p = e.getPosition();

    if (e.mods.isPopupMenu())
    {
        int hitAny = -1;
        for (int i = (int) elements.size() - 1; i >= 0; --i)
            if (elements[(size_t) i].bounds.contains (p)) { hitAny = i; break; }
        if (hitAny < 0) return;

        const bool wasLocked = elements[(size_t) hitAny].locked;
        juce::PopupMenu m;
        m.addSectionHeader (elements[(size_t) hitAny].id);
        m.addItem (1, wasLocked ? "Unlock" : "Lock");

        auto& lm = faceplate.getLayoutManager();
        const int idx = hitAny;
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                         [this, &lm, idx, wasLocked] (int choice)
                         {
                             if (choice != 1) return;
                             elements[(size_t) idx].locked = ! wasLocked;
                             lm.setLocked (elements[(size_t) idx].id, ! wasLocked);
                             lm.save();
                             if (! wasLocked) selection.erase (idx);
                             repaint();
                         });
        return;
    }

    int hit = hitTest (p);
    if (hit < 0)
    {
        if (! e.mods.isCtrlDown()) selection.clear();
        draggingIndex = -1;
        rubberBanding = true;
        rubberOrigin = p;
        rubberBand = {};
        repaint();
        return;
    }

    if (e.mods.isCtrlDown())
    {
        if (selection.count (hit) > 0)
        {
            selection.erase (hit);
            draggingIndex = -1;
            repaint();
            return;
        }
        selection.insert (hit);
    }
    else
    {
        if (selection.count (hit) == 0) selection = { hit };
    }

    pushUndoSnapshot();

    draggingIndex = hit;
    startBounds = elements[(size_t) hit].bounds;
    dragAnchor = p;

    auto hnd = juce::Rectangle<int> (startBounds.getRight() - kResizeHandle,
                                     startBounds.getBottom() - kResizeHandle,
                                     kResizeHandle, kResizeHandle);
    resizing = hnd.contains (p) || e.mods.isShiftDown();

    moveStartAll.assign (elements.size(), {});
    for (int idx : selection)
        moveStartAll[(size_t) idx] = elements[(size_t) idx].bounds;

    repaint();
}

void LayoutEditOverlay::mouseDrag (const juce::MouseEvent& e)
{
    if (! editMode) return;

    if (rubberBanding)
    {
        auto p = e.getPosition();
        rubberBand = juce::Rectangle<int> (juce::jmin (rubberOrigin.x, p.x),
                                           juce::jmin (rubberOrigin.y, p.y),
                                           std::abs (p.x - rubberOrigin.x),
                                           std::abs (p.y - rubberOrigin.y));
        repaint();
        return;
    }

    if (draggingIndex < 0) return;

    const bool snapOn = ! e.mods.isAltDown();
    auto delta = e.getPosition() - dragAnchor;
    activeGuides.clear();

    auto& lm = faceplate.getLayoutManager();

    if (resizing)
    {
        int newW = juce::jmax (4, startBounds.getWidth()  + delta.x);
        int newH = juce::jmax (4, startBounds.getHeight() + delta.y);

        const bool freeResize = e.mods.isShiftDown();
        if (! freeResize && startBounds.getHeight() > 0 && startBounds.getWidth() > 0)
        {
            const float aspect = (float) startBounds.getWidth()
                               / (float) startBounds.getHeight();
            if (std::abs (delta.x) >= std::abs (delta.y))
                newH = juce::jmax (4, (int) std::round ((float) newW / aspect));
            else
                newW = juce::jmax (4, (int) std::round ((float) newH * aspect));
        }

        if (snapOn)
        {
            newW = ((newW + gridSize / 2) / gridSize) * gridSize;
            newH = ((newH + gridSize / 2) / gridSize) * gridSize;

            std::vector<int> staticX, staticY;
            for (size_t i = 0; i < elements.size(); ++i)
            {
                if ((int) i == draggingIndex) continue;
                auto b = elements[i].bounds;
                staticX.push_back (b.getX());
                staticX.push_back (b.getRight());
                staticY.push_back (b.getY());
                staticY.push_back (b.getBottom());
            }

            int right  = startBounds.getX() + newW;
            int bottom = startBounds.getY() + newH;
            auto sx = bestSnap ({ right },  staticX, snapThresholdPx);
            auto sy = bestSnap ({ bottom }, staticY, snapThresholdPx);
            if (sx.active) { newW += sx.delta;
                activeGuides.push_back ({ { sx.guideCoord, 0 }, { sx.guideCoord, getHeight() } }); }
            if (sy.active) { newH += sy.delta;
                activeGuides.push_back ({ { 0, sy.guideCoord }, { getWidth(), sy.guideCoord } }); }
        }

        const int dw = newW - startBounds.getWidth();
        const int dh = newH - startBounds.getHeight();

        for (int idx : selection)
        {
            auto& s = moveStartAll[(size_t) idx];
            juce::Rectangle<int> nb (s.getX(), s.getY(),
                                     juce::jmax (4, s.getWidth()  + dw),
                                     juce::jmax (4, s.getHeight() + dh));
            elements[(size_t) idx].bounds = nb;
            lm.setBounds (elements[(size_t) idx].id, nb, elements[(size_t) idx].type);
        }
    }
    else
    {
        juce::Rectangle<int> moving = startBounds + delta;

        if (snapOn)
        {
            auto snapToGrid = [this] (int v)
            {
                return ((v + (v >= 0 ? gridSize / 2 : -gridSize / 2)) / gridSize) * gridSize;
            };
            moving.setPosition (snapToGrid (moving.getX()), snapToGrid (moving.getY()));

            std::vector<int> staticX, staticY;
            for (size_t i = 0; i < elements.size(); ++i)
            {
                if (selection.count ((int) i) > 0) continue;
                auto b = elements[i].bounds;
                staticX.push_back (b.getX());
                staticX.push_back (b.getRight());
                staticY.push_back (b.getY());
                staticY.push_back (b.getBottom());
            }

            auto sx = bestSnap ({ moving.getX(), moving.getRight(), moving.getCentreX() },
                                staticX, snapThresholdPx);
            auto sy = bestSnap ({ moving.getY(), moving.getBottom(), moving.getCentreY() },
                                staticY, snapThresholdPx);
            if (sx.active) { moving.translate (sx.delta, 0);
                activeGuides.push_back ({ { sx.guideCoord, 0 }, { sx.guideCoord, getHeight() } }); }
            if (sy.active) { moving.translate (0, sy.delta);
                activeGuides.push_back ({ { 0, sy.guideCoord }, { getWidth(), sy.guideCoord } }); }
        }

        auto snappedDelta = moving.getPosition() - startBounds.getPosition();

        for (int idx : selection)
        {
            auto nb = moveStartAll[(size_t) idx] + snappedDelta;
            elements[(size_t) idx].bounds = nb;
            lm.setBounds (elements[(size_t) idx].id, nb, elements[(size_t) idx].type);
        }
    }

    faceplate.resized();
    faceplate.repaint();
    repaint();
}

void LayoutEditOverlay::mouseUp (const juce::MouseEvent& e)
{
    if (! editMode) return;

    if (rubberBanding)
    {
        rubberBanding = false;
        if (rubberBand.getWidth() > 4 || rubberBand.getHeight() > 4)
        {
            if (! e.mods.isCtrlDown()) selection.clear();
            for (int i = 0; i < (int) elements.size(); ++i)
                if (! elements[(size_t) i].locked && rubberBand.intersects (elements[(size_t) i].bounds))
                    selection.insert (i);
        }
        rubberBand = {};
        repaint();
        return;
    }

    if (draggingIndex < 0) return;
    auto& lm = faceplate.getLayoutManager();
    lm.save();
    draggingIndex = -1;
    resizing = false;
    activeGuides.clear();
    refreshElements();
    repaint();
}

} // namespace bombo
