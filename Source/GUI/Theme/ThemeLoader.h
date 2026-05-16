#pragma once

#include "Palette.h"

#include <juce_core/juce_core.h>

#include <string>

namespace bombo
{

class ThemeLoader
{
public:
    struct Result
    {
        bool ok = false;
        std::string name;
        std::string displayName;
        Palette palette;
        std::string error;   // populated when ok == false
    };

    // Parse a JSON string. Caller checks Result::ok.
    static Result parse(juce::StringRef json);
};

} // namespace bombo
