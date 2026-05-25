// tests/GameInputTests.cpp
// Pins the Task-7 input routing (handleKey state dispatch) and a render smoke
// test (every GameState draws something without crashing). Rendering itself is
// NOT pixel-verified here — a human must visually smoke-test in a DAW.
#include "GUI/BBS/Game/Game.h"
#include "GUI/BBS/Game/Framebuffer.h"
#include "GUI/BBS/Game/Palette.h"
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace
{
using namespace bombo::game;

using KP = juce::KeyPress;
static const juce::ModifierKeys kNoMods;

// Convenience: send a key code to the game.
bool send(Game& g, int code) { return g.handleKey(code, kNoMods); }

class TitleMenuRoutingTest : public juce::UnitTest
{
public:
    TitleMenuRoutingTest() : juce::UnitTest("GameInput: Title menu routing") {}
    void runTest() override
    {
        beginTest("Enter on NEW GAME (default selection) -> Playing");
        {
            Game g;
            expect(g.state() == GameState::Title);
            expect(send(g, KP::returnKey));
            expect(g.state() == GameState::Playing);
            expect(! g.testDaily());
        }

        beginTest("Down to DAILY RUN + Enter -> Playing + daily seed");
        {
            Game g;
            expect(send(g, KP::downKey));   // -> DAILY RUN
            expect(send(g, KP::returnKey));
            expect(g.state() == GameState::Playing);
            expect(g.testDaily());
        }

        beginTest("Down x2 to HIGHSCORES -> HighScores");
        {
            Game g;
            send(g, KP::downKey); send(g, KP::downKey);
            expect(send(g, KP::returnKey));
            expect(g.state() == GameState::HighScores);
        }

        beginTest("Up wraps to EXIT -> sets wantsExit");
        {
            Game g;
            expect(send(g, KP::upKey));      // wrap to EXIT (index 4)
            expect(send(g, KP::returnKey));
            expect(g.wantsExit());
        }
    }
};

class PlayingRoutingTest : public juce::UnitTest
{
public:
    PlayingRoutingTest() : juce::UnitTest("GameInput: Playing routing") {}
    void runTest() override
    {
        beginTest("P -> Paused");
        {
            Game g; g.startNewRun(false);
            expect(send(g, 'P'));
            expect(g.state() == GameState::Paused);
        }

        beginTest("A toggles autofire");
        {
            Game g; g.startNewRun(false);
            const bool before = g.player().autofireOn;
            expect(send(g, 'A'));
            expect(g.player().autofireOn != before);
            expect(send(g, 'a'));   // lowercase normalises too
            expect(g.player().autofireOn == before);
        }

        beginTest("ESC -> Paused; ESC again resumes");
        {
            Game g; g.startNewRun(false);
            expect(send(g, KP::escapeKey));
            expect(g.state() == GameState::Paused);
            expect(send(g, KP::escapeKey));     // ESC in Paused resumes
            expect(g.state() == GameState::Playing);
        }

        beginTest("H -> Help, F charges; arrows are consumed but do NOT nudge (movement is polled)");
        {
            Game g; g.startNewRun(false);
            // Movement in Playing/Boss is the polled setMoveInput source, NOT a
            // handleKey per-press nudge (avoids double-counting once BBSComponent
            // polls isKeyCurrentlyDown each tick). handleKey still consumes the
            // arrow so it doesn't leak, but the player position must not change.
            const float x0 = g.player().x;
            expect(send(g, KP::rightKey));       // consumed
            expect(g.player().x == x0);          // ...but no nudge
            // The actual movement path: setMoveInput + tick integrates velocity.
            Game::InputState in; in.right = true;
            g.setMoveInput(in);
            g.tick();
            expect(g.player().x > x0);           // polled input moves the player
            expect(send(g, 'F'));
            expect(g.player().charging);
            expect(send(g, 'H'));
            expect(g.state() == GameState::Help);
        }
    }
};

class QuitConfirmRoutingTest : public juce::UnitTest
{
public:
    QuitConfirmRoutingTest() : juce::UnitTest("GameInput: QuitConfirm routing") {}
    void runTest() override
    {
        beginTest("Y -> Title + wantsExit");
        {
            Game g; g.startNewRun(false);
            g.requestQuit();
            expect(g.state() == GameState::QuitConfirm);
            expect(send(g, 'Y'));
            expect(g.state() == GameState::Title);
            expect(g.wantsExit());
        }

        beginTest("N -> back to Playing");
        {
            Game g; g.startNewRun(false);
            g.requestQuit();
            expect(send(g, 'N'));
            expect(g.state() == GameState::Playing);
            expect(! g.wantsExit());
        }

        beginTest("ESC also quits");
        {
            Game g; g.startNewRun(false);
            g.requestQuit();
            expect(send(g, KP::escapeKey));
            expect(g.state() == GameState::Title);
            expect(g.wantsExit());
        }
    }
};

class HelpToggleRoutingTest : public juce::UnitTest
{
public:
    HelpToggleRoutingTest() : juce::UnitTest("GameInput: Help toggle") {}
    void runTest() override
    {
        beginTest("H in Playing -> Help; H in Help -> back to Playing");
        {
            Game g; g.startNewRun(false);
            expect(send(g, 'H'));
            expect(g.state() == GameState::Help);
            expect(send(g, 'H'));
            expect(g.state() == GameState::Playing);
        }

        beginTest("ESC in Help returns to prior state");
        {
            Game g; g.startNewRun(false);
            send(g, 'H');
            expect(g.state() == GameState::Help);
            expect(send(g, KP::escapeKey));
            expect(g.state() == GameState::Playing);
        }
    }
};

class ShopRoutingTest : public juce::UnitTest
{
public:
    ShopRoutingTest() : juce::UnitTest("GameInput: Shop routing") {}
    void runTest() override
    {
        beginTest("Space advances out of Shop");
        {
            // Drive into the shop via the W2 wave-clear cadence.
            Game g; g.startNewRun(false);
            g.testSetCurrentWave(2);
            g.testForceWaveClear();
            g.tick();   // Playing -> WaveClear (onWaveCleared)
            // Burn the WaveClear flash to reach the shop.
            int guard = 0;
            while (g.state() == GameState::WaveClear && guard++ < 1000) g.tick();
            expect(g.state() == GameState::Shop);

            const int waveBefore = g.currentWave();
            expect(send(g, KP::spaceKey));
            expect(g.state() != GameState::Shop);
            expect(g.currentWave() == waveBefore + 1);
        }

        beginTest("Left/Right move selection wrap; R/H consumed");
        {
            Game g; g.startNewRun(false);
            g.testSetCurrentWave(2);
            g.testForceWaveClear();
            g.tick();
            int guard = 0;
            while (g.state() == GameState::WaveClear && guard++ < 1000) g.tick();
            expect(g.state() == GameState::Shop);
            expect(send(g, KP::rightKey));
            expectEquals(g.shopSelectedSlot(), 1);
            expect(send(g, KP::leftKey));
            expectEquals(g.shopSelectedSlot(), 0);
            expect(send(g, 'H'));   // free heal consumed
        }
    }
};

class InitialsRoutingTest : public juce::UnitTest
{
public:
    InitialsRoutingTest() : juce::UnitTest("GameInput: Initials entry") {}
    void runTest() override
    {
        beginTest("cycle letters to ABC, confirm -> Results");
        {
            // Force a qualifying score so onGameOver routes to Initials.
            Game g;
            g.testSetHighScoresPath(juce::File{});   // in-memory table
            g.startNewRun(false);
            g.testSetScore(99999);
            g.testSetLives(1);
            // Kill the player to reach game-over -> Initials.
            // Easiest: directly drive via the public requestQuit? No — need death.
            // Force lives to 0 path by spawning an enemy shot on the player.
            auto& shots = g.testEnemyShots();
            shots.spawn(g.player().x, g.player().y, 0.0f, 0.0f, 1);
            g.testSetLives(1);
            g.tick();   // resolveCombat -> takeHit -> lives 0 -> onGameOver
            expect(g.state() == GameState::Initials);

            // New mapping: Left/Right move between slots; Up/Down scroll the
            // alphabet at the current slot (Up = next letter).
            // Slot 0 starts at 'A'. Leave it. Move to slot 1, cycle to 'B'.
            expect(send(g, KP::rightKey));  // slot -> 1
            expectEquals(g.initialsSlot(), 1);
            expect(send(g, KP::upKey));     // 'A' -> 'B'
            expect(send(g, KP::rightKey));  // slot -> 2
            expect(send(g, KP::upKey));     // 'A' -> 'B'
            expect(send(g, KP::upKey));     // 'B' -> 'C'
            const auto in = g.initials();
            expectEquals((int) in[0], (int) 'A');
            expectEquals((int) in[1], (int) 'B');
            expectEquals((int) in[2], (int) 'C');
            expect(send(g, KP::returnKey)); // confirm
            expect(g.state() == GameState::Results);
        }

        beginTest("typing letters fills slots and auto-advances");
        {
            Game g;
            g.testSetHighScoresPath(juce::File{});
            g.startNewRun(false);
            g.testSetScore(99999);
            g.testSetLives(1);
            g.testEnemyShots().spawn(g.player().x, g.player().y, 0.0f, 0.0f, 1);
            g.tick();
            expect(g.state() == GameState::Initials);

            expect(send(g, 'D'));   // slot 0 = D, advance -> 1
            expect(send(g, 'A'));   // slot 1 = A, advance -> 2
            expect(send(g, 'Z'));   // slot 2 = Z (clamped at last slot)
            const auto in = g.initials();
            expectEquals((int) in[0], (int) 'D');
            expectEquals((int) in[1], (int) 'A');
            expectEquals((int) in[2], (int) 'Z');
        }
    }
};

class ResultsRoutingTest : public juce::UnitTest
{
public:
    ResultsRoutingTest() : juce::UnitTest("GameInput: Results -> Title on any key") {}
    void runTest() override
    {
        Game g;
        g.testSetHighScoresPath(juce::File{});
        g.startNewRun(false);
        g.testSetScore(1);   // won't qualify, but force Results via initials path
        // Reach Results directly: confirm from Initials requires Initials state.
        // Instead drive a non-qualifying game-over: spawn shot, die -> Results.
        g.testEnemyShots().spawn(g.player().x, g.player().y, 0.0f, 0.0f, 1);
        g.testSetLives(1);
        // Stack the high score table so score=1 does NOT qualify.
        // (in-memory empty table => qualifies; so route through Initials+confirm.)
        g.tick();
        if (g.state() == GameState::Initials)
            g.initialsConfirm();
        beginTest("any key in Results -> Title");
        expect(g.state() == GameState::Results);
        expect(send(g, KP::returnKey));
        expect(g.state() == GameState::Title);
    }
};

// Render smoke: every GameState must draw at least one non-zero pixel without
// crashing. NOTE: this does NOT validate the visual look — human smoke-test
// required in a DAW/standalone.
class RenderSmokeTest : public juce::UnitTest
{
public:
    RenderSmokeTest() : juce::UnitTest("GameInput: renderInto smoke (all states)") {}

    static bool anyNonZero(const Framebuffer& fb)
    {
        for (int y = 0; y < Framebuffer::height(); ++y)
            for (int x = 0; x < Framebuffer::width(); ++x)
                if (fb.peek(x, y) != 0) return true;
        return false;
    }

    void check(const char* label, Game& g, GameState expected)
    {
        beginTest(juce::String("renders: ") + label);
        expect(g.state() == expected);
        Framebuffer fb;
        const Palette pal = getGamePalette("vault");
        g.renderInto(fb, pal);
        expect(anyNonZero(fb), juce::String(label) + " drew nothing");
    }

    void runTest() override
    {
        const Palette pal = getGamePalette("vault");

        // Title (fresh game).
        { Game g; check("Title", g, GameState::Title); }

        // Playing.
        { Game g; g.startNewRun(false); check("Playing", g, GameState::Playing); }

        // Playing with live entities (exercises sprite/bullet/pickup paths).
        {
            Game g; g.startNewRun(false);
            g.testEnemies().spawn(EnemyKind::Mudball, 100.0f, 50.0f, 0.0f, 0.0f);
            g.testEnemies().spawn(EnemyKind::Aliaser, 120.0f, 30.0f, 0.0f, 0.0f);
            g.testPlayerBullets().spawn(40.0f, 56.0f, 100.0f, 0.0f, 1);
            g.testEnemyShots().spawn(80.0f, 56.0f, -100.0f, 0.0f, 1);
            g.testPickups().spawn(Pickup::Kind::DbSmall, 70.0f, 60.0f);
            g.testPickups().spawn(Pickup::Kind::Mystery, 90.0f, 40.0f);
            Framebuffer fb; g.renderInto(fb, pal);
            beginTest("renders: Playing with entities");
            expect(anyNonZero(fb));
        }

        // Paused.
        { Game g; g.startNewRun(false); g.togglePause();
          check("Paused", g, GameState::Paused); }

        // WaveClear.
        {
            Game g; g.startNewRun(false);
            g.testForceWaveClear(); g.tick();
            check("WaveClear", g, GameState::WaveClear);
        }

        // Shop.
        {
            Game g; g.startNewRun(false);
            g.testSetCurrentWave(2); g.testForceWaveClear(); g.tick();
            int guard = 0;
            while (g.state() == GameState::WaveClear && guard++ < 1000) g.tick();
            check("Shop", g, GameState::Shop);
        }

        // Boss (clearing the last normal wave, kBossWave-1 == 11, enters boss).
        {
            Game g; g.startNewRun(false);
            g.testSetCurrentWave(11); g.testForceWaveClear(); g.tick();
            int guard = 0;
            while (g.state() == GameState::WaveClear && guard++ < 1000) g.tick();
            check("Boss", g, GameState::Boss);
        }

        // Help.
        { Game g; g.startNewRun(false); g.handleKey('H', kNoMods);
          check("Help", g, GameState::Help); }

        // HighScores (empty table still draws "NO SCORES YET").
        {
            Game g; g.handleKey(KP::downKey, kNoMods);
            g.handleKey(KP::downKey, kNoMods);   // -> HIGHSCORES
            g.handleKey(KP::returnKey, kNoMods);
            check("HighScores", g, GameState::HighScores);
        }

        // QuitConfirm.
        { Game g; g.startNewRun(false); g.requestQuit();
          check("QuitConfirm", g, GameState::QuitConfirm); }

        // Initials.
        {
            Game g; g.testSetHighScoresPath(juce::File{});
            g.startNewRun(false);
            g.testSetScore(99999);
            g.testEnemyShots().spawn(g.player().x, g.player().y, 0.0f, 0.0f, 1);
            g.testSetLives(1);
            g.tick();
            check("Initials", g, GameState::Initials);
        }

        // Results / GameOver.
        {
            Game g; g.testSetHighScoresPath(juce::File{});
            g.startNewRun(false);
            g.testEnemyShots().spawn(g.player().x, g.player().y, 0.0f, 0.0f, 1);
            g.testSetLives(1);
            g.tick();
            if (g.state() == GameState::Initials) g.initialsConfirm();
            check("Results", g, GameState::Results);
        }
    }
};

static TitleMenuRoutingTest   t1;
static PlayingRoutingTest     t2;
static QuitConfirmRoutingTest t3;
static HelpToggleRoutingTest  t4;
static ShopRoutingTest        t5;
static InitialsRoutingTest    t6;
static ResultsRoutingTest     t7;
static RenderSmokeTest        t8;
}
