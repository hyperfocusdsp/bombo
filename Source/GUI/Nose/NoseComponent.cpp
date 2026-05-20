#include "NoseComponent.h"

namespace bombo
{

NoseComponent::NoseComponent()
{
    setOpaque(false);
    setInterceptsMouseClicks(true, false);
    setRepaintsOnMouseActivity(false);
}

void NoseComponent::setProgressionLevel(int level)
{
    level_ = juce::jlimit(0, 4, level);
    repaint();
}

juce::String NoseComponent::getTooltip()
{
    // After first BBS unlock the tooltip reflects the persistent progression
    // level. Before unlock it ratchets through tap-progression warnings so
    // a curious user gets escalating dread feedback during the 7-tap sequence.
    if (firstEntryDone_)
        return kTooltips[juce::jlimit(0, 4, level_)];

    const auto sinceTap = (juce::Time::getCurrentTime() - lastTapTime_).inMilliseconds();
    const int idx = (sinceTap > kTapTimeoutMs)
                    ? 0
                    : juce::jlimit(0, 6, tapCount_);
    return kTapTooltips[idx];
}

bool NoseComponent::hitTest(int x, int y)
{
    // The overlay covers the macro cluster (OUT + 6 satellites). Without this
    // pass-through, the overlay's full-bounds mouse interception eats every
    // knob click. macroHitTester (installed by FaceplatePanel) returns true
    // when the point sits on a macro slider; we then return false so JUCE
    // dispatches the click to the slider beneath.
    if (macroHitTester && macroHitTester(juce::Point<int>(x, y)))
        return false;
    return true;
}

void NoseComponent::mouseDown(const juce::MouseEvent&)
{
    const auto now = juce::Time::getCurrentTime();
    const bool timedOut = (now - lastTapTime_).inMilliseconds() > kTapTimeoutMs;

    if (timedOut) tapCount_ = 0;
    lastTapTime_ = now;

    // Force-reset: only available after first-time sequence is done.
    if (firstEntryDone_ && isForceResetReady && isForceResetReady())
    {
        const bool fastTap = (now - lastResetTapTime_).inMilliseconds() < kResetTimeout;
        lastResetTapTime_ = now;
        resetTapCount_ = fastTap ? resetTapCount_ + 1 : 1;
        if (resetTapCount_ >= kResetTaps)
        {
            resetTapCount_ = 0;
            if (onForceReset) onForceReset();
            return;  // don't also open BBS
        }
    }

    if (firstEntryDone_)
    {
        if (onActivationComplete) onActivationComplete();
        return;
    }

    ++tapCount_;
    if (tapCount_ < kRequiredTaps)
    {
        if (onGlitchTap) onGlitchTap(tapCount_);
    }
    else
    {
        tapCount_ = 0;
        if (onActivationComplete) onActivationComplete();
    }
}

void NoseComponent::paint(juce::Graphics& g)
{
    if (level_ <= 0) return;  // level 0 = no overlay; faceplate paints the nose
    const auto b = getLocalBounds().toFloat();
    paintCracks(g, b, level_);
    if (level_ >= 2) paintGlow(g, b, level_);
}

void NoseComponent::paintCracks(juce::Graphics& g, juce::Rectangle<float> b, int level)
{
    g.setColour(juce::Colour(0xFF1A1A1A).withAlpha(0.8f));
    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    const float scale = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f;

    // L1+: hairline crack from upper-right
    juce::Path crack;
    crack.startNewSubPath(cx + scale * 0.3f, cy - scale * 0.5f);
    crack.lineTo(cx + scale * 0.1f, cy - scale * 0.1f);
    g.strokePath(crack, juce::PathStrokeType(level >= 3 ? 2.0f : 1.0f));

    if (level >= 2)
    {
        juce::Path crack2;
        crack2.startNewSubPath(cx + scale * 0.1f, cy - scale * 0.1f);
        crack2.lineTo(cx - scale * 0.2f, cy + scale * 0.3f);
        crack2.lineTo(cx + scale * 0.05f, cy + scale * 0.5f);
        g.strokePath(crack2, juce::PathStrokeType(level >= 3 ? 1.8f : 1.2f));
    }

    if (level >= 3)
    {
        juce::Path crack3;
        crack3.startNewSubPath(cx - scale * 0.4f, cy - scale * 0.2f);
        crack3.lineTo(cx - scale * 0.1f, cy + scale * 0.1f);
        crack3.lineTo(cx - scale * 0.3f, cy + scale * 0.4f);
        g.strokePath(crack3, juce::PathStrokeType(1.5f));
    }
}

void NoseComponent::paintGlow(juce::Graphics& g, juce::Rectangle<float> b, int level)
{
    const float alpha = (level == 2) ? 0.15f
                      : (level == 3) ? 0.30f
                      :                0.55f;  // level 4

    const auto glowColour = (level >= 4)
        ? juce::Colour(0xFFC8FF8Cu).withAlpha(alpha)
        : juce::Colour(0xFFFFE066u).withAlpha(alpha);

    g.setGradientFill(juce::ColourGradient(
        glowColour,
        b.getCentreX(), b.getCentreY(),
        juce::Colours::transparentBlack,
        b.getCentreX() + b.getWidth() * 0.5f, b.getCentreY(),
        true));
    g.fillRect(b);
}

} // namespace bombo
