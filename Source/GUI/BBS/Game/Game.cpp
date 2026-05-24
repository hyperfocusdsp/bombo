// Source/GUI/BBS/Game/Game.cpp
#include "Game.h"
#include <algorithm>

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

    float Game::speedMult() const noexcept
    {
        return std::max(kBpmMinMult, std::min(kBpmMaxMult, hostBpm_ / kBpmRef));
    }

    int computeWaveClearBonus(int timeRem, int peakChain, int lives) noexcept
    {
        return timeRem * 10 + peakChain * 5 + lives * 50;
    }

    void ChainState::onKill() noexcept
    {
        ++count_;
        if (count_ > peak_) peak_ = count_;
        idle_ = 0.0f;
    }

    void ChainState::tick(float dt) noexcept
    {
        if (count_ == 0) return;
        idle_ += dt;
        if (idle_ >= kChainDrainSec) { count_ = 0; idle_ = 0.0f; }
    }

    float chainMultiplierFor(int c) noexcept
    {
        if (c >= kChainThresholds[4]) return kChainMultipliers[4];
        if (c >= kChainThresholds[3]) return kChainMultipliers[3];
        if (c >= kChainThresholds[2]) return kChainMultipliers[2];
        if (c >= kChainThresholds[1]) return kChainMultipliers[1];
        return kChainMultipliers[0];
    }
}
