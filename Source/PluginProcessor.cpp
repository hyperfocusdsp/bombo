#include "PluginProcessor.h"
#include "PluginEditor.h"

BomboProcessor::BomboProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, &undoManager, "BomboState", bombo::createParameterLayout())
{
    cacheParameterPointers();
}

void BomboProcessor::cacheParameterPointers()
{
    using namespace bombo::pid;
    pMasterOut     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(masterOut));
    pWaveform      = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(waveform));
    pPitchStart    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(pitchStart));
    pPitchEnd      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(pitchEnd));
    pPitchDecay    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(pitchDecay));
    pPitchCurve    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(pitchCurve));
    pMidPitchStart = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(midPitchStart));
    pMidPitchEnd   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(midPitchEnd));
    pMidDecay      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(midDecay));
    pMidLevel      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(midLevel));
    pAmpAttack     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(ampAttack));
    pAmpDecay      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(ampDecay));
    pClickAmount   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(clickAmount));
    pClickCenter   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(clickCenter));
    pNoiseAmount   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(noiseAmount));
    pNoiseColor    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(noiseColor));
    pDriveAmount   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(driveAmount));
    pDriveMode     = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(driveMode));
    pDriftAmount   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(driftAmount));
    jassert(pMasterOut != nullptr && pWaveform != nullptr && pDriveMode != nullptr);
}

bombo::VoiceTrigger BomboProcessor::buildTriggerFromParams() const noexcept
{
    bombo::VoiceTrigger t;
    t.waveform        = pWaveform->getIndex();
    t.pitchStartHz    = pPitchStart->get();
    t.pitchEndHz      = pPitchEnd->get();
    t.pitchEnvDecayMs = pPitchDecay->get();
    t.pitchCurve      = pPitchCurve->get();
    t.midPitchStartHz = pMidPitchStart->get();
    t.midPitchEndHz   = pMidPitchEnd->get();
    t.midDecayMs      = pMidDecay->get();
    t.midLevel        = pMidLevel->get();
    t.ampAttackMs     = pAmpAttack->get();
    t.ampDecayMs      = pAmpDecay->get();
    t.clickAmount     = pClickAmount->get();
    t.clickCenterHz   = pClickCenter->get();
    t.noiseAmount     = pNoiseAmount->get();
    t.noiseColor      = pNoiseColor->get();
    t.driveAmount     = pDriveAmount->get();
    t.driveMode       = pDriveMode->getIndex();
    t.driftAmount     = pDriftAmount->get();
    return t;
}

void BomboProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate_ = static_cast<float>(sampleRate);
    for (auto& v : voices_) v.setSampleRate(currentSampleRate_);
    for (auto& slot : pending_) { slot.live = false; slot.samplesUntil = 0; }
    activeVoice_ = 0;

    chain_.setSampleRate(currentSampleRate_);
    chain_.reset();

    // Phase-1b chain defaults — audible rumble without UI control yet.
    // APVTS wiring for these lands in Phase 2.
    chainParams_ = bombo::ChainParams{};
    chainParams_.delayMs = 380.0f;
    chainParams_.delayFeedback = 0.55f;
    chainParams_.delayDrift = 0.25f;
    chainParams_.delayMorph = 0.4f;
    chainParams_.delayMix = 0.25f;
    chainParams_.reverbSize = 0.55f;
    chainParams_.reverbDecay = 0.70f;
    chainParams_.reverbDamp = 0.45f;
    chainParams_.reverbDiffusion = 0.6f;
    chainParams_.reverbPredelayMs = 30.0f;
    chainParams_.reverbMix = 0.35f;
    chainParams_.duckAttackMs = 2.0f;
    chainParams_.duckReleaseMs = 220.0f;
    chainParams_.duckDepth = 0.6f;
    chain_.update(chainParams_);

    masterGainSmoothed.reset(sampleRate, 0.010);
    masterGainSmoothed.setCurrentAndTargetValue(
        juce::Decibels::decibelsToGain(pMasterOut->get()));
}

void BomboProcessor::releaseResources() {}

bool BomboProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void BomboProcessor::pushPending(int samplesUntil) noexcept
{
    for (auto& slot : pending_)
    {
        if (!slot.live)
        {
            slot.samplesUntil = samplesUntil;
            slot.live = true;
            return;
        }
    }
}

int BomboProcessor::tickPending() noexcept
{
    int count = 0;
    for (auto& slot : pending_)
    {
        if (!slot.live) continue;
        if (slot.samplesUntil == 0) { ++count; slot.live = false; }
        else                        { --slot.samplesUntil; }
    }
    return count;
}

void BomboProcessor::stealVoice() noexcept
{
    if (!voices_[activeVoice_].isActive()) return;
    voices_[activeVoice_].startFadeout(currentSampleRate_);
    activeVoice_ = (activeVoice_ + 1) % kNumVoices;
    if (voices_[activeVoice_].isActive())
        voices_[activeVoice_].startFadeout(currentSampleRate_);
}

bool BomboProcessor::anyVoiceActive() const noexcept
{
    for (const auto& v : voices_) if (v.isActive()) return true;
    return false;
}

void BomboProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Synth — clear output first; we'll write voices into it.
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    // Schedule MIDI NoteOns into the pending ring. Cap at NUM_VOICES per
    // buffer; drain the rest from the host's queue so they don't accumulate.
    const int bufSamples = numSamples > 0 ? numSamples - 1 : 0;
    int scheduled = 0;
    for (const auto meta : midi)
    {
        const auto& msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            if (scheduled >= kNumVoices) continue;
            int offset = meta.samplePosition;
            if (offset < 0) offset = 0;
            if (offset > bufSamples) offset = bufSamples;
            pushPending(offset);
            ++scheduled;
        }
    }

    // Snapshot params for any triggers this buffer fires. (Per-trigger
    // snapshot — the BombVoice locks these in for its lifetime.)
    const bombo::VoiceTrigger trig = buildTriggerFromParams();

    masterGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(pMasterOut->get()));

    auto* left  = numChannels > 0 ? buffer.getWritePointer(0) : nullptr;
    auto* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    chain_.update(chainParams_);

    for (int i = 0; i < numSamples; ++i)
    {
        const int nFired = tickPending();
        for (int k = 0; k < nFired; ++k)
        {
            stealVoice();
            voices_[activeVoice_].trigger(trig);
            chain_.killTail();
        }

        float dry = 0.0f;
        for (auto& v : voices_) dry += v.tick();

        const float wet = chain_.process(dry);
        const float g = masterGainSmoothed.getNextValue();
        const float out = wet * g;

        if (left)  left[i]  = out;
        if (right) right[i] = out;
    }
}

juce::AudioProcessorEditor* BomboProcessor::createEditor()
{
    return new BomboEditor(*this);
}

void BomboProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.state.createXml())
        copyXmlToBinary(*xml, destData);
}

void BomboProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (tree.isValid()) apvts.replaceState(tree);
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BomboProcessor();
}
