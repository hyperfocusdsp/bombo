#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <juce_core/juce_core.h>

namespace bombo
{

// Identity of each reorderable FX stage. VOICE A/B sit upstream of the
// RumbleChain (they sum into `dry`) and DUCKER sits downstream — only the
// four stages below are user-reorderable.
enum class FxId : std::uint8_t { Drive = 0, Filter = 1, Delay = 2, Reverb = 3 };

using FxOrder = std::array<FxId, 4>;

inline constexpr FxOrder kDefaultFxOrder {
    FxId::Drive, FxId::Filter, FxId::Delay, FxId::Reverb
};

// String <-> FxId for preset serialization. Lowercase, stable forever.
inline const char* fxIdToString(FxId f) noexcept
{
    switch (f) {
        case FxId::Drive:  return "drive";
        case FxId::Filter: return "filter";
        case FxId::Delay:  return "delay";
        case FxId::Reverb: return "reverb";
    }
    return "drive";
}

inline bool fxIdFromString(const juce::String& s, FxId& out) noexcept
{
    if      (s == "drive")  { out = FxId::Drive;  return true; }
    else if (s == "filter") { out = FxId::Filter; return true; }
    else if (s == "delay")  { out = FxId::Delay;  return true; }
    else if (s == "reverb") { out = FxId::Reverb; return true; }
    return false;
}

// True when every FxId appears exactly once. Sanitizer for incoming
// preset / APVTS data — never trust an externally-supplied order.
inline bool isValidFxOrder(const FxOrder& o) noexcept
{
    bool seen[4] = { false, false, false, false };
    for (auto f : o)
    {
        const auto i = static_cast<std::size_t>(f);
        if (i >= 4 || seen[i]) return false;
        seen[i] = true;
    }
    return true;
}

} // namespace bombo
