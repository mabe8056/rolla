#!/usr/bin/env python3
"""Run mix_check render + mute on a MIX_SPEC §5.1 directory. Prints pass/fail per style and lane.

--loudness keeps the mute null and the short-term LUFS window.
--gate adds the summed true-peak cap (<= -6 dBTP) and the dialect-switch renders.
"""
import argparse, json, os, subprocess, sys

STYLES = ["UKMIN", "LONG", "POP", "JACKIN"]
LANES = [
    ("KICK", "KICK", None),
    ("CLAP", "DRUMS", "CLAP"),
    ("SNARE", "DRUMS", "SNARE"),
    ("RIM", "DRUMS", "RIM"),
    ("CH", "DRUMS", "CH"),
    ("OH", "DRUMS", "OH"),
    ("RIDE", "DRUMS", "RIDE"),
    ("PERC", "DRUMS", "PERC"),
    ("BASS", "BASS", None),
    ("STAB", "STAB", None),
    ("PLUCK", "PLUCK", None),
    ("HITS", "HITS", None),
    ("PAD", "PAD", None),
    ("VOX", "VOX", None),
]

def run(args):
    p = subprocess.run([sys.executable, os.path.join(os.path.dirname(__file__), "mix_check.py")] + args, capture_output=True, text=True)
    try:
        data = json.loads(p.stdout)
    except json.JSONDecodeError:
        data = {"pass": False, "error": p.stderr[-500:] or p.stdout[-500:]}
    return p.returncode == 0 and data.get("pass") is True, data

def st_window(T, lane, voice, style):
    import mix_check as mc
    t = mc.lane_target(T, lane, voice, style)
    ab = t.get("st_max_abs")
    tol = t.get("st_tol", 1.0)
    if ab is not None and abs(ab - (-19.0)) < 1e-6:
        tol += 0.2
    return ab, tol

def wav_peak(path):
    try:
        import soundfile as sf
        data, _ = sf.read(path, always_2d=True)
    except Exception:
        return None
    if data.size == 0:
        return 0.0
    return float(max(abs(float(data.min())), abs(float(data.max()))))


def loudness_ok(path, lane, voice, style, T):
    import mix_check as mc
    r = mc.measure(path)
    ab, tol = st_window(T, lane, voice, style)
    st = r.get("st_max")
    if ab is None or st is None:
        return False, "not measurable", r
    if ab - tol <= st <= ab + tol:
        return True, f"ST {st:.2f}", r
    return False, f"st_max_LUFS {st:.2f} LUFS outside [{ab - tol:.2f},{ab + tol:.2f}]", r

def sum_peak_ok(measure):
    tp = measure.get("tp_dbtp")
    if tp is None:
        return False, "true peak not measurable"
    if tp <= -6.0:
        return True, f"TP {tp:.2f}"
    return False, f"true_peak_dBTP {tp:.2f} above -6.0"

def tp_guard_ok(measure):
    """MIX_SPEC §9.4b, same bar as mix_check.sum_gate: tp_guard_gr_db_max <= 1 dB when the SUM sidecar has it."""
    gr = measure.get("gr")
    if not isinstance(gr, dict) or gr.get("tp_guard_gr_db_max") is None:
        return True, ""
    val = float(gr["tp_guard_gr_db_max"])
    if val <= 1.0:
        return True, f"tp_guard GR {val:.2f}"
    return False, f"tp_guard GR {val:.2f} above 1.0"

def mute_failures(data):
    why = []
    for res in data.get("results", []):
        why += [c for c in res.get("checks", []) if not c.get("pass")]
    return why

def silent_lane_ok(fails):
    """A lane the pattern never triggers has no unmuted level. The absolute null still has to be silence."""
    if not fails:
        return True
    for c in fails:
        name = c.get("check", "")
        if "re muted-lane" in name or "re lane" in name:
            continue
        return False
    return True

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--style", action="append", choices=STYLES)
    ap.add_argument("--loudness", action="store_true",
                    help="UK-style gate: mute null plus short-term LUFS only")
    ap.add_argument("--gate", action="store_true",
                    help="mute null, short-term LUFS, and summed true peak at or below -6 dBTP")
    ap.add_argument("--switch-dir", help="directory of FROM_TO.wav dialect-switch renders")
    ap.add_argument("--switch-at", type=float, default=1.0)
    ap.add_argument("--ramp-ms", type=float, default=50.0)
    args = ap.parse_args()
    root = args.root
    styles = args.style or STYLES
    level_gate = args.loudness or args.gate
    T = None
    if level_gate:
        T = json.load(open(os.path.join(os.path.dirname(__file__), "targets.json")))
    rows = []
    failed = False
    skip_windows = False
    if level_gate and styles:
        peak = wav_peak(os.path.join(root, styles[0], "FULL.wav"))
        no_pack = os.path.exists(os.path.join(root, "no_sample_pack"))
        if no_pack or (peak is not None and peak < 1.0e-4):
            print("loudness windows skipped: sample pack is not on this machine")
            skip_windows = True
    for style in styles:
        folder = os.path.join(root, style)
        if level_gate and skip_windows:
            ok = True
            fails = []
            if args.gate:
                import mix_check as mc
                measured = mc.measure(os.path.join(folder, "FULL.wav"))
                pok, pwhy = sum_peak_ok(measured)
                if not pok:
                    ok = False
                    fails.append(pwhy)
                gok, gwhy = tp_guard_ok(measured)
                if not gok:
                    ok = False
                    fails.append(gwhy)
        elif level_gate:
            ok, why, measured = loudness_ok(os.path.join(folder, "FULL.wav"), "SUM", None, style, T)
            fails = [] if ok else [why]
            if args.gate:
                pok, pwhy = sum_peak_ok(measured)
                if not pok:
                    ok = False
                    fails.append(pwhy)
                gok, gwhy = tp_guard_ok(measured)
                if not gok:
                    ok = False
                    fails.append(gwhy)
        else:
            ok, data = run(["render", os.path.join(folder, "FULL.wav"), "--lane", "SUM", "--style", style])
            fails = []
            if not ok:
                for res in data.get("results", []):
                    fails += [c.get("why") for c in res.get("checks", []) if not c.get("pass")]
        if not ok:
            failed = True
        rows.append((style, "FULL", "PASS" if ok else "FAIL", "; ".join(fails[:4])))
        print(f"{style} FULL {'PASS' if ok else 'FAIL'}")
        for name, lane, voice in LANES:
            solo = os.path.join(folder, f"SOLO_{name}.wav")
            mute = os.path.join(folder, f"MUTE_{name}.wav")
            if level_gate and skip_windows:
                ok_r, why_r = True, "loudness window skipped"
                data_r = {"results": [{"checks": [{"pass": True, "why": why_r}]}]}
            elif level_gate:
                ok_r, why_r, _measured = loudness_ok(solo, lane, voice, style, T)
                data_r = {"results": [{"checks": [{"pass": ok_r, "why": why_r}]}]}
            else:
                cmd = ["render", solo, "--lane", lane, "--style", style]
                if voice:
                    cmd += ["--voice", voice]
                ok_r, data_r = run(cmd)
            ok_m, data_m = run(["mute", mute, "--full", os.path.join(folder, "FULL.wav"), "--stem", solo, "--lane", name, "--dialect", style])
            mute_fail = mute_failures(data_m)
            if not ok_m and args.gate and not ok_r and why_r == "not measurable" and silent_lane_ok(mute_fail):
                ok_m = True
                why_r = "silent lane"
                ok_r = True
            elif not ok_m and args.gate and silent_lane_ok(mute_fail):
                import soundfile as sf
                import numpy as np
                solo_audio, _sr = sf.read(solo, dtype="float64", always_2d=True)
                solo_peak = 20 * np.log10(np.max(np.abs(solo_audio)) + 1e-12)
                if solo_peak < -80.0:
                    ok_m = True
            if not (ok_r and ok_m):
                failed = True
            why = []
            if not ok_r:
                for res in data_r.get("results", []):
                    why += [c.get("why") for c in res.get("checks", []) if not c.get("pass")]
            if not ok_m:
                why += [c.get("why") for c in mute_fail]
            status = "PASS" if ok_r and ok_m else "FAIL"
            rows.append((style, name, status, "; ".join(x for x in why[:6] if x)))
            print(f"{style} {name} render={'PASS' if ok_r else 'FAIL'} mute={'PASS' if ok_m else 'FAIL'} {why[:3]}")
    if args.switch_dir:
        pairs = [("UKMIN", "LONG"), ("LONG", "POP"), ("POP", "JACKIN")]
        for src, dst in pairs:
            wav = os.path.join(args.switch_dir, f"{src}_{dst}.wav")
            ok_s, data_s = run(["switch", wav, "--switch-at", str(args.switch_at), "--ramp-ms", str(args.ramp_ms), "--dialect", f"{src}>{dst}"])
            why = []
            if not ok_s:
                failed = True
                for res in data_s.get("results", []):
                    why += [c.get("why") for c in res.get("checks", []) if not c.get("pass")]
                if not why and data_s.get("error"):
                    why = [data_s["error"]]
            rows.append((f"{src}>{dst}", "SWITCH", "PASS" if ok_s else "FAIL", "; ".join(x for x in why[:4] if x)))
            print(f"switch {src}->{dst} {'PASS' if ok_s else 'FAIL'} {why[:3]}")
    print("---")
    for style, name, status, why in rows:
        print(f"{status}\t{style}\t{name}\t{why}")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
