#!/usr/bin/env python3
"""
score_rolls.py -- A&R per-roll scorer for ROLLA generator output.

v2.2: BOUNCY (pop) has two modes, 'bounce' (REF_B1) and 'pulse' (REF_B2); a pop roll is
measured in both and keeps the mode it fits (CSV lane_mode); DEEP (long) likewise has 'sparse' (ref3) and 'dark' (REF_D1, REF_D2). DEEP held-note cap (held_ge4_pct <= 30, held = >= 4 sixteenths) and
root share 40-70 (soft ROOT_SHARE_OUT; v2.2.1 also BOUNCY bounce 30-64); JACKIN swing 50-57 (--swing-heavy 50-68), top2 >= 45, flavour = one of dotted / flat-2nd / sustained stabs /
short off-beat counter. On-kick is counted on the nearest 16th slot (|attack - kick| <= 0.49 16th), so ducked/late notes count.
Banned skeleton: <refs>/_banned_skeletons (shared dotted pulse of REF_B2/ref2/REF_D2) -> COPY_OF_REF:_banned_skeletons.
Lane refs live in LANE_REFS (one config spot); re-derive bands with derive_lane_bands.py.

Reference MIDI is private and never in the repo. Point the scorer at it with --refs DIR or the ROLLA_REFS
environment variable: one sub-folder per generic ref id (REF_R1, REF_D1, ref2, ...). Without either, the
originality check is skipped (same as --no-orig).

Usage:
  python3 score_rolls.py <dir_of_rolls> --style ukmin|long|pop|jackin [--out DIR] [--bars N] [--bpm 130]
                         [--refs DIR | env ROLLA_REFS] [--no-orig] [--exclude-self]
                         [--hard-profile maestro|gr_v2]

GENERATOR_RULES v2 (2026-10-17): a roll FAILS only on the hard codes of the chosen profile. Default `maestro`:
  OUT_OF_KEY, BASS_ON_KICK (>35% bass attacks on the kick, every style), EMPTY_BAR (incl. NO_BASS), COPY_OF_REF, CLIPPING, CLICKS, TAIL_INTO_BAR1, STUCK_NOTE,
  LANE_FIT_LOW, DUPLICATE_OF_ROLL, SAME_SKELETON_AS_ROLL; plus the batch skeleton gate (<=2 same-skeleton pairs at
  skel >= 0.55, <=3 rolls involved) and the soft-recurrence gate (any one soft code on > 40% of rolls, batches
  of >= 10) as batch-level FAILs. v2.1 also makes CLIPPING, CLICKS, TAIL_INTO_BAR1, STUCK_NOTE hard.
  Every other code is soft (shown `soft:`).
  `gr_v2` adds the GR v2 §5 technical list (NO_BASS, BASS_REGISTER, NO_GAPS, NO_HOLES, STUCK_NOTE, TAIL_*,
  CLIPPING, CLICKS, BANNED_FIGURE) -- opt-in only.

A ROLL is either a .mid file at the top of <dir> (bass/stab/drum as separate tracks or channels), or a
sub-folder holding several .mid files that are merged (e.g. bass.mid + stab.mid + drums.mid). A .wav with the
same stem (or any .wav inside the roll folder) is checked too (tail at loop end/start, peak, clipping, clicks).

Part detection (naming is not trusted):
  drums = MIDI channel 10, or a track/file name with drum|kick|perc|hat|clap|snare|rim
          (file names count only when a roll is a folder of per-part files)
  kick  = GM note 35/36 in the drum part, else the lowest drum note; no drums -> 4/4 kick assumed (CTR A11)
  bass  = the lowest-median-pitch melodic part (a part named bass/sub wins if there is one)
  stab  = a part named stab|chord|key|organ|piano|other|hit, else the remaining melodic part with the most
          chord onsets; vox/chop/string/lead/hook/pad/swell parts are ignored

Outputs (in --out, default <dir>/_score):  scores_<style>.csv  and  report_<style>.md
Sheet, thresholds and reason codes: tools/ROLLA_ROLL_SCORING.md.  Validation: tools/score_rolls_validation.md.
Exit code: 0 = style ships (>=80% pass on hard codes AND both batch gates hold), 1 = no-ship.

After listening:  python3 score_rolls.py --tally <out>/scores_<style>.csv
  combines the machine verdict with the judges' GENERATOR_RULES §8 38-point totals and the MELAK_KEEP ear column.
"""
import argparse, csv, math, os, re, statistics, sys, wave
from collections import defaultdict, Counter
import mido
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import originality_check as OC

SCORER_VERSION = "v2.2.1"
# ---------------------------------------------------------------- v2.2: ONE config spot for the refs behind every lane
# Each lane band below is re-derivable from these refs: add a ref (folder under the private refs dir, $ROLLA_REFS,
# holding bass/drums/stabs MIDI), then run  python3 derive_lane_bands.py <lane>  and paste the suggested min/max (+margin)
# into LANE / STYLES. Ref ids are generic folder names; the id -> track mapping stays private, outside the repo.
# 'pending' = named, no MIDI yet -> not used until it lands; placement by measurement.
LANE_REFS = {
    "ukmin":  dict(name="ROLLER", refs=["REF_R1", "REF_R2", "REF_R3", "REF_R4", "REF_R5", "REF_R6"],
                   support=["ref2"],          # support-only: on-kick 34.7-39.2%
                   pending=[]),
    "long":   dict(name="DEEP", modes={"sparse": ["ref3"],                       # space + sparseness lead
                                       "dark": ["REF_D1", "REF_D2"]},             # darkness/sub/pad lead = REF_D1
                   comparison=["REF_R2"],   # sparse-DEEP-like, but stays ROLLER in the scorer (no churn)
                   spectral_refs=["REF_D1", "REF_D2", "REF_R2", "ref2"],
                   pending=[]),
    "pop":    dict(name="BOUNCY", modes={"bounce": ["REF_B1"], "pulse": ["REF_B2"]}, pending=[]),
    "jackin": dict(name="JACKIN", refs=["REF_J1", "REF_J2", "ref1"],
                   support=["REF_J3"],   # radio edit; 4-bar sparse loop: groove/production support, not in bands
                   pending=[]),
}
BANNED_SKELETON_DIR = "_banned_skeletons"   # <refs>/_banned_skeletons: synthesized MIDI of the shared REF_B2/ref2/REF_D2
                                            # dotted pulse (v2.2); a roll matching it fails COPY_OF_REF:_banned_skeletons
# v2.2.1: originality refs switched OFF until confirmed. Each entry = path under the refs dir (suffix match)
# + why. Re-enable by deleting the entry (or commenting it out). originality_check.py's own CLI still scans every file.
ORIG_EXCLUDE = {
    "REF_J1/stabs.mid": "low-confidence CQT stab transcription; off until the Composer confirms it by ear",
    "REF_J1/stabs_b73-88.mid": "low-confidence CQT stab transcription; off until the Composer confirms it by ear",
}
KICK_TOL16 = 0.49   # v2.2: a bass attack is ON the kick when it sits within 0.49 16th of a kick hit (= same nearest 16th slot).
                    # v2.1 used 0.3: a ducked/late (40-50 ms) or transcription-jittered note escaped the 35% hard rule.
HELD_MIN16 = 4      # v2.2: a 'held note' = a bass note sounding >= 4 sixteenths (count share of bass notes) -> held_ge4_pct
# ---------------------------------------------------------------- style profiles (sources in ROLLA_ROLL_SCORING.md §3)
BASE = dict(
    offkick_min=65.0,          # CTR A2/E1 72-98% off kick; GR AV16 hard cap on-kick 35%; refs 68.4-98.8 (REF_R2 2nd drop 68.4)
    top2_min=55.0,             # CTR A3/E2: top-2 pitch classes 63-98% (main drops); REF_R3 2nd drop 58.6 here -> 55
    len_med=(1.0, 2.5),        # CTR A4/E3: median 1.1-2.3 sixteenths; +-0.1/0.2 tolerance
    pcs_bar=(1.3, 5.2),        # CTR A3: 1.9-4.8 distinct pitches/bar; this scorer: 1.94-5.0 on refs -> 1.3-5.2
    roll_pcs_max=6,            # TOO_MANY_PCS: pcs holding >=5% of bass time; refs 2-6 (REF_R4 6); DESIGN cap 4 + 2
    notes_bar=(1.5, 9.0),      # density: refs 2.2-8.6 attacks/bar (CTR C REF_R1 8.25, validation table)
    sounding_max=0.975,        # NO_GAPS: refs UK 0.36-0.97 (REF_D2 0.97); REF_R6 0.99-1.00 = CTR outlier
    run_max=7,                 # NOTE_RUN: >=8 consecutive 16th attacks (CM 2a.11 'no continuous 16ths'); REF_R1 max 7
    oct_max=26.0,              # OCTAVE_PUMP: A&R review §0.3 octaves 0-25% (bimodal 0-8 / 15-25), flag >26; REF_B1 25.6
    reg_med=(20, 45),          # BASS_REGISTER: median MIDI pitch A0..A2 (CTR C: REF_R3 A0-F#1; REF_B1 octave-2 mid-bass)
    swing_max=58.5,            # BASS_SWUNG: bass timing 46-58% (CTR A8); REF_R2 57.5
    rep_min=0.50,              # LOOP_NO_REPEAT = GR AV09: gr_match@2 < 0.50 AND gr_match@4 < 0.47 (refs 0.536-0.863 / 0.469-0.917)
    static_max=0.90,           # STATIC_LOOP: 1-bar match; CTR A7 1-bar 0.05-0.75, this scorer 0.0-0.67 on refs
    stab_onbeat_max=33.0,      # STAB_ON_BEAT: CTR A9/E7 <=30% on the beat; +3 tolerance
    stab_pcs_max=4,            # STAB_TOO_THICK: median pcs per hit >4 (CTR A9 1-3; QUARTAL 4 allowed, DESIGN rule 17)
    stab_clash_max=80.0,       # STAB_CLASH: >80% of stab hits double a bass attack; refs 24-75% (REF_R4 counter-riff)
    kick4_min=0.80,            # KICK_NOT_4x4: CTR A11 >=94% of quarters; transcribed refs read 0.83-0.98 -> 0.80
    bpm=(128.0, 134.0),        # TEMPO_OUT (soft): CTR A1 130-132
    # GR §6 AVOID checks, re-calibrated on the references (see ROLLA_ROLL_SCORING.md §6)
    inbeat_min=3,              # GRID_LOCKED: in-beat positions used by >=10% of onsets (GR AV02); refs 3-4
    npb_sd_min=0.5, npb_range_min=2,   # CONSTANT_DENSITY = GR AV04 (sd<0.5 OR range<2); refs sd 0.66-2.33, range 2-7
    rest_min16=1,              # NO_HOLES: no silent 16th at all; GR AV05 >=4 fails 8/17 ref sections (refs 1-37)
    root_max=80.0,             # ROOT_DRONE: GR AV06 root share >80%
    leap_max=19,               # LEAP_TOO_BIG: GR AV07 says 15; refs leap up to 17 (REF_D2) and 19 (REF_B1) -> 19
    step_run_max=5,            # SCALE_RUN: GR AV01 >5 same-direction steps (refs max 1-5)
)
STYLES = {
    "ukmin":  dict(BASE),
    "long":   dict(BASE, len_med=(1.8, 8.0), notes_bar=(1.0, 7.5), allow_long=True),            # REF_D2 anchor: DESIGN §4 LONG 2-8/16; measured 6.9-7.1 attacks/bar
    "pop":    dict(BASE, offkick_min=65.0, len_med=(0.8, 1.6)),        # GR v2 H2: on-kick > 35% fails in every style                # REF_B1 98% off kick, 1.12/16
    "jackin": dict(BASE, offkick_min=65.0, gr_match1_max=0.20, swing_mode=True, len_med=(1.5, 2.6), stab_onbeat_max=50.0, stab_clash_max=100.0,
                   sounding_max=0.97, kick4_min=0.85, swing_max=60.0, top2_min=45.0),   # CTR D1-D6; v2.2 top2 45 (ref1 49.3, REF_J1/J2 56-66)
}
STYLES["long"]["root_band"] = (40.0, 70.0)
STYLES["pop"]["root_band"] = (30.0, 64.0)       # v2.2.1 BOUNCY bounce root share: GR 30-42 widened to the derived REF_B1 band 57.4-63.1 -> 30-64      # v2.2 DEEP root share (time): ref3 62.8, REF_D2 43.8-48.1 -> soft ROOT_SHARE_OUT
# v2.2 BOUNCY 'pulse' mode (REF_B2, bars 65-96): steady dotted pulse, longer notes, higher register.
STYLES["pop_pulse"] = dict(BASE, offkick_min=65.0, len_med=(1.8, 3.2), static_max=0.95, pulse=True, reg_med=(20, 45))
# v2.2 DEEP 'dark' flavour (REF_D1 + REF_D2): deep through darkness and a constant pad, not sparseness.
# Busier, shorter bass allowed (REF_D1 8.0-8.6 attacks/bar, median 1.68/16); pad re-attacks may sit on the beat (REF_D1 52-57%).
STYLES["long_dark"] = dict(STYLES["long"], len_med=(1.4, 8.0), notes_bar=(1.0, 9.5), stab_onbeat_max=60.0)
STYLE_CHOICES = ["jackin", "long", "pop", "ukmin"]          # *_pulse / *_dark are modes, chosen per roll inside score_dir
# A&R review §8 lane-fit envelopes (UK reference ranges; LONG/POP = their anchors; JACKIN its own). Range membership,
# never similarity to a reference. A roll fits the lane when <= 2 of the measurable features miss (">= 10 of 12").
LANE_UK = dict(offkick=(70, 100), top2=58, pcs=(2, 6), len=(1.0, 2.4), long6=8, npb=(3.9, 8.7), run=7, eaa=20,
               reg=(25, 36), rep=0.50, rep1=0.70, oct_ok=((0, 8.5), (15, 26)), stab_beat=30, signpost=50,
               stab_pcs=(1, 4), clash=80, kick=0.85, clap=75, hat_sw=(49, 59.5), duck=(17, None))
# Review §8 numbers widened to the measured UK envelope where the review's own drops fall outside it
# (ar_vocab_measure.json uk_ranges): top-2 58.6, pcs>=5% up to 6, len 1.07-2.31, npb 3.94-8.62, hat swing 49.1-59.1,
# stab pcs/hit up to 4 (DESIGN rule 17 quartal); kick 0.85 = transcription tolerance (CTR A11 94% is audio-measured).
LANE = {"ukmin": LANE_UK,
        "long": dict(LANE_UK, len=(1.8, 4.0)),                                   # REF_D2 2.25-2.31 (+DESIGN LONG 2-8)
        "pop": dict(LANE_UK, offkick=(85, 100), len=(0.9, 1.4)),                 # REF_B1 97.7-98.8 off kick, 1.07-1.11
        "jackin": dict(LANE_UK, offkick=(65, 100), top2=45, pcs=(2, 5), len=(1.5, 2.6), dotted=55, rep1=0.10, rep2g=0.60,
                       b2=10, stab_sus=6, duck=(3, 36), hat_sw=(50, 57), stab_beat=50, clash=100)}
HEAVY_SWING_BAND = (50, 68)   # --swing-heavy (soft, optional 90s switch): JACKIN hats may swing up to 68%
# v2.2: JACKIN = REF_J2 + REF_J1 (lead) + ref1; REF_J3 = support (not in bands).
# Hat swing 50-57 (Transcriber hat swing 50.6-55.3 across the JACKIN refs; our MIDI 51.8-53.6), --swing-heavy widens to 50-68.
# top2 >= 45 (ref1 49.3), rep2 >= 0.60 (REF_J1 0.62-0.66), duck 3-36 dB wide (REF_J2 3.6 .. ref1 33), stab on-beat <= 50,
# stab/bass clash unbounded (= STYLES jackin). Flavour (F6) needs ONE of: dotted >= 55% / flat-2nd >= 10% / sustained stabs
# (median >= 6/16; REF_J1 organ-pad stabs 6.4-8.1) / short off-beat counter-stab.
# DEEP (long): held_ge4 cap 30 (ref3 23.1-ish, REF_D2 0-6) replaces long6 in F3.
LANE["long"]["held4"] = 30.0
# BOUNCY pulse lane (REF_B2): off-kick 65-100 (REF_B2 72.4), len 1.8-3.2 (2.7), register 25-42 (median 39), 1-bar repeat allowed to 0.95
LANE["pop_pulse"] = dict(LANE_UK, offkick=(65, 100), len=(1.8, 3.2), reg=(25, 42), rep1=0.95)
# DEEP dark lane (REF_D1 + REF_D2): length 1.4-4.0 (REF_D1 1.68), density up to 9.5 (REF_D1 8.0-8.6), bar repeat >= 0.42
# (REF_D1 grid rep 0.43-0.53), a continuous pad (stab median >= 16/16) satisfies F8. DEEP sparse = LANE["long"] (ref3 + REF_D2).
LANE["long_dark"] = dict(LANE["long"], len=(1.4, 4.0), npb=(3.9, 9.5), rep=0.42, pad_ok=True)
ALL_CODES = {"NO_BASS", "BASS_ON_KICK", "TOP2_LOW", "TOO_MANY_PCS", "PCS_PER_BAR", "BASS_NOTES_TOO_SHORT",
        "BASS_NOTES_TOO_LONG", "NO_GAPS", "NOTE_RUN", "OCTAVE_PUMP", "BASS_REGISTER", "BASS_SWUNG", "DENSITY_LOW",
        "DENSITY_HIGH", "LOOP_NO_REPEAT", "STATIC_LOOP", "STAB_ON_BEAT", "STAB_TOO_THICK", "STAB_CLASH",
        "KICK_NOT_4x4", "COPY_OF_REF", "DUPLICATE_OF_ROLL", "SAME_SKELETON_AS_ROLL", "STUCK_NOTE",
        "TAIL_PAST_LOOP", "TAIL_INTO_BAR1", "CLIPPING", "CLICKS",
        "GRID_LOCKED", "CONSTANT_DENSITY", "NO_HOLES", "ROOT_DRONE", "LEAP_TOO_BIG", "SCALE_RUN", "BANNED_FIGURE",
        "CALL_ANSWER_MISSING", "LANE_FIT_LOW", "BASS_ORGAN_8THS", "BASS_16TH_ROLL", "BASS_REESE_LONG", "OFFBEAT_PUMP_ONLY",
        "PIANO_OFFBEAT_CHORDS", "HAT_16THS", "SWING_90S", "BUILD_FX_IN_LOOP", "LATIN_PERC_BED", "OUT_OF_KEY", "EMPTY_BAR", "ROOT_SHARE_OUT"}
# What fails a roll (GENERATOR_RULES v2 §1 + Maestro's clarification, 2026-10-17 ~4:53 PM ET). Everything else is soft.
HARD_PROFILES = {
    # default: the 4 hard rules (key, kick/bass clash > 35%, empty/broken bars, originality) + lane fit + duplicates
    "maestro": {"OUT_OF_KEY", "BASS_ON_KICK", "EMPTY_BAR", "COPY_OF_REF", "LANE_FIT_LOW", "DUPLICATE_OF_ROLL", "SAME_SKELETON_AS_ROLL",
                "CLIPPING", "CLICKS", "TAIL_INTO_BAR1", "STUCK_NOTE"},
    # opt-in, never the default: GENERATOR_RULES v2 §5 lists these A&R codes under H2/H3/H4 as well
    "gr_v2": {"OUT_OF_KEY", "BASS_ON_KICK", "EMPTY_BAR", "COPY_OF_REF", "LANE_FIT_LOW", "DUPLICATE_OF_ROLL", "SAME_SKELETON_AS_ROLL",
              "NO_BASS", "BASS_REGISTER", "NO_GAPS", "NO_HOLES", "STUCK_NOTE", "TAIL_PAST_LOOP", "TAIL_INTO_BAR1", "CLIPPING",
              "CLICKS", "BANNED_FIGURE"},
}
HARD = set(HARD_PROFILES["maestro"])
SCALE = {"ukmin": {0, 1, 2, 3, 5, 7, 8, 9, 10}, "long": {0, 1, 2, 3, 5, 7, 8, 9, 10}, "pop": {0, 1, 2, 3, 5, 7, 8, 9, 10},
         "jackin": {0, 1, 2, 3, 5, 7, 8, 10}}            # GR v2 H1 (degrees rel. tonic: 1 b2 2 b3 4 5 b6 (6) b7)
for _k in STYLES: STYLES[_k]["scale"] = SCALE[_k.split("_")[0]]
FX_RE = re.compile(r"riser|sweep|uplift|downlift|crash|whoosh|impact|noise ?fx|\bfx\b|build", re.I)
LATIN_RE = re.compile(r"clave|conga|bongo|timbal|latin", re.I)

def lane_fit(m, style, winfo):
    """12 range-membership features (A&R review §8). Returns (in_range, measurable, misses list)."""
    E = LANE[style]; res = {}
    g = lambda k: m.get(k)
    def rng(v, lo, hi): return None if v is None else ((lo is None or v >= lo) and (hi is None or v <= hi))
    res["F1_offkick"] = rng(g("offkick_pct"), *E["offkick"])
    res["F2_pitch"] = None if g("top2_pct") is None else (g("top2_pct") >= E["top2"] and E["pcs"][0] <= g("pcs_ge5") <= E["pcs"][1])
    if "held4" in E:   # v2.2 DEEP: held notes (>= HELD_MIN16) are a trait; cap from ref3 + REF_D2
        res["F3_length"] = None if g("len_med16") is None else (rng(g("len_med16"), *E["len"]) and g("held_ge4_pct") <= E["held4"])
    else:
        res["F3_length"] = None if g("len_med16") is None else (rng(g("len_med16"), *E["len"]) and g("long_ge6_pct") <= E["long6"])
    res["F4_density"] = None if g("notes_per_bar") is None else (rng(g("notes_per_bar"), *E["npb"]) and g("max_16th_run") <= E["run"] and g("beats_eaa_pct") <= E["eaa"])
    res["F5_register"] = rng(g("bass_median_pitch"), *E["reg"])
    if style == "jackin":
        flav = (g("dotted_share_pct") >= E["dotted"] or g("b2_time_pct") >= E["b2"] or (g("stab_dur_med16") or 0) >= E["stab_sus"]
                or (bool(g("stab_hits")) and (g("stab_dur_med16") or 99) <= 1.5 and (g("stab_onbeat_pct") or 100) <= 10))
        res["F6_loop"] = None if g("rep1") is None else (g("rep1") <= E["rep1"] and (g("rep2") or 0) >= E["rep2g"] and flav)
    else:
        best = max([x for x in (g("rep2"), g("rep4")) if x is not None] or [0])
        res["F6_loop"] = None if g("rep1") is None else (best >= E["rep"] and g("rep1") <= E["rep1"])   # back-half = soft tendency
    o = g("octave_move_pct")
    res["F7_octaves"] = None if o is None else any(lo <= o <= hi for lo, hi in E["oct_ok"])
    if g("stab_hits") and g("stab_onbeat_pct") is not None and E.get("pad_ok") and (g("stab_dur_med16") or 0) >= 16:
        res["F8_stab"] = True                                                 # v2.2 DEEP dark: continuous pad
        res["F9_stab_vs_bass"] = g("stab_on_bass_attack_pct") <= E["clash"]
    elif g("stab_hits") and g("stab_onbeat_pct") is not None:
        ok = g("stab_onbeat_pct") <= E["stab_beat"] and E["stab_pcs"][0] <= g("stab_pcs_med") <= E["stab_pcs"][1]
        if style == "jackin": pass          # v2.2: sustained stabs are one JACKIN flavour option (F6), not required
        else: ok = ok and g("stab_mode") in ("dub", "riff") and (g("stab_mode") == "dub" or g("stab_signpost_bars_pct") >= E["signpost"])
        res["F8_stab"] = ok
        res["F9_stab_vs_bass"] = g("stab_on_bass_attack_pct") <= E["clash"]
    else:
        res["F8_stab"] = res["F9_stab_vs_bass"] = None
    res["F10_kick_clap"] = None if g("kick_quarter_cov") is None else (g("kick_quarter_cov") >= E["kick"] and (g("clap_2_4_bars_pct") or 0) >= E["clap"])
    hsw = g("hat_swing_pct")
    res["F11_hats"] = None if g("hat_offbeat_bars_pct") is None else (g("hat_offbeat_bars_pct") >= 50 and (g("closed_hat_per_bar") or 0) <= 10 and (hsw is None or rng(hsw, *E["hat_sw"])))
    res["F12_duck"] = rng(winfo.get("duck_db") if winfo else None, *E["duck"])
    meas = [k for k, v in res.items() if v is not None]
    miss = [k for k in meas if not res[k]]
    return len(meas) - len(miss), len(meas), miss
SHIP_RATE = 0.80
REC_MAX_SHARE, REC_MIN_N = 0.40, 10      # v2.1 soft-recurrence gate (Maestro 2026-10-17)
GATE_MAX_PAIRS, GATE_MAX_ROLLS = 2, 3      # GR v2 §4.2: <= 2 same-skeleton pairs and <= 3 rolls in any pair per 20-roll batch
HARD_PROFILE_NAME = ["maestro"]
INFO_SOFT = {"KICK_ASSUMED", "NO_STAB", "BONUS_STABS_IN_RESTS", "WAV_UNREADABLE", "SKIPPED_AUDIO"}   # not deviations
DUP_SKEL, SAME_SKEL = 0.80, 0.55          # bass skeleton sim between two rolls (OC skel_win FAIL = 0.55)
DRUM_RE = re.compile(r"drum|kick|perc|hat|clap|snare|rim|\bbd\b|909|808", re.I)
BASS_RE = re.compile(r"bass|sub", re.I)
STAB_RE = re.compile(r"stab|chord|key|organ|piano|other|hit", re.I)
IGNORE_RE = re.compile(r"vox|vocal|chop|string|lead|hook|pad|swell|bell|arp", re.I)

# ---------------------------------------------------------------- parsing
def parse_roll(paths):
    groups, stuck, tempo, end_beats = [], [], None, 0.0
    for path in paths:
        mid = mido.MidiFile(path); ppq = mid.ticks_per_beat
        for ti, tr in enumerate(mid.tracks):
            t, tname, on, bych = 0, "", defaultdict(list), defaultdict(list)
            for msg in tr:
                t += msg.time
                if msg.type == "set_tempo" and tempo is None: tempo = mido.tempo2bpm(msg.tempo)
                elif msg.type == "track_name": tname = msg.name
                elif msg.type == "note_on" and msg.velocity > 0: on[(msg.channel, msg.note)].append((t, msg.velocity))
                elif msg.type in ("note_off", "note_on"):
                    k = (msg.channel, msg.note)
                    if on[k]:
                        s, v = on[k].pop(0); bych[msg.channel].append((s / ppq, t / ppq, msg.note, v))
            end_beats = max(end_beats, t / ppq)
            for (ch, n), lst in on.items():
                for s, v in lst: stuck.append(f"{os.path.basename(path)}:{tname} note {n} @beat {s/ppq:.2f}")
            for ch, notes in bych.items():
                # the file name is a part name only when a roll is a folder of per-part files (bass.mid, drums.mid);
                # a single-file roll's name ("roll_07_kickless.mid") must not decide the part
                fname = os.path.basename(path) if len(paths) > 1 else ""
                name = f"{fname}|{tname}|ch{ch+1}"
                groups.append(dict(name=name, ch=ch, notes=sorted(notes),
                                   drum=(ch == 9 or bool(DRUM_RE.search(tname)) or bool(DRUM_RE.search(fname)))))
    return groups, stuck, tempo, end_beats

def assign_parts(groups):
    drums = [g for g in groups if g["drum"]]
    mel = [g for g in groups if not g["drum"] and len(g["notes"]) >= 4]
    med = lambda g: statistics.median(n[2] for n in g["notes"])
    named_bass = [g for g in mel if BASS_RE.search(g["name"].split("|", 1)[1] or g["name"]) or BASS_RE.search(g["name"].split("|")[0])]
    named_bass = [g for g in named_bass if not STAB_RE.search(g["name"].split("|")[0] + g["name"].split("|")[1]) or len(named_bass) == 1]
    cand = named_bass or [g for g in mel if not IGNORE_RE.search(g["name"])] or mel
    bass = min(cand, key=med) if cand else None
    rest = [g for g in mel if g is not bass and not IGNORE_RE.search(g["name"])]
    named_stab = [g for g in rest if STAB_RE.search(g["name"])]
    def chordiness(g):
        by = Counter(round(n[0] * 4) for n in g["notes"]); return sum(1 for c in by.values() if c >= 2) / max(1, len(by))
    stab = (max(named_stab, key=lambda g: len(g["notes"])) if named_stab else
            (max(rest, key=chordiness) if rest else None))
    kick = []
    if drums:
        alln = [n for g in drums for n in g["notes"]]
        k = [n for n in alln if n[2] in (35, 36)]
        if not k and alln:
            lo = min(n[2] for n in alln); k = [n for n in alln if n[2] == lo]
        kick = sorted(n[0] * 4 for n in k)
        if kick:   # drop stray off-beat kick detections (transcription false positives) unless they form a pattern
            nbar = max(1, int(max(kick) // 16) + 1)
            cnt = Counter(int(round(t)) % 16 for t in kick if int(round(t)) % 4 != 0)
            keep = {sl for sl, c in cnt.items() if c >= 0.5 * nbar}
            kick = [t for t in kick if int(round(t)) % 4 == 0 or int(round(t)) % 16 in keep]
    hats = sorted(n[0] * 4 for g in drums for n in g["notes"] if n[2] in (42, 44, 46))
    return bass, stab, kick, hats, drums

# ---------------------------------------------------------------- measurements
def mono_bass(notes):
    by = {}
    for s, e, p, v in notes:
        k = round(s * 4)
        if k not in by or p < by[k][2]: by[k] = (s * 4, e * 4, p, v)
    ev = [by[k] for k in sorted(by)]
    out = []
    for i, (s, e, p, v) in enumerate(ev):
        nxt = ev[i + 1][0] if i + 1 < len(ev) else None
        out.append((s, e, p, v, max(0.05, min(e, nxt) - s) if nxt is not None else max(0.05, e - s)))
    return out          # (start16, end16, pitch, vel, sounding_len16)

def swing_pct(times16):
    odd = [t - round(t) for t in times16 if round(t) % 2 == 1]
    even = [t - round(t) for t in times16 if round(t) % 2 == 0]
    if len(odd) < 4 or len(even) < 4: return None
    return round(50 + 50 * (statistics.median(odd) - statistics.median(even)), 1)

def grid_tokens(bn, L):
    tok = ["."] * L
    for s, e, p, v, ln in bn:
        a = int(round(s))
        if 0 <= a < L:
            tok[a] = str(p % 12)
            for k in range(a + 1, min(L, int(round(s + ln)))):
                if tok[k] == ".": tok[k] = "-"
    return tok

def grid_match(tok, lag):
    # attack slots + pitch class must match; sustain and rest count as the same 'no attack' state
    na = lambda x: "_" if x in ".-" else x
    pairs = [(na(tok[i]), na(tok[i + lag])) for i in range(len(tok) - lag) if not (na(tok[i]) == "_" and na(tok[i + lag]) == "_")]
    return round(sum(a == b for a, b in pairs) / len(pairs), 3) if pairs else None

def measure(bass, stab, kick, hats, drums, bars, tempo, S):
    m, codes, soft = {}, [], []
    L = bars * 16
    kick_assumed = not kick
    if kick_assumed:
        kick = [float(i) for i in range(0, L, 4)]; soft.append("KICK_ASSUMED")
    else:
        q = [i for i in range(0, L, 4)]
        cov = sum(1 for b in q if any(abs(k - b) <= 0.5 for k in kick)) / len(q)
        m["kick_quarter_cov"] = round(cov, 2)
        if cov < S["kick4_min"]: codes.append("KICK_NOT_4x4")
    if tempo:
        m["bpm"] = round(tempo, 2)
        if not (S["bpm"][0] <= tempo <= S["bpm"][1]): soft.append("TEMPO_OUT")
    if not bass:
        return m, codes + ["NO_BASS", "EMPTY_BAR"], soft
    bn = [n for n in mono_bass(bass["notes"]) if n[0] < L - 0.5]
    if len(bn) < 4:
        return m, codes + ["NO_BASS", "EMPTY_BAR"], soft
    att = [n[0] for n in bn]
    ks = sorted(kick)
    def near_kick(t): return any(abs(t - k) <= KICK_TOL16 for k in ks)
    m["offkick_pct"] = round(100 * sum(1 for t in att if not near_kick(t)) / len(att), 1)
    if m["offkick_pct"] < S["offkick_min"]: codes.append("BASS_ON_KICK")
    elif 100 - m["offkick_pct"] > 28: soft.append("ONKICK_HIGH")                 # GR v2: 28-35% on the kick = soft
    # ---- GR v2 H3: empty bars (no bass onset or sounding bass note in a bar)
    empty = [b + 1 for b in range(bars) if not any(n[0] < (b + 1) * 16 and n[1] > b * 16 for n in bn)]
    m["empty_bars"] = " ".join(map(str, empty))
    if empty: codes.append("EMPTY_BAR")
    # ---- GR v2 H1: key / scale. Tonic = whichever of the two most-sounding bass pcs fits the dialect scale best.
    sc = S.get("scale", SCALE["ukmin"]); tp = Counter()
    for n in bn: tp[n[2] % 12] += n[4]
    ttot = sum(tp.values()) or 1
    stab_ons = [(s0, p0) for s0, e0, p0, v0 in (stab["notes"] if stab else []) if s0 * 4 < L - 0.5]
    sp_c = Counter(p0 % 12 for _, p0 in stab_ons)
    def misfit(t):          # out-of-scale share: bass time + stab note-ons (equal weight)
        bo = sum(v for pc, v in tp.items() if (pc - t) % 12 not in sc) / ttot
        so = (sum(c for pc, c in sp_c.items() if (pc - t) % 12 not in sc) / len(stab_ons)) if stab_ons else 0
        return bo + so
    cands = [pc for pc, _ in tp.most_common(2)]                 # GR H1: tonic is one of the 2 most-sounding bass pcs
    tonic_k = min(cands, key=misfit)
    inset = lambda p: (p - tonic_k) % 12 in sc
    m["key_tonic_pc"] = tonic_k
    m["bass_out_of_key_pct"] = round(100 * sum(v for pc, v in tp.items() if (pc - tonic_k) % 12 not in sc) / ttot, 1)
    nn_t = 0.0
    for i, n in enumerate(bn):
        if inset(n[2]): continue
        nb = [bn[j][2] for j in (i - 1, i + 1) if 0 <= j < len(bn)]
        if not any(abs(n[2] - q) == 1 and inset(q) for q in nb): nn_t += n[4]
    m["bass_out_nonneighbour_pct"] = round(100 * nn_t / ttot, 1)
    stab_out = sum(1 for _, p0 in stab_ons if not inset(p0))
    stab_db = sum(1 for s0, p0 in stab_ons if not inset(p0) and int(round(s0 * 4)) % 16 == 0)
    m["stab_out_of_key_pct"] = round(100 * stab_out / len(stab_ons), 1) if stab_ons else None
    m["stab_out_on_downbeat"] = stab_db
    kfail = []
    if m["bass_out_of_key_pct"] > 5: kfail.append(f"bass {m['bass_out_of_key_pct']}% out of scale")
    if m["bass_out_nonneighbour_pct"] > 2: kfail.append(f"bass {m['bass_out_nonneighbour_pct']}% non-neighbour")
    m["out_of_key_why"] = "; ".join(kfail)
    # stab half of H1 is reported, not hard: unvalidated (reference stabs are CQT peaks; their harmonics read 0-30% "out of key")
    if m["stab_out_of_key_pct"] is not None and (m["stab_out_of_key_pct"] > 5 or stab_db): soft.append("STAB_OUT_OF_KEY")
    if kfail: codes.append("OUT_OF_KEY")
    tpc = Counter()
    for s, e, p, v, ln in bn: tpc[p % 12] += ln
    tot = sum(tpc.values()); top = tpc.most_common()
    m["top2_pct"] = round(100 * sum(v for _, v in top[:2]) / tot, 1)
    m["root_pct"] = round(100 * top[0][1] / tot, 1)
    if m["top2_pct"] < S["top2_min"]: codes.append("TOP2_LOW")
    m["roll_pcs_all"] = len(tpc)
    m["roll_pcs"] = sum(1 for v in tpc.values() if v >= 0.05 * tot)     # pitch classes with >=5% of bass time
    if m["roll_pcs"] > S["roll_pcs_max"]: codes.append("TOO_MANY_PCS")
    perbar = [len({n[2] for n in bn if b * 16 <= n[0] < b * 16 + 16}) for b in range(bars)]
    perbar = [x for x in perbar if x]
    m["pitches_per_bar"] = round(statistics.mean(perbar), 2) if perbar else 0
    if not (S["pcs_bar"][0] <= m["pitches_per_bar"] <= S["pcs_bar"][1]): codes.append("PCS_PER_BAR")
    lens = [n[4] for n in bn]
    m["len_med16"] = round(statistics.median(lens), 2)
    if m["len_med16"] < S["len_med"][0]: codes.append("BASS_NOTES_TOO_SHORT")
    if m["len_med16"] > S["len_med"][1]: codes.append("BASS_NOTES_TOO_LONG")
    m["notes_per_bar"] = round(len(bn) / bars, 2)
    if m["notes_per_bar"] < S["notes_bar"][0]: codes.append("DENSITY_LOW")
    if m["notes_per_bar"] > S["notes_bar"][1]: codes.append("DENSITY_HIGH")
    snd = [False] * L
    for s, e, p, v, ln in bn:
        for k in range(int(round(s)), min(L, int(round(s + min(ln, e - s))))): snd[k] = True
    m["sounding_frac"] = round(sum(snd) / L, 2)
    m["rest_slots_frac"] = round(1 - m["sounding_frac"], 2)
    if m["sounding_frac"] > S["sounding_max"]: codes.append("NO_GAPS")
    slots = sorted({int(round(t)) for t in att}); run = best = 1
    for a, b in zip(slots, slots[1:]):
        run = run + 1 if b == a + 1 else 1; best = max(best, run)
    m["max_16th_run"] = best
    if best > S["run_max"]: codes.append("NOTE_RUN")
    mv = [bn[i + 1][2] - bn[i][2] for i in range(len(bn) - 1)]
    m["octave_move_pct"] = round(100 * sum(1 for x in mv if abs(x) in (12, 24)) / max(1, len(mv)), 1)
    if m["octave_move_pct"] > S["oct_max"]: codes.append("OCTAVE_PUMP")
    m["bass_median_pitch"] = statistics.median(n[2] for n in bn)
    if not (S["reg_med"][0] <= m["bass_median_pitch"] <= S["reg_med"][1]): codes.append("BASS_REGISTER")
    sw = swing_pct(att); m["bass_swing_pct"] = sw
    if sw is not None and sw > S["swing_max"]: codes.append("BASS_SWUNG")
    if sw is not None and 59.5 < sw <= 62 and not S.get("swing_mode"): codes.append("SWING_90S")
    if hats: m["hat_swing_pct"] = swing_pct(hats)
    if drums:
        dn = [(n[0] * 4, n[2]) for g in drums for n in g["notes"] if n[0] * 4 < L - 0.5]
        cl = {int(round(t)) for t, p in dn if p in (38, 39, 40)}
        m["clap_2_4_bars_pct"] = round(100 * sum(1 for b in range(bars) if b * 16 + 4 in cl and b * 16 + 12 in cl) / bars, 1) if cl else 0.0
        hs = {int(round(t)) for t, p in dn if p in (42, 44, 46)}
        m["hat_offbeat_bars_pct"] = round(100 * sum(1 for b in range(bars) if any(b * 16 + o in hs for o in (2, 6, 10, 14))) / bars, 1)
        m["closed_hat_per_bar"] = round(sum(1 for t, p in dn if p in (42, 44)) / bars, 2)
        if m["closed_hat_per_bar"] > 10: codes.append("HAT_16THS")
        sw = m.get("hat_swing_pct")
        if sw is not None and 59.5 < sw <= 62 and not S.get("swing_mode"): codes.append("SWING_90S")   # UK refs hats 49.1-59.1
        if any(p in (49, 52, 55, 57) for t, p in dn): codes.append("BUILD_FX_IN_LOOP")        # GM crash/china/splash
        lat = sum(1 for t, p in dn if 60 <= p <= 66 or p == 75)                                  # bongo/conga/timbale/claves
        if lat >= bars and not S.get("allow_latin"): codes.append("LATIN_PERC_BED")
    tok = grid_tokens(bn, L)
    m["rep1"], m["rep2"], m["rep4"] = grid_match(tok, 16), grid_match(tok, 32), grid_match(tok, 64) if bars >= 8 else None
    best_rep = max([x for x in (m["rep2"], m["rep4"]) if x is not None] or [0])
    m["rep_best_2_4"] = best_rep
    # variation position: where does each bar differ from the bar one period earlier?
    per = 64 if (m["rep4"] or 0) >= (m["rep2"] or 0) and bars >= 8 else 32
    diff = [i % 16 for i in range(per // 2, L) if i - 16 >= 0 and tok[i] != tok[i - 16]]
    m["var_last4_pct"] = round(100 * sum(1 for d in diff if d >= 12) / len(diff), 1) if diff else None
    m["var_backhalf_pct"] = round(100 * sum(1 for d in diff if d >= 8) / len(diff), 1) if diff else None
    if m["var_backhalf_pct"] is not None and m["var_backhalf_pct"] < 50: soft.append("VAR_NOT_IN_BACK_HALF")
    bars_tok = ["".join(tok[b * 16:(b + 1) * 16]) for b in range(bars)]
    m["distinct_bass_bars"] = len(set(bars_tok))
    if (m["rep1"] or 0) > S["static_max"] or m["distinct_bass_bars"] == 1: codes.append("STATIC_LOOP")
    # ---- GENERATOR_RULES.md (GR) §6 AVOID metrics, measured so the rubric and this scorer share one basis
    m["onkick_pct"] = round(100 - m["offkick_pct"], 1)
    ib = Counter(int(round(t)) % 4 for t in att)
    m["inbeat_pos_ge10"] = sum(1 for k in range(4) if ib[k] >= 0.10 * len(att))           # GR AV02
    barsets = [frozenset(int(round(t)) % 16 for t in att if b * 16 <= round(t) < b * 16 + 16) for b in range(bars)]
    m["generic_bars"] = sum(1 for x in barsets if x and x in OC.GENERIC_BAR_RHYTHMS)
    if m["inbeat_pos_ge10"] < S["inbeat_min"] or m["generic_bars"] > bars // 2: codes.append("GRID_LOCKED")
    npb = [len(x) for x in barsets]
    m["npb_sd"] = round(statistics.pstdev(npb), 2) if len(npb) > 1 else 0.0
    m["npb_range"] = max(npb) - min(npb) if npb else 0                                      # GR AV04
    if S.get("pulse"):      # v2.2 BOUNCY pulse: a steady attack count IS the mode; variety must come from a turnaround bar
        if m["distinct_bass_bars"] < 2: codes.append("CONSTANT_DENSITY")
    elif m["npb_sd"] < S["npb_sd_min"] or m["npb_range"] < S["npb_range_min"]: codes.append("CONSTANT_DENSITY")
    rest, best_rest = 0, 0
    for k in range(L):
        rest = 0 if snd[k] else rest + 1; best_rest = max(best_rest, rest)
    m["longest_rest16"] = best_rest                                                           # GR AV05
    if best_rest < S["rest_min16"]: codes.append("NO_HOLES")
    if bars == 8:
        r8 = [0]
        for k in range(7 * 16, 8 * 16): r8.append(0 if snd[k] else r8[-1] + 1)
        m["bar8_rest16"] = max(r8)
        m["bar8_vs_bar4_same"] = bars_tok_eq = ("".join(tok[112:128]) == "".join(tok[48:64]))
        if m["bar8_rest16"] < 4 or bars_tok_eq: soft.append("NO_BAR8_TURNAROUND")         # GR §2 / AV05 / AV08 (8-bar rolls)
    if m["root_pct"] > S["root_max"]: codes.append("ROOT_DRONE")                              # GR AV06
    m["max_leap"] = max((abs(x) for x in mv), default=0)
    if m["max_leap"] > S["leap_max"]: codes.append("LEAP_TOO_BIG")                            # GR AV07
    run_d = best_d = 0; prev = 0
    for x in mv:                                                                              # GR AV01: same-direction steps
        d = (1 if x > 0 else -1) if 0 < abs(x) <= 2 else 0
        run_d = run_d + 1 if d and d == prev else (1 if d else 0); prev = d; best_d = max(best_d, run_d)
    m["step_run_moves"] = best_d
    if best_d > S["step_run_max"]: codes.append("SCALE_RUN")
    # GR match@L: fraction of 16th slots whose sounding pitch (or rest) equals the slot L bars later
    sp = [None] * L
    for s0, e0, p0, v0, ln0 in bn:
        for k in range(int(round(s0)), min(L, int(round(s0 + ln0)))): sp[k] = p0
    gm = lambda lag: round(sum(sp[i] == sp[i + lag] for i in range(L - lag)) / (L - lag), 3) if L > lag else None
    m["gr_match1"], m["gr_match2"], m["gr_match4"] = gm(16), gm(32), gm(64)
    if (m["gr_match2"] or 0) < S["rep_min"] and (m["gr_match4"] is None or m["gr_match4"] < 0.47): codes.append("LOOP_NO_REPEAT")
    if (m["gr_match1"] or 0) > 0.95 and "STATIC_LOOP" not in codes: codes.append("STATIC_LOOP")
    if S.get("gr_match1_max") is not None and (m["gr_match1"] or 0) > S["gr_match1_max"]: codes.append("CALL_ANSWER_MISSING")
    # GR AV14 banned figures (relative to the most-sounding pitch class = tonic proxy)
    tonic = top[0][0]
    ton_on = {int(round(n[0])) for n in bn if n[2] % 12 == tonic}
    f1 = sum(1 for b in range(bars) if b * 16 + 1 in ton_on and b * 16 + 3 in ton_on)       # root on slots 2 AND 4
    f2 = sum(1 for n in bn if int(round(n[0])) % 16 == 14 and (n[2] - tonic) % 12 == 3 and n[1] > (int(round(n[0])) // 16 + 1) * 16)
    m["banned_2and4_bars"], m["banned_b3_tie_bars"] = f1, f2
    if f1 > bars / 2 or f2 > bars / 2: codes.append("BANNED_FIGURE")
    # ---- A&R review §8 lane features + generic-house flags: bass part
    lens_all = [n[4] for n in bn]; S_att = {int(round(t)) for t in att}; beats = range(0, L, 4)
    m["long_ge6_pct"] = round(100 * sum(1 for x in lens_all if x >= 6) / len(lens_all), 1)
    m["held_ge4_pct"] = round(100 * sum(1 for x in lens_all if x >= HELD_MIN16) / len(lens_all), 1)
    if S.get("root_band") and not (S["root_band"][0] <= m["root_pct"] <= S["root_band"][1]): codes.append("ROOT_SHARE_OUT")
    m["beats_eaa_pct"] = round(100 * sum(1 for b in beats if {b + 1, b + 2, b + 3} <= S_att) / len(beats), 1)
    m["beats_offbeat_only_pct"] = round(100 * sum(1 for b in beats if b + 2 in S_att and not ({b, b + 1, b + 3} & S_att)) / len(beats), 1)
    m["dotted_share_pct"] = round(100 * sum(1 for k in S_att if k % 16 in (0, 3, 6, 9, 12, 15)) / len(S_att), 1)
    m["b2_time_pct"] = round(100 * sum(v for pc, v in tpc.items() if (pc - tonic) % 12 == 1) / tot, 1)
    m["pcs_ge5"] = m["roll_pcs"]
    e8 = [sum(1 for k in range(b * 16, b * 16 + 16, 2) if k in S_att) for b in range(bars)]
    m["eighths_attacked_med"] = statistics.median(e8) if e8 else 0
    if m["eighths_attacked_med"] >= 6 and m["onkick_pct"] >= 40: codes.append("BASS_ORGAN_8THS")
    if m["max_16th_run"] > 7 or m["beats_eaa_pct"] > 25: codes.append("BASS_16TH_ROLL")
    if m["long_ge6_pct"] >= 15 and not S.get("allow_long"): codes.append("BASS_REESE_LONG")
    if m["beats_offbeat_only_pct"] >= 75 and m["root_pct"] >= 95: codes.append("OFFBEAT_PUMP_ONLY")
    vels = [n[3] for n in bn]
    m["vel_layers"] = len({v // 8 for v in vels})
    if m["vel_layers"] < 2: soft.append("ONE_VELOCITY")
    # ---- stab
    if not stab:
        soft.append("NO_STAB")
    else:
        by = defaultdict(list)
        for s, e, p, v in stab["notes"]:
            if s * 4 < L - 0.5: by[round(s * 4)].append((s * 4, e * 4, p))
        hits = sorted(by.items())
        m["stab_hits"] = len(hits); m["stab_hits_per_bar"] = round(len(hits) / bars, 2)
        if hits:
            m["stab_onbeat_pct"] = round(100 * sum(1 for k, _ in hits if k % 4 == 0) / len(hits), 1)
            if m["stab_onbeat_pct"] > S["stab_onbeat_max"]: codes.append("STAB_ON_BEAT")
            pcs = [len({p % 12 for _, _, p in ns}) for _, ns in hits]
            m["stab_pcs_med"] = statistics.median(pcs)
            if m["stab_pcs_med"] > S["stab_pcs_max"]: codes.append("STAB_TOO_THICK")
            bslots = {int(round(t)) for t in att}
            m["stab_on_bass_attack_pct"] = round(100 * sum(1 for k, _ in hits if k in bslots) / len(hits), 1)
            m["stab_in_bass_rest_pct"] = round(100 * sum(1 for k, _ in hits if 0 <= k < L and not snd[k]) / len(hits), 1)
            if m["stab_on_bass_attack_pct"] > S["stab_clash_max"]: codes.append("STAB_CLASH")
            m["stab_median_pitch"] = statistics.median(p for _, ns in hits for _, _, p in ns)
            sb = ["|".join(f"{k%16}:{sorted({p%12 for _,_,p in ns})}" for k, ns in hits if b * 16 <= k < b * 16 + 16) for b in range(bars)]
            m["distinct_stab_bars"] = len(set(sb))
            hk = [k for k, _ in hits]
            m["stab_ea_pct"] = round(100 * sum(1 for k in hk if k % 4 in (1, 3)) / len(hk), 1)
            m["stab_dur_med16"] = round(statistics.median(max(e - s0 for s0, e, _ in ns) for _, ns in hits), 2)
            pres = sum(1 for b in range(bars) if any(b * 16 + sl in by for sl in (14, 3, 7, 9)))
            m["stab_signpost_bars_pct"] = round(100 * pres / bars, 1)
            hpb = m["stab_hits_per_bar"]
            # density modes (A&R review §0.2): dub 0.5-3/bar, riff 4-10/bar (+0.5 tol: REF_B1 10.38). The review's
            # ">=70% of riff hits on e/a" is reported (stab_ea_pct) but not required: refs read 27.8-75% (only REF_B1 >=70).
            m["stab_mode"] = "dub" if 0.5 <= hpb <= 3 else ("riff" if 4 <= hpb <= 10.5 else "generic")
            pob = [sum(1 for k, ns in hits if b * 16 <= k < b * 16 + 16 and k % 4 == 2 and len({p % 12 for _, _, p in ns}) >= 4) for b in range(bars)]
            if sum(1 for x in pob if x >= 3) > bars / 2: codes.append("PIANO_OFFBEAT_CHORDS")
    return m, codes, soft

# ---------------------------------------------------------------- technical
def midi_technical(groups, stuck, bars, end_beats):
    codes, info = [], {}
    if stuck: codes.append("STUCK_NOTE"); info["stuck"] = stuck[:3]
    L = bars * 4
    over = [(g["name"], n[2], round(n[1] - L, 3)) for g in groups for n in g["notes"] if n[0] < L and n[1] > L + 1e-3]
    if over: codes.append("TAIL_PAST_LOOP"); info["tail_past_loop"] = over[:3]
    info["end_beats"] = round(end_beats, 3)
    return codes, info

def d2_clicks(x, sr, thr=10.0, floor=0.02):
    """Step discontinuities: a sample-to-sample jump |x[n]-x[n-1]| that is >thr x the local RMS of the first
    difference (+-10 ms) and >floor (about a -34 dBFS step). Kick/hat attacks and fast envelopes are smooth at
    sample level and do not trigger it; hard-gated notes, buffer seams and cut releases do (validated on
    _validation/neg_technical T1/T2 vs T4)."""
    import numpy as np
    d = np.zeros_like(x); d[1:] = x[1:] - x[:-1]
    w = int(.01 * sr); c = np.concatenate([[0], np.cumsum(d.astype(np.float64) ** 2)])
    i = np.arange(len(d)); lo = np.clip(i - w, 0, len(d)); hi = np.clip(i + w, 0, len(d))
    loc = np.sqrt((c[hi] - c[lo]) / np.maximum(hi - lo, 1))
    idx = np.where((np.abs(d) > thr * np.maximum(loc, 1e-6)) & (np.abs(d) > floor))[0]
    ev = []
    for k in idx:
        if not ev or k - ev[-1] > int(.005 * sr): ev.append(int(k))
    return [round(k / sr, 3) for k in ev if k > int(.005 * sr)]

def wav_technical(path, bars, bpm):
    import numpy as np
    codes, info = [], {}
    w = wave.open(path); sr, ch, sw, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(n); w.close()
    if sw == 2: x = np.frombuffer(raw, np.int16).astype(np.float32) / 32768
    elif sw == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3); x = ((b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8) | (b[:, 2].astype(np.int32) << 16)) << 8 >> 8).astype(np.float32) / 8388608
    else: x = np.frombuffer(raw, np.int32).astype(np.float32) / 2147483648
    x = x.reshape(-1, ch); m = x.mean(1)
    db = lambda a: float(10 * np.log10(np.mean(a.astype(np.float64) ** 2) + 1e-20))
    pk = float(np.max(np.abs(x))); info["peak_dbfs"] = round(20 * math.log10(pk + 1e-12), 2)
    info["clipped_samples"] = int((np.abs(x) >= 0.999).sum())
    if info["clipped_samples"] > 0: codes.append("CLIPPING")
    info["dc"] = round(float(m.mean()), 5)
    # loop start = first non-silent sample if it is within one 16th of the file start (render pre-roll), else 0
    s16 = 60.0 / bpm / 4; nz = np.where(np.abs(m) > 1e-4)[0]
    off = int(nz[0]) if len(nz) and nz[0] < int(s16 * sr) else 0
    Ls = int(round(bars * 16 * s16 * sr)); end = off + Ls; k16 = int(s16 * sr)
    loop = m[off:end]
    info["loop_rms_dbfs"] = round(db(loop), 1)
    info["bar1_first16_dbfs"] = round(db(m[off:off + k16]), 1)
    info["last16_dbfs"] = round(db(m[end - k16:end]), 1)
    if len(m) > end + int(0.01 * sr):
        tail = m[end:end + k16]
        info["tail_first16_dbfs"] = round(db(tail), 1)
        info["tail_0_500ms_dbfs"] = round(db(m[end:end + int(.5 * sr)]), 1)
        rel = info["tail_first16_dbfs"] - info["bar1_first16_dbfs"]
        info["tail_vs_bar1_db"] = round(rel, 1)
        if info["tail_first16_dbfs"] > -60 and (rel > -20 or (info["bar1_first16_dbfs"] < -50 and info["tail_first16_dbfs"] > -40)):
            codes.append("TAIL_INTO_BAR1")
    else:
        info["tail_note"] = "no tail rendered past the loop end: wrap not measurable"
        info["seam_step"] = round(float(abs(m[min(end, len(m)) - 1] - m[off])), 4)
        if info["seam_step"] > 0.05: codes.append("CLICKS")
    # bass ducking proxy (A&R review F12): <150 Hz level in the 40 ms after each beat vs the loudest 40 ms of that beat
    try:
        F = np.fft.rfft(loop.astype(np.float64)); fr = np.fft.rfftfreq(len(loop), 1 / sr); F[fr > 150] = 0
        lb = np.fft.irfft(F, len(loop)); del F
        w40 = int(.04 * sr); beat = int(4 * s16 * sr); dips = []
        for b0 in range(0, len(lb) - beat, beat):
            seg = lb[b0:b0 + beat] ** 2
            wins = [seg[i:i + w40].mean() for i in range(0, beat - w40, w40 // 2)]
            if max(wins) > 1e-8: dips.append(10 * math.log10(max(wins) / max(seg[:w40].mean(), 1e-12)))
        if dips: info["duck_db"] = round(float(np.median(dips)), 1)
    except Exception:
        pass
    cl = d2_clicks(m, sr); info["clicks_at_s"] = cl[:6]
    if cl: codes.append("CLICKS")
    return codes, info

# ---------------------------------------------------------------- originality + diversity
_REF_CACHE = {}
def ref_streams(refs_dir):
    if refs_dir not in _REF_CACHE:
        files = sorted(os.path.join(r, f) for r, _, fs in os.walk(refs_dir) for f in fs if f.lower().endswith((".mid", ".midi")))
        files = [f for f in files if not any(f.replace(os.sep, "/").endswith("/" + x) for x in ORIG_EXCLUDE)]   # v2.2.1
        _REF_CACHE[refs_dir] = [(f, OC.load_streams(f)) for f in files]
    return _REF_CACHE[refs_dir]

def roll_streams(bass, stab):
    st, alln = {}, []
    for nm, g in (("bass", bass), ("stab", stab)):
        if g:
            notes = [(s, e, p, v) for s, e, p, v in g["notes"]]
            st[nm] = (OC.to_events(notes), OC.to_chords(notes)); alln += notes
    if len(st) > 1: st["MERGED_LOW"] = (OC.to_events(alln), OC.to_chords(alln))
    return st

def originality(st, refs_dir, exclude):
    worst, hits = "PASS", []
    for rp, rs in ref_streams(refs_dir):
        if any(x and x in rp for x in exclude): continue
        for cn, ce in st.items():
            for rn, re_ in rs.items():
                lv, why = OC.verdict(OC.compare(ce, re_))
                # A rhythm-skeleton-only FAIL against a LOW-confidence reference stream (stem CQT peaks, merged
                # parts) is transcription noise, not a copy: downgrade it to FLAG (REF_FLAG, listen). Validation:
                # one JACKIN ref's stabs "failed" vs a DEEP ref's CQT chops at skel_win 0.60 (two unrelated records).
                low = "LOW" in rn.upper() or cn == "MERGED_LOW"
                if lv == "FAIL" and low and all(w.startswith("skel") for w in why if "FAIL" in w):
                    lv, why = "FLAG", [w + " (LOW-confidence ref, downgraded)" for w in why]
                if lv != "PASS":
                    hits.append((lv, os.path.relpath(rp, refs_dir), cn, rn, why[:2]))
                    worst = max(worst, lv, key=OC.RANK.get)
    return worst, sorted(hits, key=lambda h: -OC.RANK[h[0]])

def skel_sim(a, b):
    if len(a) < OC.MIN_EVENTS or len(b) < OC.MIN_EVENTS: return 0.0
    f = lambda x, y: max([v["max"] for k, v in OC.skeleton_window(x, y).items() if k in (2, 4)] or [0])
    return round(max(f(a, b), f(b, a)), 3)

def stab_sim(a, b):
    if not a or not b: return None
    A = {(s % 32, pcs) for s, pcs in a}; B = {(s % 32, pcs) for s, pcs in b}
    sa = {s % 32 for s, _ in a}; sb = {s % 32 for s, _ in b}
    return round(0.5 * OC.jac(sa, sb) + 0.5 * OC.jac(A, B), 3)

def stab_events(g):
    if not g: return []
    by = defaultdict(set)
    for s, e, p, v in g["notes"]: by[round(s * 4)].add(p % 12)
    root = Counter(p % 12 for s, e, p, v in g["notes"]).most_common(1)[0][0]
    return [(k, frozenset((x - root) % 12 for x in pcs)) for k, pcs in sorted(by.items())]

# ---------------------------------------------------------------- driver
def find_rolls(d):
    rolls = []
    for name in sorted(os.listdir(d)):
        p = os.path.join(d, name)
        if name.startswith(("_", ".")): continue
        if os.path.isdir(p):
            mids = sorted(os.path.join(p, f) for f in os.listdir(p) if f.lower().endswith((".mid", ".midi")))
            wavs = sorted(os.path.join(p, f) for f in os.listdir(p) if f.lower().endswith(".wav"))
            if mids: rolls.append((name, mids, wavs[0] if wavs else None))
        elif name.lower().endswith((".mid", ".midi")):
            stem = os.path.splitext(p)[0]; wv = stem + ".wav"
            rolls.append((os.path.splitext(name)[0], [p], wv if os.path.exists(wv) else None))
    return rolls

def score_dir(d, style, out=None, bars=None, bpm=None, refs=OC.DEFAULT_REFS, no_orig=False, exclude_self=False, quiet=False):
    S = STYLES[style]; rows = []
    if not refs: no_orig = True          # no private refs dir given: originality is skipped, never guessed
    refs = refs or ""
    ref_folders = sorted({os.path.relpath(r, refs).split(os.sep)[0] for r, _, fs in os.walk(refs) if any(f.endswith(".mid") for f in fs)}) if os.path.isdir(refs) else []
    for name, mids, wav in find_rolls(d):
        groups, stuck, tempo, end_beats = parse_roll(mids)
        nb = bars
        if not nb:
            last = max([n[0] for g in groups for n in g["notes"]] or [0])
            eb = end_beats if end_beats > 0 else last
            nb = int(round(eb / 4)) if abs(eb / 4 - round(eb / 4)) < 0.07 and eb / 4 >= last / 4 else int(math.floor(last / 4) + 1)
            nb = max(1, min(nb, OC.MAX_BARS))
        bass, stab, kick, hats, drums = assign_parts(groups)
        modes = {"pop": [("pop", "bounce"), ("pop_pulse", "pulse")],      # v2.2 BOUNCY modes
                 "long": [("long", "sparse"), ("long_dark", "dark")]}.get(style, [(style, "")])   # v2.2 DEEP flavours
        meas_by_mode = [(lk, md) + tuple(measure(bass, stab, kick, hats, drums, nb, tempo, STYLES[lk])) for lk, md in modes]
        _, _, m, codes, soft = meas_by_mode[0]; codes = list(codes); soft = list(soft); extra = []
        c2, tinfo = midi_technical(groups, stuck, nb, end_beats); extra += c2
        winfo = {}
        audio_skip = {"CLIPPING", "CLICKS", "TAIL_INTO_BAR1", "LOUDNESS"}
        if wav:
            try:
                c3, winfo = wav_technical(wav, nb, tempo or bpm or 130.0)
                if winfo.get("peak_dbfs", 0) < -45:
                    c3 = [c for c in c3 if c.split(":")[0] not in audio_skip]
                    winfo["audio_checks"] = "SKIPPED"
                    winfo["skip_reason"] = "no sample energy; clicks, tails, and loudness are not scored"
                    extra.append("~SKIPPED_AUDIO")
                extra += c3
            except Exception as e:
                extra.append("~WAV_UNREADABLE"); winfo = {"error": str(e)}
        else:
            winfo = {"audio_checks": "SKIPPED", "skip_reason": "no wav; clicks, tails, and loudness are not scored"}
            extra.append("~SKIPPED_AUDIO")
        names = " ".join(g["name"] for g in groups)
        if FX_RE.search(names): extra.append("BUILD_FX_IN_LOOP")
        if LATIN_RE.search(names) and not S.get("allow_latin"): extra.append("LATIN_PERC_BED")
        if drums: winfo.pop("duck_db", None)            # a full mix: the kick is in the low band, so no duck reading
        best = None
        for lk, md, m_, codes_, soft_ in meas_by_mode:
            codes_ = list(codes_) + [c for c in extra if not c.startswith("~")]; soft_ = list(soft_) + [c[1:] for c in extra if c.startswith("~")]
            if "NO_BASS" not in codes_:
                fin, fme, fmiss = lane_fit(m_, lk, winfo)
                m_["lane_fit"] = f"{fin}/{fme}"; m_["lane_miss"] = " ".join(fmiss); m_["lane_mode"] = md
                if fme - fin > 2: codes_.append("LANE_FIT_LOW")
                if m_.get("stab_in_bass_rest_pct", 0) >= 50: soft_.append("BONUS_STABS_IN_RESTS")
                key = (sum(1 for c in codes_ if c.split(":")[0] in HARD), fme - fin, len(soft_))
            else:
                key = (99, 99, 99)
            if best is None or key < best[0]: best = (key, m_, codes_, soft_)
        _, m, codes, soft = best
        codes[:] = list(dict.fromkeys(codes))
        excl = [f for f in ref_folders if exclude_self and name.startswith(f)]
        orig, ohits = ("SKIPPED", [])
        st = roll_streams(bass, stab)
        if not no_orig and st:
            orig, ohits = originality(st, refs, [os.sep + f + os.sep for f in excl])
            if orig == "FAIL":
                codes.append("COPY_OF_REF:" + ohits[0][1].split(os.sep)[0])
            elif orig == "FLAG":
                soft.append("REF_FLAG:" + ohits[0][1].split(os.sep)[0])
        rows.append(dict(roll=name, bars=nb, files=";".join(os.path.basename(x) for x in mids), wav=os.path.basename(wav) if wav else "",
                         bass_part=bass["name"] if bass else "", stab_part=stab["name"] if stab else "",
                         kick_src="assumed 4/4" if not kick else f"{len(kick)} hits", m=m, codes=codes, soft=soft, tinfo=tinfo,
                         winfo=winfo, orig=orig, ohits=ohits[:3], st=st, bass_notes=bass["notes"] if bass else [], stab_ev=stab_events(stab), excluded_refs=excl))
        if not quiet: print(f"  scored {name}: {'FAIL' if any(c.split(':')[0] in HARD for c in codes) else 'PASS'} {codes}", file=sys.stderr)
    # ---- batch diversity
    n = len(rows); sims = {}
    for i in range(n):
        for j in range(i + 1, n):
            bi = rows[i]["st"].get("bass", ([], []))[0]; bj = rows[j]["st"].get("bass", ([], []))[0]
            sk = skel_sim(bi, bj); ss = stab_sim([(s, p) for s, p in rows[i]["stab_ev"]], [(s, p) for s, p in rows[j]["stab_ev"]])
            exact = bool(bi) and len(bi) == len(bj) and len({(a[0], (a[1] - b[1]) % 12) for a, b in zip(bi, bj)}) == len({a[0] for a in bi}) \
                and len({(a[1] - b[1]) for a, b in zip(bi, bj)}) == 1 and all(a[0] == b[0] for a, b in zip(bi, bj))
            sims[(i, j)] = (sk, ss, exact)
    for j in range(n):
        for i in range(j):
            sk, ss, exact = sims[(i, j)]
            tag = None
            if exact or (sk >= DUP_SKEL and (ss is None or ss >= 0.75)): tag = f"DUPLICATE_OF_ROLL:{rows[i]['roll']}"
            elif sk >= SAME_SKEL: tag = f"SAME_SKELETON_AS_ROLL:{rows[i]['roll']}"
            if tag and not any(c.startswith(tag.split(':')[0]) for c in rows[j]["codes"]):
                rows[j]["codes"].append(tag); rows[j]["m"]["closest_roll"] = f"{rows[i]['roll']} skel={sk} stab={ss}"
    pair_sk = [v[0] for v in sims.values()]
    # GR §7.2 basis: pairwise Jaccard of 8-bar bass onset-slot sets (0..127)
    osets = [frozenset(int(round(s0)) for s0, *_ in mono_bass(r["bass_notes"]) if s0 < 128) for r in rows]
    pj = sorted(len(osets[i] & osets[j]) / len(osets[i] | osets[j]) for i in range(n) for j in range(i + 1, n) if osets[i] | osets[j])
    div = dict(pairs=len(pair_sk), gr_onset_jaccard_mean=round(statistics.mean(pj), 3) if pj else None,
               gr_onset_jaccard_p90=round(pj[int(0.9 * (len(pj) - 1))], 3) if pj else None,
               gr_targets="mean <= 0.35, p90 <= 0.45 (GENERATOR_RULES 7.2, ESTIMATE)", mean_bass_skel_sim=round(statistics.mean(pair_sk), 3) if pair_sk else None,
               max_bass_skel_sim=max(pair_sk) if pair_sk else None,
               pairs_ge_flag040=sum(1 for v in pair_sk if v >= 0.40), pairs_ge_same055=sum(1 for v in pair_sk if v >= SAME_SKEL),
               distinct_bar1_bass_rhythms=len({tuple(sorted(s % 16 for s, _ in r["st"].get("bass", ([], []))[0] if s < 16)) for r in rows}),
               distinct_stab_voicings=len({p for r in rows for _, p in r["stab_ev"]}),
               distinct_partner_pc=len({sorted(Counter(p % 12 for _, p in r["st"]["bass"][0]).items(), key=lambda kv: -kv[1])[1][0] -
                                        sorted(Counter(p % 12 for _, p in r["st"]["bass"][0]).items(), key=lambda kv: -kv[1])[0][0]
                                        for r in rows if "bass" in r["st"] and len({p % 12 for _, p in r["st"]["bass"][0]}) > 1}))
    # GR v2 §4.2 batch skeleton gate: same-skeleton pair = skel_sim >= 0.55 (duplicates always count)
    same_pairs = [(i, j) for (i, j), (sk, ss, exact) in sims.items() if exact or sk >= SAME_SKEL]
    in_pairs = sorted({k for pr in same_pairs for k in pr})
    gate_ok = len(same_pairs) <= GATE_MAX_PAIRS and len(in_pairs) <= GATE_MAX_ROLLS
    div["gate_same_skeleton_pairs"] = len(same_pairs); div["gate_rolls_in_pairs"] = len(in_pairs)
    div["gate_near_pairs_ge040"] = sum(1 for v in pair_sk if v >= 0.40)
    div["batch_skeleton_gate"] = ("PASS" if gate_ok else "FAIL") + f" (pairs {len(same_pairs)}/{GATE_MAX_PAIRS} max, rolls {len(in_pairs)}/{GATE_MAX_ROLLS} max)"
    for r in rows:
        r["soft_count"] = len({c.split(":")[0] for c in r["codes"] if c.split(":")[0] not in HARD} |
                              {c.split(":")[0] for c in r["soft"] if c.split(":")[0] not in INFO_SOFT})
        r["verdict"] = "FAIL" if any(c.split(":")[0] in HARD for c in r["codes"]) else "PASS"
        # solo = the roll on its own (ignores the batch-diversity codes); the ship gate uses `verdict`
        r["solo_verdict"] = "FAIL" if any(c.split(":")[0] in HARD and c.split(":")[0] not in BATCH for c in r["codes"]) else "PASS"
    # v2.1 SOFT-RECURRENCE GATE (Maestro, hard, batch-level): a soft code on > 40% of a batch's rolls (n >= 10) = systematic generator bias
    ADV = INFO_SOFT | {"REF_FLAG", "STAB_OUT_OF_KEY"}
    rec = Counter(x for r in rows for x in {c.split(":")[0] for c in r["soft"] + r["codes"] if c.split(":")[0] not in HARD} - ADV)
    offenders = [(k, v) for k, v in rec.most_common() if n >= REC_MIN_N and v > REC_MAX_SHARE * n]
    rec_ok = not offenders
    top = rec.most_common(1)
    div["soft_recurrence_gate"] = ("PASS" if rec_ok else "FAIL") + (
        " (" + ", ".join(f"{k} {v}/{n}={100*v/n:.0f}%" for k, v in offenders) + ")" if offenders else
        (f" (max {top[0][0]} {top[0][1]}/{n}={100*top[0][1]/n:.0f}%; limit >40%" + ("" if n >= REC_MIN_N else f"; not applied, n<{REC_MIN_N}") + ")" if top else " (no soft codes)"))
    div["advisory_rolls_with_4plus_soft"] = sum(1 for r in rows if len({c.split(":")[0] for c in r["soft"] + r["codes"] if c.split(":")[0] not in HARD} - ADV) >= 4)
    npass = sum(r["verdict"] == "PASS" for r in rows); rate = npass / n if n else 0
    ship = rate >= SHIP_RATE and gate_ok and rec_ok
    why = [] if ship else ([f"<80% pass"] if rate < SHIP_RATE else []) + ([] if gate_ok else ["batch skeleton gate FAIL"]) + \
        ([] if rec_ok else ["soft-recurrence gate FAIL: " + ", ".join(f"{k} {v}/{n}" for k, v in offenders)])
    summary = dict(style=style, rolls=n, passed=npass, pass_rate=round(rate, 3), solo_passed=sum(r["solo_verdict"] == "PASS" for r in rows),
                   gate_ok=gate_ok, rec_ok=rec_ok, hard_profile=HARD_PROFILE_NAME[0],
                   verdict=("SHIP-CANDIDATE (auto >=80% + skeleton gate + soft-recurrence gate; still needs the ear columns)" if ship else "NO-SHIP (" + ", ".join(why) + ")"),
                   code_counts=Counter(c.split(":")[0] for r in rows for c in r["codes"] if c.split(":")[0] in HARD),
                   soft_load_median=statistics.median([r["soft_count"] for r in rows]) if rows else 0, soft_counts=Counter(c.split(":")[0] for r in rows for c in r["soft"] + [c for c in r["codes"] if c.split(":")[0] not in HARD]),
                   diversity=div)
    if out is not False:
        out = out or os.path.join(d, "_score"); os.makedirs(out, exist_ok=True)
        write_outputs(rows, summary, out, style, S, d)
    return rows, summary

BATCH = {"DUPLICATE_OF_ROLL", "SAME_SKELETON_AS_ROLL"}
# GENERATOR_RULES.md §8 rubric: which machine codes are the proxy for each criterion (weight, codes)
RUBRIC = {
    "R1_groove":      (3, {"GRID_LOCKED", "NOTE_RUN", "DENSITY_HIGH", "BASS_SWUNG", "CONSTANT_DENSITY", "BASS_NOTES_TOO_SHORT",
                           "BASS_ORGAN_8THS", "BASS_16TH_ROLL", "OFFBEAT_PUMP_ONLY", "HAT_16THS", "SWING_90S", "KICK_NOT_4x4"}),
    "R2_hook":        (3, {"NO_BASS", "OUT_OF_KEY", "SCALE_RUN", "ROOT_DRONE", "TOP2_LOW", "TOO_MANY_PCS", "PCS_PER_BAR", "LEAP_TOO_BIG", "OCTAVE_PUMP", "BASS_REGISTER"}),
    "R3_space":       (2, {"EMPTY_BAR", "NO_GAPS", "NO_HOLES", "DENSITY_LOW", "BASS_NOTES_TOO_LONG", "BASS_REESE_LONG"}),
    "R4_kick_bass":   (2, {"BASS_ON_KICK", "KICK_NOT_4x4"}),
    "R5_phrase":      (2, {"LOOP_NO_REPEAT", "STATIC_LOOP", "CALL_ANSWER_MISSING"}),
    "R6_stab":        (2, {"STAB_ON_BEAT", "STAB_TOO_THICK", "STAB_CLASH", "PIANO_OFFBEAT_CHORDS"}),
    "R7_dialect_surprise": (2, {"LANE_FIT_LOW", "LATIN_PERC_BED", "BUILD_FX_IN_LOOP"}),  # GR v2: dialect fit + surprise (judges; SURPRISE_GROOVES)
    "R8_originality": (2, {"COPY_OF_REF", "BANNED_FIGURE"}),
    "R9_clean":       (1, {"TAIL_PAST_LOOP", "TAIL_INTO_BAR1", "STUCK_NOTE", "CLIPPING", "CLICKS"}),
    "variety":        (0, {"DUPLICATE_OF_ROLL", "SAME_SKELETON_AS_ROLL"}),
}
def rubric_proxy(codes, soft=()):
    """Per GR v2 §5: hard codes show as FAIL:CODE, every other code as soft:CODE (advisory; judges score by ear)."""
    out = {}
    base = [c.split(":")[0] for c in list(codes) + list(soft)]
    r16 = set().union(*(RUBRIC[x][1] for x in ("R1_groove", "R2_hook", "R3_space", "R4_kick_bass", "R5_phrase", "R6_stab")))
    for k, (_, cs) in RUBRIC.items():
        hit = sorted({c for c in base if c in cs or (k == "R4_kick_bass" and c == "ONKICK_HIGH") or (k == "R6_stab" and c == "STAB_OUT_OF_KEY")})
        if k == "R7_dialect_surprise": hit = sorted({c for c in base if c in cs or c in r16})
        hard = [c for c in hit if c in HARD]; sof = [c for c in hit if c not in HARD]
        out[k] = " ".join((["FAIL:" + "+".join(hard)] if hard else []) + (["soft:" + "+".join(sof)] if sof else [])) or "ok"
    return out
COLS = ["offkick_pct", "top2_pct", "root_pct", "roll_pcs", "pitches_per_bar", "len_med16", "notes_per_bar", "sounding_frac", "max_16th_run",
        "octave_move_pct", "bass_median_pitch", "bass_swing_pct", "rep1", "rep2", "rep4", "var_last4_pct", "var_backhalf_pct",
        "distinct_bass_bars", "stab_hits_per_bar", "stab_onbeat_pct", "stab_pcs_med", "stab_on_bass_attack_pct", "stab_in_bass_rest_pct",
        "distinct_stab_bars", "kick_quarter_cov", "hat_swing_pct", "bpm", "closest_roll",
        "onkick_pct", "inbeat_pos_ge10", "generic_bars", "npb_sd", "npb_range", "longest_rest16", "bar8_rest16", "bar8_vs_bar4_same",
        "max_leap", "step_run_moves", "lane_fit", "lane_miss", "lane_mode", "long_ge6_pct", "held_ge4_pct", "beats_eaa_pct", "beats_offbeat_only_pct",
        "dotted_share_pct", "b2_time_pct", "eighths_attacked_med", "stab_mode", "stab_ea_pct", "stab_dur_med16", "stab_signpost_bars_pct",
        "clap_2_4_bars_pct", "hat_offbeat_bars_pct", "closed_hat_per_bar", "gr_match1", "gr_match2", "gr_match4", "banned_2and4_bars", "banned_b3_tie_bars", "vel_layers",
        "key_tonic_pc", "bass_out_of_key_pct", "bass_out_nonneighbour_pct", "stab_out_of_key_pct", "stab_out_on_downbeat", "out_of_key_why", "empty_bars"]

def write_outputs(rows, summary, out, style, S, d):
    cp = os.path.join(out, f"scores_{style}.csv")
    with open(cp, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["roll", "verdict", "solo_verdict", "hard_codes", "soft_codes", "soft_count", "batch_skeleton_gate", "soft_recurrence_gate", "originality", "orig_worst_ref"] + COLS +
                   ["wav_peak_dbfs", "wav_clipped", "wav_duck_db", "wav_tail_vs_bar1_db", "wav_clicks", "bars", "bass_part", "stab_part", "kick_src",
                    ] + list(RUBRIC) + ["rubric_machine_gate", "judgeA_total_of_38", "judgeB_total_of_38",
                    "SURPRISE_GROOVES (Y/N, judges)", "MELAK_KEEP (Y/N, required, ear)", "composer_ear_note", "ar_ear_note"])
        for r in rows:
            w.writerow([r["roll"], r["verdict"], r["solo_verdict"], " ".join(c for c in r["codes"] if c.split(':')[0] in HARD), " ".join(r["soft"] + [c for c in r["codes"] if c.split(':')[0] not in HARD]),
                        r["soft_count"], summary["diversity"]["batch_skeleton_gate"], summary["diversity"]["soft_recurrence_gate"],
                        r["orig"], r["ohits"][0][1] if r["ohits"] else ""] + [r["m"].get(k, "") for k in COLS] +
                       [r["winfo"].get("peak_dbfs", ""), r["winfo"].get("clipped_samples", ""), r["winfo"].get("duck_db", ""), r["winfo"].get("tail_vs_bar1_db", ""),
                        len(r["winfo"].get("clicks_at_s", [])) if r["winfo"] else "", r["bars"], r["bass_part"], r["stab_part"], r["kick_src"]] +
                       list(rubric_proxy(r["codes"], r["soft"]).values()) + ["0 (machine FAIL: not auditioned)" if r["verdict"] == "FAIL" else "audition", "", "", "", "", "", ""])
    L = [f"# ROLLA roll scores: style `{style}`", f"*Input: `{d}`. Generated by `tools/score_rolls.py`. Sheet: `tools/ROLLA_ROLL_SCORING.md`.*", "",
         f"## Verdict: **{summary['verdict']}**: {summary['passed']}/{summary['rolls']} rolls pass the hard codes ({100*summary['pass_rate']:.0f}%; ship gate 80%). On their own (ignoring batch duplicates): {summary['solo_passed']}/{summary['rolls']}. "
         f"Batch skeleton gate: **{summary['diversity']['batch_skeleton_gate']}**. Soft-recurrence gate: **{summary['diversity']['soft_recurrence_gate']}**. Hard profile: `{summary['hard_profile']}` ({', '.join(sorted(HARD))}). "
         f"Median soft codes per roll: {summary['soft_load_median']} (advisory).", "",
         "> Auto-scoring covers the measurable rules only. **The MELAK_KEEP ear column (Composer + A&R, from Melak's taste) is required** "
         "before anything ships. A style ships only if it passes ≥80% here *and* ≥80% of the auto-passing rolls are marked keep by ear.", "",
         "## Batch diversity", "| metric | value |", "|---|---|"]
    for k, v in summary["diversity"].items(): L.append(f"| {k} | {v} |")
    npairs = sum(1 for r in rows for c in r["codes"] if c.split(":")[0] in BATCH)
    L += ["", "## GENERATOR_RULES §8 rubric hook-up",
          f"- Machine gate: {summary['passed']}/{summary['rolls']} rolls go to the judges; the other {summary['rolls']-summary['passed']} score 0 (GR §8 'any machine FAIL scores 0').",
          f"- Machine same-skeleton/duplicate pairs: {summary['diversity']['gate_same_skeleton_pairs']}, rolls involved {summary['diversity']['gate_rolls_in_pairs']} (GR v2 §4.2: batch FAILS if > 2 pairs or > 3 rolls).",
          "- Proxy columns show hard hits as `FAIL:` and everything else as `soft:` (GR v2 freedom: soft codes cost points by ear, they don't fail).",
          "- Judges also mark `SURPRISE_GROOVES` Y/N per roll (R7 = dialect fit + surprise); a set needs >= 4/20 Y.",
          "- Per-criterion proxy flags (R1-R9) are in the CSV. Judges fill `judgeA_total_of_38`, `judgeB_total_of_38` and `MELAK_KEEP`, then run "
          "`score_rolls.py --tally scores_<style>.csv` for: median >= 26, >= 14/20 at >= 22, >= 5/20 at >= 30, agreement, >= 4/20 SURPRISE_GROOVES, the batch gate, and the 80% gates.", ""]
    L += ["", "## Failure codes (hard) across the batch", "| code | rolls |", "|---|---|"]
    for k, v in summary["code_counts"].most_common():
        if k in HARD: L.append(f"| {k} | {v} |")
    L += ["", "Soft codes (GR v2: don't fail a roll; they're the freedom budget, read them): " + (", ".join(f"{k} ×{v}" for k, v in summary["soft_counts"].most_common()) or "none"), "",
          "## Per roll", "| roll | verdict | hard codes | soft | off-kick % | top-2 % | pcs | len med (16ths) | notes/bar | sounding | rep 1/2/4 | stab on-beat % | stab pcs | stab on bass attack % | orig | MELAK_KEEP (ear) |",
          "|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|"]
    for r in rows:
        m = r["m"]
        L.append(f"| {r['roll']} | **{r['verdict']}** | {' '.join(c for c in r['codes'] if c.split(':')[0] in HARD) or '-'} | {' '.join([c for c in r['codes'] if c.split(':')[0] not in HARD] + r['soft']) or '-'} | "
                 f"{m.get('offkick_pct','')} | {m.get('top2_pct','')} | {m.get('roll_pcs','')} | {m.get('len_med16','')} | {m.get('notes_per_bar','')} | "
                 f"{m.get('sounding_frac','')} | {m.get('rep1','')}/{m.get('rep2','')}/{m.get('rep4','')} | {m.get('stab_onbeat_pct','')} | {m.get('stab_pcs_med','')} | "
                 f"{m.get('stab_on_bass_attack_pct','')} | {r['orig']} | ☐ |")
    L += ["", "## Details (originality hits, WAV, MIDI technical)"]
    for r in rows:
        bits = []
        if r["ohits"]: bits.append("orig: " + "; ".join(f"{h[0]} {h[1]} ({h[2]} vs {h[3]}: {', '.join(h[4])})" for h in r["ohits"]))
        if r["excluded_refs"]: bits.append(f"self-match excluded: {', '.join(r['excluded_refs'])}")
        if r["winfo"]: bits.append("wav: " + ", ".join(f"{k}={v}" for k, v in r["winfo"].items()))
        ti = {k: v for k, v in r["tinfo"].items() if k != "end_beats"}
        if ti: bits.append("midi: " + ", ".join(f"{k}={v}" for k, v in ti.items()))
        bits.append(f"parts: bass=`{r['bass_part']}` stab=`{r['stab_part'] or '-'}` kick={r['kick_src']}, bars={r['bars']}")
        L.append(f"- **{r['roll']}**: " + " | ".join(bits))
    open(os.path.join(out, f"report_{style}.md"), "w").write("\n".join(L) + "\n")
    print(f"wrote {cp} and {os.path.join(out, f'report_{style}.md')}", file=sys.stderr)

def tally(csv_path):
    """Combine the machine verdict with the judges' 38-point scores and the MELAK_KEEP ear column (GR §8 + this sheet)."""
    rows = list(csv.DictReader(open(csv_path))); n = len(rows)
    num = lambda x: float(x) if str(x).strip() not in ("", "-") else None
    tot = []
    for r in rows:
        a, b = num(r.get("judgeA_total_of_38", "")), num(r.get("judgeB_total_of_38", ""))
        t = 0.0 if r["verdict"] == "FAIL" else (None if a is None or b is None else (a + b) / 2)
        tot.append((r["roll"], t, a, b, r.get("MELAK_KEEP (Y/N, required, ear)", "").strip().upper()))
    missing = [x[0] for x in tot if x[1] is None]
    if missing: print(f"incomplete: judge scores missing for {len(missing)} auditioned rolls: {', '.join(missing[:8])}"); return 2
    sc = sorted(x[1] for x in tot); med = statistics.median(sc)
    ge22, ge30 = sum(v >= 22 for v in sc), sum(v >= 30 for v in sc)
    disagree = sum(1 for x in tot if x[2] is not None and x[3] is not None and abs(x[2] - x[3]) > 8)
    machine = sum(r["verdict"] == "PASS" for r in rows)
    keep = sum(1 for r, x in zip(rows, tot) if r["verdict"] == "PASS" and x[4] == "Y")
    gate = (rows[0].get("batch_skeleton_gate", "") if rows else "")
    surpr = sum(1 for r in rows if r.get("SURPRISE_GROOVES (Y/N, judges)", "").strip().upper() == "Y")
    rg = (rows[0].get("soft_recurrence_gate", "") if rows else "")
    checks = [("batch skeleton gate (<= 2 pairs, <= 3 rolls)", gate.startswith("PASS"), gate),
              ("soft-recurrence gate (no soft code on > 40% of rolls, n >= 10)", rg.startswith("PASS") or rg == "", rg or "n/a (pre-v2.1 CSV)"),
              (f">= 4/20 SURPRISE_GROOVES = Y", surpr >= 4 * n / 20, f"{surpr}/{n}"),("median >= 26/38", med >= 26, f"{med:.1f}"), (f">= 14/{n} rolls >= 22", ge22 >= 14 * n / 20, ge22),
              (f">= 5/{n} keepers >= 30", ge30 >= 5 * n / 20, ge30),               ("machine pass >= 80%", machine >= SHIP_RATE * n, f"{machine}/{n}"),
              ("auto-pass AND MELAK_KEEP=Y >= 80% of rolls", keep >= SHIP_RATE * n, f"{keep}/{n}")]
    for name, ok, v in checks: print(f"{'PASS' if ok else 'FAIL'}  {name}: {v}")
    if disagree > 4: print(f"RECALIBRATE: judges differ by > 8 on {disagree} rolls (GR §8)")
    ok = all(c[1] for c in checks) and disagree <= 4
    print("SET VERDICT:", "SHIP to Melak" if ok else "NO-SHIP"); return 0 if ok else 1

def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--tally":
        sys.exit(tally(sys.argv[2]))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir"); ap.add_argument("--style", required=True, choices=STYLE_CHOICES)
    ap.add_argument("--out"); ap.add_argument("--bars", type=int); ap.add_argument("--bpm", type=float, default=130.0)
    ap.add_argument("--refs", default=OC.DEFAULT_REFS,
                    help="private reference MIDI dir (default: $ROLLA_REFS); required unless --no-orig")
    ap.add_argument("--no-orig", action="store_true")
    ap.add_argument("--hard-profile", default="maestro", choices=sorted(HARD_PROFILES),
                    help="maestro (default) = GR v2 four hard rules + LANE_FIT_LOW + duplicates; gr_v2 = also the GR v2 §5 technical list")
    ap.add_argument("--exclude-self", action="store_true", help="skip refs whose folder name (under --refs) prefixes the roll name (validation)")
    ap.add_argument("--swing-heavy", action="store_true", help="v2.2 optional JACKIN heavier-swing switch: jackin hat swing band 50-68 instead of 50-57")
    a = ap.parse_args()
    if not a.no_orig and not a.refs:
        ap.error("the originality check needs --refs DIR or ROLLA_REFS=DIR (private reference MIDI); or pass --no-orig")
    if a.swing_heavy: LANE["jackin"]["hat_sw"] = HEAVY_SWING_BAND
    HARD.clear(); HARD.update(HARD_PROFILES[a.hard_profile]); HARD_PROFILE_NAME[0] = a.hard_profile
    rows, s = score_dir(a.dir, a.style, a.out, a.bars, a.bpm, a.refs, a.no_orig, a.exclude_self)
    print(f"{s['style']}: {s['passed']}/{s['rolls']} pass ({100*s['pass_rate']:.0f}%) -> {s['verdict']}  [solo {s['solo_passed']}/{s['rolls']}]")
    sys.exit(0 if s["pass_rate"] >= SHIP_RATE and s["gate_ok"] and s["rec_ok"] else 1)

if __name__ == "__main__":
    main()
