#!/usr/bin/env python3
"""Measure SOLO renders made with makeup 0 and --bypass-sum, then write resources/mix_makeup.json.

Lane makeup is target ST minus measured ST (comp and sat sit before the trim, so this is 1:1).
sum_align_db is filled in a second pass: render FULL with that makeup and --bypass-sum, then
re-run with --sum-from-full. The frozen mask-63 trim is not part of that FULL, so
sum_align_db = G - trim63 and the shipped renders (trim on) sit G dB from the bypass measurement.
G is limited to ±0.45 dB so kick/bass stay inside their ±0.5 LU window.
"""
import json, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mix_check as mc

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
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

def main():
    if len(sys.argv) < 3:
        print("usage: calibrate_makeup.py <render-dir> <makeup.json> [--sum-from-full]", file=sys.stderr)
        return 2
    src, dest = sys.argv[1], sys.argv[2]
    sum_pass = "--sum-from-full" in sys.argv
    T = json.load(open(os.path.join(os.path.dirname(__file__), "targets.json")))
    trim = json.load(open(os.path.join(ROOT, "resources", "main_sum_trim.json")))
    prev = {"dialects": {}}
    if os.path.exists(dest):
        prev = json.load(open(dest))
    out = {"dialects": {}}
    for style in STYLES:
        lanes = {}
        tr = float(trim["dialects"][style]["by_mask"]["63"]["trim_db"])
        align = 0.0
        if sum_pass:
            full = os.path.join(src, style, "FULL.wav")
            r = mc.measure(full)
            g = -19.0 - r["st_max"]
            g = max(-0.45, min(0.45, g))
            if r["tp_dbtp"] + g > -5.05:
                g = min(g, -5.05 - r["tp_dbtp"])
            align = g - tr
            print(f"{style} FULL bypass ST {r['st_max']:.2f} TP {r['tp_dbtp']:.2f} trim63 {tr:.2f} -> sum_align {align:.2f} (net {g:.2f})")
        else:
            align = float(prev.get("dialects", {}).get(style, {}).get("sum_align_db", 0.0))
        for name, lane, voice in LANES:
            path = os.path.join(src, style, f"SOLO_{name}.wav")
            if sum_pass and not os.path.exists(path):
                lanes[name] = prev["dialects"][style]["lanes"].get(name, 0.0)
                continue
            r = mc.measure(path)
            target = mc.lane_target(T, lane, voice, style)["st_max_abs"]
            measured = r["st_max"]
            prev_mk = float(prev.get("dialects", {}).get(style, {}).get("lanes", {}).get(name, 0.0))
            if measured is None:
                mk = prev_mk
                print(f"{style} {name} silent")
            else:
                mk = prev_mk + (target - measured)
            mk = max(-36.0, min(36.0, mk))
            lanes[name] = round(mk, 2)
            if not sum_pass:
                got = None if measured is None else round(measured + mk, 2)
                print(f"{style} {name} measured {measured} target {target} makeup {lanes[name]} -> {got} TP {r['tp_dbtp']:.2f}")
        out["dialects"][style] = {"sum_align_db": round(align, 2), "lanes": lanes}
    json.dump(out, open(dest, "w"), indent=1)
    print("wrote", dest)
    return 0

if __name__ == "__main__":
    sys.exit(main())
