#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace bombo
{

// Lock-free SPSC ring for audio-thread → GUI-thread scope rendering. The
// processor pushes one sample per audio sample; we sub-sample by kDecim
// before writing, so the ring holds ~500 ms of mono signal at 48 kHz.
//
// Atomic write position with release ordering on the producer side and
// acquire ordering on the consumer side gives the right happens-before
// for the buffer contents without any locking. The single relaxed store
// path is allocator-free, which matters because push() is called inside
// processBlock.
class WaveBuffer
{
public:
    static constexpr int kSize  = 1024;
    static constexpr int kMask  = kSize - 1;
    static constexpr int kDecim = 24;  // ~500 ms display window at 48 kHz

    // Producer. RT-safe — no allocation, single atomic store every kDecim
    // calls. The dropped samples in between are fine for scope display;
    // we're showing a downsampled scope, not capturing every sample.
    void push(float s) noexcept
    {
        if (++pendingCount_ >= kDecim)
        {
            const int w = writePos_.load(std::memory_order_relaxed);
            buffer_[static_cast<std::size_t>(w & kMask)] = s;
            writePos_.store(w + 1, std::memory_order_release);
            pendingCount_ = 0;
        }
    }

    // Consumer. Reads the most recent `n` ring entries into `dst` in
    // chronological order (oldest first). Reading slightly stale data is
    // fine — the scope re-renders at 30 Hz anyway.
    void readLatest(float* dst, int n) const noexcept
    {
        if (n > kSize) n = kSize;
        const int w = writePos_.load(std::memory_order_acquire);
        for (int i = 0; i < n; ++i)
            dst[i] = buffer_[static_cast<std::size_t>((w - n + i) & kMask)];
    }

    void clear() noexcept
    {
        for (auto& s : buffer_) s = 0.0f;
        writePos_.store(0, std::memory_order_release);
        pendingCount_ = 0;
    }

private:
    std::array<float, kSize> buffer_{};
    std::atomic<int> writePos_{0};
    int pendingCount_ = 0;  // Audio thread only — no atomic needed.
};

} // namespace bombo
