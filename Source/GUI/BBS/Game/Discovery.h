// Source/GUI/BBS/Game/Discovery.h
#pragma once
#include <random>
#include "Constants.h"

namespace bombo::game
{
    // The BBS "discovery" mechanic: while the BBS overlay is open, a tiny 8-bit
    // invader sprite drifts across the scope strip every 60-120s. The first time
    // one is ever seen, a cabinet glyph in the BBS header powers on (persisted by
    // the caller). Clicking the invader (or the lit cabinet) launches the game.
    //
    // This class owns ONLY the spawn cadence + drift + hit-test logic so it can be
    // unit-tested headlessly. Rendering + persistence + click→launch live in the
    // BBSComponent wiring.
    class Discovery
    {
    public:
        explicit Discovery(std::mt19937& rng) : rng_(rng) {}

        // dt seconds; bbsVisible = BBS overlay open (and not in-game).
        // Advances the spawn timer + invader drift.
        void tick(float dt, bool bbsVisible);

        bool  hasActiveInvader() const noexcept { return invaderActive_; }
        float invaderX() const noexcept { return invaderX_; }
        float invaderY() const noexcept { return invaderY_; }

        // Hit-test a click (in the same logical coord space as invaderX/Y);
        // despawns + returns true on hit.
        bool tryHitInvader(float mx, float my);

        // True exactly once, the first time an invader ever spawns
        // (caller persists cabinetLit then).
        bool consumePoweredOnEvent();

    private:
        std::mt19937& rng_;
        bool  invaderActive_ = false;
        float invaderX_ = 0.0f, invaderY_ = 0.0f, invaderTtl_ = 0.0f;
        float nextSpawnTimer_ = 0.0f;
        bool  poweredOnEvent_ = false;
        bool  everSeenInvader_ = false;
    };
}
