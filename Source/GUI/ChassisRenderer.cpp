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

    // Procedural mottled-camo fill for BodyStyle::Camo. Soft overlapping
    // blobs in three tones derived from the body palette plus a faint accent
    // tint, clipped to the silhouette. Fixed RNG seed so the pattern is stable
    // across repaints (no shimmer). A light grain pass on top breaks up the
    // blob edges so it reads as material, not polka dots.
    void drawCamo(juce::Graphics& g, const Ctx& ctx)
    {
        juce::Graphics::ScopedSaveState ss(g);
        g.reduceClipRegion(ctx.chassisPath);
        // Keep the nose cone clean — camo only on the upper body. The nose
        // (the noseRed region below redRegionTopY) stays a solid colour.
        g.excludeClipRegion(juce::Rectangle<int>(0, ctx.redRegionTopY,
                                                 ctx.panelWidth,
                                                 ctx.panelHeight - ctx.redRegionTopY));
        const auto b = ctx.chassisPath.getBounds();
        if (b.isEmpty()) return;

        const juce::Colour tones[3] = {
            col::bodyHi().brighter(0.10f),
            col::bodyLo().darker(0.14f),
            col::accentAmber().withSaturation(0.55f).darker(0.35f)
        };
        const float alphas[3] = { 0.45f, 0.50f, 0.22f };

        juce::Random rng((juce::int64) 0xB0BB0);
        constexpr int kBlobs = 46;
        for (int i = 0; i < kBlobs; ++i)
        {
            const int   t = rng.nextInt(3);
            const float w = b.getWidth() * (0.18f + rng.nextFloat() * 0.34f);
            const float h = w * (0.55f + rng.nextFloat() * 0.95f);
            const float x = b.getX() + rng.nextFloat() * b.getWidth()  - w * 0.5f;
            const float y = b.getY() + rng.nextFloat() * b.getHeight() - h * 0.5f;
            g.setColour(tones[t].withAlpha(alphas[t]));
            g.fillEllipse(x, y, w, h);
        }

        const auto overlay = loadOverlayImage();
        if (overlay.isValid())
        {
            g.setOpacity(0.18f);
            g.drawImage(overlay, b, juce::RectanglePlacement::stretchToFit);
        }
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

    // Chassis interior treatment (per-theme bodyStyle):
    //   Grain — baked scratches/AO/grain overlay at chassisOverlayOpacity.
    //   Camo  — procedural mottled blobs (drawCamo) so the body reads as
    //           material rather than flat black (esp. on neon themes).
    //   Flat  — body gradient only.
    switch (col::bodyStyle())
    {
        case BodyStyle::Flat:
            break;

        case BodyStyle::Camo:
            drawCamo(g, ctx);
            break;

        case BodyStyle::Grain:
        default:
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
            break;
        }
    }

    // Silhouette outline — a constant-thickness frame tracing the whole body
    // perimeter so the black areas around the rack read as a shaped unit. On
    // neon themes it's a thin accent-coloured frame (the neon "edge" the flat
    // black body was missing); on classic themes a soft bone stroke.
    {
        juce::Graphics::ScopedSaveState ss(g);
        const bool neon = col::isNeon();
        g.setColour(neon ? col::accentAmber().withAlpha(0.90f)
                         : col::bone().withAlpha(0.65f));
        g.strokePath(ctx.chassisPath, juce::PathStrokeType(neon ? 1.2f : 1.5f));
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
