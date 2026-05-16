#pragma once

#include <array>
#include <juce_audio_processors/juce_audio_processors.h>

#include "Parameters.h"
#include "DSP/BombVoice.h"

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

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> masterGainSmoothed;

    std::array<bombo::BombVoice, kNumVoices> voices_;
    int activeVoice_ = 0;
    std::array<PendingHit, kPendingRingSize> pending_{};
    float currentSampleRate_ = 48000.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BomboProcessor)
};
