#!/usr/bin/env python3
"""Bombo in-game chiptune loop generator — AUDITION tool.

Pure stdlib + numpy (no scipy, no installs). NES-style voices + a few
"retro-futuristic" extras that read as SPACE without reverb:
  - 2 pulse channels (duty + PWM + detune)    -> lead + chord arp
  - 1 triangle channel                         -> bass
  - 1 noise channel                            -> kick / snare / hats
  - optional pad (held detuned PWM chord), vibrato, glide/portamento,
    and a seamless circular multi-tap ECHO (discrete delay, not reverb).

Writes mono 22.05 kHz WAVs (loop tiled x3 so you hear the seam) to
  ~/repos/bombo/tools/chiptune_preview/

Candidates 01-04 = the original "driving" batch (unchanged).
Candidates 05-08 = space-y / retro-futuristic, with varied FORM.

Iterate: tweak CANDIDATES (bpm / prog / bass+drum style / fx / seed),
re-run, pick a winner -> its step/pattern data gets ported to the
in-plugin C++ renderer (GameMusic). This script is the source of truth.

Usage:  python3 tools/chiptune_gen.py
"""

import os
import sys
import wave
import numpy as np

SR = 22050
OUT = os.path.expanduser("~/repos/bombo/tools/chiptune_preview")
STEPS_PER_BAR = 16  # 16th-note grid

# --- modes (interval sets) ------------------------------------------------
IONIAN = [0, 2, 4, 5, 7, 9, 11]
DORIAN = [0, 2, 3, 5, 7, 9, 10]
LYDIAN = [0, 2, 4, 6, 7, 9, 11]
NAT_MIN = [0, 2, 3, 5, 7, 8, 10]
HAR_MIN = [0, 2, 3, 5, 7, 8, 11]

# --- chord qualities (semitone offsets) -----------------------------------
QUAL = {
    "maj": [0, 4, 7], "min": [0, 3, 7],
    "maj7": [0, 4, 7, 11], "min7": [0, 3, 7, 10], "dom7": [0, 4, 7, 10],
    "add9": [0, 4, 7, 14], "madd9": [0, 3, 7, 14],
    "sus2": [0, 2, 7], "sus4": [0, 5, 7],
}

# --- lead rhythmic templates: (step, length_steps, snap_to_chord_tone) ----
TEMPLATES = [  # original driving batch
    [(0, 2, 1), (2, 2, 0), (4, 2, 1), (6, 2, 0), (8, 3, 1), (11, 2, 0), (13, 3, 0)],
    [(0, 3, 1), (3, 1, 0), (4, 2, 1), (8, 2, 1), (10, 2, 0), (12, 4, 1)],
    [(0, 2, 1), (2, 2, 0), (4, 2, 0), (6, 2, 1), (8, 2, 0), (10, 2, 1), (12, 2, 0), (14, 2, 0)],
]
AIRY = [  # long notes + gaps
    [(0, 4, 1), (6, 2, 0), (8, 4, 1), (14, 2, 0)],
    [(2, 2, 0), (4, 4, 1), (10, 2, 0), (12, 3, 1)],
    [(0, 2, 1), (8, 6, 1)],
]
CALLRESP = [  # short call, leaves space, then answer
    [(0, 2, 1), (2, 2, 0), (4, 2, 1), (6, 1, 0)],
    [(8, 3, 1), (11, 1, 0), (12, 4, 1)],
    [(0, 3, 1), (8, 3, 1), (12, 2, 0)],
]
SPARSE = [  # lots of negative space
    [(0, 3, 1), (8, 3, 1)],
    [(2, 2, 1), (6, 2, 0), (10, 4, 1)],
    [(0, 4, 1), (10, 2, 0), (12, 2, 1)],
]
LEGATO = [  # long tied notes for glide
    [(0, 8, 1), (8, 8, 1)],
    [(0, 6, 1), (8, 4, 1), (12, 4, 0)],
    [(0, 4, 1), (4, 4, 0), (8, 8, 1)],
]


def mfreq(m):
    return 440.0 * 2.0 ** ((np.asarray(m, dtype=np.float64) - 69.0) / 12.0)


def scale_pool(tonic_pc, intervals, lo, hi):
    pcs = {(tonic_pc + i) % 12 for i in intervals}
    return [m for m in range(lo, hi + 1) if m % 12 in pcs]


def env_ad(n, atk, dec, sus, rel):
    """Attack-decay-sustain-release that fully closes within n samples."""
    atk, dec, rel = int(atk), int(dec), int(rel)
    tot = atk + dec + rel
    if tot > n and tot > 0:
        sc = n / tot
        atk, dec, rel = int(atk * sc), int(dec * sc), int(rel * sc)
    sus_n = max(0, n - atk - dec - rel)
    segs = []
    if atk > 0:
        segs.append(np.linspace(0.0, 1.0, atk, endpoint=False))
    if dec > 0:
        segs.append(np.linspace(1.0, sus, dec, endpoint=False))
    if sus_n > 0:
        segs.append(np.full(sus_n, sus))
    if rel > 0:
        segs.append(np.linspace(sus, 0.0, rel, endpoint=True))
    out = np.concatenate(segs) if segs else np.zeros(n)
    if len(out) < n:
        out = np.concatenate([out, np.zeros(n - len(out))])
    return out[:n]


def osc(wave_type, freq_arr, duty=0.5):
    ph = 2.0 * np.pi * np.cumsum(freq_arr) / SR
    t = (ph / (2.0 * np.pi)) % 1.0
    if wave_type == "pulse":
        return np.where(t < duty, 1.0, -1.0)
    if wave_type == "tri":
        return 2.0 * np.abs(2.0 * (t - np.floor(t + 0.5))) - 1.0
    return 2.0 * t - 1.0


def pulse_voice(freq_arr, duty, detune_cents=0.0):
    """A pulse; optionally a second slightly-detuned pulse for width."""
    s = osc("pulse", freq_arr, duty)
    if detune_cents:
        s = 0.5 * (s + osc("pulse", freq_arr * (2.0 ** (detune_cents / 1200.0)), duty))
    return s


def add(buf, start, sig):
    n = min(len(sig), len(buf) - start)
    if n > 0:
        buf[start:start + n] += sig[:n]


def echo_circular(x, delay, fb, taps):
    """Seamless looping multi-tap echo (np.roll wraps the tail)."""
    out = x.copy()
    d = x.copy()
    for _ in range(taps):
        d = np.roll(d, delay) * fb
        out = out + d
    return out


def kick(n):
    k = np.linspace(0, 1, n)
    f = 165.0 * (45.0 / 165.0) ** k
    s = np.sin(2.0 * np.pi * np.cumsum(f) / SR)
    return s * np.exp(-k * 7.0)


def snare(n, rng):
    k = np.linspace(0, 1, n)
    nz = rng.uniform(-1, 1, n) * np.exp(-k * 9.0)
    tone = 0.3 * np.sin(2.0 * np.pi * 185.0 * np.arange(n) / SR) * np.exp(-k * 13.0)
    return nz + tone


def hat(n, rng):
    nz = rng.uniform(-1, 1, n)
    nz = np.concatenate([[0.0], np.diff(nz)])
    k = np.linspace(0, 1, n)
    return nz * np.exp(-k * 30.0)


def melody_pcs(chord_root, quality, key_pcs):
    """Consonant melodic pitch-classes over a chord, in the key.

    Returns (chord_tone_pcs, allowed_pcs). allowed = chord tones + key tones
    that are NOT an avoid-note (a half-step above a chord tone) and NOT a
    cross-relation (a half-step from a CHROMATIC chord tone, e.g. G vs the G#
    of a V chord). Accidentals enter only via the chord tones themselves.
    """
    ct = {(chord_root + o) % 12 for o in QUAL[quality]}
    chromatic = {p for p in ct if p not in key_pcs}
    allowed = set(ct)
    for p in key_pcs:
        if p in ct:
            allowed.add(p)
            continue
        if (p - 1) % 12 in ct:                 # avoid-note: half-step above a chord tone
            continue
        if any((p - cc) % 12 in (1, 11) for cc in chromatic):  # cross-relation
            continue
        allowed.add(p)
    return ct, allowed


def gen_lead(buf, rng, c, spS):
    tonic, mode, lead_lo = c["tonic"], c["mode"], c["lead_lo"]
    duty_base, gain = c["duty"], c.get("lead_gain", 0.33)
    templates = c.get("templates", TEMPLATES)
    pwm_d, pwm_r = c.get("pwm", 0.0), c.get("pwm_rate", 0.0)
    detune = c.get("detune", 0.0)
    vib_d, vib_r = c.get("vib", 0.0), c.get("vib_rate", 5.0)
    glide = c.get("glide", False)
    glide_s = int(c.get("glide_ms", 45) / 1000.0 * SR)
    glide_min = c.get("glide_min", 4)  # only glide on leaps >= this many semitones
    a_, df_, s_, rf_ = c.get("lead_env", (60, 0.25, 0.55, 0.30))
    span = 14
    lo, hi = lead_lo, lead_lo + span
    key_pcs = {(tonic + i) % 12 for i in mode}

    def pick(cands, ref):
        if not cands:
            return None
        w = 1.0 / (1.0 + np.abs(np.array(cands) - ref))
        if rng.random_sample() < 0.18:
            w = np.ones_like(w)
        return int(rng.choice(cands, p=w / w.sum()))

    prev = None
    prevf = None
    for b in range(c["bars"]):
        root_pc, quality, _ = c["prog"][b]
        ct_pcs, ok_pcs = melody_pcs(root_pc, quality, key_pcs)
        ct_midi = [m for m in range(lo, hi + 1) if m % 12 in ct_pcs]
        ok_midi = [m for m in range(lo, hi + 1) if m % 12 in ok_pcs]
        tmpl = templates[rng.randint(len(templates))]
        bar0 = b * STEPS_PER_BAR * spS
        for (st, ln, strong) in tmpl:
            if prev is None:
                note = pick(ct_midi, (lo + hi) // 2)
            elif strong:
                note = pick(ct_midi, prev)               # strong beat -> chord tone
            elif rng.random_sample() < 0.55:
                note = pick(ct_midi, prev)               # weak beat, mostly chord tone
            else:
                steps = [m for m in ok_midi if 0 < abs(m - prev) <= 2]  # else stepwise passing/neighbor
                note = pick(steps, prev) if steps else pick(ct_midi, prev)
            if note is None:
                note = prev if prev is not None else (lo + hi) // 2
            # leaps resolve only to chord tones
            if prev is not None and abs(note - prev) > 7 and note % 12 not in ct_pcs:
                near = [m for m in ct_midi if abs(m - prev) <= 7]
                note = pick(near or ct_midi, prev)
            leap = abs(note - prev) if prev is not None else 0
            prev = note
            n = ln * spS
            ft = float(mfreq(note))
            if glide and prevf is not None and glide_s > 0 and leap >= glide_min:
                g = min(glide_s, n)
                base = np.concatenate([np.linspace(prevf, ft, g), np.full(n - g, ft)])
            else:
                base = np.full(n, ft)
            prevf = ft
            if vib_d > 0:
                tt = np.arange(n) / SR
                base = base * (1.0 + vib_d * np.sin(2.0 * np.pi * vib_r * tt))
            if pwm_d > 0:
                tt = np.arange(n) / SR
                duty = np.clip(duty_base + pwm_d * np.sin(2.0 * np.pi * pwm_r * tt), 0.05, 0.95)
            else:
                duty = duty_base
            sig = pulse_voice(base, duty, detune) * env_ad(n, a_, df_ * n, s_, rf_ * n) * gain
            add(buf, bar0 + st * spS, sig)


def gen_arp(buf, c, spS):
    if not c.get("arp_on", True):
        return
    gain, duty = c.get("arp_gain", 0.18), c.get("arp_duty", 0.125)
    rate, direction, octs = c.get("arp_rate", 1.0), c.get("arp_dir", "up"), c.get("arp_oct", 1)
    detune = c.get("arp_detune", 0.0)
    pwm_d, pwm_r = c.get("arp_pwm", 0.0), c.get("arp_pwm_rate", 0.0)
    for b in range(c["bars"]):
        root_pc, quality, _ = c["prog"][b]
        base = 60 + (root_pc % 12)
        tones = [base + o for o in QUAL[quality]]
        if octs >= 2:
            tones = tones + [t + 12 for t in tones]
        if direction == "down":
            tones = tones[::-1]
        elif direction == "updown" and len(tones) > 2:
            tones = tones + tones[-2:0:-1]
        n = STEPS_PER_BAR * spS
        seg = max(1, int(round(spS * rate)))
        idx = (np.arange(n) // seg) % len(tones)
        farr = mfreq(np.array(tones))[idx]
        if pwm_d > 0:
            tt = np.arange(n) / SR
            dty = np.clip(duty + pwm_d * np.sin(2.0 * np.pi * pwm_r * tt), 0.05, 0.95)
        else:
            dty = duty
        sig = pulse_voice(farr, dty, detune) * env_ad(n, 30, 0.2 * n, 0.8, 0.1 * n) * gain
        add(buf, b * STEPS_PER_BAR * spS, sig)


def gen_pad(buf, c, spS):
    gain, detune = c.get("pad_gain", 0.10), c.get("pad_detune", 8.0)
    pwm_d, pwm_r = c.get("pad_pwm", 0.15), c.get("pad_pwm_rate", 0.4)
    lo = c.get("pad_lo", 55)
    for b in range(c["bars"]):
        root_pc, _q, _ = c["prog"][b]
        notes = [lo + (root_pc % 12), lo + (root_pc % 12) + 7]
        n = STEPS_PER_BAR * spS
        tt = np.arange(n) / SR
        duty = np.clip(0.5 + pwm_d * np.sin(2.0 * np.pi * pwm_r * tt), 0.05, 0.95)
        for m in notes:
            sig = pulse_voice(np.full(n, float(mfreq(m))), duty, detune) * env_ad(n, 400, 0.1 * n, 0.8, 0.2 * n) * gain
            add(buf, b * STEPS_PER_BAR * spS, sig)


def gen_bass(buf, c, spS):
    style, gain = c["bass"], c.get("bass_gain", 0.55)
    for b in range(c["bars"]):
        _pc, _q, root = c["prog"][b]
        bar0 = b * STEPS_PER_BAR * spS
        if style == "octave":
            steps = [(s, root + (12 if (s // 2) % 2 else 0)) for s in range(0, 16, 2)]
            glen = int(1.7 * spS)
        elif style == "sixteen":
            steps = [(s, root) for s in range(16)]
            glen = int(0.9 * spS)
        elif style == "arpbass":
            seq = [root, root, root + 12, root + 7]
            steps = [(s, seq[s % 4]) for s in range(16)]
            glen = int(0.9 * spS)
        elif style == "offbeat":
            steps = [(s, root + (12 if (s // 4) % 2 else 0)) for s in (2, 6, 10, 14)]
            glen = int(1.4 * spS)
        else:  # pedal
            steps = [(0, root), (6, root + 12), (8, root), (14, root + 12)]
            glen = int(5.5 * spS)
        for (s, m) in steps:
            sig = osc("tri", np.full(glen, float(mfreq(m)))) * env_ad(glen, 40, 0.2 * glen, 0.7, 0.25 * glen) * gain
            add(buf, bar0 + s * spS, sig)


def gen_drums(buf, rng, c, spS):
    dens = c.get("drums", "med")
    drop = set(c.get("drop_bars", []))
    pat = c.get("drum_pat")
    if pat:
        kk, sn, hh = pat["kick"], pat["snare"], pat["hat"]
    elif dens == "busy":
        kk, sn, hh = [0, 3, 8, 11], [4, 12], list(range(16))
    elif dens == "sparse":
        kk, sn, hh = [0, 8], [12], [4, 12]
    elif dens == "four":
        kk, sn, hh = [0, 4, 8, 12], [4, 12], [2, 6, 10, 14]
    else:
        kk, sn, hh = [0, 8, 14], [4, 12], list(range(2, 16, 2))
    kn, snn, hn = int(0.13 * SR), int(0.16 * SR), int(0.04 * SR)
    for b in range(c["bars"]):
        if b in drop:
            continue
        bar0 = b * STEPS_PER_BAR * spS
        for s in kk:
            add(buf, bar0 + s * spS, kick(kn) * 0.62)
        for s in sn:
            add(buf, bar0 + s * spS, snare(snn, rng) * 0.42)
        for s in hh:
            add(buf, bar0 + s * spS, hat(hn, rng) * 0.13)


def crush(x, bits):
    if not bits:
        return x
    lv = 2 ** (bits - 1) - 1
    return np.round(x * lv) / lv


def build_mix(c):
    """Render ONE loop of a candidate to a float mono array in [-1, 1]."""
    rng = np.random.RandomState(c["seed"])
    spS = int(round(SR * (60.0 / c["bpm"]) / 4.0))
    total = spS * STEPS_PER_BAR * c["bars"]
    melodic = np.zeros(total)
    rhythm = np.zeros(total)

    gen_lead(melodic, rng, c, spS)
    gen_arp(melodic, c, spS)
    if c.get("pad"):
        gen_pad(melodic, c, spS)
    gen_bass(rhythm, c, spS)
    gen_drums(rhythm, rng, c, spS)

    if c.get("echo_on"):
        melodic = echo_circular(melodic, int(c.get("echo_div", 3)) * spS,
                                c.get("echo_fb", 0.33), int(c.get("echo_taps", 4)))

    mix = melodic + rhythm
    peak = np.max(np.abs(mix)) + 1e-9
    mix = (mix / peak) * 0.9
    return crush(mix, c.get("bits", 8))


def write_wav(path, x):
    xi = (np.clip(x, -1, 1) * 32767.0).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(xi.tobytes())


def render(c):
    """Audition file: one loop tiled x3 with tiny edge fades so it doesn't click."""
    mix = build_mix(c)
    tiled = np.tile(mix, 3)
    fade = int(0.004 * SR)
    tiled[:fade] *= np.linspace(0, 1, fade)
    tiled[-fade:] *= np.linspace(1, 0, fade)
    path = os.path.join(OUT, c["file"])
    write_wav(path, tiled)
    return path, len(mix) / SR


def bake(name, out_path):
    """Write ONE seamless loop (no tiling/fade) for in-plugin baking. The
    GameAudioBus loops the buffer cyclically, so the asset is a single cycle."""
    cand = next((c for c in CANDIDATES
                 if c["file"].startswith(name) or name in c["file"]), None)
    if cand is None:
        raise SystemExit(f"no candidate matching '{name}'")
    mix = build_mix(cand)
    write_wav(out_path, mix)
    print(f"baked {cand['file']} -> {out_path}  ({len(mix) / SR:.2f}s loop, {SR} Hz mono)")


# prog entries: (root_pc, quality, bass_root_midi)
CANDIDATES = [
    # ---- original driving batch (unchanged) ----
    {"file": "01_propulsion.wav", "seed": 1, "bpm": 150, "bars": 4, "tonic": 9, "mode": NAT_MIN,
     "prog": [(9, "min", 45), (5, "maj", 41), (0, "maj", 48), (7, "maj", 43)],
     "bass": "octave", "drums": "med", "duty": 0.25, "lead_lo": 69, "bits": 8},
    {"file": "02_chase.wav", "seed": 7, "bpm": 162, "bars": 4, "tonic": 9, "mode": NAT_MIN,
     "prog": [(9, "min", 45), (4, "min", 40), (5, "maj", 41), (7, "maj", 43)],
     "bass": "sixteen", "drums": "busy", "duty": 0.125, "lead_lo": 69, "bits": 8},
    {"file": "03_menace.wav", "seed": 3, "bpm": 130, "bars": 4, "tonic": 9, "mode": HAR_MIN,
     "prog": [(9, "min", 45), (9, "min", 45), (2, "min", 38), (4, "maj", 40)],
     "bass": "pedal", "drums": "sparse", "duty": 0.5, "lead_lo": 64, "bits": 6},
    {"file": "04_drive2.wav", "seed": 21, "bpm": 152, "bars": 4, "tonic": 9, "mode": NAT_MIN,
     "prog": [(9, "min", 45), (5, "maj", 41), (0, "maj", 48), (7, "maj", 43)],
     "bass": "octave", "drums": "med", "duty": 0.25, "lead_lo": 69, "bits": 8},

    # ---- space-y / retro-futuristic batch ----
    {  # bright, wide, twinkly starfield — I-V-vi-IV in C, pads + echo
        "file": "05_starfield.wav", "seed": 11, "bpm": 118, "bars": 8, "tonic": 0, "mode": IONIAN,
        "prog": [(0, "add9", 36), (7, "maj", 31), (9, "madd9", 33), (5, "maj7", 29)] * 2,
        "bass": "pedal", "drums": "four", "duty": 0.25, "lead_lo": 72, "bits": 8,
        "templates": AIRY, "pwm": 0.18, "pwm_rate": 0.7, "detune": 10, "vib": 0.006,
        "arp_rate": 0.5, "arp_dir": "updown", "arp_oct": 2, "arp_detune": 6, "arp_duty": 0.25,
        "pad": True, "echo_on": True, "echo_div": 3, "echo_fb": 0.34, "echo_taps": 4,
    },
    {  # urgent synthwave hyperdrive — arp bass, glide lead, 8-bar with a break
        "file": "06_hyperdrive.wav", "seed": 5, "bpm": 140, "bars": 8, "tonic": 9, "mode": DORIAN,
        "prog": [(9, "madd9", 45), (9, "madd9", 45), (5, "maj", 41), (7, "maj", 43),
                 (9, "madd9", 45), (0, "maj", 48), (5, "maj", 41), (7, "dom7", 43)],
        "bass": "arpbass", "drums": "busy", "drop_bars": [4], "duty": 0.125, "lead_lo": 69, "bits": 8,
        "templates": CALLRESP, "pwm": 0.12, "pwm_rate": 6.0, "detune": 7, "vib": 0.005,
        "glide": True, "glide_ms": 35, "arp_rate": 1.0, "arp_duty": 0.125,
        "echo_on": True, "echo_div": 3, "echo_fb": 0.30, "echo_taps": 3,
    },
    {  # vector-arcade — maj7/lydian stabs, call-and-response with space, off-beat bass
        "file": "07_vector.wav", "seed": 14, "bpm": 128, "bars": 8, "tonic": 2, "mode": LYDIAN,
        "prog": [(2, "maj7", 38), (0, "maj7", 36), (7, "maj7", 43), (4, "min7", 40)] * 2,
        "bass": "offbeat", "drums": "sparse", "drop_bars": [3], "duty": 0.5, "lead_lo": 71, "bits": 8,
        "templates": SPARSE, "pwm": 0.2, "pwm_rate": 0.5, "detune": 12,
        "arp_rate": 2.0, "arp_dir": "updown", "arp_oct": 2, "arp_duty": 0.125,
        "pad": True, "echo_on": True, "echo_div": 6, "echo_fb": 0.35, "echo_taps": 4,
    },
    {  # floaty nebula — glide-heavy legato lead, held pad, harmonic-minor mystery
        "file": "08_nebula.wav", "seed": 9, "bpm": 110, "bars": 8, "tonic": 9, "mode": HAR_MIN,
        "prog": [(9, "min", 45), (9, "min", 45), (5, "maj7", 41), (4, "maj", 40),
                 (2, "min7", 38), (2, "min7", 38), (7, "maj", 43), (4, "dom7", 40)],
        "bass": "pedal", "drums": "sparse", "drop_bars": [2, 6], "duty": 0.5, "lead_lo": 66, "bits": 7,
        "templates": LEGATO, "pwm": 0.15, "pwm_rate": 0.6, "detune": 9, "vib": 0.008, "vib_rate": 4.5,
        "glide": True, "glide_ms": 80, "lead_env": (80, 0.2, 0.75, 0.35),
        "arp_rate": 2.0, "arp_duty": 0.125, "arp_gain": 0.12,
        "pad": True, "echo_on": True, "echo_div": 4, "echo_fb": 0.33, "echo_taps": 4,
    },
]


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "--bake":
        bake(sys.argv[2], sys.argv[3])   # python3 chiptune_gen.py --bake propulsion <out.wav>
        return
    os.makedirs(OUT, exist_ok=True)
    print(f"Bombo chiptune candidates -> {OUT}  (SR={SR} Hz, mono)\n")
    for c in CANDIDATES:
        path, loop_sec = render(c)
        kb = os.path.getsize(path) / 1024.0
        print(f"  {c['file']:<22}  {c['bpm']:>3} BPM  {c['bars']}-bar  loop={loop_sec:4.1f}s (x3)  {kb:6.0f} KB")
    print("\nPlay (PipeWire):  pw-play <file>     or:  mpv <file>")


if __name__ == "__main__":
    main()
