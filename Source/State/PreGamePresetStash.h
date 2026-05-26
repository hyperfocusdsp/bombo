#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "PresetBank.h"

namespace bombo
{

// Stash + restore the user's "working" preset around the in-game forced
// "Pew" load. Lives on BBSComponent. The two non-obvious contracts:
//
//   1. capture() is IDEMPOTENT while a stash is live. The launchGame()
//      re-entrancy guard already blocks a second launch while the BBS is
//      on the Game screen, but a non-exitGame dismiss path (visibilityChanged
//      in a DAW host, hide() called externally) can leave the BBS off the
//      Game screen while Pew is still the bank's "current_". A second
//      launchGame() then runs through the guard, and a naive
//      "stashedIdx_ = currentIndex()" would capture Pew as the "user's
//      preset" and on exit "restore" Pew, permanently losing the original.
//      Idempotent capture preserves the FIRST captured index until restore.
//
//   2. restore() with stashedIdx_ < 0 is a no-op on the bank's params (we
//      can't synthesise a preset from "nothing was loaded"). It still
//      clears the stash so the next capture starts fresh.
class PreGamePresetStash
{
public:
    void capture(const PresetBank& bank) noexcept
    {
        if (stashed_) return;             // idempotent: preserve the first capture
        stashedIdx_ = bank.currentIndex();
        stashed_    = true;
    }

    void restore(PresetBank& bank, juce::AudioProcessorValueTreeState& apvts)
    {
        if (! stashed_) return;
        if (stashedIdx_ >= 0)
            bank.applyByIndex(stashedIdx_, apvts);
        stashedIdx_ = -1;
        stashed_    = false;
    }

    bool isStashed()    const noexcept { return stashed_; }
    int  stashedIndex() const noexcept { return stashedIdx_; }

private:
    int  stashedIdx_ = -1;
    bool stashed_    = false;
};

} // namespace bombo
