#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bombo::chassisRenderer
{
    // Geometry context for the chassis renderer. All paths/rectangles are
    // computed by FaceplatePanel::resized() (via BombShape) and passed in
    // by reference each frame. The renderer is stateless and pure.
    struct Ctx
    {
        const juce::Path&      chassisPath;     // unified body silhouette (egg + tail)
        const juce::Path&      capPath;         // rear cap (sits behind body)
        const juce::Path&      finPathL;        // left fin
        const juce::Path&      finPathR;        // right fin
        juce::Rectangle<int>   chassisRectArea; // inscribed UI region
        int                    chassisApexY;    // gradient anchor
        float                  redRegionTopY;   // y-split: graphite above, red below
        int                    panelWidth;
        int                    panelHeight;
    };

    // Clears the panel to transparent black. The shaped corners outside
    // chassisPath stay alpha=0 so the OS compositor (standalone) or host
    // background (plugin) shows through and the window reads as a bomb
    // silhouette.
    void drawBackground(juce::Graphics& g);

    // Rear cap + two red fins. MUST be drawn BEFORE drawChassis so the
    // body silhouette hides their attachment edges (matches the
    // tools/bombshape_gen.py render order).
    void drawCapAndFins(juce::Graphics& g, const Ctx& ctx);

    // Body gradient (bodyHi → bodyLo) filled into chassisPath.
    void drawChassis(juce::Graphics& g, const Ctx& ctx);

    // Red nose region: a rectangle fill clipped to chassisPath, with a
    // thin dark ring line at redRegionTopY marking the paint split.
    void drawRedRegion(juce::Graphics& g, const Ctx& ctx);
}
