// Source/GUI/BBS/Game/Game.h
#pragma once
#include "Constants.h"
#include "Framebuffer.h"
#include "Palette.h"
#include "Entities.h"
#include "Waves.h"
#include "Drops.h"
#include "Effects.h"
#include "Shop.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <random>
#include <vector>

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

    struct ChainState
    {
        void onKill() noexcept;
        void tick(float dt) noexcept;       // resets count to 0 after kChainDrainSec idle
        int  count() const noexcept { return count_; }
        int  peak()  const noexcept { return peak_; }
        void resetForWave() noexcept { peak_ = 0; count_ = 0; idle_ = 0.0f; }
    private:
        int   count_ = 0;
        int   peak_  = 0;
        float idle_  = 0.0f;
    };

    float chainMultiplierFor(int count) noexcept;

    class Game
    {
    public:
        Game();

        struct InputState { bool up = false, down = false, left = false, right = false; };

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
        void  setHostBpm(float bpm) noexcept { hostBpm_ = bpm; }

        // BPM speed multiplier: clamp(hostBpm/120, 0.5, 1.8).
        // Applied to spawn/movement rates starting Task 26; exposed here for tests.
        float speedMult() const noexcept;

        // --- Input plumbing (Task 7) ---
        void setMoveInput(const InputState& in) noexcept { input_ = in; }
        void setCharging(bool c) noexcept;          // begins/ends player charge
        bool releaseChargedShot() noexcept;         // on charge-key release; fires wide bullet if ready

        // Audio seam (Task 24): assigned by BBS to trigger the active preset/kick on each shot.
        // Null-safe — spawnPlayerShot() invokes it only when set.
        std::function<void()> onShot;

        // --- Test inspectors for the in-wave loop ---
        const Player&     player()        const noexcept { return player_; }
        const BulletPool& playerBullets() const noexcept { return playerBullets_; }
        const BulletPool& enemyShots()    const noexcept { return enemyShots_; }
        const EnemyPool&  enemies()       const noexcept { return enemies_; }
        const PickupPool& pickups()       const noexcept { return pickups_; }
        const ChainState& chain()         const noexcept { return chain_; }
        int  ownedItem(ShopItemId id)     const noexcept { return ownedItems_[(int) id]; }

       #if defined(BOMBO_GAME_TEST_HOOKS)
        // Test-only mutable accessors for headless integration tests (Task 7).
        // Compiled only when BOMBO_GAME_TEST_HOOKS is defined (the test target).
        Player&     testPlayer()        noexcept { return player_; }
        BulletPool& testPlayerBullets() noexcept { return playerBullets_; }
        BulletPool& testEnemyShots()    noexcept { return enemyShots_; }
        EnemyPool&  testEnemies()       noexcept { return enemies_; }
        PickupPool& testPickups()       noexcept { return pickups_; }
        void        testGrantItem(ShopItemId id, int n) noexcept { ownedItems_[(int) id] = n; }
        void        testClearWaveSchedule() noexcept { wave_ = WaveSchedule{}; }
       #endif

    private:
        void transitionTo(GameState s);

        // In-wave loop helpers (Task 7).
        void clampPlayerToField() noexcept;
        void spawnPlayerShot();
        void resolveCombat();
        bool enemyShotsHitPlayer() noexcept;
        void collectPickupsTouchingPlayer();
        void maybeSpawnDrop(EnemyKind kind, float x, float y);
        void grantRandomShopItem();
        int  scoreBaseFor(EnemyKind kind) const noexcept;

        GameState state_       = GameState::Title;
        GameState priorState_  = GameState::Title;   // for QuitConfirm cancel
        int       currentWave_ = 0;
        int       score_       = 0;
        int       lives_       = kPlayerStartLives;
        int       currencyDB_  = 0;
        bool      wantsExit_   = false;
        float     hostBpm_     = kBpmRef;
        uint32_t  runSeed_     = 0;

        // --- In-wave simulation state (Task 7) ---
        Player        player_;
        BulletPool    playerBullets_;
        BulletPool    enemyShots_;
        EnemyPool     enemies_;
        PickupPool    pickups_;
        ChainState    chain_;
        EffectState   effects_;
        WaveSchedule  wave_;
        std::array<int, 15> ownedItems_{};   // shop stacks, indexed by (int)ShopItemId
        int           tickCounter_ = 0;
        std::mt19937  runRng_;               // drop rolls + random-item grants (seeded from runSeed_)
        InputState    input_;
    };

    // Wave-clear bonus: timeRemaining*10 + peakChain*5 + livesRemaining*50
    int computeWaveClearBonus(int timeRemaining, int peakChain, int livesRemaining) noexcept;
}
