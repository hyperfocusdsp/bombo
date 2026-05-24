// Source/GUI/BBS/Game/Game.cpp
#include "Game.h"

namespace bombo::game
{
    Game::Game() = default;

    void Game::transitionTo(GameState s)
    {
        priorState_ = state_;
        state_      = s;
    }

    void Game::startNewRun(bool /*dailySeed*/)
    {
        currentWave_ = 1;
        score_       = 0;
        lives_       = kPlayerStartLives;
        currencyDB_  = 0;
        // dailySeed handling deferred to Task 20 (HighScores + daily seed)
        runSeed_     = 1u;
        transitionTo(GameState::Playing);
    }

    void Game::togglePause()
    {
        if      (state_ == GameState::Playing) transitionTo(GameState::Paused);
        else if (state_ == GameState::Paused)  transitionTo(GameState::Playing);
    }

    void Game::requestQuit() { transitionTo(GameState::QuitConfirm); }

    void Game::confirmQuit()
    {
        transitionTo(GameState::Title);
        wantsExit_ = true;
    }

    void Game::cancelQuit()
    {
        jassert(state_ == GameState::QuitConfirm);
        // Return to whatever we were doing before QuitConfirm
        state_ = priorState_;
    }

    void Game::tick() { /* fleshed out in Task 7 and later */ }

    void Game::renderInto(Framebuffer& fb, const Palette& /*palette*/) const
    {
        fb.clear(0);
        fb.drawText("KICK IMPACT", 40, 40, 3);   // placeholder until Task 22
    }

    bool Game::handleKey(int /*key*/, juce::ModifierKeys /*mods*/) { return false; }
    bool Game::handleMouseClick(int /*fbX*/, int /*fbY*/)          { return false; }
}
