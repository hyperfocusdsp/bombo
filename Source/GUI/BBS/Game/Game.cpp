// Source/GUI/BBS/Game/Game.cpp
#include "Game.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace
{
    // WaveClear "WAVE n CLEAR" flash duration (spec §4.4): 1.5s.
    constexpr int kWaveClearFlashTicks = static_cast<int>(1.5f * bombo::game::kTickHz);

    // Boss = wave 8 (the run is 8 waves; W8 is RUMBLR per spec §4.1).
    constexpr int kBossWave = 8;

    // Shops occur after W2, W4, W6 (spec §4.1 cadence).
    bool shopFollowsWave(int wave) noexcept { return wave == 2 || wave == 4 || wave == 6; }
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
    }

    void Game::advanceAfterWaveClear()
    {
        // currentWave_ is the wave that just cleared. Decide what comes next.
        if (shopFollowsWave(currentWave_))      // after W2 / W4 / W6 -> shop
        {
            enterShop();
            return;
        }
        if (currentWave_ == kBossWave - 1)      // after W7 -> boss (W8)
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
        ++currentWave_;   // -> 8
        // Clear any stray non-boss leftovers, then spawn the RUMBLR.
        enemies_ = EnemyPool{};
        enemies_.spawn(EnemyKind::Rumblr, static_cast<float>(kFbW) - 30.0f,
                       static_cast<float>(kFbH) / 2.0f, 0.0f, 0.0f);
        transitionTo(GameState::Boss);
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
        victory_ = victory;
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

        // Pickups vs player.
        collectPickupsTouchingPlayer();
    }

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
