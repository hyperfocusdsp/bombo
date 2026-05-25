// tests/MidiLearnTests.cpp -- MIDI Learn CC<->param map invariants.
//
// The map is the engine behind right-click-knob MIDI Learn: the GUI arms a
// param, the audio thread binds the next CC and thereafter drives the param.
// Pins: arm/bind handshake, one-CC-per-param, one-param-per-CC, forget, and
// the dirty flag the GUI uses to re-persist.

#include <juce_core/juce_core.h>

#include "../Source/State/MidiLearn.h"

namespace
{

class MidiLearnTests : public juce::UnitTest
{
public:
    MidiLearnTests() : juce::UnitTest("MidiLearn: CC<->param map") {}

    void runTest() override
    {
        using bombo::MidiLearn;

        beginTest("arm -> first CC binds, disarms, applies nothing that turn");
        {
            MidiLearn ml;
            expect(! ml.isArmed());
            ml.arm(7);
            expect(ml.isArmed());
            expectEquals(ml.armedParam(), 7);

            const int applied = ml.handleCc(20);   // learn turn
            expectEquals(applied, MidiLearn::kNone, "learn turn applies nothing");
            expect(! ml.isArmed(), "disarmed after bind");
            expectEquals(ml.paramForCc(20), 7);
            expectEquals(ml.ccForParam(7), 20);
            expectEquals(ml.lastBound(), 7);
        }

        beginTest("subsequent CC drives the bound param");
        {
            MidiLearn ml;
            ml.arm(3);
            ml.handleCc(64);
            expectEquals(ml.handleCc(64), 3, "mapped CC returns its param");
            expectEquals(ml.handleCc(65), MidiLearn::kNone, "unmapped CC returns none");
        }

        beginTest("one CC per param -- re-learning moves the binding");
        {
            MidiLearn ml;
            ml.bind(10, 5);
            expectEquals(ml.ccForParam(5), 10);
            ml.bind(11, 5);                          // same param, new CC
            expectEquals(ml.ccForParam(5), 11, "param now on the new CC");
            expectEquals(ml.paramForCc(10), MidiLearn::kNone, "old CC released");
            expectEquals(ml.paramForCc(11), 5);
        }

        beginTest("one param per CC -- re-using a CC re-points it");
        {
            MidiLearn ml;
            ml.bind(30, 1);
            ml.bind(30, 2);                          // same CC, new param
            expectEquals(ml.paramForCc(30), 2);
            expectEquals(ml.ccForParam(1), MidiLearn::kNone);
        }

        beginTest("forgetParam clears the mapping");
        {
            MidiLearn ml;
            ml.bind(40, 9);
            ml.forgetParam(9);
            expectEquals(ml.ccForParam(9), MidiLearn::kNone);
            expectEquals(ml.paramForCc(40), MidiLearn::kNone);
        }

        beginTest("dirty flag latches on bind and clears on consume");
        {
            MidiLearn ml;
            ml.consumeDirty();                       // clear initial
            expect(! ml.consumeDirty(), "starts clean");
            ml.arm(2);
            ml.handleCc(50);
            expect(ml.consumeDirty(), "bind set dirty");
            expect(! ml.consumeDirty(), "consume cleared it");
        }

        beginTest("out-of-range CC is ignored");
        {
            MidiLearn ml;
            ml.bind(-1, 0);
            ml.bind(128, 0);
            expectEquals(ml.ccForParam(0), MidiLearn::kNone, "no binding from bad CC");
            expectEquals(ml.paramForCc(200), MidiLearn::kNone);
            expectEquals(ml.paramForCc(-5), MidiLearn::kNone);
        }

        beginTest("clear wipes everything");
        {
            MidiLearn ml;
            ml.bind(12, 1);
            ml.bind(13, 2);
            ml.arm(3);
            ml.clear();
            expectEquals(ml.paramForCc(12), MidiLearn::kNone);
            expectEquals(ml.paramForCc(13), MidiLearn::kNone);
            expect(! ml.isArmed());
        }
    }
};

static MidiLearnTests midiLearnTests;

} // namespace
