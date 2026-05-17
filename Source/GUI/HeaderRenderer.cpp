#include "HeaderRenderer.h"

#include "Colours.h"
#include "Fonts.h"

namespace bombo::headerRenderer
{

void draw(juce::Graphics& g,
          juce::Rectangle<int> area,
          const juce::Path& chassisClip)
{
    // Graphite-hi strip, clipped to the chassis silhouette so the rounded
    // corners stay crisp at the chassis edges.
    g.saveState();
    g.reduceClipRegion(chassisClip);
    g.setColour(col::graphiteHi());
    g.fillRect(area);
    g.restoreState();

    // BOMBO logo, left-aligned with a 20 px inset.
    g.setColour(col::bone());
    g.setFont(fonts::title(26.0f));
    g.drawText("BOMBO",
               area.withTrimmedLeft(20).removeFromLeft(160),
               juce::Justification::centredLeft);

    // Hairline at the bottom edge — separates header from scope below.
    g.setColour(col::ink().withAlpha(0.7f));
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);
}

} // namespace bombo::headerRenderer
