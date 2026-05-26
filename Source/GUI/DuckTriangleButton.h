#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>

#include "../ParameterIds.h"
#include "Colours.h"

namespace bombo
{

// Reverse-bass duck routing pill, shaped like a right-triangle wedge that
// hugs the bomb's body edge. Cycles pid::duckRouting on click:
//   Off → A → B → AB → Off
// Off paints faint outline + low-alpha 'D' so the affordance is still
// readable when the feature is disabled. A/B/AB paint the triangle filled
// with the theme accent, with the letter rotated to ride the hypotenuse
// (tl→apex diagonal) so it reads down the angled edge of the wedge.
class DuckTriangleButton : public juce::Button
{
public:
    DuckTriangleButton(juce::AudioProcessorValueTreeState& apvts)
        : juce::Button({}),
          apvts_(apvts),
          choice_(dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(pid::duckRouting))),
          attachment_(*apvts.getParameter(pid::duckRouting),
                      [this](float) { repaint(); })
    {
        setClickingTogglesState(false);
        setWantsKeyboardFocus(false);
        setMouseClickGrabsKeyboardFocus(false);
        setTooltip("Reverse-bass duck routing. Off: chain-tail duck only. "
                   "A: also duck Voice A sub keyed by Voice B body. "
                   "B: also duck Voice B body keyed by Voice A sub. "
                   "AB: both per-voice ducks active. Click to cycle.");
        attachment_.sendInitialUpdate();
    }

    // Explicit triangle in LOCAL coords. tl = top-left (on body edge),
    // tr = top-right (flat top), apex = bottom point (on body edge, lower).
    // tl -> apex is the angled hypotenuse the letter rides along.
    void setTriangle(juce::Point<float> tl, juce::Point<float> tr, juce::Point<float> apex)
    {
        tl_ = tl; tr_ = tr; apex_ = apex; haveTri_ = true;
        repaint();
    }

    void clicked() override
    {
        // Cycle Off(0) → A(1) → B(2) → AB(3) → Off(0).
        // Mirrors DecRoutingPill's direct-gesture flow (works reliably);
        // setValueAsCompleteGesture via ParameterAttachment did not propagate.
        auto* p = apvts_.getParameter(pid::duckRouting);
        if (p == nullptr) return;
        const int cur  = juce::jlimit(0, 3, (int) std::round(p->convertFrom0to1(p->getValue())));
        const int next = (cur + 1) % 4;
        const float norm = p->convertTo0to1((float) next);
        p->beginChangeGesture();
        p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, norm));
        p->endChangeGesture();
    }

    void paintButton(juce::Graphics& g, bool, bool) override
    {
        const auto r   = getLocalBounds().toFloat();
        const bool hot = isMouseOverOrDragging();
        const int   idx   = choice_ != nullptr ? choice_->getIndex() : 0;
        const bool  isOff = (idx == 0);

        const juce::Point<float> tl   = haveTri_ ? tl_   : r.getTopLeft();
        const juce::Point<float> tr   = haveTri_ ? tr_   : juce::Point<float>(r.getRight(), r.getY());
        const juce::Point<float> apex = haveTri_ ? apex_ : juce::Point<float>(r.getRight(), r.getBottom());

        juce::Path tri;
        tri.addTriangle(tl, tr, apex);
        tri = tri.createPathWithRoundedCorners(3.5f);

        const auto accent = col::isNeon() ? col::accentAmber() : col::duck();
        const auto stroke = accent;
        const auto fill   = isOff ? juce::Colour() : accent.withAlpha(hot ? 1.0f : 0.92f);

        if (! isOff)
        {
            g.setColour(fill);
            g.fillPath(tri);
        }
        g.setColour(stroke.withAlpha(isOff ? (hot ? 0.85f : 0.55f) : 1.0f));
        g.strokePath(tri, juce::PathStrokeType(1.4f));

        // Off shows the full word "DUCK" (the resting state); when actively
        // routed it shows which voice is ducked: "A"/"B"/"AB". Text rides the
        // tl→apex diagonal hypotenuse.
        const juce::String label = isOff ? juce::String("DUCK")
                                 : idx == 1 ? juce::String("A")
                                 : idx == 2 ? juce::String("B")
                                 :            juce::String("AB");

        // Anchor at midpoint of tl↔apex, nudged slightly toward tr so the
        // text sits INSIDE the triangle rather than on the edge.
        const juce::Point<float> mid = (tl + apex) * 0.5f;
        const juce::Point<float> nudge = (tr - mid) * 0.20f;
        const juce::Point<float> anchor = mid + nudge;

        const float angle = std::atan2(apex.y - tl.y, apex.x - tl.x);
        const float side  = std::min(r.getWidth(), r.getHeight());
        float fontH = juce::jmax(8.0f, side * 0.32f);

        // Shrink to fit the hypotenuse so the longer labels ("DUCK", "AB")
        // don't overflow the wedge while a single glyph still fills it.
        juce::Font font(fontH, juce::Font::bold);
        const float hypLen  = tl.getDistanceFrom(apex);
        const float textW   = font.getStringWidthFloat(label);
        if (textW > hypLen * 0.82f && textW > 0.0f)
        {
            fontH *= (hypLen * 0.82f) / textW;
            fontH  = juce::jmax(7.0f, fontH);
            font.setHeight(fontH);
        }

        g.saveState();
        g.addTransform(juce::AffineTransform::rotation(angle, anchor.x, anchor.y));
        g.setFont(font);
        // Contrast-aware text in BOTH states. When routed (A/B/AB) the wedge is
        // filled, so contrast against the fill. When Off (outline-only) the
        // wedge sits on the chassis BODY just below VOICE A, so contrast against
        // the body colour: dark text on light bodies (VAULT olive), light text
        // on the dark/neon bodies.
        const auto textColor = isOff
            ? (col::bodyHi().getPerceivedBrightness() > 0.45f ? col::ink() : col::bone())
            : (fill.getBrightness() > 0.55f ? juce::Colours::black
                                            : juce::Colours::white);
        g.setColour(textColor);
        const juce::Rectangle<float> textBox(anchor.x - 40.0f, anchor.y - fontH * 0.5f,
                                             80.0f, fontH);
        g.drawText(label, textBox, juce::Justification::centred, false);
        g.restoreState();
    }

private:
    juce::AudioProcessorValueTreeState& apvts_;
    juce::AudioParameterChoice* choice_ = nullptr;
    juce::ParameterAttachment   attachment_;
    juce::Point<float>          tl_, tr_, apex_;
    bool                        haveTri_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DuckTriangleButton)
};

} // namespace bombo
