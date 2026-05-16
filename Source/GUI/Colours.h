#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bombo::col
{

// Chassis / panel — graphite stays dark; bone/ink are the brand neutrals.
inline const juce::Colour graphite   { 0xFF14'15'17u };
inline const juce::Colour graphiteHi { 0xFF1A'1C'1Fu };
inline const juce::Colour ink        { 0xFF0A'0B'0Du };
inline const juce::Colour bone       { 0xFFF4'F1'EAu };
inline const juce::Colour boneDim    { 0xFF8A'88'82u };

// Section column body fills — saturated mil-rice palette to match the
// pre-port reference. Each FX column paints with its own colour; VOICE A
// and VOICE B stay close to chassis so the synth side reads as the
// "neutral" half.
inline const juce::Colour voice      { 0xFF40'3D'38u };  // warm charcoal — sits above chassis
inline const juce::Colour drive      { 0xFFD2'78'45u };  // rust orange
inline const juce::Colour delayC     { 0xFF3E'A4'9Eu };  // teal
inline const juce::Colour reverb     { 0xFF6A'AE'5Au };  // green
inline const juce::Colour filterC    { 0xFF5C'8A'BBu };  // gunmetal blue
inline const juce::Colour duck       { 0xFFC8'A2'71u };  // sand / khaki

// Knob — caps are dark plastic regardless of column tint so values stay
// readable on every backdrop. Bevel is a thin metal ring.
inline const juce::Colour knobCap    { 0xFF1B'1C'1Eu };  // near-black core
inline const juce::Colour knobBevel  { 0xFF60'60'66u };  // metal bevel
inline const juce::Colour knobRubber { 0xFF15'15'17u };  // outer grip

inline const juce::Colour accentAmber{ 0xFFFF'B8'00u };  // hero — OUT macro, LIM pill

} // namespace bombo::col
