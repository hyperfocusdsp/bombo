#pragma once

#include <array>
#include <atomic>

namespace bombo
{

// Lock-free MIDI CC <-> parameter map for MIDI Learn.
//
// The GUI thread "arms" a learn target (a parameter index); the audio thread
// binds the next incoming CC to that param and thereafter drives the param
// from that CC. One CC per param (re-binding a param moves it to the new CC);
// a CC drives exactly one param (re-using a CC re-points it).
//
// "Parameter index" is a stable index the owner assigns — e.g. the position in
// a filtered RangedAudioParameter list. Persistence maps index<->paramID
// OUTSIDE this class (in the processor) so the on-disk format survives
// parameter reordering between plugin versions.
//
// All state is plain atomics: the audio thread only ever does relaxed loads/
// stores and a bounded 128-iteration scan — no locks, no allocation.
class MidiLearn
{
public:
    static constexpr int kNumCc = 128;
    static constexpr int kNone  = -1;

    MidiLearn()
    {
        for (auto& c : ccToParam_) c.store(kNone, std::memory_order_relaxed);
    }

    // ---------------- GUI thread ----------------

    void arm(int paramIndex)  { learnTarget_.store(paramIndex, std::memory_order_relaxed); }
    void disarm()             { learnTarget_.store(kNone, std::memory_order_relaxed); }
    int  armedParam() const   { return learnTarget_.load(std::memory_order_relaxed); }
    bool isArmed() const      { return armedParam() != kNone; }

    // First CC (0..127) bound to paramIndex, or kNone.
    int ccForParam(int paramIndex) const
    {
        for (int cc = 0; cc < kNumCc; ++cc)
            if (ccToParam_[cc].load(std::memory_order_relaxed) == paramIndex) return cc;
        return kNone;
    }

    void forgetParam(int paramIndex)
    {
        for (int cc = 0; cc < kNumCc; ++cc)
            if (ccToParam_[cc].load(std::memory_order_relaxed) == paramIndex)
                ccToParam_[cc].store(kNone, std::memory_order_relaxed);
    }

    // Direct bind — used by state restore and tests. Enforces one CC per param.
    void bind(int cc, int paramIndex)
    {
        if (cc < 0 || cc >= kNumCc) return;
        forgetParam(paramIndex);
        ccToParam_[cc].store(paramIndex, std::memory_order_relaxed);
    }

    int paramForCc(int cc) const
    {
        return (cc >= 0 && cc < kNumCc) ? ccToParam_[cc].load(std::memory_order_relaxed) : kNone;
    }

    void clear()
    {
        for (auto& c : ccToParam_) c.store(kNone, std::memory_order_relaxed);
        learnTarget_.store(kNone, std::memory_order_relaxed);
    }

    // ---------------- Audio thread ----------------

    // Handle an incoming CC. If armed: bind the armed param to this CC, disarm,
    // record it for GUI feedback, flag dirty (so the GUI re-persists), and
    // return kNone (nothing to apply this time). Otherwise return the param
    // index this CC drives (or kNone if unmapped).
    int handleCc(int cc)
    {
        const int armed = learnTarget_.load(std::memory_order_relaxed);
        if (armed != kNone)
        {
            bind(cc, armed);
            learnTarget_.store(kNone, std::memory_order_relaxed);
            lastBound_.store(armed, std::memory_order_relaxed);
            dirty_.store(true, std::memory_order_relaxed);
            return kNone;
        }
        return paramForCc(cc);
    }

    // ---------------- GUI polling ----------------

    // True once after a bind/forget changed the map (so the GUI re-serializes).
    bool consumeDirty()    { return dirty_.exchange(false, std::memory_order_relaxed); }
    void markDirty()       { dirty_.store(true, std::memory_order_relaxed); }
    int  lastBound() const { return lastBound_.load(std::memory_order_relaxed); }

private:
    std::array<std::atomic<int>, kNumCc> ccToParam_;
    std::atomic<int>  learnTarget_ { kNone };
    std::atomic<int>  lastBound_   { kNone };
    std::atomic<bool> dirty_       { false };
};

} // namespace bombo
