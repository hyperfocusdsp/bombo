// tests/GameWavesTests.cpp
#include "GUI/BBS/Game/Waves.h"
#include "GUI/BBS/Game/Entities.h"
#include <juce_core/juce_core.h>

namespace
{
using namespace bombo::game;

class WavesSameSeedSameSpawnsTest : public juce::UnitTest
{
public:
    WavesSameSeedSameSpawnsTest() : juce::UnitTest("Waves: same seed produces same spawn sequence") {}
    void runTest() override
    {
        beginTest("two pools fed identical (seed, wave) end up with identical active enemies");
        EnemyPool a, b;
        runWave(/*seed=*/1234u, /*waveIdx=*/3, a);
        runWave(/*seed=*/1234u, /*waveIdx=*/3, b);
        for (int i = 0; i < EnemyPool::kMax; ++i)
        {
            const auto& ea = a.enemies()[i];
            const auto& eb = b.enemies()[i];
            expect(ea.active == eb.active);
            if (ea.active)
            {
                expect(ea.kind == eb.kind);
                expectWithinAbsoluteError(ea.x, eb.x, 0.01f);
                expectWithinAbsoluteError(ea.y, eb.y, 0.01f);
            }
        }
    }
};

class WavesDifferentSeedDiffersTest : public juce::UnitTest
{
public:
    WavesDifferentSeedDiffersTest() : juce::UnitTest("Waves: different seeds usually differ") {}
    void runTest() override
    {
        beginTest("two different seeds produce a different spawn layout for the same wave");
        EnemyPool a, b;
        runWave(1u, 5, a);
        runWave(999999u, 5, b);
        bool anyDifference = false;
        for (int i = 0; i < EnemyPool::kMax; ++i)
        {
            const auto& ea = a.enemies()[i];
            const auto& eb = b.enemies()[i];
            if (ea.active != eb.active || (ea.active && (ea.kind != eb.kind ||
                std::abs(ea.x - eb.x) > 0.01f || std::abs(ea.y - eb.y) > 0.01f)))
            { anyDifference = true; break; }
        }
        expect(anyDifference);
    }
};

class WavesW1OnlyMudballTest : public juce::UnitTest
{
public:
    WavesW1OnlyMudballTest() : juce::UnitTest("Waves: W1 archetype only spawns Mudball") {}
    void runTest() override
    {
        beginTest("wave 1 contains only Mudball enemies");
        EnemyPool pool;
        runWave(1u, 1, pool);
        int active = 0;
        for (const auto& e : pool.enemies())
            if (e.active) { ++active; expect(e.kind == EnemyKind::Mudball); }
        expectGreaterThan(active, 0);
    }
};

class WaveScheduleDripFeedsTest : public juce::UnitTest
{
public:
    WaveScheduleDripFeedsTest() : juce::UnitTest("WaveSchedule: spawns drip in over time, not all at once") {}
    void runTest() override
    {
        beginTest("scheduleWave produces events with increasing timestamps; ticking drip-feeds the pool");
        auto sched = scheduleWave(42u, 4);
        expect(! sched.events.empty());
        // events should be time-ordered non-decreasing
        for (size_t i = 1; i < sched.events.size(); ++i)
            expect(sched.events[i].t >= sched.events[i-1].t);

        EnemyPool pool;
        // Tick a small dt — not all events should fire immediately.
        sched.tick(pool, 0.1f);
        int earlyActive = 0; for (const auto& e : pool.enemies()) if (e.active) ++earlyActive;
        // Now advance well past the last event time.
        sched.tick(pool, 100.0f);
        int lateActive = 0; for (const auto& e : pool.enemies()) if (e.active) ++lateActive;
        expectGreaterThan(lateActive, earlyActive);
        expect(sched.done());
    }
};

static WavesSameSeedSameSpawnsTest    a;
static WavesDifferentSeedDiffersTest  b;
static WavesW1OnlyMudballTest         c;
static WaveScheduleDripFeedsTest      d;
} // namespace
