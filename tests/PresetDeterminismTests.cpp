// tests/PresetDeterminismTests.cpp -- factory presets load deterministically.
//
// Factory presets are sparse: each JSON lists only the params that matter
// to that sound. PresetBank::applyByIndex must therefore reset every
// non-excluded param to its default before overlaying the preset, or a
// param a preset omits silently inherits whatever the previously loaded
// preset left behind -- so the same preset sounds different depending on
// load order, and params added after a preset was authored (kbtrk,
// voice_b_synth_on, reverb_type, dec_routing) never return to default.
//
// This pins the contract: "preset B loaded after preset A" must equal
// "preset B loaded from defaults", for every parameter.
//
// Compiled as its own translation unit (see CMakeLists.txt); links the
// real APVTS layout (Source/Parameters.h) and PresetBank.cpp + BinaryData.

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <string>

#include "../Source/ParameterIds.h"
#include "../Source/Parameters.h"
#include "../Source/State/PresetBank.h"

namespace
{

// Minimal host for an APVTS built from the real parameter layout. None of
// the audio plumbing is exercised -- we only need live RangedAudioParameters
// with the production defaults and ranges so convertTo0to1 round-trips
// exactly as it does in the plugin.
class DummyProcessor : public juce::AudioProcessor
{
public:
    DummyProcessor()
        : apvts(*this, nullptr, "BomboState", bombo::createParameterLayout()) {}

    const juce::String getName() const override            { return "Dummy"; }
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

// Plain-value snapshot of every parameter (plain, not normalized). Used to
// compare full plugin state between two load paths.
std::map<std::string, float> snapshot(juce::AudioProcessorValueTreeState& apvts)
{
    std::map<std::string, float> out;
    for (auto* p : apvts.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p))
            out[rp->getParameterID().toStdString()] = rp->convertFrom0to1(rp->getValue());
    return out;
}

class PresetDeterminismTests : public juce::UnitTest
{
public:
    PresetDeterminismTests()
        : juce::UnitTest("Presets: deterministic apply (no bleed)") {}

    void runTest() override
    {
        DummyProcessor proc;
        auto& apvts = proc.apvts;
        bombo::PresetBank bank;

        beginTest("factory bank has enough presets to test ordering");
        const int n = bank.size();
        expect(n >= 2, "need at least two factory presets");
        if (n < 2) return;

        // Pick two presets whose param sets differ. PULSE (0) sets reverb /
        // drive_mode / click_center; SUB-RUMBLE is sparse and omits them.
        const int a = 0;
        int b = -1;
        for (int i = 1; i < n; ++i)
            if (juce::String(bank.at(i).displayName).containsIgnoreCase("rumble")) { b = i; break; }
        if (b < 0) b = (n > 5 ? 5 : n - 1);

        beginTest("preset B after preset A == preset B from defaults");
        {
            bank.applyByIndex(a, apvts);
            bank.applyByIndex(b, apvts);
            const auto afterA = snapshot(apvts);

            bank.applyDefaults(apvts);
            bank.applyByIndex(b, apvts);
            const auto fromDefaults = snapshot(apvts);

            int mismatches = 0;
            for (const auto& kv : fromDefaults)
            {
                const auto it = afterA.find(kv.first);
                if (it == afterA.end()) { ++mismatches; continue; }
                if (std::abs(it->second - kv.second) > 1.0e-6f)
                {
                    ++mismatches;
                    logMessage("  bleed on " + juce::String(kv.first)
                               + ": afterA=" + juce::String(it->second)
                               + " fromDefaults=" + juce::String(kv.second));
                }
            }
            expectEquals(mismatches, 0,
                         "no parameter differs between the two load paths");
        }

        beginTest("loading a preset restores params it omits to their default");
        {
            // Capture clean defaults.
            bank.applyDefaults(apvts);
            const auto defaults = snapshot(apvts);

            // Dirty a param that the sparse preset B does not list, then load
            // B and confirm the param snapped back to its default.
            auto* kbtrk = apvts.getParameter(bombo::pid::kbtrk);
            expect(kbtrk != nullptr, "kbtrk param exists");
            if (kbtrk != nullptr)
            {
                kbtrk->beginChangeGesture();
                kbtrk->setValueNotifyingHost(1.0f);  // force ON
                kbtrk->endChangeGesture();

                bank.applyByIndex(b, apvts);

                auto* rp = dynamic_cast<juce::RangedAudioParameter*>(kbtrk);
                const float now = rp->convertFrom0to1(rp->getValue());
                expectWithinAbsoluteError(now, defaults.at(bombo::pid::kbtrk),
                                          1.0e-6f,
                                          "kbtrk returned to default after load");
            }
        }
    }
};

static PresetDeterminismTests presetDeterminismTests;

} // anonymous namespace
