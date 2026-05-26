// tests/PreGamePresetStashTests.cpp -- preset stash/restore around in-game Pew load.
//
// BBSComponent::launchGame() captures the user's current preset index, then
// force-loads "Pew" as the in-game shot sound; BBSComponent::exitGame()
// restores the captured index. The bug pinned here: if any BBS dismiss path
// (visibilityChanged in a host, hide() called without exitGame, etc.) lands
// without firing exitGame, the next launchGame would re-capture whatever
// preset is currently loaded -- which is Pew -- and on exit "restore" Pew,
// permanently losing the user's working preset.
//
// The fix is a tiny PreGamePresetStash helper with idempotent capture: a
// second capture while one is already live preserves the original index.
// hide() also gains an exitGame() call when in-game so the bypass paths
// can't strand Pew loaded in the first place; that wiring is tested via
// the existing GameStateTests + manual repro, not here.

#include <juce_audio_processors/juce_audio_processors.h>

#include "../Source/Parameters.h"
#include "../Source/State/PresetBank.h"
#include "../Source/State/PreGamePresetStash.h"

namespace
{

class StashDummyProcessor : public juce::AudioProcessor
{
public:
    StashDummyProcessor()
        : apvts(*this, nullptr, "BomboState", bombo::createParameterLayout()) {}

    const juce::String getName() const override            { return "StashDummy"; }
    void prepareToPlay(double, int) override                {}
    void releaseResources() override                        {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
    double getTailLengthSeconds() const override            { return 0.0; }
    bool acceptsMidi() const override                       { return false; }
    bool producesMidi() const override                      { return false; }
    juce::AudioProcessorEditor* createEditor() override     { return nullptr; }
    bool hasEditor() const override                         { return false; }
    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram(int) override                    {}
    const juce::String getProgramName(int) override         { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override   {}
    void setStateInformation(const void*, int) override     {}

    juce::AudioProcessorValueTreeState apvts;
};

class PreGamePresetStashTests : public juce::UnitTest
{
public:
    PreGamePresetStashTests()
        : juce::UnitTest("BBS: PreGamePresetStash (capture/restore around in-game Pew)") {}

    void runTest() override
    {
        StashDummyProcessor proc;
        auto& apvts = proc.apvts;
        bombo::PresetBank bank;

        const int n = bank.size();
        expect(n >= 3, "factory bank has at least 3 presets to play with");
        if (n < 3) return;

        // Stand-ins: idx 0 = user's working preset (analogue of Reverze 1),
        // idx 1 = the force-loaded in-game shot (analogue of Pew).
        const int userIdx = 0;
        const int pewIdx  = 1;

        beginTest("capture+restore round-trip puts current_ back to the captured index");
        {
            bank.applyByIndex(userIdx, apvts);
            expectEquals(bank.currentIndex(), userIdx);

            bombo::PreGamePresetStash stash;
            stash.capture(bank);
            expect(stash.isStashed(), "capture flagged");

            bank.applyByIndex(pewIdx, apvts);
            expectEquals(bank.currentIndex(), pewIdx, "Pew loaded mid-game");

            stash.restore(bank, apvts);
            expectEquals(bank.currentIndex(), userIdx, "user's preset restored");
            expect(! stash.isStashed(), "stash cleared after restore");
        }

        beginTest("restore re-applies the captured preset's params (not just the index)");
        {
            // userIdx and pewIdx must differ on at least one param for the
            // assertion below to be meaningful. They do (different factory
            // presets), but pick a param we know varies: master_out is in
            // kExcludedFromPresets so it can't help -- use one of the params
            // a sparse preset always touches. The simplest signal: any
            // param whose value at userIdx != value at pewIdx.
            bank.applyByIndex(userIdx, apvts);
            std::map<std::string, float> before;
            for (auto* p : apvts.processor.getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
                    before[rp->getParameterID().toStdString()] = rp->convertFrom0to1(rp->getValue());

            bombo::PreGamePresetStash stash;
            stash.capture(bank);
            bank.applyByIndex(pewIdx, apvts);
            stash.restore(bank, apvts);

            int mismatches = 0;
            for (auto* p : apvts.processor.getParameters())
            {
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
                {
                    const float now = rp->convertFrom0to1(rp->getValue());
                    const auto& id = rp->getParameterID().toStdString();
                    if (std::abs(now - before.at(id)) > 1.0e-6f)
                        ++mismatches;
                }
            }
            expectEquals(mismatches, 0, "every param matches the pre-game snapshot");
        }

        beginTest("re-entrant capture preserves the original index (the bypass-path defender)");
        {
            // The scenario: launchGame captures userIdx, Pew loaded, then a
            // non-exitGame BBS dismiss happens (so the stash is NOT cleared)
            // and the user opens BBS again and re-enters launchGame. The
            // SECOND capture must NOT clobber stashedIdx_ with the now-current
            // Pew index, or we'd "restore" Pew on exit.
            bank.applyByIndex(userIdx, apvts);

            bombo::PreGamePresetStash stash;
            stash.capture(bank);
            expectEquals(stash.stashedIndex(), userIdx, "first capture took");

            bank.applyByIndex(pewIdx, apvts);  // game force-loads Pew
            stash.capture(bank);               // bypass path causes re-entry
            expectEquals(stash.stashedIndex(), userIdx,
                         "second capture is a no-op while stashed");

            stash.restore(bank, apvts);
            expectEquals(bank.currentIndex(), userIdx,
                         "restore lands on the ORIGINAL preset, not Pew");
        }

        beginTest("capture with no preset loaded (currentIndex == -1) flags the stash but restore is a no-op");
        {
            bank.applyDefaults(apvts);
            expectEquals(bank.currentIndex(), -1, "no preset selected after applyDefaults");

            bombo::PreGamePresetStash stash;
            stash.capture(bank);
            // We still flag the stash so a subsequent in-stash capture
            // remains idempotent, but restore can't materialise a preset
            // from -1, so it leaves whatever the caller has loaded alone.
            expect(stash.isStashed(), "capture flagged even on -1");
            expectEquals(stash.stashedIndex(), -1);

            bank.applyByIndex(pewIdx, apvts);
            const int currentAfterPew = bank.currentIndex();
            stash.restore(bank, apvts);
            expectEquals(bank.currentIndex(), currentAfterPew,
                         "restore is a no-op when nothing real was stashed");
            expect(! stash.isStashed(), "stash still cleared after a -1 restore");
        }

        beginTest("restore on an empty stash is a no-op (defends against double-exit)");
        {
            bank.applyByIndex(pewIdx, apvts);
            bombo::PreGamePresetStash stash;
            // Never captured.
            stash.restore(bank, apvts);
            expectEquals(bank.currentIndex(), pewIdx,
                         "current_ untouched when nothing was stashed");
        }
    }
};

static PreGamePresetStashTests preGamePresetStashTests;

} // anonymous namespace
