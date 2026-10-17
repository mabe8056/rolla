#!/usr/bin/env python3
"""Score a batch of rolls against GENERATOR_RULES.md §6 and §7.2.

    python3 tools/score_rolls/__main__.py batch.jsonl --refs /tmp/rolla_refs/transcriptions

Input is JSON lines from tools/dump_grooves.cpp. One object per roll:
style, key, bars, tag (plan|contour|cells), lanes, drums.

AV01–AV17 are soft flags. They are reported, and they do not reject a roll.
The merge gate is the v2.1 listening set (80% per style), not these checks.
Diversity metrics run once per style when the batch has at least the configured
minimum. ESTIMATE thresholds are read from docs/generator/estimates.json.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import math
import os
import statistics
import sys
import tempfile
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ESTIMATES = json.loads((ROOT / "docs" / "generator" / "estimates.json").read_text())
AV = ESTIMATES["avoid"]
DIV = ESTIMATES["diversity"]

# §5 presets. Ranges are inclusive. on_kick / stab_beat / oct_max are ceilings.
PRESET = {
    "ukmin": dict(npb=(4.5, 7.5), len16=(1.4, 2.2), on_kick=0.10, root=(0.35, 0.76),
                  top2=(0.63, 0.98), oct_max=0.10, sounding=(0.50, 0.85),
                  match2=0.60, match4=0.77, stab_pb=(0.5, 3.0), stab_beat=0.10,
                  plans={"P1": 0.35, "P2": 0.25, "P3": 0.15, "P5": 0.25},
                  cells=("beat1_signature", "hold_lean", "syncop_pair", "answer_tag", "rest"),
                  contours=("pedal", "neighbour", "ladder_down", "fifth_pop", "step_up")),
    "long": dict(npb=(6.5, 8.0), len16=(2.0, 2.4), on_kick=0.30, root=(0.30, 0.45),
                 top2=(0.75, 0.85), oct_max=0.0, sounding=(0.75, 0.90),
                 match2=0.75, match4=0.78, stab_pb=(2.5, 4.5), stab_beat=0.15,
                 plans={"P1": 0.40, "P2": 0.30, "P3": 0.30},
                 cells=("hold_lean", "answer_tag", "pedal", "rest"),
                 contours=("pedal", "neighbour", "ladder_down")),
    "pop": dict(npb=(4.5, 6.0), len16=(1.0, 1.25), on_kick=0.05, root=(0.30, 0.42),
                top2=(0.94, 0.99), oct=(0.15, 0.30), sounding=(0.28, 0.40),
                match2=0.50, match4=0.75, stab_pb=(9.0, 11.0), stab_beat=0.20,
                plans={"P3": 0.60, "P2": 0.40},
                cells=("off_8th_pump", "fifth_pop", "rest"),
                contours=("fifth_pop", "pedal", "step_up")),
    "gaskin": dict(npb=(6.0, 8.0), len16=(1.9, 2.3), on_kick=0.35, root=(0.28, 0.40),
                   top2=(0.55, 0.70), oct_max=0.05, sounding=(0.75, 0.85),
                   match1_max=0.20, match2=0.85, stab_pb=(5.5, 6.5), stab_beat=0.20,
                   plans={"P4": 0.80, "P2": 0.20},
                   cells=("dotted_grid", "call_answer_2bar"),
                   contours=("call_answer_2bar", "neighbour")),
}

GENERIC = [
    frozenset(range(16)),
    frozenset(range(0, 16, 2)),
    frozenset(range(1, 16, 2)),
    frozenset(range(0, 16, 4)),
    frozenset(range(2, 16, 4)),
    frozenset(),
]
KICK = frozenset({0, 4, 8, 12})
HAT_VOICES = {6, 7}  # HatC, HatO


def degree(midi, key):
    return (int(midi) - int(key)) % 12


def quant(start):
    return int(round(float(start)))


def covers(note, slot):
    a = float(note["start"])
    b = a + float(note["len"])
    return a < slot + 1 - 1e-4 and b > slot + 1e-4


def grid(notes, nslots):
    g = [None] * nslots
    for note in notes:
        pitch = note["pitches"][0] if note.get("pitches") else None
        for s in range(nslots):
            if covers(note, s) and g[s] is None:
                g[s] = pitch
    return g


def onsets(notes):
    out = []
    for note in notes:
        s = quant(note["start"])
        pitch = note["pitches"][0] if note.get("pitches") else 0
        out.append((s, pitch, int(note.get("vel", 100)), float(note["len"]), bool(note.get("glide"))))
    out.sort()
    return out


def match_at(g, lag_bars):
    lag = lag_bars * 16
    if lag <= 0 or lag >= len(g):
        return 1.0
    same = 0
    n = len(g) - lag
    for i in range(n):
        if g[i] == g[i + lag]:
            same += 1
    return same / n if n else 1.0


def pstdev(xs):
    if len(xs) < 2:
        return 0.0
    return statistics.pstdev(xs)


def median(xs):
    return statistics.median(xs) if xs else 0.0


def parse_tag(tag):
    parts = (tag or "").split("|")
    plan = parts[0] if parts and parts[0].startswith("P") else ""
    contour = parts[1] if len(parts) > 1 else ""
    cells = [c for c in (parts[2].split(",") if len(parts) > 2 else []) if c]
    return plan, contour, cells


def bar_slots(onset_steps, bar):
    return frozenset(s % 16 for s, *_ in onset_steps if s // 16 == bar)


def score_roll(roll, preset):
    """Return (fails, flags, metrics)."""
    fails, flags = [], []
    key = int(roll.get("key", 2))
    bars = int(roll.get("bars", 8))
    nslots = bars * 16
    bass = roll["lanes"].get("bass", [])
    stab = roll["lanes"].get("stab", [])
    hits = roll["lanes"].get("hits", [])
    pad = roll["lanes"].get("pad", [])
    g = grid(bass, nslots)
    ev = onsets(bass)
    steps = [e[0] for e in ev]
    degs = [degree(e[1], key) for e in ev]
    vels = [e[2] for e in ev]
    lens = [e[3] for e in ev]

    per_bar = [sum(1 for s in steps if s // 16 == b) for b in range(bars)]
    npb = sum(per_bar) / bars if bars else 0
    occupied = sum(1 for x in g if x is not None)
    sounding = occupied / nslots if nslots else 0
    silent = 1 - sounding

    # intervals between successive onsets
    ivs = []
    for i in range(len(ev) - 1):
        ivs.append(ev[i + 1][1] - ev[i][1])

    pcs = [degree(p, key) for p in (x for x in g if x is not None)]
    pc_count = Counter(pcs)
    top2 = 0.0
    root_share = 0.0
    if pc_count:
        ranked = pc_count.most_common()
        root_share = pc_count.get(0, 0) / len(pcs)
        top2 = sum(c for _, c in ranked[:2]) / len(pcs)
        root_in_top2 = 0 in {pc for pc, _ in ranked[:2]}
    else:
        root_in_top2 = False
    distinct_pcs = len({degree(e[1], key) for e in ev})
    distinct_per_bar = []
    for b in range(bars):
        distinct_per_bar.append(len({e[1] for e in ev if e[0] // 16 == b}))

    on_kick = sum(1 for s in steps if s % 16 in KICK) / len(steps) if steps else 0
    len_med = median(lens)

    # AV01 theory
    run = best = 0
    prev_sign = 0
    for iv in ivs:
        if iv != 0 and abs(iv) <= 2 and (prev_sign == 0 or (iv > 0) == (prev_sign > 0)):
            run += 1
            prev_sign = 1 if iv > 0 else -1
            best = max(best, run)
        else:
            run = 0
            prev_sign = 0
    if best > AV["same_direction_steps_fail"]:
        fails.append("AV01_theory_generic")
    elif best > AV["same_direction_steps_flag"]:
        flags.append("AV01_theory_generic")

    # AV02 grid locked
    pos = Counter(s % 4 for s in steps)
    spread = sum(1 for k in range(4) if steps and pos[k] / len(steps) >= 0.10)
    generic_bar = False
    one_vel = len(set(vels)) <= 1 and len(vels) > 0
    for b in range(bars):
        if bar_slots(ev, b) in GENERIC:
            generic_bar = True
    if spread < ESTIMATES["bass"]["in_beat_spread_min_positions"] or generic_bar or one_vel:
        fails.append("AV02_grid_locked")

    # AV04 density
    if pstdev(per_bar) < AV["constant_density_sd"] or (max(per_bar) - min(per_bar) < AV["constant_density_range"] if per_bar else True):
        fails.append("AV04_constant_density")

    # AV05 holes
    rest_run = best_rest = 0
    for x in g:
        if x is None:
            rest_run += 1
            best_rest = max(best_rest, rest_run)
        else:
            rest_run = 0
    bar8 = g[(bars - 1) * 16: bars * 16] if bars else []
    end_hole = len(bar8) >= 4 and all(x is None for x in bar8[-4:])
    if best_rest < 4 or silent < ESTIMATES["bass"]["silent_slot_fraction_min"] or not end_hole:
        fails.append("AV05_no_holes")

    # AV06 drone
    low_npb_root = bool(per_bar) and min(per_bar) < 3 and root_share > 0.99
    if root_share > AV["root_share_max"] or distinct_pcs < 2 or low_npb_root:
        fails.append("AV06_root_only_drone")

    # AV07 wandering
    leap = max((abs(iv) for iv in ivs), default=0)
    b2_bad = False
    for i, deg in enumerate(degs):
        if deg != 1:
            continue
        ok = False
        for j in (i - 1, i + 1):
            if 0 <= j < len(degs) and degs[j] == 0 and abs(ev[i][1] - ev[j][1]) == 1:
                ok = True
        if not ok:
            b2_bad = True
    if top2 < AV["top2_min"] or (distinct_per_bar and max(distinct_per_bar) > AV["distinct_pitches_per_bar_max"]) or leap > AV["leap_max"] or not root_in_top2 or b2_bad:
        fails.append("AV07_wandering_pitch")

    # AV08 identical
    m1 = match_at(g, 1)
    m2 = match_at(g, 2)
    m4 = match_at(g, 4)
    bar4 = g[3 * 16: 4 * 16] if bars >= 8 else []
    bar8g = g[7 * 16: 8 * 16] if bars >= 8 else []
    if m1 > AV["identical_match1"] or (bar4 and bar4 == bar8g) or m4 > AV["identical_match4"]:
        fails.append("AV08_identical_bars")

    # AV09 random
    if m2 < AV["too_random_match2"] and m4 < AV["too_random_match4"]:
        fails.append("AV09_too_random")

    # AV15 machine gun
    run16 = best16 = 1 if steps else 0
    for a, b in zip(steps, steps[1:]):
        if b == a + 1:
            run16 += 1
            best16 = max(best16, run16)
        elif b != a:
            run16 = 1
    if best16 > AV["machinegun_run"] or npb > AV["machinegun_npb"] or len_med < AV["machinegun_len_median"]:
        fails.append("AV15_machinegun")

    # AV16 on kick
    cap = min(float(preset["on_kick"]), float(AV["on_kick_hard_cap"]))
    if on_kick > cap + 1e-9:
        fails.append("AV16_on_kick_bass")

    # AV14 banned figures
    root_24 = 0
    tie = 0
    for b in range(bars):
        at = {s % 16: degree(p, key) for s, p, *_ in ev if s // 16 == b}
        if at.get(1) == 0 and at.get(3) == 0:
            root_24 += 1
        for s, p, vel, ln, glide in ev:
            if s // 16 == b and s % 16 == 14 and degree(p, key) == 3 and s + ln > (b + 1) * 16 + 1e-3:
                tie += 1
    if root_24 > 4 or tie > 4:
        fails.append("AV14_banned_figures")

    # AV10 stab / hits collision
    bass_on = {s for s in steps}
    stab_ev = onsets(stab)
    stab_steps = [s for s, *_ in stab_ev]
    same = sum(1 for s in stab_steps if s in bass_on)
    if roll["style"] == "ukmin":
        collide = same > 0
    else:
        collide = bool(stab_steps) and same / len(stab_steps) > AV["stab_overlap_others_max"]
    close = False
    for s in stab_steps:
        for b in bass_on:
            gap = s - b
            if 0 < gap < ESTIMATES["stab"]["min_gap_after_bass_onset_16ths"]:
                close = True
    hit_ev = onsets(hits)
    other = set(bass_on) | set(stab_steps)
    for lane_name in ("pluck", "pad", "vox"):
        other |= {s for s, *_ in onsets(roll["lanes"].get(lane_name, []))}
    hit_bad = False
    for s, *_ in hit_ev:
        if s % 16 in KICK or s in other:
            hit_bad = True
    if collide or close or hit_bad:
        fails.append("AV10_stab_bass_collision")

    # AV11 on-beat stabs
    if stab_steps:
        beat = sum(1 for s in stab_steps if s % 16 in KICK) / len(stab_steps)
        if beat > min(float(preset["stab_beat"]), float(AV["stab_on_beat_hard_cap"])):
            fails.append("AV11_on_beat_stabs")
    else:
        beat = 0.0

    # AV12 tails
    tail = False
    for lane in roll["lanes"].values():
        for note in lane:
            if float(note["start"]) + float(note["len"]) > nslots + 1e-3:
                tail = True
    if tail:
        fails.append("AV12_tail_wrap")

    # AV17 pad under the drop
    pad_g = grid(pad, nslots)
    pad_sound = sum(1 for x in pad_g if x is not None) / nslots if nslots else 0
    if pad_sound > AV["pad_sounding_max"]:
        fails.append("AV17_pad_under_drop")

    # AV03 robotic hats (flag)
    hat_vels = [h["vel"] for h in roll.get("drums", []) if h["voice"] in HAT_VOICES]
    hat_sd = pstdev(hat_vels) if len(hat_vels) > 1 else 0.0
    if hat_sd < AV["hat_vel_sd_min"]:
        flags.append("AV03_robotic_timing")

    buckets = Counter()
    for ln in lens:
        if ln < 1.25:
            buckets["<1.25"] += 1
        elif ln < 2.5:
            buckets["1.25-2.5"] += 1
        else:
            buckets[">=2.5"] += 1

    oct_jumps = sum(1 for iv in ivs if abs(iv) == 12) / len(ivs) if ivs else 0.0
    metrics = dict(npb=npb, len_med=len_med, on_kick=on_kick, root=root_share, top2=top2,
                   sounding=sounding, match1=m1, match2=m2, match4=m4, oct=oct_jumps,
                   stab_pb=(len(stab_steps) / bars if bars else 0), stab_beat=beat,
                   per_bar=per_bar, degrees=degs, onset_mask=0, bar1=bar_slots(ev, 0))
    mask = 0
    for s in steps:
        if 0 <= s < 128:
            mask |= 1 << s
    metrics["onset_mask"] = mask
    # AVOID is a listening note, not a reject. v2.1 at 80% per style is the gate.
    kept = []
    for code in fails:
        if code.startswith("AV"):
            flags.append(code)
        else:
            kept.append(code)
    return kept, flags, metrics


def jaccard(a, b):
    uni = (a | b).bit_count()
    if uni == 0:
        return 0.0
    return (a & b).bit_count() / uni


def entropy(counts):
    total = sum(counts.values())
    if total <= 0:
        return 0.0
    h = 0.0
    for c in counts.values():
        if c:
            p = c / total
            h -= p * math.log2(p)
    return h


def coverage(values, lo, hi):
    if hi <= lo or len(values) < 5:
        return 0.0
    xs = sorted(values)
    def pct(p):
        k = (len(xs) - 1) * p
        i = int(k)
        f = k - i
        if i + 1 >= len(xs):
            return xs[-1]
        return xs[i] * (1 - f) + xs[i + 1] * f
    a, b = pct(0.10), pct(0.90)
    overlap = max(0.0, min(b, hi) - max(a, lo))
    return overlap / (hi - lo)


def diversity(rolls, metrics, preset):
    out = {"n": len(rolls), "ok": True, "fails": []}
    if len(rolls) < DIV["batch_min"]:
        out["ok"] = False
        out["fails"].append(f"batch {len(rolls)} < {DIV['batch_min']}")
        return out
    masks = [m["onset_mask"] for m in metrics]
    degs = [tuple(m["degrees"]) for m in metrics]
    pair_j = []
    near = 0
    pairs = 0
    # Full pair scan is fine at a few thousand rolls.
    for i in range(len(masks)):
        ai, di = masks[i], degs[i]
        for j in range(i + 1, len(masks)):
            jac = jaccard(ai, masks[j])
            pair_j.append(jac)
            pairs += 1
            if jac >= DIV["near_duplicate_jaccard"] and di == degs[j]:
                near += 1
    pair_j.sort()
    mean_j = sum(pair_j) / len(pair_j) if pair_j else 0
    p90 = pair_j[int(0.90 * (len(pair_j) - 1))] if pair_j else 0
    out["onset_jaccard_mean"] = round(mean_j, 3)
    out["onset_jaccard_p90"] = round(p90, 3)
    out["near_duplicate_rate"] = round(near / pairs, 4) if pairs else 0
    if mean_j > DIV["onset_jaccard_mean_max"]:
        out["fails"].append("onset_jaccard_mean")
    if p90 > DIV["onset_jaccard_p90_max"]:
        out["fails"].append("onset_jaccard_p90")
    if out["near_duplicate_rate"] > DIV["near_duplicate_rate_max"]:
        out["fails"].append("near_duplicate_rate")

    bar1 = {m["bar1"] for m in metrics}
    share = len(bar1) / len(metrics)
    out["unique_bar1_share"] = round(share, 3)
    if share < DIV["unique_bar1_onset_sets_min_share"]:
        out["fails"].append("unique_bar1")

    grams = set()
    for deg in degs:
        for i in range(len(deg) - 3):
            grams.add(deg[i:i + 4])
    per_k = grams.__len__() * (1000 / len(metrics))
    out["degree_4grams_per_1000"] = int(per_k)
    if per_k < DIV["unique_degree_4grams_per_1000"]:
        out["fails"].append("degree_4grams")

    plans, contours, cells = Counter(), Counter(), Counter()
    tagged = 0
    for roll in rolls:
        plan, contour, cell = parse_tag(roll.get("tag", ""))
        if plan:
            tagged += 1
            plans[plan] += 1
        if contour:
            contours[contour] += 1
        for c in cell:
            cells[c] += 1
    out["tagged_share"] = round(tagged / len(rolls), 3)
    if tagged < len(rolls) * 0.5:
        out["fails"].append("plans_untagged")
    else:
        for plan, weight in preset["plans"].items():
            share_p = plans[plan] / len(rolls)
            if share_p < DIV["plan_weight_share_min"] * weight:
                out["fails"].append(f"plan_{plan}")
        for cell in preset["cells"]:
            if cells[cell] / len(rolls) < DIV["cell_type_min_share"]:
                out["fails"].append(f"cell_{cell}")
        for contour in preset["contours"]:
            if contours[contour] / len(rolls) < DIV["contour_min_share"]:
                out["fails"].append(f"contour_{contour}")
    out["plans"] = dict(plans)
    out["cells"] = dict(cells)
    out["contours"] = dict(contours)

    hist = Counter(int(round(m["npb"])) for m in metrics)
    bits = entropy(hist)
    out["npb_entropy_bits"] = round(bits, 2)
    if bits < DIV["npb_entropy_bits_min"]:
        out["fails"].append("npb_entropy")

    ranges = {
        "npb": preset["npb"],
        "len_med": preset["len16"],
        "root": preset["root"],
        "top2": preset["top2"],
        "sounding": preset["sounding"],
        "stab_pb": preset["stab_pb"],
    }
    cov = {}
    for name, (lo, hi) in ranges.items():
        cov[name] = round(coverage([m[name] for m in metrics], lo, hi), 2)
        if cov[name] < DIV["range_coverage_min"]:
            out["fails"].append(f"spread_{name}")
    out["range_coverage"] = cov
    out["ok"] = not out["fails"]
    return out


def load_originality():
    path = ROOT / "tools" / "originality_check.py"
    spec = importlib.util.spec_from_file_location("originality_check", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def roll_to_midi(roll, path):
    import mido
    mid = mido.MidiFile(ticks_per_beat=480)
    track = mido.MidiTrack()
    mid.tracks.append(track)
    track.append(mido.MetaMessage("set_tempo", tempo=mido.bpm2tempo(130), time=0))
    events = []
    channel = {"bass": 0, "stab": 1, "hits": 2, "pluck": 3, "pad": 4, "vox": 5}
    for name, ch in channel.items():
        for note in roll["lanes"].get(name, []):
            on = int(round(float(note["start"]) * 120))
            off = int(round((float(note["start"]) + float(note["len"])) * 120))
            if off <= on:
                off = on + 30
            for pitch in note.get("pitches") or [60]:
                events.append((on, 1, ch, int(pitch), int(note.get("vel", 100))))
                events.append((off, 0, ch, int(pitch), 0))
    events.sort()
    last = 0
    for tick, kind, ch, pitch, vel in events:
        delta = max(0, tick - last)
        last = tick
        if kind:
            track.append(mido.Message("note_on", channel=ch, note=max(0, min(127, pitch)), velocity=max(1, vel), time=delta))
        else:
            track.append(mido.Message("note_off", channel=ch, note=max(0, min(127, pitch)), velocity=0, time=delta))
    mid.save(path)


def originality_fails(rolls, refs, mod, limit):
    """AV13 on the first `limit` rolls. Returns list of (index, verdict)."""
    out = []
    if not refs or mod is None:
        return None
    sample = rolls[:limit]
    for roll in sample:
        fd, name = tempfile.mkstemp(suffix=".mid")
        os.close(fd)
        try:
            roll_to_midi(roll, name)
            rep = mod.run(name, refs)
        except Exception as exc:
            out.append((roll.get("index"), f"ERR {exc}"))
            continue
        finally:
            try:
                os.remove(name)
            except OSError:
                pass
        verdict = rep.get("verdict", "PASS")
        out.append((roll.get("index"), verdict))
    return out


def summarise(style, rolls, refs, mod, originality_n):
    preset = PRESET[style]
    rows = []
    fail_c, flag_c = Counter(), Counter()
    passed = 0
    for roll in rolls:
        fails, flags, metrics = score_roll(roll, preset)
        rows.append((fails, flags, metrics))
        if not fails:
            passed += 1
        for f in fails:
            fail_c[f] += 1
        for f in flags:
            flag_c[f] += 1
    oc = originality_fails(rolls, refs, mod, originality_n) if originality_n else None
    oc_fail = 0
    if oc:
        for idx, verdict in oc:
            if verdict == "FAIL":
                oc_fail += 1
                flag_c["AV13_reference_skeleton_copy"] += 1
            elif verdict == "FLAG":
                flag_c["AV13_reference_skeleton_copy"] += 1
    div = diversity(rolls, [m for _, _, m in rows], preset)
    return {
        "style": style,
        "n": len(rolls),
        "pass": passed,
        "pass_rate": round(passed / len(rolls), 3) if rolls else 0,
        "fails": fail_c.most_common(),
        "flags": flag_c.most_common(),
        "diversity": div,
        "originality_n": 0 if oc is None else len(oc),
        "originality_fail": oc_fail,
    }


def main():
    ap = argparse.ArgumentParser(description="Score rolls against GENERATOR_RULES §6 and §7.2")
    ap.add_argument("batch")
    ap.add_argument("--refs", default="")
    ap.add_argument("--label", default="batch")
    ap.add_argument("--originality", type=int, default=20)
    ap.add_argument("--out", default="")
    args = ap.parse_args()

    by = defaultdict(list)
    with open(args.batch) as fh:
        for line in fh:
            line = line.strip()
            if not line:
                continue
            roll = json.loads(line)
            by[roll["style"]].append(roll)
    mod = None
    if args.refs and args.originality:
        try:
            mod = load_originality()
        except Exception as exc:
            print(f"originality unavailable: {exc}", file=sys.stderr)
    reports = []
    for style in ("ukmin", "long", "pop", "gaskin"):
        if style not in by:
            continue
        print(f"scoring {args.label} {style} ({len(by[style])})", file=sys.stderr)
        reports.append(summarise(style, by[style], args.refs, mod, args.originality))

    lines = [f"# {args.label}", ""]
    for rep in reports:
        lines.append(f"## {rep['style']}  {rep['pass']}/{rep['n']} pass ({rep['pass_rate']:.0%})")
        if rep["fails"]:
            lines.append("fails: " + ", ".join(f"{k} {v}" for k, v in rep["fails"]))
        else:
            lines.append("fails: none")
        if rep["flags"]:
            lines.append("flags: " + ", ".join(f"{k} {v}" for k, v in rep["flags"][:8]))
        div = rep["diversity"]
        lines.append(
            "diversity: "
            + ("PASS" if div.get("ok") else "FAIL")
            + f" jaccard mean {div.get('onset_jaccard_mean', '—')} p90 {div.get('onset_jaccard_p90', '—')}"
            + f" near-dup {div.get('near_duplicate_rate', '—')}"
            + f" bar1 {div.get('unique_bar1_share', '—')}"
            + f" 4grams/1k {div.get('degree_4grams_per_1000', '—')}"
            + f" npb H {div.get('npb_entropy_bits', '—')}"
        )
        if div.get("fails"):
            lines.append("diversity misses: " + ", ".join(div["fails"][:24]))
        if div.get("range_coverage"):
            lines.append("range coverage: " + ", ".join(f"{k} {v}" for k, v in div["range_coverage"].items()))
        if rep["originality_n"]:
            lines.append(f"AV13 on {rep['originality_n']} rolls: {rep['originality_fail']} FAIL")
        lines.append("")
    text = "\n".join(lines)
    print(text)
    if args.out:
        Path(args.out).write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
