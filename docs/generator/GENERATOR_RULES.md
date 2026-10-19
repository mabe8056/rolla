# GENERATOR_RULES.md: a machine-checkable spec for UK minimal / deep-tech roller parts (v1)

Written by the Composer, 2026-10-17 (ET), for the engineer rebuilding ROLLA around a cell vocabulary that gets recombined, mutated and rule-checked.
Every number has a source tag. **ESTIMATE** means the number isn't measured: it's a judgement anchored to the nearest measured value, which is named. Calibrate ESTIMATE numbers against the first 20-roll test set (§8).

---
## 0. Conventions

```yaml
grid:
  slots_per_bar: 16          # 16ths, 1-indexed, slot 1 = downbeat (same as CTR, DESIGN)
  kick_slots: [1, 5, 9, 13]
  beat_pos: {1: beat, 2: e, 3: and, 4: a}      # in_beat = ((slot-1) % 4) + 1
  tempo_bpm: [124, 131]      # measured refs 130-131 [SA]; 1 sixteenth = 115 ms at 130 BPM
  key_ref: tonic-relative degrees; MIDI shown for D minor (D1=26) unless stated   # Direction A key [DA]
  loop_len_bars: [1, 2, 4, 8]  # generator outputs 8-bar phrases built from 1/2/4-bar cells
units:
  len16: note length in 16ths
  npb: note onsets per bar
  slot_pct: % of a lane's onsets that land on each slot (sums to 100)
  match@L: pitch-grid match at lag L bars = fraction of 16th slots whose sounding pitch (or rest) equals the slot L bars later
```

**Source keys.**
- **MD**: private measured-drop table. It covers 16 drop windows from 9 tracks, made by a private aggregation script from the private per-track drop transcriptions. "UK pool" = REF_R3, REF_R1, REF_D2, REF_R4, REF_B1 and REF_R2 (12 windows); the GASKIN-dialect refs (REF_J1/J2) and REF_R6 are kept separate.
- **CTR**: private cross-track rules (CTR) (rule ids A1–A11, B, C, D1–D7, E1–E8).
- **SA**: private style analysis (4 Beatport previews).
- **PB**: `rulebook/house-master-playbook.md`. §12 is the measured full-WAV REF_R5; §14.x are the writing rules.
- **SI**: `rulebook/structure-and-instrumentation.md`.
- **DES**: `plugin-design/DESIGN.md`.
- **OC**: `tools/originality_check.py` (the A&R gate).
- **CM**: `writing/COMPOSER_METHOD.md`.
- **DA**: Direction A ("Hold & Lean"), passed by A&R; numbers re-measured today: match@1/2/4 = 0.35/0.80/0.88, sounding 0.53, npb per bar [5,3,5,6,5,3,5,4].
- **PATCH**: `writing/sketch_v1/PATCHES.md` §6.

**Measurement caveat (SA, CTR).** Every number apart from REF_R5 comes from automatic transcription of previews or stems. Treat values as tendencies. Bar phase is less reliable than in-beat position, so checks prefer in-beat stats where possible.

---
## 1. Why the current output has too little variation (read of `mockup_v4/index.html` genBass)

Today's generator works like this:
- one weighted slot table per dialect;
- one cell A, plus a cell B that is A's front half with one back note added;
- a fixed A B A(sec) B′ plan;
- the same opening slot in every bar, and only the root plus one partner pitch.

So every roll is "a random subset of the same weight vector" played through the same 4-bar plan. The search space is about (opening × subset) and the pitch content is close to fixed.

The rebuild should:
1. draw from a **vocabulary of typed cells** (§3);
2. pick **one of several phrase plans** (§2);
3. draw pitch from **contour templates** rather than per-note coin flips (§3.2);
4. **reject** rolls with the §6 checks instead of shaping them by hand.

The diversity targets are in §7.2.

---
## 2. Global phrase rules (all lanes)

```yaml
phrase:
  length_bars: 8
  plans:                      # bar-level labels; ' = small variation (<= 4 slots changed), " = larger (5-8 slots)
    P1: [A, B, A, B', A, B, A, C]        # DA, PB12 REF_R5 (4-bar loop + bar-8 dropout)
    P2: [A, B, A, B', A, B, A', C]       # DES 5 rule plan A B A' B''
    P3: [A, A', A, A'', A, A', A, C]     # 1-bar loops: REF_R1/REF_R4/REF_B1 match@1 0.58-0.75 [MD]
    P4: [A, B, A, B, A, B, A, C]         # Gaskin 2-bar call/answer, match@1 0.01-0.16, match@2 0.86-0.92 [MD, CTR D2]
    P5: [A, B, C, D, A, B, C, D']        # REF_R5 4-bar loop A B C D [PB12]; REF_R6/REF_R3 match@4 > match@2 [MD]
  plan_weights: {UK_MIN: {P1: .35, P2: .25, P3: .15, P5: .25}, LONG: {P1: .4, P2: .3, P3: .3},
                 POP: {P3: .6, P2: .4}, GASKIN_90S: {P4: .8, P2: .2}}   # ESTIMATE (weights), plan shapes measured
  repetition_targets:          # main-drop windows, bass lane [MD]; DA in brackets
    match_at_1: {UK_pool: [0.05, 0.75], GASKIN: [0.01, 0.16]}            # [DA 0.35]
    match_at_2: {UK_pool: [0.51, 0.86], GASKIN: [0.86, 0.93]}            # [DA 0.80]
    match_at_4: {UK_pool: [0.47, 0.85], CTR_A6_main_loop_min: 0.77}       # [DA 0.88]
  variation_zone:
    default: last 2-4 sixteenths of the bar (slots 13-16)                 # CTR A7
    bar_4: may change slots 9-16 (one "B'" answer)                        # PB14.1#7, DA bar 4
    bar_8_turnaround: change 4-12 slots; end in a hole of >= 4 sixteenths   # PB12 (1.5 beats = 6 sixteenths), DA (~1.4 beats)
  max_changed_slots_per_bar_vs_parent: 8      # ESTIMATE (beyond this it reads as a new riff, not a variation; DA bar 8 changes 7)
  fills:
    where: bar 8 always, bar 4 optional (p 0.5)          # PB14.1#7, SI 3.1 switch-ups; p ESTIMATE
    max_lanes_changing_in_bar_8: 3                       # ESTIMATE (DA: bass+stab+hits change in bar 8)
  tails:
    all_note_offs_le_loop_end: true                      # A&R B1, CM render rule
    render_last_10ms_peak_dbfs_max: -45                  # A&R gate (DA measured -49.9)
```

---
## 3. Lanes

### 3.1 BASS (the hook; PB14.1#1)

```yaml
bass:
  register_midi: {min: 21, max: 52, home_octave: [24, 35]}     # MD measured A0(21)-C#3(49); DA D1-F1 (26-29); PB14.1#3 35-60 Hz fundamentals
  pitch_range_semitones_per_8bars: {UK_pool: [8, 23], median: 13, GASKIN: [5, 16], DA: 3}   # MD; DA
  density:
    npb: {UK_pool: [4.38, 8.75], median: 7.14, DA: 4.5, hard_max: 9}  # MD; DES rule 1 cap 7 (UK MIN); hard_max = MD max per-bar mean
    npb_per_bar_range: [3, 11]                                 # MD per-bar min/max across windows (REF_B1 4, REF_R1 11)
    npb_sd_across_8_bars: {min: 0.5, ref: [0.9, 2.67]}          # MD (DA 1.0); min 0.5 = ESTIMATE floor ("constant density" check)
    sounding_fraction: {UK_pool: [0.30, 0.89], median: 0.70, DA: 0.53}   # MD
    silent_slot_fraction_min: 0.10                              # ESTIMATE from MD (lowest UK sounding 0.89 -> 0.11 silent)
  note_length_len16:
    median: {UK_pool: [1.07, 2.26], DA: 2.0, CTR_A3: [1.1, 2.3]}       # MD, CTR
    p25_p75_envelope: [[0.91, 1.91], [1.17, 3.92]]              # MD
    distribution_targets:       # share of notes per length bucket (UK pool min-max, MD len16_hist)
      "<1.25":  [0.05, 0.80]
      "1.25-2.5": [0.10, 0.75]
      ">=2.5":  [0.02, 0.45]
    min_distinct_length_buckets_per_8bars: 2                   # ESTIMATE (every MD window uses >=2 buckets at >=10%)
  rhythm:
    onset_slot_weights_pct:     # UK pool mean, MD bass_onset_slot_pct; use as PRIOR, not a fixed table
      [8.0, 3.6, 8.3, 4.0, 4.0, 8.6, 4.7, 11.4, 4.2, 8.7, 5.7, 4.9, 6.4, 4.8, 10.2, 2.6]
    on_kick_pct: {UK_pool: [1.2, 28.0], median: 24.4, DA: 0, REF_R5: "~2", hard_max: 30}   # MD, PB12; CTR A1 (72-98% off kick)
    max_consecutive_16th_onsets: 3                              # DES rule (no runs >3)
    in_beat_spread_min_positions: 3                             # ESTIMATE; MD: every UK window uses >=3 of e/and/a at >=10%
  cells:                        # cell = onset set within 4 or 8 slots + lengths; bars are made of 2-4 cells. Measured exemplars:
    # Cells are TYPES. The engine may transpose them in time by +-1 slot and change lengths within the bucket targets,
    # but never copy a full exemplar bar (see the originality gate, §7.3).
    beat1_signature: {slots_within_beat: [[2,4],[3],[2],[3,4]], src: "PB12 REF_R5 2&4 root figure (banned verbatim, CM5.7); DA slot 3; REF_R4 1,3"}
    hold_lean:       {pattern: "onset at e/and, held 3-8 sixteenths through the next kick", src: "REF_D2 root 1-6 [CTR], DA bar 1 slot 3->6"}
    off_8th_pump:    {slots: [3,7,11,15], len16: [1,1.5], src: "REF_B1 3/6/8/12 family, CTR B; REF_R1"}
    syncop_pair:     {slots_within_2beats: [[2,4],[3,6],[6,8]], src: "REF_R4 1,3,6,8 [CTR C]"}
    answer_tag:      {slots: [14,15,16] subset, len16: [1,2], src: "CTR A7, REF_D2 tag 15-16"}
    dotted_grid:     {slots: [1,4,7,10,13,16], ghost: 12, only_dialect: GASKIN_90S, src: "CTR D1"}
    rest_cell:       {slots: [], len_slots: [4, 8], src: "PB14.1#8 holes"}
    cell_combination: {per_bar_cells: [2, 4], rest_cell_per_8bars_min: 2}   # rest-cell minimum = ESTIMATE (DA has 3: bars 2,6,8)
  pitch:
    degree_time_share_UK_mean: {"1": 40.5, "2": 14.2, "5": 13.3, "b7": 13.1, "b3": 11.8, "4": 3.4, "b2": 1.2, "6": 1.0}  # MD
    root_share: {UK_pool: [28.6, 60.2], REF_R5: 68, DA: 76, hard_range: [28, 80]}    # MD, PB12, DA
    top2_share: {UK_pool: [58.1, 98.4], median: 93.9, CTR_E: [63, 98], root_in_top2: required}   # MD, CTR
    distinct_pitches_per_bar: {UK_pool: [1.94, 5.0], median: 3.25, DA: "1-3"}            # MD
    distinct_pitch_classes_per_8bars: [2, 6]                    # SA 2-6 per bar; PB14.1#2; CM triviality >=2
    allowed_partner_degrees: [b7, b3, "5", "2", "4", b2]        # MD degree shares; b2 = chromatic neighbour only (SA habit 4)
    b2_rule: "only as a neighbour (approach/leave by 1 semitone to/from root); time share <= 5% (GASKIN <= 30%)"  # MD UK b2 1.2%; CTR D REF_J1/J2 26%
    interval_distribution_targets:   # non-zero moves, UK pool [MD bass_intervals]
      same_pitch_repeat_share_of_all_moves: [0.15, 0.62]       # MD rep 15.2-62.4%
      step_1_2: [0.25, 0.60]       # MD pooled +-1/+-2 = 43%
      third_3_4: [0.0, 0.20]       # pooled 11%
      fourth_fifth_5_7: [0.0, 0.40]  # pooled 19%; REF_R4 38% (CTR C)
      octave_12: {UK_MIN: [0, 0.10], LONG: 0, POP: [0.10, 0.30], GASKIN: [0, 0.10], pooled_ref: 0.14}   # MD oct 0-25.6%
      other_6_8_9_10_11: [0.0, 0.12]  # pooled 10.7%
    max_leap_semitones: 15                                      # SA REF_R2 +15
    contour_templates:          # per bar or per 2-bar cell; pitch path over the onsets
      - pedal: [1, 1, 1, ..., partner_tag_end]                  # REF_D2/REF_R2 [CTR]
      - neighbour: [1, x, 1] with x in {b7 below, 2, b2}         # REF_R1 1<->2, REF_J1/J2 b2 [CTR]
      - ladder_down: [5, 2?, b7, b3?, 1]                        # DES DARK ladder
      - fifth_pop: [1, 5, 1, 5(+12?)]                           # REF_B1 98% root+5 [MD]
      - step_up: [1, 2, b3]                                     # REF_R3 [CTR]
      - call_answer_2bar: {barA: [b7, 1], barB: [b2, b3]}       # GASKIN only [CTR D2]
  velocity: {levels: [112, 80], ghost_share: [0.15, 0.40]}      # DES; PB14.1#10 ghosts 6-7 dB down; share ESTIMATE
  glide: {share_of_notes: {UK_pool: [0, 0.20], DES: [0.02, 0.11]}, time_ms: [30, 40]}   # MD glide 0-20.3%; PB12
```

### 3.2 KICK/BASS interlock

```yaml
interlock:
  bass_onset_on_kick_slot_pct_max: {UK_MIN: 10, LONG: 30, POP: 5, GASKIN_90S: 35}   # MD: DA 0, REF_R5 ~2, REF_D2 26, REF_B1 2.3, REF_J1/J2 17-34 (grid includes slot 1)
  bass_onset_off_kick_pct_min: 72                     # CTR A1 (72-98%)
  held_through_kick_allowed: true                     # PB14.1#5 (sidechain does the ducking)
  sidechain_db: {UK_pool: [17, 54], REF_R5: 12, DA: 13, GASKIN: [3.6, 8.1], REF_R6: [3.6, 8]}   # MD sidechain_bass; PB12; CTR E
  sidechain_recovery_ms: [100, 160]                   # PB12 ~100; MD 140-157 (REF_D2)
  kick_fundamental_hz: [49, 55]                       # PATCH6, PB14.5#1
  first_bass_onset_in_bar_slot: {prefer: [2, 3, 4], allow: [1] only GASKIN_90S or POP}   # PB12, DA (slot 3), CTR D1
```

### 3.3 STAB (chord hits; PB14.2, 14.3)

```yaml
stab:
  register_midi: {min: 45, max: 79, voicing_span_semitones: [7, 19]}   # DA A3-C5 (57-72) span 15; SA 5th+oct (12-19); range ESTIMATE
  voicing:
    pitch_classes_per_hit: [1, 3] (+ optional colour tone -> max 4)    # CTR E (1-3 pcs); DA quartal 4 notes
    root_in_voicing: avoid (bass owns root)                           # PB14.2#3
    allowed_types: [quartal/sus no-3rd, "5th+octave", "minor triad over other root (iv, bVI, b7 triad -> m11/9sus)"]   # PB14.3#1-3, SA habit 7
    voicing_changes_per_8_bars: [1, 3]                                 # SA habit 8 (colour every 2-4 bars), DA (QA/QB)
  density:
    hits_per_bar: {UK_pool: [2.75, 10.38], median: 5.25, DA: 0.75, UK_MIN_default: [0.5, 3]}   # MD; DA; default ESTIMATE between DA and REF_D2 2.8
    on_beat_pct_max: 30                                               # CTR E (<=30%); MD 2.5-29.9
    len16: {UK_pool_median: [2.16, 34.3], typical: [1, 3], GASKIN_sustained: [6.8, 14.5]}   # MD (34 = REF_R1 pad-like); SA 0.9-1.9
  rhythm:
    onset_slot_weights_pct:  # UK pool mean, MD stab_onset_slot (CQT LOW-MED confidence)
      [4.0, 4.1, 5.8, 8.3, 2.7, 5.1, 6.6, 8.4, 3.8, 8.6, 6.1, 4.0, 5.7, 6.3, 14.4, 6.1]
    preferred_in_beat: [and, e]                                       # SA (REF_R2 "and" 55%, REF_J1/J2 "e" 45%)
  placement_vs_bass:
    same_slot_as_bass_onset: {UK_MIN: forbidden, others: "<= 25% of stab hits"}   # DA 0/8; refs overlap 0.50-0.78 histogram-wise [MD] -> allowed outside UK MIN
    min_gap_after_bass_onset_16ths: {hard: 1, prefer: 2}             # DA min gap 2 (115-230 ms @130); hard = ESTIMATE
    over_sounding_bass_note_pct_max: 25                              # DA 2/8 hits over a held tail (A&R noted as polish); ESTIMATE ceiling
    call_response: "stab hits fall in bass rest cells or the last 3 slots of a bass hold"   # DA (6/8 in clear rests), PB14.1#8
    dialect_counter_riff: {REF_R4_style: "stab may double bass rhythm (overlap 0.78)", only: LONG/POP opt-in}   # MD
  phrase: {placement_bars_default: [2, 4, 6, 8], bar8_extra_hits: [1, 3]}   # DA bars 2/4/6/8; ESTIMATE that this generalises
```

### 3.4 HITS (one-shot FX signposts; no measured stem: lane largely ESTIMATE)

```yaml
hits:
  per_4_bars: [1, 2]                     # DES15.1 (1 per 4 bars at bars 4 and 8); SI4 FX one-shots 2-5 per track section
  bars: {required: [4, 8], optional: [2] if BUSY > 0.6}   # DES15.1
  slots: [14, 15, 16]                    # DES15.1
  collision: "never on a kick slot or on another lane's onset (same slot)"   # DES15.1
  len16: [2, 8]; tail_must_end_before_next_bar_1: true    # A&R B1 tail rule; length ESTIMATE
  pitch: unpitched or root/5th only      # ESTIMATE
  level_db_vs_bass_note: -4              # DES levels
```

### 3.5 PLUCK (= DES HOOK lane, off by default)

```yaml
pluck:
  default: off                           # DES15.1; PB14.1#1 "bass is the hook, no lead in the drops"
  on_dialects: [POP]                     # REF_B1 lead riff 9.6-10.4 hits/bar on e/a, louder than bass [MD, CTR B]
  motif_len_bars: 2                      # DES15.1
  register_midi: [60, 84]                # ESTIMATE (above stab range; REF_B1 riff C4-C6 per CTR is LOW-MED confidence)
  hits_per_bar: {POP: [6, 10], other: [2, 5]}   # MD (REF_B1); other ESTIMATE
  pitch_classes_per_motif: [2, 4]; scale: tonic minor pentatonic + 2   # ESTIMATE from bass degree shares
  clearance_16ths_from_other_lanes: 1    # DES15.1 (+-1 sixteenth clear)
  len16: [0.5, 1.5]                      # ESTIMATE (staccato like REF_B1 bass 1.07-1.12)
  level_db_vs_bass_note: -9              # DES
```

### 3.6 PAD (swells, not a drop pad)

```yaml
pad:
  in_drop: {default: off, allowed_as: "one swell per 2-4 bars"}   # PB14.3#5 (no pad under drop), PB14.2#1 swell every ~4 bars, DES15.1 every 2 bars
  entry_slot: 2                          # DES15.1 ("e" of beat 1)
  len16: {swell: [12, 32], GASKIN_chord: [12, 16]}   # CTR D (REF_J1/J2 bVI/bVII ~14/16), MD REF_R1 stab len 34
  voicing: same rules as stab.voicing; changes <= 2 per 8 bars   # SA habit 8
  must_gate_before_barline_of_section_end: true      # CM render rule, A&R B1
  level_db_vs_bass_note: -14             # DES
```

### 3.7 VOX (chops)

```yaml
vox:
  chops_per_bar: {UK_pool: [0.25, 3.0], median: 1.43, REF_R6: [2.4, 5.25]}   # MD chops
  len16_median: {UK_pool: [2.16, 16.7], median: 5.78}                         # MD
  call_bars: [1, 3, 5, 7] (alternate bars)                                    # PB14.4#1 (REF_R5), DES15.1
  positions: "same slot set every call bar (+-0); typical slots 3,5,7,13-15"  # PB14.4#1 (16ths 2.5, 4.5, 6.5, 13-15)
  cuts_per_call: [1, 3]                                                       # DES15.1
  phrase: "one phrase per track, unpitched; pitch shift |st| <= 5"            # PB14.4#1, #4 (6 st 'useless')
  no_sung_topline_in_drop: true                                               # PB14.4#6
  answer_variant_at_bar_8: optional (p 0.5)                                   # ESTIMATE
  level_db_vs_bass_note: -5                                                   # DES
```

### 3.8 DRUMS (Melak's own samples; spec is for pattern generation/QA)

```yaml
drums:
  hit_prob_by_slot:   # UK pool ranges, MD drums hit_frac_by_slot (stem detection: kick bleed inflates slots 1/5/9/13 in other rows)
    kick:   {slots_1_5_9_13: [0.69, 1.0], other_slots_max: 0.31, exceptions: "REF_R4 slot 7 0.81, REF_R6 slot 8 0.75"}
    clap:   {slots_5_13: [0.94, 1.0], ghost_slots: {8: [0, 0.71], 16: [0, 1.0]}, ghost_db: -5}   # MD; PATCH6, PB14.5#2
    hats:   {offbeat_3_7_11_15: [0.25, 1.0] (typ >= 0.85), e_slots_2_10: [0, 1.0], a_slots_4_8_12_16: [0, 0.12] (REF_R4 up to 1.0)}
    open_hat_offbeat: {share_of_offbeat_hits_with_decay_gt_90ms: [0, 1.0]}   # MD (REF_R2 ~0, REF_R4/REF_R3/REF_B1 0.8-1.0)
  hat_decay_ms_median: [52, 162]                     # MD (UK pool); PATCH6 -20 dB @140 ms
  swing_pct:
    hats: {UK_pool: [49.7, 57.9], REF_R6: [64.7, 65.3], REF_R5: 50, GASKIN: "~67 (SA; MD unmeasurable)"}   # MD, PB12, SA
    clap_snare: {UK_pool: [52.9, 63.2]}               # MD
    bass_and_stabs: "near grid, <= 58"                 # CTR, PB14.5#4
  per_bar_variation: "4/4 kick fixed; hat/perc vary in slots 13-16 of bars 4/8 only; open hat on 'and' of 4 every 2nd or 4th bar"   # PATCH6, CTR A7
  groove_test: "drums alone must roll: >= 6 distinct onset slots per bar across hats+perc"   # PB14.5#6; threshold ESTIMATE
```

---
## 4. Dialect table (main-drop targets; generator presets)

| param | UK MIN (REF_R5 + DA + UK pool) | LONG (REF_D2) | POP (REF_B1) | GASKIN 90s |
|---|---|---|---|---|
| bass npb | 4.5–7.5 (DA 4.5; pool median 7.1) [DA, MD] | 7.1–7.2 [MD] | 5.3–5.4 [MD] | 6.0–7.6 [MD] |
| bass len16 median | 1.4–2.2 [MD, DA 2.0] | 2.2–2.3 [MD] | 1.0–1.2 [MD] | 2.0–2.2 [MD] |
| on-kick onset % max | 10 (DA 0, REF_R5 ~2) [DA, PB12] | 30 (26) [MD] | 5 (1.2–2.3) [MD] | 35 (grid on 1) [MD, CTR D1] |
| root share % | 35–76 (pool 29–60, REF_R5 68, DA 76) [MD, PB12, DA] | 30–45; b7 may lead (CTR) [MD 34–36] | 35–40; 5th 55–65 [MD, CTR] | 28–40 [MD 30–37] |
| top-2 share % | 63–98 [CTR E, MD] | 75–85 [MD 80–82] | 94–99 [MD 95–98] | 55–70 [MD 56–66] |
| distinct pitches/bar | 1.5–4 [DA, MD] | 3.5–4.5 [MD 4.1–4.3] | 1.8–2.2 [MD] | 2.5–3.5 [MD 2.9–3.1] |
| octave jumps % | 0–10 [PB14.1#11, MD] | 0 [MD] | 15–30 [MD 20–26] | 0–5 (SA says 25; MD 0–1 → ESTIMATE) |
| sounding fraction | 0.50–0.85 [DA 0.53, MD] | 0.75–0.90 [MD 0.83–0.89] | 0.28–0.40 [MD 0.30–0.34] | 0.75–0.85 [MD 0.79] |
| sidechain dB | 12–30 [PB12, DA, MD] | 26–30 [MD] | gate/≥40 [MD 52–54] | 3.6–8 [MD] |
| bass swing % | 50–56 | 50–54 | 50 | 50 (straight per CTR D5) |
| hat swing % | 50–58 [MD, PB12] | 50–52 [MD] | 49.7–50.4 [MD] | ~65–67 [SA; ESTIMATE] |
| match@2 / @4 | ≥0.6 / ≥0.77 [MD, CTR A6] | ≥0.75 / ≥0.78 [MD] | ≥0.5 / ≥0.75 [MD] | ≥0.85 / ≥0.85, match@1 ≤0.2 [MD] |
| stab hits/bar | 0.5–3 [DA, MD REF_D2; ESTIMATE] | 2.5–4.5 [MD] | 9–11 lead riff [MD] | 5.5–6.5, sustained len 7–15 [MD] |
| stab on-beat % max | 10 (DA 0) | 15 [MD 9–13] | 20 [MD 6.5–18.7] | 20 [MD 9.5–18] |
| vox chops/bar | 0.25–1.5 [MD, PB14.4] | 1.5–2 [MD 1.69] | 0–0.5 (none measured) | 0–1 (ESTIMATE) |
| plan weights | P1/P2/P3/P5 | P1/P2/P3 | P3/P2 | P4/P2 |
| cell set | beat1_signature, hold_lean, syncop_pair, answer_tag, rest | hold_lean, answer_tag, pedal | off_8th_pump, fifth_pop, rest | dotted_grid, call_answer_2bar |

---
## 5. Machine-readable dialect presets

```yaml
dialects:
  UK_MIN:  {bass_npb: [4.5, 7.5], len16_med: [1.4, 2.2], on_kick_max: 0.10, root: [0.35, 0.76], top2: [0.63, 0.98], oct_max: 0.10, sounding: [0.50, 0.85], match2_min: 0.60, match4_min: 0.77, stab_pb: [0.5, 3], stab_beat_max: 0.10, vox_pb: [0.25, 1.5], hat_swing: [50, 58]}
  LONG:    {bass_npb: [6.5, 8.0], len16_med: [2.0, 2.4], on_kick_max: 0.30, root: [0.30, 0.45], top2: [0.75, 0.85], oct_max: 0.0, sounding: [0.75, 0.90], match2_min: 0.75, match4_min: 0.78, stab_pb: [2.5, 4.5], stab_beat_max: 0.15, vox_pb: [1.0, 2.0], hat_swing: [50, 53]}
  POP:     {bass_npb: [4.5, 6.0], len16_med: [1.0, 1.25], on_kick_max: 0.05, root: [0.30, 0.42], top2: [0.94, 0.99], oct: [0.15, 0.30], sounding: [0.28, 0.40], match2_min: 0.50, match4_min: 0.75, stab_pb: [9, 11], stab_beat_max: 0.20, vox_pb: [0, 0.5], hat_swing: [49, 51]}
  GASKIN_90S: {bass_npb: [6.0, 8.0], len16_med: [1.9, 2.3], on_kick_max: 0.35, root: [0.28, 0.40], top2: [0.55, 0.70], oct_max: 0.05, sounding: [0.75, 0.85], match1_max: 0.20, match2_min: 0.85, stab_pb: [5.5, 6.5], stab_len16: [7, 15], stab_beat_max: 0.20, hat_swing: [62, 68], sidechain_db: [3.6, 8], b2_share_max: 0.30}
```

---
## 6. AVOID checks

The producer split these. Only four checks reject a roll. The other thirteen are weighted biases and a penalty, not filters. A high hard-reject rate means the vocabulary is too narrow.

Hard:

- Out-of-key notes (a chromatic neighbour of an in-scale note is allowed).
- A bass attack on the same 16th as a kick, once that low-end stack is more than 35% of the bass attacks.
- Clearly broken rhythm: an empty bar in the bass or the kick lane, or a note running past the loop.
- The originality gate, including banned figures.

Soft: slot weights, density ranges, stab-vs-bass placement, phrase plans, contour templates, rest rules, and every other AVOID id. Freedom (default 0.35) widens the bands. BUSY and BOUNCE extremes, a hidden per-roll bump, and an optional chaos value raise it. Candidates are ranked by the soft penalty and sampled with temperature, not taken as the single best.

```yaml
avoid:
  - id: AV01_theory_generic          # scale-run / arpeggio lines
    test: "run of consecutive same-direction steps (|int|<=2) > 5 moves (FAIL) / > 4 (FLAG)  OR  pitch set == triad arpeggio played in order over >= 4 onsets in >= 4 bars"
    fail: true; src: "MD measured max same-direction step run per window: 1-5 moves (REF_R2 1, REF_R4/REF_B1 2, REF_R3/REF_J1/J2 5); arpeggio clause ESTIMATE (CM ban list)"
  - id: AV02_grid_locked
    test: "bass onsets: in-beat positions used at >= 10% < 3  OR  onset set of any bar in OC GENERIC_BAR_RHYTHMS (all 16ths / all 8ths / all offbeats)  OR  velocity levels == 1"
    fail: true; src: "MD (all UK windows use >= 3 positions), OC GENERIC_BAR_RHYTHMS, PB14.1#10"
  - id: AV03_robotic_timing
    test: "hats lane swing == 50.0 AND no velocity variance (sd < 3) on hats"
    flag: true; src: "MD hat swing 49.7-57.9, PATCH6 shaker 71/18/90/57"
  - id: AV04_constant_density
    test: "sd(npb over 8 bars) < 0.5  OR  max(npb)-min(npb) < 2"
    fail: true; src: "MD per-bar sd 0.9-2.67; DA sd 1.0, range 3; threshold ESTIMATE"
  - id: AV05_no_holes
    test: "no bass rest >= 4 sixteenths in 8 bars  OR  silent_slot_fraction < 0.10  OR  bar 8 lacks a rest >= 4 sixteenths"
    fail: true; src: "PB12 (1.5-beat bar-8 dropout), PB14.1#8, MD sounding <= 0.89"
  - id: AV06_root_only_drone
    test: "root share > 0.80  OR  distinct pitch classes per 8 bars < 2  OR  bass onsets per bar < 3 with all on root"
    fail: true; src: "DA 76% (max passed), CM triviality >= 2 pcs, MD min npb 3"
  - id: AV07_wandering_pitch
    test: "top-2 share < 0.55  OR  distinct pitches per bar > 5  OR  any |leap| > 15  OR  root not in top-2  OR  b2 used as non-neighbour"
    fail: true; src: "MD top2 >= 0.556 (REF_J1/J2), distinct <= 5.0, SA +15, CTR E"
  - id: AV08_identical_bars
    test: "all 8 bars identical (match@1 > 0.95 across all lags)  OR  bar 8 identical to bar 4  OR  match@4 > 0.97 including bar 8"
    fail: true; src: "MD match@1 max 0.75; PB12 bar-8 change; threshold ESTIMATE"
  - id: AV09_too_random
    test: "match@2 < 0.50 AND match@4 < 0.47"
    fail: true; src: "MD floors (0.51 / 0.47)"
  - id: AV10_stab_bass_collision
    test: "stab onset on same slot as bass onset (UK_MIN) OR > 25% of stab hits do (others)  OR  stab onset < 1 sixteenth after a bass onset  OR  hits lane onset on kick/any lane onset"
    fail: true; src: "DA, DES15.1, MD overlap; gap ESTIMATE"
  - id: AV11_on_beat_stabs
    test: "stab on-beat share > dialect stab_beat_max (hard cap 0.30)"
    fail: true; src: "CTR E <= 30%, MD"
  - id: AV12_tail_wrap
    test: "any note_off > loop_end  OR  rendered last 10 ms peak > -45 dBFS  OR  pad/vox/hit tail crosses into bar 1 on loop"
    fail: true; src: "A&R B1, CM render rule (DA -49.9 dBFS)"
  - id: AV13_reference_skeleton_copy
    test: "OC gate vs all private reference transcriptions: skel_win >= 0.55 OR skel_ngram >= 0.45 OR voicing >= 0.75 OR (int_ngram >= 0.60 AND rhythm_cov >= 0.70) -> FAIL; skel_win >= 0.40 / skel_ngram >= 0.30 / int_ngram >= 0.50 / pc_ngram >= 0.60 / voicing >= 0.50 -> FLAG"
    fail: true; src: OC TH dict
  - id: AV14_banned_figures
    test: "bass root on slots 2 AND 4 in > 4 of 8 bars (REF_R5 2&4 root figure)  OR  b3 on slot 15 tied over barline in > 4 of 8 bars"
    fail: true; src: "DES rule (REF_R4/R5 bans), CM5.7"
  - id: AV15_machinegun
    test: "> 3 consecutive 16th onsets  OR  bass npb > 9  OR  len16 median < 0.9"
    fail: true; src: "DES, MD max 8.75, MD min median 1.07"
  - id: AV16_on_kick_bass
    test: "bass on-kick share > dialect on_kick_max (hard cap 0.35)"
    fail: true; src: "CTR A1, MD"
  - id: AV17_pad_under_drop
    test: "pad sounding fraction in drop > 0.5"
    fail: true; src: "PB14.3#5"
```

---
## 7. Variation, mutation, diversity, originality

### 7.1 Mutation operators

```yaml
mutations:
  safe:   # keep every §6 check passing with high probability; apply 1-3 per derived bar (A', B')
    - shift_onset: "+-1 slot within the same beat, never onto a kick slot (UK_MIN/POP)"        # MD in-beat spread
    - swap_partner_degree: "b7 <-> b3 <-> 5 <-> 2 (dialect-allowed set)"                      # MD degree shares
    - length_bucket_change: "move one note to an adjacent length bucket"                      # MD len hist
    - ghost_toggle: "velocity 112 <-> 80 on non-first onsets"                                 # PB14.1#10
    - tail_tag: "add/remove 1 note in slots 14-16"                                             # CTR A7
    - drop_note: "remove 1 non-first onset (creates hole)"                                     # PB14.1#8
    - octave_pop: "+12 on one non-first note (only if dialect oct_max > 0)"                    # MD
    - glide_toggle: "on a step <= 3 st"                                                        # PB12
    - cell_swap: "replace one cell with another cell of the same type/length"                  # this doc §3.1
    - stab_voicing_rotate: "next voicing in the dialect set at a 2/4-bar boundary"            # SA habit 8
  style_breaking:  # never do these (or the roll must fail AV checks)
    - add_onsets_to_all_kick_slots
    - fill_all_rests (removes holes)
    - transpose_cell_by_non_scale_interval (e.g. tritone/major 3rd)
    - add_runs_of_4+_16ths
    - change_more_than_8_slots_in_non_turnaround_bar
    - stab_on_beat_or_on_bass_onset
    - change_key_or_tempo_mid_phrase
    - vox_pitch_shift_beyond_5_st
  budget_per_8bars: {safe_ops: [3, 10], bar8_extra_ops: [2, 6]}   # ESTIMATE (DA ~6 changes vs pure loop)
```

### 7.2 Diversity metrics for a batch of N rolls (N >= 1000; same dialect and knobs)

```yaml
diversity:
  per_pair_bass_onset_jaccard:      # 8-bar onset-slot sets (0-127), pairwise over the batch
    mean_max: 0.35                   # MD: cross-track ref pairs mean 0.27 (measured today, 16 windows / 9 tracks); +0.08 headroom = ESTIMATE
    p90_max: 0.45                    # MD ref p90 0.38, max 0.61; headroom ESTIMATE
  near_duplicate_rate_max: 0.02                  # pairs with onset Jaccard >= 0.90 AND same degree sequence; ESTIMATE
  unique_bar1_onset_sets_min_share: 0.30         # distinct bar-1 onset sets / N; ESTIMATE
  unique_degree_4grams_min: 200 per 1000 rolls   # ESTIMATE
  coverage:
    plans_used: "all plans in dialect, each >= 50% of its weight share"
    cell_types_used: "each dialect cell type in >= 10% of rolls"
    contour_templates_used: "each >= 5%"
    npb_histogram_entropy_bits_min: 1.5         # ESTIMATE (spread across 4-9 npb)
  per_metric_spread: "for every §5 dialect range, the batch's 10th-90th percentile must cover >= 60% of the range"   # ESTIMATE; prevents collapse to one corner
  pass_rate_target: ">= 0.60 of raw rolls pass §6 before reroll"   # ESTIMATE (sampler quality)
```

### 7.3 Originality gate (every roll, vs every private reference transcription)
- **A&R gate (OC; hard).** These are the AV13 thresholds. Calibrated 2026-10-16 (`tools/originality_check_tests.md`).
- **Composer check (CM §5; hard).**
  - Onset Jaccard per 8-bar stream must be < 0.50.
  - Degree-sequence similarity must be < 0.70.
  - FAIL if both are at or above the flag pair (Jaccard ≥ 0.40 and similarity ≥ 0.60).
- **Triviality guard.**
  - A stream must have ≥6 onsets and ≥2 pitch classes before it's compared [CM, DA scripts].
  - Shorter streams are exempt from the gate but must still pass AV06.
- **Exemplar cells.** Cells may be measured types (§3.1), but no 2-bar window may reproduce an exemplar's full onset set and pitch order. That's exactly the skel_win test.

---
## 8. 20-roll test-set scoring rubric (A&R + Maestro co-judge)

How each test set runs:
- **Rolls:** 20 per dialect, plus 20 with randomised knobs.
- **Order:** blind and shuffled. Each judge scores alone, and the two scores are averaged.
- **Machine first:** §6 and §7.3 run before the judges listen. Any machine FAIL scores 0 and isn't auditioned.

| # | criterion | 0 | 1 | 2 | weight | machine proxy |
|---|---|---|---|---|---|---|
| R1 | Groove / roll (would a DJ loop it 32 bars?) | stiff | ok | rolls | 3 | AV02/03/15 pass |
| R2 | Bass hook identity (hummable, 1-2 ideas) | generic | some | owns it | 3 | AV01/06/07 |
| R3 | Holes & space | crowded | ok | breathes | 2 | sounding vs dialect, AV05 |
| R4 | Kick/bass lock & low end | fights | ok | locked | 2 | §3.2 on-kick, sidechain |
| R5 | Phrase arc (bar 4 answer, bar 8 turnaround) | flat/random | present | lands | 2 | match@2/4, AV08/09 |
| R6 | Stab/other lanes call-response | collide | ok | conversational | 2 | AV10/11 |
| R7 | Dialect fit (sounds like chosen preset) | wrong | close | right | 2 | §5 ranges inside |
| R8 | Originality (no reference recalled) | recognisable | faint echo | own | 2 | §7.3 scores |
| R9 | Clean loop/tails/mix | clicks/wrap | minor | clean | 1 | AV12, LUFS match |

- **Score per roll:** Σ weight × score; the maximum is 38.
- **Test set PASS** needs all three:
  - median roll ≥ 26/38;
  - ≥ 14/20 rolls ≥ 22;
  - ≥ 5/20 rolls ≥ 30 ("keepers").
- **Variety:** judges also mark "sounds like another roll in this set" pairs. FAIL if more than 2 such pairs. This is the human check on §7.2.
- **Agreement:** if the two judges' per-roll scores differ by more than 8 on more than 4 rolls, re-calibrate on those rolls before deciding.

All rubric thresholds are ESTIMATE. Set them from the first test set: today's generator should score below them, and DA (scored as a roll) should score at least 30.

---
## 9. Measured vs estimated

- **Measured** (MD, CTR, PB12, DA, SA, OC):
  - all bass density, length, slot-weight, on-kick, root/top-2, distinct-pitch, interval, octave, sounding, repeat-match and sidechain numbers;
  - stab per-bar, on-beat, length and slot weights (stab stem is LOW-MED confidence);
  - vox chops per bar and length;
  - drum slot probabilities, swing and hat decay;
  - originality thresholds (A&R-calibrated).
- **Sourced or designed rather than measured** (DES, PB14 rules, PATCH):
  - hits slots and bars, pad swell cadence, pluck motif length, lane levels;
  - ban list, velocities, glide range.
- **ESTIMATE:**
  - phrase-plan weights, the max-slot-change budget, and the floors for constant density and min silence;
  - stab gap hard minimum (1 sixteenth) and the over-held-note ceiling;
  - pluck register and density outside POP;
  - all hits-lane numbers except slots and bars;
  - every diversity metric (§7.2), mutation budgets, and rubric thresholds;
  - GASKIN-dialect hat swing (MD unmeasurable) and GASKIN-dialect octave share (SA 25% vs MD 0–1%: conflict).
