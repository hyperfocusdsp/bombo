#include "BoomFeed.h"
#include <juce_core/juce_core.h>
#include <cstring>
#include <cmath>

namespace bombo
{

// Musically-sane bounds for full-random generation.
// Excludes: masterOut, waveform/driveMode/fxDriveMode/delayTimeMode (enums),
// bpm/loopOn/tailKillOn/*Mute/limiterOn (transport / global toggles).
const BoomFeed::ParamBounds BoomFeed::kRandomParams[] =
{
    { pid::pitchStart,      0.10f, 0.90f },
    { pid::pitchEnd,        0.00f, 0.50f },
    { pid::pitchDecay,      0.10f, 0.80f },
    { pid::pitchCurve,      0.20f, 0.80f },
    { pid::subHpf,          0.00f, 0.20f },
    { pid::midPitchStart,   0.00f, 1.00f },
    { pid::midPitchEnd,     0.00f, 0.70f },
    { pid::midDecay,        0.10f, 0.80f },
    { pid::midLevel,        0.00f, 0.80f },
    { pid::ampAttack,       0.00f, 0.30f },
    { pid::ampDecay,        0.10f, 0.90f },
    { pid::clickAmount,     0.00f, 1.00f },
    { pid::clickCenter,     0.20f, 0.80f },
    { pid::noiseAmount,     0.00f, 0.50f },
    { pid::noiseColor,      0.00f, 1.00f },
    { pid::driveAmount,     0.00f, 0.80f },
    { pid::voiceBalance,    0.10f, 0.90f },
    { pid::fxDriveAmount,   0.00f, 0.70f },
    { pid::fxDriveMix,      0.00f, 0.60f },
    { pid::filterHp,        0.00f, 0.40f },
    { pid::filterHpQ,       0.00f, 0.70f },
    { pid::filterLp,        0.30f, 1.00f },
    { pid::filterLpQ,       0.00f, 0.70f },
    { pid::filterColor,     0.00f, 1.00f },
    { pid::delayTime,       0.00f, 0.70f },
    { pid::delayFeedback,   0.00f, 0.60f },
    { pid::delayMorph,      0.00f, 1.00f },
    { pid::delayMix,        0.00f, 0.35f },
    { pid::reverbSize,      0.00f, 0.65f },
    { pid::reverbDecay,     0.00f, 0.70f },
    { pid::reverbDamp,      0.00f, 1.00f },
    { pid::reverbDiffusion, 0.00f, 1.00f },
    { pid::reverbPredelay,  0.00f, 0.30f },
    { pid::reverbMix,       0.00f, 0.40f },
    { pid::duckAtk,         0.00f, 0.80f },
    { pid::duckHold,        0.00f, 0.70f },
    { pid::duckRel,         0.00f, 0.80f },
    { pid::duckDepth,       0.00f, 0.70f },
    { pid::limiterAmount,   0.50f, 1.00f },
};
const int BoomFeed::kRandomParamsCount =
    static_cast<int>(sizeof(kRandomParams) / sizeof(kRandomParams[0]));

BoomFeed::BoomFeed() = default;

void BoomFeed::setApvts(juce::AudioProcessorValueTreeState* apvts) noexcept
{
    apvts_ = apvts;
}
void BoomFeed::setTriggerCallback(std::function<void()> cb) noexcept
{
    triggerCb_ = std::move(cb);
}

void BoomFeed::advance(Mode mode)
{
    pushHistory(current_);
    current_ = (mode == Mode::Random)
                   ? generateRandom(rng_)
                   : mutateFrom(current_, rng_);
    applySnapshot(current_);
    if (triggerCb_) triggerCb_();
}

void BoomFeed::prev()
{
    if (historyCount_ == 0) return;
    historyHead_ = (historyHead_ + kHistorySize - 1) % kHistorySize;
    --historyCount_;
    current_ = history_[static_cast<size_t>(historyHead_)];
    applySnapshot(current_);
    if (triggerCb_) triggerCb_();
}

juce::String BoomFeed::currentFilename()      const { return snapshotToFilename(current_); }
juce::String BoomFeed::currentWaveformAscii() const { return snapshotToWaveform(current_); }

BoomFeed::Snapshot BoomFeed::generateRandom(juce::Random& rng)
{
    Snapshot s;
    s.values.reserve(static_cast<size_t>(kRandomParamsCount));
    for (int i = 0; i < kRandomParamsCount; ++i)
    {
        const auto& b = kRandomParams[i];
        const float v = b.lo + rng.nextFloat() * (b.hi - b.lo);
        s.values.push_back({ b.id, v });
    }
    return s;
}

BoomFeed::Snapshot BoomFeed::mutateFrom(const Snapshot& src, juce::Random& rng)
{
    Snapshot s = src;
    for (auto& [id, val] : s.values)
    {
        // Gaussian approximation: sum of two uniforms [-σ, +σ].
        const float sigma = 0.12f;
        const float delta = (rng.nextFloat() - 0.5f) * sigma * 2.0f
                          + (rng.nextFloat() - 0.5f) * sigma * 2.0f;
        val = juce::jlimit(0.0f, 1.0f, val + delta);
    }
    return s;
}

void BoomFeed::applySnapshot(const Snapshot& s)
{
    if (apvts_ == nullptr) return;
    for (const auto& [id, val] : s.values)
    {
        if (auto* p = apvts_->getParameter(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(val);
            p->endChangeGesture();
        }
    }
}

void BoomFeed::pushHistory(const Snapshot& s)
{
    history_[static_cast<size_t>(historyHead_)] = s;
    historyHead_ = (historyHead_ + 1) % kHistorySize;
    if (historyCount_ < kHistorySize) ++historyCount_;
}

juce::String BoomFeed::snapshotToFilename(const Snapshot& s) const
{
    // XOR-fold the normalised values into two 16-bit words for the filename.
    uint32_t h = 0;
    for (const auto& [id, val] : s.values)
    {
        uint32_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        h ^= bits * 2654435761u;  // Knuth multiplicative hash
    }
    const juce::String hex = juce::String::toHexString(static_cast<int>(h)).toUpperCase();
    return "KICK-" + hex.substring(0, 4) + "-" + hex.substring(4, 8) + ".KCK";
}

juce::String BoomFeed::snapshotToWaveform(const Snapshot& s) const
{
    // Approximate a kick waveform shape from ampDecay: descending bar chart.
    float decayNorm = 0.5f;
    for (const auto& [id, val] : s.values)
        if (id == juce::String(pid::ampDecay)) { decayNorm = val; break; }

    const char* blockChars[] = { " ", "\xe2\x96\x81", "\xe2\x96\x82", "\xe2\x96\x83",
                                  "\xe2\x96\x84", "\xe2\x96\x85", "\xe2\x96\x86",
                                  "\xe2\x96\x87", "\xe2\x96\x88" };
    juce::String result;
    for (int i = 0; i < 18; ++i)
    {
        const float t     = static_cast<float>(i) / 17.0f;
        const float decay = 1.0f + decayNorm * 8.0f;
        const float env   = (i == 0) ? 1.0f : std::exp(-t * decay);
        const int   idx   = juce::jlimit(0, 8, static_cast<int>(env * 8.0f));
        result += blockChars[idx];
    }
    return result;
}

} // namespace bombo
