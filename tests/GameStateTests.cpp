// tests/GameStateTests.cpp
#include "GUI/BBS/Game/Game.h"
#include <juce_core/juce_core.h>

namespace
{
using namespace bombo::game;

class GameStartsAtTitleTest : public juce::UnitTest
{
public:
    GameStartsAtTitleTest() : juce::UnitTest("Game: starts at TITLE") {}
    void runTest() override
    {
        beginTest("freshly constructed Game is in Title state");
        Game g;
        expect(g.state() == GameState::Title);
    }
};

class GameTitleToPlayingTest : public juce::UnitTest
{
public:
    GameTitleToPlayingTest() : juce::UnitTest("Game: TITLE -> PLAYING via startNewRun()") {}
    void runTest() override
    {
        beginTest("startNewRun transitions to Playing and resets wave to 1");
        Game g;
        g.startNewRun(/*daily=*/false);
        expect(g.state() == GameState::Playing);
        expectEquals(g.currentWave(), 1);
        expectEquals(g.score(), 0);
        expectEquals(g.lives(), kPlayerStartLives);
    }
};

class GamePauseResumeTest : public juce::UnitTest
{
public:
    GamePauseResumeTest() : juce::UnitTest("Game: PLAYING <-> PAUSED toggle") {}
    void runTest() override
    {
        beginTest("togglePause flips between Playing and Paused");
        Game g;
        g.startNewRun(false);
        g.togglePause();
        expect(g.state() == GameState::Paused);
        g.togglePause();
        expect(g.state() == GameState::Playing);
    }
};

class GameQuitConfirmFlowTest : public juce::UnitTest
{
public:
    GameQuitConfirmFlowTest() : juce::UnitTest("Game: ESC -> QuitConfirm, confirmQuit -> Title") {}
    void runTest() override
    {
        beginTest("requestQuit opens QuitConfirm, confirmQuit returns to Title");
        Game g;
        g.startNewRun(false);
        g.requestQuit();
        expect(g.state() == GameState::QuitConfirm);
        g.confirmQuit();
        expect(g.state() == GameState::Title);

        beginTest("confirmQuit sets wantsExit so the BBS component tears the game down");
        Game g2;
        g2.startNewRun(false);
        expect(! g2.wantsExit());      // fresh run, no exit signal
        g2.requestQuit();
        expect(! g2.wantsExit());      // pending confirm, still no exit signal
        g2.confirmQuit();
        expect(g2.wantsExit());        // confirmed -> signal BBS to exit
    }
};

class GameCancelQuitReturnsToPriorTest : public juce::UnitTest
{
public:
    GameCancelQuitReturnsToPriorTest() : juce::UnitTest("Game: cancelQuit returns to prior state") {}
    void runTest() override
    {
        beginTest("cancelQuit from QuitConfirm returns to whichever state preceded it");
        Game g;
        g.startNewRun(false);
        g.requestQuit();
        expect(g.state() == GameState::QuitConfirm);
        g.cancelQuit();
        expect(g.state() == GameState::Playing);
    }
};

static GameStartsAtTitleTest             a;
static GameTitleToPlayingTest            b;
static GamePauseResumeTest               c;
static GameQuitConfirmFlowTest           d;
static GameCancelQuitReturnsToPriorTest  e;

class BpmScalingTest : public juce::UnitTest
{
public:
    BpmScalingTest() : juce::UnitTest("Game: BPM scales speedMult, clamped") {}
    void runTest() override
    {
        beginTest("speedMult = clamp(bpm/120, 0.5, 1.8)");
        Game g;
        g.setHostBpm(60.0f);
        expectWithinAbsoluteError(g.speedMult(), 0.5f, 0.001f);   // 60/120 = 0.5, at lower clamp boundary
        g.setHostBpm(120.0f);
        expectWithinAbsoluteError(g.speedMult(), 1.0f, 0.001f);
        g.setHostBpm(240.0f);
        expectWithinAbsoluteError(g.speedMult(), 1.8f, 0.001f);   // 2.0 clamped to 1.8
        g.setHostBpm(30.0f);
        expectWithinAbsoluteError(g.speedMult(), 0.5f, 0.001f);   // 0.25 clamped to 0.5
    }
};

class WaveClearBonusTest : public juce::UnitTest
{
public:
    WaveClearBonusTest() : juce::UnitTest("Game: wave clear bonus formula") {}
    void runTest() override
    {
        beginTest("bonus = timeRemaining*10 + peakChain*5 + livesRemaining*50");
        const int b = computeWaveClearBonus(/*timeRem=*/12, /*peakChain=*/8, /*lives=*/3);
        expectEquals(b, 12*10 + 8*5 + 3*50);
    }
};
static BpmScalingTest a_bpm;
static WaveClearBonusTest b_bonus;
}
