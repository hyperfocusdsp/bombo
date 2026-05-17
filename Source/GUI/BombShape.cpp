#include "BombShape.h"

namespace bombo::BombShape
{

namespace
{

// Scale helper — maps reference-canvas coords (360 × 640) to actual bounds.
struct Scaler
{
    juce::Rectangle<float> bounds;
    float sx, sy;

    static Scaler make(juce::Rectangle<float> b)
    {
        return Scaler{b, b.getWidth() / kRefW, b.getHeight() / kRefH};
    }
    float x(float refX) const { return bounds.getX() + refX * sx; }
    float y(float refY) const { return bounds.getY() + refY * sy; }
};

// Smoothstep for body-width interpolation (matches Python helper).
float smoothstep(float t)
{
    t = juce::jlimit(0.0f, 1.0f, t);
    return t * t * (3.0f - 2.0f * t);
}

} // anonymous namespace

float bodyWidthAtRefY(float refY, const Params& p)
{
    const float bulgeY = p.bodyTopY + (p.bodyBotY - p.bodyTopY) * p.bodyBulgeYFrac;

    if (refY <= p.bodyTopY)   return p.bodyTopW;
    if (refY >= p.bodyBotY)   return p.bodyBotW;

    if (refY <= bulgeY)
    {
        const float t = smoothstep((refY - p.bodyTopY) / juce::jmax(1.0f, bulgeY - p.bodyTopY));
        return p.bodyTopW + (p.bodyBulgeW - p.bodyTopW) * t;
    }
    // Lower half — bulge to bot
    const float t = smoothstep((refY - bulgeY) / juce::jmax(1.0f, p.bodyBotY - bulgeY));
    return p.bodyBulgeW + (p.bodyBotW - p.bodyBulgeW) * t;
}

float bodyWidthAt(float boundsY, juce::Rectangle<float> bounds, const Params& p)
{
    const float refY = (boundsY - bounds.getY()) * (kRefH / bounds.getHeight());
    return bodyWidthAtRefY(refY, p) * (bounds.getWidth() / kRefW);
}

juce::Path buildBombPath(juce::Rectangle<float> bounds, const Params& p)
{
    const Scaler sc = Scaler::make(bounds);
    const float c  = kRefW * 0.5f;
    const float bulgeY = p.bodyTopY + (p.bodyBotY - p.bodyTopY) * p.bodyBulgeYFrac;

    // Anchor points in reference space
    const float topLX = c - p.bodyTopW * 0.5f;
    const float topRX = c + p.bodyTopW * 0.5f;
    const float midLX = c - p.bodyBulgeW * 0.5f;
    const float midRX = c + p.bodyBulgeW * 0.5f;
    const float lowLX = c - p.bodyBotW * 0.5f;
    const float lowRX = c + p.bodyBotW * 0.5f;

    // Bezier tangent biases — must match unified_silhouette_path in
    // tools/bombshape_gen.py exactly.
    constexpr float pullUpper = 0.6f;
    const float upperPullY = p.bodyTopY + (bulgeY - p.bodyTopY) * pullUpper;
    const float lowerPullY = bulgeY + (p.bodyBotY - bulgeY) * 0.4f;

    const float tipRound = p.bodyBotW * (0.55f - 0.45f * p.tipSharpness);
    const float neckExt  = (p.tipY - p.bodyBotY) * (0.55f + 0.25f * (1.0f - p.tipSharpness));

    juce::Path path;
    path.startNewSubPath(sc.x(topLX), sc.y(p.bodyTopY));
    path.lineTo(sc.x(topRX), sc.y(p.bodyTopY));

    // Right side: top_r → mid_r (upper egg curve)
    path.cubicTo(sc.x(midRX), sc.y(upperPullY),
                 sc.x(midRX), sc.y(upperPullY),
                 sc.x(midRX), sc.y(bulgeY));
    // mid_r → low_r (lower egg curve)
    path.cubicTo(sc.x(midRX), sc.y(lowerPullY),
                 sc.x(midRX), sc.y(lowerPullY),
                 sc.x(lowRX), sc.y(p.bodyBotY));
    // low_r → tip (continuous taper into tip)
    path.cubicTo(sc.x(lowRX),         sc.y(p.bodyBotY + neckExt),
                 sc.x(c + tipRound),  sc.y(p.tipY),
                 sc.x(c),             sc.y(p.tipY));
    // tip → low_l (mirror)
    path.cubicTo(sc.x(c - tipRound),  sc.y(p.tipY),
                 sc.x(lowLX),         sc.y(p.bodyBotY + neckExt),
                 sc.x(lowLX),         sc.y(p.bodyBotY));
    // low_l → mid_l
    path.cubicTo(sc.x(midLX), sc.y(lowerPullY),
                 sc.x(midLX), sc.y(lowerPullY),
                 sc.x(midLX), sc.y(bulgeY));
    // mid_l → top_l
    path.cubicTo(sc.x(midLX), sc.y(upperPullY),
                 sc.x(midLX), sc.y(upperPullY),
                 sc.x(topLX), sc.y(p.bodyTopY));
    path.closeSubPath();
    return path;
}

juce::Path buildCapPath(juce::Rectangle<float> bounds, const Params& p)
{
    const Scaler sc = Scaler::make(bounds);
    const float c = kRefW * 0.5f;

    juce::Path path;
    path.startNewSubPath(sc.x(c - p.capW * 0.5f), sc.y(p.capTopY));
    path.lineTo(sc.x(c + p.capW * 0.5f),          sc.y(p.capTopY));
    path.lineTo(sc.x(c + p.bodyTopW * 0.5f),      sc.y(p.capBotY));
    path.lineTo(sc.x(c - p.bodyTopW * 0.5f),      sc.y(p.capBotY));
    path.closeSubPath();
    return path;
}

juce::Path buildFinPath(juce::Rectangle<float> bounds, int side, const Params& p)
{
    // side: -1 for left, +1 for right
    jassert(side == -1 || side == 1);
    const Scaler sc = Scaler::make(bounds);
    const float c = kRefW * 0.5f;

    const float innerTopW = bodyWidthAtRefY(p.finTopY, p);
    const float innerBotW = bodyWidthAtRefY(p.finBotY, p);
    const float innerTopX = c + side * innerTopW * 0.5f;
    const float innerBotX = c + side * innerBotW * 0.5f;

    const float farX = c + side * (p.bodyBulgeW * 0.5f + p.finOutX);

    // square_chamfered style — rectangular fin with chamfered bottom-outer corner.
    // chamfer length = chamferFrac × fin height, clamped to fit fin_out_x.
    const float finHeight = p.finBotY - p.finTopY;
    const float rawChamfer = p.finChamferFrac * finHeight;
    const float chamfer    = juce::jmin(rawChamfer, p.finOutX * 0.85f);

    juce::Path path;
    path.startNewSubPath(sc.x(innerTopX),         sc.y(p.finTopY));
    path.lineTo(sc.x(farX),                       sc.y(p.finTopY));            // outer top
    path.lineTo(sc.x(farX),                       sc.y(p.finBotY - chamfer));  // outer down to chamfer start
    path.lineTo(sc.x(farX - side * chamfer),      sc.y(p.finBotY));            // chamfered diagonal
    path.lineTo(sc.x(innerBotX),                  sc.y(p.finBotY));            // inner-bottom
    path.closeSubPath();
    return path;
}

juce::Rectangle<float> bandRect(juce::Rectangle<float> bounds, const Params& p)
{
    const Scaler sc = Scaler::make(bounds);
    const float c = kRefW * 0.5f;
    const float bandMidY = (p.bandTopY + p.bandBotY) * 0.5f;
    const float bandW    = bodyWidthAtRefY(bandMidY, p) - 2.0f * p.bandInsetX;

    return { sc.x(c - bandW * 0.5f),
             sc.y(p.bandTopY),
             bandW * sc.sx,
             (p.bandBotY - p.bandTopY) * sc.sy };
}

float redRegionTopYInBounds(juce::Rectangle<float> bounds, const Params& p)
{
    return Scaler::make(bounds).y(p.redRegionTopY);
}

} // namespace bombo::BombShape
