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

// True on themes where `ink` is a LIGHT secondary foreground instead of
// a dark recess colour (MATRIX/CYBER/PLASMA). These themes need
// dark-bg + accent-text styling for toggle pills, the OUT macro cap,
// the scope panel, and nose macro labels — otherwise the saturated neon
// `accentAmber`/`bone` end up text-on-text or text-on-glow and become
// unreadable. The ink-brightness check is the cleanest discriminator:
// neon palettes invert ink's semantic from "dark ink" to "light off-bone".
inline bool isNeon() noexcept { return ink().getPerceivedBrightness() > 0.5f; }

} // namespace bombo::col
