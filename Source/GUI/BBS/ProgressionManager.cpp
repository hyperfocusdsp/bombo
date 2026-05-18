#include "ProgressionManager.h"
#include <juce_core/juce_core.h>

namespace bombo
{

ProgressionManager::ProgressionManager(PersistentState& state)
    : state_(state)
{
    loadFromState();
}

void ProgressionManager::onKickSaved()
{
    ++savesCount_;
    checkAndApplyLevelUp();
    saveToState();
}

int ProgressionManager::currentSysopIndex() const
{
    if (unlockedSysops_.empty()) return 0;
    const int day = static_cast<int>(
        juce::Time::getCurrentTime().getDayOfWeek());  // 0=Sun…6=Sat
    return unlockedSysops_[static_cast<size_t>(day) % unlockedSysops_.size()];
}

void ProgressionManager::forceReset()
{
    level_      = 0;
    savesCount_ = 0;
    unlockedSysops_ = { 0, 1, 2 };
    state_.setBbsLevel(0);
    state_.setBbsSavesCount(0);
    state_.setBbsUnlockedSysops("0,1,2");
    state_.setBbsUnlocked(false);
}

void ProgressionManager::loadFromState()
{
    savesCount_ = state_.getBbsSavesCount();
    level_      = state_.getBbsLevel();

    const auto csv = state_.getBbsUnlockedSysops();
    unlockedSysops_.clear();
    for (auto& tok : juce::StringArray::fromTokens(csv, ",", ""))
        if (tok.isNotEmpty())
            unlockedSysops_.push_back(tok.trim().getIntValue());
    if (unlockedSysops_.empty())
        unlockedSysops_ = { 0, 1, 2 };
}

void ProgressionManager::saveToState()
{
    state_.setBbsSavesCount(savesCount_);
    state_.setBbsLevel(level_);

    juce::StringArray parts;
    for (int idx : unlockedSysops_)
        parts.add(juce::String(idx));
    state_.setBbsUnlockedSysops(parts.joinIntoString(","));
}

void ProgressionManager::checkAndApplyLevelUp()
{
    while (level_ < 4 && savesCount_ >= kThresholds[level_ + 1])
    {
        ++level_;
        const int newSysop = kUnlockAtLevel[level_];
        if (newSysop >= 0)
            unlockedSysops_.push_back(newSysop);
        if (onLevelUp) onLevelUp(level_);
    }
}

} // namespace bombo
