#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Colours.h"

namespace bombo
{

// ── VGA / CRT filter — shared on/off state ──────────────────────────────────
// Single source of truth for the green-phosphor "VGA" filter so the scope AND
// the preset readout flip together when the user clicks the scope's toggle.
// Mirrors the ThemeProvider singleton already used for palette state.
//
// Effective state = manual override if the user has clicked, else the theme
// default (on for FALLOUT's green-phosphor screen, off for the flat/neon
// themes). Derives from ChangeBroadcaster: every CRT-aware display subscribes
// and repaints on toggle, so one click updates them all.
class CrtState : public juce::ChangeBroadcaster
{
public:
    static CrtState& get()
    {
        static CrtState instance;
        return instance;
    }

    bool active() const
    {
        if (manual_) return userState_;
        return col::chassisArt() == "fallout";   // theme default: FALLOUT only
    }

    // Flip the filter and notify every subscriber (scope + preset display).
    void toggle()
    {
        userState_ = ! active();
        manual_    = true;
        sendChangeMessage();
    }

private:
    CrtState() = default;
    bool manual_    = false;   // has the user clicked to override the theme default?
    bool userState_ = false;   // the override value
};

// ── VGA / CRT painting helpers ──────────────────────────────────────────────
// Extracted from ScopeComponent so the scope and the preset readout render the
// identical phosphor screen. The look: dark green-black screen + centre bloom,
// bright phosphor-green foreground, and a baked scanline + vignette overlay.
namespace crt
{
    inline constexpr juce::uint32 kPhosphor = 0xFF7DFF7Au;  // bright phosphor green (foreground)
    inline constexpr juce::uint32 kScreenBg = 0xFF09140Cu;  // dark green-black (screen)
    inline constexpr juce::uint32 kGlow     = 0x2233FF55u;  // green bloom from the centre

    // Dark green-black screen fill + centre glow. Use INSTEAD of the normal
    // panel fill when the filter is active.
    inline void paintScreen(juce::Graphics& g, juce::Rectangle<float> bounds, float corner)
    {
        g.setColour(juce::Colour(kScreenBg));
        g.fillRoundedRectangle(bounds, corner);
        juce::ColourGradient glow(juce::Colour(kGlow),
                                  bounds.getCentreX(), bounds.getCentreY(),
                                  juce::Colours::transparentBlack,
                                  bounds.getX(), bounds.getY(), true);
        g.setGradientFill(glow);
        g.fillRoundedRectangle(bounds, corner);
    }

    // Baked scanline (1px dark line every 3px) + radial vignette texture.
    inline juce::Image buildScreenTexture(int w, int h)
    {
        juce::Image img(juce::Image::ARGB, juce::jmax(1, w), juce::jmax(1, h), true);
        juce::Graphics ig(img);
        ig.setColour(juce::Colours::black.withAlpha(0.16f));
        for (int y = 0; y < h; y += 3)
            ig.fillRect(0, y, w, 1);
        juce::ColourGradient vig(juce::Colours::transparentBlack,
                                 static_cast<float>(w) * 0.5f, static_cast<float>(h) * 0.5f,
                                 juce::Colours::black.withAlpha(0.55f),
                                 0.0f, 0.0f, true);
        vig.addColour(0.65, juce::Colours::transparentBlack);
        ig.setGradientFill(vig);
        ig.fillRect(0, 0, w, h);
        return img;
    }

    // Clipped blit of the scanline/vignette overlay, drawn last (over the
    // content). `cache`/`cw`/`ch` are caller-owned persistent storage so the
    // texture is rebuilt only on resize.
    inline void blitOverlay(juce::Graphics& g, juce::Image& cache, int& cw, int& ch,
                            juce::Rectangle<float> bounds, float corner)
    {
        const int wI = juce::jmax(1, juce::roundToInt(bounds.getWidth()));
        const int hI = juce::jmax(1, juce::roundToInt(bounds.getHeight()));
        if (! cache.isValid() || cw != wI || ch != hI)
        {
            cache = buildScreenTexture(wI, hI);
            cw = wI;
            ch = hI;
        }
        juce::Graphics::ScopedSaveState ss(g);
        juce::Path clip;
        clip.addRoundedRectangle(bounds, corner);
        g.reduceClipRegion(clip);
        g.drawImageAt(cache, juce::roundToInt(bounds.getX()),
                      juce::roundToInt(bounds.getY()));
    }
}

} // namespace bombo
