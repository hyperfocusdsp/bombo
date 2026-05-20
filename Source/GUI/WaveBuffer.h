#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>

namespace bombo
{

// Trigger-locked capture buffer for the scope display.
//
// On each new kick trigger the audio thread calls triggerReset(), which saves
// the effective tail length from the previous cycle (prevLength_), resets the
// write position to zero, and bumps triggerVersion_.  Subsequent push() calls
// fill the buffer left-to-right; pushes past kCapture are silently dropped.
//
// lastLoudIdx_ tracks the last decimated position where |sample| exceeded
// kSilenceFloor.  At reset time prevLength_ is set to lastLoudIdx_ + a short
// grace pad so the GUI can scale the X axis to exactly the previous sound's
// duration without showing lots of silent headroom.
//
// Memory ordering: prevLength_ and writePos_ are both stored with release
// before triggerVersion_ is bumped (also release), so a GUI thread that
// acquires triggerVersion_ is guaranteed to see up-to-date prevLength_ and
// writePos_ == 0.
class WaveBuffer
{
public:
    static constexpr int   kCapture     = 8192;
    static constexpr int   kDecim       = 24;
    static constexpr float kSilenceFloor = 0.002f;  // ≈ −54 dBFS
    static constexpr int   kSilencePad  = 80;       // ≈ 40 ms of grace after last loud sample
    // Cap on prevLength_ — ≈ 750 ms at 48 kHz / kDecim=24. Without this a
    // long reverb tail bleeding above kSilenceFloor pushes prevLength_ into
    // multi-second territory, which compresses the next dry kick to 5-15 %
    // of the scope's X axis.
    static constexpr int   kMaxPrevLength = 1500;

    // Audio thread: call once when a new trigger fires. RT-safe.
    void triggerReset() noexcept
    {
        // Compute effective tail length from the just-finished capture cycle.
        const int tail = lastLoudIdx_ > 0
                       ? std::min({lastLoudIdx_ + kSilencePad, kCapture, kMaxPrevLength})
                       : 0;
        prevLength_.store(tail, std::memory_order_release);

        capturePos_   = 0;
        lastLoudIdx_  = 0;
        pendingCount_ = 0;
        writePos_.store(0, std::memory_order_release);
        triggerVersion_.fetch_add(1, std::memory_order_release);
    }

    // Audio thread: post-master sample feed. RT-safe.
    void push(float s) noexcept
    {
        if (++pendingCount_ >= kDecim)
        {
            pendingCount_ = 0;
            const int pos = capturePos_;
            if (pos < kCapture)
            {
                buffer_[static_cast<std::size_t>(pos)] = s;
                capturePos_ = pos + 1;
                writePos_.store(capturePos_, std::memory_order_release);
                if (s > kSilenceFloor || s < -kSilenceFloor)
                    lastLoudIdx_ = capturePos_;
            }
        }
    }

    // Audio thread: call from prepareToPlay / reset.
    void clear() noexcept
    {
        for (auto& s : buffer_) s = 0.0f;
        capturePos_   = 0;
        lastLoudIdx_  = 0;
        pendingCount_ = 0;
        writePos_.store(0, std::memory_order_release);
    }

    // GUI thread accessors.
    int          triggerVersion() const noexcept { return triggerVersion_.load(std::memory_order_acquire); }
    int          writePos()       const noexcept { return writePos_.load(std::memory_order_acquire); }
    // Effective tail length of the PREVIOUS trigger cycle. 0 on the very first
    // trigger (no history yet). GUI uses this to normalise the X axis.
    int          prevLength()     const noexcept { return prevLength_.load(std::memory_order_acquire); }
    const float* data()           const noexcept { return buffer_.data(); }

private:
    std::array<float, kCapture> buffer_{};
    std::atomic<int> triggerVersion_{0};
    std::atomic<int> writePos_{0};
    std::atomic<int> prevLength_{0};
    int capturePos_   = 0;  // Audio thread only.
    int lastLoudIdx_  = 0;  // Audio thread only.
    int pendingCount_ = 0;  // Audio thread only.
};

} // namespace bombo
