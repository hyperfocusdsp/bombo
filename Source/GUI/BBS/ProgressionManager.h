#pragma once
#include "../../State/PersistentState.h"
#include <functional>
#include <vector>

namespace bombo
{

// Owns BBS progression state. Tracks how many kicks have been saved,
// computes the current level, and maintains the list of unlocked SYSOP
// indices. All reads/writes go through PersistentState so state survives
// plugin restarts.
//
// Level thresholds (saves):  0→L1=5, L1→L2=15, L2→L3=30, L3→L4=50.
// SYSOP unlocks per level:   L0={0,1,2}, +3 at L1, +4 at L2, +5 at L3, +6 at L4.
// Current SYSOP:             unlockedSysops[UTC_weekday % unlockedSysops.size()].
class ProgressionManager
{
public:
    explicit ProgressionManager(PersistentState& state);

    // Call whenever the user saves a kick. Increments saves counter,
    // checks for level-up, fires onLevelUp if level changed.
    void onKickSaved();

    int                      currentLevel() const noexcept  { return level_; }
    const std::vector<int>&  unlockedSysopIndices() const noexcept { return unlockedSysops_; }

    // Returns the SYSOP index active today (rotates by UTC weekday within
    // the unlocked pool). Safe to call before any kicks are saved.
    int currentSysopIndex() const;

    // Resets all progression state to factory defaults (level 0, 0 saves,
    // sysops {0,1,2}, bbs.unlocked = false). The secret reset gesture.
    void forceReset();

    // Fired after a level-up, with the new level (1–4).
    std::function<void(int newLevel)> onLevelUp;

private:
    PersistentState& state_;
    int level_        = 0;
    int savesCount_   = 0;
    std::vector<int> unlockedSysops_;

    static constexpr int kThresholds[5] = { 0, 5, 15, 30, 50 };
    // Maps level (1-4) → SYSOP index to unlock at that level.
    static constexpr int kUnlockAtLevel[5] = { -1, 3, 4, 5, 6 };

    void loadFromState();
    void saveToState();
    void checkAndApplyLevelUp();
};

} // namespace bombo
