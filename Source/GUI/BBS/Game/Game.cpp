// Source/GUI/BBS/Game/Game.cpp
#include "Game.h"
#include "SpriteData.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

namespace
{
    // WaveClear "WAVE n CLEAR" flash duration (spec §4.4): 1.5s.
    constexpr int kWaveClearFlashTicks = static_cast<int>(1.5f * bombo::game::kTickHz);

    // Boss = wave 12 (the run is 12 waves; W12 is RUMBLR). Extended from the
    // original 8-wave run for a longer, harder campaign (W1-W11 normal + boss).
    constexpr int kBossWave = 12;

    // Shops occur after W2, W4, W6, W8, W10 (every other wave) — extra mid-run
    // economy to match the longer campaign.
    bool shopFollowsWave(int wave) noexcept
    {
        return wave == 2 || wave == 4 || wave == 6 || wave == 8 || wave == 10;
    }
}

namespace bombo::game
{
    Game::Game() = default;

    void Game::transitionTo(GameState s)
    {
        priorState_ = state_;
        state_      = s;
    }

    void Game::startNewRun(bool dailySeed)
    {
        currentWave_ = 1;
        score_       = 0;
        lives_       = kPlayerStartLives;
        currencyDB_  = 0;
        daily_       = dailySeed;
        victory_     = false;

        if (dailySeed)
        {
            runSeed_ = dailySeedToday();
        }
        else
        {
            std::random_device rd;
            runSeed_ = rd();
            if (runSeed_ == 0u) runSeed_ = 1u;   // keep determinism contract: never 0
        }

        // Reset all in-wave simulation state for a fresh run.
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
        tickCounter_   = 0;
        runRng_.seed(runSeed_);
        wave_          = scheduleWave(runSeed_, currentWave_);

        // Reset run-flow state.
        waveClearTicks_   = 0;
        shop_.reset();
        shopSlot_         = 0;
        shopFreeHealUsed_ = false;
        initials_         = { 'A', 'A', 'A' };
        initialsSlot_     = 0;

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

    void Game::setCharging(bool c) noexcept
    {
        if (c) player_.beginCharge();
        else   player_.charging = false;
    }

    bool Game::releaseChargedShot() noexcept
    {
        const bool fired = player_.releaseCharge();
        if (fired)
        {
            // Wide charged bullet: high damage, pierces through several enemies.
            playerBullets_.spawn(player_.x + 4.0f, player_.y, kBulletSpeedPxS, 0.0f,
                                 /*damage=*/5, /*wide=*/true, /*pierce=*/4);
            if (onShot) onShot();
        }
        return fired;
    }

    // ────────────────────────────────────────────────────────────────────────
    // Run-flow cadence: wave-clear -> next wave / shop / boss / game-over.
    // ────────────────────────────────────────────────────────────────────────

    bool Game::waveIsClear() const noexcept
    {
        if (! wave_.done()) return false;   // schedule still has spawns to fire
        for (const auto& e : enemies_.enemies())
            if (e.active && e.kind != EnemyKind::Rumblr)
                return false;               // a live non-boss enemy remains
        return true;
    }

    void Game::onWaveCleared()
    {
        // timeRemaining proxy: there is no per-wave countdown timer in the sim, so we
        // honestly pass 0 (the bonus is then peakChain*5 + lives*50). Documented choice.
        score_ += computeWaveClearBonus(/*timeRemaining=*/0, chain_.peak(), lives_);
        chain_.resetForWave();
        waveClearTicks_ = kWaveClearFlashTicks;
        transitionTo(GameState::WaveClear);
        if (onWaveClear) onWaveClear();   // Task 24 SFX seam (additive)
    }

    void Game::advanceAfterWaveClear()
    {
        // currentWave_ is the wave that just cleared. Decide what comes next.
        if (shopFollowsWave(currentWave_))      // after W2/W4/W6/W8/W10 -> shop
        {
            enterShop();
            return;
        }
        if (currentWave_ == kBossWave - 1)      // after W11 -> boss (W12)
        {
            enterBoss();
            return;
        }
        // Otherwise advance to the next ordinary wave.
        ++currentWave_;
        wave_ = scheduleWave(runSeed_, currentWave_);
        transitionTo(GameState::Playing);
    }

    void Game::enterShop()
    {
        shop_             = std::make_unique<ShopVisit>(runSeed_ ^ static_cast<uint32_t>(currentWave_));
        shopSlot_         = 0;
        shopFreeHealUsed_ = false;
        transitionTo(GameState::Shop);
    }

    void Game::advanceAfterShop()
    {
        shop_.reset();
        ++currentWave_;
        wave_ = scheduleWave(runSeed_, currentWave_);
        transitionTo(GameState::Playing);
    }

    void Game::enterBoss()
    {
        ++currentWave_;   // -> kBossWave (12)
        // Clear any stray non-boss leftovers, then spawn the RUMBLR.
        enemies_ = EnemyPool{};
        enemies_.spawn(EnemyKind::Rumblr, static_cast<float>(kFbW) - 30.0f,
                       static_cast<float>(kFbH) / 2.0f, 0.0f, 0.0f);
        transitionTo(GameState::Boss);
        if (onBossTelegraph) onBossTelegraph();   // Task 24 SFX seam (additive)
    }

    bool Game::bossIsDead() const noexcept
    {
        for (const auto& e : enemies_.enemies())
            if (e.active && e.kind == EnemyKind::Rumblr)
                return false;
        return true;
    }

    void Game::onGameOver(bool victory)
    {
        // Tripwire: never re-enter the game-over flow once it's begun.
        if (state_ == GameState::GameOver || state_ == GameState::Initials || state_ == GameState::Results)
            return;

        victory_ = victory;
        if (onGameOverFx) onGameOverFx(victory);   // Task 24 SFX seam (additive)
        highScores_.load();   // refresh from disk before checking qualification
        if (highScores_.qualifiesForTopTen(score_))
        {
            initials_     = { 'A', 'A', 'A' };
            initialsSlot_ = 0;
            transitionTo(GameState::Initials);
        }
        else
        {
            transitionTo(GameState::Results);
        }
    }

    // ────────────────────────────────────────────────────────────────────────
    // Shop interaction (called by the input layer in Task 22).
    // ────────────────────────────────────────────────────────────────────────

    void Game::shopMoveSelection(int delta) noexcept
    {
        if (state_ != GameState::Shop) return;
        const int n = kShopOfferCount;
        shopSlot_ = ((shopSlot_ + delta) % n + n) % n;
    }

    bool Game::shopBuySelected()
    {
        if (state_ != GameState::Shop || shop_ == nullptr) return false;
        return shop_->buy(shopSlot_, currencyDB_, ownedItems_);   // buy() deducts dB itself
    }

    bool Game::shopReroll()
    {
        if (state_ != GameState::Shop || shop_ == nullptr) return false;
        // FOOTGUN: reroll() does NOT deduct, and rerollCost() rises AFTER a successful
        // reroll. Capture the cost first, then deduct it ourselves on success.
        const int cost = shop_->rerollCost();
        if (shop_->reroll(currencyDB_))
        {
            currencyDB_ -= cost;
            shopSlot_    = 0;
            return true;
        }
        return false;
    }

    bool Game::shopUseFreeHeal()
    {
        if (state_ != GameState::Shop || shopFreeHealUsed_) return false;
        shopFreeHealUsed_ = true;
        if (lives_ < kPlayerMaxLives) ++lives_;
        return true;
    }

    void Game::shopContinue()
    {
        if (state_ != GameState::Shop) return;
        advanceAfterShop();
    }

    // ────────────────────────────────────────────────────────────────────────
    // Initials entry + results (called by the input layer in Task 22).
    // ────────────────────────────────────────────────────────────────────────

    void Game::initialsCycleLetter(int slot, int delta) noexcept
    {
        if (slot < 0 || slot >= 3) return;
        int v = (initials_[(size_t) slot] - 'A' + delta) % 26;
        if (v < 0) v += 26;
        initials_[(size_t) slot] = static_cast<char>('A' + v);
    }

    void Game::initialsMoveSlot(int delta) noexcept
    {
        initialsSlot_ = ((initialsSlot_ + delta) % 3 + 3) % 3;
    }

    void Game::initialsConfirm()
    {
        if (state_ != GameState::Initials) return;

        ScoreEntry e;
        e.initials = juce::String(juce::CharPointer_ASCII(initials_.data()), (size_t) 3);
        e.score    = score_;
        e.wave     = currentWave_;
        e.date     = juce::Time::getCurrentTime().formatted("%Y-%m-%d");
        e.daily    = daily_;
        e.seed     = daily_ ? runSeed_ : 0u;

        highScores_.recordRun(e);
        highScores_.save();
        transitionTo(GameState::Results);
    }

    void Game::resultsContinue()
    {
        if (state_ != GameState::Results) return;
        transitionTo(GameState::Title);
    }

   #if defined(BOMBO_GAME_TEST_HOOKS)
    void Game::testForceWaveClear() noexcept
    {
        wave_ = WaveSchedule{};            // schedule done
        for (auto& e : enemies_.enemies()) // clear all non-boss enemies
            if (e.kind != EnemyKind::Rumblr)
                e.active = false;
    }
   #endif

    void Game::clampPlayerToField() noexcept
    {
        // Spec intends L/R as +/-30 from a column; for v1.0.x just keep the player
        // fully on-screen with a small margin.
        player_.x = std::max(4.0f, std::min(static_cast<float>(kFbW) - 4.0f, player_.x));
        player_.y = std::max(4.0f, std::min(static_cast<float>(kFbH) - 4.0f, player_.y));
    }

    void Game::spawnPlayerShot()
    {
        // TransientBuff: +1 damage per stack. EffectState carries no damage buff,
        // so damage derives purely from the shop item here.
        const int   dmg     = 1 + ownedItems_[(int) ShopItemId::TransientBuff];
        const bool  spread  = ownedItems_[(int) ShopItemId::Spread] > 0 || effects_.spreadActive();
        const float bx      = player_.x + 4.0f;
        const float by      = player_.y;

        playerBullets_.spawn(bx, by, kBulletSpeedPxS, 0.0f, dmg);
        if (spread)
        {
            // +/- ~15deg angled bullets (tan(15deg) ~ 0.27).
            const float vy = kBulletSpeedPxS * 0.27f;
            playerBullets_.spawn(bx, by, kBulletSpeedPxS, -vy, dmg);
            playerBullets_.spawn(bx, by, kBulletSpeedPxS,  vy, dmg);
        }
        if (onShot) onShot();   // Task 24 audio seam — fires the active preset/kick.
    }

    void Game::tick()
    {
        // WaveClear is a brief flash; count it down then advance the cadence.
        if (state_ == GameState::WaveClear)
        {
            if (--waveClearTicks_ <= 0)
                advanceAfterWaveClear();
            return;
        }

        // Sim runs in PLAYING and BOSS only. Shop/Paused/etc. freeze the sim.
        if (state_ != GameState::Playing && state_ != GameState::Boss)
            return;

        ++tickCounter_;

        // (a) input -> player velocity
        const float mx = (input_.right ? 1.0f : 0.0f) - (input_.left ? 1.0f : 0.0f);
        const float my = (input_.down  ? 1.0f : 0.0f) - (input_.up   ? 1.0f : 0.0f);
        player_.vx = mx * kPlayerSpeedPxS;
        player_.vy = my * kPlayerSpeedPxS;
        player_.tick();
        clampPlayerToField();

        // (b) autofire -> spawn bullet(s).
        // FireRate shop item shortens the autofire interval (+12%/stack). We emulate
        // this by injecting extra shots: kAutoFireTicks is owned by Player::tick(), so
        // rather than reach in, we let the base autofire flag drive one shot and add a
        // probabilistic extra-shot pass per FireRate stack each frame.
        if (player_.wantsShootThisTick())
        {
            spawnPlayerShot();
            player_.clearShootFlag();
        }
        const int fireRate = ownedItems_[(int) ShopItemId::FireRate];
        if (fireRate > 0)
        {
            // Each stack lifts effective fire rate by ~12%; convert to a per-tick chance.
            // Clamp to 0.9 so a future bump to FireRate's maxStacks (currently 3) can't
            // drive the probability >= 1.0 (which would make every tick fire an extra shot).
            const float extraChance = std::min(0.9f, 0.12f * static_cast<float>(fireRate));
            std::uniform_real_distribution<float> d(0.0f, 1.0f);
            if (d(runRng_) < extraChance)
                spawnPlayerShot();
        }

        // (c) tick pools. speedMult feeds the dt-accepting systems (wave/effects/chain),
        // giving a felt tempo effect at high BPM. Entity pools use fixed kTickDt internally;
        // entity-velocity scaling is a v1.1 refinement (DEFERRED, documented).
        const float dt = kTickDt * speedMult();
        playerBullets_.tick();
        enemyShots_.tick();
        enemies_.tick(&player_, &enemyShots_);
        pickups_.tick(player_.x, player_.y,
                      ownedItems_[(int) ShopItemId::DbMagnet] > 0);
        wave_.tick(enemies_, dt);
        effects_.tick(dt);
        chain_.tick(dt);

        // (d) collisions. resolveCombat() may transition to GameOver on a fatal hit.
        resolveCombat();
        if (state_ != GameState::Playing && state_ != GameState::Boss)
            return;   // death already ended the run this tick

        // (e) cadence transitions.
        if (state_ == GameState::Boss)
        {
            if (bossIsDead())
            {
                // Boss-clear bonus (spec §9.1): 5000 + lives*200.
                score_ += 5000 + lives_ * 200;
                onGameOver(/*victory=*/true);
            }
            return;
        }

        // PLAYING: wave is clear when the schedule is exhausted and no live mobs remain.
        if (waveIsClear())
            onWaveCleared();
    }

    int Game::scoreBaseFor(EnemyKind kind) const noexcept
    {
        switch (kind)
        {
            case EnemyKind::Mudball:     return 10;
            case EnemyKind::Clipper:     return 10;
            case EnemyKind::Limiter:     return 25;
            case EnemyKind::Aliaser:     return 20;
            case EnemyKind::AliaserMini: return 5;
            case EnemyKind::DiveBomber:  return 20;
            case EnemyKind::SilenceVoid: return 50;
            case EnemyKind::Rumblr:      return 500;
            case EnemyKind::Warble:      return 12;
            case EnemyKind::Hiss:        return 12;
            case EnemyKind::Crackle:     return 15;
            case EnemyKind::Wobble:      return 15;
            case EnemyKind::Stutter:     return 18;
            case EnemyKind::Overdrive:   return 60;
            case EnemyKind::Phaser:      return 60;
            case EnemyKind::Flanger:     return 70;
            case EnemyKind::Resonator:   return 80;
        }
        return 10;
    }

    void Game::maybeSpawnDrop(EnemyKind kind, float x, float y)
    {
        DropSource src;
        switch (kind)
        {
            case EnemyKind::Mudball:
            case EnemyKind::Clipper:
            case EnemyKind::Limiter:
                src = DropSource::CommonMob; break;
            case EnemyKind::Aliaser:
            case EnemyKind::AliaserMini:
            case EnemyKind::DiveBomber:
                src = DropSource::MidMob; break;
            case EnemyKind::SilenceVoid:
                src = DropSource::SpecialMob; break;
            case EnemyKind::Rumblr:
                // Boss drops handled by the boss flow in a later task.
                return;
            default:
                src = DropSource::CommonMob; break;
        }

        const RollResult r = rollDrop(makeTableForSource(src), runRng_);
        if (r.dropped)
            pickups_.spawn(r.kind, x, y);
    }

    bool Game::enemyShotsHitPlayer() noexcept
    {
        bool hit = false;
        for (auto& b : enemyShots_.bullets())
        {
            if (! b.active) continue;
            if (std::abs(b.x - player_.x) < 6.0f && std::abs(b.y - player_.y) < 6.0f)
            {
                b.active = false;
                hit = true;
            }
        }
        return hit;
    }

    bool Game::resolveEnemyBodyContact() noexcept
    {
        // Body-contact threat (spec §6.1). Player hitbox ~6px, enemy ~5px
        // half-extent → treat an overlap as contact when both axes are within
        // 8px (Chebyshev/AABB), mirroring the enemy-shot test's tolerance for a
        // fair, slightly-forgiving feel. Returns true on a life-costing hit.
        constexpr float kContactExtent = 8.0f;

        // SilenceVoid drains the chain on contact (NON-lethal). This runs every
        // tick the player overlaps a void — the intended "energy suck" that keeps
        // the chain suppressed while you linger near it (spec §6.1). It never
        // touches lives and never short-circuits the lethal pass below.
        for (auto& e : enemies_.enemies())
        {
            if (! e.active || e.kind != EnemyKind::SilenceVoid) continue;
            if (std::abs(e.x - player_.x) < kContactExtent &&
                std::abs(e.y - player_.y) < kContactExtent)
                chain_.drain();
        }

        // Lethal contact (all OTHER kinds). Gate the WHOLE block on i-frames so
        // we lose at most one life per tick no matter how many enemies overlap,
        // exactly like the enemy-shot path. The 150-tick invincibility set by
        // takeHit() then guarantees no multi-tick drain. We do NOT deactivate the
        // enemy: ordinary mobs (and DiveBomber kamikazes) pass through and the
        // player eats the hit. Keeping DiveBomber non-detonating is the simple
        // v1.0.x choice — it flies off-screen and culls normally. (DOCUMENTED.)
        if (player_.isInvincible()) return false;

        for (const auto& e : enemies_.enemies())
        {
            if (! e.active || e.kind == EnemyKind::SilenceVoid) continue;
            if (std::abs(e.x - player_.x) < kContactExtent &&
                std::abs(e.y - player_.y) < kContactExtent)
                return true;   // one damaging contact is enough this tick
        }
        return false;
    }

    void Game::grantRandomShopItem()
    {
        const auto& cat = catalogV10();
        if (cat.empty()) return;
        std::uniform_int_distribution<size_t> d(0, cat.size() - 1);
        const int idx = (int) cat[d(runRng_)].id;
        if (idx >= 0 && idx < (int) ownedItems_.size())
            ++ownedItems_[idx];
    }

    void Game::collectPickupsTouchingPlayer()
    {
        for (auto& p : pickups_.pickups())
        {
            if (! p.active) continue;
            if (std::abs(p.x - player_.x) >= 6.0f || std::abs(p.y - player_.y) >= 6.0f)
                continue;

            const PickupOutcome o = resolvePickup(p.kind);

            currencyDB_ += o.currencyDelta;
            if (o.lifeDelta != 0)
                lives_ = std::min(kPlayerMaxLives, lives_ + o.lifeDelta);
            if (o.refillCharge)
                player_.chargeMeter = 1.0f;
            for (int i = 0; i < o.chainBankAdd; ++i)
                chain_.onKill();
            if (o.spawnCurrencyCluster > 0)
                for (int i = 0; i < o.spawnCurrencyCluster; ++i)
                    pickups_.spawn(Pickup::Kind::DbSmall,
                                   player_.x + static_cast<float>((i % 3) - 1) * 5.0f,
                                   player_.y + static_cast<float>((i / 3) - 1) * 5.0f);
            // magnetiseCurrency: one-shot pull is left to the dB Magnet path (DEFERRED) —
            // applying it here would need a transient magnet timer; skipped for v1.0.x.
            effects_.applyOutcome(o);
            if (o.clearEnemyBullets)
                enemyShots_ = BulletPool{};
            if (o.grantPhaseLock)
                player_.invincTimer = 2 * kTickHz;   // 2s phase-lock
            if (o.grantRandomShopItem)
                grantRandomShopItem();

            if (onPickup) onPickup(dropTierOf(p.kind));   // Task 24 SFX seam (additive)

            p.active = false;   // consumed
        }
    }

    void Game::resolveCombat()
    {
        // Player bullets vs enemies (scoring + drops).
        std::vector<EnemyPool::KillInfo> kills;
        enemies_.applyBulletDamage(playerBullets_, &kills);
        for (const auto& k : kills)
        {
            chain_.onKill();
            const int base = scoreBaseFor(k.kind);
            score_ += static_cast<int>(base * chainMultiplierFor(chain_.count()));
            maybeSpawnDrop(k.kind, k.x, k.y);
            if (onEnemyHit) onEnemyHit(k.kind);   // Task 24 SFX seam (additive)
        }

        // Enemy shots vs player.
        if (! player_.isInvincible() && enemyShotsHitPlayer())
        {
            player_.takeHit();
            --lives_;
            if (lives_ <= 0)
            {
                lives_ = 0;
                onGameOver(/*victory=*/false);
                return;   // run is over; skip pickup collection this tick
            }
        }

        // Enemy bodies vs player (spec §6.1). resolveEnemyBodyContact() also
        // applies the SilenceVoid chain-drain internally (non-lethal). The
        // returned bool is true only for a life-costing contact, and the helper
        // already gates on i-frames, so a player made invincible by the shot pass
        // above will correctly take no further contact damage this tick.
        if (resolveEnemyBodyContact())
        {
            player_.takeHit();
            --lives_;
            if (lives_ <= 0)
            {
                lives_ = 0;
                onGameOver(/*victory=*/false);
                return;   // run is over; skip pickup collection this tick
            }
        }

        // Pickups vs player.
        collectPickupsTouchingPlayer();
    }

    // ────────────────────────────────────────────────────────────────────────
    // Rendering (Task 7 / §5). Pragmatic mono-text + sprite blits into the
    // 160x112 palette-index framebuffer. Palette indices: 0=bg 1=dim 2=mid
    // 3=accent 4=hot 5=hilite. Not pixel-perfect — visual polish pending an
    // in-DAW smoke test by a human (see report).
    // ────────────────────────────────────────────────────────────────────────
    namespace
    {
        // Map an enemy kind to its sprite blit (data ptr + dimensions). Returns
        // false for kinds with no sprite (none currently — all 8 are covered).
        struct SpriteRef { const uint8_t* data; int w; int h; };

        SpriteRef spriteForEnemy(EnemyKind k) noexcept
        {
            using namespace sprites;
            switch (k)
            {
                case EnemyKind::Mudball:     return { &kMudball[0][0],     10, 10 };
                case EnemyKind::Clipper:     return { &kClipper[0][0],     10, 10 };
                case EnemyKind::SilenceVoid: return { &kSilenceVoid[0][0], 10, 10 };
                case EnemyKind::Limiter:     return { &kLimiter[0][0],     10, 10 };
                case EnemyKind::Aliaser:     return { &kAliaser[0][0],     10, 10 };
                case EnemyKind::AliaserMini: return { &kAliaserMini[0][0], 10, 10 };
                case EnemyKind::DiveBomber:  return { &kDiveBomber[0][0],  10, 10 };
                case EnemyKind::Rumblr:      return { &kRumblr[0][0],      30, 30 };
                case EnemyKind::Warble:      return { &kWarble[0][0],      10, 10 };
                case EnemyKind::Hiss:        return { &kHiss[0][0],        10, 10 };
                case EnemyKind::Crackle:     return { &kCrackle[0][0],     10, 10 };
                case EnemyKind::Wobble:      return { &kWobble[0][0],      10, 10 };
                case EnemyKind::Stutter:     return { &kStutter[0][0],     10, 10 };
                case EnemyKind::Overdrive:   return { &kOverdrive[0][0],   10, 10 };
                case EnemyKind::Phaser:      return { &kPhaser[0][0],      10, 10 };
                case EnemyKind::Flanger:     return { &kFlanger[0][0],     10, 10 };
                case EnemyKind::Resonator:   return { &kResonator[0][0],   10, 10 };
            }
            return { &kMudball[0][0], 10, 10 };
        }

        // Format an unsigned, zero-padded fixed-width number into buf.
        void fmtNum(char* buf, size_t cap, const char* prefix, int value, int width)
        {
            // value is clamped non-negative; width digits, zero-padded.
            if (value < 0) value = 0;
            std::snprintf(buf, cap, "%s%0*d", prefix, width, value);
        }
    }

    void Game::renderInto(Framebuffer& fb, const Palette& pal) const
    {
        fb.clear(0);

        switch (state_)
        {
            case GameState::Playing:
            case GameState::Boss:
            {
                // --- starfield (static dim/hilite dots) ---
                static constexpr int kStars[][3] = {
                    {12, 18, 1}, {40, 8, 1}, {70, 30, 1}, {100, 14, 5},
                    {130, 40, 1}, {22, 70, 1}, {88, 84, 1}, {145, 90, 5},
                    {55, 100, 1}, {118, 66, 1},
                };
                for (auto& s : kStars) fb.pset(s[0], s[1], (uint8_t) s[2]);

                // --- enemies ---
                for (const auto& e : enemies_.enemies())
                {
                    if (! e.active) continue;
                    const SpriteRef sr = spriteForEnemy(e.kind);
                    fb.blitSprite(sr.data, sr.w, sr.h,
                                  (int) e.x - sr.w / 2, (int) e.y - sr.h / 2);
                }

                // --- pickups ---
                for (const auto& p : pickups_.pickups())
                {
                    if (! p.active) continue;
                    const int px = (int) p.x, py = (int) p.y;
                    const DropTier tier = dropTierOf(p.kind);
                    // Currency uses the dB token sprite; others a small accent block.
                    if (isCurrency(p.kind))
                        fb.blitSprite(&sprites::kDbSmall[0][0], 6, 6, px - 3, py - 3);
                    else
                        fb.fillRect(px - 2, py - 2, 4, 4,
                                    (uint8_t) (tier == DropTier::Legendary
                                                   ? legendaryColorIndex(tickCounter_)
                                                   : 3));
                    if (shouldSparkle(tier, tickCounter_))
                    {
                        fb.pset(px - 3, py - 3, 5);
                        fb.pset(px + 3, py + 3, 5);
                    }
                }

                // --- player bullets (accent) ---
                for (const auto& b : playerBullets_.bullets())
                {
                    if (! b.active) continue;
                    if (b.wide) fb.fillRect((int) b.x, (int) b.y - 1, 4, 3, 3);
                    else        fb.hline((int) b.x, (int) b.x + 2, (int) b.y, 3);
                }

                // --- enemy shots (hot) ---
                for (const auto& b : enemyShots_.bullets())
                {
                    if (! b.active) continue;
                    fb.hline((int) b.x - 2, (int) b.x, (int) b.y, 4);
                }

                // --- player (kick) ---
                {
                    const bool blink = player_.isInvincible() && ((tickCounter_ / 4) % 2);
                    if (! blink)
                        fb.blitSprite(&sprites::kPlayer[0][0], 16, 16,
                                      (int) player_.x - 8, (int) player_.y - 8);
                }

                // --- charge meter (when charging) ---
                if (player_.charging || player_.chargeProgress > 0.0f)
                {
                    const int w = (int) (player_.chargeProgress * 20.0f);
                    fb.fillRect((int) player_.x - 10, (int) player_.y + 7, 20, 2, 1);
                    if (w > 0) fb.fillRect((int) player_.x - 10, (int) player_.y + 7, w, 2, 5);
                }

                // --- HUD (top row) ---
                char buf[16];
                fmtNum(buf, sizeof buf, "SC ", score_, 5);
                fb.drawText(buf, 1, 1, 5);

                fmtNum(buf, sizeof buf, "DB ", currencyDB_, 4);
                fb.drawText(buf, 64, 1, 3);

                fmtNum(buf, sizeof buf, "W", currentWave_, 1);
                fb.drawText(buf, 110, 1, 5);

                if (chain_.count() > 1)
                {
                    fmtNum(buf, sizeof buf, "X", chain_.count(), 1);
                    fb.drawText(buf, 128, 1, 4);
                }

                // --- HUD (bottom row): lives as mini-gunships + autofire ---
                // Each remaining life is a tiny ship. On a hit the player is
                // briefly invincible (i-frames) — during that window the just-
                // lost ship is drawn as a ghost blinking in sync with the player
                // ship, then vanishes when the i-frames end (3 -> 2 -> ...).
                {
                    const int icon = 9;   // 8px sprite + 1px gap
                    const bool blink = ((tickCounter_ / 4) % 2) != 0;
                    for (int i = 0; i < lives_; ++i)
                        fb.blitSprite(&sprites::kPlayerMini[0][0], 8, 8,
                                      1 + i * icon, kFbH - 9);
                    // Ghost of the life being lost (only while i-frames run).
                    if (player_.isInvincible() && lives_ < kPlayerMaxLives && blink)
                        fb.blitSprite(&sprites::kPlayerMini[0][0], 8, 8,
                                      1 + lives_ * icon, kFbH - 9);
                }
                fb.drawText(player_.autofireOn ? "AUTO ON" : "AUTO OFF",
                            100, kFbH - 6, 3);

                if (state_ == GameState::Boss)
                    fb.drawTextCentered("RUMBLR", 8, 4);
                break;
            }

            case GameState::WaveClear:
            {
                // Dim field backdrop + centered banner.
                fb.fillRect(0, 0, kFbW, kFbH, 0);
                char buf[20];
                fmtNum(buf, sizeof buf, "WAVE ", currentWave_, 1);
                fb.drawTextCentered(buf, kFbH / 2 - 8, 5);
                fb.drawTextCentered("CLEAR", kFbH / 2, 4);
                fmtNum(buf, sizeof buf, "SC ", score_, 5);
                fb.drawTextCentered(buf, kFbH / 2 + 10, 3);
                break;
            }

            case GameState::Shop:
            {
                fb.drawTextCentered("SHOP", 3, 5);
                const int kRight = kFbW - 4;   // shared right-align margin
                const int kLeft  = 4;          // shared left-align column
                // Credits you can spend (dB = the currency dropped by enemies).
                char buf[16];
                fmtNum(buf, sizeof buf, "DB ", currencyDB_, 1);
                fb.drawText(buf, kLeft, 11, 3);

                if (shop_ != nullptr)
                {
                    const auto& offers = shop_->offers();
                    for (int i = 0; i < (int) offers.size(); ++i)
                    {
                        const int y = 22 + i * 13;
                        const bool sel = (i == shopSlot_);
                        const bool afford = currencyDB_ >= offers[(size_t) i].cost;
                        // Selector ">" + name, both left-aligned at a fixed column.
                        fb.drawText(sel ? ">" : " ", kLeft, y, 4);
                        fb.drawText(offers[(size_t) i].shortName, kLeft + 6, y,
                                    (uint8_t) (sel ? 5 : (afford ? 2 : 1)));
                        // Cost right-aligned at the shared margin.
                        char cb[12];
                        fmtNum(cb, sizeof cb, "", offers[(size_t) i].cost, 1);
                        fb.drawText(cb, kRight - Framebuffer::textWidth(cb), y,
                                    (uint8_t) (afford ? 3 : 1));
                    }
                }
                // Controls hint + footer actions.
                fb.drawText("ARROWS PICK  ENTER BUY", kLeft, kFbH - 25, 1);
                if (shop_ != nullptr)
                {
                    char rb[20];
                    fmtNum(rb, sizeof rb, "R REROLL ", shop_->rerollCost(), 1);
                    fb.drawText(rb, kLeft, kFbH - 17, 2);
                }
                fb.drawText(shopFreeHealAvailable() ? "H HEAL" : "H USED",
                            kLeft, kFbH - 9, 2);
                // SPACE READY right-aligned to share the cost column edge.
                fb.drawText("SPACE GO",
                            kRight - Framebuffer::textWidth("SPACE GO"),
                            kFbH - 9, 4);
                break;
            }

            case GameState::Title:
            {
                fb.drawTextCentered("KICK IMPACT", 14, 4);
                fb.hline(kFbW / 2 - 30, kFbW / 2 + 30, 22, 3);

                static const char* kItems[] = {
                    "NEW GAME", "DAILY RUN", "HIGHSCORES", "HELP", "EXIT"
                };
                for (int i = 0; i < 5; ++i)
                {
                    const int y = 36 + i * 10;
                    const bool sel = (i == titleSel_);
                    const int lx = (kFbW - Framebuffer::textWidth(kItems[i])) / 2;
                    if (sel) fb.drawText(">", lx - 6, y, 4);
                    fb.drawText(kItems[i], lx, y, (uint8_t) (sel ? 5 : 2));
                }
                fb.drawText(daily_ ? "DAILY SEED" : "SEED", 4, kFbH - 6, 1);
                break;
            }

            case GameState::Paused:
            {
                fb.drawTextCentered("PAUSED", 18, 5);
                static const char* kItems[] = {
                    "RESUME", "RESTART", "HIGHSCORES", "HELP", "QUIT"
                };
                for (int i = 0; i < 5; ++i)
                {
                    const int y = 36 + i * 10;
                    const bool sel = (i == pauseSel_);
                    const int lx = (kFbW - Framebuffer::textWidth(kItems[i])) / 2;
                    if (sel) fb.drawText(">", lx - 6, y, 4);
                    fb.drawText(kItems[i], lx, y, (uint8_t) (sel ? 5 : 2));
                }
                break;
            }

            case GameState::Help:
            {
                fb.drawText("HELP", 4, 2, 5);
                // Keymap (left column).
                fb.drawText("ARROWS MOVE",  4, 14, 2);
                fb.drawText("F FIRE",        4, 22, 2);
                fb.drawText("A AUTO",        4, 30, 2);
                fb.drawText("P PAUSE",       4, 38, 2);
                fb.drawText("H HELP",        4, 46, 2);
                fb.drawText("ESC QUIT",      4, 54, 2);
                // Bestiary (right column) — a couple of enemy sprites + labels.
                fb.blitSprite(&sprites::kMudball[0][0], 10, 10, 96, 14);
                fb.drawText("MUD", 110, 16, 3);
                fb.blitSprite(&sprites::kClipper[0][0], 10, 10, 96, 28);
                fb.drawText("CLIP", 110, 30, 3);
                fb.blitSprite(&sprites::kAliaser[0][0], 10, 10, 96, 42);
                fb.drawText("ALIAS", 110, 44, 3);
                fb.drawText("SHOP AFTER W2 W4 W6", 4, kFbH - 8, 1);
                break;
            }

            case GameState::HighScores:
            {
                fb.drawTextCentered("HIGH SCORES", 2, 5);
                const auto& rows = highScores_.topTen();
                for (int i = 0; i < (int) rows.size() && i < 10; ++i)
                {
                    const int y = 14 + i * 9;
                    char rb[8];
                    fmtNum(rb, sizeof rb, "", i + 1, 2);
                    fb.drawText(rb, 4, y, 2);
                    fb.drawText(rows[(size_t) i].initials.toRawUTF8(), 22, y, 5);
                    char sb[12];
                    fmtNum(sb, sizeof sb, "", rows[(size_t) i].score, 6);
                    fb.drawText(sb, 50, y, 3);
                    char wb[6];
                    fmtNum(wb, sizeof wb, "W", rows[(size_t) i].wave, 1);
                    fb.drawText(wb, 120, y, 4);
                }
                if (rows.empty())
                    fb.drawTextCentered("NO SCORES YET", kFbH / 2, 2);
                break;
            }

            case GameState::QuitConfirm:
            {
                fb.fillRect(kFbW / 2 - 36, kFbH / 2 - 12, 72, 24, 1);
                fb.drawTextCentered("QUIT", kFbH / 2 - 6, 5);
                fb.drawTextCentered("Y N", kFbH / 2 + 2, 4);
                break;
            }

            case GameState::Initials:
            {
                fb.drawTextCentered("ENTER NAME", 18, 5);
                // 3 slots spaced 18px apart, centered as a group.
                const int slotStep = 18;
                const int groupX = (kFbW - (2 * slotStep + 4)) / 2;
                for (int i = 0; i < 3; ++i)
                {
                    const int x = groupX + i * slotStep;
                    const char letter[2] = { initials_[(size_t) i], '\0' };
                    const bool sel = (i == initialsSlot_);
                    // Draw the letter big-ish by stamping it then underlining.
                    fb.drawText(letter, x, kFbH / 2 - 4, (uint8_t) (sel ? 5 : 2));
                    if (sel) fb.hline(x, x + 4, kFbH / 2 + 4, 4);
                }
                char sb[16];
                fmtNum(sb, sizeof sb, "SC ", score_, 5);
                fb.drawTextCentered(sb, kFbH - 12, 3);
                break;
            }

            case GameState::GameOver:
            case GameState::Results:
            {
                fb.drawTextCentered(victory_ ? "VICTORY" : "GAME OVER",
                                    24, victory_ ? 5 : 4);
                char sb[16];
                fmtNum(sb, sizeof sb, "SC ", score_, 5);
                fb.drawTextCentered(sb, kFbH / 2, 3);
                fb.drawTextCentered("PRESS ENTER", kFbH - 16, 2);
                break;
            }
        }

        juce::ignoreUnused(pal);   // palette resolved at blit time by Framebuffer.
    }

    // ────────────────────────────────────────────────────────────────────────
    // Input routing (Task 7 / §5). State-dispatched. Key codes are JUCE
    // KeyPress codes: arrows are juce::KeyPress::leftKey etc.; letters arrive as
    // ASCII (BBS forwards key.getKeyCode(), which for letters is the uppercase
    // ASCII value). Returns true if the key was consumed.
    //
    // HELD-MOVEMENT NOTE: JUCE keyPressed is edge-triggered. Smooth held
    // movement in Playing/Boss is now driven by BBSComponent::timerCallback,
    // which polls juce::KeyPress::isKeyCurrentlyDown each tick and calls
    // setMoveInput({up,down,left,right}); tick() integrates input_ into player
    // velocity. So the arrow branches in the Playing/Boss case below are no-ops
    // for movement (they only consume the key) — the polled path is the single
    // movement source, avoiding double-counting. Arrow keys in the menu states
    // (Title/Pause/Shop/Initials) remain edge-driven here for selection. (DONE.)
    //
    // ESC NOTE: BBSComponent no longer intercepts escapeKey under BOMBO_GAME_V2;
    // it forwards ESC to handleKey so the game owns ESC semantics. In Playing/
    // Boss/Shop ESC opens QuitConfirm; in QuitConfirm ESC/Y/Enter confirmQuit()
    // sets wantsExit_, which BBSComponent::timerCallback polls -> exitGame().
    // (DONE.)
    // ────────────────────────────────────────────────────────────────────────
    bool Game::handleKey(int key, juce::ModifierKeys /*mods*/)
    {
        using KP = juce::KeyPress;
        const bool isUp    = (key == KP::upKey);
        const bool isDown  = (key == KP::downKey);
        const bool isLeft  = (key == KP::leftKey);
        const bool isRight = (key == KP::rightKey);
        const bool isEnter = (key == KP::returnKey);
        const bool isEsc   = (key == KP::escapeKey);
        // Letters: BBS forwards getKeyCode(); for ASCII letters that is the
        // uppercase code. Normalise to uppercase for robustness.
        const int ch = (key >= 'a' && key <= 'z') ? key - 32 : key;

        switch (state_)
        {
            case GameState::Title:
            {
                if (isUp)   { titleSel_ = (titleSel_ + 4) % 5; return true; }
                if (isDown) { titleSel_ = (titleSel_ + 1) % 5; return true; }
                if (isEnter)
                {
                    switch (titleSel_)
                    {
                        case 0: startNewRun(false);              return true;
                        case 1: startNewRun(true);               return true;
                        case 2: transitionTo(GameState::HighScores); return true;
                        case 3: transitionTo(GameState::Help);   return true;
                        case 4: wantsExit_ = true;               return true;
                    }
                    return true;
                }
                if (isEsc) { wantsExit_ = true; return true; }
                return false;
            }

            case GameState::Playing:
            case GameState::Boss:
            {
                // Movement in Playing/Boss is the POLLED source: BBSComponent's
                // timerCallback polls isKeyCurrentlyDown each tick and calls
                // setMoveInput(); tick() integrates input_ into velocity. So the
                // arrow keys here must NOT also nudge the player or we'd double-
                // count (polled velocity + per-press nudge). We still consume
                // them (return true) so they don't leak to other handlers.
                // (Arrows in menus/shop/initials remain edge-driven below.)
                if (isLeft || isRight || isUp || isDown) return true;
                if (ch == 'F' || key == KP::spaceKey) { setCharging(true); return true; }
                if (ch == 'A') { player_.autofireOn = ! player_.autofireOn; return true; }
                if (ch == 'P') { togglePause(); return true; }
                if (ch == 'H') { transitionTo(GameState::Help); return true; }
                if (isEsc)     { togglePause(); return true; }   // ESC opens the pause menu
                return false;
            }

            case GameState::Paused:
            {
                if (isUp)   { pauseSel_ = (pauseSel_ + 4) % 5; return true; }
                if (isDown) { pauseSel_ = (pauseSel_ + 1) % 5; return true; }
                if (ch == 'P') { togglePause(); return true; }   // resume
                if (isEnter)
                {
                    switch (pauseSel_)
                    {
                        case 0: togglePause();                       return true; // resume
                        case 1: startNewRun(daily_);                 return true; // restart
                        case 2: transitionTo(GameState::HighScores); return true;
                        case 3: transitionTo(GameState::Help);       return true;
                        case 4: requestQuit();                       return true;
                    }
                    return true;
                }
                if (isEsc) { togglePause(); return true; }   // ESC resumes from pause
                return false;
            }

            case GameState::QuitConfirm:
            {
                if (ch == 'Y' || isEnter || isEsc) { confirmQuit(); return true; }
                if (ch == 'N')                     { cancelQuit();  return true; }
                return false;
            }

            case GameState::Help:
            {
                if (ch == 'H' || isEsc) { transitionTo(priorState_); return true; }
                return false;
            }

            case GameState::HighScores:
            {
                // Any key returns to Title.
                transitionTo(GameState::Title);
                return true;
            }

            case GameState::Shop:
            {
                if (isLeft || isUp)   { shopMoveSelection(-1); return true; }
                if (isRight || isDown) { shopMoveSelection(+1); return true; }
                if (isEnter) { shopBuySelected();     return true; }
                if (ch == 'R') { shopReroll();        return true; }
                if (ch == 'H') { shopUseFreeHeal();   return true; }
                if (key == KP::spaceKey) { shopContinue(); return true; }
                if (isEsc) { requestQuit(); return true; }
                return false;
            }

            case GameState::Initials:
            {
                // Left/Right move between the 3 slots; Up/Down scroll the
                // alphabet at the current slot. Raw A-Z typing fills the slot
                // and auto-advances (arcade-style). Typing owns letter keys here
                // so it never collides with BBS shortcuts like T/F.
                if (isLeft)  { initialsMoveSlot(-1); return true; }
                if (isRight) { initialsMoveSlot(+1); return true; }
                if (isUp)    { initialsCycleLetter(initialsSlot_, +1); return true; }
                if (isDown)  { initialsCycleLetter(initialsSlot_, -1); return true; }
                if (isEnter) { initialsConfirm();    return true; }
                if (ch >= 'A' && ch <= 'Z')
                {
                    initials_[(size_t) initialsSlot_] = static_cast<char>(ch);
                    if (initialsSlot_ < 2) ++initialsSlot_;   // auto-advance, clamp at last slot
                    return true;
                }
                return false;
            }

            case GameState::Results:
            {
                // Any key (typically Enter) advances back to Title.
                resultsContinue();
                return true;
            }

            case GameState::WaveClear:
            case GameState::GameOver:
                // Transient / auto-advancing — swallow input without action.
                return false;
        }
        return false;
    }

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
