#include "GUI/BBS/ProgressionManager.h"
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace
{

class ProgressionLevelThresholdTest : public juce::UnitTest
{
public:
    ProgressionLevelThresholdTest()
        : juce::UnitTest("Progression: level thresholds") {}
    void runTest() override
    {
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_prog_thresh_"
                                     + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();
        bombo::PersistentState state(tmp);
        bombo::ProgressionManager pm(state);

        beginTest("starts at level 0");
        expectEquals(pm.currentLevel(), 0);

        beginTest("level 1 at exactly 5 saves");
        for (int i = 0; i < 5; ++i) pm.onKickSaved();
        expectEquals(pm.currentLevel(), 1);

        beginTest("level 2 at exactly 15 saves (10 more)");
        for (int i = 0; i < 10; ++i) pm.onKickSaved();
        expectEquals(pm.currentLevel(), 2);

        beginTest("level 3 at 30 saves (15 more)");
        for (int i = 0; i < 15; ++i) pm.onKickSaved();
        expectEquals(pm.currentLevel(), 3);

        beginTest("level 4 at 50 saves (20 more)");
        for (int i = 0; i < 20; ++i) pm.onKickSaved();
        expectEquals(pm.currentLevel(), 4);

        beginTest("level stays at 4 after more saves");
        for (int i = 0; i < 100; ++i) pm.onKickSaved();
        expectEquals(pm.currentLevel(), 4);

        tmp.deleteRecursively();
    }
};

class ProgressionUnlockTest : public juce::UnitTest
{
public:
    ProgressionUnlockTest()
        : juce::UnitTest("Progression: sysop unlocks") {}
    void runTest() override
    {
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_prog_unlock_"
                                     + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();
        bombo::PersistentState state(tmp);
        bombo::ProgressionManager pm(state);

        beginTest("starts with sysops {0,1,2}");
        expectEquals((int)pm.unlockedSysopIndices().size(), 3);

        beginTest("L1 adds sysop 3");
        for (int i = 0; i < 5; ++i) pm.onKickSaved();
        expectEquals((int)pm.unlockedSysopIndices().size(), 4);
        expect(pm.unlockedSysopIndices().back() == 3);

        beginTest("L2 adds sysop 4");
        for (int i = 0; i < 10; ++i) pm.onKickSaved();
        expectEquals((int)pm.unlockedSysopIndices().size(), 5);
        expect(pm.unlockedSysopIndices().back() == 4);

        tmp.deleteRecursively();
    }
};

class ProgressionPersistenceTest : public juce::UnitTest
{
public:
    ProgressionPersistenceTest()
        : juce::UnitTest("Progression: survives restart") {}
    void runTest() override
    {
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_prog_persist_"
                                     + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();

        beginTest("level and saves survive reconstruction");
        {
            bombo::PersistentState state(tmp);
            bombo::ProgressionManager pm(state);
            for (int i = 0; i < 17; ++i) pm.onKickSaved();  // level 2
            expectEquals(pm.currentLevel(), 2);
        }
        {
            bombo::PersistentState state(tmp);
            bombo::ProgressionManager pm(state);
            expectEquals(pm.currentLevel(), 2);
            expectEquals((int)pm.unlockedSysopIndices().size(), 5);
        }

        tmp.deleteRecursively();
    }
};

class ProgressionForceResetTest : public juce::UnitTest
{
public:
    ProgressionForceResetTest()
        : juce::UnitTest("Progression: force reset") {}
    void runTest() override
    {
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_prog_reset_"
                                     + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();
        bombo::PersistentState state(tmp);
        bombo::ProgressionManager pm(state);

        for (int i = 0; i < 30; ++i) pm.onKickSaved();
        expectEquals(pm.currentLevel(), 3);

        beginTest("forceReset brings back level 0 and 3 sysops");
        pm.forceReset();
        expectEquals(pm.currentLevel(), 0);
        expectEquals((int)pm.unlockedSysopIndices().size(), 3);

        tmp.deleteRecursively();
    }
};

static ProgressionLevelThresholdTest progressionLevelThresholdTest;
static ProgressionUnlockTest         progressionUnlockTest;
static ProgressionPersistenceTest    progressionPersistenceTest;
static ProgressionForceResetTest     progressionForceResetTest;

} // anonymous namespace
