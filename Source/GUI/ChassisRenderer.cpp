#include "ChassisRenderer.h"

#include "Colours.h"

#include <BinaryData.h>

#include <juce_graphics/juce_graphics.h>

namespace bombo::chassisRenderer
{

namespace
{
    // Baked surface texture (scratches + suspension band + edge AO + grain + spec).
    // Loaded once via ImageCache; redraws after the body gradient and clipped to
    // chassisPath so it never bleeds outside the silhouette. Master alpha comes
    // from the active palette (col::chassisOverlayOpacity) so dark themes can
    // dial it back to ~0.20 while VAULT/BANDW carry it at ~0.55.
    juce::Image loadOverlayImage()
    {
        return juce::ImageCache::getFromMemory(BinaryData::chassis_overlay_png,
                                               BinaryData::chassis_overlay_pngSize);
    }
}

void drawBackground(juce::Graphics& g)
{
    // Normal use: keep the corners transparent so the bomb silhouette reads
    // as a shaped window against any DAW background. Marketing-screenshot
    // mode (BOMBO_SOLID_BG=1) fills with a brand-dark backdrop so grim
    // captures clean corners instead of bleeding through to the host
    // workspace behind the standalone window.
    if (std::getenv("BOMBO_SOLID_BG") != nullptr)
        g.fillAll(juce::Colour(0xFF14161B));
    else
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

    // Surface texture overlay (chassis_overlay.png), clipped to the silhouette
    // so the baked scratches + AO + grain only paint inside the bomb. Per-theme
    // opacity lets dark palettes (NIGHTRUN/MATRIX/CYBER/PLASMA) dial it back.
    {
        const float opacity = col::chassisOverlayOpacity();
        if (opacity > 0.001f)
        {
            const auto overlay = loadOverlayImage();
            if (overlay.isValid())
            {
                juce::Graphics::ScopedSaveState ss(g);
                g.reduceClipRegion(ctx.chassisPath);
                g.setOpacity(juce::jlimit(0.0f, 1.0f, opacity));
                g.drawImage(overlay,
                            ctx.chassisPath.getBounds(),
                            juce::RectanglePlacement::stretchToFit);
            }
        }
    }

    // Silhouette outline — bone stroke above the orange region only.
    // Skipped on neon themes: bone IS the vivid neon colour there, which
    // produces a bright neon border the user doesn't want. The silhouette
    // reads fine on neon via the body gradient contrast alone.
    if (!col::isNeon())
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
