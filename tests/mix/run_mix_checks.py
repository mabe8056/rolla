#!/usr/bin/env python3
"""Run mix_check render + mute on a MIX_SPEC §5.1 directory. Prints pass/fail per style and lane.

--loudness keeps the mute null and the short-term LUFS window, and skips the
spectral / true-peak checks. Those belong with the full render set.
"""
import argparse, json, os, subprocess, sys

STYLES = ["UKMIN", "LONG", "POP", "GASKIN"]
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
        return False, "not measurable"
    if ab - tol <= st <= ab + tol:
        return True, f"ST {st:.2f}"
    return False, f"st_max_LUFS {st:.2f} LUFS outside [{ab - tol:.2f},{ab + tol:.2f}]"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root")
    ap.add_argument("--style", action="append", choices=STYLES)
    ap.add_argument("--loudness", action="store_true",
                    help="UK-style gate: mute null plus short-term LUFS only")
    args = ap.parse_args()
    root = args.root
    styles = args.style or STYLES
    T = None
    if args.loudness:
        T = json.load(open(os.path.join(os.path.dirname(__file__), "targets.json")))
    rows = []
    failed = False
    if args.loudness and styles:
        peak = wav_peak(os.path.join(root, styles[0], "FULL.wav"))
        if peak is not None and peak < 1.0e-4:
            print("loudness skipped: sample pack is not on this machine")
            return 0
    for style in styles:
        folder = os.path.join(root, style)
        if args.loudness:
            ok, why = loudness_ok(os.path.join(folder, "FULL.wav"), "SUM", None, style, T)
            fails = [] if ok else [why]
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
            if args.loudness:
                ok_r, why_r = loudness_ok(solo, lane, voice, style, T)
                data_r = {"results": [{"checks": [{"pass": ok_r, "why": why_r}]}]}
            else:
                cmd = ["render", solo, "--lane", lane, "--style", style]
                if voice:
                    cmd += ["--voice", voice]
                ok_r, data_r = run(cmd)
            ok_m, data_m = run(["mute", mute, "--full", os.path.join(folder, "FULL.wav"), "--stem", solo, "--lane", name, "--dialect", style])
            if not (ok_r and ok_m):
                failed = True
            why = []
            if not ok_r:
                for res in data_r.get("results", []):
                    why += [c.get("why") for c in res.get("checks", []) if not c.get("pass")]
            if not ok_m:
                for res in data_m.get("results", []):
                    why += [c.get("why") for c in res.get("checks", []) if not c.get("pass")]
            status = "PASS" if ok_r and ok_m else "FAIL"
            rows.append((style, name, status, "; ".join(x for x in why[:6] if x)))
            print(f"{style} {name} render={'PASS' if ok_r else 'FAIL'} mute={'PASS' if ok_m else 'FAIL'} {why[:3]}")
    print("---")
    for style, name, status, why in rows:
        print(f"{status}\t{style}\t{name}\t{why}")
    return 1 if failed else 0

if __name__ == "__main__":
    sys.exit(main())
