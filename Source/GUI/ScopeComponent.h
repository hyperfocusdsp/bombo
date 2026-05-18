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
                       private juce::Timer
{
public:
    ScopeComponent();
    ~ScopeComponent() override;

    // Editor wires the processor's WaveBuffer here once on construction.
    // Caller keeps ownership; lifetime must outlive this component.
    void setWaveBuffer(const WaveBuffer* wb) noexcept { wb_ = wb; }

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeComponent)
};

} // namespace bombo
