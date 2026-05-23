#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <memory>

namespace bombo
{

// One-shot sample layer (originally a "punch" snippet; now a full-length
// sample player). Loaded on the message thread, fed to voices at trigger
// time via a shared_ptr. The audio thread only reads — never allocates,
// never holds the load mutex.
//
// On load: mono-mix to 1ch, resample to the host sample rate, defensive
// hard-cap at kMaxMs (long enough for full-length 808 kicks + headroom),
// then bake a short linear fade only on the *last* few ms so the sample's
// natural amplitude is preserved while playback ending stays click-free.
// DEC + the runtime amp envelope handle musical shortening.
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
        return prepareReader(reader.get(), targetSampleRate);
    }

    // Variant that loads from an in-memory WAV/AIFF blob (BinaryData
    // factory samples). Same fade/cap/resample contract as loadFromFile.
    static std::shared_ptr<const juce::AudioBuffer<float>>
    loadFromMemory(const void* data, size_t numBytes, double targetSampleRate)
    {
        if (data == nullptr || numBytes == 0 || targetSampleRate <= 0.0)
            return nullptr;

        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        // JUCE 8 createReaderFor takes a unique_ptr<InputStream> and assumes
        // ownership of the stream regardless of whether the reader is
        // constructed successfully.
        std::unique_ptr<juce::InputStream> stream(
            new juce::MemoryInputStream(data, numBytes, /*keepCopy=*/ false));
        std::unique_ptr<juce::AudioFormatReader> reader(
            fm.createReaderFor(std::move(stream)));
        return prepareReader(reader.get(), targetSampleRate);
    }

private:
    // Shared post-reader pipeline: read leading slice, mono-mix, resample
    // to target SR, hard-cap at 200 ms, bake linear fade-to-silence.
    static std::shared_ptr<const juce::AudioBuffer<float>>
    prepareReader(juce::AudioFormatReader* reader, double targetSampleRate)
    {
        if (reader == nullptr || reader->lengthInSamples <= 0)
            return nullptr;

        // Defensive cap so a 60 s field recording can't bloat the per-voice
        // buffer. 10 s easily covers the longest 808 sub or tom hit.
        constexpr double kMaxMs    = 10000.0;
        constexpr double kEndFadeMs = 5.0;
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

        // Hard cap expressed at the target SR (the resample math above can
        // leave us a sample or two over the limit on odd ratios).
        const int maxOut = static_cast<int>(
            std::round((kMaxMs * 0.001) * targetSampleRate));
        const int finalLen = std::min(outLen, maxOut);
        if (finalLen <= 0) return nullptr;

        // Preserve the sample's natural amplitude — only fade the final
        // few ms so playback ending doesn't click on samples whose last
        // frame isn't already at zero. DEC + the live amp envelope are
        // the user-facing length controls; the loader stays neutral.
        const int endFadeLen = std::min(
            finalLen,
            std::max(1, static_cast<int>(
                std::round((kEndFadeMs * 0.001) * targetSampleRate))));
        const int fadeStart  = finalLen - endFadeLen;
        for (int i = 0; i < endFadeLen; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(endFadeLen);
            resampled[static_cast<size_t>(fadeStart + i)] *= (1.0f - t);
        }

        auto out = std::make_shared<juce::AudioBuffer<float>>(1, finalLen);
        std::copy_n(resampled.data(), finalLen, out->getWritePointer(0));
        return out;
    }
};

} // namespace bombo
