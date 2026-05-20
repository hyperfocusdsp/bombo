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

    // Single-pass fill of the entire chassis: vertical gradient bodyHi → bodyLo
    // (body region) with a sharp ~1px transition into noseRed at redRegionTopY
    // (nose region). Using ONE fillPath instead of body-gradient + orange-rect
    // overlay eliminates the horizontal AA seam that previously produced a thin
    // diagonal line at the corners where the orange rect's top edge crossed
    // the silhouette diagonal. There is no horizontal boundary inside the path
    // and the silhouette anti-aliasing is computed identically top-to-bottom.
    const float topY  = static_cast<float>(ctx.chassisRectArea.getY());
    const float apexY = static_cast<float>(ctx.chassisApexY);
    const float redY  = static_cast<float>(ctx.redRegionTopY);

    juce::ColourGradient grad(col::bodyHi(),
                              static_cast<float>(ctx.panelWidth) * 0.5f,
                              topY,
                              col::noseRed(),
                              static_cast<float>(ctx.panelWidth) * 0.5f,
                              apexY,
                              false);

    if (apexY > topY)
    {
        const double redNorm = juce::jlimit(0.0, 1.0,
            static_cast<double>((redY - topY) / (apexY - topY)));
        // 1px-equivalent epsilon — keeps the body→orange transition AA to
        // a single pixel without colliding stops at the same position.
        const double eps = juce::jmax(0.001, 1.0 / static_cast<double>(apexY - topY));
        // Hold bodyLo for the lower portion of the body region — preserves
        // the solid block at the body tail (replaces the old 0.78 stop).
        const double holdNorm = juce::jmax(0.0, redNorm - 0.20);
        grad.addColour(holdNorm,            col::bodyLo());
        grad.addColour(redNorm - eps,       col::bodyLo());
        grad.addColour(redNorm + eps,       col::noseRed());
    }

    g.setGradientFill(grad);
    g.fillPath(ctx.chassisPath);

    // Silhouette outline — bone stroke above the orange region only.
    // Below redRegionTopY, the noseRed fill is full alpha and its chassisPath
    // edge anti-aliases directly against the host bg, providing the silhouette
    // there with no stroke. A stroke below would be visible either inside
    // (contrast against orange, theme-dependent) or outside (darker outline
    // against host bg) — both have been observed across all themes.
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.excludeClipRegion(juce::Rectangle<int>(0,
                                                  static_cast<int>(redY),
                                                  ctx.panelWidth,
                                                  ctx.panelHeight - static_cast<int>(redY)));
        g.setColour(col::bone().withAlpha(0.65f));
        g.strokePath(ctx.chassisPath, juce::PathStrokeType(1.5f));
    }
}

void drawRedRegion(juce::Graphics& /*g*/, const Ctx& /*ctx*/)
{
    // Body and nose are now painted in a single fillPath inside drawChassis()
    // — the prior body-gradient + orange-fillRect-overlay produced a visible
    // diagonal AA seam at the silhouette corners (see bug parked 2026-05-19).
    // Kept as a no-op so existing paint-order call sites stay valid.
}

} // namespace bombo::chassisRenderer
