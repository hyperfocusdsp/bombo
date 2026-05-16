#pragma once

#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

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
    juce::AudioParameterFloat*  pDriftAmount = nullptr;

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
    juce::AudioParameterFloat*  pDuckRel = nullptr;
    juce::AudioParameterFloat*  pDuckDepth = nullptr;
    juce::AudioParameterBool*   pLimiterOn = nullptr;
    juce::AudioParameterFloat*  pLimiterAmount = nullptr;
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

    bombo::WaveBuffer waveBuffer_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboProcessor)
};
