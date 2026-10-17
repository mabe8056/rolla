#!/usr/bin/env python3
"""Run mix_check render + mute on a MIX_SPEC §5.1 directory. Prints pass/fail per style and lane."""
import json, os, subprocess, sys

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

def main():
    if len(sys.argv) < 2:
        print("usage: run_mix_checks.py <render-dir>", file=sys.stderr)
        return 2
    root = sys.argv[1]
    rows = []
    failed = False
    for style in STYLES:
        folder = os.path.join(root, style)
        ok, data = run(["render", os.path.join(folder, "FULL.wav"), "--lane", "SUM", "--style", style])
        fails = []
        if not ok:
            failed = True
            for res in data.get("results", []):
                fails += [c.get("why") for c in res.get("checks", []) if not c.get("pass")]
        rows.append((style, "FULL", "PASS" if ok else "FAIL", "; ".join(fails[:4])))
        print(f"{style} FULL {'PASS' if ok else 'FAIL'}")
        for name, lane, voice in LANES:
            solo = os.path.join(folder, f"SOLO_{name}.wav")
            mute = os.path.join(folder, f"MUTE_{name}.wav")
            args = ["render", solo, "--lane", lane, "--style", style]
            if voice:
                args += ["--voice", voice]
            ok_r, data_r = run(args)
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
