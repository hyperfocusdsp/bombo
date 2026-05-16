#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace bombo
{

// One-shot punch-layer sample. Loaded on the message thread, fed to voices
// at trigger time via a shared_ptr. The audio thread only reads — never
// allocates, never holds the load mutex.
//
// On load: mono-mix to 1ch, resample to the host sample rate, clip total
// playback length to ≤ 200 ms (per the user spec "autofade regardless of
// sample length to 20–200 ms"), then bake a linear fade-to-silence ramp
// in-place so the per-sample mix loop is just a buffer read.
struct SampleSlot
{
    // Returns a fully prepared mono buffer ready for voice playback,
    // or nullptr on any load failure (host should show no filename).
    // Fade shape: linear 1.0 → 0.0 across the clipped length.
    static std::shared_ptr<const juce::AudioBuffer<float>>
    loadFromFile(const juce::File& file, double targetSampleRate)
    {
        if (! file.existsAsFile() || targetSampleRate <= 0.0)
            return nullptr;

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader(fm.createReaderFor(file));
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return nullptr;

        // Read the leading slice — up to twice the cap so resampling has
        // headroom for sample-rate conversion. Cast to size_t carefully.
        constexpr double kMaxMs = 200.0;
        const double srcSR = reader->sampleRate > 0.0
                           ? reader->sampleRate : targetSampleRate;
        const juce::int64 cap = static_cast<juce::int64>(
            std::ceil((kMaxMs * 0.001) * srcSR));
        const juce::int64 srcLen = std::min<juce::int64>(reader->lengthInSamples, cap);
        const int srcLenI = static_cast<int>(srcLen);
        if (srcLenI <= 0) return nullptr;

        juce::AudioBuffer<float> srcBuf(
            static_cast<int>(reader->numChannels), srcLenI);
        srcBuf.clear();
        if (! reader->read(&srcBuf, 0, srcLenI, 0, true, true))
            return nullptr;

        // Mono-mix in place (channel 0 holds the average).
        const int nc = srcBuf.getNumChannels();
        if (nc > 1)
        {
            auto* dst = srcBuf.getWritePointer(0);
            const float scale = 1.0f / static_cast<float>(nc);
            for (int ch = 1; ch < nc; ++ch)
            {
                const auto* src = srcBuf.getReadPointer(ch);
                for (int i = 0; i < srcLenI; ++i) dst[i] += src[i];
            }
            for (int i = 0; i < srcLenI; ++i) dst[i] *= scale;
        }

        // Resample (if needed) to the host SR via JUCE's Lagrange interpolator.
        std::vector<float> resampled;
        const float ratio = static_cast<float>(srcSR / targetSampleRate);
        const int outLen = static_cast<int>(
            std::ceil(static_cast<double>(srcLenI) / ratio));
        if (outLen <= 0) return nullptr;
        resampled.resize(static_cast<size_t>(outLen), 0.0f);

        if (std::abs(ratio - 1.0f) < 1.0e-4f)
        {
            std::copy_n(srcBuf.getReadPointer(0),
                        std::min(srcLenI, outLen),
                        resampled.data());
        }
        else
        {
            juce::LagrangeInterpolator interp;
            interp.process(static_cast<double>(ratio),
                           srcBuf.getReadPointer(0),
                           resampled.data(),
                           outLen);
        }

        // Hard cap at 200 ms expressed at the target SR (the resample math
        // above can leave us a sample or two over the limit on odd ratios).
        const int maxOut = static_cast<int>(
            std::round((kMaxMs * 0.001) * targetSampleRate));
        const int finalLen = std::min(outLen, maxOut);
        if (finalLen <= 0) return nullptr;

        // Bake the fade. Linear 1.0 → 0.0 across the whole clipped length.
        // Anything past the audible boundary (~20 ms minimum implied) is
        // baked in too — voices just play through to silence and stop.
        for (int i = 0; i < finalLen; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(finalLen);
            resampled[static_cast<size_t>(i)] *= (1.0f - t);
        }

        auto out = std::make_shared<juce::AudioBuffer<float>>(1, finalLen);
        std::copy_n(resampled.data(), finalLen, out->getWritePointer(0));
        return out;
    }
};

} // namespace bombo
