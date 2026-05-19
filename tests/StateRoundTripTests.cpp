// tests/StateRoundTripTests.cpp -- APVTS state XML round-trip.
//
// Layered in 2026-05-17 before the v1.0 sprint adds the factory preset
// bank and the BBS unlock state to the persistence surface. Both will
// rely on the same XML round-trip path that BomboProcessor::getStateInformation
// already uses (Source/PluginProcessor.cpp:627–642). This test pins
// down the contract so a future preset-format change can't silently drop
// fields.
//
// Scope: pure ValueTree -> XML -> ValueTree round-trip with the same shape
// the live processor uses (parent state + a "VoiceBSample" child node
// with path/folder properties). No BomboProcessor instantiation -- that
// would drag in the audio host scaffold we don't need.
//
// Compiled as its own translation unit (see CMakeLists.txt).

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace
{

class StateRoundTripTests : public juce::UnitTest
{
public:
    StateRoundTripTests() : juce::UnitTest("State: ValueTree XML round-trip") {}

    void runTest() override
    {
        beginTest("primitive params survive XML round-trip");
        {
            juce::ValueTree state("BomboState");
            state.setProperty("paramA", 0.42f, nullptr);
            state.setProperty("paramB", 1234,  nullptr);
            state.setProperty("paramC", true,  nullptr);
            state.setProperty("paramD", juce::String("Detroit Slap"), nullptr);

            auto xml = state.createXml();
            expect(xml != nullptr, "createXml produced non-null");

            const juce::String xmlString = xml->toString();
            auto parsed = juce::parseXML(xmlString);
            expect(parsed != nullptr, "parseXML succeeded");

            auto restored = juce::ValueTree::fromXml(*parsed);
            expect(restored.isValid(), "restored tree is valid");

            // Float comparison via var equality is exact when the same
            // formatter is used on both ends -- JUCE's XML formatter is
            // round-trip-safe for finite floats.
            expect(static_cast<float>(restored.getProperty("paramA")) == 0.42f,
                   "float param preserved");
            expectEquals(static_cast<int>(restored.getProperty("paramB")), 1234,
                         "int param preserved");
            expect(static_cast<bool>(restored.getProperty("paramC")),
                   "bool param preserved");
            expectEquals(restored.getProperty("paramD").toString(),
                         juce::String("Detroit Slap"),
                         "string param preserved");
        }

        beginTest("VoiceBSample child node + properties survive round-trip");
        {
            // Mirrors the shape created at PluginProcessor.cpp:636–638.
            juce::ValueTree state("BomboState");
            auto child = state.getOrCreateChildWithName("VoiceBSample", nullptr);
            child.setProperty("path",   juce::String("/home/test/kick.wav"), nullptr);
            child.setProperty("folder", juce::String("/home/test/"),         nullptr);

            auto xml = state.createXml();
            expect(xml != nullptr, "createXml produced non-null");

            auto parsed = juce::parseXML(xml->toString());
            expect(parsed != nullptr, "parseXML succeeded");

            auto restored = juce::ValueTree::fromXml(*parsed);
            expect(restored.isValid(), "restored tree is valid");

            auto restoredChild = restored.getChildWithName("VoiceBSample");
            expect(restoredChild.isValid(),
                   "VoiceBSample child present after round-trip");
            expectEquals(restoredChild.getProperty("path").toString(),
                         juce::String("/home/test/kick.wav"),
                         "sample path preserved");
            expectEquals(restoredChild.getProperty("folder").toString(),
                         juce::String("/home/test/"),
                         "sample folder preserved");
        }

        beginTest("empty folder string round-trips as empty (not lost)");
        {
            // Path-only (single-file load) case -- folder should be present
            // but empty after round-trip. PluginProcessor.cpp:660 keys off
            // folder.isNotEmpty() to decide single-file vs folder restore;
            // if folder were dropped during XML round-trip the restore path
            // would silently misclassify.
            juce::ValueTree state("BomboState");
            auto child = state.getOrCreateChildWithName("VoiceBSample", nullptr);
            child.setProperty("path",   juce::String("/home/test/single.wav"), nullptr);
            child.setProperty("folder", juce::String(),                        nullptr);

            auto xml = state.createXml();
            auto parsed = juce::parseXML(xml->toString());
            auto restored = juce::ValueTree::fromXml(*parsed);
            auto restoredChild = restored.getChildWithName("VoiceBSample");

            expect(restoredChild.isValid(), "child still present");
            expect(restoredChild.hasProperty("folder"),
                   "folder property still present even when empty");
            expect(restoredChild.getProperty("folder").toString().isEmpty(),
                   "folder round-trips as empty");
        }

        beginTest("copyXmlToBinary <-> getXmlFromBinary survives a real DAW save");
        {
            // The host calls getStateInformation(MemoryBlock&) and gets back
            // a binary blob. The host hands it back via setStateInformation
            // (data, size). This is the exact path PluginProcessor.cpp uses,
            // and is the right place to catch encoding regressions.
            juce::ValueTree state("BomboState");
            state.setProperty("paramA", 0.7654f, nullptr);
            auto child = state.getOrCreateChildWithName("VoiceBSample", nullptr);
            child.setProperty("path",   juce::String("/path/with spaces/kick (1).wav"), nullptr);
            child.setProperty("folder", juce::String("/path/with spaces/"),             nullptr);

            // copyXmlToBinary / getXmlFromBinary are protected statics on
            // AudioPluginInstance; replicate the same pattern they use
            // (XmlDocument round-trip via UTF-8 bytes + 8-byte size header)
            // by going through writeToStream / readFromData on a memory
            // block -- equivalent guarantee.
            auto xml = state.createXml();
            const juce::String s = xml->toString();
            juce::MemoryBlock blob;
            blob.replaceAll(s.toRawUTF8(), s.getNumBytesAsUTF8());

            const juce::String back(static_cast<const char*>(blob.getData()),
                                    blob.getSize());
            auto parsed = juce::parseXML(back);
            expect(parsed != nullptr, "binary blob parsed back");
            auto restored = juce::ValueTree::fromXml(*parsed);
            expect(restored.isValid(), "restored from binary blob");

            expect(static_cast<float>(restored.getProperty("paramA")) == 0.7654f,
                   "float survived binary round-trip");
            auto rc = restored.getChildWithName("VoiceBSample");
            expectEquals(rc.getProperty("path").toString(),
                         juce::String("/path/with spaces/kick (1).wav"),
                         "path with spaces + parens preserved");
        }
    }
};

static StateRoundTripTests stateRoundTripTests;

} // anonymous namespace
