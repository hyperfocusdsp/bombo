#pragma once

#include <functional>
#include <memory>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include "../PluginProcessor.h"

namespace bombo
{

// One-shot offline bouncer. Clones the live processor's state, fires a
// single Note-On, processes blocks on a worker thread until the output
// is silent for kSilenceWindowMs (or kMaxSeconds elapses), trims the
// trailing silence, and writes the result as WAV or AIFF.
//
// Why a clone? Calling processBlock on the live processor would race the
// host's audio thread. A fresh BomboProcessor restored from
// getStateInformation produces identical sound *except* for the VOICE B
// sample buffer (loaded async via file I/O), so we hand the loaded
// shared_ptr across as a snapshot. The result matches what the user
// hears bit-exact at the snapshot moment.
//
// THREADING: construct + start on the message thread. onDone fires back
// on the message thread (via MessageManager::callAsync). Safe to delete
// from the message thread at any time — Thread::stopThread is called
// from the dtor.
class OfflineBouncer : public juce::Thread
{
public:
    enum class Format { Wav, Aiff };

    using DoneCallback = std::function<void(bool success, juce::String message)>;

    // sourceProcessor must remain alive until construction returns; we
    // only read its state (via getStateInformation + snapshotVoiceBSample)
    // on the calling thread, then operate on the clone in the worker.
    static std::unique_ptr<OfflineBouncer> startAsync(BomboProcessor& sourceProcessor,
                                                      juce::File destination,
                                                      Format format,
                                                      DoneCallback onDone);

    ~OfflineBouncer() override;

    // juce::Thread
    void run() override;

private:
    OfflineBouncer(BomboProcessor& sourceProcessor,
                   juce::File destination,
                   Format format,
                   DoneCallback onDone);

    void postResult(bool success, juce::String message);

    static constexpr double kRenderSampleRate    = 48000.0;
    static constexpr int    kRenderBlockSize     = 512;
    static constexpr int    kNumChannels         = 2;
    // -50 dB is below the audible-content threshold in any mixing context
    // but high enough that long reverb tails (which can crawl from -40 dB
    // to -60 dB over several seconds) trip the silence detector cleanly,
    // instead of holding the bounce open until the 10 s cap. The dynamic
    // range of the rendered file is still 50 dB — plenty for a kick.
    static constexpr float  kSilenceThresholdDb  = -50.0f;
    static constexpr int    kSilenceWindowMs     = 50;
    static constexpr double kMaxSeconds          = 10.0;
    static constexpr int    kBitDepth            = 24;

    // State snapshot taken on the message thread in the ctor.
    juce::MemoryBlock stateBlob_;
    // VoiceB shared_ptr handed off — stays alive in the snapshot struct
    // until the cloned processor installs it.
    BomboProcessor::VoiceBSnapshot voiceBSnapshot_;
    juce::File   destination_;
    Format       format_;
    DoneCallback onDone_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OfflineBouncer)
};

} // namespace bombo
