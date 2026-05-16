#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bombo::fonts
{

// Loaded from BinaryData::AllertaStencilRegular_ttf — cached as a
// Typeface::Ptr at startup. The first font::title()/etc. call after
// program load creates the cache lazily.
juce::Font title (float pointSize);  // Allerta Stencil — section headers.
juce::Font label (float pointSize);  // JUCE default sans bold — knob labels.
juce::Font value (float pointSize);  // JUCE default mono — value readouts.

} // namespace bombo::fonts
