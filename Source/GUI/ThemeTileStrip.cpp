#include "ThemeTileStrip.h"

#include "Colours.h"

namespace bombo
{

ThemeTileStrip::ThemeTileStrip(OnThemeChosen onChosen)
    : onChosen_(std::move(onChosen))
{
    setInterceptsMouseClicks(true, false);

    auto& tp = ThemeProvider::get();
    for (const auto& name : tp.registeredNames())
    {
        if (auto* pal = tp.getRegisteredPalette(name))
        {
            Tile t;
            t.name    = name;
            t.bodyHi  = pal->bodyHi;
            t.bodyLo  = pal->bodyLo;
            t.noseRed = pal->noseRed;
            tiles_.push_back(t);
        }
    }

    tp.addChangeListener(this);
}

ThemeTileStrip::~ThemeTileStrip()
{
    ThemeProvider::get().removeChangeListener(this);
}

void ThemeTileStrip::resized()
{
    int x = 0;
    const int y = (getHeight() - kTileSize) / 2;
    for (auto& t : tiles_)
    {
        t.bounds = { x, y, kTileSize, kTileSize };
        x += kTileSize + kTileGap;
    }
}

void ThemeTileStrip::paint(juce::Graphics& g)
{
    // Subtle ink tray behind the tiles so the strip reads as a deliberate
    // selector rather than a floating row of squares. Rounded-rect with low
    // alpha keeps it from competing visually with the chassis above.
    const auto trayBounds = getLocalBounds().toFloat().expanded(4.0f);
    g.setColour(col::ink().withAlpha(0.45f));
    g.fillRoundedRectangle(trayBounds, 6.0f);
    g.setColour(col::bone().withAlpha(0.10f));
    g.drawRoundedRectangle(trayBounds.reduced(0.5f), 6.0f, 1.0f);

    const auto activeName = ThemeProvider::get().activeName();

    for (size_t i = 0; i < tiles_.size(); ++i)
    {
        const auto& t = tiles_[i];
        const bool isActive = (t.name == activeName);
        const bool isHover  = (static_cast<int>(i) == hoverIndex_);

        // Two-tone thumbnail: bodyHi on top ~70%, noseRed on bottom ~30%.
        // Mirrors how the chassis itself splits.
        const auto r       = t.bounds.toFloat();
        const float splitY = r.getY() + r.getHeight() * 0.70f;

        // Body half: vertical gradient bodyHi → bodyLo for depth.
        juce::ColourGradient bodyGrad (t.bodyHi, r.getX(), r.getY(),
                                       t.bodyLo, r.getX(), splitY,
                                       false);
        g.setGradientFill(bodyGrad);
        g.fillRect(r.withBottom(splitY));

        // Nose half.
        g.setColour(t.noseRed);
        g.fillRect(r.withTop(splitY));

        // Hover lift: thin bone overlay on the whole tile.
        if (isHover && ! isActive)
        {
            g.setColour(col::bone().withAlpha(0.10f));
            g.fillRect(r);
        }

        // Outline. Active = bone 1.5 px. Inactive = bone @ 0.30 alpha 1 px.
        if (isActive)
        {
            g.setColour(col::bone());
            g.drawRect(r, 1.5f);
        }
        else
        {
            g.setColour(col::bone().withAlpha(0.30f));
            g.drawRect(r, 1.0f);
        }
    }
}

int ThemeTileStrip::hitTest_(juce::Point<int> p) const
{
    for (size_t i = 0; i < tiles_.size(); ++i)
        if (tiles_[i].bounds.contains(p))
            return static_cast<int>(i);
    return -1;
}

void ThemeTileStrip::mouseDown(const juce::MouseEvent& e)
{
    const int idx = hitTest_(e.getPosition());
    if (idx < 0) return;
    if (onChosen_)
        onChosen_(tiles_[static_cast<size_t>(idx)].name);
}

void ThemeTileStrip::mouseMove(const juce::MouseEvent& e)
{
    const int idx = hitTest_(e.getPosition());
    if (idx != hoverIndex_)
    {
        hoverIndex_ = idx;
        setMouseCursor(idx >= 0 ? juce::MouseCursor::PointingHandCursor
                                : juce::MouseCursor::NormalCursor);
        repaint();
    }
}

void ThemeTileStrip::mouseExit(const juce::MouseEvent& /*e*/)
{
    if (hoverIndex_ != -1)
    {
        hoverIndex_ = -1;
        repaint();
    }
}

void ThemeTileStrip::changeListenerCallback(juce::ChangeBroadcaster* /*source*/)
{
    repaint();
}

} // namespace bombo
