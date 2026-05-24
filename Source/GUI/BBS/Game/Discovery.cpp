// Source/GUI/BBS/Game/Discovery.cpp
#include "Discovery.h"
#include <cmath>

namespace bombo::game
{
    void Discovery::tick(float dt, bool bbsVisible)
    {
        if (! bbsVisible)
            return;  // the discovery clock only runs while the BBS is open

        if (invaderActive_)
        {
            invaderX_   += kInvaderDriftPxS * dt;
            invaderTtl_ -= dt;

            // Despawn once it drifts past the right edge or its life expires.
            if (invaderTtl_ <= 0.0f
                || invaderX_ > static_cast<float>(kFbW) + 16.0f)
            {
                invaderActive_ = false;
            }
            return;
        }

        // No active invader — count down toward the next spawn. Re-arm the timer
        // with a fresh random interval whenever it's been consumed.
        if (nextSpawnTimer_ <= 0.0f)
        {
            std::uniform_real_distribution<float> dist(kInvaderMinIntervalSec,
                                                       kInvaderMaxIntervalSec);
            nextSpawnTimer_ = dist(rng_);
        }

        nextSpawnTimer_ -= dt;
        if (nextSpawnTimer_ <= 0.0f)
        {
            // Spawn: enter from the left edge, drift right, random y across the
            // scope-strip band (a few px of vertical margin top + bottom).
            invaderActive_ = true;
            invaderX_      = -10.0f;
            invaderTtl_    = kInvaderOnScreenSec;

            std::uniform_real_distribution<float> yDist(6.0f,
                                                        static_cast<float>(kFbH) - 6.0f);
            invaderY_ = yDist(rng_);

            // Re-arm for the following spawn on the next idle tick.
            nextSpawnTimer_ = 0.0f;

            if (! everSeenInvader_)
            {
                everSeenInvader_ = true;
                poweredOnEvent_  = true;
            }
        }
    }

    bool Discovery::tryHitInvader(float mx, float my)
    {
        if (! invaderActive_)
            return false;

        const float dx = mx - invaderX_;
        const float dy = my - invaderY_;
        const float r  = 6.0f;  // generous ~6px hit radius
        if (dx * dx + dy * dy <= r * r)
        {
            invaderActive_ = false;
            return true;
        }
        return false;
    }

    bool Discovery::consumePoweredOnEvent()
    {
        const bool fired = poweredOnEvent_;
        poweredOnEvent_ = false;
        return fired;
    }
}
