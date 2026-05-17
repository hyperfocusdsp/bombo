#pragma once

#include <juce_graphics/juce_graphics.h>

namespace bombo::headerRenderer
{
    // Stateless renderer for the Bombo faceplate header band. Paints the
    // graphite-hi strip (clipped to the chassis silhouette), the BOMBO logo,
    // and the bottom hairline. Header pills (BPM/LIM/LOOP/DICE) paint
    // themselves — they are real components positioned by layoutHeader().
    void draw(juce::Graphics& g,
              juce::Rectangle<int> area,
              const juce::Path& chassisClip);
}
