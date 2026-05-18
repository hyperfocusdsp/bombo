#include "OfflineBouncer.h"

#include <juce_events/juce_events.h>
#include <cmath>

namespace bombo
{

namespace
{
juce::AudioFormat* makeFormatFor(OfflineBouncer::Format f, juce::AudioFormatManager& mgr)
{
    // findFormatForFileExtension returns a non-owning pointer into the
    // manager's registered formats. Manager owns the lifetime, so do not
    // delete.
    const juce::String ext = (f == OfflineBouncer::Format::Wav) ? ".wav" : ".aiff";
    return mgr.findFormatForFileExtension(ext);
}
} // namespace

std::unique_ptr<OfflineBouncer>
OfflineBouncer::startAsync(BomboProcessor& sourceProcessor,
                           juce::File destination,
                           Format format,
                           DoneCallback onDone)
{
    auto* raw = new OfflineBouncer(sourceProcessor, std::move(destination),
                                   format, std::move(onDone));
    std::unique_ptr<OfflineBouncer> owner(raw);
    owner->startThread(juce::Thread::Priority::normal);
    return owner;
}

OfflineBouncer::OfflineBouncer(BomboProcessor& sourceProcessor,
                               juce::File destination,
                               Format format,
                               DoneCallback onDone)
    : juce::Thread("Bombo Offline Bouncer"),
      destination_(std::move(destination)),
      format_(format),
      onDone_(std::move(onDone))
{
    // Snapshot on the message thread BEFORE the worker starts. Once we're
    // running we never touch sourceProcessor again — the worker only
    // operates on the clone.
    sourceProcessor.getStateInformation(stateBlob_);
    voiceBSnapshot_ = sourceProcessor.snapshotVoiceBSample();
}

OfflineBouncer::~OfflineBouncer()
{
    // 2 s grace before terminate — a 10 s-cap render of 512-sample blocks
    // at 48 kHz is < 50 ms of actual CPU, so this is generous.
    stopThread(2000);
}

void OfflineBouncer::run()
{
    // ── 1. Clone the processor ──────────────────────────────────────
    BomboProcessor clone;
    clone.setNonRealtime(true);
    if (stateBlob_.getSize() > 0)
        clone.setStateInformation(stateBlob_.getData(),
                                  static_cast<int>(stateBlob_.getSize()));
    clone.installVoiceBSampleSnapshot(std::move(voiceBSnapshot_));

    // Force stereo I/O on the clone (the live processor accepts mono+stereo
    // but we always bounce stereo so VOICE B's stereo content + the
    // stereo finalizer survive).
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add(juce::AudioChannelSet::stereo());
    if (clone.checkBusesLayoutSupported(layout))
        clone.setBusesLayout(layout);

    clone.prepareToPlay(kRenderSampleRate, kRenderBlockSize);

    // ── 2. Render loop ─────────────────────────────────────────────
    const int    maxSamples       = static_cast<int>(kMaxSeconds * kRenderSampleRate);
    const int    silenceWindowSamples = static_cast<int>(
        (kSilenceWindowMs / 1000.0) * kRenderSampleRate);
    const float  silenceThreshold = juce::Decibels::decibelsToGain(kSilenceThresholdDb);

    juce::AudioBuffer<float> growing(kNumChannels, maxSamples);
    growing.clear();

    juce::AudioBuffer<float> block(kNumChannels, kRenderBlockSize);
    juce::MidiBuffer         midi;

    // Fire one trigger BEFORE the first processBlock. The processor drains
    // keyboardTriggers_ atomically at the top of processBlock and spawns a
    // voice using the current APVTS-derived trigger params.
    clone.triggerFromKeyboard();

    int  cursor          = 0;
    int  silenceRunSamples = 0;
    bool gotSignalYet    = false;
    int  finalSampleCount = 0;

    while (! threadShouldExit() && cursor < maxSamples)
    {
        const int samplesThisBlock = juce::jmin(kRenderBlockSize, maxSamples - cursor);
        block.setSize(kNumChannels, samplesThisBlock, false, false, true);
        block.clear();
        midi.clear();

        clone.processBlock(block, midi);

        // Peak-detect in this block to update the silence run counter.
        float blockPeak = 0.0f;
        for (int ch = 0; ch < kNumChannels; ++ch)
            blockPeak = juce::jmax(blockPeak, block.getMagnitude(ch, 0, samplesThisBlock));

        if (blockPeak >= silenceThreshold)
        {
            gotSignalYet      = true;
            silenceRunSamples = 0;
        }
        else if (gotSignalYet)
        {
            silenceRunSamples += samplesThisBlock;
        }

        // Copy into the growing buffer.
        for (int ch = 0; ch < kNumChannels; ++ch)
            growing.copyFrom(ch, cursor, block, ch, 0, samplesThisBlock);
        cursor += samplesThisBlock;

        // Stop once we've had real signal AND the tail has been silent
        // long enough. Keep one full silence window in the file so the
        // trimmed end isn't truncated mid-fade.
        if (gotSignalYet && silenceRunSamples >= silenceWindowSamples)
        {
            finalSampleCount = cursor;
            break;
        }
    }

    if (finalSampleCount == 0)
        finalSampleCount = cursor;   // hit the cap or never got signal

    if (! gotSignalYet)
    {
        postResult(false, "Bounce produced no audio.");
        return;
    }

    // Trim trailing silence in place (clamp to the last non-silent block
    // plus a small post-roll so we don't cut a fade off mid-sample).
    // silenceRunSamples is what we've accumulated past the last loud
    // block, so writing finalSampleCount - silenceRunSamples + small
    // tail keeps natural decay.
    const int postRollSamples = static_cast<int>(0.020 * kRenderSampleRate);  // 20 ms
    int writeLength = finalSampleCount - silenceRunSamples + postRollSamples;
    writeLength = juce::jlimit(1, finalSampleCount, writeLength);

    // ── 3. Write to disk ──────────────────────────────────────────
    juce::AudioFormatManager mgr;
    mgr.registerBasicFormats();   // WAV + AIFF live here
    juce::AudioFormat* fmt = makeFormatFor(format_, mgr);
    if (fmt == nullptr)
    {
        postResult(false, "No format handler registered for "
                          + juce::String(format_ == Format::Wav ? "WAV" : "AIFF") + ".");
        return;
    }

    // Ensure parent dir exists (WSL with no DE often lacks ~/Music, and
    // the FileChooser will happily return paths in non-existent dirs).
    // createDirectory is recursive + no-op if it already exists.
    const auto parent = destination_.getParentDirectory();
    if (! parent.isDirectory())
    {
        const auto res = parent.createDirectory();
        if (res.failed())
        {
            postResult(false, "Could not create folder: "
                              + parent.getFullPathName()
                              + " (" + res.getErrorMessage() + ")");
            return;
        }
    }

    // Overwrite if exists — the FileChooser already prompted the user.
    if (destination_.existsAsFile())
        destination_.deleteFile();

    std::unique_ptr<juce::FileOutputStream> out(destination_.createOutputStream());
    if (out == nullptr || ! out->openedOk())
    {
        postResult(false, "Could not open file for writing: "
                          + destination_.getFullPathName());
        return;
    }

    std::unique_ptr<juce::AudioFormatWriter> writer(
        fmt->createWriterFor(out.get(),
                             kRenderSampleRate,
                             static_cast<unsigned int>(kNumChannels),
                             kBitDepth,
                             {},
                             0));
    if (writer == nullptr)
    {
        postResult(false, "Could not create writer.");
        return;
    }
    out.release();   // writer owns the stream now and will delete it

    const bool wrote = writer->writeFromAudioSampleBuffer(growing, 0, writeLength);
    writer.reset();  // flushes + closes the stream

    if (! wrote)
    {
        postResult(false, "Write failed mid-stream.");
        return;
    }

    postResult(true, destination_.getFileName());
}

void OfflineBouncer::postResult(bool success, juce::String message)
{
    if (! onDone_) return;
    auto cb = std::move(onDone_);
    juce::MessageManager::callAsync(
        [cb = std::move(cb), success, message = std::move(message)]() mutable
        { cb(success, std::move(message)); });
}

} // namespace bombo
