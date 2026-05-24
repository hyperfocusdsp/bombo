#pragma once

#include "Theme/ThemeProvider.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace bombo
{

// In-skin theme selector: a horizontal row of small tiles, one per registered
// theme. Each tile paints a 2-tone thumbnail of its palette (chassis body over
// nose region) so the active visual language is legible at a glance. Replaces
// the temporary juce::ComboBox themeSelector_ from Plan A T7.
//
// The strip listens to ThemeProvider so the active marker repaints whenever
// the active theme changes (whether from a tile click or programmatic call).
class ThemeTileStrip : public juce::Component,
                       public juce::ChangeListener
{
public:
    // onThemeChosen runs when a tile is clicked. Caller is responsible for
    // calling ThemeProvider::setActive() and persisting the choice — the strip
    // does not touch those concerns directly so the same callback path can be
    // shared with any other future trigger (preset-driven theme, MIDI, etc.).
    using OnThemeChosen = std::function<void(const std::string& themeName)>;

    explicit ThemeTileStrip(OnThemeChosen onChosen);
    ~ThemeTileStrip() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // ChangeListener — repaints the active marker when ThemeProvider changes.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Hard-coded geometry — kept simple. Adjust here if the header gets
    // re-proportioned. Tile = small square, palette thumb only (no text).
    // 6 tiles × 20 + 5 × 4 = 140 px wide, fits the header band right of the
    // BOMBO wordmark at design-width 600.
    static constexpr int kTileSize = 20;
    static constexpr int kTileGap  = 4;

private:
    struct Tile
    {
        std::string name;
        juce::Colour bodyHi;
        juce::Colour bodyLo;
        juce::Colour noseRed;
        juce::Rectangle<int> bounds;
    };

    OnThemeChosen onChosen_;
    std::vector<Tile> tiles_;
    int hoverIndex_ = -1;

    int hitTest_(juce::Point<int> p) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeTileStrip)
};

} // namespace bombo
