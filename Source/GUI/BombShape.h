#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bombo::BombShape
{

// All coordinates are in a REFERENCE canvas of 360 × 640 (9:16 IG-Reels
// native), matching tools/bombshape_gen.py — the parametric SVG generator
// used during the design brainstorm. The build* functions below scale
// these reference coordinates to the actual bounds passed in.
//
// Defaults are R4B-CLASSIC, locked 2026-05-17. See memory file
// project_bombo_silhouette_locked_r4b_classic.md for the design history.
constexpr float kRefW = 360.0f;
constexpr float kRefH = 640.0f;
constexpr float kRefAspect = kRefW / kRefH;   // = 9.0/16.0 = 0.5625

struct Params
{
    // Body (egg-shape ovoid) — bulged egg from cap-area to bottom shoulder
    float bodyTopY       = 92.0f;
    float bodyBotY       = 460.0f;
    float bodyTopW       = 100.0f;
    float bodyBotW       = 150.0f;
    float bodyBulgeW     = 290.0f;
    float bodyBulgeYFrac = 0.52f;

    // Tip — unified silhouette continues from body bottom shoulder down to here
    float tipY           = 600.0f;
    // 0 = very rounded teardrop, 1 = sharp point
    float tipSharpness   = 0.78f;

    // Rear cap (drawn BEHIND body so body's top edge hides the seam)
    float capTopY        = 22.0f;
    float capBotY        = 94.0f;
    float capW           = 92.0f;
    float capInnerW      = 64.0f;

    // Red paint region (clipped to body silhouette)
    float redRegionTopY  = 462.0f;

    // Yellow hazard band
    float bandTopY       = 106.0f;
    float bandBotY       = 150.0f;
    float bandInsetX     = 8.0f;

    // Side fins (square_chamfered style)
    float finTopY        = 34.0f;
    float finBotY        = 128.0f;
    float finTipYFrac    = 0.5f;
    float finOutX        = 22.0f;
    float finChamferFrac = 0.42f;
};

// ── Path builders ──────────────────────────────────────────────────────
// Build the unified body silhouette: ONE continuous egg-shape path from
// cap-area through bulge to tip. Outline is seamless — caller draws the
// red "nose" as a clipped paint region inside this same path.
juce::Path buildBombPath(juce::Rectangle<float> bounds,
                         const Params& p = Params{});

// Build the rear cap (sits BEHIND body — body's top edge covers seam).
juce::Path buildCapPath(juce::Rectangle<float> bounds,
                        const Params& p = Params{});

// Build one fin. side = -1 for left, +1 for right.
juce::Path buildFinPath(juce::Rectangle<float> bounds, int side,
                        const Params& p = Params{});

// ── Region helpers ─────────────────────────────────────────────────────
// Yellow hazard band rectangle in actual-bounds coordinates.
juce::Rectangle<float> bandRect(juce::Rectangle<float> bounds,
                                const Params& p = Params{});

// Y where the red paint region begins (in actual-bounds coordinates).
float redRegionTopYInBounds(juce::Rectangle<float> bounds,
                            const Params& p = Params{});

// ── Inscribed-rect helper for UI inhabitability ────────────────────────
// Approximate body width at a given reference y, interpolating through
// the egg bulge. Used by FaceplatePanel to size header/macro/rack into
// the silhouette's inscribed interior.
float bodyWidthAtRefY(float refY, const Params& p = Params{});

// Actual body width in bounds coords at the given bounds-y.
float bodyWidthAt(float boundsY, juce::Rectangle<float> bounds,
                  const Params& p = Params{});

} // namespace bombo::BombShape
