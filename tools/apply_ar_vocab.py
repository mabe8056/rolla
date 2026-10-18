#!/usr/bin/env python3
"""Apply A&R's review to PATTERN_VOCAB.json and emit src/Generator/VocabCells.h.

DX3N stays (Melak named it). Fuse, Hottrax and Solid Grooves go to weight 0.
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VOCAB = ROOT / "research" / "genre_wide" / "PATTERN_VOCAB.json"
HEADER = ROOT / "src" / "Generator" / "VocabCells.h"

ZERO = {"UK_MIN": 0, "LONG": 0, "POP": 0, "GASKIN_90S": 0}
STYLES = ("UK_MIN", "LONG", "POP", "GASKIN_90S")


def cells(data):
    for lane, rows in data["cells"].items():
        for row in rows:
            yield lane, row


def by_id(data):
    return {row["id"]: row for _, row in cells(data)}


def set_w(row, **kw):
    for k, v in kw.items():
        row["style_w"][k] = v


def zero(row):
    row["style_w"] = dict(ZERO)
    row["cut"] = True


def drop_labels(row, banned):
    row["labels"] = [x for x in row.get("labels", []) if x not in banned]


def parse_bar(grid):
    attacks = []
    i = 0
    while i < len(grid):
        c = grid[i]
        if c in "xXo":
            j = i + 1
            while j < len(grid) and grid[j] == "=":
                j += 1
            attacks.append((i, j - i, c))
            i = j
        else:
            i += 1
    return attacks[:12]


def main():
    data = json.loads(VOCAB.read_text())
    data["version"] = "genre_wide_v2_ar"
    cid = by_id(data)

    uk = data["style_params"]["UK_MIN"]
    uk["bass_on_kick_max"] = 0.28
    uk["octave_share"] = [0, 0.25]
    uk["swing_hats"] = [50, 57]
    uk["swing_hats_default"] = 52
    uk["swing_groove"] = [50, 56]
    uk["swing_switch"] = [62, 66]
    uk["bass_swing"] = [48, 58]
    uk["stab_modes"] = {"dub": [0.5, 3], "riff": [4, 10], "riff_on_ea_min": 0.7}
    uk["stab_hits_per_bar"] = [0.5, 3]
    uk["sidechain"] = "heavy (17-38 dB stem dips)"

    long = data["style_params"]["LONG"]
    long["bass_on_kick_max"] = 0.28
    long["octave_share"] = [0, 0.25]
    long["swing_hats"] = [50, 57]
    long["swing_groove"] = [50, 56]
    long["sidechain"] = "heavy (17-30 dB)"
    long["stab_modes"] = uk["stab_modes"]

    pop = data["style_params"]["POP"]
    pop["bass_on_kick_max"] = 0.15
    pop["octave_share"] = [0, 0.25]
    pop["swing_hats"] = [50, 56]
    pop["swing_groove"] = [50, 56]
    pop["sidechain"] = "medium-heavy (12-25 dB)"
    pop["stab_modes"] = uk["stab_modes"]

    g = data["style_params"]["GASKIN_90S"]
    g["bass_on_kick_max"] = 0.35
    g["swing_hats"] = [50, 56]
    g["swing_clap"] = [50, 56]
    g["swing_groove"] = [50, 56]
    g["swing_switch"] = [62, 66]
    g.pop("two_step_option", None)
    g["two_step_switch"] = "62-66 only, not the 90s default"
    g["octave_share"] = [0, 0.25]
    g["sidechain"] = "light (3.6-8 dB)"

    # Grids and weights. Cut means the cell stays in the file at weight 0.
    cid["B_octave_bounce"]["grid"] = "--x--x----x--X--"
    cid["B_octave_bounce"]["use"] = "root on the and, one octave on the following a; no kick"
    set_w(cid["B_octave_bounce"], POP=0.5)
    cid["B_tresillo_off"]["grid"] = "-x-x--x--x-x--x-"
    cid["B_tresillo_off"]["use"] = "tresillo rotated by 3, off the kick"
    cid["B_sparse_space"]["grid"] = "------x------x--"
    cid["B_sparse_space"]["use"] = "two off-kick notes, reveal bars only"
    set_w(cid["B_sparse_space"], UK_MIN=0.4)
    cid["B_hold_lean"]["grid"] = "--x==-x==---x==-"
    cid["B_hold_lean"]["use"] = "2-3 sixteenth leans off the kick, ties across the beat"
    set_w(cid["B_hold_lean"], UK_MIN=0.7)
    cid["B_root_b7_halfbar"]["grid"] = "--x--x--x--x--x-"
    cid["B_root_b7_halfbar"]["use"] = "root then b7 over an off-kick grid, len 2-3; seed, run originality"
    cid["B_root_b7_halfbar"]["seed_only"] = True
    cid["B_dotted"]["grid"] = "--x--x--x--x-x-x"
    cid["B_dotted"]["use"] = "3-3-3-3-2-2 off the downbeat; one kick at most. Gaskin keeps the straight dotted grid in the generator."
    set_w(cid["B_dotted"], UK_MIN=0.5, LONG=0.5, GASKIN_90S=0.9)
    cid["B_dotted_offset"]["use"] = "dotted grid, one kick per bar at most (inside the 1-28% range)"
    set_w(cid["B_dotted_offset"], UK_MIN=0.5)
    set_w(cid["B_tresillo"], GASKIN_90S=0.4)
    cid["B_syncop8"]["seed_only"] = True
    cid["B_syncop8"]["use"] = "family seed only; shift or drop before emit, never the V Line grid verbatim"
    cid["B_mhigh_staccato"]["seed_only"] = True
    cid["B_mhigh_staccato"]["use"] = "family seed only; never M-High's slots and 1/5/1/8 contour verbatim"
    cid["ST_counter_riff"]["seed_only"] = True
    cid["ST_counter_riff"]["use"] = "family seed only; V Line rhythm must be mutated"
    set_w(cid["ST_rest"], UK_MIN=0.25)
    set_w(cid["ST_a4"], UK_MIN=0.9)
    set_w(cid["ST_offbeat8"], UK_MIN=0, GASKIN_90S=0.3)
    set_w(cid["ST_syncop_90s"], GASKIN_90S=0.5)
    cid["ST_rave_stab"]["grid"] = "---x-------x----"
    cid["ST_rave_stab"]["use"] = "moved off the beat"
    set_w(cid["ST_rave_stab"], UK_MIN=0.05)
    cid["ST_tresillo"]["grid"] = "---x--x---x-----"
    cid["ST_tresillo"]["use"] = "3-3-4 starting on the a, off the beat"
    cid["HT_reverse_in"]["grid"] = "x==============="
    cid["HT_reverse_in"]["use"] = "reverse sample, the hit is the start and it ends at the next downbeat"

    for cut_id in (
        "B_16_full", "B_roll3", "B_roll3_vel", "B_pre_kick_pair", "B_one_note_pedal",
        "B_reese_long", "B_organ_8ths", "B_poly_3", "B_off8_pump", "ST_piano_offbeat",
        "CH_16_accent", "SH_16_vel", "TAMB_16",
        "CLAVE_son32", "CLAVE_rumba", "CONGA_tumbao", "BONGO_run", "COWBELL_off",
        "SN_roll_bar8", "CRASH_phrase", "HT_sweep_up", "HT_downlifter", "HT_phrase_one",
    ):
        if cut_id in cid:
            zero(cid[cut_id])

    cid["B_ladder_down"]["fill_only"] = True
    cid["B_ladder_down"]["use"] = "bar 4/8 fill only, slots 11-16; breaks the two-pitch rule as a loop"
    set_w(cid["B_ladder_down"], UK_MIN=0.2, LONG=0.2, POP=0.2, GASKIN_90S=0.1)

    set_w(cid["K_2step"], GASKIN_90S=0.2)
    set_w(cid["K_four_pickup"], UK_MIN=0.2)
    set_w(cid["K_four_double"], UK_MIN=0.1)
    set_w(cid["CP_4_only"], UK_MIN=0.1, LONG=0.2)
    set_w(cid["OH_off8"], UK_MIN=0.85)
    set_w(cid["CH_syncop4"], UK_MIN=0.2)
    if "PD_rest" in cid:
        set_w(cid["PD_rest"], UK_MIN=0.5)
    for pid, uk_w, long_w in (("PL_7step", None, 0.3), ("PL_16_gate", None, 0.2)):
        if pid in cid and long_w is not None:
            set_w(cid[pid], LONG=long_w)
    if "PL_offbeat_8" in cid:
        set_w(cid["PL_offbeat_8"], POP=0.4)
    for hid in ("HT_sweep_up", "HT_downlifter", "HT_reverse_in", "HT_phrase_one"):
        if hid in cid and not cid[hid]["style_w"]["UK_MIN"] and hid != "HT_reverse_in":
            pass
        if hid in cid and hid != "HT_reverse_in":
            set_w(cid[hid], POP=0)

    if "ST_ea_riff" not in cid:
        data["cells"]["STAB"].append({
            "id": "ST_ea_riff",
            "grid": "-x-x-x-x-x-x----",
            "use": "lead stab riff, 6 hits on the e/a, at most one on a beat, 1-2 pitch classes",
            "style_w": {"UK_MIN": 0.5, "LONG": 0.3, "POP": 0.6, "GASKIN_90S": 0.2},
            "labels": ["YM", "UTS"],
            "src": ["AR-MHIGH"],
            "inference": False,
        })

    banned_tags = {"FUSE", "HOTTRAX", "F93", "SOLID", "LW"}
    for _, row in cells(data):
        drop_labels(row, banned_tags)
        if row.get("inference") is False and row["id"] in {
            "B_off8_pump", "B_16_full", "B_roll3", "B_roll3_vel", "B_pre_kick_pair",
            "SH_16_vel", "TAMB_16", "PERC_3_5_7_poly", "RIM_3step", "ST_syncop_90s",
            "B_hold_lean", "B_rest_bar",
        }:
            row["inference"] = True

    weights = data["label_style_weights"]
    weights["FUSE"] = dict(ZERO)
    weights["HOTTRAX"] = dict(ZERO)
    weights["F93"]["UK_MIN"] = 0.3
    weights["NERV"]["UK_MIN"] = 0.6
    weights["NERV"]["GASKIN_90S"] = 0.6
    weights["HC"]["UK_MIN"] = 0.5
    weights["SOLID"] = dict(ZERO)
    weights["LW"] = dict(ZERO)
    data["labels"]["FUSE"] = "Fuse London (rejected by Melak, weight 0)"
    data["labels"]["HOTTRAX"] = "Hottrax (rejected by Melak, weight 0)"
    data["labels"]["SOLID"] = "Solid Grooves (rejected by Melak, weight 0)"
    data["labels"]["DX3N"] = "DX3N Records (Max/Luke/Joss Dean; in the lane, Melak named it)"
    data["labels"].pop("LW", None)
    weights.pop("LW", None)

    for rule in data["hard_rules"]:
        if rule["id"] == "H2_lowend_clash":
            rule["rule"] = "bass on the kick stays at or under 28% (Gaskin 35%). Above that is a low-end clash and a hard reject."
        if rule["id"] == "H1_key":
            rule["rule"] = "pitched notes stay in the minor/dorian set; Gaskin may add b2. Anything else is the wrong key."
    for rule in data["soft_rules"]:
        if rule["id"] == "S_stab_in_bass_holes":
            rule["rule"] = "per-style preference, not a fail. References put about 25% of stabs in rests and about 50% on bass attacks."
            rule["weight"] = 0.5
        if rule["id"] == "S_tail_variation":
            rule["rule"] = "1-4 events in slots 9-16, favour 13-16. A back-half tendency, not the last 2-4 sixteenths only."
            rule["weight"] = 0.8
        if rule["id"] == "S_two_pitch_core":
            rule["rule"] = "top-2 pitch time preferred at or above 63% (floor 58)"
        if rule["id"] == "S_space":
            rule["rule"] = "prefer at least 25% silent bass slots; LIGHTLEAK shows a 3% floor"
        if rule["id"] == "S_octave_style":
            rule["rule"] = "octave moves 0-25%, either none or about 15-25%"
        if rule["id"] == "S_off_kick_bass":
            rule["rule"] = "bass onsets prefer e/and/a; up to 28% may sit on the kick"
    for op in data["operators"]:
        if op["id"] == "OP_swing_lane":
            op["desc"] = "groove swing 50-56, hat swing 50-57 default 52, clap up to 63. A 62-66 switch is the Ellia/2-step option, not Gaskin."
        if op["id"] == "OP_tail_mutate":
            op["desc"] = "change 1-4 events in slots 9-16, favour 13-16"
            op["style_w"] = {s: 0.8 for s in STYLES}
        if op["id"] == "OP_callresp_swap":
            op["desc"] = "optional: move a stab toward a bass rest. Not required."
            op["style_w"] = {s: 0.5 for s in STYLES}
    for tmpl in data["templates"]:
        if tmpl["id"] == "T8_1bar_mutate":
            set_w(tmpl, UK_MIN=0.4)
        if tmpl["id"] == "T8_4bar_cycle":
            set_w(tmpl, UK_MIN=0.8)

    data["ar_notes"] = [
        "Applied AR_REVIEW 2026-10-17. DX3N kept. Fuse, Hottrax and Solid Grooves weight 0.",
        "Hard rejects are only wrong key, kick/bass clash above the cap, empty bars, and originality.",
    ]
    VOCAB.write_text(json.dumps(data, indent=1) + "\n")
    write_header(data)
    print("patched", VOCAB)
    print("wrote", HEADER)


def write_header(data):
    rows = []
    lane_of = {"BASS": 0, "STAB": 1}
    for lane, row in cells(data):
        if lane not in lane_of:
            continue
        if all(row["style_w"][s] <= 0 for s in STYLES):
            continue
        grids = row["grid"] if isinstance(row["grid"], list) else [row["grid"]]
        for bi, grid in enumerate(grids[:1]):
            attacks = parse_bar(grid)
            if not attacks:
                continue
            shape = 0
            if row["id"] == "B_root_b7_halfbar":
                shape = 1
            if row["id"] == "B_octave_bounce":
                shape = 2
            slots, lens, ivs, vels = [], [], [], []
            octave_used = False
            for slot, six, ch in attacks:
                iv = 0
                if shape == 1 and slot >= 8:
                    iv = 10
                if shape == 2 and ch == "X" and not octave_used:
                    iv = 12
                    octave_used = True
                slots.append(slot)
                lens.append(max(1, six * 2))
                ivs.append(iv)
                vels.append(80 if ch == "o" else 112)
            rows.append((row["id"] if bi == 0 else row["id"] + f"_{bi}", lane_of[lane],
                         1 if row.get("seed_only") else 0,
                         1 if row.get("fill_only") else 0,
                         shape, slots, lens, ivs, vels,
                         [row["style_w"][s] for s in STYLES]))

    lines = [
        "// Generated by tools/apply_ar_vocab.py from PATTERN_VOCAB.json. Do not edit.",
        "#pragma once",
        "#include <cstdint>",
        "namespace rolla { namespace vocab {",
        "struct Cell {",
        "    const char* id;",
        "    int lane;",
        "    int n;",
        "    int seedOnly;",
        "    int fillOnly;",
        "    int shape;",
        "    int slot[12];",
        "    int len2[12];",
        "    int iv[12];",
        "    int vel[12];",
        "    float w[4];",
        "};",
        "inline constexpr Cell kCells[] = {",
    ]
    for ident, lane, seed, fill, shape, slots, lens, ivs, vels, w in rows:
        def pad(xs, cast):
            xs = list(xs) + [0] * (12 - len(xs))
            return ", ".join(str(cast(x)) for x in xs[:12])
        lines.append(
            "    { \"%s\", %d, %d, %d, %d, %d, { %s }, { %s }, { %s }, { %s }, { %s, %s, %s, %s } },"
            % (ident, lane, len(slots), seed, fill, shape,
               pad(slots, int), pad(lens, int), pad(ivs, int), pad(vels, int),
               *(f"{float(x):.2f}f" for x in w))
        )
    lines.append("};")
    lines.append("inline constexpr int kCellN = %d;" % len(rows))
    lines.append("} }")
    lines.append("")
    HEADER.write_text("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
