#!/usr/bin/env python3
"""derive_lane_bands.py  --  re-derive a lane's bands from its refs (scorer v2.2).

Usage:  python3 derive_lane_bands.py <lane> [--mode bounce|pulse|sparse|dark] [--margin 0.1] [--bars 16] [--refs DIR]
        lane = ukmin (ROLLER) | long (DEEP) | pop (BOUNCY) | jackin (JACKIN)

Reads the ref list from score_rolls.LANE_REFS (private: $ROLLA_GENRE_DATA_DIR/score_refs.json), finds each ref's MIDI under
<refs>/<ref id>/ (bass*.mid, drums*.mid, stabs*.mid; '_bXX-YY' suffixes = extra drop sections),
measures every section with the scorer's own measure() (no originality, no WAV), and prints per metric:
ref min / max, a suggested band (min/max widened by --margin of the span, at least the tolerance below),
and the band currently in LANE / STYLES.  It never edits the scorer: paste the numbers you accept,
note them in the (private) scorer changelog, then re-run the ref->lane check and PR rolls before/after.
Pending refs (no MIDI yet) are listed and skipped.
<refs> = --refs DIR or $ROLLA_REFS: the private reference MIDI folder, never stored in the repo.
"""
import argparse, glob, os, re, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import score_rolls as SR
T = os.environ.get("ROLLA_REFS", "")
KEYS = [("offkick_pct", "offkick"), ("top2_pct", "top2"), ("root_pct", None), ("roll_pcs", "pcs"), ("len_med16", "len"),
        ("held_ge4_pct", "held4"), ("long_ge6_pct", "long6"), ("notes_per_bar", "npb"), ("sounding_frac", None),
        ("bass_median_pitch", "reg"), ("rep1", "rep1"), ("rep2", None), ("rep4", None), ("octave_move_pct", "oct_ok"),
        ("dotted_share_pct", "dotted"), ("b2_time_pct", "b2"), ("stab_onbeat_pct", "stab_beat"), ("stab_dur_med16", "stab_sus"),
        ("hat_swing_pct", "hat_sw")]

def sections(ref):
    d = os.path.join(T, ref); out = {}
    for f in glob.glob(os.path.join(d, "*.mid")):
        m = re.match(r"(bass|drums|stabs)(_b\d+-\d+)?\.mid$", os.path.basename(f))
        if m: out.setdefault(m.group(2) or "main", []).append(f)
    return {k: v for k, v in out.items() if any("bass" in os.path.basename(x) for x in v)}

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("lane"); ap.add_argument("--mode"); ap.add_argument("--margin", type=float, default=0.1)
    ap.add_argument("--bars", type=int, help="force the bar count (e.g. 16: some Transcriber MIDI end-of-track marks imply 32 bars)")
    ap.add_argument("--refs", default=T, help="private reference MIDI dir (default: $ROLLA_REFS)")
    a = ap.parse_args(); cfg = SR.LANE_REFS[a.lane]
    if not a.refs: ap.error("needs --refs DIR or ROLLA_REFS=DIR (private reference MIDI)")
    globals()["T"] = a.refs
    refs = cfg.get("refs") or [r for md, rs in cfg.get("modes", {}).items() if not a.mode or md == a.mode for r in rs]
    if not refs: sys.exit("no refs listed for this lane: set ROLLA_GENRE_DATA_DIR to the folder with the private score_refs.json")
    lane_key = {("pop", "pulse"): "pop_pulse", ("long", "dark"): "long_dark"}.get((a.lane, a.mode), a.lane)
    print(f"# {cfg['name']} ({lane_key}) from {len(refs)} refs; pending (skipped): {cfg.get('pending') or 'none'}")
    rows = []
    with tempfile.TemporaryDirectory() as tmp:
        for ref in refs:
            for sec, files in sections(ref).items():
                rd = os.path.join(tmp, ref.replace("/", "_") + "__" + sec.strip("_")); os.makedirs(rd)
                for f in files: os.symlink(f, os.path.join(rd, re.sub(r"_b\d+-\d+", "", os.path.basename(f))))
        if not os.listdir(tmp): sys.exit("no ref MIDI found")
        rows = SR.score_dir(tmp, a.lane, out=os.path.join(tmp, "_score"), bars=a.bars, no_orig=True, quiet=True)[0]
        # rows are the refs listed for this lane/mode; metrics come from whichever mode the scorer picked (mode-independent here)
    if not rows: sys.exit("score_dir returned no rows (API changed?)")
    print("ref sections:", ", ".join(r["roll"] for r in rows))
    E = SR.LANE[lane_key]
    print(f"{'metric':22} {'ref min':>8} {'ref max':>8} {'suggest':>16}   current")
    for k, lk in KEYS:
        v = [r["m"].get(k) for r in rows if isinstance(r["m"].get(k), (int, float))]
        if not v: continue
        lo, hi = min(v), max(v); pad = max((hi - lo) * a.margin, abs(hi) * 0.03)
        print(f"{k:22} {lo:8.2f} {hi:8.2f} {f'{lo - pad:.2f}-{hi + pad:.2f}':>16}   {E.get(lk) if lk else '-'}")

if __name__ == "__main__":
    main()
