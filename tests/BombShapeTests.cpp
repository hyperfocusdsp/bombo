// tests/BombShapeTests.cpp — geometry invariants for the parametric
// Mini-Nuke silhouette generator.
//
// Phase 2 safety net: locks the R4B-CLASSIC silhouette's geometric
// contract so future tweaks to BombShape.h can't silently shift
// proportions away from the brainstorm-locked design.

#include "GUI/BombShape.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace
{

class BombShapePathBoundsTest : public juce::UnitTest
{
public:
    BombShapePathBoundsTest()
        : juce::UnitTest("BombShape: path bounds approximate input rect") {}

    void runTest() override
    {
        beginTest("path bounds match input bounds at multiple sizes");
        for (auto size : { 200, 360, 540, 800 })
        {
            const juce::Rectangle<float> bounds(0.0f, 0.0f,
                                                static_cast<float>(size),
                                                static_cast<float>(size) * 16.0f / 9.0f);
            const auto path  = bombo::BombShape::buildBombPath(bounds);
            const auto pb    = path.getBounds();
            // Tolerance: 6 px because the silhouette is inscribed inside
            // the bounds — left/right edges are at body_bulge_w/2, not at
            // bounds.getRight(). Just assert it's well within bounds.
            expect(pb.getX() >= bounds.getX() - 1.0f, "left within bounds");
            expect(pb.getRight() <= bounds.getRight() + 1.0f, "right within bounds");
            expect(pb.getY() >= bounds.getY() - 1.0f, "top within bounds");
            expect(pb.getBottom() <= bounds.getBottom() + 1.0f, "bottom within bounds");
            // Path must have non-zero area
            expect(pb.getWidth() > size * 0.5f,  "path width > 50% of bounds width");
            expect(pb.getHeight() > size * 0.5f, "path height > 50% of bounds height");
        }
    }
};

class BombShapeContainsTest : public juce::UnitTest
{
public:
    BombShapeContainsTest()
        : juce::UnitTest("BombShape: containment at known interior + exterior points") {}

    void runTest() override
    {
        beginTest("center of body bulge is inside path; corner of bounds is outside");
        const juce::Rectangle<float> bounds(0.0f, 0.0f, 360.0f, 640.0f);
        const auto path = bombo::BombShape::buildBombPath(bounds);

        const bombo::BombShape::Params p;
        const float bulgeY = p.bodyTopY + (p.bodyBotY - p.bodyTopY) * p.bodyBulgeYFrac;
        const float cx     = bombo::BombShape::kRefW * 0.5f;

        // Center of body bulge — must be inside path
        expect(path.contains(cx, bulgeY), "body bulge center is inside silhouette");
        // Corners of bounds — must be outside path (silhouette is inscribed)
        expect(! path.contains(2.0f, 2.0f),                 "top-left corner outside");
        expect(! path.contains(358.0f, 2.0f),               "top-right corner outside");
        expect(! path.contains(2.0f, 638.0f),               "bottom-left corner outside");
        expect(! path.contains(358.0f, 638.0f),             "bottom-right corner outside");
        // Tip area — center-bottom must be inside (or on edge) at refY just above tipY
        expect(path.contains(cx, p.tipY - 8.0f), "just above tip is inside");
    }
};

class BombShapeWidthInterpolationTest : public juce::UnitTest
{
public:
    BombShapeWidthInterpolationTest()
        : juce::UnitTest("BombShape: bodyWidthAtRefY interpolation") {}

    void runTest() override
    {
        const bombo::BombShape::Params p;

        beginTest("widths at anchor y's match parameter values exactly");
        expect(std::abs(bombo::BombShape::bodyWidthAtRefY(p.bodyTopY, p) - p.bodyTopW) < 0.001f,
               "width at bodyTopY == bodyTopW");
        expect(std::abs(bombo::BombShape::bodyWidthAtRefY(p.bodyBotY, p) - p.bodyBotW) < 0.001f,
               "width at bodyBotY == bodyBotW");

        const float bulgeY = p.bodyTopY + (p.bodyBotY - p.bodyTopY) * p.bodyBulgeYFrac;
        expect(std::abs(bombo::BombShape::bodyWidthAtRefY(bulgeY, p) - p.bodyBulgeW) < 0.001f,
               "width at bulge y == bodyBulgeW");

        beginTest("width grows monotonically from top to bulge");
        float prev = p.bodyTopW;
        for (float y = p.bodyTopY; y < bulgeY; y += 4.0f)
        {
            const float w = bombo::BombShape::bodyWidthAtRefY(y, p);
            expect(w >= prev - 0.001f, "non-decreasing on the way to bulge");
            prev = w;
        }

        beginTest("width shrinks monotonically from bulge to bot");
        prev = p.bodyBulgeW;
        for (float y = bulgeY; y < p.bodyBotY; y += 4.0f)
        {
            const float w = bombo::BombShape::bodyWidthAtRefY(y, p);
            expect(w <= prev + 0.001f, "non-increasing past bulge");
            prev = w;
        }

        beginTest("clamps outside body range");
        expect(bombo::BombShape::bodyWidthAtRefY(-10.0f, p) == p.bodyTopW,
               "y above bodyTopY clamps to bodyTopW");
        expect(bombo::BombShape::bodyWidthAtRefY(1000.0f, p) == p.bodyBotW,
               "y below bodyBotY clamps to bodyBotW");
    }
};

class BombShapeFinTest : public juce::UnitTest
{
public:
    BombShapeFinTest()
        : juce::UnitTest("BombShape: fin paths fit inside expected extent") {}

    void runTest() override
    {
        const juce::Rectangle<float> bounds(0.0f, 0.0f, 360.0f, 640.0f);
        const bombo::BombShape::Params p;

        beginTest("right fin extends correctly outside body");
        const auto right = bombo::BombShape::buildFinPath(bounds, +1, p);
        const auto rb = right.getBounds();
        const float maxRefX = bombo::BombShape::kRefW * 0.5f + p.bodyBulgeW * 0.5f + p.finOutX;
        expect(rb.getRight() <= maxRefX + 0.5f,
               "right-fin extent matches body_bulge_w/2 + fin_out_x");
        // Inner edge of the right fin sits at the body's right shoulder
        // (right of center), within the body's bulge half-width — i.e.
        // the fin attaches to the body, not floating in space.
        const float cx = bombo::BombShape::kRefW * 0.5f;
        const float bodyHalfBulge = p.bodyBulgeW * 0.5f;
        expect(rb.getX() > cx,
               "right-fin inner edge is right of center");
        expect(rb.getX() < cx + bodyHalfBulge,
               "right-fin inner edge is within the body's bulge half-width");
        expect(rb.getY() >= p.finTopY - 0.5f, "fin top y matches");
        expect(rb.getBottom() <= p.finBotY + 0.5f, "fin bot y matches");

        beginTest("left fin is mirror of right");
        const auto left = bombo::BombShape::buildFinPath(bounds, -1, p);
        const auto lb = left.getBounds();
        const float minRefX = bombo::BombShape::kRefW * 0.5f - p.bodyBulgeW * 0.5f - p.finOutX;
        expect(lb.getX() >= minRefX - 0.5f,
               "left-fin extent mirrors right");
    }
};

class BombShapeBandTest : public juce::UnitTest
{
public:
    BombShapeBandTest()
        : juce::UnitTest("BombShape: band rect sits inside body width") {}

    void runTest() override
    {
        const juce::Rectangle<float> bounds(0.0f, 0.0f, 360.0f, 640.0f);
        const bombo::BombShape::Params p;
        const auto band = bombo::BombShape::bandRect(bounds, p);

        beginTest("band is inside body silhouette at its y range");
        const float bandMidY = (p.bandTopY + p.bandBotY) * 0.5f;
        const float bodyW    = bombo::BombShape::bodyWidthAtRefY(bandMidY, p);
        expect(band.getWidth() <= bodyW,
               "band width never exceeds body width at band y");
        expect(band.getWidth() > 0.0f, "band width is positive");
        expect(band.getHeight() > 0.0f, "band height is positive");
    }
};

class BombShapeDeterminismTest : public juce::UnitTest
{
public:
    BombShapeDeterminismTest()
        : juce::UnitTest("BombShape: same params → same path (pure function)") {}

    void runTest() override
    {
        beginTest("two calls with identical params produce identical bounds");
        const juce::Rectangle<float> bounds(0.0f, 0.0f, 540.0f, 960.0f);
        const auto p1 = bombo::BombShape::buildBombPath(bounds);
        const auto p2 = bombo::BombShape::buildBombPath(bounds);
        expect(p1.getBounds() == p2.getBounds(),
               "pure function — same input, same path bounds");
    }
};

static BombShapePathBoundsTest          bombShapePathBoundsTest;
static BombShapeContainsTest            bombShapeContainsTest;
static BombShapeWidthInterpolationTest  bombShapeWidthInterpolationTest;
static BombShapeFinTest                 bombShapeFinTest;
static BombShapeBandTest                bombShapeBandTest;
static BombShapeDeterminismTest         bombShapeDeterminismTest;

} // anonymous namespace
