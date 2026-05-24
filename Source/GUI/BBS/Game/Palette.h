#pragma once
#include <cstdint>
#include <string>

namespace bombo::game
{
    struct Palette
    {
        uint32_t bg, dim, mid, accent, hot, hilite;

        uint32_t byIndex(int i) const noexcept
        {
            switch (i)
            {
                case 0: return bg;
                case 1: return dim;
                case 2: return mid;
                case 3: return accent;
                case 4: return hot;
                case 5: return hilite;
                default: return bg;
            }
        }
    };

    // themeName is expected to be the lowercase theme key returned by
    // bombo::ThemeProvider::get().activeName() — e.g. "vault", "matrix",
    // "cyber", "plasma". Any other value (including "bandw", "nightrun",
    // empty string, or garbage) falls back to the MATRIX game palette.
    Palette getGamePalette(const std::string& themeName) noexcept;
}
