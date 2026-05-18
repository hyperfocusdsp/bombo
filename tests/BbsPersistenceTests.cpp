// tests/BbsPersistenceTests.cpp — registered UnitTests for the BBS
// hidden-terminal persistence keys on PersistentState.
//
// Compiled as its own translation unit (see CMakeLists.txt). JUCE finds
// the tests via static UnitTest registration in the anonymous namespace.

#include "State/PersistentState.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace
{

class BbsUnlockedDefaultTest : public juce::UnitTest
{
public:
    BbsUnlockedDefaultTest() : juce::UnitTest("BBS: unlocked defaults to false") {}

    void runTest() override
    {
        beginTest("fresh PersistentState reports bbs.unlocked = false");
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_test_bbs_default_"
                                     + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();

        bombo::PersistentState state(tmp);
        expect(! state.getBbsUnlocked(),
               "default is false when no state file present");
        expectEquals(state.getBbsLastScreen(), 0,
                     "default screen enum is 0 (login)");

        tmp.deleteRecursively();
    }
};

class BbsUnlockedRoundTripTest : public juce::UnitTest
{
public:
    BbsUnlockedRoundTripTest() : juce::UnitTest("BBS: unlocked + lastScreen persist") {}

    void runTest() override
    {
        beginTest("setBbsUnlocked(true) survives PersistentState reconstruction");
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_test_bbs_roundtrip_"
                                     + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();

        {
            bombo::PersistentState state(tmp);
            state.setBbsUnlocked(true);
            state.setBbsLastScreen(2);  // arbitrary non-default
        }   // dtor flushes via saveIfNeeded

        {
            bombo::PersistentState state(tmp);
            expect(state.getBbsUnlocked(),
                   "bbs.unlocked persists across instances");
            expectEquals(state.getBbsLastScreen(), 2,
                         "bbs.lastScreen persists across instances");
        }

        tmp.deleteRecursively();
    }
};

class BbsUnlockedIndependentOfThemeTest : public juce::UnitTest
{
public:
    BbsUnlockedIndependentOfThemeTest()
        : juce::UnitTest("BBS: unlocked state survives theme switches") {}

    void runTest() override
    {
        // Memory `project_bombo_archie_fuze_nose_locked.md` flags this
        // explicitly: BBS unlock state and active theme are independent
        // persistence keys, and toggling one must not clobber the other.
        beginTest("setting theme does not clear bbs.unlocked");
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_test_bbs_indep_"
                                     + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();

        {
            bombo::PersistentState state(tmp);
            state.setBbsUnlocked(true);
            state.setActiveTheme("phosphor");
            state.setActiveTheme("nightrun");  // switch again to be sure
        }

        {
            bombo::PersistentState state(tmp);
            expect(state.getBbsUnlocked(),
                   "unlock state survives multiple theme switches");
            expectEquals(state.getActiveTheme(), juce::String("nightrun"),
                         "final theme persists");
        }

        tmp.deleteRecursively();
    }
};

class BbsProgressionKeysTest : public juce::UnitTest
{
public:
    BbsProgressionKeysTest() : juce::UnitTest("BBS: progression keys default + round-trip") {}
    void runTest() override
    {
        auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("bombo_test_prog_" + juce::String(juce::Time::currentTimeMillis()));
        tmp.createDirectory();

        beginTest("fresh state has zero saves, level 0, three unlocked sysops");
        {
            bombo::PersistentState s(tmp);
            expectEquals(s.getBbsSavesCount(), 0);
            expectEquals(s.getBbsLevel(), 0);
            expectEquals(s.getBbsUnlockedSysops(), juce::String("0,1,2"));
        }

        beginTest("saves + level + sysops survive reconstruction");
        {
            bombo::PersistentState s(tmp);
            s.setBbsSavesCount(17);
            s.setBbsLevel(2);
            s.setBbsUnlockedSysops("0,1,2,3,4");
        }
        {
            bombo::PersistentState s(tmp);
            expectEquals(s.getBbsSavesCount(), 17);
            expectEquals(s.getBbsLevel(), 2);
            expectEquals(s.getBbsUnlockedSysops(), juce::String("0,1,2,3,4"));
        }

        tmp.deleteRecursively();
    }
};

static BbsUnlockedDefaultTest             bbsUnlockedDefaultTest;
static BbsUnlockedRoundTripTest           bbsUnlockedRoundTripTest;
static BbsUnlockedIndependentOfThemeTest  bbsUnlockedIndependentOfThemeTest;
static BbsProgressionKeysTest             bbsProgressionKeysTest;

} // anonymous namespace
