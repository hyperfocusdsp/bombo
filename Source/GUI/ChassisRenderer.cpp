#include "ChassisRenderer.h"

#include "Colours.h"
#include "Fonts.h"

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

    // Boundary ring line — designed marker at the paint split.
    g.setColour(col::ink().withAlpha(0.6f));
    g.drawLine(0.0f, ctx.redRegionTopY,
               static_cast<float>(ctx.panelWidth), ctx.redRegionTopY, 1.5f);

    g.restoreState();
}

void drawBand(juce::Graphics& g, const Ctx& ctx)
{
    if (ctx.bandRect.isEmpty()) return;
    g.saveState();
    g.reduceClipRegion(ctx.chassisPath);
    g.setColour(col::bandYellow());
    g.fillRect(ctx.bandRect);

    const float bx = ctx.bandRect.getX();
    const float by = ctx.bandRect.getY();
    const float bw = ctx.bandRect.getWidth();
    const float bh = ctx.bandRect.getHeight();
    g.setColour(col::ink());
    g.setFont(fonts::value(bh * 0.32f));
    g.drawText("BOMBO-TEC",
               juce::Rectangle<float>(bx, by + bh * 0.10f, bw, bh * 0.42f),
               juce::Justification::centred, false);
    g.setFont(fonts::value(bh * 0.18f));
    g.drawText("PEACE EDITION · 1992 · FOSS",
               juce::Rectangle<float>(bx, by + bh * 0.55f, bw, bh * 0.40f),
               juce::Justification::centred, false);
    g.restoreState();
}

} // namespace bombo::chassisRenderer
