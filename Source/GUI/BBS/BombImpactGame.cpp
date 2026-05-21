#include "BombImpactGame.h"
#include <cmath>

namespace bombo
{

// ─────────────────────────────────────────────────────────────────────────────
const juce::String& BombImpactGame::termFont()
{
    static const juce::String name = []
    {
        for (const auto& n : juce::Font::findAllTypefaceNames())
            if (n.containsIgnoreCase("JetBrainsMono") || n.startsWithIgnoreCase("JetBrains Mono"))
                return n;
        return juce::String("Courier New");
    }();
    return name;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void BombImpactGame::startGame()
{
    state_         = State::Attract;
    stateTimer_    = 75;  // 1.5 s @50Hz
    wave_          = 0;
    score_         = 0;
    chain_         = 0;
    lives_         = 3;
    deathCount_    = 0;
    nimerMode_     = false;
    overdriveActive_ = false;
    vaultAccessEarned_ = false;
    wantsExit_     = false;
    attractBlink_  = 0;
    bgScrollOffset_= 0;
    toastTick_     = 0;
    enemies_.clear();
    bullets_.clear();
}

void BombImpactGame::stopGame()
{
    state_     = State::Idle;
    wantsExit_ = false;
    enemies_.clear();
    bullets_.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Input
// ─────────────────────────────────────────────────────────────────────────────

bool BombImpactGame::keyPressed(const juce::KeyPress& key)
{
    if (!isActive()) return false;

    if (key == juce::KeyPress::escapeKey) { wantsExit_ = true; return true; }

    if (state_ == State::Attract)
    {
        transitionTo(State::Playing);
        return true;
    }
    if (state_ != State::Playing) return true; // swallow in end-screens

    const float step = fieldH_ * 0.13f;
    if (key == juce::KeyPress::upKey)
    {
        shipTargetY_ = juce::jmax(10.0f, shipTargetY_ - step);
        return true;
    }
    if (key == juce::KeyPress::downKey)
    {
        shipTargetY_ = juce::jmin(fieldH_ - 10.0f, shipTargetY_ + step);
        return true;
    }
    const auto ch = juce::CharacterFunctions::toLowerCase(key.getTextCharacter());
    if (ch == 't') { fireChargedShot(); return true; }

    return true; // swallow everything while game is running
}

// ─────────────────────────────────────────────────────────────────────────────
// Game loop
// ─────────────────────────────────────────────────────────────────────────────

void BombImpactGame::tick()
{
    if (!isActive()) return;

    ++bgScrollOffset_;
    if (toastTick_ > 0) --toastTick_;

    switch (state_)
    {
        case State::Attract:
            ++attractBlink_;
            if (--stateTimer_ <= 0) transitionTo(State::Playing);
            break;

        case State::Playing:
        {
            // Smooth ship movement
            shipY_ += (shipTargetY_ - shipY_) * 0.22f;

            // Auto-fire
            if (++autoFireTick_ >= kAutoFireInterval)
            {
                autoFireTick_ = 0;
                fireAutoShot();
            }

            // Spawn enemies from queue
            for (const auto& s : spawnQueue_)
                if (spawnTick_ == s.tick)
                    enemies_.push_back({ s.type, fieldW_ + 12.0f, s.yFrac * fieldH_,
                                         s.type == Enemy::Clipper  ? -2.1f :
                                         s.type == Enemy::SilenceVoid ? -0.9f :
                                         s.type == Enemy::Limiter  ? -0.35f : -1.6f,
                                         0.0f,
                                         s.type == Enemy::Clipper  ? 2 :
                                         s.type == Enemy::Limiter  ? 15 : 1 });
            ++spawnTick_;

            updateEntities();
            checkCollisions();
            checkEasterEggs();

            if (overdriveTick_ > 0 && --overdriveTick_ == 0) overdriveActive_ = false;

            // Wave complete when all spawns done and no killable enemies remain
            if (!spawnQueue_.empty() && spawnTick_ > spawnQueue_.back().tick)
            {
                bool anyKillable = false;
                for (const auto& e : enemies_)
                    if (e.alive && e.type != Enemy::SilenceVoid) { anyKillable = true; break; }
                if (!anyKillable) transitionTo(State::WaveClear);
            }
            break;
        }

        case State::WaveClear:
            if (--stateTimer_ <= 0)
            {
                if (wave_ < 3) transitionTo(State::Playing);
                else           transitionTo(State::Complete);
            }
            break;

        case State::GameOver:
        case State::Complete:
            if (--stateTimer_ <= 0) wantsExit_ = true;
            break;

        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Entity updates
// ─────────────────────────────────────────────────────────────────────────────

void BombImpactGame::updateEntities()
{
    for (auto& e : enemies_)
    {
        if (!e.alive) continue;
        e.x += e.vx;

        if (e.type == Enemy::Clipper)
        {
            e.phase += 0.065f;
            e.y     += std::sin(e.phase) * 2.0f;
            e.y      = juce::jlimit(8.0f, fieldH_ - 8.0f, e.y);
        }
        // Limiter halts at ~55% from left
        if (e.type == Enemy::Limiter && e.x < fieldW_ * 0.55f)
            e.vx = 0.0f;

        if (e.x < -20.0f) e.alive = false;
    }

    for (auto& b : bullets_)
    {
        if (!b.alive) continue;
        b.x += b.charged ? 7.0f : 5.5f;
        if (b.x > fieldW_ + 12.0f) b.alive = false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Collisions
// ─────────────────────────────────────────────────────────────────────────────

void BombImpactGame::checkCollisions()
{
    // Ship vs enemies
    for (auto& e : enemies_)
    {
        if (!e.alive || e.type == Enemy::SilenceVoid) continue;
        const float dx = std::abs(e.x - kShipX), dy = std::abs(e.y - shipY_);
        const float hw = e.type == Enemy::Limiter ? 18.0f : 13.0f;
        const float hh = e.type == Enemy::Limiter ? fieldH_ * 0.35f : 10.0f;
        if (dx < hw && dy < hh)
        {
            e.alive = false;
            chain_  = 0;
            diedThisWave_ = true;
            ++deathCount_;
            if (--lives_ <= 0) { lives_ = 0; transitionTo(State::GameOver); return; }
        }
    }

    // Bullets vs enemies
    for (auto& b : bullets_)
    {
        if (!b.alive) continue;
        for (auto& e : enemies_)
        {
            if (!e.alive) continue;
            // SilenceVoid absorbs shots (invincible, bullet dies)
            if (e.type == Enemy::SilenceVoid)
            {
                if (std::abs(e.x - b.x) < 15.0f && std::abs(e.y - b.y) < 17.0f)
                    b.alive = false;
                continue;
            }
            const float hw = e.type == Enemy::Limiter ? 8.0f  : 12.0f;
            const float hh = e.type == Enemy::Limiter ? fieldH_ * 0.38f : 10.0f;
            if (std::abs(e.x - b.x) < hw && std::abs(e.y - b.y) < hh)
            {
                b.alive = false;
                killEnemy(e, true);
                break;
            }
        }
    }
}

void BombImpactGame::killEnemy(Enemy& e, bool byShot)
{
    if (!e.alive) return;
    if (--e.hp > 0) return;
    e.alive = false;
    if (!byShot) return;

    ++chain_;
    const int multiplier = juce::jmax(1, chain_ / 3 + 1);
    const int pts = (e.type == Enemy::Clipper ? 200 : e.type == Enemy::Limiter ? 800 : 100)
                    * multiplier;
    score_ += pts;
}

// ─────────────────────────────────────────────────────────────────────────────
// Easter eggs
// ─────────────────────────────────────────────────────────────────────────────

void BombImpactGame::checkEasterEggs()
{
    if (chain_ >= 9 && !overdriveActive_)
    {
        overdriveActive_ = true;
        overdriveTick_   = kOverdriveDuration;
        addToast("OVERDRIVE", 130);
        if (onBoomFeedLog) onBoomFeedLog("[OVERDRIVE] CHAIN x9 -- DRIVE MAXED");
    }
    if (deathCount_ == 9 && !nimerMode_)
    {
        nimerMode_ = true;
        addToast("NINER MODE", 110);
        if (onBoomFeedLog) onBoomFeedLog("[NINER] 9 DEATHS LOGGED");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// State transitions
// ─────────────────────────────────────────────────────────────────────────────

void BombImpactGame::transitionTo(State s)
{
    if (state_ == s) return;
    state_ = s;

    switch (s)
    {
        case State::Playing:
            ++wave_;
            diedThisWave_ = false;
            enemies_.clear();
            bullets_.clear();
            spawnTick_    = 0;
            autoFireTick_ = 0;
            shipTargetY_  = fieldH_ * 0.5f;
            shipY_        = fieldH_ * 0.5f;
            theRoomWave_  = (rng_.nextInt(80) == 0);
            if (theRoomWave_)
            {
                addToast("THE ROOM", 200);
                if (onBoomFeedLog) onBoomFeedLog("[THE ROOM] HEADROOM PRESERVED");
            }
            buildSpawnQueue(wave_ - 1);
            break;

        case State::WaveClear:
            stateTimer_ = 100;
            if (wave_ == 3 && !diedThisWave_)
            {
                vaultAccessEarned_ = true;
                addToast("VAULT ACCESS", 160);
                if (onBoomFeedLog) onBoomFeedLog("[VAULT] WAVE 3 CLEAN -- EDEN UNLOCKED");
            }
            for (const auto& e : enemies_)
                if (e.type == Enemy::Limiter && !e.alive)
                {
                    addToast("BRICK WALL", 120);
                    if (onBoomFeedLog) onBoomFeedLog("[ACHIEVEMENT] BRICK WALL -- COMPRESSION DEFEATED");
                }
            break;

        case State::GameOver:
            stateTimer_ = 200;
            if (onBoomFeedLog) onBoomFeedLog("[KICK IMPACT] SIGNAL LOST -- SCORE: " + juce::String(score_));
            break;

        case State::Complete:
            stateTimer_ = 250;
            if (onBoomFeedLog) onBoomFeedLog("[KICK IMPACT] MISSION COMPLETE -- SCORE: " + juce::String(score_));
            break;

        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Spawn queues -- authored wave patterns
// ─────────────────────────────────────────────────────────────────────────────

void BombImpactGame::buildSpawnQueue(int waveIdx)
{
    spawnQueue_.clear();
    if (theRoomWave_) return; // empty field easter egg

    auto add = [&](int tick, Enemy::Type t, float yf)
    {
        spawnQueue_.push_back({ tick, t, juce::jlimit(0.1f, 0.9f, yf) });
    };

    switch (waveIdx)
    {
        case 0: // Wave 1 -- slow groove introduction
            add(40,  Enemy::Mudball,     0.25f);
            add(90,  Enemy::Mudball,     0.75f);
            add(130, Enemy::Mudball,     0.50f);
            add(170, Enemy::SilenceVoid, 0.40f);
            add(180, Enemy::Mudball,     0.20f);
            add(220, Enemy::Mudball,     0.80f);
            add(260, Enemy::Mudball,     0.55f);
            break;

        case 1: // Wave 2 -- Clippers added
            add(30,  Enemy::Mudball,     0.30f);
            add(65,  Enemy::Clipper,     0.65f);
            add(100, Enemy::Mudball,     0.20f);
            add(140, Enemy::SilenceVoid, 0.50f);
            add(150, Enemy::Clipper,     0.80f);
            add(190, Enemy::Mudball,     0.35f);
            add(225, Enemy::Mudball,     0.70f);
            add(260, Enemy::Clipper,     0.45f);
            add(300, Enemy::SilenceVoid, 0.30f);
            add(315, Enemy::Mudball,     0.60f);
            break;

        case 2: // Wave 3 -- THE LIMITER finale
            add(30,  Enemy::Mudball,     0.25f);
            add(65,  Enemy::Clipper,     0.70f);
            add(100, Enemy::Mudball,     0.45f);
            add(135, Enemy::SilenceVoid, 0.55f);
            add(145, Enemy::Clipper,     0.20f);
            add(185, Enemy::Mudball,     0.80f);
            add(220, Enemy::Clipper,     0.40f);
            add(260, Enemy::Mudball,     0.60f);
            add(295, Enemy::SilenceVoid, 0.35f);
            add(335, Enemy::Clipper,     0.65f);
            add(390, Enemy::Limiter,     0.50f);
            break;

        default: break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Shots
// ─────────────────────────────────────────────────────────────────────────────

void BombImpactGame::fireAutoShot()
{
    bullets_.push_back({ kShipX + 16.0f, shipY_, false, true });
    if (onKick) onKick();
}

void BombImpactGame::fireChargedShot()
{
    bullets_.push_back({ kShipX + 16.0f, shipY_, true, true });
    if (onKick) onKick();
}

void BombImpactGame::addToast(const juce::String& msg, int ticks)
{
    toastMsg_  = msg;
    toastTick_ = ticks;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

const char* BombImpactGame::enemyGlyph(Enemy::Type t) noexcept
{
    switch (t) {
        case Enemy::Mudball:     return "~%";
        case Enemy::Clipper:     return "<>";
        case Enemy::SilenceVoid: return "[]";
        case Enemy::Limiter:     return "##";
    }
    return "??";
}

juce::Colour BombImpactGame::enemyColour(Enemy::Type t) noexcept
{
    switch (t) {
        case Enemy::Mudball:     return juce::Colour(0xFFFF5555u);
        case Enemy::Clipper:     return juce::Colour(0xFFFFBB00u);
        case Enemy::SilenceVoid: return juce::Colour(0xFF4488CCu);
        case Enemy::Limiter:     return juce::Colour(0xFFCCCCCCu);
    }
    return juce::Colours::white;
}

void BombImpactGame::paint(juce::Graphics& g, juce::Rectangle<int> content)
{
    if (!isActive()) return;

    const int hudH  = 20;
    const int footH = 14;
    auto hud    = content.removeFromTop(hudH);
    auto footer = content.removeFromBottom(footH);
    // content is now the game field

    fieldW_ = (float)content.getWidth();
    fieldH_ = (float)content.getHeight();

    // Field background
    g.setColour(juce::Colour(0xFF000000u));
    g.fillRect(content);
    // Scanline pass
    g.setColour(juce::Colour(0xFF080808u));
    for (int y = content.getY(); y < content.getBottom(); y += 4)
        g.fillRect(content.getX(), y, content.getWidth(), 2);

    switch (state_)
    {
        case State::Attract:   paintAttract   (g, content); break;
        case State::Playing:   paintField     (g, content); break;
        case State::WaveClear: paintField     (g, content); paintWaveClear(g, content); break;
        case State::GameOver:  paintGameOver  (g, content); break;
        case State::Complete:  paintComplete  (g, content); break;
        default: break;
    }

    // Toast
    if (toastTick_ > 0)
    {
        const float alpha = juce::jmin(1.0f, (float)toastTick_ / 25.0f);
        g.setFont(juce::Font(juce::FontOptions(termFont(), 15.0f, juce::Font::bold)));
        g.setColour((overdriveActive_ ? juce::Colour(0xFFFFCC00u)
                                      : juce::Colour(0xFF88FF44u)).withAlpha(alpha));
        g.drawText(toastMsg_, content, juce::Justification::centred);
    }

    // HUD
    paintHUD(g, hud);

    // Footer hint
    g.setColour(juce::Colour(0xFF0D0D0Du));
    g.fillRect(footer);
    g.setFont(juce::Font(juce::FontOptions(termFont(), 10.0f, juce::Font::plain)));
    g.setColour(juce::Colour(0xFF333333u));
    g.drawText("[ UP/DN ] DODGE  [ T ] BOOST  [ ESC ] ABORT",
               footer, juce::Justification::centred);
}

void BombImpactGame::paintHUD(juce::Graphics& g, juce::Rectangle<int> area)
{
    g.setColour(juce::Colour(0xFF0D0D0Du));
    g.fillRect(area);
    g.setColour(juce::Colour(0xFF1A3A1Au));
    g.fillRect(area.getX(), area.getBottom() - 1, area.getWidth(), 1);

    g.setFont(juce::Font(juce::FontOptions(termFont(), 11.5f, juce::Font::plain)));
    const int w4 = area.getWidth() / 4;

    g.setColour(juce::Colour(0xFF88FF44u));
    g.drawText(juce::String(score_).paddedLeft('0', 6),
               area.withWidth(w4), juce::Justification::centredLeft, false);

    g.setColour(juce::Colour(0xFF336633u));
    g.drawText("WV " + juce::String(juce::jmax(1, wave_)) + "/3",
               area.withX(area.getX() + w4).withWidth(w4),
               juce::Justification::centred, false);

    if (chain_ > 1)
    {
        g.setColour(chain_ >= 9 ? juce::Colour(0xFFFFCC00u) : juce::Colour(0xFF44AA44u));
        g.drawText("x" + juce::String(chain_),
                   area.withX(area.getX() + w4 * 2).withWidth(w4),
                   juce::Justification::centred, false);
    }

    juce::String livesStr;
    for (int i = 0; i < 3; ++i) livesStr += (i < lives_) ? "[*]" : "[ ]";
    if (nimerMode_) livesStr = "<9> " + livesStr;
    g.setColour(juce::Colour(0xFFFF6666u));
    g.drawText(livesStr,
               area.withX(area.getX() + w4 * 3).withWidth(w4),
               juce::Justification::centredRight, false);
}

void BombImpactGame::paintField(juce::Graphics& g, juce::Rectangle<int> f)
{
    const int ox = f.getX(), oy = f.getY();
    const int dotSpX = 22, dotSpY = 18;
    const int scroll = bgScrollOffset_ % dotSpX;

    g.setColour(juce::Colour(0xFF0F1F0Fu));
    for (int y = oy; y < f.getBottom(); y += dotSpY)
        for (int x = ox - scroll; x < f.getRight(); x += dotSpX)
            g.fillRect(x, y, 2, 2);

    g.setFont(juce::Font(juce::FontOptions(termFont(), 13.0f, juce::Font::plain)));

    // Ship
    g.setColour(overdriveActive_ ? juce::Colour(0xFFFFDD00u) : juce::Colour(0xFF88FF44u));
    g.drawText(">=>", ox + (int)kShipX - 6, oy + (int)shipY_ - 7, 36, 14,
               juce::Justification::centredLeft);

    // Bullets
    for (const auto& b : bullets_)
    {
        if (!b.alive) continue;
        g.setColour(b.charged ? juce::Colour(0xFFFFCC00u) : juce::Colour(0xFF44FF44u));
        g.fillRect(ox + (int)b.x, oy + (int)b.y - 1, b.charged ? 8 : 5, 2);
    }

    // Enemies
    for (const auto& e : enemies_)
    {
        if (!e.alive) continue;
        g.setColour(enemyColour(e.type));
        if (e.type == Enemy::Limiter)
        {
            const float barH  = fieldH_ * 0.80f;
            const float barT  = (fieldH_ - barH) * 0.5f;
            const float hpFrac = (float)e.hp / 15.0f;
            g.setColour(juce::Colour(0xFF666666u));
            g.fillRect(ox + (int)e.x - 4, oy + (int)barT, 8, (int)barH);
            g.setColour(juce::Colour(0xFFFF4444u).withAlpha(0.6f + hpFrac * 0.4f));
            g.fillRect(ox + (int)e.x - 3, oy + (int)barT + 2, 6, juce::jmax(2, (int)(barH * hpFrac) - 4));
            g.setColour(juce::Colour(0xFF888888u));
            g.setFont(juce::Font(juce::FontOptions(termFont(), 9.0f, juce::Font::plain)));
            g.drawText("LIMITER", ox + (int)e.x - 28, oy + (int)barT - 13, 60, 11,
                       juce::Justification::centred);
        }
        else
        {
            g.drawText(enemyGlyph(e.type),
                       ox + (int)e.x - 11, oy + (int)e.y - 8, 26, 16,
                       juce::Justification::centred);
        }
    }
}

void BombImpactGame::paintAttract(juce::Graphics& g, juce::Rectangle<int> f)
{
    g.setFont(juce::Font(juce::FontOptions(termFont(), 22.0f, juce::Font::bold)));
    g.setColour(juce::Colour(0xFF88FF44u));
    g.drawText("KICK IMPACT", f.withBottom(f.getCentreY()), juce::Justification::centredBottom);

    g.setFont(juce::Font(juce::FontOptions(termFont(), 10.5f, juce::Font::plain)));
    g.setColour(juce::Colour(0xFF2A5A2Au));
    g.drawText(">=>  AUTO-FIRE  |  [T] BOOST  |  [UP/DN] DODGE",
               f.withTrimmedTop(f.getHeight() / 2 + 6).withBottom(f.getBottom() - 44),
               juce::Justification::centred);

    if ((attractBlink_ / 14) % 2 == 0)
    {
        g.setFont(juce::Font(juce::FontOptions(termFont(), 12.0f, juce::Font::plain)));
        g.setColour(juce::Colour(0xFF44AA44u));
        g.drawText("[ PRESS ANY KEY -- OR WAIT ]",
                   f.withTop(f.getBottom() - 40), juce::Justification::centred);
    }
}

void BombImpactGame::paintWaveClear(juce::Graphics& g, juce::Rectangle<int> f)
{
    const float a = juce::jmin(1.0f, (float)stateTimer_ / 18.0f);
    g.setColour(juce::Colour(0xFF000000u).withAlpha(0.65f));
    g.fillRect(f);
    g.setFont(juce::Font(juce::FontOptions(termFont(), 17.0f, juce::Font::bold)));
    g.setColour(juce::Colour(0xFF88FF44u).withAlpha(a));
    g.drawText("WAVE " + juce::String(wave_) + " CLEARED", f, juce::Justification::centred);
}

void BombImpactGame::paintGameOver(juce::Graphics& g, juce::Rectangle<int> f)
{
    g.setFont(juce::Font(juce::FontOptions(termFont(), 20.0f, juce::Font::bold)));
    g.setColour(juce::Colour(0xFFFF4444u));
    g.drawText("SIGNAL LOST", f.withBottom(f.getCentreY()), juce::Justification::centredBottom);
    g.setFont(juce::Font(juce::FontOptions(termFont(), 12.0f, juce::Font::plain)));
    g.setColour(juce::Colour(0xFF555555u));
    g.drawText("SCORE  " + juce::String(score_),
               f.reduced(0, f.getHeight() / 4), juce::Justification::centred);
}

void BombImpactGame::paintComplete(juce::Graphics& g, juce::Rectangle<int> f)
{
    g.setFont(juce::Font(juce::FontOptions(termFont(), 17.0f, juce::Font::bold)));
    g.setColour(juce::Colour(0xFF88FF44u));
    g.drawText("MISSION COMPLETE", f.withBottom(f.getCentreY()), juce::Justification::centredBottom);
    g.setFont(juce::Font(juce::FontOptions(termFont(), 12.0f, juce::Font::plain)));
    g.setColour(juce::Colour(0xFF44AA44u));
    g.drawText("SCORE  " + juce::String(score_),
               f.reduced(0, f.getHeight() / 4), juce::Justification::centred);
    if (vaultAccessEarned_)
    {
        g.setFont(juce::Font(juce::FontOptions(termFont(), 11.0f, juce::Font::plain)));
        g.setColour(juce::Colour(0xFFFFCC00u));
        g.drawText("VAULT ACCESS GRANTED -- EDEN UNLOCKED",
                   f.withTop(f.getCentreY() + 20), juce::Justification::centredTop);
    }
}

} // namespace bombo
