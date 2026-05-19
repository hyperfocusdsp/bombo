#include "ChassisRenderer.h"

#include "Colours.h"

namespace bombo::chassisRenderer
{

void drawBackground(juce::Graphics& g)
{
    g.fillAll(juce::Colours::transparentBlack);
}

void drawCapAndFins(juce::Graphics& g, const Ctx& ctx)
{
    if (ctx.capPath.isEmpty()) return;
    g.setColour(col::cap());
    g.fillPath(ctx.capPath);
    g.setColour(col::noseRed());
    g.fillPath(ctx.finPathL);
    g.fillPath(ctx.finPathR);
}

void drawChassis(juce::Graphics& g, const Ctx& ctx)
{
    if (ctx.chassisPath.isEmpty()) return;

    // Body gradient: bodyHi (top) → bodyLo (apex). The 0.78 stop holds the
    // tail end at bodyLo so the saturation reads as a solid block at the
    // tail tip, not a fade-to-black.
    juce::ColourGradient grad(col::bodyHi(),
                              static_cast<float>(ctx.panelWidth) * 0.5f,
                              static_cast<float>(ctx.chassisRectArea.getY()),
                              col::bodyLo(),
                              static_cast<float>(ctx.panelWidth) * 0.5f,
                              static_cast<float>(ctx.chassisApexY),
                              false);
    grad.addColour(0.78, col::bodyLo());
    g.setGradientFill(grad);
    g.fillPath(ctx.chassisPath);

    // Silhouette outline — pulls the bomb shape together against any
    // backdrop (Hyprland transparency, dark wallpaper, light wallpaper).
    // Without this stroke the dark graphite chassis dissolves into a
    // dark host window and the rack columns read as floating tiles.
    g.setColour(col::bone().withAlpha(0.65f));
    g.strokePath(ctx.chassisPath, juce::PathStrokeType(1.5f));
}

void drawRedRegion(juce::Graphics& g, const Ctx& ctx)
{
    if (ctx.chassisPath.isEmpty()) return;
    g.saveState();
    g.reduceClipRegion(ctx.chassisPath);

    g.setColour(col::noseRed());
    g.fillRect(juce::Rectangle<float>(0.0f,
                                      ctx.redRegionTopY,
                                      static_cast<float>(ctx.panelWidth),
                                      static_cast<float>(ctx.panelHeight) - ctx.redRegionTopY));

    g.restoreState();
}

} // namespace bombo::chassisRenderer
