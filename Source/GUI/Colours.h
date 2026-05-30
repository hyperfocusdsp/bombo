#pragma once

#include "Theme/ThemeProvider.h"

#include <juce_graphics/juce_graphics.h>

// Theme-aware colour accessors. Every value reads from ThemeProvider,
// so swapping themes at runtime swaps every paint on the next repaint.
//
// CALL CONVENTION: every call site is `col::graphite()`, NOT `col::graphite`.
// The trailing parens are mandatory — these are functions, not constants.
namespace bombo::col
{

inline juce::Colour graphite()    { return bombo::ThemeProvider::current().graphite; }
inline juce::Colour graphiteHi()  { return bombo::ThemeProvider::current().graphiteHi; }
inline juce::Colour ink()         { return bombo::ThemeProvider::current().ink; }
inline juce::Colour bone()        { return bombo::ThemeProvider::current().bone; }
inline juce::Colour boneDim()     { return bombo::ThemeProvider::current().boneDim; }

inline juce::Colour voice()       { return bombo::ThemeProvider::current().voice; }
inline juce::Colour drive()       { return bombo::ThemeProvider::current().drive; }
inline juce::Colour delayC()      { return bombo::ThemeProvider::current().delayC; }
inline juce::Colour reverb()      { return bombo::ThemeProvider::current().reverb; }
inline juce::Colour filterC()     { return bombo::ThemeProvider::current().filterC; }
inline juce::Colour duck()        { return bombo::ThemeProvider::current().duck; }

inline juce::Colour knobCap()     { return bombo::ThemeProvider::current().knobCap; }
inline juce::Colour knobBevel()   { return bombo::ThemeProvider::current().knobBevel; }
inline juce::Colour knobRubber()  { return bombo::ThemeProvider::current().knobRubber; }

inline juce::Colour accentAmber() { return bombo::ThemeProvider::current().accentAmber; }

// Mini-Nuke chassis surfaces (Phase 2e).
inline juce::Colour bodyHi()      { return bombo::ThemeProvider::current().bodyHi; }
inline juce::Colour bodyLo()      { return bombo::ThemeProvider::current().bodyLo; }
inline juce::Colour cap()         { return bombo::ThemeProvider::current().cap; }
inline juce::Colour noseRed()     { return bombo::ThemeProvider::current().noseRed; }
inline juce::Colour bandYellow()  { return bombo::ThemeProvider::current().bandYellow; }

inline float chassisOverlayOpacity() { return bombo::ThemeProvider::current().chassisOverlayOpacity; }

inline bombo::BodyStyle bodyStyle() { return bombo::ThemeProvider::current().bodyStyle; }

inline juce::String chassisArt() { return bombo::ThemeProvider::current().chassisArt; }

// True on themes where `ink` is a LIGHT secondary foreground instead of
// a dark recess colour (MATRIX/CYBER/PLASMA). These themes need
// dark-bg + accent-text styling for toggle pills, the OUT macro cap,
// the scope panel, and nose macro labels — otherwise the saturated neon
// `accentAmber`/`bone` end up text-on-text or text-on-glow and become
// unreadable. The ink-brightness check is the cleanest discriminator:
// neon palettes invert ink's semantic from "dark ink" to "light off-bone".
inline bool isNeon() noexcept { return ink().getPerceivedBrightness() > 0.5f; }

} // namespace bombo::col

// ── Fin-pill chrome ─────────────────────────────────────────────────────
// Single source of truth for the LIM/TAIL pill look. EVERY fin control
// (BNC, LIM, TAIL, LOOP, KBTRK, DICE, BPM) draws its background, border, and
// foreground through these so they stay pixel-identical and can't drift.
//   on    = active/toggled (or host-locked) state.
//   hover = mouse-over highlight.
// Neon themes (matrix/cyber/plasma) keep the fill dark in BOTH states and
// signal ON via a brighter/thicker amber border + full-opacity accent fg;
// classic themes flip to a bright amber fill when ON.
namespace bombo::pill
{
inline constexpr float corner = 4.0f;

inline juce::Colour fill(bool on, bool hover)
{
    const float hoverFill = hover ? 0.10f : 0.0f;
    if (col::isNeon())
        return col::graphite().withAlpha(on ? 0.95f : 0.88f + hoverFill * 0.5f);
    return on ? col::accentAmber().withAlpha(0.40f + hoverFill)
              : col::graphite().withAlpha(0.88f);
}

inline juce::Colour border(bool on, bool hover)
{
    const float hoverBorder = hover ? 0.20f : 0.0f;
    if (col::isNeon())
        return on ? col::accentAmber() : col::accentAmber().withAlpha(0.55f + hoverBorder);
    return on ? col::accentAmber() : col::accentAmber().withAlpha(0.50f + hoverBorder);
}

inline float borderWidth(bool on) { return (col::isNeon() && on) ? 1.5f : 1.0f; }

// Foreground (text or icon) colour.
inline juce::Colour fg(bool on)
{
    if (col::chassisArt() == "fallout")
        return on ? col::accentAmber() : col::bone().withAlpha(0.85f);
    if (col::isNeon())
        return col::accentAmber().withAlpha(on ? 1.0f : 0.75f);
    // Classic themes: pick the label colour to CONTRAST the pill's own fill so
    // it stays legible in every palette — dark ink on the light (amber) ON
    // fill, light bone on the dark (graphite) OFF fill. ("dark on light,
    // light on dark.") Keying off fill() brightness means it can never end up
    // dark-on-dark or light-on-light regardless of a theme's graphite/amber.
    return fill(on, false).getPerceivedBrightness() > 0.5f ? col::ink()
                                                           : col::bone();
}

inline void paintBackground(juce::Graphics& g, juce::Rectangle<float> r,
                            bool on, bool hover)
{
    // FALLOUT: the pills sit in the display bezel's dark cutout SLOTS, so they
    // read as recessed. The fill is OPAQUE — these pills must never be
    // translucent: the editor background outside the bomb body is non-opaque, so
    // a see-through pill lets the host/desktop show through under the plugin
    // (the KBTRK/LIM/TAIL/LOOP bleed). Fill with GRAPHITE, the near-black recess
    // colour: NOTE `ink` is repurposed as a LIGHT foreground (#E8D4B4) in this
    // palette, so it must not be used as a fill — the light bone/amber pill text
    // would vanish on it. ON lifts the slot slightly + adds an amber keyline;
    // OFF sits darker. A faint bevel sells the recess, lit from the UPPER-LEFT.
    if (col::chassisArt() == "fallout")
    {
        g.setColour((on ? col::graphite().brighter(0.12f) : col::graphite()).withAlpha(1.0f));
        g.fillRoundedRectangle(r, corner);

        // Directional bevel (light upper-left): a soft black shadow nudged
        // down/right, a faint bone highlight nudged up/left.
        g.setColour(juce::Colours::black.withAlpha(0.25f));
        g.drawRoundedRectangle(r.reduced(0.5f).translated(0.6f, 0.6f), corner, 1.0f);
        g.setColour(col::bone().withAlpha(0.16f));
        g.drawRoundedRectangle(r.reduced(0.5f).translated(-0.6f, -0.6f), corner, 1.0f);

        if (hover)
        {
            g.setColour(col::bone().withAlpha(0.10f));
            g.fillRoundedRectangle(r, corner);
        }
        if (on)
        {
            g.setColour(col::accentAmber().withAlpha(0.55f));
            g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
        }
        return;
    }
    g.setColour(fill(on, hover));
    g.fillRoundedRectangle(r, corner);
    g.setColour(border(on, hover));
    g.drawRoundedRectangle(r.reduced(0.5f), corner, borderWidth(on));
}
} // namespace bombo::pill
