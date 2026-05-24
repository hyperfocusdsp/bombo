// Source/GUI/BBS/Game/Game.h
#pragma once
#include "Constants.h"
#include "Framebuffer.h"
#include "Palette.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace bombo::game
{
    enum class GameState
    {
        Title,
        Playing,
        Paused,
        Shop,
        WaveClear,
        Boss,
        GameOver,
        Initials,
        Results,
        QuitConfirm,
        Help,
        HighScores,
    };

    class Game
    {
    public:
        Game();

        // Lifecycle
        void startNewRun(bool dailySeed);
        void togglePause();
        void requestQuit();      // opens QuitConfirm modal from any state
        void confirmQuit();      // returns to Title, sets wantsExit_ to false
        void cancelQuit();       // returns to prior state

        // One simulation tick (1/60s by spec) — stub for now
        void tick();

        // Render current frame into framebuffer — placeholder until later tasks
        void renderInto(Framebuffer& fb, const Palette& palette) const;

        // Input entry points — stubs for now, wired in Tasks 7 and 22+
        bool handleKey(int juceKeyCode, juce::ModifierKeys mods);
        bool handleMouseClick(int fbX, int fbY);

        // Inspectors
        GameState state()       const noexcept { return state_; }
        int currentWave()       const noexcept { return currentWave_; }
        int score()             const noexcept { return score_; }
        int lives()             const noexcept { return lives_; }
        int currencyDB()        const noexcept { return currencyDB_; }
        bool wantsExit()        const noexcept { return wantsExit_; }

        // Tempo coupling (set from BomboProcessor in later tasks)
        void setHostBpm(float bpm) noexcept { hostBpm_ = bpm; }

    private:
        void transitionTo(GameState s);

        GameState state_       = GameState::Title;
        GameState priorState_  = GameState::Title;   // for QuitConfirm cancel
        int       currentWave_ = 0;
        int       score_       = 0;
        int       lives_       = kPlayerStartLives;
        int       currencyDB_  = 0;
        bool      wantsExit_   = false;
        float     hostBpm_     = kBpmRef;
        uint32_t  runSeed_     = 0;
    };
}
