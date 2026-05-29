#pragma once

#include <array>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Theme/ThemedComponent.h"
#include "WaveBuffer.h"

namespace bombo
{

// 30 Hz scope strip. Pulls from a `WaveBuffer` owned by the processor and
// paints a single-stroke polyline. The "SCOPE  POST" label sits in the
// top-left corner of the panel, matching the pre-port UI.
class ScopeComponent : public juce::Component,
                       public bombo::ThemedComponent,
                       public juce::TooltipClient,
                       private juce::Timer
{
public:
    ScopeComponent();
    ~ScopeComponent() override;

    // Editor wires the processor's WaveBuffer here once on construction.
    // Caller keeps ownership; lifetime must outlive this component.
    void setWaveBuffer(const WaveBuffer* wb) noexcept { wb_ = wb; }

    // Click anywhere on the scope toggles the VGA/CRT filter. Default state
    // follows the theme (on for FALLOUT's green-phosphor screen, off for the
    // flat/neon themes); a manual click pins it until the next click.
    void mouseDown(const juce::MouseEvent&) override;
    juce::String getTooltip() override;

    // Transient overlay used during the BBS nose 7-tap sequence. Pass the
    // tap number (1..6); the overlay fades out over ~1.2 s. The existing
    // 30 Hz timer keeps repainting while the fade is active. Tap 7 opens
    // BBS so no warning fires.
    void showTapWarning(int tapNumber);

    // Confirmation overlay for the BBS-progression reset (Ctrl+Shift+R /
    // force-reset gesture). Same fade behavior as the tap warning but a
    // distinct message + colour so the user gets unambiguous feedback.
    void showResetConfirmation();

    void paint(juce::Graphics&) override;

private:
    void timerCallback() override;

    const WaveBuffer*                         wb_            = nullptr;
    std::array<float, WaveBuffer::kCapture>   snapshot_{};      // current capture
    std::array<float, WaveBuffer::kCapture>   prevSnapshot_{};  // ghosted previous
    int                                       lastVersion_   = -1;
    int                                       drawUpTo_      = 0;
    int                                       displayLength_ = 0;  // X normaliser for current wave
    int                                       prevDrawnTo_   = 0;  // samples in prevSnapshot_
    int                                       prevXTotal_    = 0;  // X normaliser for prevSnapshot_

    // Generic top-right overlay: tap-progression warnings, reset confirmation,
    // etc. Empty string == no overlay.
    juce::String overlayText_;
    juce::Colour overlayColour_;
    juce::Time   overlayStart_;
    static constexpr int kOverlayDurationMs = 1200;

    // ── VGA / CRT filter ────────────────────────────────────────────────
    // Effective state = manual override if set, else theme default (FALLOUT
    // only). Cheap: nothing extra is painted when inactive, so the flat/neon
    // themes pay zero cost. The scanline + vignette layer is baked once into
    // crtScreen_ and blitted; only the chromatic-aberration wave passes are
    // per-frame (a couple of extra strokePath on an already-built path).
    bool crtActive() const;
    void buildCrtScreen(int w, int h);

    bool         crtManual_    = false;   // has the user clicked to override?
    bool         crtUserState_ = false;   // the override value
    juce::Image  crtScreen_;              // baked scanlines + vignette
    int          crtScreenW_   = 0;
    int          crtScreenH_   = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeComponent)
};

} // namespace bombo
