#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bombo
{

// POD palette mirroring every entry in the pre-refactor Source/GUI/Colours.h.
// Add new fields here ONLY when adding a new themeable surface.
struct Palette
{
    // Chassis / panel
    juce::Colour graphite;
    juce::Colour graphiteHi;
    juce::Colour ink;
    juce::Colour bone;
    juce::Colour boneDim;

    // Section column body fills
    juce::Colour voice;
    juce::Colour drive;
    juce::Colour delayC;
    juce::Colour reverb;
    juce::Colour filterC;
    juce::Colour duck;

    // Knob
    juce::Colour knobCap;
    juce::Colour knobBevel;
    juce::Colour knobRubber;

    // Accent
    juce::Colour accentAmber;
};

// Hard-coded BANDW palette = exact values from pre-refactor Colours.h.
// Used as the fallback when no JSON theme is loaded.
inline Palette bandwPalette()
{
    Palette p;
    p.graphite   = juce::Colour { 0xFF141517u };
    p.graphiteHi = juce::Colour { 0xFF1A1C1Fu };
    p.ink        = juce::Colour { 0xFF0A0B0Du };
    p.bone       = juce::Colour { 0xFFF4F1EAu };
    p.boneDim    = juce::Colour { 0xFF8A8882u };
    p.voice      = juce::Colour { 0xFF403D38u };
    p.drive      = juce::Colour { 0xFFD27845u };
    p.delayC     = juce::Colour { 0xFF3EA49Eu };
    p.reverb     = juce::Colour { 0xFF6AAE5Au };
    p.filterC    = juce::Colour { 0xFF5C8ABBu };
    p.duck       = juce::Colour { 0xFFC8A271u };
    p.knobCap    = juce::Colour { 0xFF1B1C1Eu };
    p.knobBevel  = juce::Colour { 0xFF606066u };
    p.knobRubber = juce::Colour { 0xFF151517u };
    p.accentAmber= juce::Colour { 0xFFFFB800u };
    return p;
}

} // namespace bombo
