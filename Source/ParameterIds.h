#pragma once

#include <array>

namespace bombo
{

// Param ID constants — sole source of truth for both Parameters.h
// (createParameterLayout) and the processor's cached pointer lookups.
// Matches Rust archive param IDs where possible so saved presets carry
// over once we ship the JSON-to-XML migrator.
//
// This header has NO juce dependency and pulls in nothing else — files
// that only need parameter IDs (e.g. FaceplatePanel knob wiring,
// PresetBank exclusion list) can include this instead of the heavy
// Parameters.h and avoid a recompile when the APVTS layout changes.
namespace pid
{
    inline constexpr const char* masterOut       = "master_out";
    inline constexpr const char* waveform        = "waveform";
    inline constexpr const char* pitchStart      = "pitch_start";
    inline constexpr const char* pitchEnd        = "pitch_end";
    inline constexpr const char* pitchDecay      = "pitch_decay";
    inline constexpr const char* pitchCurve      = "pitch_curve";
    // Sub-layer HPF (VOICE A 6th slot, added 2026-05-17 for tight psytrance
    // kick shaping). Pre-filter on the SUB layer only; carves muddy
    // ultra-lows so kicks punch at 50-60 Hz instead of pumping the system.
    // Default 20 Hz = bypass.
    inline constexpr const char* subHpf          = "sub_hpf";
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
    inline constexpr const char* driveBias       = "drive_bias";
    // VOICE A ↔ VOICE B balance. 0 = A only, 0.5 = both at unity (tent),
    // 1 = B only.
    inline constexpr const char* voiceBalance    = "voice_balance";

    // ── Rumble FX chain ─────────────────────────────────────────────
    inline constexpr const char* fxDriveAmount   = "fx_drive_amount";
    inline constexpr const char* fxDriveMode     = "fx_drive_mode";
    inline constexpr const char* fxDriveMix      = "fx_drive_mix";
    inline constexpr const char* filterHp        = "filter_hp";
    inline constexpr const char* filterHpQ       = "filter_hp_q";
    inline constexpr const char* filterLp        = "filter_lp";
    inline constexpr const char* filterLpQ       = "filter_lp_q";
    inline constexpr const char* filterColor     = "filter_color";
    inline constexpr const char* filterTeeth     = "filter_teeth";
    inline constexpr const char* delayTime       = "delay_time";
    inline constexpr const char* delayFeedback   = "delay_feedback";
    // Replaced the old `delay_drift` Float (LFO drift was musically
    // pointless) with a tempo-sync mode Choice 2026-05-17. When != Free
    // the DSP computes effective delay ms from host BPM + chosen note
    // value; the TIME knob value is ignored.
    inline constexpr const char* delayTimeMode   = "delay_time_mode";
    inline constexpr const char* delayMorph      = "delay_morph";
    inline constexpr const char* delaySmear      = "delay_smear";
    inline constexpr const char* delayMix        = "delay_mix";
    // reverbType: Choice over the IRBank algos (Room/Plate/Hall/Spring/
    // Chamber/Bunker). Added 2026-05-23 with the convolution rework — see
    // ConvolutionReverb.h. Default index 2 = Hall.
    inline constexpr const char* reverbType      = "reverb_type";
    inline constexpr const char* reverbSize      = "reverb_size";
    inline constexpr const char* reverbDecay     = "reverb_decay";
    inline constexpr const char* reverbDamp      = "reverb_damp";
    // reverbDiffusion: hidden after the convolution rework — IR has
    // built-in diffusion. Kept in APVTS so old presets round-trip; not
    // wired to any UI control.
    inline constexpr const char* reverbDiffusion = "reverb_diffusion";
    inline constexpr const char* reverbPredelay  = "reverb_predelay";
    inline constexpr const char* reverbMix       = "reverb_mix";
    inline constexpr const char* duckAtk         = "duck_atk";
    inline constexpr const char* duckHold        = "duck_hold";
    inline constexpr const char* duckRel         = "duck_rel";
    inline constexpr const char* duckDepth       = "duck_depth";
    inline constexpr const char* duckShape       = "duck_shape";
    inline constexpr const char* duckGrowl     = "duck_growl";
    inline constexpr const char* limiterOn       = "limiter_on";
    inline constexpr const char* limiterAmount   = "limiter_amount";
    // TRANSPORT (standalone-only knobs the host doesn't drive). loopOn =
    // auto-fire triggers at the BPM rate; in a DAW the host BPM overrides
    // the param value and triggers snap to PPQ.
    inline constexpr const char* loopOn          = "loop_on";
    inline constexpr const char* bpm             = "bpm";
    // Auto tail-kill between triggers (default ON) — when ON, the FX bus
    // (delay + reverb) dies one beat after the last trigger so each new
    // trig starts on a clean slate AND loop-off immediately cuts the
    // tail. When OFF the deferred kill is skipped so long delay/reverb
    // tails ring naturally after one-shot edge cases.
    inline constexpr const char* tailKillOn      = "tail_kill_on";
    // SECTION MUTES — click the section title strip in the UI to toggle.
    // Voice A mute silences the SUB layer; Voice B mute silences MID +
    // click + noise + sample. DRIVE mute bypasses both the per-voice
    // V.AMT clipper and the rumble-bus B.AMT stage.
    inline constexpr const char* voiceAMute      = "voice_a_mute";
    inline constexpr const char* voiceBMute      = "voice_b_mute";
    // Voice B synth-layer master gate. ON (default) = mid sine + click +
    // noise contribute to Voice B's body bus alongside any loaded sample.
    // OFF = sample-only mode; the synth layer is bypassed so the SAMPLE
    // slot is the sole sound source for Voice B. Added 2026-05-24 after
    // user reported a residual "snare attack" transient from the mid
    // sine that no individual knob (CLICK, BODY, COLOR, DEC) could fully
    // silence. Lives in voice-mute neighbourhood semantically.
    inline constexpr const char* voiceBSynthOn   = "voice_b_synth_on";
    // DEC routing — picks which voice(s) the VOICE B section's DEC knob
    // writes to. "A" = Voice A's ampDecay only (the sub-layer envelope).
    // "B" = Voice B's midDecay (mid sine + sample env). "AB" = both, with
    // the same plain-ms value. Default "B" preserves the current per-
    // voice DEC scoping. Added 2026-05-24 because Voice A had no UI knob
    // for ampDecay other than the global DECAY macro — users wanted a
    // direct way to dial Voice A's amp tail without losing Voice B
    // control. Choice param so hosts can show "A"/"B"/"AB" in automation.
    inline constexpr const char* decRouting      = "dec_routing";
    inline constexpr const char* driveMute       = "drive_mute";
    inline constexpr const char* delayMute       = "delay_mute";
    inline constexpr const char* reverbMute      = "reverb_mute";
    inline constexpr const char* filterMute      = "filter_mute";
    inline constexpr const char* duckMute        = "duck_mute";
}

// Params NOT to bake into presets. These are global mixer / transport /
// per-section mute toggles — a preset should restore the synth voicing,
// not stomp on master out, host BPM, or whatever sections the user has
// muted while auditioning.
//
// Single source of truth: referenced by PresetBank::applyByIndex,
// PresetBank::saveCurrentAs, and PresetBank::applyDefaults. Adding a new
// excluded param means adding it here once.
inline constexpr std::array<const char*, 11> kExcludedFromPresets = {
    pid::masterOut,
    pid::bpm,
    pid::loopOn,
    pid::limiterOn,
    pid::voiceAMute,
    pid::voiceBMute,
    pid::driveMute,
    pid::delayMute,
    pid::reverbMute,
    pid::filterMute,
    pid::duckMute,
};

} // namespace bombo
