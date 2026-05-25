#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>

namespace bombo
{

// Declick the loop-cache seam.
//
// The loop-cache (PluginProcessor LoopCache) captures exactly one beat of
// stereo output and replays it for every subsequent loop beat. The capture
// window opens just AFTER a killTail() (so buf[0] is the near-silent attack
// onset) and closes one beat later mid-tail (so buf[beatSamples-1] still
// holds the reverb tail). With a long reverb that tail is loud, and the
// hard replay wrap buf[last] -> buf[0] is an audible step = a click on
// every beat after the first.
//
// The live signal hides this because killTail() fades the old tail over a
// few ms before the new attack — but that fade lands BETWEEN beats, which
// the one-beat capture window omits. This re-applies it: a raised-cosine
// fade-to-zero over the last `fadeMs` of the captured beat, mirroring the
// per-kick TAIL-ON chop. (Tight chop IS the intended TAIL-ON semantic;
// TAIL OFF keeps the cache inactive and rings the tail live across beats.)
//
// Operates on the first `beatSamples` of `buf` only. No allocation; safe to
// call once per capture from the audio thread.
inline void applyLoopSeamFade(juce::AudioBuffer<float>& buf,
                              int   beatSamples,
                              float sampleRate,
                              float fadeMs = 10.0f) noexcept
{
    if (beatSamples <= 1 || sampleRate <= 0.0f) return;
    int fade = static_cast<int>(fadeMs * 0.001f * sampleRate);
    fade = juce::jlimit(1, beatSamples / 4, fade);

    const int nCh = buf.getNumChannels();
    for (int k = 0; k < fade; ++k)
    {
        // g: 1 -> 0 across the last `fade` samples (Hann half-window).
        const float ph = juce::MathConstants<float>::pi
                       * static_cast<float>(k) / static_cast<float>(fade);
        const float g  = 0.5f * (1.0f + std::cos(ph));
        const int   idx = beatSamples - fade + k;
        for (int ch = 0; ch < nCh; ++ch)
            buf.setSample(ch, idx, buf.getSample(ch, idx) * g);
    }
}

} // namespace bombo
