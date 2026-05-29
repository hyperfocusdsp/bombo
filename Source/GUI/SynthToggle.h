#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Colours.h"

namespace bombo
{

// Compact pill toggle that paints a downward-decay ramp icon instead of
// text. Drives the Voice B synth-layer master gate (pid::voiceBSynthOn):
//   ON  = mid sine + click + noise active (default — preserves legacy
//         presets)
//   OFF = bypassed → Voice B is sample-only.
//
// Visual: same pill chrome as LIM/TAIL/LOOP (neon-aware: dark bg + accent
// border on matrix/cyber/plasma; amber-tinted fill on classic). The icon
// is a short downward ramp on the right side of the pill, mirroring the
// kick-envelope curve. The icon is always drawn — toggle state is signaled
// by border thickness and accent opacity, same idiom as LoopButton.
class SynthToggle : public juce::ToggleButton
{
public:
    SynthToggle() : juce::ToggleButton(juce::String()) {}

    void paintButton(juce::Graphics& g, bool, bool) override
    {
        const auto r = getLocalBounds().toFloat();
        const bool on = getToggleState();
        const bool neon = col::isNeon();
        const bool fallout = (col::chassisArt() == "fallout");

        // Pill bg + amber border — mirrors drawToggleButton (LIM/TAIL/LOOP).
        if (fallout)
        {
            // Recessed-in-slot look (matches bombo::pill on FALLOUT): dark fill,
            // no bright border off; subtle amber keyline when ON.
            g.setColour(col::ink().withAlpha(on ? 0.30f : 0.55f));
            g.fillRoundedRectangle(r, 4.0f);
            if (on)
            {
                g.setColour(col::accentAmber().withAlpha(0.45f));
                g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
            }
        }
        else if (neon)
        {
            g.setColour(col::graphite().withAlpha(on ? 0.95f : 0.88f));
            g.fillRoundedRectangle(r, 4.0f);
            g.setColour(on ? col::accentAmber()
                           : col::accentAmber().withAlpha(0.55f));
            g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, on ? 1.5f : 1.0f);
        }
        else
        {
            g.setColour(on ? col::accentAmber().withAlpha(0.40f)
                           : col::graphite().withAlpha(0.88f));
            g.fillRoundedRectangle(r, 4.0f);
            g.setColour(on ? col::accentAmber()
                           : col::accentAmber().withAlpha(0.50f));
            g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        }

        // Sharp-fall decay icon: a baseline that holds high across the
        // first third, then drops steeply almost-vertical down to the
        // bottom right. Reads as a percussive amp envelope — much
        // closer to an actual kick decay than a smooth ramp (user
        // feedback 2026-05-24). Stroke colour matches the LoopButton
        // convention.
        const auto inner = r.reduced(4.0f, 3.0f);
        const float left    = inner.getX();
        const float right   = inner.getRight();
        const float top     = inner.getY() + 1.0f;
        const float bottom  = inner.getBottom() - 1.0f;
        const float kneeX   = left + inner.getWidth() * 0.30f;  // hold-then-fall knee

        juce::Path ramp;
        ramp.startNewSubPath(left, top);
        // Flat segment at the top — the "instant attack" plateau.
        ramp.lineTo(kneeX, top);
        // Steep fall via a tight quadratic that sweeps the line almost
        // vertical out of the knee, easing toward the bottom-right.
        // Control point pulled hard down-and-left keeps the early part
        // of the fall near-vertical, giving the sharp "kick decay" feel.
        ramp.quadraticTo(kneeX + inner.getWidth() * 0.08f, bottom,
                         right, bottom);

        g.setColour(fallout
                    ? (on ? col::accentAmber() : col::bone().withAlpha(0.85f))
                    : neon
                        ? col::accentAmber().withAlpha(on ? 1.0f : 0.75f)
                        : (on ? col::ink() : col::bone()));
        g.strokePath(ramp, juce::PathStrokeType(1.5f,
                            juce::PathStrokeType::curved,
                            juce::PathStrokeType::rounded));
    }

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SynthToggle)
};

} // namespace bombo
