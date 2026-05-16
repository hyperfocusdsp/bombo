#pragma once

#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "Parameters.h"
#include "DSP/BombVoice.h"
#include "DSP/RumbleChain.h"
#include "GUI/WaveBuffer.h"

class BomboProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kNumVoices = 4;
    static constexpr int kPendingRingSize = 8;

    BomboProcessor();
    ~BomboProcessor() override = default;

    // AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Editor-side trigger bridge (Space / T keys, on-screen buttons).
    // Atomic so the audio thread can swap(0) without a lock. Capped at
    // kNumVoices per buffer in processBlock.
    void triggerFromKeyboard() noexcept { keyboardTriggers_.fetch_add(1, std::memory_order_relaxed); }
    // T-style one-shot — fires a kick AND schedules a deferred tail kill
    // at the next would-be beat, so we get "hit → one tail → silence".
    void triggerOneShot() noexcept
    {
        keyboardTriggers_.fetch_add(1, std::memory_order_relaxed);
        oneShotTriggers_ .fetch_add(1, std::memory_order_relaxed);
    }
    // Toggle loop on/off via the AudioParameterBool (so DAW automation +
    // host UI stay in sync). Loop-off triggers the deferred tail-kill via
    // the existing edge-detect in processBlock.
    void toggleLoop()
    {
        if (pLoopOn == nullptr) return;
        const bool cur = pLoopOn->get();
        pLoopOn->beginChangeGesture();
        pLoopOn->setValueNotifyingHost(cur ? 0.0f : 1.0f);
        pLoopOn->endChangeGesture();
    }

    // VOICE B sample slot. UI calls these on the message thread; audio
    // thread reads voiceBSample_ at trigger time only (allocator-free
    // shared_ptr copy). A juce::SpinLock guards the swap.
    //
    // Folder-browse flow: setVoiceBSampleFolder() scans the parent folder
    // of the picked file, populates voiceBFolderSamples_, finds the index
    // of that file in the sorted list, and loads it. loadVoiceBSampleByIndex
    // swaps to a different file from the cached list without rescanning.
    void loadVoiceBSample(const juce::File& file);   // single-file load (clears folder list)
    void setVoiceBSampleFolder(const juce::File& filePicked);
    void loadVoiceBSampleByIndex(int idx);
    void clearVoiceBSample();
    juce::String voiceBSamplePath() const;

    // DICE: randomize every musically-meaningful param in one shot. Ranges
    // are deliberately bounded inside each param's domain (no full-scale
    // chaos) so the result still sounds like a kick. Excludes transport
    // (loop, BPM, mutes), master out, voice balance, sample slot, limiter.
    void randomizeBombo();
    juce::StringArray voiceBSampleNames() const;     // display names (no extension)
    int voiceBSampleIndex() const;                   // current index in the folder list, or -1

    // 0.0f if the host isn't providing a BPM (standalone, or DAW that hides
    // it). UI polls this on a timer to drive the BPM display readout.
    float hostBpm() const noexcept { return hostBpmAtomic_.load(std::memory_order_relaxed); }

    // Post-master scope feed. Editor pulls from this at 30 Hz to draw the
    // hero scope strip. Producer side is RT-safe.
    const bombo::WaveBuffer& waveBuffer() const noexcept { return waveBuffer_; }

    juce::AudioProcessorValueTreeState apvts;

private:
    struct PendingHit
    {
        int samplesUntil = 0;
        bool live = false;
    };

    void cacheParameterPointers();
    bombo::VoiceTrigger buildTriggerFromParams() const noexcept;
    void pushPending(int samplesUntil) noexcept;
    int  tickPending() noexcept;  // returns count of hits fired this sample
    void stealVoice() noexcept;
    bool anyVoiceActive() const noexcept;

    juce::UndoManager undoManager;

    // Cached raw param pointers. Phase 1 — kick voice path only.
    juce::AudioParameterFloat*  pMasterOut = nullptr;
    juce::AudioParameterChoice* pWaveform = nullptr;
    juce::AudioParameterFloat*  pPitchStart = nullptr;
    juce::AudioParameterFloat*  pPitchEnd = nullptr;
    juce::AudioParameterFloat*  pPitchDecay = nullptr;
    juce::AudioParameterFloat*  pPitchCurve = nullptr;
    juce::AudioParameterFloat*  pMidPitchStart = nullptr;
    juce::AudioParameterFloat*  pMidPitchEnd = nullptr;
    juce::AudioParameterFloat*  pMidDecay = nullptr;
    juce::AudioParameterFloat*  pMidLevel = nullptr;
    juce::AudioParameterFloat*  pAmpAttack = nullptr;
    juce::AudioParameterFloat*  pAmpDecay = nullptr;
    juce::AudioParameterFloat*  pClickAmount = nullptr;
    juce::AudioParameterFloat*  pClickCenter = nullptr;
    juce::AudioParameterFloat*  pNoiseAmount = nullptr;
    juce::AudioParameterFloat*  pNoiseColor = nullptr;
    juce::AudioParameterFloat*  pDriveAmount = nullptr;
    juce::AudioParameterChoice* pDriveMode = nullptr;
    juce::AudioParameterFloat*  pVoiceBalance = nullptr;

    // Rumble FX chain params (Phase 2).
    juce::AudioParameterFloat*  pFxDriveAmount = nullptr;
    juce::AudioParameterChoice* pFxDriveMode = nullptr;
    juce::AudioParameterFloat*  pFxDriveMix = nullptr;
    juce::AudioParameterFloat*  pFilterHp = nullptr;
    juce::AudioParameterFloat*  pFilterHpQ = nullptr;
    juce::AudioParameterFloat*  pFilterLp = nullptr;
    juce::AudioParameterFloat*  pFilterLpQ = nullptr;
    juce::AudioParameterFloat*  pFilterColor = nullptr;
    juce::AudioParameterFloat*  pDelayTime = nullptr;
    juce::AudioParameterFloat*  pDelayFeedback = nullptr;
    juce::AudioParameterFloat*  pDelayDrift = nullptr;
    juce::AudioParameterFloat*  pDelayMorph = nullptr;
    juce::AudioParameterFloat*  pDelayMix = nullptr;
    juce::AudioParameterFloat*  pReverbSize = nullptr;
    juce::AudioParameterFloat*  pReverbDecay = nullptr;
    juce::AudioParameterFloat*  pReverbDamp = nullptr;
    juce::AudioParameterFloat*  pReverbDiffusion = nullptr;
    juce::AudioParameterFloat*  pReverbPredelay = nullptr;
    juce::AudioParameterFloat*  pReverbMix = nullptr;
    juce::AudioParameterFloat*  pDuckAtk = nullptr;
    juce::AudioParameterFloat*  pDuckHold = nullptr;
    juce::AudioParameterFloat*  pDuckRel = nullptr;
    juce::AudioParameterFloat*  pDuckDepth = nullptr;
    juce::AudioParameterBool*   pLimiterOn = nullptr;
    juce::AudioParameterFloat*  pLimiterAmount = nullptr;
    juce::AudioParameterBool*   pLoopOn = nullptr;
    juce::AudioParameterFloat*  pBpm = nullptr;
    juce::AudioParameterBool*   pVoiceAMute = nullptr;
    juce::AudioParameterBool*   pVoiceBMute = nullptr;
    juce::AudioParameterBool*   pDriveMute  = nullptr;
    juce::AudioParameterBool*   pDelayMute  = nullptr;
    juce::AudioParameterBool*   pReverbMute = nullptr;
    juce::AudioParameterBool*   pFilterMute = nullptr;
    juce::AudioParameterBool*   pDuckMute   = nullptr;

    bombo::ChainParams buildChainParamsFromApvts() const noexcept;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> masterGainSmoothed;

    std::array<bombo::BombVoice, kNumVoices> voices_;
    int activeVoice_ = 0;
    std::array<PendingHit, kPendingRingSize> pending_{};
    float currentSampleRate_ = 48000.0f;

    bombo::RumbleChain chain_{ 48000.0f };
    bombo::ChainParams chainParams_{}; // Phase 1b: defaults only; APVTS wiring lands in Phase 2.

    std::atomic<int> keyboardTriggers_{0};
    std::atomic<int> oneShotTriggers_{0};   // T-style: fires + arms tail kill

    // Loop scheduler state. samplesUntilLoopFire_ is the per-buffer counter
    // for free-running mode (standalone, or DAW with transport stopped).
    // hostBpmAtomic_ exposes the host's reported BPM to the UI thread so
    // the header readout shows what's actually driving the rate.
    int   samplesUntilLoopFire_ = 0;
    bool  lastLoopOn_ = false;               // edge-detect for deferred tail kill
    // When loop transitions on→off we schedule a tail kill at the spot where
    // the NEXT trigger would have fired (one beat later). Keeps the rhythmic
    // pattern of "kick - tail - silence" instead of "kick - tail - kick - tail".
    // -1 = no pending kill, else samples-until-fire (decremented each block).
    int   pendingTailKillSamples_ = -1;
    std::atomic<float> hostBpmAtomic_{0.0f}; // 0 = no host BPM (standalone)

    // VOICE B sample state. SpinLock-protected since UI thread swaps the
    // shared_ptr at file-load time while the audio thread may be copying it
    // into a fresh VoiceTrigger at note-on. Audio thread uses try_enter so
    // it never blocks; on contention it falls back to the previously held
    // ptr (cached locally), so a load mid-trigger drops at most one hit's
    // sample layer — never glitches or stalls audio.
    mutable juce::SpinLock voiceBSampleLock_;
    std::shared_ptr<const juce::AudioBuffer<float>> voiceBSample_;
    juce::String voiceBSamplePath_;
    juce::String voiceBFolderPath_;
    std::vector<juce::File> voiceBFolderSamples_;
    int voiceBFolderIndex_ = -1;
    // Pending-restore queue: setStateInformation may run before
    // prepareToPlay (currentSampleRate_ is still 0), so SampleSlot::loadFromFile
    // would early-return. We stash the path here and replay it from
    // prepareToPlay once the SR is known.
    juce::String pendingRestorePath_;
    bool pendingRestoreIsFolder_ = false;

    bombo::WaveBuffer waveBuffer_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboProcessor)
};
