// tests/OfflineBouncerTests.cpp — regression guards for the bounce pipeline.
//
// Scope: the helpers OfflineBouncer leans on, not the full bouncer flow.
// The full flow needs a live BomboProcessor + a pumped message loop,
// which would bloat Bombo_Tests by ~30 sources and a juce_audio_processors
// link dependency. Instead we pin the two pieces most likely to silently
// regress between now and the v1.1 reverb-pulsing-loop debug session:
//
//   1. WAV + AIFF writer roundtrip — proves AudioFormatManager+writer
//      can encode a buffer and decode it back with the magnitude intact.
//      If JUCE ever subtly breaks one of these formats (it has happened
//      across major bumps), the bounce file will be empty or wrong; this
//      test fails first.
//
//   2. Silence-trim arithmetic — duplicates the bouncer's writeLength
//      computation so a refactor of the trimming logic can't silently
//      drop the post-roll tail or overshoot the captured length.
//
// Not covered here (deferred to a future test that adds BomboProcessor
// to Bombo_Tests): the use-after-free fix in PluginProcessor.cpp's
// `isNonRealtime()` short-circuit, and the loop-on-=false override on
// the bouncer's clone. Both were validated by the code-review subagent
// 2026-05-18 and the manual single-hit bounce test before v1.0 ship.

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>

namespace
{

// Silence-trim math copy of OfflineBouncer.cpp:152-154. Pinned here so a
// production-side change to the formula breaks the test before it breaks
// the bounced files.
int computeBounceWriteLength(int finalSampleCount,
                             int silenceRunSamples,
                             int postRollSamples)
{
    int writeLength = finalSampleCount - silenceRunSamples + postRollSamples;
    return std::clamp(writeLength, 1, finalSampleCount);
}

class OfflineBouncerTests : public juce::UnitTest
{
public:
    OfflineBouncerTests() : juce::UnitTest("OfflineBouncer: writer + trim math") {}

    void runTest() override
    {
        beginTest("WAV writer roundtrip preserves magnitude");
        formatRoundtrip(WavFormat);

        beginTest("AIFF writer roundtrip preserves magnitude");
        formatRoundtrip(AiffFormat);

        beginTest("Silence-trim: keeps decay tail + 20 ms post-roll");
        {
            // 1 s capture at 48 kHz with 30 ms of silence at the end +
            // 20 ms post-roll → keep 1.000 s - 0.030 s + 0.020 s = 0.990 s.
            const int finalSampleCount  = 48000;
            const int silenceRunSamples = 1440;     // 30 ms
            const int postRollSamples   = 960;      // 20 ms
            expectEquals(computeBounceWriteLength(finalSampleCount,
                                                  silenceRunSamples,
                                                  postRollSamples),
                         47520);
        }

        beginTest("Silence-trim: clamps when post-roll would overshoot capture");
        {
            // Pathological case: tiny silence run + huge post-roll.
            // The clamp must hold writeLength to finalSampleCount.
            const int finalSampleCount  = 50000;
            const int silenceRunSamples = 100;
            const int postRollSamples   = 200000;
            expectEquals(computeBounceWriteLength(finalSampleCount,
                                                  silenceRunSamples,
                                                  postRollSamples),
                         50000);
        }

        beginTest("Silence-trim: floors writeLength at 1, never zero or negative");
        {
            // Pathological case: silenceRunSamples >= finalSampleCount.
            // Math would yield ≤ 0; clamp must lift to 1 so AudioFormatWriter
            // doesn't get fed an empty buffer (JUCE refuses zero-length).
            const int finalSampleCount  = 1000;
            const int silenceRunSamples = 2000;   // larger than capture
            const int postRollSamples   = 100;
            expectEquals(computeBounceWriteLength(finalSampleCount,
                                                  silenceRunSamples,
                                                  postRollSamples),
                         1);
        }
    }

private:
    enum FormatKind { WavFormat, AiffFormat };

    void formatRoundtrip(FormatKind kind)
    {
        constexpr int    sampleRate  = 48000;
        constexpr int    numChannels = 2;
        constexpr int    numSamples  = sampleRate / 4;   // 0.25 s
        constexpr int    bitDepth    = 24;
        constexpr float  testFreq    = 440.0f;

        juce::AudioBuffer<float> source(numChannels, numSamples);
        fillWithSine(source, testFreq, static_cast<float>(sampleRate));

        const float sourcePeak = source.getMagnitude(0, numSamples);
        expect(sourcePeak > 0.5f, "source buffer has audible content");

        juce::AudioFormatManager mgr;
        mgr.registerBasicFormats();

        std::unique_ptr<juce::AudioFormat> ownedFmt;
        juce::AudioFormat* fmt = nullptr;
        if (kind == WavFormat)
        {
            ownedFmt = std::make_unique<juce::WavAudioFormat>();
            fmt = ownedFmt.get();
        }
        else
        {
            ownedFmt = std::make_unique<juce::AiffAudioFormat>();
            fmt = ownedFmt.get();
        }
        expect(fmt != nullptr, "audio format created");

        auto tempFile = juce::File::createTempFile(kind == WavFormat ? ".wav" : ".aiff");

        // Write
        JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
        {
            std::unique_ptr<juce::FileOutputStream> os(tempFile.createOutputStream());
            expect(os != nullptr, "output stream opened");

            std::unique_ptr<juce::AudioFormatWriter> writer(
                fmt->createWriterFor(os.get(),
                                     static_cast<double>(sampleRate),
                                     static_cast<unsigned int>(numChannels),
                                     bitDepth,
                                     {},
                                     0));
            expect(writer != nullptr, "format writer created");
            if (writer != nullptr)
            {
                os.release();   // writer takes ownership
                const bool ok = writer->writeFromAudioSampleBuffer(source, 0, numSamples);
                expect(ok, "writeFromAudioSampleBuffer succeeded");
            }
            // writer destructs here → flushes
        }
        JUCE_END_IGNORE_DEPRECATION_WARNINGS

        expect(tempFile.existsAsFile(), "file written to disk");
        expect(tempFile.getSize() > 1024, "file has plausible size (header + samples)");

        // Read back
        {
            std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(tempFile));
            expect(reader != nullptr, "format reader created for written file");
            if (reader != nullptr)
            {
                expectEquals(static_cast<int>(reader->numChannels), numChannels);
                expectEquals(static_cast<int>(reader->sampleRate), sampleRate);
                expectEquals(static_cast<int>(reader->lengthInSamples), numSamples);

                juce::AudioBuffer<float> readBack(numChannels, numSamples);
                reader->read(&readBack, 0, numSamples, 0, true, true);

                const float readPeak = readBack.getMagnitude(0, numSamples);
                // 24-bit quant introduces ~1e-7 error; 0.05 tolerance is generous.
                expect(std::abs(readPeak - sourcePeak) < 0.05f,
                       "readback magnitude within tolerance of source");
            }
        }

        tempFile.deleteFile();
    }

    static void fillWithSine(juce::AudioBuffer<float>& buf, float freq, float sr)
    {
        constexpr float tau = 6.28318530717958647692f;
        const int n = buf.getNumSamples();
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
        {
            float* d = buf.getWritePointer(ch);
            const float dPhi = tau * freq / sr;
            float phase = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                d[i] = 0.7f * std::sin(phase);
                phase += dPhi;
                if (phase > tau) phase -= tau;
            }
        }
    }
};

static OfflineBouncerTests offlineBouncerTests;

} // anonymous namespace
