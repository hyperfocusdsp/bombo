#include "VoiceManager.h"

namespace bombo
{

VoiceManager::VoiceManager() = default;

void VoiceManager::prepare(float sampleRate) noexcept
{
    sampleRate_ = sampleRate;
    for (auto& v : voices_) v.setSampleRate(sampleRate);
    for (auto& slot : pending_) { slot.live = false; slot.samplesUntil = 0; }
    activeVoice_ = 0;
}

void VoiceManager::pushPending(int samplesUntil) noexcept
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

int VoiceManager::tickPending() noexcept
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

void VoiceManager::stealAndAdvance() noexcept
{
    // Kick drum is conceptually monophonic — the voice pool exists ONLY
    // so the 5 ms steal fadeout has room to complete before the new note
    // takes over. With round-robin steal-of-current-only, intermediate
    // voices in a 4-slot pool kept ringing through new triggers (long
    // ampDecay default of 700 ms made this audible as "voice A continues
    // its release past the next trig"). Fix: fade EVERY still-active
    // voice on each trigger, then advance the cursor to a fresh slot.
    for (auto& v : voices_)
        if (v.isActive())
            v.startFadeout(sampleRate_);
    activeVoice_ = (activeVoice_ + 1) % kNumVoices;
}

void VoiceManager::trigger(const VoiceTrigger& t) noexcept
{
    voices_[activeVoice_].trigger(t);
}

void VoiceManager::fadeoutAllActive() noexcept
{
    // Deferred tail-kill path: caller has decided no more triggers are
    // coming until the next user action. Fade every voice in step with
    // the FX bus killTail() so the audible decay dies cleanly.
    for (auto& v : voices_)
        if (v.isActive())
            v.startFadeout(sampleRate_);
}

float VoiceManager::renderSample() noexcept
{
    float dry = 0.0f;
    for (auto& v : voices_) dry += v.tick();
    return dry;
}

bool VoiceManager::anyActive() const noexcept
{
    for (const auto& v : voices_) if (v.isActive()) return true;
    return false;
}

} // namespace bombo
