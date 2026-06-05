#!/usr/bin/env python3
"""Launch Bombo per-theme, burst-capture the scope, pick the frame with the
tallest fully-drawn kick. Writes OUTDIR/<theme>.png (clean source for compositing)."""
import json, os, subprocess, sys, time
from PIL import Image

BOMBO = os.path.expanduser("~/repos/bombo/build/Bombo_artefacts/Release/Standalone/Bombo")
OUTDIR = os.path.expanduser("~/Pictures/bombo_screenshots")
FRAMES = "/tmp/bombo_review/frames"
os.makedirs(OUTDIR, exist_ok=True)
os.makedirs(FRAMES, exist_ok=True)

_HIDDEN = []   # windows moved off-screen during capture (restored on exit)
_WSID = None
THEMES = sys.argv[1:] or ["vault", "bandw", "nightrun", "matrix", "cyber", "plasma", "fallout"]
N_FRAMES = 36
FRAME_GAP = 0.06   # ~2.2s burst → ~6 kick cycles at 161 BPM
WARMUP = 4.0       # audio device init can lag
MAX_TRIES = 4      # relaunch if the scope never fills (ALSA open race)


def active_ws():
    try:
        return json.loads(subprocess.check_output(["hyprctl", "activeworkspace", "-j"]))["id"]
    except Exception:
        return None


def list_ws_windows(wsid):
    """Non-Bombo windows on the given workspace: (address, wsid)."""
    try:
        data = json.loads(subprocess.check_output(["hyprctl", "clients", "-j"]))
    except Exception:
        return []
    out = []
    for c in data:
        if c.get("class", "") == "Bombo":
            continue
        ws = c.get("workspace", {})
        if ws.get("id") == wsid and c.get("address"):
            out.append((c["address"], wsid))
    return out


def move_windows(items, target):
    for addr, _ in items:
        subprocess.run(["hyprctl", "dispatch", "movetoworkspacesilent",
                        f"{target},address:{addr}"], stdout=subprocess.DEVNULL)


def win_geom():
    try:
        data = json.loads(subprocess.check_output(["hyprctl", "clients", "-j"]))
    except Exception:
        return None
    for c in data:
        if c.get("class", "") == "Bombo":
            x, y = c["at"]; w, h = c["size"]
            return (x, y, w, h)
    return None


def score_kick(path):
    """Score a frame by the scope waveform: tall (vertical extent) AND
    fully-drawn (trace reaches toward the right edge). Returns (score, vext)."""
    try:
        img = Image.open(path).convert("RGB")
    except Exception:
        return (-1, 0)
    w, h = img.size
    px = img.load()
    band_top, band_bot = int(h * 0.012), int(h * 0.085)  # scope strip only (above BNC row)
    # Bound to the LEFT/main portion of the scope: the kick lives here, and the
    # right edge can carry compositor bleed on transparent themes.
    x0, x1 = int(w * 0.04), int(w * 0.72)
    ys, xs = [], []
    for x in range(x0, x1, 2):
        col_hit = []
        for y in range(band_top, band_bot):
            r, g, b = px[x, y]
            # Color-agnostic trace detection: the scope bg is dark; the trace is
            # a bright/saturated line in ANY theme hue (bone, pink, green…).
            if max(r, g, b) > 110:
                col_hit.append(y)
        if col_hit:
            ys.extend(col_hit)
            xs.append(x)
    if len(ys) < 8:
        return (0, 0)
    vext = max(ys) - min(ys)                 # waveform peak-to-peak (px)
    rightmost = max(xs)
    completeness = (rightmost - x0) / float(x1 - x0)   # 0..1, how far right it drew
    score = vext * (0.45 + 0.55 * completeness)
    return (score, vext)


def main():
    for rule in ("noinitialfocus", "opaque", "noblur", "noshadow", "rounding 0"):
        subprocess.run(["hyprctl", "keyword", "windowrulev2",
                        f"{rule},class:^(Bombo)$"], stdout=subprocess.DEVNULL)

    # Clear the workspace behind the captures: the shaped window has alpha-0
    # corners + a (partly) transparent scope, so anything behind it bleeds into
    # the grab. Move other windows to a hidden special workspace SILENTLY (no
    # focus steal, no active-workspace switch) and restore them at the end.
    global _HIDDEN, _WSID
    _WSID = active_ws()
    _HIDDEN = list_ws_windows(_WSID) if _WSID is not None else []
    if _HIDDEN:
        print(f"   hiding {len(_HIDDEN)} window(s) on ws {_WSID} during capture")
        move_windows(_HIDDEN, "special:bombohide")
        time.sleep(0.6)
    for theme in THEMES:
        print(f"-> {theme}")
        best = None; best_score = 0; best_vext = 0
        for attempt in range(1, MAX_TRIES + 1):
            env = dict(os.environ, BOMBO_SCREENSHOT="1", BOMBO_FORCE_THEME=theme)
            proc = subprocess.Popen([BOMBO], env=env,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            geom = None
            for _ in range(30):
                time.sleep(0.5)
                geom = win_geom()
                if geom:
                    break
            if not geom:
                proc.kill(); time.sleep(1.0); continue
            x, y, w, h = geom
            gstr = f"{x},{y} {w}x{h}"
            time.sleep(WARMUP)

            frames = []
            for i in range(N_FRAMES):
                fp = f"{FRAMES}/{theme}_{attempt}_{i:02d}.png"
                subprocess.run(["grim", "-g", gstr, fp], stdout=subprocess.DEVNULL)
                frames.append(fp)
                time.sleep(FRAME_GAP)
            proc.terminate()
            try: proc.wait(timeout=3)
            except Exception: proc.kill()
            time.sleep(1.0)  # let ALSA fully release before next launch

            scored = [(score_kick(f), f) for f in frames]
            scored.sort(key=lambda t: t[0][0], reverse=True)
            (sc, vext), bf = scored[0]
            if sc > best_score:
                best_score, best_vext, best = sc, vext, bf
            if best_vext >= 30:          # got a satisfyingly tall kick
                break
            print(f"   attempt {attempt}: best so far {best_vext}px — retrying" )

        if best is None:
            print(f"   FAILED: no frame captured for {theme}"); continue
        out = f"{OUTDIR}/{theme}.png"
        Image.open(best).save(out)
        print(f"   best: {os.path.basename(best)}  kick_height={best_vext}px -> {theme}.png")

    print("done")


def cleanup():
    subprocess.run(["hyprctl", "keyword", "windowrulev2",
                    "unset,class:^(Bombo)$"], stdout=subprocess.DEVNULL)
    if _HIDDEN:
        move_windows(_HIDDEN, _WSID)
        print(f"   restored {len(_HIDDEN)} window(s) to ws {_WSID}")


if __name__ == "__main__":
    try:
        main()
    finally:
        cleanup()
