// Source/GUI/BBS/Game/Framebuffer.h
#pragma once
#include <array>
#include <cstdint>
#include <juce_graphics/juce_graphics.h>
#include "Constants.h"
#include "Palette.h"

namespace bombo::game
{
    class Framebuffer
    {
    public:
        Framebuffer();

        void    clear(uint8_t paletteIdx) noexcept;
        void    pset(int x, int y, uint8_t paletteIdx) noexcept;
        uint8_t peek(int x, int y) const noexcept;
        void    hline(int x0, int x1, int y, uint8_t idx) noexcept;
        void    vline(int x, int y0, int y1, uint8_t idx) noexcept;
        void    fillRect(int x, int y, int w, int h, uint8_t idx) noexcept;

        // Blit a w×h palette-index sprite at (dx, dy); index 0 is transparent.
        void blitSprite(const uint8_t* data, int w, int h, int dx, int dy) noexcept;

        // Draw null-terminated string using the 4×5 bitmap font from SpriteData.h.
        // Characters outside 0x20..0x7F are skipped. Stride is 4px per character.
        void drawText(const char* str, int x, int y, uint8_t idx) noexcept;

        // Resolve the palette-index buffer to ARGB into dst (must be kFbW×kFbH ARGB).
        void resolveToARGB(juce::Image& dst, const Palette& palette) const;

        static constexpr int width()  noexcept { return kFbW; }
        static constexpr int height() noexcept { return kFbH; }

    private:
        std::array<uint8_t, static_cast<size_t>(kFbW) * kFbH> pixels_{};
    };
} // namespace bombo::game
