#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Parameters.h"        // createParameterLayout (heavy include, .cpp-only)
#include "DSP/SampleSlot.h"
#include <BinaryData.h>        // factory WAV bank baked in by juce_add_binary_data
#include <algorithm>
#include <numeric>

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
    pSubHpf        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(subHpf));
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
    pDriveBias     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(driveBias));
    pVoiceBalance  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(voiceBalance));

    pFxDriveAmount   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(fxDriveAmount));
    pFxDriveMode     = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(fxDriveMode));
    pFxDriveMix      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(fxDriveMix));
    pFilterHp        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(filterHp));
    pFilterHpQ       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(filterHpQ));
    pFilterLp        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(filterLp));
    pFilterLpQ       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(filterLpQ));
    pFilterColor     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(filterColor));
    pFilterTeeth     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(filterTeeth));
    pDelayTime       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(delayTime));
    pDelayFeedback   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(delayFeedback));
    pDelayTimeMode   = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(delayTimeMode));
    pDelayMorph      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(delayMorph));
    pDelaySmear    = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(delaySmear));
    pDelayMix        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(delayMix));
    pReverbType      = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(reverbType));
    pReverbSize      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(reverbSize));
    pReverbDecay     = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(reverbDecay));
    pReverbDamp      = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(reverbDamp));
    pReverbDiffusion = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(reverbDiffusion));
    pReverbPredelay  = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(reverbPredelay));
    pReverbMix       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(reverbMix));
    pDuckAtk         = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(duckAtk));
    pDuckHold        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(duckHold));
    pDuckRel         = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(duckRel));
    pDuckDepth       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(duckDepth));
    pDuckShape       = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(duckShape));
    pDuckGrowl        = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(duckGrowl));
    pLimiterOn       = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(limiterOn));
    pLimiterAmount   = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(limiterAmount));
    pTailKillOn      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(tailKillOn));
    pLoopOn          = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(loopOn));
    pBpm             = dynamic_cast<juce::AudioParameterFloat*> (apvts.getParameter(bpm));
    pVoiceAMute      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(voiceAMute));
    pVoiceBMute      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(voiceBMute));
    pVoiceBSynthOn   = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(voiceBSynthOn));
    pDriveMute       = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(driveMute));
    pDelayMute       = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(delayMute));
    pReverbMute      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(reverbMute));
    pFilterMute      = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(filterMute));
    pDuckMute        = dynamic_cast<juce::AudioParameterBool*>  (apvts.getParameter(duckMute));

    jassert(pMasterOut != nullptr && pWaveform != nullptr && pDriveMode != nullptr);
    jassert(pLimiterOn != nullptr && pReverbMix != nullptr);
    jassert(pDriveMute != nullptr && pDelayMute != nullptr);
}

bombo::ChainParams BomboProcessor::buildChainParamsFromApvts() const noexcept
{
    bombo::ChainParams p;
    p.driveAmount     = pFxDriveAmount->get();
    p.driveMode       = pFxDriveMode->getIndex();
    p.driveMix        = pFxDriveMix->get();
    p.hpHz            = pFilterHp->get();
    p.hpQ             = pFilterHpQ->get();
    p.lpHz            = pFilterLp->get();
    p.lpQ             = pFilterLpQ->get();
    p.filterColor     = pFilterColor->get();
    p.filterTeeth     = pFilterTeeth->get();
    p.delayMs         = pDelayTime->get();
    p.delayFeedback   = pDelayFeedback->get();
    p.delayTimeMode   = pDelayTimeMode->getIndex();
    // RumbleChain computes the effective ms from (mode, hostBpm, delayMs).
    // hostBpm 0 means "no host BPM" → fall back to the BPM param.
    p.hostBpm         = hostBpmAtomic_.load(std::memory_order_relaxed);
    if (p.hostBpm < 1.0f && pBpm != nullptr) p.hostBpm = pBpm->get();
    p.delayMorph      = pDelayMorph->get();
    p.delaySmear    = pDelaySmear->get();
    p.delayMix        = pDelayMix->get();
    p.reverbType      = pReverbType != nullptr ? pReverbType->getIndex() : 2;
    p.reverbSize      = pReverbSize->get();
    p.reverbDecay     = pReverbDecay->get();
    p.reverbDamp      = pReverbDamp->get();
    p.reverbDiffusion = pReverbDiffusion->get();
    p.reverbPredelayMs = pReverbPredelay->get();
    p.reverbMix       = pReverbMix->get();
    p.duckAttackMs    = pDuckAtk->get();
    p.duckHoldMs      = pDuckHold->get();
    p.duckReleaseMs   = pDuckRel->get();
    p.duckDepth       = pDuckDepth->get();
    p.duckShape       = pDuckShape->get();
    p.duckGrowl        = pDuckGrowl->get();
    p.limiterOn       = pLimiterOn->get();
    p.limiterAmount   = pLimiterAmount->get();
    p.tailKillOn      = (pTailKillOn != nullptr) && pTailKillOn->get();
    p.driveMute       = pDriveMute->get();
    p.delayMute       = pDelayMute->get();
    p.reverbMute      = pReverbMute->get();
    p.filterMute      = pFilterMute->get();
    p.duckMute        = pDuckMute->get();
    return p;
}

bombo::VoiceTrigger BomboProcessor::buildTriggerFromParams() const noexcept
{
    bombo::VoiceTrigger t;
    t.waveform        = pWaveform->getIndex();
    t.pitchStartHz    = pPitchStart->get();
    t.pitchEndHz      = pPitchEnd->get();
    t.pitchEnvDecayMs = pPitchDecay->get();
    t.pitchCurve      = pPitchCurve->get();
    t.subHpfHz        = pSubHpf->get();
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
    t.driveBias       = pDriveBias->get();
    t.voiceAMute      = pVoiceAMute->get();
    t.voiceBMute      = pVoiceBMute->get();
    t.voiceBSynthOn   = pVoiceBSynthOn->get();
    t.driveMute       = pDriveMute->get();
    t.voiceBalance    = pVoiceBalance->get();
    // Copy the current sample shared_ptr under spin-lock. shared_ptr copy is
    // an atomic refcount bump — allocator-free on libstdc++. try_enter so
    // the audio thread never blocks if the UI is mid-swap.
    {
        juce::SpinLock::ScopedTryLockType lock(voiceBSampleLock_);
        if (lock.isLocked()) t.sampleBuf = voiceBSample_;
    }
    return t;
}

void BomboProcessor::prepareToPlay(double sampleRate, int /*samplesPerBlock*/)
{
    currentSampleRate_ = static_cast<float>(sampleRate);
    voiceMgr_.prepare(currentSampleRate_);
    samplesUntilLoopFire_ = 0;

    // Replay any sample-restore that was stashed by setStateInformation
    // before the SR was known. Loading off the audio thread via callAsync.
    juce::String pendingPath;
    bool pendingIsFolder = false;
    bool sampleAlreadyLoaded = false;
    {
        juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
        pendingPath = pendingRestorePath_;
        pendingIsFolder = pendingRestoreIsFolder_;
        pendingRestorePath_.clear();
        sampleAlreadyLoaded = (voiceBSample_ != nullptr);
    }
    if (pendingPath.isNotEmpty())
    {
        const bool isFactoryRestore = pendingPath.startsWith("<factory>");
        auto restore = [this, pendingPath, pendingIsFolder, isFactoryRestore]()
        {
            if (isFactoryRestore)
            {
                this->loadFactorySamples();
                const int savedIdx = pendingPath.fromFirstOccurrenceOf(":", false, false)
                                                .getIntValue();
                if (savedIdx > 0) this->loadVoiceBSampleByIndex(savedIdx);
            }
            else if (pendingIsFolder) this->setVoiceBSampleFolder(juce::File(pendingPath));
            else                      this->loadVoiceBSample      (juce::File(pendingPath));
        };
        // Offline render (bouncer clone) destructs as soon as run() returns;
        // dispatching to the message thread would invoke `this` after free.
        if (isNonRealtime()) restore();
        else                 juce::MessageManager::callAsync(std::move(restore));
    }
    else if (! sampleAlreadyLoaded)
    {
        // Fresh install / blank state — bootstrap the factory bank so VOICE B
        // has something playable out of the box.
        if (isNonRealtime()) loadFactorySamples();
        else                 juce::MessageManager::callAsync([this]() { this->loadFactorySamples(); });
    }

    chain_.setSampleRate(currentSampleRate_);
    chain_.reset();
    stereoFin_.prepare(currentSampleRate_);
    stereoFin_.reset();
    chainParams_ = buildChainParamsFromApvts();
    chain_.update(chainParams_);

    // Loop-cache sized for one beat at the slowest supported BPM (60).
    // Always stereo; gets resized once at prepare and not on the audio
    // thread.
    const int maxBeatSamples = static_cast<int>(currentSampleRate_ * (60.0f / 60.0f));
    loopCache_.buf.setSize(2, maxBeatSamples, false, true, true);
    loopCache_.buf.clear();
    loopCache_.capturing   = false;
    loopCache_.valid       = false;
    loopCache_.writePos    = 0;
    loopCache_.readPos     = 0;
    loopCache_.beatSamples = 0;

    waveBuffer_.clear();

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

void BomboProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // Drain preset-apply tail-reset request (set on the message thread
    // by requestPresetTailReset). Runs exactly once per apply: chain_
    // .killTail() schedules the click-free fade so any inherited reverb/
    // delay state from the previous preset is gone before the next
    // trigger lands. Cheap when no apply is pending (single relaxed
    // atomic load + the strong exchange).
    if (presetTailResetPending_.exchange(false, std::memory_order_acquire))
        chain_.killTail();

    // Synth — clear output first; we'll write voices into it.
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    // Schedule MIDI NoteOns into the pending ring. Cap at NUM_VOICES per
    // buffer; drain the rest from the host's queue so they don't accumulate.
    const int bufSamples = numSamples > 0 ? numSamples - 1 : 0;
    int scheduled = 0;

    // Drain editor keyboard triggers first (Space / T). They land at
    // sample 0 of the buffer since the editor doesn't tell us when each
    // key was pressed within the block.
    int kbCount = keyboardTriggers_.exchange(0, std::memory_order_relaxed);
    if (kbCount > bombo::VoiceManager::kNumVoices) kbCount = bombo::VoiceManager::kNumVoices;
    for (int i = 0; i < kbCount; ++i)
    {
        voiceMgr_.pushPending(0);
        ++scheduled;
    }

    for (const auto meta : midi)
    {
        const auto& msg = meta.getMessage();
        if (msg.isNoteOn())
        {
            if (scheduled >= bombo::VoiceManager::kNumVoices) continue;
            int offset = meta.samplePosition;
            if (offset < 0) offset = 0;
            if (offset > bufSamples) offset = bufSamples;
            voiceMgr_.pushPending(offset);
            ++scheduled;
        }
    }

    // ── Transport: pull host BPM + play state, expose to UI ─────────
    float effectiveBpm = pBpm->get();
    bool  hostPlaying = false;
    double hostPpqAtBlockStart = 0.0;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (pos->getBpm().hasValue() && *pos->getBpm() > 0.0)
                effectiveBpm = static_cast<float>(*pos->getBpm());
            hostPlaying = pos->getIsPlaying();
            if (pos->getPpqPosition().hasValue())
                hostPpqAtBlockStart = *pos->getPpqPosition();
        }
    }
    // 0 when host doesn't report BPM (standalone), used by UI for the
    // "host-driven, read-only" display state.
    {
        const float hostReported = (effectiveBpm == pBpm->get()) ? 0.0f : effectiveBpm;
        hostBpmAtomic_.store(hostReported, std::memory_order_relaxed);
    }

    // ── Loop scheduler ──────────────────────────────────────────────
    // Loop on: schedule extra triggers at the BPM rate. When the host is
    // playing, triggers snap to the integer PPQ grid (so they ride the
    // host's beat phase); otherwise we free-run from samplesUntilLoopFire_.
    const bool loopNow = pLoopOn->get();
    // Edge-detect: catch the moment the user turns loop OFF so we can
    // schedule an immediate tail-kill (within ~1 block) rather than
    // waiting a full beat for the universal deferred timer. With long
    // delay times + high feedback, waiting a full beat lets the natural
    // delay decay ring out audibly before the kill fires.
    const bool loopJustTurnedOff = (lastLoopOn_ && ! loopNow);
    lastLoopOn_ = loopNow;

    if (loopNow && effectiveBpm > 0.0f && currentSampleRate_ > 0.0f)
    {
        const double samplesPerBeat =
            (60.0 / static_cast<double>(effectiveBpm))
            * static_cast<double>(currentSampleRate_);

        if (hostPlaying && samplesPerBeat > 1.0)
        {
            // Fire on every integer ppq inside this buffer.
            const double ppqStart = hostPpqAtBlockStart;
            const double ppqEnd   = ppqStart + static_cast<double>(numSamples) / samplesPerBeat;
            const int firstBeat = static_cast<int>(std::ceil(ppqStart - 1.0e-6));
            const int lastBeat  = static_cast<int>(std::floor(ppqEnd - 1.0e-6));
            for (int b = firstBeat; b <= lastBeat; ++b)
            {
                if (scheduled >= bombo::VoiceManager::kNumVoices) break;
                const double sampleOfBeat = (static_cast<double>(b) - ppqStart) * samplesPerBeat;
                int offset = static_cast<int>(std::round(sampleOfBeat));
                if (offset < 0) offset = 0;
                if (offset > bufSamples) offset = bufSamples;
                voiceMgr_.pushPending(offset);
                ++scheduled;
            }
            // Free-running counter resets so a transport-stop transitions
            // cleanly to the standalone rate.
            samplesUntilLoopFire_ = 0;
        }
        else
        {
            // Free-running. Decrement the counter, fire when it hits zero,
            // then reset to the beat length.
            int cursor = 0;
            while (cursor < numSamples && scheduled < bombo::VoiceManager::kNumVoices)
            {
                if (samplesUntilLoopFire_ <= 0)
                {
                    voiceMgr_.pushPending(cursor);
                    ++scheduled;
                    samplesUntilLoopFire_ = static_cast<int>(std::round(samplesPerBeat));
                }
                const int step = std::min(samplesUntilLoopFire_,
                                          numSamples - cursor);
                samplesUntilLoopFire_ -= step;
                cursor += step;
            }
        }
    }
    else
    {
        // Loop off: reset the free-running counter so it fires immediately
        // when the user re-enables (no skipped first beat).
        samplesUntilLoopFire_ = 0;
    }

    // Universal deferred tail kill — fires one beat after the LAST trigger
    // from ANY source. Must run AFTER the loop scheduler so it sees all
    // trigger pushes from this block (keyboard, MIDI, loop). Triggers
    // re-arm to a full beat from now; the last in a stream lets the
    // counter tick down and fires `chain_.killTail()` once.
    //
    // Subsumes the old loop-off and T-only kill paths — both categories
    // route through `scheduled++` so they're handled uniformly here.
    oneShotTriggers_.exchange(0, std::memory_order_relaxed); // legacy drain

    // The whole deferred tail-kill machinery is opt-in via the
    // pTailKillOn toggle (default ON). When OFF, no killing happens —
    // long delay + reverb tails ring naturally past the last trig.
    const bool tailKillEnabled = (pTailKillOn != nullptr) && pTailKillOn->get();

    if (tailKillEnabled && scheduled > 0 && effectiveBpm > 0.0f && currentSampleRate_ > 0.0f)
    {
        const double spb =
            (60.0 / static_cast<double>(effectiveBpm))
            * static_cast<double>(currentSampleRate_);
        pendingTailKillSamples_ = static_cast<int>(std::round(spb));
    }
    // Loop just turned off — arm kill for one beat from now so the
    // current kick's tail plays through once before silence. Only arms
    // if no kill is already pending (the universal timer above already
    // scheduled one if a trigger fired this block).
    if (tailKillEnabled && loopJustTurnedOff
        && pendingTailKillSamples_ < 0
        && effectiveBpm > 0.0f && currentSampleRate_ > 0.0f)
    {
        const double spb = (60.0 / static_cast<double>(effectiveBpm))
                           * static_cast<double>(currentSampleRate_);
        pendingTailKillSamples_ = static_cast<int>(std::round(spb));
    }
    // When the toggle goes from ON → OFF mid-stream, cancel any kill
    // that was already pending so it doesn't fire after the user has
    // asked the tail to keep ringing.
    if (! tailKillEnabled)
        pendingTailKillSamples_ = -1;
    if (pendingTailKillSamples_ >= 0)
    {
        pendingTailKillSamples_ -= numSamples;
        if (pendingTailKillSamples_ <= 0)
        {
            // User-reported 2026-05-17: "in any scenario, regardless of
            // trig method, the tail should cut - and without introducing
            // any clicking at any point." chain_.killTail() fades the FX
            // wet over 6 ms but leaves the dry voice's natural decay
            // running — so the kick body continued past the kill point
            // while the wet dropped out, reading as "tail dies before
            // next trig." Fix: also startFadeout() on every active voice
            // so the dry body fades over its own 5 ms steal-fadeout
            // alongside the wet, giving a tight clean tail cut.
            voiceMgr_.fadeoutAllActive();
            chain_.killTailSoft();
            pendingTailKillSamples_ = -1;
        }
    }

    // Snapshot params for any triggers this buffer fires. (Per-trigger
    // snapshot — the BombVoice locks these in for its lifetime.)
    const bombo::VoiceTrigger trig = buildTriggerFromParams();

    masterGainSmoothed.setTargetValue(juce::Decibels::decibelsToGain(pMasterOut->get()));

    auto* left  = numChannels > 0 ? buffer.getWritePointer(0) : nullptr;
    auto* right = numChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    chainParams_ = buildChainParamsFromApvts();
    chain_.update(chainParams_);

    // Loop-cache: live capture on the first trigger after entering
    // LOOP + TAIL ON, then replay for subsequent beats so every loop
    // pass is sample-identical. Param change invalidates the cache so
    // the next trigger re-captures with the new settings. See
    // PluginProcessor.h LoopCache for the rationale.
    const bool loopCacheActive = loopNow && tailKillEnabled;
    if (! loopCacheActive)
    {
        loopCache_.capturing = false;
        loopCache_.valid     = false;
    }
    else if (loopCache_.valid
             && (std::memcmp(&loopCache_.capturedParams,
                             &chainParams_,
                             sizeof(chainParams_)) != 0
                 || loopCache_.capturedTrigger != trig))
    {
        // Any chain OR voice param moved — cache is stale, re-capture
        // on next trig. The currently-playing cached beat keeps reading
        // out of the buffer so the user hears the loop finish cleanly;
        // the next downbeat captures fresh with the new settings.
        // ALSO reset chain + stereo finalizer state so the re-capture
        // starts from clean FX state — without it the next captured
        // beat inherits whatever delay buffer + reverb conv residue
        // the live chain was carrying at the moment of the tweak.
        loopCache_.valid     = false;
        loopCache_.capturing = false;
        chain_.reset();
        stereoFin_.reset();
    }
    // beatSamples for the cache: round one beat to integer samples.
    const int cacheBeatSamples = (effectiveBpm > 0.0f && currentSampleRate_ > 0.0f)
        ? juce::jmin(loopCache_.buf.getNumSamples(),
                     static_cast<int>(std::round((60.0 / effectiveBpm) * currentSampleRate_)))
        : 0;

    for (int i = 0; i < numSamples; ++i)
    {
        const int nFired = voiceMgr_.tickPending();
        if (nFired > 0) waveBuffer_.triggerReset();
        for (int k = 0; k < nFired; ++k)
        {
            voiceMgr_.stealAndAdvance();
            voiceMgr_.trigger(trig);
            chain_.killTail();
            chain_.onTrigger(trig.pitchEnvDecayMs);

            if (loopCacheActive && cacheBeatSamples > 0)
            {
                if (! loopCache_.valid)
                {
                    // Begin a fresh capture of this beat.
                    loopCache_.capturing       = true;
                    loopCache_.valid           = false;
                    loopCache_.writePos        = 0;
                    loopCache_.readPos         = 0;
                    loopCache_.beatSamples     = cacheBeatSamples;
                    loopCache_.capturedParams  = chainParams_;
                    loopCache_.capturedTrigger = trig;
                }
                else
                {
                    // Sync replay cursor to the new trigger.
                    loopCache_.readPos = 0;
                }
            }
        }

        const float dry = voiceMgr_.renderSample();
        const float wet = chain_.process(dry);
        const float g = masterGainSmoothed.getNextValue();
        const float out = wet * g;

        // Hidden EDM finalizer: sub-mono < 120 Hz, slight Haas-width
        // on the upper band. Always on, no UI exposure. See
        // StereoFinalizer.h for the rationale.
        float oL = out, oR = out;
        stereoFin_.process(out, oL, oR);

        // Loop cache capture / playback.
        if (loopCacheActive)
        {
            if (loopCache_.capturing
                && loopCache_.writePos < loopCache_.beatSamples)
            {
                loopCache_.buf.setSample(0, loopCache_.writePos, oL);
                loopCache_.buf.setSample(1, loopCache_.writePos, oR);
                ++loopCache_.writePos;
                if (loopCache_.writePos >= loopCache_.beatSamples)
                {
                    loopCache_.capturing = false;
                    loopCache_.valid     = true;
                    loopCache_.readPos   = loopCache_.beatSamples;  // wait for next trig
                }
            }
            else if (loopCache_.valid
                     && loopCache_.readPos < loopCache_.beatSamples)
            {
                // Override live output with the captured beat.
                oL = loopCache_.buf.getSample(0, loopCache_.readPos);
                oR = loopCache_.buf.getSample(1, loopCache_.readPos);
                ++loopCache_.readPos;
            }
        }

        if (left)  left[i]  = oL;
        if (right) right[i] = oR;

        // Feed the scope ring after master gain so what users see matches
        // what they hear. The scope sees the L channel (sub-mono content
        // is identical between channels; upper-band Haas is sub-perceptual
        // for the visual representation).
        waveBuffer_.push(oL);
    }
}

juce::AudioProcessorEditor* BomboProcessor::createEditor()
{
    return new BomboEditor(*this);
}

namespace
{
// Discover bundled factory WAVs by scanning BinaryData's named-resource
// list. Returns parallel arrays sorted by original filename so the
// playback order matches the lexical order of Resources/Samples/.
struct FactoryBank
{
    std::vector<juce::String> originalFilenames;  // e.g. "kick_01.wav"
    std::vector<juce::String> resourceNames;      // e.g. "kick_01_wav"
};

FactoryBank discoverFactoryBank()
{
    FactoryBank b;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String resName(BinaryData::namedResourceList[i]);
        const juce::String origName(
            BinaryData::getNamedResourceOriginalFilename(resName.toRawUTF8()));
        if (origName.startsWith("kick_") && origName.endsWithIgnoreCase(".wav"))
        {
            b.originalFilenames.push_back(origName);
            b.resourceNames.push_back(resName);
        }
    }
    // Stable lexical sort over both arrays in lockstep.
    std::vector<size_t> idx(b.originalFilenames.size());
    std::iota(idx.begin(), idx.end(), size_t{0});
    std::sort(idx.begin(), idx.end(),
              [&](size_t a, size_t c) {
                  return b.originalFilenames[a].compareIgnoreCase(b.originalFilenames[c]) < 0;
              });
    FactoryBank sorted;
    sorted.originalFilenames.reserve(idx.size());
    sorted.resourceNames.reserve(idx.size());
    for (auto i : idx)
    {
        sorted.originalFilenames.push_back(b.originalFilenames[i]);
        sorted.resourceNames.push_back(b.resourceNames[i]);
    }
    return sorted;
}

// JUCE's BinaryData generator mangles non-alphanumerics to '_', so
// "kick_01.wav" → "kick_01_wav". We use this to convert a display name
// (which we persist in voiceBFactoryNames_) back to the resource id.
juce::String factoryResourceFromName(const juce::String& filename) noexcept
{
    return filename.replaceCharacter('.', '_');
}
} // namespace

void BomboProcessor::loadVoiceBSample(const juce::File& file)
{
    auto buf = bombo::SampleSlot::loadFromFile(
        file, static_cast<double>(currentSampleRate_));
    {
        juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
        voiceBSample_       = std::move(buf);
        voiceBSamplePath_   = (voiceBSample_ ? file.getFullPathName() : juce::String());
        // Single-file load clears any folder browse state AND factory mode.
        voiceBFolderPath_.clear();
        voiceBFolderSamples_.clear();
        voiceBFolderIndex_  = -1;
        voiceBIsFactory_    = false;
        voiceBFactoryNames_.clear();
    }
}

void BomboProcessor::setVoiceBSampleFolder(const juce::File& filePicked)
{
    // Scan the parent folder for samples we can load. Cap at 256 entries
    // so a user accidentally pointing at a giant library doesn't lag the UI.
    constexpr int kMaxFolderSamples = 256;
    auto folder = filePicked.getParentDirectory();
    if (! folder.isDirectory()) return;

    juce::Array<juce::File> found;
    folder.findChildFiles(found, juce::File::findFiles, false,
                          "*.wav;*.aif;*.aiff;*.flac");
    // Stable sort by filename (case-insensitive) so the index ordering is
    // predictable across sessions and machines.
    std::sort(found.begin(), found.end(),
              [](const juce::File& a, const juce::File& b)
              {
                  return a.getFileName().compareIgnoreCase(b.getFileName()) < 0;
              });
    if (found.size() > kMaxFolderSamples) found.removeRange(kMaxFolderSamples,
                                                            found.size() - kMaxFolderSamples);

    int idx = -1;
    for (int i = 0; i < found.size(); ++i)
        if (found.getReference(i) == filePicked) { idx = i; break; }
    if (idx < 0 && ! found.isEmpty()) idx = 0;
    if (idx < 0) return; // empty folder

    auto buf = bombo::SampleSlot::loadFromFile(
        found.getReference(idx), static_cast<double>(currentSampleRate_));

    {
        juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
        voiceBFolderPath_ = folder.getFullPathName();
        voiceBFolderSamples_.assign(found.begin(), found.end());
        voiceBFolderIndex_ = idx;
        voiceBSample_      = std::move(buf);
        voiceBSamplePath_  = (voiceBSample_
                              ? found.getReference(idx).getFullPathName()
                              : juce::String());
        voiceBIsFactory_   = false;
        voiceBFactoryNames_.clear();
    }
}

void BomboProcessor::loadVoiceBSampleByIndex(int idx)
{
    bool isFactory = false;
    juce::String factoryRes;
    juce::String factoryDisplay;
    juce::File   userFile;
    {
        juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
        if (voiceBIsFactory_)
        {
            if (idx < 0 || idx >= static_cast<int>(voiceBFactoryNames_.size()))
                return;
            factoryDisplay = voiceBFactoryNames_[static_cast<size_t>(idx)];
            factoryRes     = factoryResourceFromName(factoryDisplay);
            isFactory      = true;
        }
        else
        {
            if (idx < 0 || idx >= static_cast<int>(voiceBFolderSamples_.size()))
                return;
            userFile = voiceBFolderSamples_[static_cast<size_t>(idx)];
        }
    }

    std::shared_ptr<const juce::AudioBuffer<float>> buf;
    juce::String newPath;
    if (isFactory)
    {
        int sizeBytes = 0;
        const char* data = BinaryData::getNamedResource(factoryRes.toRawUTF8(), sizeBytes);
        if (data == nullptr || sizeBytes <= 0) return;
        buf = bombo::SampleSlot::loadFromMemory(
            data, static_cast<size_t>(sizeBytes),
            static_cast<double>(currentSampleRate_));
        newPath = factoryDisplay;
    }
    else
    {
        buf = bombo::SampleSlot::loadFromFile(
            userFile, static_cast<double>(currentSampleRate_));
        newPath = buf ? userFile.getFullPathName() : juce::String();
    }

    {
        juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
        voiceBFolderIndex_ = idx;
        voiceBSample_      = std::move(buf);
        voiceBSamplePath_  = newPath;
    }
}

void BomboProcessor::loadFactorySamples()
{
    const auto bank = discoverFactoryBank();
    if (bank.originalFilenames.empty()) return;

    int sizeBytes = 0;
    const char* data = BinaryData::getNamedResource(bank.resourceNames[0].toRawUTF8(),
                                                    sizeBytes);
    auto buf = (data != nullptr && sizeBytes > 0 && currentSampleRate_ > 0.0f)
        ? bombo::SampleSlot::loadFromMemory(
              data, static_cast<size_t>(sizeBytes),
              static_cast<double>(currentSampleRate_))
        : std::shared_ptr<const juce::AudioBuffer<float>>{};

    juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
    voiceBIsFactory_     = true;
    voiceBFactoryNames_  = bank.originalFilenames;
    voiceBFolderSamples_.clear();
    voiceBFolderPath_    = "<factory>";   // sentinel; state save recognises this
    voiceBFolderIndex_   = 0;
    voiceBSample_        = std::move(buf);
    voiceBSamplePath_    = bank.originalFilenames[0];
}

bool BomboProcessor::voiceBIsFactory() const noexcept
{
    juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
    return voiceBIsFactory_;
}

void BomboProcessor::clearVoiceBSample()
{
    juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
    voiceBSample_.reset();
    voiceBSamplePath_.clear();
    voiceBFolderPath_.clear();
    voiceBFolderSamples_.clear();
    voiceBFolderIndex_ = -1;
    voiceBIsFactory_   = false;
    voiceBFactoryNames_.clear();
}

juce::String BomboProcessor::voiceBSamplePath() const
{
    juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
    return voiceBSamplePath_;
}

BomboProcessor::VoiceBSnapshot BomboProcessor::snapshotVoiceBSample() const
{
    juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
    return { voiceBSample_, voiceBSamplePath_ };
}

void BomboProcessor::installVoiceBSampleSnapshot(VoiceBSnapshot snap)
{
    juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
    voiceBSample_     = std::move(snap.buffer);
    voiceBSamplePath_ = std::move(snap.path);
}

void BomboProcessor::randomizeBombo()
{
    using namespace bombo::pid;
    juce::Random r;

    // Set helpers. `setPlain` takes a plain (un-normalized) value and lets
    // the param convert via its range; `setNorm` writes a normalized 0..1
    // value directly. Both wrap begin/endChangeGesture so the host sees
    // user-initiated changes.
    auto setPlain = [&](const char* id, float plain)
    {
        if (auto* p = apvts.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(plain));
            p->endChangeGesture();
        }
    };
    auto setNorm = [&](const char* id, float n)
    {
        if (auto* p = apvts.getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, n));
            p->endChangeGesture();
        }
    };
    auto setChoice = [&](const char* id, int numChoices)
    {
        if (auto* p = apvts.getParameter(id))
        {
            const int idx = r.nextInt(numChoices);
            const float n = (numChoices > 1)
                ? static_cast<float>(idx) / static_cast<float>(numChoices - 1)
                : 0.0f;
            p->beginChangeGesture();
            p->setValueNotifyingHost(n);
            p->endChangeGesture();
        }
    };
    auto rng = [&](float lo, float hi) { return lo + r.nextFloat() * (hi - lo); };

    // ── VOICE A (sub) ────────────────────────────────────────────────
    setChoice(waveform, 4);
    setPlain (pitchStart,    rng( 80.0f, 220.0f));
    setPlain (pitchEnd,      rng( 30.0f,  75.0f));
    setPlain (pitchDecay,    rng( 40.0f, 250.0f));
    setPlain (pitchCurve,    rng(  1.5f,   4.5f));

    // ── VOICE B (body) ───────────────────────────────────────────────
    setPlain (ampAttack,     rng(  0.2f,   3.0f));
    setPlain (ampDecay,      rng(200.0f,1200.0f));
    setNorm  (clickAmount,   rng(  0.0f,   0.6f));
    setNorm  (noiseAmount,   rng(  0.0f,   0.50f));
    setNorm  (noiseColor,    rng(  0.10f,  0.7f));

    // ── DRIVE ───────────────────────────────────────────────────────
    setNorm  (driveAmount,   rng(  0.0f,   0.6f));
    setChoice(driveMode, 4);
    // BIAS centre is 0.5 normalized (= 0.0 plain). Keep mostly centred
    // with mild asymmetry; extremes reserved for deliberate design.
    setNorm  (driveBias,     rng(  0.35f,  0.65f));
    setNorm  (fxDriveAmount, rng(  0.0f,   0.5f));
    setChoice(fxDriveMode, 4);
    setNorm  (fxDriveMix,    rng(  0.5f,   1.0f));

    // ── DELAY ───────────────────────────────────────────────────────
    setPlain (delayTime,     rng( 50.0f, 500.0f));
    setNorm  (delayFeedback, rng(  0.2f,   0.65f));
    // 60% chance free, 40% chance a musically-tame sync mode (1/4..1/8T).
    setChoice(delayTimeMode, (rng(0.0f, 1.0f) < 0.6f) ? 0 : (4 + (int) (rng(0.0f, 6.0f))));
    setNorm  (delayMorph,    rng(  0.15f,  0.85f));
    setNorm  (delaySmear,  rng(  0.0f,   0.5f));
    setNorm  (delayMix,      rng(  0.0f,   0.35f));

    // ── REVERB ──────────────────────────────────────────────────────
    // Pick any algo with equal weight. Diffusion is hidden post-rework
    // so we leave its value alone (preset-compat only).
    setChoice(reverbType, (int) (rng(0.0f, (float) bombo::ir::kNumAlgos)));
    setNorm  (reverbSize,      rng(0.3f, 0.8f));
    setNorm  (reverbDecay,     rng(0.3f, 0.7f));
    setNorm  (reverbDamp,      rng(0.3f, 0.7f));
    setPlain (reverbPredelay,  rng(0.0f, 80.0f));
    setNorm  (reverbMix,       rng(0.0f, 0.4f));

    // ── FILTER ──────────────────────────────────────────────────────
    setPlain (filterHp,    rng(  20.0f,   80.0f));
    setPlain (filterHpQ,   rng(   0.7f,    1.3f));
    setPlain (filterLp,    rng(1500.0f,12000.0f));
    setPlain (filterLpQ,   rng(   0.7f,    1.3f));
    setNorm  (filterColor, rng(   0.0f,    0.5f));
    // TEETH: 0.5 norm = 0 (no tracking). Bias toward positive (closing with pitch).
    setNorm  (filterTeeth, rng(   0.4f,    0.8f));

    // ── DUCK ────────────────────────────────────────────────────────
    setPlain (duckAtk,    rng(  0.5f,  10.0f));
    setPlain (duckHold,   rng(  0.0f,  80.0f));
    setPlain (duckRel,    rng( 80.0f, 350.0f));
    setNorm  (duckDepth,  rng(  0.0f,   0.8f));
    // SHAPE: 0.5 norm = 0.0 plain (linear). Spread into both exp and log territory.
    setNorm  (duckShape,  rng(  0.2f,   0.8f));
    setNorm  (duckGrowl,   rng(  0.0f,   0.55f));

    // Section mutes — clear all so the user hears the full result. (No
    // point getting a randomized kick that's silent because DRIVE muted.)
    for (const auto* id : { driveMute, delayMute, reverbMute, filterMute,
                            duckMute, voiceAMute, voiceBMute })
        setNorm(id, 0.0f);
}

juce::StringArray BomboProcessor::voiceBSampleNames() const
{
    juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
    juce::StringArray out;
    if (voiceBIsFactory_)
    {
        out.ensureStorageAllocated(static_cast<int>(voiceBFactoryNames_.size()));
        for (const auto& s : voiceBFactoryNames_)
            out.add(s.upToLastOccurrenceOf(".", false, false));
    }
    else
    {
        out.ensureStorageAllocated(static_cast<int>(voiceBFolderSamples_.size()));
        for (const auto& f : voiceBFolderSamples_)
            out.add(f.getFileNameWithoutExtension());
    }
    return out;
}

int BomboProcessor::voiceBSampleIndex() const
{
    juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
    return voiceBFolderIndex_;
}

void BomboProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Stamp the sample path AND folder context onto the APVTS tree as a
    // child node so it round-trips through XML serialization. `folder`
    // (when present) tells the restore path to use setVoiceBSampleFolder
    // (which repopulates the cached list); `path` alone is treated as a
    // single-file load. Factory mode uses "<factory>" as the folder
    // sentinel and "<factory>:<idx>" as the path so the restore can
    // route to loadFactorySamples + loadVoiceBSampleByIndex.
    {
        juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
        auto child = apvts.state.getOrCreateChildWithName("VoiceBSample", nullptr);
        if (voiceBIsFactory_)
        {
            child.setProperty("folder", "<factory>", nullptr);
            child.setProperty("path",
                juce::String("<factory>:") + juce::String(voiceBFolderIndex_),
                nullptr);
        }
        else
        {
            child.setProperty("path",   voiceBSamplePath_, nullptr);
            child.setProperty("folder", voiceBFolderPath_, nullptr);
        }
    }
    // FX chain order — comma-separated lowercase stage names. Sanitized on
    // read; absent property means default order (drive,filter,delay,reverb).
    {
        const auto order = chain_.getFxOrder();
        auto child = apvts.state.getOrCreateChildWithName("FxOrder", nullptr);
        juce::StringArray parts;
        for (auto f : order) parts.add(bombo::fxIdToString(f));
        child.setProperty("order", parts.joinIntoString(","), nullptr);
    }
    if (auto xml = apvts.state.createXml())
        copyXmlToBinary(*xml, destData);
}

void BomboProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
    {
        auto tree = juce::ValueTree::fromXml(*xml);
        if (! tree.isValid()) return;
        apvts.replaceState(tree);

        // Restore FX chain order if present. Legacy state (pre-feature) has
        // no FxOrder child — chain_ keeps its default order in that case.
        if (auto fxNode = apvts.state.getChildWithName("FxOrder"); fxNode.isValid())
        {
            const juce::String s = fxNode.getProperty("order", juce::String()).toString();
            juce::StringArray parts;
            parts.addTokens(s, ",", "");
            parts.trim();
            if (parts.size() == 4)
            {
                bombo::FxOrder o{};
                bool ok = true;
                for (int i = 0; i < 4; ++i)
                    ok = ok && bombo::fxIdFromString(parts[i], o[(std::size_t) i]);
                if (ok) chain_.setFxOrder(o);  // setFxOrder re-validates
            }
        }

        // Re-load the VOICE B sample asynchronously — file I/O off the
        // audio thread, and SR must be settled (prepareToPlay may not
        // have run yet on first project load).
        auto child = apvts.state.getChildWithName("VoiceBSample");
        const juce::String path   = child.isValid()
            ? child.getProperty("path", juce::String()).toString() : juce::String();
        const juce::String folder = child.isValid()
            ? child.getProperty("folder", juce::String()).toString() : juce::String();
        if (path.isNotEmpty())
        {
            const bool isFactoryRestore = path.startsWith("<factory>");

            // Stash for prepareToPlay if SR isn't known yet (host hasn't
            // called prepare). Either way fire an async load — if SR is
            // ready it'll succeed, otherwise prepareToPlay will replay it.
            {
                juce::SpinLock::ScopedLockType lock(voiceBSampleLock_);
                pendingRestorePath_     = path;
                pendingRestoreIsFolder_ = folder.isNotEmpty();
            }
            auto restore = [this, path, folder, isFactoryRestore]()
            {
                if (isFactoryRestore)
                {
                    this->loadFactorySamples();
                    const int savedIdx = path.fromFirstOccurrenceOf(":", false, false)
                                              .getIntValue();
                    if (savedIdx > 0) this->loadVoiceBSampleByIndex(savedIdx);
                }
                else if (folder.isNotEmpty())
                    this->setVoiceBSampleFolder(juce::File(path));
                else
                    this->loadVoiceBSample(juce::File(path));
            };
            // setStateInformation can run on the bouncer's clone — calling
            // back through the message thread would land after destruction.
            if (isNonRealtime()) restore();
            else                 juce::MessageManager::callAsync(std::move(restore));
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BomboProcessor();
}
