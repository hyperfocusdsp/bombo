#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"
#include "Fonts.h"
#include "Theme/ThemeProvider.h"

namespace bombo
{

// Bombo knob. Section colour lives in the column body — the knob cap is
// dark plastic with a bone indicator + bone value text. Hosts that want a
// hero tint (e.g. the OUT macro) override `rotarySliderOutlineColourId`
// to amber; everyone else uses the default `col::knobCap`.
//
// Two-line value layout (number above, unit below) is preserved from the
// pre-port UI: `slider.getTextFromValue` is split on the first space so
// the param's `stringFromValueFunction` does the formatting.
class BomboLookAndFeel : public juce::LookAndFeel_V4, public juce::ChangeListener
{
public:
    BomboLookAndFeel()
    {
        setColour(juce::Slider::backgroundColourId,        col::graphite());
        setColour(juce::Slider::rotarySliderFillColourId,  col::bone());
        // Default cap = dark plastic. Per-knob override is supported (the
        // OUT macro sets this to col::accentAmber).
        setColour(juce::Slider::rotarySliderOutlineColourId, col::knobCap());
        setColour(juce::Slider::thumbColourId,             col::bone());
        setColour(juce::Slider::textBoxOutlineColourId,    juce::Colour(0));
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0));
        setColour(juce::Slider::textBoxTextColourId,       col::boneDim());
        setColour(juce::Label::textColourId,               col::bone());
        setColour(juce::ComboBox::backgroundColourId,      col::ink());
        setColour(juce::ComboBox::textColourId,            col::bone());
        setColour(juce::ComboBox::outlineColourId,         col::graphiteHi());
        setColour(juce::ComboBox::arrowColourId,           col::boneDim());
        setColour(juce::PopupMenu::backgroundColourId,     col::graphiteHi());
        setColour(juce::PopupMenu::textColourId,           col::bone());
        setColour(juce::PopupMenu::highlightedBackgroundColourId, col::accentAmber());
        setColour(juce::PopupMenu::highlightedTextColourId, col::ink());
        setColour(juce::ToggleButton::textColourId,        col::bone());
        setColour(juce::ToggleButton::tickColourId,        col::accentAmber());
        setColour(juce::ToggleButton::tickDisabledColourId, col::boneDim());

        bombo::ThemeProvider::get().addChangeListener(this);
    }

    ~BomboLookAndFeel() override
    {
        bombo::ThemeProvider::get().removeChangeListener(this);
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override
    {
        // Hook for future themed-resource invalidation (tinted images, cached
        // gradients). Per-component repaint is handled by ThemedComponent.
    }

    // ── Pill-style buttons ────────────────────────────────────────────
    // All TextButtons and ToggleButtons use a uniform dark-graphite pill
    // with an amber accent for the active/pressed state. Matches the fin
    // control look (dark on orange background = good contrast).
    static constexpr float kPillCorner = 4.0f;

    // Shared pill chrome. The LIM/TAIL toggle pills and any action TextButton
    // tagged with the "finPill" property (e.g. BNC) render identically — same
    // fill, amber border, font, and neon-theme handling — so the fin row reads
    // as one consistent set. Plain TextButtons (preset-bar arrows) are NOT
    // tagged and keep the default action-button chrome below.
    static bool isFinPill(const juce::Button& btn)
    {
        return static_cast<bool>(btn.getProperties().getWithDefault("finPill", false));
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
                              const juce::Colour& /*backgroundColour*/,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override
    {
        const auto r     = btn.getLocalBounds().toFloat();
        const bool on    = shouldDrawButtonAsDown || btn.getToggleState();
        const bool hover = shouldDrawButtonAsHighlighted;
        if (isFinPill(btn))
        {
            bombo::pill::paintBackground(g, r, on, hover);
            return;
        }
        // Default action-button chrome: dark graphite fill, bone border. Off:
        // dark fill so bone text is legible on the orange fin. On: amber fill.
        const float hoverFill   = hover ? 0.10f : 0.0f;
        const float hoverBorder = hover ? 0.20f : 0.0f;
        g.setColour(on ? col::accentAmber().withAlpha(0.40f + hoverFill)
                       : col::graphite().withAlpha(0.88f));
        g.fillRoundedRectangle(r, kPillCorner);
        g.setColour(on ? col::accentAmber()
                       : col::boneDim().withAlpha(0.45f + hoverBorder));
        g.drawRoundedRectangle(r.reduced(0.5f), kPillCorner, 1.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& btn,
                        bool /*shouldDrawButtonAsHighlighted*/,
                        bool shouldDrawButtonAsDown) override
    {
        const bool on = shouldDrawButtonAsDown || btn.getToggleState();
        // Fin pills follow the shared neon-aware pill foreground; plain action
        // buttons use dark-on-amber / light-on-graphite.
        g.setColour(isFinPill(btn) ? bombo::pill::fg(on)
                                   : (on ? col::ink() : col::bone()));
        g.setFont(fonts::value(16.0f));
        g.drawText(btn.getButtonText(), btn.getLocalBounds(),
                   juce::Justification::centred, false);
    }

    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& btn,
                          bool shouldDrawButtonAsHighlighted,
                          bool /*shouldDrawButtonAsDown*/) override
    {
        const auto r     = btn.getLocalBounds().toFloat();
        const bool on    = btn.getToggleState();
        const bool hover = shouldDrawButtonAsHighlighted;
        bombo::pill::paintBackground(g, r, on, hover);
        g.setColour(bombo::pill::fg(on));
        g.setFont(fonts::value(16.0f));
        g.drawText(btn.getButtonText(), r, juce::Justification::centred, false);
    }

    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle, float rotaryEndAngle,
                          juce::Slider& slider) override
    {
        const auto bounds = juce::Rectangle<float>(static_cast<float>(x),
                                                   static_cast<float>(y),
                                                   static_cast<float>(width),
                                                   static_cast<float>(height));
        const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
        const auto cx = bounds.getCentreX();
        const auto cy = bounds.getCentreY();
        const float radius = diameter * 0.5f - 4.0f;
        if (radius < 6.0f) return;

        // Cap colour: dark by default, amber for the OUT macro — EXCEPT on
        // FALLOUT, where the OUT macro drops the amber/logo hero face and renders
        // as a normal dark-graphite knob (graphite core + bevel ring + bone
        // indicator). The red nose already carries the weight; the amber bullseye
        // clashed and let the red show through.
        const bool logoKnobProp = static_cast<bool>(
            slider.getProperties().getWithDefault("logoKnob", false));
        const bool falloutTheme = (col::chassisArt() == "fallout");
        auto core = slider.findColour(juce::Slider::rotarySliderOutlineColourId);
        if (falloutTheme && logoKnobProp)
            core = col::knobCap();               // dark graphite OUT on FALLOUT
        const bool capIsDark = core.getPerceivedBrightness() < 0.5f;
        const auto indicatorColour = capIsDark ? col::bone() : col::ink();
        const auto valueColour     = capIsDark ? col::bone() : col::ink();

        // 1. Mounting recess — SOFT drop shadow. A radial gradient that is dark
        //    at the knob rim and fades to transparent a few px beyond, offset
        //    slightly downward so it reads as a soft cast shadow instead of the
        //    old hard black ring. The knob body (rubber, or the logo rings) is
        //    drawn on top, covering the dark centre.
        {
            const float shOuter = radius + 7.0f;
            const float shCy    = cy + 3.0f;   // cast downward
            juce::ColourGradient halo(
                juce::Colour(0xFF000000).withAlpha(0.72f), cx, shCy,
                juce::Colours::transparentBlack,           cx, shCy - shOuter, true);
            halo.addColour(juce::jlimit(0.05, 0.90, static_cast<double>(radius / shOuter)),
                           juce::Colour(0xFF000000).withAlpha(0.72f));
            g.setGradientFill(halo);
            g.fillEllipse(cx - shOuter, shCy - shOuter, shOuter * 2.0f, shOuter * 2.0f);
        }

        // Cap geometry — needed by the indicator + readout regardless of face style.
        const float coreOuterR = radius * 0.80f;
        const float coreR      = coreOuterR - 1.0f;
        const bool  isHover    = slider.isMouseOverOrDragging();
        // Hero OUT knob opts into the Hyperfocus logo face via this property
        // (set in FaceplatePanel where macro_[6] is built).
        // Logo (Hyperfocus mark) face only shows OFF FALLOUT; on FALLOUT the OUT
        // macro renders via the normal dark-graphite knob path above.
        const bool  logoKnob       = logoKnobProp && ! falloutTheme;
        const auto  logoRingColour = col::accentAmber();

        if (logoKnob)
        {
            // The Hyperfocus "focus ring" mark as the cap face: concentric
            // aperture rings in the active THEME ACCENT, so the hero knob
            // recolours per theme (amber on classic, neon hue on
            // matrix/cyber/plasma). It sits on the dark recess from step 1; the
            // gaps between rings show that dark backing. Normalised ring radii
            // are lifted from the logo master (mark-only.svg). This is tier-3
            // functional UI — theming the mark here does NOT touch the
            // monochrome brand-lockup rule.
            const float mR   = radius * 0.92f;
            const auto  ring = col::accentAmber().withAlpha(isHover ? 1.0f : 0.92f);
            auto annulus = [&](float rOut, float rIn)
            {
                juce::Path p;
                p.addEllipse(cx - mR * rOut, cy - mR * rOut, mR * rOut * 2.0f, mR * rOut * 2.0f);
                p.addEllipse(cx - mR * rIn,  cy - mR * rIn,  mR * rIn  * 2.0f, mR * rIn  * 2.0f);
                p.setUsingNonZeroWinding(false);   // even-odd → a ring with a hole
                g.setColour(ring);
                g.fillPath(p);
            };
            annulus(1.000f, 0.727f);   // outer ring
            annulus(0.566f, 0.417f);   // mid ring
            g.setColour(ring);
            g.fillEllipse(cx - mR * 0.228f, cy - mR * 0.228f,
                          mR * 0.228f * 2.0f, mR * 0.228f * 2.0f);  // centre disc
        }
        else
        {
            // 2. Rubber grip — dark with subtle top highlight.
            g.setColour(col::knobRubber());
            g.fillEllipse(cx - radius, cy - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(juce::Colour(0xFF24'24'26u));
            const float hlR = radius * 0.95f;
            g.fillEllipse(cx - hlR, cy - radius - radius * 0.05f, hlR * 2.0f, hlR * 2.0f);
            g.setColour(col::knobRubber());
            g.fillEllipse(cx - radius * 0.88f, cy - radius * 0.88f,
                          radius * 0.88f * 2.0f, radius * 0.88f * 2.0f);

            // 3. Metal bevel ring between rubber and the cap core.
            g.setColour(col::knobBevel());
            g.fillEllipse(cx - coreOuterR - 1.5f, cy - coreOuterR - 1.5f,
                          (coreOuterR + 1.5f) * 2.0f, (coreOuterR + 1.5f) * 2.0f);

            // 4. Cap core. Subtle top-down gradient sells the moulded-plastic feel.
            // Hover brightens the cap so the user gets tactile feedback pre-click.
            const float hoverBoost = isHover ? 0.08f : 0.0f;
            const auto coreTop = core.brighter(0.10f + hoverBoost);
            const auto coreBot = core.darker (0.18f - hoverBoost * 0.5f);
            g.setGradientFill(juce::ColourGradient(coreTop, cx, cy - coreR,
                                                   coreBot, cx, cy + coreR, false));
            g.fillEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f);

            // 5. Inner shadow at cap edge for recessed look.
            g.setColour(juce::Colour::fromRGBA(0, 0, 0, 0x55));
            g.drawEllipse(cx - coreR, cy - coreR, coreR * 2.0f, coreR * 2.0f, 1.0f);
        }

        // 6. Indicator stem — wedge from cap edge out to the rubber outer.
        const float a = juce::jmap(sliderPos, rotaryStartAngle, rotaryEndAngle);
        const float ang = a - juce::MathConstants<float>::halfPi;
        const float ic = std::cos(ang);
        const float is = std::sin(ang);
        const bool  logoKnobForStem = static_cast<bool>(
            slider.getProperties().getWithDefault("logoKnob", false));
        const float stemInR  = coreR;
        const float stemOutR = radius;
        // Wider stem on the logo knob so a dark fill stays visible: a thin
        // keyline outline on a 2.6 px stem covers most of the fill and reads as
        // the outline colour (white). 4.2 px keeps the fill dominant.
        const float stemW    = logoKnobForStem ? 4.2f : 2.6f;
        juce::Path stem;
        const float perpC = -is;
        const float perpS =  ic;
        stem.startNewSubPath(cx + ic * stemInR  + perpC * stemW * 0.5f,
                             cy + is * stemInR  + perpS * stemW * 0.5f);
        stem.lineTo(cx + ic * stemOutR + perpC * stemW * 0.6f,
                    cy + is * stemOutR + perpS * stemW * 0.6f);
        stem.lineTo(cx + ic * stemOutR - perpC * stemW * 0.6f,
                    cy + is * stemOutR - perpS * stemW * 0.6f);
        stem.lineTo(cx + ic * stemInR  - perpC * stemW * 0.5f,
                    cy + is * stemInR  - perpS * stemW * 0.5f);
        stem.closeSubPath();
        if (logoKnob)
        {
            // Contrast-aware pointer. The cap face is the theme accent (magenta,
            // amber, neon hue…), so a fixed bone pointer vanishes on bright
            // accents. Pick a dark-grey stem over a bright ring (e.g. magenta)
            // and a bone stem over a dark ring; the opposite-tone outline keeps
            // it legible over the dark gaps between rings too.
            const bool  ringIsBright = logoRingColour.getPerceivedBrightness() > 0.5f;
            const auto  stemFill     = ringIsBright ? juce::Colour(0xFF2A'2A'2Eu) // dark grey
                                                    : col::bone();
            const auto  stemOutline  = ringIsBright ? col::bone().withAlpha(0.85f)
                                                    : col::ink().withAlpha(0.85f);
            g.setColour(stemFill);
            g.fillPath(stem);
            g.setColour(stemOutline);
            g.strokePath(stem, juce::PathStrokeType(0.7f));
        }
        else
        {
            g.setColour(indicatorColour);
            g.fillPath(stem);
        }

        // 7. Tick markers. Discrete-choice knobs (addChoice in FaceplatePanel)
        //    stash a "numChoices" int in slider.properties so we draw exactly
        //    N bright ticks aligned to each snap position — gives the dial a
        //    "click-through" feel as the user rotates. Continuous knobs keep
        //    the original 11 dim dots.
        const int numChoices = static_cast<int>(
            slider.getProperties().getWithDefault("numChoices", -1));
        if (numChoices > 1)
        {
            const float tickInR  = radius + 1.5f;
            const float tickOutR = tickInR + juce::jmax(3.0f, radius * 0.18f);
            const int activeIdx  = juce::roundToInt(sliderPos * (numChoices - 1));
            for (int i = 0; i < numChoices; ++i)
            {
                const float t = static_cast<float>(i)
                              / static_cast<float>(numChoices - 1);
                const float ta = juce::jmap(t, rotaryStartAngle, rotaryEndAngle)
                               - juce::MathConstants<float>::halfPi;
                const float cc = std::cos(ta);
                const float ss = std::sin(ta);
                g.setColour(i == activeIdx ? col::bone()
                                            : col::bone().withAlpha(0.45f));
                g.drawLine(cx + cc * tickInR,  cy + ss * tickInR,
                           cx + cc * tickOutR, cy + ss * tickOutR,
                           i == activeIdx ? 2.0f : 1.4f);
            }
        }
        else
        {
            const float dotR = juce::jmax(0.9f, radius * 0.045f);
            const float dotRingR = radius + 2.5f;
            for (int i = 0; i <= 10; ++i)
            {
                const float t = static_cast<float>(i) / 10.0f;
                const float ta = juce::jmap(t, rotaryStartAngle, rotaryEndAngle)
                               - juce::MathConstants<float>::halfPi;
                const float dx = cx + std::cos(ta) * dotRingR;
                const float dy = cy + std::sin(ta) * dotRingR;
                g.setColour(col::bone().withAlpha(0.60f));
                g.fillEllipse(dx - dotR, dy - dotR, dotR * 2.0f, dotR * 2.0f);
            }
        }

        // 8. Value / choice text inside the cap. For discrete-choice knobs we
        //    look up the choice name from the "choiceNames" var-array stamped
        //    by addChoice; for continuous knobs we use the param's formatter.
        //    Two-line layout (number above, unit below) kicks in only when the
        //    formatter returns "<number> <unit>" — choice names ignore it.
        // The logo-mark hero knob shows no numeric readout — the focus-ring mark
        // is its identity and the indicator shows the level. Keeps it clean.
        if (logoKnob)
            return;

        juce::String text;
        bool isChoiceText = false;
        if (numChoices > 1)
        {
            if (const auto* choicesVar = slider.getProperties()
                                              .getVarPointer("choiceNames"))
            {
                if (auto* arr = choicesVar->getArray())
                {
                    const int idx = juce::jlimit(0, arr->size() - 1,
                                                  juce::roundToInt(slider.getValue()));
                    text = (*arr)[idx].toString();
                    isChoiceText = true;
                }
            }
        }
        if (! isChoiceText)
            text = slider.getTextFromValue(slider.getValue());
        const float capInner = coreR * 0.95f;

        // The two-line "<number> / <unit>" layout looked clever at first
        // but at 38–54 px knob sizes it gave un-legible 7 pt unit text
        // that read as random glyphs ('dB' looked like '0', etc.). User
        // 2026-05-17: "make them legible and not clashing ffs". So we
        // now always render the FULL value string single-line. Choice
        // knobs render their string label as-is.
        //
        // Day 2 polish (KVRDC 2026 screenshot legibility): bumped the size
        // ratio + min so the readout stays legible at gallery-thumbnail
        // resolution, and added a subtle inset drop-shadow so the text
        // separates from any cap colour (mid-tone caps like duck/drive
        // lost the bone/ink contrast at small sizes).
        const float valueFontSize = juce::jlimit(10.0f, 16.0f, capInner * 0.60f);
        g.setFont(fonts::value(valueFontSize).boldened());
        const auto textRect = juce::Rectangle<float>(cx - capInner, cy - capInner,
                                                     capInner * 2.0f, capInner * 2.0f);
        // Drop shadow — black at low alpha, offset 1px down. Adds definition
        // without darkening the cap centre when the readout is short.
        g.setColour(juce::Colour::fromRGBA(0, 0, 0, 90));
        g.drawText(text, textRect.translated(0.0f, 1.0f),
                   juce::Justification::centred);
        // Primary readout.
        g.setColour(valueColour.withAlpha(0.97f));
        g.drawText(text, textRect, juce::Justification::centred);
    }

    void drawLabel(juce::Graphics& g, juce::Label& label) override
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));
        if (!label.isBeingEdited())
        {
            g.setColour(label.findColour(juce::Label::textColourId));
            g.setFont(label.getFont());
            g.drawFittedText(label.getText(), label.getLocalBounds(),
                             label.getJustificationType(), 1, 0.9f);
        }
    }

    juce::Font getLabelFont(juce::Label& l) override { return l.getFont(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboLookAndFeel)
};

} // namespace bombo
