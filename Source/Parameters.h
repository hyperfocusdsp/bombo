#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/Oscillators.h"
#include "DSP/VoiceClip.h"

namespace bombo
{

// Param ID constants — sole source of truth for both Parameters.h and the
// processor's cached pointer lookups. Matches Rust archive param IDs where
// possible so saved presets carry over once we ship the JSON-to-XML migrator.
namespace pid
{
    inline constexpr const char* masterOut       = "master_out";
    inline constexpr const char* waveform        = "waveform";
    inline constexpr const char* pitchStart      = "pitch_start";
    inline constexpr const char* pitchEnd        = "pitch_end";
    inline constexpr const char* pitchDecay      = "pitch_decay";
    inline constexpr const char* pitchCurve      = "pitch_curve";
    inline constexpr const char* midPitchStart   = "mid_pitch_start";
    inline constexpr const char* midPitchEnd     = "mid_pitch_end";
    inline constexpr const char* midDecay        = "mid_decay";
    inline constexpr const char* midLevel        = "mid_level";
    inline constexpr const char* ampAttack       = "amp_attack";
    inline constexpr const char* ampDecay        = "amp_decay";
    inline constexpr const char* clickAmount     = "click_amount";
    inline constexpr const char* clickCenter     = "click_center";
    inline constexpr const char* noiseAmount     = "noise_amount";
    inline constexpr const char* noiseColor      = "noise_color";
    inline constexpr const char* driveAmount     = "drive_amount";
    inline constexpr const char* driveMode       = "drive_mode";
    inline constexpr const char* driftAmount     = "drift_amount";
}

inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    using Float  = juce::AudioParameterFloat;
    using Choice = juce::AudioParameterChoice;
    using Range  = juce::NormalisableRange<float>;
    using FAttr  = juce::AudioParameterFloatAttributes;

    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;

    auto skewRange = [](float lo, float hi, float skew) {
        return Range(lo, hi, 0.0f, skew);
    };

    auto dbFormat = FAttr()
        .withStringFromValueFunction([](float v, int) {
            return v <= -59.9f ? juce::String("-inf") : juce::String(v, 1) + " dB";
        })
        .withValueFromStringFunction([](const juce::String& s) {
            return s.contains("inf") ? -60.0f : s.getFloatValue();
        });

    auto hzFormat = FAttr().withLabel("Hz");
    auto msFormat = FAttr().withLabel("ms");
    auto pctFormat = FAttr();

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::masterOut, 1},
        "Master Out", Range(-60.0f, 6.0f, 0.01f, 0.3f), 0.0f, dbFormat));

    p.push_back(std::make_unique<Choice>(juce::ParameterID{pid::waveform, 1},
        "Waveform", juce::StringArray{"Sine", "Triangle", "Saw", "Pulse"},
        WAVE_SINE));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::pitchStart, 1},
        "Pitch Start", skewRange(40.0f, 400.0f, 0.4f), 150.0f, hzFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::pitchEnd, 1},
        "Pitch End", skewRange(20.0f, 200.0f, 0.4f), 45.0f, hzFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::pitchDecay, 1},
        "Pitch Decay", skewRange(5.0f, 800.0f, 0.4f), 80.0f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::pitchCurve, 1},
        "Pitch Curve", Range(0.5f, 6.0f, 0.01f), 3.0f, FAttr()));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::midPitchStart, 1},
        "Mid Pitch Start", skewRange(100.0f, 800.0f, 0.4f), 300.0f, hzFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::midPitchEnd, 1},
        "Mid Pitch End", skewRange(40.0f, 300.0f, 0.4f), 80.0f, hzFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::midDecay, 1},
        "Mid Decay", skewRange(10.0f, 500.0f, 0.4f), 80.0f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::midLevel, 1},
        "Mid Level", Range(0.0f, 1.0f, 0.001f), 0.70f, pctFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::ampAttack, 1},
        "Amp Attack", skewRange(0.1f, 20.0f, 0.4f), 0.5f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::ampDecay, 1},
        "Amp Decay", skewRange(50.0f, 2000.0f, 0.4f), 700.0f, msFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::clickAmount, 1},
        "Click", Range(0.0f, 1.0f, 0.001f), 0.30f, pctFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::clickCenter, 1},
        "Click Freq", skewRange(200.0f, 8000.0f, 0.4f), 4500.0f, hzFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::noiseAmount, 1},
        "Noise", Range(0.0f, 1.0f, 0.001f), 0.30f, pctFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::noiseColor, 1},
        "Noise Color", Range(0.0f, 1.0f, 0.001f), 0.20f, pctFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::driveAmount, 1},
        "Drive", Range(0.0f, 1.0f, 0.001f), 0.30f, pctFormat));
    p.push_back(std::make_unique<Choice>(juce::ParameterID{pid::driveMode, 1},
        "Drive Mode", juce::StringArray{"Off", "Tanh", "Diode", "Cubic"},
        VC_DIODE));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::driftAmount, 1},
        "Drift", Range(0.0f, 1.0f, 0.001f), 0.0f, pctFormat));

    return { p.begin(), p.end() };
}

} // namespace bombo
