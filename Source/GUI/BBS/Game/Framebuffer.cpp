// Source/GUI/BBS/Game/Framebuffer.cpp
#include "Framebuffer.h"
#include "SpriteData.h"
#include <algorithm>

namespace bombo::game
{
    Framebuffer::Framebuffer() { clear(0); }

    void Framebuffer::clear(uint8_t idx) noexcept
    {
        pixels_.fill(idx);
    }

    void Framebuffer::pset(int x, int y, uint8_t idx) noexcept
    {
        if ((unsigned) x >= (unsigned) kFbW || (unsigned) y >= (unsigned) kFbH) return;
        pixels_[static_cast<size_t>(y) * kFbW + x] = idx;
    }

    uint8_t Framebuffer::peek(int x, int y) const noexcept
    {
        if ((unsigned) x >= (unsigned) kFbW || (unsigned) y >= (unsigned) kFbH) return 0;
        return pixels_[static_cast<size_t>(y) * kFbW + x];
    }

    void Framebuffer::hline(int x0, int x1, int y, uint8_t idx) noexcept
    {
        if (x0 > x1) std::swap(x0, x1);
        for (int x = x0; x <= x1; ++x) pset(x, y, idx);
    }

    void Framebuffer::vline(int x, int y0, int y1, uint8_t idx) noexcept
    {
        if (y0 > y1) std::swap(y0, y1);
        for (int y = y0; y <= y1; ++y) pset(x, y, idx);
    }

    void Framebuffer::fillRect(int x, int y, int w, int h, uint8_t idx) noexcept
    {
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx) pset(x + xx, y + yy, idx);
    }

    void Framebuffer::blitSprite(const uint8_t* data, int w, int h, int dx, int dy) noexcept
    {
        for (int yy = 0; yy < h; ++yy)
            for (int xx = 0; xx < w; ++xx)
            {
                uint8_t v = data[yy * w + xx];
                if (v != 0) pset(dx + xx, dy + yy, v);
            }
    }

    void Framebuffer::drawText(const char* str, int x, int y, uint8_t idx) noexcept
    {
        int cx = x;
        for (const char* p = str; *p; ++p, cx += 5)   // 4px glyph + 1px gap
        {
            auto c = static_cast<unsigned char>(*p);
            if (c < 32 || c > 127) continue;
            const uint8_t* glyph = kFont[c - 32]; // 4 columns, 5 bits each
            for (int col = 0; col < 4; ++col)
            {
                uint8_t bits = glyph[col];
                for (int row = 0; row < 5; ++row)
                    if (bits & (1u << row)) pset(cx + col, y + row, idx);
            }
        }
    }

    int Framebuffer::textWidth(const char* str) noexcept
    {
        int n = 0;
        for (const char* p = str; *p; ++p) ++n;
        return n > 0 ? n * 5 - 1 : 0;   // 5px stride, drop the trailing gap
    }

    void Framebuffer::drawTextCentered(const char* str, int y, uint8_t idx) noexcept
    {
        drawText(str, (kFbW - textWidth(str)) / 2, y, idx);
    }

    void Framebuffer::resolveToARGB(juce::Image& dst, const Palette& palette) const
    {
        jassert(dst.getWidth() == kFbW && dst.getHeight() == kFbH);
        juce::Image::BitmapData bmp(dst, juce::Image::BitmapData::writeOnly);
        for (int row = 0; row < kFbH; ++row)
        {
            uint8_t* line = bmp.getLinePointer(row);
            const uint8_t* src = &pixels_[static_cast<size_t>(row) * kFbW];
            for (int col = 0; col < kFbW; ++col)
            {
                const uint32_t argb = palette.byIndex(src[col]);
                // Palette values are 0xAARRGGBB.
                // Use PixelARGB index enums so byte layout is always correct.
                uint8_t* px = line + col * 4;
                px[juce::PixelARGB::indexA] = static_cast<uint8_t>((argb >> 24) & 0xFFu);
                px[juce::PixelARGB::indexR] = static_cast<uint8_t>((argb >> 16) & 0xFFu);
                px[juce::PixelARGB::indexG] = static_cast<uint8_t>((argb >>  8) & 0xFFu);
                px[juce::PixelARGB::indexB] = static_cast<uint8_t>( argb        & 0xFFu);
            }
        }
    }
} // namespace bombo::game
