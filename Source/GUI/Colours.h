#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bombo::col
{

// Chassis / panel.
inline const juce::Colour graphite   { 0xFF14'1517u };  // Bombo base background.
inline const juce::Colour graphiteHi { 0xFF1A'1C'1Fu };  // Section fill.
inline const juce::Colour ink        { 0xFF0A'0B'0Du };  // Recesses, label silk.
inline const juce::Colour bone       { 0xFFF4'F1'EAu };  // Hyperfocus warm white.
inline const juce::Colour boneDim    { 0xFF8A'88'82u };  // Sub-label / secondary text.

// Section accents — mil-rice palette, ported from the Rust archive's
// editor.rs C_* constants. Each FX section in the rack carries its own
// tint on knob ticks + label underline.
inline const juce::Colour voice      { 0xFFA8'98'80u };  // warm bone
inline const juce::Colour drive      { 0xFFC7'7A'55u };  // rust / oxide red
inline const juce::Colour filterC    { 0xFF7B'8E'A8u };  // gunmetal blue
inline const juce::Colour delayC     { 0xFF8F'A6'64u };  // olive drab
inline const juce::Colour reverb     { 0xFFA4'B5'8Au };  // sage
inline const juce::Colour duck       { 0xFFB5'9A'6Bu };  // khaki

inline const juce::Colour accentAmber{ 0xFFFF'B8'00u };  // Hyperfocus hero (used SPARINGLY).

} // namespace bombo::col
