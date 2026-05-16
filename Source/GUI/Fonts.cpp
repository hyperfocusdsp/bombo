#include "Fonts.h"

#include <BinaryData.h>

namespace bombo::fonts
{

static juce::Typeface::Ptr stencilTypeface()
{
    // Lazy singleton — the typeface lives for the editor's lifetime;
    // JUCE caches glyph rasterisations against it.
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor(
        BinaryData::AllertaStencilRegular_ttf,
        BinaryData::AllertaStencilRegular_ttfSize);
    return tf;
}

juce::Font title(float pointSize)
{
    return juce::Font(juce::FontOptions().withTypeface(stencilTypeface())
                                         .withHeight(pointSize));
}

juce::Font label(float pointSize)
{
    return juce::Font(juce::FontOptions(pointSize, juce::Font::bold));
}

juce::Font value(float pointSize)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(),
                                        pointSize, juce::Font::plain));
}

} // namespace bombo::fonts
