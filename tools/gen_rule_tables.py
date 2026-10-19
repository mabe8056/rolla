#!/usr/bin/env python3
"""Writes src/Generator/RuleTables.h from resources/genre/genre_writing_model.json.

Every measured number the rule composer samples from comes from the model file
(pooled, smoothed statistics; no phrases, no per-source tables). The composer's
structural rules (retry counts, the turn rest, drum placement weights) are not
measurements; they sit in COMPOSER_RULES below and are written verbatim.

Deterministic: the same JSON gives the same header byte for byte.

  python3 tools/gen_rule_tables.py            # write the header
  python3 tools/gen_rule_tables.py --check    # exit 1 if the header is stale
"""
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL = os.path.join(ROOT, "resources", "genre", "genre_writing_model.json")
HEADER = os.path.join(ROOT, "src", "Generator", "RuleTables.h")

STYLES = ["ROLLER", "DEEP", "BOUNCY", "JACKIN"]  # Dialect order: UkMin, Long, Pop, Jackin
MODES = ["Dorian", "Aeolian", "Phrygian"]       # phrase::Mode order
PALETTE = ["pickup_into_bar1", "drop_out_last_beat", "fill_up_16ths", "pitch_answer",
           "octave_lift", "rhythmic_displacement", "truncation"]
MAX_VOICINGS = 12


def f(x):
    s = ("%.4f" % float(x)).rstrip("0")
    if s.endswith("."):
        s += "0"
    return s + "f"


def arr(v):
    return "{ " + ", ".join(f(x) for x in v) + " }"


def per_style(name, values, ctype="float", comment=""):
    vals = ", ".join(f(v) if ctype == "float" else str(v) for v in values)
    c = f" // {comment}" if comment else ""
    return f"inline constexpr {ctype} {name}[kStyles] = {{ {vals} }};{c}\n"


def nested(name, dims, rows_by_style, indent="    "):
    out = f"inline constexpr float {name}[kStyles]{''.join('[%d]' % d for d in dims)} = {{\n"
    for s, rows in zip(STYLES, rows_by_style):
        if len(dims) == 1:
            out += f"{indent}{arr(rows)}, // {s}\n"
        else:
            out += f"{indent}{{ // {s}\n"
            for r in rows:
                out += f"{indent}    {arr(r)},\n"
            out += f"{indent}}},\n"
    return out + "};\n"


COMPOSER_RULES = r"""
// ============================================================ composer rules
// Structural rules of the composer, not measurements (kept from the rule
// composer and the A&R gate fixes). Edit them here in tools/gen_rule_tables.py.

// Share of a sampled distribution replaced by a uniform draw over the legal
// choices, on top of the model's entropy floors.
inline constexpr float kUniformMix = 0.06f;

// Longest run of attacks on adjacent 16ths (scorer F4: run <= 7, eaa <= 20%).
inline constexpr int kMaxAdjacentRun = 2;
// The last bar rests from this 16th on (the turn; the bar-8 pickup goes there later).
inline constexpr int kTurnRestFrom = 12;
// Compositions tried before the best one ships (guards: kick, entropy, tonic, thin cell).
inline constexpr int kComposeRetries = 8;
// Bar counts are drawn inside [lo, hi] notes a bar.
inline constexpr int kBarCountLo = 2;
inline constexpr int kBarCountHi = 8;

// Bar 2 against bar 1 (the call): keep the call's front half with this P and
// redraw the back half; JACKIN instead draws bar 2 into the call's silence so
// the 1-bar repeat stays under its cap.
inline constexpr float kAnswerKeepFront[kStyles] = { 0.50f, 0.60f, 0.55f, 0.0f };
inline constexpr float kAnswerCallSlotWeight[kStyles] = { 0.45f, 0.50f, 0.50f, 0.12f };
inline constexpr float kAnswerSilentBoost[kStyles] = { 1.0f, 1.0f, 1.0f, 1.8f };
// P(bar 5 or 6 restates with a back-half mutation).
inline constexpr float kBackHalfVariation[kStyles] = { 0.55f, 0.65f, 0.60f, 0.40f };
// P(the call's first onset is the root): an anchor for the tonic guard.
inline constexpr float kFirstRoot[kStyles] = { 0.65f, 0.70f, 0.80f, 0.55f };
// DARK tilt: bright boosts the 5th, dark the b3 and the 2nd / b2.
inline constexpr float kBrightFifth = 3.0f;
inline constexpr float kDarkThird = 2.5f;
inline constexpr float kDarkSecond = 2.0f;
// DARK tilt on the mode weights: dorian x (1.6 - 1.2 dark), phrygian x (0.4 + 1.2 dark).
inline constexpr float kModeDarkTilt = 1.2f;
// Minimum slots changed by the bar-4 answer.
inline constexpr int kAnswerMinChanged = 3;
// Multiplier on a stab slot right after a bass phrase end (the answer).
inline constexpr float kStabAnswerBoost = 3.0f;

// Density shape of bars 5-7 against bars 1-3, as a tag (no stored shape).
inline constexpr int kArcShapes = 4;
inline constexpr const char* kArcNames[kArcShapes] = { "arc_flat", "arc_build", "arc_dip", "arc_swell" };

// Stab placement shares per bar against the bass: in a gap, over a sustain, on
// an attack. The batch median of the gap share is the model's soft target
// (gap_soft_target_pct); slot choice inside a class follows the model's
// p_onset_given_bass tables. The on-attack share (kStabOnsetShare, generated
// below) is the model's own: stabs land on a bass note when the references do,
// and the composer keeps them consonant with it.
inline constexpr float kStabGapShare[kStyles] = { 0.18f, 0.32f, 0.20f, 0.29f };
// Stab in-beat class weights for the HITS lane (e, and, a); beats are kick steps.
inline constexpr float kStabSlotClass[3] = { 0.30f, 0.45f, 0.25f };
inline constexpr float kAnswerTailFactor = 1.2f;
// Stab register band, semitones over the lane's export base.
inline constexpr int kStabRegLo[kStyles] = { 0, 0, 3, 0 };
inline constexpr int kStabRegHi[kStyles] = { 19, 17, 19, 19 };
inline constexpr float kStabOctaveMove[kStyles] = { 0.15f, 0.10f, 0.15f, 0.12f };

// Drums (placement rules for the hat / perc / rim / ghost passes).
inline constexpr float kHatOffbeatProb[kStyles] = { 0.85f, 0.75f, 0.95f, 0.90f };
inline constexpr float kOpenHatTail = 0.85f;
inline constexpr float kOpenHatOther = 0.05f;
inline constexpr float kPercEaWeight = 1.0f;
inline constexpr float kPercAndWeight = 2.0f;
inline constexpr float kRimAndWeight = 2.5f;
inline constexpr float kRimEaWeight = 1.0f;
inline constexpr float kSnareGhostBar = 0.5f;
"""


def build(M):
    S = [M["styles"][s] for s in STYLES]
    ac = M["anti_copy"]
    ef = ac["entropy_floor"]
    g = M["global_rules"]
    o = []
    o.append("#pragma once\n\n")
    o.append("// GENERATED by tools/gen_rule_tables.py from resources/genre/genre_writing_model.json\n")
    o.append(f"// (model version {M['version']}). Do not edit by hand: change the model or the script\n")
    o.append("// and run `python3 tools/gen_rule_tables.py`.\n//\n")
    o.append("// Pooled, smoothed statistics only: first-order degree tables, per-16th onset\n")
    o.append("// weights, density curves, conditional stab-vs-bass weights and a chord-quality\n")
    o.append("// palette. No row, cell or value is a note sequence, rhythm cell or voicing of\n")
    o.append("// any one source. Style order is the Dialect enum: ROLLER (ukmin), DEEP (long),\n")
    o.append("// BOUNCY (pop), JACKIN (jackin).\n\n")
    o.append("namespace rolla::rules::tables\n{\n\n")
    o.append("inline constexpr int kStyles = 4;\n")
    o.append(f"inline constexpr const char* kModelVersion = \"{M['version']}\";\n\n")

    o.append("// ================================================================ anti-copy\n")
    o.append(f"inline constexpr float kCellCeiling = {f(ac['transition_cell_ceiling'])};\n")
    o.append(f"inline constexpr float kEntropyMixStep = {f(ac['entropy_mix_step'])};\n")
    o.append(f"inline constexpr float kFloorOnsetBass = {f(ef['onset_grid']['bass'])};\n")
    o.append(f"inline constexpr float kFloorOnsetStab = {f(ef['onset_grid']['stab'])};\n")
    o.append(f"inline constexpr float kFloorRowBass = {f(ef['transition_row']['bass'])};\n")
    o.append(f"inline constexpr float kFloorRowStab = {f(ef['transition_row']['stab'])};\n")
    o.append(f"inline constexpr float kFloorIntervalBass = {f(ef['interval_dist']['bass'])};\n")
    o.append(f"inline constexpr float kOutputOnsetMargin = {f(ac['output_onset_entropy_margin'])};\n\n")

    o.append("// ============================================================== global rules\n")
    o.append(f"inline constexpr int kStabHitsCap = {g['stab_hits_per_bar_cap']};\n")
    o.append(f"inline constexpr float kBassKickHardCap = {f(g['bass_on_kick_hard_cap_pct'] / 100.0)};\n")
    o.append(f"inline constexpr float kPhrygianB2Cap = {f(g['phrygian_b2_bass_time_max_pct'] / 100.0)};\n")
    o.append(f"inline constexpr float kPickupProb = {f(g['pickup']['prob_per_roll'])};\n")
    o.append(f"inline constexpr float kPickupMinShare = {f(g['pickup']['min_batch_share'])};\n")
    v = g["variation"]
    o.append(f"inline constexpr float kBackHalfShareMin = {f(v['back_half_mutation_share_min'])};\n")
    o.append(f"inline constexpr int kDensityRangeMin = {v['density_range_min']};\n")
    o.append(f"inline constexpr float kTwinJaccardMax = {f(g['batch']['twin_jaccard_max'])};\n")
    t = g["thin_cell_guard"]
    o.append(f"inline constexpr int kThinBarMaxAttacks = {t['max_attacks']};\n")
    o.append(f"inline constexpr int kThinBarsToFail = {t['max_bars']};\n")
    o.append(f"inline constexpr int kThinBarMinPcs = {t['min_pitch_classes_per_bar']};\n")
    o.append(f"inline constexpr float kHatSwingLo = {f(g['hat_swing_pct_band'][0] / 100.0)};\n")
    o.append(f"inline constexpr float kHatSwingHi = {f(g['hat_swing_pct_band'][1] / 100.0)};\n\n")

    o.append("// =================================================================== modes\n")
    o.append("// Mode weights in phrase::Mode order (dorian, aeolian, phrygian); every mode is\n")
    o.append("// inside the scorer's scale for the style.\n")
    o.append(nested("kModeWeights", [3], [[s["modes"]["weights"][m] for m in MODES] for s in S]))
    o.append(per_style("kB2Cap", [s["modes"]["b2_bass_time_max_pct"] / 100.0 for s in S], comment="b2 share of bass time outside phrygian"))
    o.append(per_style("kTop2Min", [s["modes"]["tonic_guard"]["top2_min"] for s in S], comment="tonic + next pitch class share of bass time"))
    o.append("\n// ================================================================== phrase\n")
    o.append(per_style("kBar4AnswerProb", [s["phrase"]["bar4_answer_prob"] for s in S]))
    o.append(per_style("kBar8AnswerProb", [s["phrase"]["bar8_answer_prob"] for s in S]))
    o.append("inline constexpr int kPaletteMoves = 7;\n")
    o.append("enum PaletteMove : int { PalPickup = 0, PalDropOut, PalFill, PalPitchAnswer, PalOctaveLift, PalDisplace, PalTruncate };\n")
    o.append(nested("kBar8Palette", [7], [[s["phrase"]["bar8_palette"].get(k, 0.0) for k in PALETTE] for s in S]))

    o.append("\n// ==================================================================== bass\n")
    o.append("// Onset slot weights per bar group: 0 body, 1 bar 4 (answer), 2 bar 8 (turn).\n")
    o.append(nested("kBassOnset", [3, 16], [[s["bass"]["onset_prob"][k] for k in ("body", "bar4", "bar8")] for s in S]))
    o.append(per_style("kBassDensityMedian", [s["bass"]["density_median"] for s in S], comment="notes per bar"))
    o.append("// Notes per bar for bars 1-8: { mean, sd }.\n")
    o.append(nested("kBassDensity", [8, 2], [[[b["mean"], b["sd"]] for b in s["bass"]["density_curve"]] for s in S]))
    o.append(nested("kBassUnigram", [12], [s["bass"]["degree_unigram"] for s in S]))
    o.append("// Row = current degree (pitch class above the tonic), column = next.\n")
    o.append(nested("kBassTrans", [12, 12], [s["bass"]["transitions"] for s in S]))
    o.append(nested("kBassInterval", [27], [s["bass"]["interval_dist"] for s in S]))
    o.append("// Degree weight by metric strength of the slot: 0 beat, 1 8th offbeat, 2 e / a.\n")
    o.append(nested("kBassChoice", [3, 12], [[s["bass"]["note_choice"][k] for k in ("strong", "medium", "weak")] for s in S]))
    ca = [s["bass"]["call_answer"] for s in S]
    o.append("// Density change (-4..+4 notes) of the answers, and the degree an answer bar ends on.\n")
    o.append(nested("kBassDeltaBar4", [9], [c["density_delta_bar4_vs_bar2_dist"] for c in ca]))
    o.append(nested("kBassDeltaBar8", [9], [c["density_delta_bar8_vs_bar4_dist"] for c in ca]))
    o.append(nested("kBassEndBar4", [12], [c["ending_degree_bar4_dist"] for c in ca]))
    o.append(nested("kBassEndBar8", [12], [c["ending_degree_bar8_dist"] for c in ca]))
    o.append(per_style("kRepeatM1Max", [s["bass"]["repeat"]["m1_max"] for s in S]))
    o.append(per_style("kRepeatM2Min", [s["bass"]["repeat"]["m2_min"] for s in S]))
    o.append(per_style("kKickTarget", [s["bass"]["kick"]["target_pct"] / 100.0 for s in S], comment="share of bass attacks on kick steps"))
    snd = [s["bass"]["sounding"] for s in S]
    o.append(per_style("kGapFraction", [x["gap_fraction"] for x in snd], comment="note length = this x the gap to the next onset"))
    o.append(per_style("kSoundingLo", [x["band"][0] for x in snd]))
    o.append(per_style("kSoundingHi", [x["band"][1] for x in snd]))
    o.append(per_style("kLenMedLo", [x["len_median_16ths"][0] for x in snd]))
    o.append(per_style("kLenMedHi", [x["len_median_16ths"][1] for x in snd]))
    o.append(per_style("kLongGe6Max", [x["long_ge6_max_share"] if x["long_ge6_max_share"] is not None else 1.0 for x in snd],
                       comment="share of notes >= 6 16ths (1 = no limit)"))

    deep = M["styles"]["DEEP"]["submodes"]
    pop = M["styles"]["BOUNCY"]["submodes"]
    o.append("\n// Submodes. DEEP dark / sparse; BOUNCY bounce / pulse (pulse is a batch quota).\n")
    o.append(f"inline constexpr float kDeepDarkWeight = {f(deep['dark']['weight'])};\n")
    for nm, d in (("Dark", deep["dark"]), ("Sparse", deep["sparse"])):
        o.append(f"inline constexpr float kDeep{nm}Npb = {f(d['notes_per_bar'])};\n")
        o.append(f"inline constexpr float kDeep{nm}Gap = {f(d['gap_fraction'])};\n")
        o.append(f"inline constexpr float kDeep{nm}LenLo = {f(d['len_median_16ths'][0])};\n")
        o.append(f"inline constexpr float kDeep{nm}LenHi = {f(d['len_median_16ths'][1])};\n")
    b, p = pop["bounce"], pop["pulse"]
    o.append(f"inline constexpr float kBounceNpb = {f(b['notes_per_bar'])};\n")
    o.append(f"inline constexpr float kBounceLenLo = {f(b['len_16ths'][0])};\n")
    o.append(f"inline constexpr float kBounceLenHi = {f(b['len_16ths'][1])};\n")
    o.append(f"inline constexpr float kPulseQuota = {f(p['quota'])};\n")
    o.append(f"inline constexpr float kPulseShareLo = {f(p['batch_share'][0])};\n")
    o.append(f"inline constexpr float kPulseShareHi = {f(p['batch_share'][1])};\n")
    o.append(f"inline constexpr float kPulseNpb = {f(p['notes_per_bar'])};\n")
    o.append(f"inline constexpr float kPulseGap = {f(p['gap_fraction'])};\n")
    o.append(f"inline constexpr float kPulseLenLo = {f(p['len_median_16ths'][0])};\n")
    o.append(f"inline constexpr float kPulseLenHi = {f(p['len_median_16ths'][1])};\n")

    o.append("\n// ==================================================================== stab\n")
    o.append("// P(stab onset) per slot by the bass state there: 0 rest (gap), 1 held, 2 attack.\n")
    o.append(nested("kStabGiven", [3, 16], [[s["stab"]["p_onset_given_bass"][k] for k in ("rest", "sus", "onset")] for s in S]))
    # Share of stab hits on a bass attack: the p_onset_given_bass mass per class,
    # weighted by a typical bar's bass occupancy (3 rest / 8 sustain / 5 attack 16ths).
    occ = {"rest": 3 / 16, "sus": 8 / 16, "onset": 5 / 16}
    shares = []
    for st in S:
        pg = st["stab"]["p_onset_given_bass"]
        w = {k: sum(pg[k]) / 16 * occ[k] for k in occ}
        shares.append(round(w["onset"] / sum(w.values()), 3))
    o.append("inline constexpr float kStabOnsetShare[kStyles] = { " + ", ".join(f"{x}f" for x in shares) + " };\n")
    o.append(f"inline constexpr float kStabGapWeight = {f(S[0]['stab']['gap_weight'])};\n")
    o.append(f"inline constexpr float kStabGapTarget = {f(S[0]['stab']['gap_soft_target_pct'] / 100.0)};\n")
    o.append(per_style("kStabOnBeatCeil", [s["stab"]["on_beat_ceiling_pct"] / 100.0 for s in S]))
    o.append(nested("kStabUnigram", [12], [s["stab"]["degree_unigram"] for s in S]))
    o.append("// Chord-bottom chain: row = current bottom degree, column = next.\n")
    o.append(nested("kStabTrans", [12, 12], [s["stab"]["transitions"] for s in S]))
    o.append(nested("kStabChoice", [3, 12], [[s["stab"]["note_choice"][k] for k in ("strong", "medium", "weak")] for s in S]))
    o.append(nested("kConsonance", [12], [s["stab"]["consonance_ic_dist"] for s in S]))
    o.append("// After a bass phrase end: index 0 = no answer, k = first hit k 16ths later.\n")
    o.append(nested("kStabAnswer", [5], [s["stab"]["answer_after_bass_phrase_end_dist"] for s in S]))
    o.append("\n// Chord-quality palette: interval sets above the chord bottom, weights.\n")
    o.append("struct Voicing\n{\n    int n;\n    int iv[4];\n    float w;\n};\n")
    o.append(f"inline constexpr int kMaxVoicings = {MAX_VOICINGS};\n")
    counts = []
    rows = []
    for s in S:
        vs = s["stab"]["voicings"][:MAX_VOICINGS]
        counts.append(len(vs))
        row = []
        for vv in vs:
            iv = list(vv["intervals"])[:4]
            row.append("{ %d, { %s }, %s }" % (len(iv), ", ".join(str(x) for x in iv + [0] * (4 - len(iv))), f(vv["w"])))
        while len(row) < MAX_VOICINGS:
            row.append("{ 0, { 0, 0, 0, 0 }, 0.0f }")
        rows.append(row)
    o.append("inline constexpr int kVoicingCount[kStyles] = { " + ", ".join(str(c) for c in counts) + " };\n")
    o.append("inline constexpr Voicing kStabVoicings[kStyles][kMaxVoicings] = {\n")
    for s, row in zip(STYLES, rows):
        o.append(f"    {{ // {s}\n")
        for i in range(0, len(row), 3):
            o.append("      " + ", ".join(row[i:i + 3]) + ",\n")
        o.append("    },\n")
    o.append("};\n")

    o.append("\n// ================================================================== groove\n")
    o.append(per_style("kHatSwingTarget", [s["drums"]["hat_swing_pct"]["target"] / 100.0 for s in S], comment="16th swing, inside [kHatSwingLo, kHatSwingHi]"))
    o.append(COMPOSER_RULES)
    o.append("\n} // namespace rolla::rules::tables\n")
    return "".join(o)


def main():
    M = json.load(open(MODEL))
    text = build(M)
    if "--check" in sys.argv:
        old = open(HEADER).read() if os.path.exists(HEADER) else ""
        if old != text:
            print("RuleTables.h is stale: run python3 tools/gen_rule_tables.py")
            return 1
        print("RuleTables.h matches the model")
        return 0
    with open(HEADER, "w") as fh:
        fh.write(text)
    print("wrote", HEADER)
    return 0


if __name__ == "__main__":
    sys.exit(main())
