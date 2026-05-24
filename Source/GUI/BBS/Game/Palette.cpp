#include "Palette.h"

namespace bombo::game
{
    static constexpr Palette kVault  { 0xff1a1612, 0xff5a5230, 0xffa08840, 0xfff0c040, 0xffc84020, 0xffe8d9b8 };
    static constexpr Palette kMatrix { 0xff020a04, 0xff0a3018, 0xff1e8048, 0xff3fd870, 0xff9aff8a, 0xffeafff0 };
    static constexpr Palette kCyber  { 0xff080018, 0xff280840, 0xff7008b8, 0xffff20a0, 0xff00e8ff, 0xfff8f8ff };
    static constexpr Palette kPlasma { 0xff1a0008, 0xff601830, 0xffe04060, 0xffff80c0, 0xffffd840, 0xff80f0e0 };

    Palette getGamePalette(const std::string& themeName) noexcept
    {
        if (themeName == "vault")  return kVault;
        if (themeName == "matrix") return kMatrix;
        if (themeName == "cyber")  return kCyber;
        if (themeName == "plasma") return kPlasma;
        return kMatrix;   // bandw / nightrun / unknown — safe default
    }
}
