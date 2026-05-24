#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <vector>

namespace bombo
{

// Nokia Space Impact homage — side-scrolling auto-shooter hidden in the BBS.
// Not a juce::Component. BBSComponent owns one instance and drives it via
// tick() and paint() when BBSScreen::Game is active.
class BombImpactGame
{
public:
    std::function<void()>                    onKick;        // fires every shot (auto + charged)
    std::function<void(const juce::String&)> onBoomFeedLog; // easter egg log → BoomFeed

    bool isActive()    const noexcept { return state_ != State::Idle; }
    bool wantsExit()   const noexcept { return wantsExit_; }

    void startGame();
    void stopGame();
    void tick();                                            // call from timerCallback @50Hz
    void paint(juce::Graphics&, juce::Rectangle<int> content); // content = full BBS content area
    bool keyPressed(const juce::KeyPress&);                // returns true if consumed

    bool hyperfocusModeActive = false;

private:
    enum class State { Idle, Attract, Playing, WaveClear, GameOver, Complete };
    State state_     = State::Idle;
    int   stateTimer_= 0;
    int   wave_      = 0;   // 1-indexed once playing (1..3)
    bool  wantsExit_ = false;

    // Field geometry — updated at start of paint() each frame
    float fieldW_ = 360.0f, fieldH_ = 350.0f;

    // Ship — X range is kShipDefaultX +/- kShipRangeX (one ship-width each side)
    float shipX_ = 52.0f;
    float shipY_ = 175.0f;
    static constexpr float kShipDefaultX = 52.0f;
    static constexpr float kShipRangeX   = 30.0f; // px left/right dodge range
    static constexpr float kShipSpeed    = 0.035f; // fraction of fieldH_ per tick

    // Life-up pickups
    struct LifeUp { float x = 0.0f, y = 0.0f; bool alive = true; };
    std::vector<LifeUp> lifeUps_;
    int lifeUpSpawnCooldown_ = 0;

    struct Enemy {
        enum Type { Mudball, Clipper, SilenceVoid, Limiter } type;
        float x = 0.0f, y = 0.0f;
        float vx = -1.5f, phase = 0.0f;
        int   hp = 1;
        bool  alive = true;
    };
    struct Bullet {
        float x = 0.0f, y = 0.0f;
        bool  charged = false;
        bool  alive   = true;
    };
    std::vector<Enemy>  enemies_;
    std::vector<Bullet> bullets_;

    // Stats
    int score_ = 0, chain_ = 0, lives_ = 3;

    // Auto-fire
    int autoFireTick_ = 0;
    static constexpr int kAutoFireInterval = 16; // @50Hz → ~3.1 shots/s

    // Spawn system — pre-authored patterns per wave
    struct SpawnEntry { int tick; Enemy::Type type; float yFrac; };
    std::vector<SpawnEntry> spawnQueue_;
    int spawnTick_ = 0;

    // Easter eggs
    int  deathCount_      = 0;
    bool diedThisWave_    = false;
    bool nimerMode_       = false;
    bool overdriveActive_ = false;
    int  overdriveTick_   = 0;
    bool theRoomWave_     = false;
    bool vaultAccessEarned_ = false;
    static constexpr int kOverdriveDuration = 250; // ticks

    // Invincibility after being hit — ship blinks, collisions skipped
    int invincTick_ = 0;
    static constexpr int kInvincDuration = 150; // 3s at 50Hz

    // Visuals
    int  attractBlink_   = 0;
    int  bgScrollOffset_ = 0;
    juce::String toastMsg_;
    int  toastTick_  = 0;

    juce::Random rng_;

    // Helpers
    void buildSpawnQueue(int waveIdx);
    void updateEntities();
    void checkCollisions();
    void checkEasterEggs();
    void transitionTo(State);
    void fireAutoShot();
    void fireChargedShot();
    void addToast(const juce::String& msg, int ticks = 100);
    void killEnemy(Enemy& e, bool byShot);

    void paintHUD      (juce::Graphics&, juce::Rectangle<int>);
    void paintField    (juce::Graphics&, juce::Rectangle<int>);
    void paintAttract  (juce::Graphics&, juce::Rectangle<int>);
    void paintWaveClear(juce::Graphics&, juce::Rectangle<int>);
    void paintGameOver (juce::Graphics&, juce::Rectangle<int>);
    void paintComplete (juce::Graphics&, juce::Rectangle<int>);

    static const char*    enemyGlyph (Enemy::Type) noexcept;
    static juce::Colour   enemyColour(Enemy::Type) noexcept;
    static const juce::String& termFont();
};

} // namespace bombo
