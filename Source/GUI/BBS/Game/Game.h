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
#include "HighScores.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <functional>
#include <memory>
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
        // Suppress the live chain count without touching the run-peak. Used by
        // SilenceVoid body contact (spec §6.1) — the void "drains" your energy
        // while you're near it, but the peak you earned still counts for scoring.
        void drain() noexcept { count_ = 0; idle_ = 0.0f; }
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

        // Lifecycle. ngPlusTier > 0 starts a New Game+ run: every enemy/boss gets
        // +tier HP and a faster approach. A plain NEW GAME passes 0.
        void startNewRun(bool dailySeed, int ngPlusTier = 0);
        int  ngPlus() const noexcept { return ngPlus_; }
        void togglePause();
        void requestQuit();      // opens QuitConfirm modal from any state
        void confirmQuit();      // returns to Title, sets wantsExit_ = true (BBS polls it to tear down)
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
        void fireManualShot() noexcept;             // tap fire: one normal bullet (works with autofire off)

        // Audio seam (Task 24): assigned by BBS to trigger the active preset/kick on each shot.
        // Null-safe — spawnPlayerShot() invokes it only when set.
        std::function<void()> onShot;

        // Procedural-SFX seams (Task 24): purely additive fire-and-forget hooks
        // emitted at existing game-logic event points. BBS assigns them to drive
        // the processor's GameAudioBus. All null-safe — the game never depends on
        // them being set, so game logic and the headless tests are unaffected.
        std::function<void(EnemyKind)> onEnemyHit;     // an enemy was destroyed
        std::function<void()>          onWaveClear;    // a wave was cleared
        std::function<void(bool)>      onGameOverFx;   // run ended (true = victory)
        std::function<void(DropTier)>  onPickup;       // a pickup was collected
        std::function<void()>          onBossTelegraph;// boss (RUMBLR) spawned
        std::function<void(bool)>      onMusicToggle;  // MUSIC menu item flipped (true = on)

        // Music on/off menu state. BBS seeds it from the persisted setting at
        // launch (setMusicOn); the MUSIC menu item flips it via onMusicToggle.
        void setMusicOn(bool on) noexcept { musicOn_ = on; }
        bool musicOn() const noexcept { return musicOn_; }

        // --- Shop interaction (input layer / Task 22 calls these) ---
        // Valid only while state()==Shop. Slot selection is 0..2.
        int  shopSelectedSlot()       const noexcept { return shopSlot_; }
        void shopMoveSelection(int delta) noexcept;
        bool shopBuySelected();                 // buy() deducts dB; returns true on purchase
        bool shopReroll();                      // deducts the PRE-reroll cost exactly once
        bool shopUseFreeHeal();                 // +1 life (max kPlayerMaxLives), once per visit
        bool shopFreeHealAvailable()  const noexcept { return shopFreeHealUsed_ == false; }
        void shopContinue();                    // leave shop -> next wave Playing
        const ShopVisit* shop()       const noexcept { return shop_.get(); }

        // --- Game-over / Initials / Results (input layer calls these) ---
        bool gameOverVictory()        const noexcept { return victory_; }
        // Initials entry — 3 slots, each 'A'..'Z'.
        void initialsCycleLetter(int slot, int delta) noexcept;
        void initialsMoveSlot(int delta) noexcept;
        void initialsConfirm();                 // records run, saves, -> Results
        std::array<char, 3> initials() const noexcept { return initials_; }
        int  initialsSlot()           const noexcept { return initialsSlot_; }
        void resultsContinue();                 // Results -> Title

        // The top-10 table, for the HighScores screen renderer (Task 22).
        const HighScores& highScores() const noexcept { return highScores_; }

        // --- Test inspectors for the in-wave loop ---
        const Player&     player()        const noexcept { return player_; }
        const BulletPool& playerBullets() const noexcept { return playerBullets_; }
        const BulletPool& enemyShots()    const noexcept { return enemyShots_; }
        const EnemyPool&  enemies()       const noexcept { return enemies_; }
        const PickupPool& pickups()       const noexcept { return pickups_; }
        const ChainState& chain()         const noexcept { return chain_; }
        int  ownedItem(ShopItemId id)     const noexcept { return ownedItems_[(int) id]; }
        int  weaponLevel()                const noexcept { return weaponLevel_; }

       #if defined(BOMBO_GAME_TEST_HOOKS)
        // Test-only mutable accessors for headless integration tests (Task 7).
        // Compiled only when BOMBO_GAME_TEST_HOOKS is defined (the test target).
        Player&     testPlayer()        noexcept { return player_; }
        BulletPool& testPlayerBullets() noexcept { return playerBullets_; }
        BulletPool& testEnemyShots()    noexcept { return enemyShots_; }
        EnemyPool&  testEnemies()       noexcept { return enemies_; }
        PickupPool& testPickups()       noexcept { return pickups_; }
        void        testGrantItem(ShopItemId id, int n) noexcept { ownedItems_[(int) id] = n; }
        void        testSetWeaponLevel(int n) noexcept { weaponLevel_ = n; }
        void        testClearWaveSchedule() noexcept { wave_ = WaveSchedule{}; }
        void        testSetLives(int n) noexcept { lives_ = n; }
        void        testSetScore(int n) noexcept { score_ = n; }
        void        testSetCurrencyDB(int n) noexcept { currencyDB_ = n; }
        void        testSetCurrentWave(int n) noexcept { currentWave_ = n; }
        // Point highScores_ at an explicit file (temp file in tests, or {} for in-memory).
        void        testSetHighScoresPath(juce::File f) { highScores_ = HighScores(std::move(f)); }
        // Force the wave-clear path (schedule emptied + all non-boss enemies cleared).
        void        testForceWaveClear() noexcept;
        uint32_t    testRunSeed() const noexcept { return runSeed_; }
        bool        testDaily()   const noexcept { return daily_; }

        // Sim-only: start a run with a FORCED seed so headless runs are
        // deterministic + reproducible (the balance sim drives this). Mirrors
        // startNewRun(false) exactly except the seed comes from the caller
        // instead of std::random_device. daily flag is left false so the
        // high-score "daily" bookkeeping path is untouched.
        void testStartRunWithSeed(uint32_t seed) noexcept
        {
            currentWave_ = 1;
            score_       = 0;
            lives_       = kPlayerStartLives;
            currencyDB_  = 0;
            daily_       = false;
            victory_     = false;
            runSeed_     = (seed == 0u) ? 1u : seed;   // determinism contract: never 0

            player_        = Player{};
            player_.x      = 30.0f;
            player_.y      = static_cast<float>(kFbH) / 2.0f;
            playerBullets_ = BulletPool{};
            enemyShots_    = BulletPool{};
            enemies_       = EnemyPool{};
            pickups_       = PickupPool{};
            chain_         = ChainState{};
            effects_       = EffectState{};
            ownedItems_    = {};
            weaponLevel_   = 0;
            ngPlus_        = 0;
            enemies_.setHpBonus(0);
            tickCounter_   = 0;
            runRng_.seed(runSeed_);
            wave_          = scheduleWave(runSeed_, currentWave_);

            waveClearTicks_   = 0;
            shop_.reset();
            shopSlot_         = 0;
            shopFreeHealUsed_ = false;
            initials_         = { 'A', 'A', 'A' };
            initialsSlot_     = 0;

            transitionTo(GameState::Playing);
        }
       #endif

    private:
        void transitionTo(GameState s);

        // --- Run-flow helpers (this task) ---
        bool waveIsClear() const noexcept;          // schedule done + no live non-boss enemies
        void onWaveCleared();                       // bonus + chain reset + -> WaveClear
        void advanceAfterWaveClear();               // WaveClear -> next wave / Shop / Boss
        void advanceAfterShop();                    // Shop continue -> next wave Playing
        void beginWave();                           // dispatch currentWave_: Normal vs encounter
        void enterShop();                           // create ShopVisit, freeze sim
        void enterBoss();                           // spawn the encounter for currentWave_
        void toggleMusic();                         // flip musicOn_ + fire onMusicToggle
        void onGameOver(bool victory);              // death or boss win -> GameOver / Initials
        bool bossIsDead() const noexcept;           // no active Rumblr remains

        // In-wave loop helpers (Task 7).
        void clampPlayerToField() noexcept;
        void spawnPlayerShot();
        void resolveCombat();
        bool enemyShotsHitPlayer() noexcept;
        // Enemy-body contact pass (spec §6.1). Returns true if a damaging (life-
        // costing) contact occurred this tick. SilenceVoid contact drains the
        // chain (handled inside) but is NOT lethal, so it does not set the result.
        bool resolveEnemyBodyContact() noexcept;
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
        bool      daily_       = false;
        bool      victory_     = false;
        float     hostBpm_     = kBpmRef;
        uint32_t  runSeed_     = 0;

        // WaveClear flash countdown (ticks). 1.5s per spec §4.4.
        int       waveClearTicks_ = 0;

        // Shop visit state (only valid in Shop state).
        std::unique_ptr<ShopVisit> shop_;
        int       shopSlot_         = 0;
        bool      shopFreeHealUsed_ = false;

        // Initials entry state.
        std::array<char, 3> initials_ { 'A', 'A', 'A' };
        int       initialsSlot_ = 0;

        // Menu-selection cursors (Task 7 input layer). Bounds-wrapped on move.
        // Title: 0=NEW GAME 1=DAILY RUN 2=HIGHSCORES 3=HELP 4=EXIT.
        // Pause: 0=RESUME 1=RESTART 2=HIGHSCORES 3=HELP 4=QUIT.
        int       titleSel_ = 0;
        int       pauseSel_ = 0;

        // High-score table (persisted). Path-injectable for tests via testSetHighScoresPath.
        HighScores highScores_ { defaultHighScoresPath() };

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
        // Firepower upgrade level from the DoubleShot drop. Unlike shop items
        // (ownedItems_, which persist for the whole run) this is a transient
        // run-state field that resets to 0 whenever a life is lost. 0 = single
        // shot, 1 = double; leaves headroom for a future triple (2).
        int           weaponLevel_ = 0;
        // NG+ tier the current run is played at (0 = base). Each tier gives every
        // enemy/boss +1 HP and a faster approach. Beating the final boss bumps the
        // persisted max tier and offers a restart at tier+1 (see B4 / GameProgress).
        int           ngPlus_ = 0;
        bool          musicOn_ = false;   // game-music toggle (persisted by BBS)
        int           tickCounter_ = 0;
        std::mt19937  runRng_;               // drop rolls + random-item grants (seeded from runSeed_)
        InputState    input_;
    };

    // Wave-clear bonus: timeRemaining*10 + peakChain*5 + livesRemaining*50
    int computeWaveClearBonus(int timeRemaining, int peakChain, int livesRemaining) noexcept;
}
