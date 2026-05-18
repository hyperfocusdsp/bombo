#include "ScopeComponent.h"

#include <algorithm>

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

    const int ver = wb_->triggerVersion();
    if (ver != lastVersion_)
    {
        lastVersion_ = ver;
        // Freeze the current wave as the ghost before starting the new cycle.
        prevSnapshot_  = snapshot_;
        prevDrawnTo_   = drawUpTo_;
        prevXTotal_    = displayLength_ > 0 ? displayLength_ : std::max(drawUpTo_, 1);
        // Reset for the new capture.
        drawUpTo_      = 0;
        displayLength_ = wb_->prevLength();  // 0 on very first trigger
        repaint();
    }

    const int wp = wb_->writePos();
    if (wp != drawUpTo_)
    {
        const float* src = wb_->data();
        const int end = juce::jmin(wp, WaveBuffer::kCapture);
        for (int i = drawUpTo_; i < end; ++i)
            snapshot_[i] = src[i];
        drawUpTo_ = end;
        repaint();
    }
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

    // Three-layer phosphor scope:
    //   1. Ghost  — previous waveform at low alpha (full width, frozen)
    //   2. Live   — current waveform at full alpha, growing left→right
    //   3. Playhead — thin amber hairline at the live/ghost boundary
    const auto plotArea = bounds.reduced(10.0f, 18.0f);
    const float w     = plotArea.getWidth();
    const float midY  = plotArea.getCentreY();
    const float halfH = plotArea.getHeight() * 0.5f * 0.92f;

    if (w <= 1.0f) return;

    const juce::PathStrokeType thinStroke (1.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const juce::PathStrokeType thickStroke(1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    // --- Layer 1: ghost (previous waveform, dim) ---
    if (prevDrawnTo_ >= 2 && prevXTotal_ > 1)
    {
        const float invPrev = 1.0f / static_cast<float>(prevXTotal_ - 1);
        juce::Path ghost;
        for (int i = 0; i < prevDrawnTo_; ++i)
        {
            const float x = plotArea.getX() + static_cast<float>(i) * invPrev * w;
            const float v = juce::jlimit(-1.5f, 1.5f, prevSnapshot_[i]);
            const float y = midY - v * halfH;
            if (i == 0) ghost.startNewSubPath(x, y);
            else        ghost.lineTo(x, y);
        }
        g.setColour(col::bone().withAlpha(0.22f));
        g.strokePath(ghost, thinStroke);
    }

    // --- Layer 2: live waveform (current capture, bright) ---
    const int n = drawUpTo_;
    if (n >= 2)
    {
        const int xTotal     = displayLength_ > 0 ? displayLength_ : n;
        const float invTotal = 1.0f / static_cast<float>(std::max(xTotal - 1, 1));

        juce::Path live;
        for (int i = 0; i < n; ++i)
        {
            const float x = plotArea.getX() + static_cast<float>(i) * invTotal * w;
            const float v = juce::jlimit(-1.5f, 1.5f, snapshot_[i]);
            const float y = midY - v * halfH;
            if (i == 0) live.startNewSubPath(x, y);
            else        live.lineTo(x, y);
        }
        g.setColour(col::bone().withAlpha(0.88f));
        g.strokePath(live, thickStroke);

        // --- Layer 3: playhead hairline (only while actively capturing) ---
        const int xTotal2 = displayLength_ > 0 ? displayLength_ : std::max(n, 1);
        if (n < xTotal2)
        {
            const float px = plotArea.getX()
                           + static_cast<float>(n) / static_cast<float>(std::max(xTotal2 - 1, 1)) * w;
            g.setColour(col::accentAmber().withAlpha(0.85f));
            g.drawVerticalLine(static_cast<int>(px),
                               plotArea.getY(),
                               plotArea.getBottom());
        }
    }
}

} // namespace bombo
