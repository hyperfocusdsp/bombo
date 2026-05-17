#include "BBSComponent.h"

#include "../Fonts.h"
#include "../Theme/ThemeProvider.h"

namespace bombo
{

namespace
{
// Placeholder banner — replaced by AsciiArt.h content in Phase 1d.
// Lines must be the same width so the centered draw doesn't jitter.
constexpr const char* kBannerLines[] = {
    "                                                          ",
    "   ##  ##  ## ##  ## ## ## ##  ## ##  ## ##   #  ##  ##  ",
    "   ##  ##  ##  #  ## #  ## ##  ## ##  ## ##   #  ##  ##  ",
    "   ######  ## ##  ## #  ## ##  ## ##  ## ##  ##  ##  ##  ",
    "   ##  ##  ##     ## #  ## ##  ## ##  ## ##   #  ##  ##  ",
    "   ##  ##  ##  #  ## #  ## ##  ##  ####   #####  ##  ##  ",
    "                                                          ",
    "         H Y P E R F O C U S   B B S   //   1 9 9 2       ",
    "                                                          ",
    "         CONNECTION ESTABLISHED -- ESC TO DISCONNECT      ",
    "                                                          "
};
constexpr int kBannerLineCount =
    sizeof(kBannerLines) / sizeof(kBannerLines[0]);
} // anonymous namespace

BBSComponent::BBSComponent()
{
    setOpaque(false);          // backdrop is translucent, not fill-overrides
    setInterceptsMouseClicks(true, false);
    setWantsKeyboardFocus(true);
    setVisible(false);
}

void BBSComponent::show()
{
    setVisible(true);
    toFront(true);             // sibling of faceplate — must paint on top
    grabKeyboardFocus();
    if (onShown) onShown();
}

void BBSComponent::hide()
{
    setVisible(false);
    if (onDismissed) onDismissed();
}

bool BBSComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        hide();
        return true;
    }
    // Swallow other keys while open — don't let them leak to the faceplate
    // (which would, e.g., re-trigger the kick if T is pressed).
    return true;
}

void BBSComponent::paint(juce::Graphics& g)
{
    const auto& p = bombo::ThemeProvider::current();
    const auto bounds = getLocalBounds();

    // Backdrop: graphite at 0.92 alpha — faceplate shows through faintly,
    // sells the "you're inside the bomb" reading rather than a hard cut.
    g.fillAll(p.graphite.withAlpha(0.92f));

    // Banner — monospace, bone fg with amber accent on the title line.
    // Cells sized to the smaller of (width/banner_chars, height/banner_lines)
    // so the banner stays inscribed at any aspect.
    constexpr float kPadFrac = 0.10f;
    const float availW = static_cast<float>(bounds.getWidth())  * (1.0f - 2.0f * kPadFrac);
    const float availH = static_cast<float>(bounds.getHeight()) * (1.0f - 2.0f * kPadFrac);

    int maxChars = 0;
    for (int i = 0; i < kBannerLineCount; ++i)
    {
        const int len = static_cast<int>(std::strlen(kBannerLines[i]));
        if (len > maxChars) maxChars = len;
    }
    const float cellW = availW / static_cast<float>(maxChars);
    const float cellH = availH / static_cast<float>(kBannerLineCount);
    const float cell  = std::min(cellW, cellH);

    // Use the project's canonical monospace font — keeps weight/metrics
    // consistent with the value readouts elsewhere in the UI.
    g.setFont(bombo::fonts::value(cell * 1.6f));

    const float bannerHeight = cell * static_cast<float>(kBannerLineCount);
    const float bannerWidth  = cell * static_cast<float>(maxChars);
    const float xStart = (static_cast<float>(bounds.getWidth())  - bannerWidth)  * 0.5f;
    const float yStart = (static_cast<float>(bounds.getHeight()) - bannerHeight) * 0.5f;

    for (int i = 0; i < kBannerLineCount; ++i)
    {
        // Title line (the "H Y P E R F O C U S  B B S" one, by convention
        // the second-from-last with text) gets the amber accent.
        const bool isTitleLine = (i == 7);
        g.setColour(isTitleLine ? p.accentAmber : p.bone);

        const juce::String line(kBannerLines[i]);
        const juce::Rectangle<float> r(xStart,
                                       yStart + cell * static_cast<float>(i),
                                       bannerWidth,
                                       cell);
        g.drawText(line, r, juce::Justification::centred, false);
    }
}

} // namespace bombo
