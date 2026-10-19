# GENERATOR_RULES.md v2: four hard rules, everything else a weighted tendency

Written by the Composer on 2026-10-17 ET. v1 is kept in `GENERATOR_RULES_v1.md`; its measured data and sources still hold.

## Changelog (v1 → v2, 2026-10-17 ~5 PM ET)

**Restructure.**
- Melak called v1 "really strict". Following Maestro's instruction, only **4 HARD rules** remain (§1).
  1. Key/scale.
  2. Kick/bass low-end clash.
  3. Broken or empty bars.
  4. The originality gate. Banned reference figures live here, because they're copy guards.
- **Every other rule is now a SOFT tendency** (§2):
  - Each one has a target band, a tolerance, a weight and a penalty curve.
  - Soft rules rank and sample rolls. They never reject one.
- A global **freedom** control (0–1) widens the tolerances, lowers the weights, and deliberately samples some rule-breaks (§2.3).

**A&R calibration** (`tools/ROLLA_ROLL_SCORING.md` §5.1, `tools/score_rolls.py`). Each band now covers the measured reference range.

| Item | v1 | v2 |
|---|---|---|
| Bass on the kick, UK MIN | max 10% | soft target ≤ 28%; HARD fail above 35% |
| Stab on the beat | ≤ 10–20% | soft ≤ 30% in every dialect |
| Stab density | one band | two modes: dub 0.5–3 hits/bar, riff 4–10 |
| "Stab never on a bass attack" | rule | dropped; at most a small UK MIN taste bonus for stabs in bass rests (refs put 24–75% of stab hits on bass attacks) |
| Consecutive 16ths | 3 | band ≤ 7 |
| Rest of 4+ sixteenths | required | soft (refs 1–37); "≥ 1 silent 16th" is part of the hard broken-bar rule |
| Leap cap | 15 | band ≤ 19 |
| Octave share | UK MIN 10% | band 0–26% (REF_R2 16–21%) |
| 4-bar repeat floor | 0.77 | band ≥ 0.47 |
| 2-bar repeat floor | — | band ≥ 0.50 |
| LONG sounding fraction | — | band 0.75–0.975 (REF_D2 0.93–0.97) |
| Notes per bar | — | band 3.9–8.7 |

**Diversity.**
- The mean onset-Jaccard target is replaced by a **same-skeleton-pairs batch gate** (§4.2).
- A "skeleton" is now defined exactly, using A&R's measure.

**Kept as agreed, now soft with high stickiness:** grid-locked, constant density, root drone, identical bars, too random, Jackin 1-bar repeat ≤ 0.20. Banned figures are kept and moved into HARD rule 4.

**Rubric.**
- Same R1–R9 and the same weights (38 points), so totals compare roll by roll with A&R's sheet.
- Machine flags pull a score down only through hard codes.
- R7 becomes "dialect fit + surprise".
- New surprise-that-grooves tags with a set-level quota (§5).

**Sources**, as in v1:
- MD = private measured-drop table (16 windows)
- CTR = private cross-track rules (CTR)
- SA = private style analysis
- PB = `rulebook/house-master-playbook.md`
- DES = `plugin-design/DESIGN.md`
- OC = `tools/originality_check.py`
- CM = `writing/COMPOSER_METHOD.md`
- DA = Direction A
- **ARS = A&R's scorer measurements, `tools/ROLLA_ROLL_SCORING.md` §2/§5.1 (17 drop sections, 72 MIDI)**
- ARV = (private A&R notes)

ESTIMATE means not measured.

---
## 0. Conventions (unchanged from v1)
- 16th grid with slots 1–16. Kick slots are 1, 5, 9 and 13. in_beat = ((slot−1) % 4) + 1, meaning beat / e / and / a.
- **match@L** = the fraction of 16th slots whose sounding pitch, or rest, equals the slot L bars later.
- **on-kick** = a bass attack within 0.3 sixteenth of a kick (ARS definition).
- Loops are 8 bars at 124–131 BPM.

---
## 1. HARD rules: the only reasons a roll is rejected

```yaml
hard:
  H1_key_scale:
    scale_sets:            # degrees relative to the roll's tonic
      UK_MIN: [1, b2, 2, b3, 4, 5, b6, 6, b7]    # aeolian + dorian 6 + phrygian b2 [MD: every UK window's bass time sits >= 95.6% inside this set]
      LONG:   [1, b2, 2, b3, 4, 5, b6, 6, b7]
      POP:    [1, b2, 2, b3, 4, 5, b6, 6, b7]
      JACKIN_90S: [1, b2, 2, b3, 4, 5, b6, b7]   # CTR D (phrygian colour, b2 = 17-26% [MD])
    checks:
      - "bass: time share on degrees outside scale_set <= 5%"       # MD max 4.4% (REF_R3 b49: 7 + 3 + b5); one 1/16 passing note per 2 bars = 3.1%
      - "every out-of-set bass note is a chromatic neighbour (|interval| = 1 to an in-set note before or after)"   # SA habit 4
      - "tonic is one of the two most-sounding bass pitch classes"  # CTR E (root always in top-2), MD 16/16 windows
      - "stab/pad/pluck: pitch classes outside scale_set <= 5% of note-ons; never on a downbeat hit"   # ESTIMATE (no measured outliers)
      - "all lanes share one tonic; no key change inside the loop"  # SA (all refs one-chord vamps)
  H2_low_end_clash:
    checks:
      - "bass on-kick attacks <= 35% of bass attacks (all dialects)"     # ARS hard (refs 1.2-31.6%); GR v1 AV16 cap
      - "bass median MIDI pitch in [20, 45]"                           # ARS BASS_REGISTER (refs A0-A2)
      - "no other pitched lane below MIDI 36 (C2) while the bass sounds" # ESTIMATE (stab/pad floors 45 [DA]; leaves room)
  H3_broken_or_empty_bars:
    checks:
      - "bass lane has >= 4 notes and >= 1 onset or sounding note in every bar"  # ARS NO_BASS; empty bar = broken
      - "the loop has >= 1 silent 16th in the bass AND bass sounding fraction <= 0.975"   # ARS NO_GAPS/NO_HOLES (refs 0.36-0.97)
      - "no stuck notes, no note-off after loop end, no zero/negative-length notes, no overlapping same-pitch notes"   # ARS TAIL_PAST_LOOP, STUCK_NOTE; CM render rule
      - "render: no clipping (>= 0.999), no clicks, no tail into bar 1 (<= -20 dB rel. and <= -40 dBFS if bar 1 starts silent)"   # ARS §2 Technical
      - "every lane that is switched on has >= 1 event in the 8 bars"  # ESTIMATE
  H4_originality:
    checks:
      - "OC vs every transcription: no FAIL (skel_win >= 0.55, skel_ngram >= 0.45, voicing >= 0.75, or int_ngram >= 0.60 AND rhythm_cov >= 0.70)"   # OC TH; ARS downgrades skeleton-only FAIL vs LOW-confidence CQT streams to FLAG
      - "Composer metric: NOT (onset Jaccard >= 0.50 OR degree-seq sim >= 0.70) and NOT both >= 0.40 / 0.60"   # CM §5
      - "banned figures: root on slots 2 AND 4 in > 4 of 8 bars; b3 on slot 15 tied over the barline in > 4 of 8 bars"   # CM 5.7, DES (REF_R5 2&4 root figure)
      - "triviality guard: streams < 6 onsets or < 2 pcs are exempt from OC"   # OC
```

Freedom never touches §1, and nothing outside §1 rejects a single roll. The only other gate is the batch-level diversity gate (§4.2), which flags the later roll of each same-skeleton pair. It doesn't judge any roll's musical quality.

---
## 2. SOFT rules: tendencies, penalties and freedom

### 2.1 Rule schema and penalty curve

```yaml
soft_rule_schema:
  id: str
  metric: str                 # same metric names as score_rolls.py CSV columns where one exists
  target: [lo, hi]            # band = measured reference range (penalty 0 inside)
  tol: float                  # distance outside the band at which the penalty reaches 1 x weight
  w: float                    # weight (1 = normal, 2 = strong, 0.5 = light)
  stick: float                # 0..1, how much freedom is ignored (1 = freedom can't relax it)
  bonus: optional             # negative penalty for a taste feature (never required)
penalty:
  d: "max(0, lo - x, x - hi) / tol_eff"
  curve: "c(d) = d^2 for d <= 1; 2d - 1 for d > 1; capped at 4"   # smooth near the band, linear far out
  p_rule: "w_eff * c(d)"
  P_roll: "sum over rules of p_rule (minus bonuses), reported per R1-R9 bucket"
```

### 2.2 Freedom: f in [0, 1]

Freedom is a global knob. The defaults are **f = 0.35** for normal rolls and **0.6** for "wild" (both ESTIMATE). Freedom does three things:

```yaml
freedom:
  tolerance:  "tol_eff = tol * (1 + 2f)"                        # f=1 triples every tolerance
  weight:     "w_eff  = w * (1 - f * (1 - stick))"               # stick=0.2 rules lose 80% weight at f=1; stick=0.7 lose 30%
  deliberate_breaks:
    k: "round(3f)"            # at f=0.35 -> 1 rule, f=0.6 -> 2, f=1 -> 3
    how: "pick k soft rules with stick < 0.5 (uniform), move their sampling target to lo - u*tol or hi + u*tol, u ~ U(0.3, 1.5); log them in roll.meta.broken_rules"
    exempt: "§1 hard rules, rules with stick >= 0.5"
  selection:
    method: "draw K = 6 candidates per output; pick one with softmax(-P_roll / T)"   # never a threshold reject
    T: "1 + 4f"               # f=0 -> almost always the lowest-penalty candidate; f=1 -> close to uniform
  calibration_checks:
    - "f = 0: every reference drop scores P_roll <= 1.0 (bands are set from refs, so most rules score 0)"   # check with MD/ARS refs
    - "f = 0.35: >= 25% of a 1000-roll batch has >= 1 rule outside its band"   # ESTIMATE; rule-breaking must actually come through
    - "f = 1: no hard-rule pass rate drop > 10 points vs f = 0"   # ESTIMATE; freedom must not just make broken rolls
```

**Stickiness classes.**
- **Sticky (0.6–0.7):** the agreed groove anchors. These are grid-locked, constant density, root drone, identical bars, too random, Jackin call/answer, and on-kick inside the soft zone.
- **Normal (0.3):** pitch and density shape.
- **Loose (0.1):** surface habits such as variation position, the bar-8 rest, velocity layers and hat swing.

### 2.3 Soft rules: phrase and global

```yaml
soft_global:
  - {id: S_match2,      metric: gr_match2, target: [0.50, 0.93], tol: 0.10, w: 1.5, stick: 0.6}   # ARS refs 0.536-0.863 (REF_J1/J2 0.92)
  - {id: S_match4,      metric: gr_match4, target: [0.47, 0.95], tol: 0.10, w: 1.0, stick: 0.3}   # ARS refs 0.469-0.917 (v1 floor 0.77 dropped)
  - {id: S_too_random,  metric: "match2<0.50 AND match4<0.47", target: false, tol: 0.10, w: 3, stick: 0.7}   # ARS AV09 (agreed); d = distance of the closer of the two below its floor
  - {id: S_identical_bars, metric: gr_match1, target: [0.0, 0.95], tol: 0.03, w: 3, stick: 0.7, also: "distinct_bass_bars >= 2"}   # ARS refs <= 0.75 (agreed)
  - {id: S_jackin_call_answer, dialect: JACKIN_90S, metric: gr_match1, target: [0.0, 0.20], tol: 0.15, w: 2, stick: 0.6}   # ARS/MD 0.008/0.161 (agreed, soft; only H3 if a bar is empty)
  - {id: S_bar8_turnaround, metric: "bar8 rest16 >= 4 OR bar8 != bar4", target: true, w: 0.5, stick: 0.1, bonus_if_true: 0.3}   # PB12, DA; ARS soft NO_BAR8_TURNAROUND
  - {id: S_var_back_half, metric: var_backhalf_pct, target: [50, 100], tol: 20, w: 0.5, stick: 0.1}   # ARS refs median 63 (32-88)
  - {id: S_plan, metric: "phrase plan in dialect plan set (v1 §2 P1-P5)", target: true, w: 0.5, stick: 0.1}   # v1 §2; any other plan just costs 0.5
```

### 2.4 BASS (soft)

```yaml
bass_soft:
  - {id: B_on_kick, metric: onkick_pct, target: {UK_MIN: [0, 28], LONG: [0, 30], POP: [0, 10], JACKIN_90S: [0, 35]}, tol: 5, w: 2, stick: 0.6}   # ARS lane <=28 (refs 1.2-31.6); hard at >35 is H2
  - {id: B_npb, metric: notes_per_bar, target: {UK_MIN: [3.9, 8.7], LONG: [3.9, 7.5], POP: [4.0, 6.0], JACKIN_90S: [6.0, 8.0]}, tol: 1.0, w: 1, stick: 0.3}   # ARS refs 3.94-8.62; MD per dialect
  - {id: B_const_density, metric: "npb_sd, npb_range", target: "sd >= 0.5 and range >= 2", tol: 0.3, w: 2.5, stick: 0.7}   # ARS refs sd 0.66-2.33, range 2-7 (agreed)
  - {id: B_len_med, metric: len_med16, target: {UK_MIN: [1.0, 2.4], LONG: [1.8, 4.0], POP: [0.9, 1.4], JACKIN_90S: [1.5, 2.6]}, tol: 0.4, w: 1, stick: 0.3}   # ARS lane bands
  - {id: B_long_notes, metric: long_ge6_pct, target: {default: [0, 15], LONG: [0, 40]}, tol: 10, w: 0.7, stick: 0.3}   # ARS BASS_REESE_LONG
  - {id: B_run, metric: max_16th_run, target: [0, 7], tol: 2, w: 1, stick: 0.3}   # ARS refs max 7 (REF_R1)
  - {id: B_grid_locked, metric: "inbeat_pos_ge10 >= 3 AND generic_bars <= 4", tol: 1, w: 3, stick: 0.7}   # ARS AV02 refs 3-4 positions (agreed)
  - {id: B_sounding, metric: sounding_frac, target: {UK_MIN: [0.36, 0.95], LONG: [0.75, 0.975], POP: [0.28, 0.45], JACKIN_90S: [0.70, 0.95]}, tol: 0.1, w: 1, stick: 0.3}   # ARS refs 0.36-0.97; MD per dialect; >0.975 is H3
  - {id: B_rest4, metric: longest_rest16, target: [4, 64], tol: 2, w: 0.5, stick: 0.1}   # PB14.1#8 taste; refs 1-37 so light only
  - {id: B_root_drone, metric: root_pct, target: [0, 80], tol: 5, w: 3, stick: 0.7}   # ARS refs <= 67.9, DA 76 (agreed)
  - {id: B_root_share, metric: root_pct, target: {UK_MIN: [28, 76], LONG: [28, 45], POP: [30, 42], JACKIN_90S: [28, 40]}, tol: 8, w: 0.7, stick: 0.3}   # MD, PB12, DA
  - {id: B_top2, metric: top2_pct, target: {default: [57, 99], JACKIN_90S: [55, 70]}, tol: 8, w: 1.5, stick: 0.3}   # ARS refs 57.3-98.4
  - {id: B_pcs, metric: "pitch classes >= 5% time", target: [2, 6], tol: 1, w: 1, stick: 0.3}   # ARS refs 2-6
  - {id: B_pitches_bar, metric: pitches_per_bar, target: {default: [1.3, 5.2], POP: [1.5, 2.5]}, tol: 0.8, w: 1, stick: 0.3}   # ARS, MD
  - {id: B_octave, metric: octave_move_pct, target: {UK_MIN: [0, 26], LONG: [0, 8], POP: [15, 30], JACKIN_90S: [0, 26]}, tol: 6, w: 0.7, stick: 0.3}   # ARS refs 0-25.6 (REF_R2 16-21); MD REF_D2 0, REF_B1 20-26
  - {id: B_leap, metric: max_leap, target: [0, 19], tol: 5, w: 0.7, stick: 0.3}   # ARS refs 17/19
  - {id: B_scale_run, metric: step_run_moves, target: [0, 5], tol: 2, w: 1.5, stick: 0.5}   # MD/ARS refs <= 5 ("theory-generic")
  - {id: B_swing, metric: bass_swing_pct, target: [46, 58.5], tol: 3, w: 1, stick: 0.3}   # ARS
  - {id: B_vel_layers, metric: vel_layers, target: [2, 4], tol: 1, w: 0.4, stick: 0.1}   # PB14.1#10
  - {id: B_glide, metric: glide_pct, target: [0, 20], tol: 8, w: 0.3, stick: 0.1}   # MD 0-20.3
  - {id: B_b2, metric: b2_time_pct, target: {default: [0, 5], JACKIN_90S: [10, 30]}, tol: 5, w: 0.7, stick: 0.3}   # MD; ARS jackin b2 >= 10
  - {id: B_dotted, dialect: JACKIN_90S, metric: dotted_share_pct, target: [55, 100], tol: 15, w: 1, stick: 0.5}   # ARS jackin profile, CTR D1
  - {id: B_organ_8ths, metric: "eighths_attacked_med >= 6 AND onkick >= 40", target: false, w: 1.5, stick: 0.3}   # ARS BASS_ORGAN_8THS (soft now)
  - {id: B_offbeat_pump_only, metric: beats_offbeat_only_pct, target: [0, 75], tol: 10, w: 1, stick: 0.3}   # ARS OFFBEAT_PUMP_ONLY
  - {id: B_16th_roll, metric: beats_eaa_pct, target: [0, 25], tol: 10, w: 1, stick: 0.3}   # ARS BASS_16TH_ROLL
```

The v1 generator priors still drive *sampling*, and they aren't penalties: slot weights, cell types, contour templates, interval distribution and dialect plan weights (v1 §3.1, §2). A roll can use any cell or contour. The soft rules only score the result.

### 2.5 STAB (soft)

```yaml
stab_soft:
  - {id: ST_on_beat, metric: stab_onbeat_pct, target: {default: [0, 30], JACKIN_90S: [0, 50]}, tol: 10, w: 1, stick: 0.3}   # ARS refs 3.6-28.4; CTR A9
  - {id: ST_density_mode, metric: stab_hits_per_bar, target: "dub [0.5, 3] OR riff [4, 10]", tol: 1.0, w: 0.8, stick: 0.3,
     mode_default: {UK_MIN: dub, LONG: dub, POP: riff, JACKIN_90S: riff}}   # ARS/REV §0.2 refs 0.5-10.38; 3-4 = "generic" zone pays d from the nearer mode
  - {id: ST_pcs, metric: stab_pcs_med, target: [1, 4], tol: 1, w: 0.8, stick: 0.3}   # CTR A9, DES quartal 4
  - {id: ST_on_bass_attack, metric: stab_on_bass_attack_pct, target: [0, 80], tol: 10, w: 0.5, stick: 0.3}   # ARS refs 23.9-74.8; v1 "never" rule DROPPED
  - {id: ST_in_rests_bonus, dialect: UK_MIN, metric: stab_in_bass_rest_pct, bonus: "-0.5 if >= 50"}   # ARS BONUS_STABS_IN_RESTS (refs median 25): taste, never required
  - {id: ST_piano_offbeat, metric: "4-pc hits on >= 3 offbeats per bar in > half the bars", target: false, w: 1.5, stick: 0.5}   # ARS PIANO_OFFBEAT_CHORDS
  - {id: ST_jackin_sustain, dialect: JACKIN_90S, metric: stab_dur_med16, target: [8, 16], tol: 4, w: 0.7, stick: 0.3}   # ARS, MD 6.8-14.5
  - {id: ST_register, metric: "stab notes MIDI", target: [45, 79], tol: 5, w: 0.5, stick: 0.3}   # DA, v1 (ESTIMATE band)
```

### 2.6 Other lanes (soft; numbers and sources as in v1 §3.4–3.8)

```yaml
other_soft:
  hits:  [{id: HI_bars, target: "bars 4/8 slots 14-16, 1-2 per 4 bars", w: 0.4, stick: 0.1}, {id: HI_on_kick, target: "not on kick/other lane onset", w: 0.8, stick: 0.3}]   # DES15.1 (ESTIMATE weights)
  pluck: [{id: PL_off_default, target: "off unless POP or user on", w: 0.3, stick: 0.1}, {id: PL_clear, target: ">= 1 sixteenth from bass/stab onsets", w: 0.5, stick: 0.3}]   # DES15.1
  pad:   [{id: PD_under_drop, metric: pad_sounding_frac, target: [0, 0.5], tol: 0.2, w: 1, stick: 0.3}, {id: PD_swell_rate, target: "1 per 2-4 bars", w: 0.4, stick: 0.1}]   # PB14.3#5, PB14.2#1
  vox:   [{id: VX_rate, metric: chops_per_bar, target: {default: [0.25, 3.0]}, tol: 1, w: 0.5, stick: 0.3}, {id: VX_same_slots, target: "call bars reuse slot set", w: 0.4, stick: 0.1}, {id: VX_pitch, target: "|shift| <= 5 st", tol: 2, w: 0.8, stick: 0.3}]   # MD, PB14.4
  drums: [{id: DR_kick_4x4, metric: kick_quarter_cov, target: [0.8, 1.0], tol: 0.1, w: 2, stick: 0.7}, {id: DR_hat_swing, metric: hat_swing_pct, target: {default: [49, 59.5], JACKIN_90S: [60, 68]}, tol: 3, w: 0.7, stick: 0.3},
          {id: DR_hat_16ths, metric: closed_hat_per_bar, target: [0, 10], tol: 2, w: 0.8, stick: 0.3}, {id: DR_latin_bed, target: "no conga/bongo bed in UK modes", w: 0.8, stick: 0.3}, {id: DR_fx_in_loop, target: "no riser/crash in drop loop", w: 0.8, stick: 0.3}]   # ARS
```

---
## 3. Dialect targets (band centres used to sample; band edges = §2 targets)

| param | UK MIN | LONG | POP | JACKIN 90s | source |
|---|---|---|---|---|---|
| bass on-kick % (soft / HARD) | ≤ 28 / > 35 | ≤ 30 / > 35 | ≤ 10 / > 35 | ≤ 35 / > 35 | ARS, MD |
| bass notes/bar | 3.9–8.7 | 3.9–7.5 | 4–6 | 6–8 | ARS, MD |
| bass len16 median | 1.0–2.4 | 1.8–4.0 | 0.9–1.4 | 1.5–2.6 | ARS |
| sounding fraction | 0.36–0.95 | 0.75–0.975 | 0.28–0.45 | 0.70–0.95 | ARS, MD |
| octave moves % | 0–26 | 0–8 | 15–30 | 0–26 | ARS, MD |
| match@1 / @2 / @4 | ≤ 0.95 / ≥ 0.50 / ≥ 0.47 | same | same | ≤ 0.20 / ≥ 0.70 / — | ARS |
| stab mode (hits/bar) | dub 0.5–3 (riff allowed) | dub | riff 4–10 | riff, sustained 8–16 | ARS, MD |
| stab on-beat % | ≤ 30 | ≤ 30 | ≤ 30 | ≤ 50 | ARS |
| hat swing % | 49–59.5 | 49–53 | 49–51 | 60–68 | ARS, MD, SA |

---
## 4. Variation, diversity and the skeleton gate

### 4.1 Mutations
The safe and style-breaking lists from v1 §7.1 still guide the *sampler*. Under v2:
- A "style-breaking" mutation is no longer forbidden. It costs whatever soft penalties it triggers.
- `freedom.deliberate_breaks` picks style-breaking mutations on purpose.
- Mutations can never break a §1 HARD rule. The sampler re-draws the mutation instead.

### 4.2 Skeleton definition and the batch gate

```yaml
skeleton:
  definition: >
    The bass SKELETON of a roll = the set of tokens (bar_index mod W, slot 1-16, pitch class relative to the
    roll's tonic) for every bass note-on, after merging pitched lanes to the lowest note per 16th onset
    (OC MERGED_LOW). Two rolls' skeleton similarity = OC.skeleton_window max over W in {2, 4} bars,
    both directions, best of 12 transpositions x bar offsets x candidate shift -2..+2 sixteenths
    (= score_rolls.py skel_sim). Pedal/pulse-only windows and streams < 6 onsets are skipped (OC guards).
  same_skeleton_pair: "skel_sim >= 0.55"      # OC skel_win FAIL; ARS
  near_pair:          "skel_sim >= 0.40"      # OC FLAG
diversity_gate_per_20_roll_batch:      # 190 pairs
  same_skeleton_pairs_max: 2           # 1.05% of pairs; GR v1 §8 "<= 2 same-roll pairs"; refs: 0 cross-track pairs >= 0.40 [ARV]; v4 mockup 29-66/190 [ARV]
  rolls_in_any_same_pair_max: 3        # i.e. no "family" of 3+ rolls; v4 POP 16/20 rolls [ARV]
  near_pairs_max: 19                   # 10% of pairs (ESTIMATE); soft: reported, lowers the batch's R-variety note
  duplicate: "skel_sim >= 0.80 AND stab_sim >= 0.75 -> always counts as a same pair"   # ARS DUPLICATE_OF_ROLL
  report_only: [mean onset Jaccard, p90, distinct bar-1 rhythms, distinct stab voicings]   # v1 0.35/0.45 targets retired
generator_side: "keep a rolling archive of the last 200 skeletons per dialect; when sampling, reject-and-redraw cells that would give skel_sim >= 0.55 to any archived roll (cheap: compare 2-bar windows only)"   # ESTIMATE implementation hint
large_batches: "for N >= 1000, same-skeleton pair rate <= 1% of pairs, and no skeleton family > 0.5% of rolls"   # ESTIMATE, scales the 20-roll rule
```

---
## 5. 20-roll scoring rubric (aligned with A&R's R1–R9 so totals compare per roll)

These are the same criteria, weights and codes as `score_rolls.py` RUBRIC, with a total of 38 points.

**Gate.** Only a §1 HARD failure makes a roll score 0/38 without audition. These A&R codes are HARD under v2:
- **H1:** a new `OUT_OF_KEY` code is needed.
- **H2:** BASS_ON_KICK at > 35, BASS_REGISTER.
- **H3:** NO_BASS, NO_GAPS, NO_HOLES, STUCK_NOTE, TAIL_PAST_LOOP, TAIL_INTO_BAR1, CLIPPING, CLICKS. An `EMPTY_BAR` code is needed.
- **H4:** COPY_OF_REF, BANNED_FIGURE.
- **Batch:** SAME_SKELETON_AS_ROLL and DUPLICATE_OF_ROLL fail the later roll of a pair under the §4.2 limits.

**Every other A&R code becomes an advisory flag.** It's shown in the R-column as `soft:CODE` and doesn't set the criterion to 0. Judges score by ear. **A roll is never marked down just for being outside a measured band.**

| # | criterion (weight) | 0 | 1 | 2 | A&R proxy codes (now advisory unless HARD) |
|---|---|---|---|---|---|
| R1 | Groove / roll (3) | doesn't move | moves | rolls, DJ would loop 32 bars | GRID_LOCKED, NOTE_RUN, CONSTANT_DENSITY, BASS_SWUNG, BASS_16TH_ROLL, OFFBEAT_PUMP_ONLY, HAT_16THS, SWING_90S, KICK_NOT_4x4… |
| R2 | Bass hook identity (3) | generic / aimless | some idea | owns it, hummable | SCALE_RUN, ROOT_DRONE, TOP2_LOW, TOO_MANY_PCS, PCS_PER_BAR, LEAP_TOO_BIG, OCTAVE_PUMP; HARD: NO_BASS, BASS_REGISTER, OUT_OF_KEY |
| R3 | Holes & space (2) | crowded | ok | breathes | DENSITY_LOW, BASS_NOTES_TOO_LONG, BASS_REESE_LONG; HARD: NO_GAPS, NO_HOLES, EMPTY_BAR |
| R4 | Kick/bass lock (2) | fights | ok | locked | HARD: BASS_ON_KICK > 35; soft: on-kick 28–35, KICK_NOT_4x4, duck F12 |
| R5 | Phrase arc (2) | static or random | present | lands (bar 4/8) | LOOP_NO_REPEAT, STATIC_LOOP, CALL_ANSWER_MISSING, NO_BAR8_TURNAROUND, VAR_NOT_IN_BACK_HALF |
| R6 | Stab & lanes call/response (2) | collide / mush | ok | conversational | STAB_ON_BEAT, STAB_TOO_THICK, STAB_CLASH, PIANO_OFFBEAT_CHORDS; bonus BONUS_STABS_IN_RESTS |
| R7 | **Dialect fit + surprise (2)** | off-style AND doesn't groove | in style but predictable, OR surprising but shaky | in style with ≥ 1 surprise that still grooves, OR a clear departure that grooves and Melak would play | LANE_FIT_LOW (advisory), meta.broken_rules |
| R8 | Originality (2) | recognisable ref | faint echo | own | HARD: COPY_OF_REF, BANNED_FIGURE; soft REF_FLAG |
| R9 | Clean loop (1) | clicks/wrap | minor | clean | HARD: TAIL_*, STUCK_NOTE, CLIPPING, CLICKS |

**Surprise rules.**
- Each judge also ticks **SURPRISE_GROOVES (Y/N)**: "something I didn't expect, and it still rolls."
- The CSV logs `broken_rules` and `P_roll` so judges can see which deviations were deliberate.
- A deviation that grooves scores R7 = 2.
- R1–R6 are judged on how the roll sounds, never on the band.
- **New test-set requirement:** ≥ 4/20 rolls tagged SURPRISE_GROOVES by at least one judge (ESTIMATE). This checks that freedom isn't producing timid rolls.

**Set PASS.**
- From v1, kept:
  - median roll ≥ 26/38;
  - ≥ 14/20 rolls at ≥ 22;
  - ≥ 5/20 keepers at ≥ 30;
  - judges agree (≤ 4 rolls with |A−B| > 8).
- New in v2:
  - the §4.2 skeleton gate holds;
  - ≥ 4/20 SURPRISE_GROOVES.
- **A&R's ship conditions still apply on top:**
  - auto-pass ≥ 80%, now counting HARD codes only;
  - MELAK_KEEP = Y on ≥ 80%.

**Advisory soft-load check.** The median P_roll of the set and the share of rolls with ≥ 1 band deviation are reported only. They never fail a set.

**Needed in `score_rolls.py` (A&R owns it; not edited by me):**
1. Re-tag the non-HARD codes in `HARD` as soft per the list above.
2. Make BASS_ON_KICK fire at > 35 for every style. The 28% lane value becomes soft `ONKICK_HIGH`.
3. Add `OUT_OF_KEY` (H1) and `EMPTY_BAR` (H3).
4. `rubric_proxy` should mark soft codes `soft:` instead of `FAIL:`.
5. Batch: apply the §4.2 limits (≤ 2 same pairs, ≤ 3 rolls in pairs).

Until then the scorer is stricter than v2, so its auto-pass rate understates v2 rolls.

---
## 6. What's measured vs estimated

- **Measured** (MD, ARS, ARV, CTR, OC):
  - every soft-rule band edge in §2.3–2.5 except the stab register;
  - H1 out-of-scale ≤ 5% (refs max 4.4%) and tonic in top-2;
  - H2 on-kick 35% and register 20–45;
  - H3 sounding ≤ 0.975 and ≥ 1 silent 16th;
  - H4 thresholds;
  - the skeleton metric and the 0.55 same-pair level, plus the refs' 0 cross-track pairs and v4's 29–66/190;
  - the stab-on-bass-attack range of 24–75%.
- **ESTIMATE:**
  - all weights, tolerances and stickiness values;
  - the penalty curve shape and the freedom formulas, with defaults f = 0.35 / 0.6;
  - k = round(3f), K = 6, T = 1 + 4f;
  - the H2 "other lanes ≥ C2" floor and the H1 rule for non-bass lanes;
  - the near-pair cap (19/190) and the large-batch rates;
  - the SURPRISE_GROOVES quota of 4/20;
  - the calibration targets in §2.2.
- **Calibrate first:** run the reference drops and the v4 mockup batches through the penalty function. Refs at f = 0 should score P ≤ 1, and v4 should fail the §4.2 gate (it does: 29–66 pairs).
