#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "DSP/Oscillators.h"
#include "DSP/VoiceClip.h"
#include "ParameterIds.h"

namespace bombo
{

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

    // Each formatter returns a "value unit" string. The knob LookAndFeel
    // splits on the first space and renders the number on the top half
    // of the cap and the unit smaller below, matching the Rust UI's
    // in-cap two-line readout.
    auto dbFormat = FAttr()
        .withStringFromValueFunction([](float v, int) {
            return v <= -59.9f ? juce::String("-inf")
                               : juce::String(v, 1) + " dB";
        })
        .withValueFromStringFunction([](const juce::String& s) {
            return s.contains("inf") ? -60.0f : s.getFloatValue();
        });

    // Hz — integer for >= 100, one decimal below. No unit text saves
    // space inside the small cap.
    auto hzFormat = FAttr()
        .withStringFromValueFunction([](float v, int) {
            if (v >= 1000.0f) return juce::String(v / 1000.0f, 2) + " k";
            return juce::String(static_cast<int>(v + 0.5f)) + " Hz";
        })
        .withValueFromStringFunction([](const juce::String& s) {
            auto trimmed = s.trim();
            if (trimmed.endsWithIgnoreCase("k"))
                return trimmed.dropLastCharacters(1).getFloatValue() * 1000.0f;
            return trimmed.getFloatValue();
        });

    // ms — int for >= 10, one decimal below.
    auto msFormat = FAttr()
        .withStringFromValueFunction([](float v, int) {
            if (v >= 10.0f) return juce::String(static_cast<int>(v + 0.5f)) + " ms";
            return juce::String(v, 1) + " ms";
        })
        .withValueFromStringFunction([](const juce::String& s) {
            return s.getFloatValue();
        });

    // 0..1 normalized — two decimals, no unit.
    auto normFormat = FAttr()
        .withStringFromValueFunction([](float v, int) {
            return juce::String(v, 2);
        })
        .withValueFromStringFunction([](const juce::String& s) {
            return s.getFloatValue();
        });

    auto curveFormat = FAttr()
        .withStringFromValueFunction([](float v, int) {
            return juce::String(v, 2);
        })
        .withValueFromStringFunction([](const juce::String& s) {
            return s.getFloatValue();
        });

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
        "Pitch Curve", Range(0.5f, 6.0f, 0.01f), 3.0f, curveFormat));
    // Sub HPF — one-pole on SUB layer only. 20 Hz default = effectively off.
    // Skew biased so the useful 30-100 Hz region gets most of the knob travel.
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::subHpf, 1},
        "Sub HPF", skewRange(20.0f, 200.0f, 0.4f), 20.0f, hzFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::midPitchStart, 1},
        "Mid Pitch Start", skewRange(100.0f, 800.0f, 0.4f), 300.0f, hzFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::midPitchEnd, 1},
        "Mid Pitch End", skewRange(40.0f, 300.0f, 0.4f), 80.0f, hzFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::midDecay, 1},
        "Mid Decay", skewRange(10.0f, 500.0f, 0.4f), 80.0f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::midLevel, 1},
        "Mid Level", Range(0.0f, 1.0f, 0.001f), 0.70f, normFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::ampAttack, 1},
        "Amp Attack", skewRange(0.1f, 20.0f, 0.4f), 0.5f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::ampDecay, 1},
        "Amp Decay", skewRange(50.0f, 2000.0f, 0.4f), 700.0f, msFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::clickAmount, 1},
        "Click", Range(0.0f, 1.0f, 0.001f), 0.30f, normFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::clickCenter, 1},
        "Click Freq", skewRange(200.0f, 8000.0f, 0.4f), 4500.0f, hzFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::noiseAmount, 1},
        "Noise", Range(0.0f, 1.0f, 0.001f), 0.30f, normFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::noiseColor, 1},
        "Noise Color", Range(0.0f, 1.0f, 0.001f), 0.20f, normFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::driveAmount, 1},
        "Drive", Range(0.0f, 1.0f, 0.001f), 0.30f, normFormat));
    p.push_back(std::make_unique<Choice>(juce::ParameterID{pid::driveMode, 1},
        "Drive Mode", juce::StringArray{"Off", "Tanh", "Diode", "Cubic"},
        VC_DIODE));
    // BIAS: DC offset into waveshaper pre-clip → asymmetric harmonics (tube character).
    // -1 = heavy negative bias, 0 = symmetric, +1 = heavy positive bias.
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::driveBias, 1},
        "Drive Bias", Range(-1.0f, 1.0f, 0.001f), 0.0f, normFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::voiceBalance, 1},
        "Voice Balance", Range(0.0f, 1.0f, 0.001f), 0.50f, normFormat));

    // ── Rumble FX chain ─────────────────────────────────────────────
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::fxDriveAmount, 1},
        "FX Drive", Range(0.0f, 1.0f, 0.001f), 0.0f, normFormat));
    p.push_back(std::make_unique<Choice>(juce::ParameterID{pid::fxDriveMode, 1},
        "FX Drive Mode", juce::StringArray{"Off", "Tanh", "Diode", "Cubic"},
        VC_TANH));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::fxDriveMix, 1},
        "FX Drive Mix", Range(0.0f, 1.0f, 0.001f), 1.0f, normFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::filterHp, 1},
        "HP Freq", skewRange(20.0f, 2000.0f, 0.4f), 30.0f, hzFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::filterHpQ, 1},
        "HP Q", Range(0.5f, 3.0f, 0.01f), 0.707f, curveFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::filterLp, 1},
        "LP Freq", skewRange(200.0f, 18000.0f, 0.4f), 4500.0f, hzFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::filterLpQ, 1},
        "LP Q", Range(0.5f, 3.0f, 0.01f), 0.707f, curveFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::filterColor, 1},
        "Filter Color", Range(0.0f, 1.0f, 0.001f), 0.0f, normFormat));
    // TEETH: LP cutoff tracks the pitch envelope. +1 = LP sweeps down with pitch
    // (filter closes as kick descends). -1 = LP sweeps up (bloom effect).
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::filterTeeth, 1},
        "Teeth", Range(-1.0f, 1.0f, 0.001f), 0.0f, normFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::delayTime, 1},
        "Delay Time", skewRange(1.0f, 1000.0f, 0.4f), 380.0f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::delayFeedback, 1},
        "Delay Fbk", Range(0.0f, 0.92f, 0.001f), 0.55f, normFormat));
    // Tempo-sync mode (replaced the old `delay_drift` Float 2026-05-17).
    // FREE = TIME knob value (ms) applies directly. Anything else snaps
    // the effective delay length to host BPM * note value.
    p.push_back(std::make_unique<Choice>(juce::ParameterID{pid::delayTimeMode, 1},
        "Delay Time Mode",
        juce::StringArray{ "Free", "1/2", "1/2.", "1/2T",
                           "1/4", "1/4.", "1/4T",
                           "1/8", "1/8.", "1/8T",
                           "1/16", "1/16.", "1/16T",
                           "1/32" },
        0));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::delayMorph, 1},
        "Delay Tone", Range(0.0f, 1.0f, 0.001f), 0.4f, normFormat));
    // SMEAR: tape-warble LFO on the delay read tap. 0 = static, 1 = heavy pitch wobble.
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::delaySmear, 1},
        "Smear", Range(0.0f, 1.0f, 0.001f), 0.0f, normFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::delayMix, 1},
        "Delay Mix", Range(0.0f, 1.0f, 0.001f), 0.25f, normFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::reverbSize, 1},
        "Reverb Size", Range(0.0f, 1.0f, 0.001f), 0.55f, normFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::reverbDecay, 1},
        "Reverb Decay", Range(0.0f, 1.0f, 0.001f), 0.70f, normFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::reverbDamp, 1},
        "Reverb Damp", Range(0.0f, 1.0f, 0.001f), 0.45f, normFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::reverbDiffusion, 1},
        "Reverb Diffusion", Range(0.0f, 1.0f, 0.001f), 0.6f, normFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::reverbPredelay, 1},
        "Reverb Predelay", skewRange(0.0f, 500.0f, 0.4f), 30.0f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::reverbMix, 1},
        "Reverb Mix", Range(0.0f, 1.0f, 0.001f), 0.35f, normFormat));

    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::duckAtk, 1},
        "Duck Atk", skewRange(0.1f, 50.0f, 0.4f), 2.0f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::duckHold, 1},
        "Duck Hold", skewRange(0.0f, 200.0f, 0.4f), 0.0f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::duckRel, 1},
        "Duck Rel", skewRange(10.0f, 500.0f, 0.4f), 220.0f, msFormat));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::duckDepth, 1},
        "Duck Depth", Range(0.0f, 1.0f, 0.001f), 0.6f, normFormat));
    // SHAPE: envelope curve. -1 = exponential (tight/punchy), 0 = linear, +1 = log (smooth).
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::duckShape, 1},
        "Duck Shape", Range(-1.0f, 1.0f, 0.001f), 0.0f, normFormat));
    // GROWL: LP filter on wet during duck — tail darkens at peak, blooms bright on release.
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::duckGrowl, 1},
        "Duck Growl", Range(0.0f, 1.0f, 0.001f), 0.0f, normFormat));

    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::limiterOn, 1}, "Limiter On", true));
    p.push_back(std::make_unique<Float>(juce::ParameterID{pid::limiterAmount, 1},
        "Limiter Amount", Range(0.0f, 1.0f, 0.001f), 0.5f, normFormat));

    // Auto tail-kill toggle (default ON — matches the established
    // "trim tails between trigs" behaviour). Off → long delay/reverb
    // tails ring naturally after the last trigger.
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::tailKillOn, 1}, "Tail Kill On", true));

    // Transport: loop toggle + BPM (integer, 60–300).
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::loopOn, 1}, "Loop On", false));
    {
        auto bpmFormat = FAttr()
            .withStringFromValueFunction([](float v, int) {
                return juce::String(static_cast<int>(v + 0.5f));
            })
            .withValueFromStringFunction([](const juce::String& s) {
                return s.getFloatValue();
            });
        p.push_back(std::make_unique<Float>(juce::ParameterID{pid::bpm, 1},
            "BPM", Range(60.0f, 300.0f, 1.0f), 120.0f, bpmFormat));
    }

    // Section mutes — all default to off so existing presets sound the same.
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::voiceAMute, 1}, "Voice A Mute", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::voiceBMute, 1}, "Voice B Mute", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::driveMute,  1}, "Drive Mute",  false));
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::delayMute,  1}, "Delay Mute",  false));
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::reverbMute, 1}, "Reverb Mute", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::filterMute, 1}, "Filter Mute", false));
    p.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{pid::duckMute,   1}, "Duck Mute",   false));

    return { p.begin(), p.end() };
}

} // namespace bombo
