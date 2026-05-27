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

#include <algorithm>
#include <map>
#include <string>
#include <vector>

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

        // Determinism is a property of the FACTORY bank. A dev box may have
        // user presets saved on disk (the ctor loads them via userPresetsDir),
        // and those must not perturb this check or its preset selection -- so
        // we only ever consider Source::Factory entries here.
        std::vector<int> factory;
        for (int i = 0; i < n; ++i)
            if (bank.at(i).source == bombo::PresetBank::Source::Factory)
                factory.push_back(i);
        expect(factory.size() >= 2, "need at least two factory presets");
        if (factory.size() < 2) return;

        const int a = factory[0];
        const int b = factory[1];

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

            auto isExcluded = [](const std::string& id)
            {
                for (const char* e : bombo::kExcludedFromPresets)
                    if (id == e) return true;
                return false;
            };

            // Find a factory preset together with a non-excluded param it omits,
            // so we genuinely exercise applyByIndex's "reset omitted params to
            // default" contract. The curated bank is near-exhaustive and no
            // longer has a single canonical sparse preset, so we DISCOVER an
            // omission rather than assume a specific param/preset.
            int useIdx = -1;
            std::string omitted;
            for (int idx : factory)
            {
                const auto& pp = bank.at(idx).params;
                for (const auto& kv : defaults)
                {
                    if (isExcluded(kv.first)) continue;
                    const bool listed = std::any_of(pp.begin(), pp.end(),
                        [&](const auto& q) { return q.first == kv.first; });
                    if (! listed) { useIdx = idx; omitted = kv.first; break; }
                }
                if (useIdx >= 0) break;
            }
            expect(useIdx >= 0, "a factory preset omits at least one non-excluded param");
            if (useIdx >= 0)
            {
                auto* p = apvts.getParameter(juce::String(omitted));
                expect(p != nullptr, "omitted param resolves in the APVTS");
                if (p != nullptr)
                {
                    // Dirty the omitted param away from its default, load the
                    // preset that omits it, and confirm it snapped back.
                    const float dflt  = p->getDefaultValue();
                    const float dirty = dflt < 0.5f ? 1.0f : 0.0f;
                    p->beginChangeGesture();
                    p->setValueNotifyingHost(dirty);
                    p->endChangeGesture();

                    bank.applyByIndex(useIdx, apvts);

                    auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
                    const float now = rp->convertFrom0to1(rp->getValue());
                    expectWithinAbsoluteError(now, defaults.at(omitted), 1.0e-6f,
                        "omitted param '" + juce::String(omitted)
                            + "' returned to default after load");
                }
            }
        }
    }
};

static PresetDeterminismTests presetDeterminismTests;

} // anonymous namespace
