#include "ScopeComponent.h"

#include "Colours.h"
#include "Fonts.h"

namespace bombo
{

ScopeComponent::ScopeComponent()
{
    setOpaque(false);
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

ScopeComponent::~ScopeComponent()
{
    stopTimer();
}

void ScopeComponent::timerCallback()
{
    if (wb_ == nullptr) return;
    wb_->readLatest(snapshot_.data(), static_cast<int>(snapshot_.size()));
    repaint();
}

void ScopeComponent::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();

    // Recessed ink panel — matches the dark scope well in the pre-port UI.
    g.setColour(col::ink());
    g.fillRoundedRectangle(bounds, 3.0f);
    g.setColour(col::graphiteHi());
    g.drawRoundedRectangle(bounds.reduced(0.5f), 3.0f, 1.0f);

    // Label
    g.setColour(col::boneDim());
    g.setFont(fonts::label(9.0f));
    g.drawText("SCOPE  \xE2\x80\xA2  POST",
               bounds.reduced(10.0f, 6.0f).removeFromTop(14.0f),
               juce::Justification::topLeft);

    // Centerline hairline so an empty scope still reads as "an oscilloscope".
    g.setColour(col::bone().withAlpha(0.06f));
    g.drawHorizontalLine(static_cast<int>(bounds.getCentreY()),
                         bounds.getX() + 8.0f, bounds.getRight() - 8.0f);

    // Polyline. Sample index i → x; sample value → y mirrored about centre.
    const auto plotArea = bounds.reduced(10.0f, 18.0f);
    const float w     = plotArea.getWidth();
    const float midY  = plotArea.getCentreY();
    const float halfH = plotArea.getHeight() * 0.5f * 0.92f;

    const int n = static_cast<int>(snapshot_.size());
    if (n < 2 || w <= 1.0f) return;

    juce::Path path;
    bool started = false;
    const float invN = 1.0f / static_cast<float>(n - 1);
    for (int i = 0; i < n; ++i)
    {
        const float x = plotArea.getX() + static_cast<float>(i) * invN * w;
        const float v = juce::jlimit(-1.5f, 1.5f, snapshot_[i]);
        const float y = midY - v * halfH;
        if (!started) { path.startNewSubPath(x, y); started = true; }
        else          { path.lineTo(x, y); }
    }

    g.setColour(col::bone().withAlpha(0.88f));
    g.strokePath(path,
                 juce::PathStrokeType(1.3f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
}

} // namespace bombo
