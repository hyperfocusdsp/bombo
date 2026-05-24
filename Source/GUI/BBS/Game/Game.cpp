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
        if (onWaveClear) onWaveClear();   // Task 24 SFX seam (additive)
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
                        fb.blitSprite(&sprites::kPlayer[0][0], 10, 10,
                                      (int) player_.x - 5, (int) player_.y - 5);
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

                // --- HUD (bottom row): lives + autofire ---
                fmtNum(buf, sizeof buf, "LIVES ", lives_, 1);
                fb.drawText(buf, 1, kFbH - 6, 4);
                fb.drawText(player_.autofireOn ? "AUTO ON" : "AUTO OFF",
                            100, kFbH - 6, 3);

                if (state_ == GameState::Boss)
                    fb.drawText("RUMBLR", kFbW / 2 - 12, 8, 4);
                break;
            }

            case GameState::WaveClear:
            {
                // Dim field backdrop + centered banner.
                fb.fillRect(0, 0, kFbW, kFbH, 0);
                char buf[20];
                fmtNum(buf, sizeof buf, "WAVE ", currentWave_, 1);
                fb.drawText(buf, kFbW / 2 - 18, kFbH / 2 - 8, 5);
                fb.drawText("CLEAR", kFbW / 2 - 10, kFbH / 2, 4);
                fmtNum(buf, sizeof buf, "SC ", score_, 5);
                fb.drawText(buf, kFbW / 2 - 16, kFbH / 2 + 10, 3);
                break;
            }

            case GameState::Shop:
            {
                fb.drawText("SHOP", kFbW / 2 - 8, 4, 5);
                char buf[16];
                fmtNum(buf, sizeof buf, "DB ", currencyDB_, 4);
                fb.drawText(buf, 4, 12, 3);

                if (shop_ != nullptr)
                {
                    const auto& offers = shop_->offers();
                    for (int i = 0; i < (int) offers.size(); ++i)
                    {
                        const int y = 26 + i * 16;
                        const bool sel = (i == shopSlot_);
                        if (sel) fb.drawText(">", 2, y, 4);
                        fb.drawText(offers[(size_t) i].shortName, 10, y,
                                    (uint8_t) (sel ? 5 : 2));
                        char cb[12];
                        fmtNum(cb, sizeof cb, "", offers[(size_t) i].cost, 3);
                        fb.drawText(cb, kFbW - 20, y, 3);
                    }
                    char rb[20];
                    fmtNum(rb, sizeof rb, "R REROLL ", shop_->rerollCost(), 2);
                    fb.drawText(rb, 4, kFbH - 18, 2);
                }
                fb.drawText(shopFreeHealAvailable() ? "H HEAL" : "H USED",
                            4, kFbH - 11, 2);
                fb.drawText("SPACE READY", kFbW - 48, kFbH - 11, 4);
                break;
            }

            case GameState::Title:
            {
                fb.drawText("KICK", kFbW / 2 - 26, 14, 4);
                fb.drawText("IMPACT", kFbW / 2 + 2, 14, 4);
                fb.hline(kFbW / 2 - 30, kFbW / 2 + 30, 22, 3);

                static const char* kItems[] = {
                    "NEW GAME", "DAILY RUN", "HIGHSCORES", "HELP", "EXIT"
                };
                for (int i = 0; i < 5; ++i)
                {
                    const int y = 36 + i * 10;
                    const bool sel = (i == titleSel_);
                    if (sel) fb.drawText(">", 38, y, 4);
                    fb.drawText(kItems[i], 46, y, (uint8_t) (sel ? 5 : 2));
                }
                fb.drawText(daily_ ? "DAILY SEED" : "SEED", 4, kFbH - 6, 1);
                break;
            }

            case GameState::Paused:
            {
                fb.drawText("PAUSED", kFbW / 2 - 12, 18, 5);
                static const char* kItems[] = {
                    "RESUME", "RESTART", "HIGHSCORES", "HELP", "QUIT"
                };
                for (int i = 0; i < 5; ++i)
                {
                    const int y = 36 + i * 10;
                    const bool sel = (i == pauseSel_);
                    if (sel) fb.drawText(">", 38, y, 4);
                    fb.drawText(kItems[i], 46, y, (uint8_t) (sel ? 5 : 2));
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
                fb.drawText("HIGH SCORES", kFbW / 2 - 22, 2, 5);
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
                    fb.drawText("NO SCORES YET", kFbW / 2 - 26, kFbH / 2, 2);
                break;
            }

            case GameState::QuitConfirm:
            {
                fb.fillRect(kFbW / 2 - 36, kFbH / 2 - 12, 72, 24, 1);
                fb.drawText("QUIT", kFbW / 2 - 22, kFbH / 2 - 6, 5);
                fb.drawText("Y N", kFbW / 2 - 6, kFbH / 2 + 2, 4);
                break;
            }

            case GameState::Initials:
            {
                fb.drawText("ENTER NAME", kFbW / 2 - 20, 18, 5);
                for (int i = 0; i < 3; ++i)
                {
                    const int x = kFbW / 2 - 24 + i * 18;
                    const char letter[2] = { initials_[(size_t) i], '\0' };
                    const bool sel = (i == initialsSlot_);
                    // Draw the letter big-ish by stamping it then underlining.
                    fb.drawText(letter, x, kFbH / 2 - 4, (uint8_t) (sel ? 5 : 2));
                    if (sel) fb.hline(x, x + 4, kFbH / 2 + 4, 4);
                }
                char sb[16];
                fmtNum(sb, sizeof sb, "SC ", score_, 5);
                fb.drawText(sb, kFbW / 2 - 16, kFbH - 12, 3);
                break;
            }

            case GameState::GameOver:
            case GameState::Results:
            {
                fb.drawText(victory_ ? "VICTORY" : "GAME OVER",
                            kFbW / 2 - 18, 24, victory_ ? 5 : 4);
                char sb[16];
                fmtNum(sb, sizeof sb, "SC ", score_, 5);
                fb.drawText(sb, kFbW / 2 - 16, kFbH / 2, 3);
                fb.drawText("PRESS ENTER", kFbW / 2 - 22, kFbH - 16, 2);
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
    // HELD-MOVEMENT NOTE: JUCE keyPressed is edge-triggered; BBSComponent only
    // forwards keyPressed (no key-up, no per-frame polling today). So here we
    // (a) keep the setMoveInput(InputState) seam — the *intended* continuous
    // path — and (b) ALSO treat an arrow keyPressed as a one-shot nudge so the
    // player still moves under the current edge-only wiring. For smooth held
    // movement the BBSComponent timerCallback should poll
    // juce::KeyPress::isKeyCurrentlyDown(juce::KeyPress::leftKey) (and up/down/
    // right) each tick and call gameV2_.setMoveInput({up,down,left,right}); that
    // BBS-side wiring is left to the integration/acceptance task and does not
    // block this one. (DOCUMENTED.)
    //
    // ESC NOTE: BBSComponent intercepts escapeKey before handleKey is reached
    // (it calls exitGame()), so the ESC branches below mainly serve the unit
    // tests / future direct callers. (DOCUMENTED.)
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
                // Discrete nudge fallback (see HELD-MOVEMENT NOTE).
                if (isLeft)  { player_.x -= 6.0f; clampPlayerToField(); return true; }
                if (isRight) { player_.x += 6.0f; clampPlayerToField(); return true; }
                if (isUp)    { player_.y -= 6.0f; clampPlayerToField(); return true; }
                if (isDown)  { player_.y += 6.0f; clampPlayerToField(); return true; }
                if (ch == 'F' || key == KP::spaceKey) { setCharging(true); return true; }
                if (ch == 'A') { player_.autofireOn = ! player_.autofireOn; return true; }
                if (ch == 'P') { togglePause(); return true; }
                if (ch == 'H') { transitionTo(GameState::Help); return true; }
                if (isEsc)     { requestQuit(); return true; }
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
                if (isEsc) { requestQuit(); return true; }
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
                if (isLeft)  { shopMoveSelection(-1); return true; }
                if (isRight) { shopMoveSelection(+1); return true; }
                if (isEnter) { shopBuySelected();     return true; }
                if (ch == 'R') { shopReroll();        return true; }
                if (ch == 'H') { shopUseFreeHeal();   return true; }
                if (key == KP::spaceKey) { shopContinue(); return true; }
                if (isEsc) { requestQuit(); return true; }
                return false;
            }

            case GameState::Initials:
            {
                if (isLeft)  { initialsCycleLetter(initialsSlot_, -1); return true; }
                if (isRight) { initialsCycleLetter(initialsSlot_, +1); return true; }
                if (isUp)    { initialsMoveSlot(-1); return true; }
                if (isDown)  { initialsMoveSlot(+1); return true; }
                if (isEnter) { initialsConfirm();    return true; }
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
