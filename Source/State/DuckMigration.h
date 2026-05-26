#pragma once

// Source/State/DuckMigration.h
//
// One-way migration of legacy duck routing state.
//
// Pre-Duck-Triangle-Cycle, Voice-A reverse-bass ducking was a single bool
// param "duck_voice_a". It has since been replaced by a 4-way Choice
// param "duck_routing" (Off / A / B / AB). Old DAW projects + factory
// presets persisted the bool — when those states load into the new APVTS
// they would silently set duck_routing=Off (APVTS ignores unknown params,
// the new param defaults to 0). This migration translates the bool into
// the equivalent Choice value so muscle memory survives the upgrade.
//
// Pure value-tree transform, no APVTS dependency, header-only so the
// unit-test target can link without pulling all of PluginProcessor.cpp.

#include <juce_data_structures/juce_data_structures.h>

namespace bombo
{

// If the loaded APVTS state has the legacy duck_voice_a bool, translate
// it to the new duck_routing Choice and remove the bool. true → routing
// index 1 (A) normalised 1/3 (4 choices: Off/A/B/AB). false → 0 (Off).
// If duck_routing is already present, it is preserved (no clobber).
// Returns the modified tree by value — ValueTree is reference-counted so
// this is cheap.
inline juce::ValueTree migrateDuckVoiceAToRouting(juce::ValueTree state)
{
    auto oldParam = state.getChildWithProperty("id", "duck_voice_a");
    if (! oldParam.isValid()) return state;

    const float boolVal = static_cast<float>(oldParam.getProperty("value", 0.0f));
    const float routingNorm = (boolVal >= 0.5f) ? (1.0f / 3.0f)   // index 1 (A) of 4 choices
                                                : 0.0f;            // index 0 (Off)

    // Replace or insert duck_routing PARAM.
    auto existing = state.getChildWithProperty("id", "duck_routing");
    if (existing.isValid())
    {
        existing.setProperty("value", routingNorm, nullptr);
    }
    else
    {
        juce::ValueTree p("PARAM");
        p.setProperty("id", "duck_routing", nullptr);
        p.setProperty("value", routingNorm, nullptr);
        state.appendChild(p, nullptr);
    }
    // Drop the legacy bool so APVTS doesn't see an orphan param.
    state.removeChild(oldParam, nullptr);
    return state;
}

} // namespace bombo
