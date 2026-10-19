# ROLLA roll scoring sheet (A&R)
*A&R, 2026-10-17 ET. **v2.1: GENERATOR_RULES v2 hard/soft split + Maestro's fail list + soft-recurrence gate + 4 technical hard codes (see §3).** v2 kept as `.v2_pre_gate` in the backup. v1 kept as `ROLLA_ROLL_SCORING.v1_pre_GRv2.md` in the backup. Tool: `tools/score_rolls.py`. Validation: private A&R notes.*

**What this does.** It passes or fails every generated roll (MIDI + WAV) against the measured references, then applies the ship gate: a style needs **≥ 16/20 (80%)** of its rolls to pass.

**What it can't do.** It measures. It doesn't listen. Melak said the v4 parts "sound bad" and have little variation. The variation half is measurable, and this sheet catches it (see the validation). "Sounds bad" is mostly timbre, feel and taste, so the **MELAK_KEEP ear column is REQUIRED**. The Composer and A&R fill it in by listening to every auto-passing WAV. Nothing ships on the machine verdict alone.

**Sources.**
- CTR = private cross-track rules (not in the repo)
- DES = `plugin-design/DESIGN.md`
- GR = `plugin-design/GENERATOR_RULES.md` (the Composer's spec and 38-point rubric)
- REV = A&R review §8 (private) (lane-fit features, generic flags)
- CM = `writing/COMPOSER_METHOD.md`
- OC = `tools/originality_check.py`
- CHK = A&R checklist (private)
- "refs" = this scorer's own measurement of the 17 drop sections from the 9 transcribed tracks, the 72 MIDI files (validation §1)
- The genre vocabulary (private) is **not** used. It scores against the reference MIDI only, and only the reviewed REV §8 recommendations were folded in.

---
## 1. How to run
```
python3 tools/score_rolls.py <dir_of_rolls> --style ukmin|long|pop|gaskin [--out DIR] [--bars 8] [--bpm 130] [--no-orig] [--hard-profile maestro|gr_v2]
python3 tools/score_rolls.py --tally <out>/scores_<style>.csv     # after the judges fill in their columns
```
- **Originality check needs references:** pass `--refs DIR` or set `ROLLA_REFS=DIR` (a private folder of reference MIDI, one sub-folder per generic ref id). Without it, pass `--no-orig`.
- **What counts as a roll:**
  - a `.mid` with a same-stem `.wav`, or
  - a sub-folder of `.mid` files (merged) plus its `.wav`.
- **Part detection doesn't trust names:**
  - **Drums:** channel 10, or a drum-like track name.
  - **Kick:** GM 35/36, else the lowest drum note.
  - **No drums:** a 4/4 kick is assumed and the roll gets the soft code KICK_ASSUMED.
  - **Bass:** the lowest melodic part. A part named bass/sub wins.
  - **Stab:** a part named stab/chord/key/organ/piano/hit, else the remaining melodic part.
  - **Ignored:** vox/pad/lead/string parts.
  - File names count only when a roll is a folder of per-part files.
- **Outputs:** `scores_<style>.csv` (one row per roll, every metric, R1–R9 proxy flags, judge columns, MELAK_KEEP) and `report_<style>.md` (verdict, batch diversity, code histogram, per-roll table, details).
- **Cost:** about 2–5 s per roll with originality on, peak memory under 200 MB. Run styles one at a time.

## 2. Per-roll sheet (one row per roll; the CSV has these columns)
> **v2 note:** "hard" in the PASS-range column below is the threshold that raises the code. Whether the code *fails* a roll is decided only by §3: off-kick ≥ 65 (BASS_ON_KICK) is hard, the other ranges raise soft codes. New v2 columns: `key_tonic_pc`, `bass_out_of_key_pct`, `bass_out_nonneighbour_pct`, `stab_out_of_key_pct`, `stab_out_on_downbeat`, `out_of_key_why`, `empty_bars`, `soft_count`, `batch_skeleton_gate`, `SURPRISE_GROOVES (Y/N, judges)`.

| Block | Field | How it's measured | PASS range (style ukmin unless noted) | Source |
|---|---|---|---|---|
| **Groove** | bass in-beat positions used ≥10% | onset slot mod 4 | ≥ 3 of beat/e/and/a | GR AV02; refs 3–4 |
| | generic bars (all 8ths/16ths/offbeats) | OC GENERIC_BAR_RHYTHMS | ≤ half the bars | GR AV02, OC |
| | density per bar: mean, sd, range | attacks per bar | 1.5–9.0; sd ≥ 0.5 **and** range ≥ 2 | CTR C (REF_R1 8.25), GR AV04; refs 3.9–8.6, sd 0.66–2.33, range 2–7 |
| | max run of consecutive 16th attacks | | ≤ 7 | CM 2a.11; refs max 7 (REF_R1) |
| | bass swing / hat swing | odd-16th offset | bass ≤ 58.5; hat 49–59.5 | CTR A8; refs hats 49.1–59.1 |
| **Bass/kick interlock** | off-kick attack % | attack > 0.3 sixteenth from a kick | hard ≥ 65 (on-kick ≤ 35); lane ≥ 72 (on-kick ≤ 28) | CTR A2, REV §0.1, GR AV16 cap; refs 68.4–98.8 |
| | gaps: sounding fraction, longest rest | 16th grid | sounding ≤ 0.975; at least one silent 16th | CTR C; refs 0.36–0.97 (REF_R6 0.99–1.0 = outlier) |
| | note length median (16ths), share ≥ 6/16 | sounding length (cut at next attack) | 1.0–2.5 (lane 1.0–2.4); long notes < 15% | CTR A4/E3 1.1–2.3; REV; refs 1.07–2.31 |
| **Stab placement** | on-beat % | onsets on slot 1/5/9/13 | ≤ 33 (lane ≤ 30) | CTR A9/E7; refs 3.6–28.4 |
| | pcs per hit (median) | | ≤ 4 (lane 1–4) | CTR A9 1–3; DES rule 17 (quartal 4) |
| | on a bass attack % | same 16th as a bass onset | ≤ 80 | refs 23.9–74.8 (REF_R4 counter-riff 74.8) |
| | in bass rests % | bass silent at that 16th | **bonus only** (≥ 50% → `BONUS_STABS_IN_RESTS`) | REV §8.9, CHK §1; refs 0–56.6, median 25 |
| | density mode | hits per bar | dub 0.5–3 or riff 4–10.5; 3–4 = "generic" | REV §0.2; refs 0.5–10.38 |
| **Genre feel** | register (bass median MIDI) | | 20–45 hard; lane 25–36 (C#1–C2) | CTR C, REV |
| | pitch classes ≥5% of time / top-2 share / root share | time-weighted | ≤ 6 / ≥ 55 (lane ≥ 58) / ≤ 80 | CTR A3/E2, GR AV06, REV; refs pcs 2–6, top-2 57.3–98.4, root ≤ 67.9 |
| | distinct pitches per bar | | 1.3–5.2 | CTR A3 1.9–4.8; refs 1.94–5.0 |
| | octave moves % | \|interval\| = 12/24 | ≤ 26 (lane 0–8.5 or 15–26, bimodal) | REV §0.3, CTR B; refs 0–25.6 |
| | largest leap | semitones | ≤ 19 | refs 17 (REF_D2), 19 (REF_B1); GR says 15 |
| | same-direction step run | moves of ≤ 2 st | ≤ 5 | GR AV01; refs ≤ 3 |
| | 2/4-bar repetition (GR match@L) | sounding pitch-or-rest equal L bars later | not (match@2 < 0.50 and match@4 < 0.47) | GR AV09; refs 0.536–0.863 / 0.469–0.917 |
| | 1-bar repetition | | match@1 ≤ 0.95 and grid rep1 ≤ 0.90, > 1 distinct bar | CTR A7, GR AV08; refs ≤ 0.75 |
| | variation position | share of bar-to-bar differences in slots 9–16 | **tendency**: soft flag if < 50 | REV/CHK (median 63, refs 32–88); replaces "last 2–4 16ths" |
| **Variation (batch)** | pairwise bass skeleton similarity | OC skeleton_window, both directions, 2/4-bar windows | < 0.55 to every earlier roll; duplicate ≥ 0.80 | OC skel_win FAIL 0.55; refs: only a track's own two drops reach ≥ 0.55 |
| | pairwise stab similarity, distinct bar-1 rhythms/voicings | Jaccard of slot mod 32 + voicing | report | |
| | GR onset Jaccard mean / p90 | 8-bar onset sets | report vs GR 0.35 / 0.45 | GR §7.2 (ESTIMATE) |
| | static loop within the roll | see 1-bar repetition | | |
| **Originality** | OC vs every reference MIDI | OC thresholds; skeleton-only FAILs against LOW-confidence CQT streams are downgraded to FLAG | no FAIL | OC, GR AV13 |
| | banned figures | root on slots 2+4 in > half the bars; b3 on slot 15 tied over the barline in > half | none | GR AV14, CM 5.7 |
| **Technical** | MIDI: notes past loop end, stuck notes | | none | CHK §4.1/4.4, DES rule 24 |
| | WAV: tail into bar 1 | first 16th after loop end vs bar 1's first 16th | ≤ −20 dB relative, and ≤ −40 dBFS if bar 1 starts silent | CHK §4.1 |
| | WAV: clipping, clicks | samples ≥ 0.999; step discontinuity > 10× local RMS of Δ and > 0.02 | none | validated on synthetic faults |
| | WAV: duck depth (bass-only WAV) | < 150 Hz level just after each beat vs the beat's loudest 40 ms | lane ≥ 17 dB (Gaskin ≤ 10) | CTR A5, REV §8.12 (approximate proxy) |
| **Lane fit** | 12 features, range membership | F1 off-kick · F2 top-2 + pcs · F3 length + long-note share · F4 density + run + e/and/a beats · F5 register · F6 loop repeat · F7 octaves · F8 stab on-beat/pcs/mode/signposts · F9 stab vs bass · F10 kick 4/4 + clap 2&4 · F11 hats offbeat/not 16ths/swing · F12 duck | **≥ 10 of 12** (≤ 2 misses among the measurable ones) | REV §8; never reference similarity |
| **Ear (REQUIRED)** | `MELAK_KEEP` (Y/N), `composer_ear_note`, `ar_ear_note`, `judgeA_total_of_38`, `judgeB_total_of_38` | Composer + A&R listen to every auto-passing WAV | Y = "Melak would keep and play this" | CHK §1 taste table, GR §8 rubric |

**Style profiles** (overrides of ukmin):
- **long** (REF_D2 anchor): note median 1.8–8.0 (lane 1.8–4.0), notes per bar 1.0–7.5, long notes allowed.
- **pop** (REF_B1 anchor): off-kick ≥ 85, median length 0.8–1.6 (lane 0.9–1.4).
- **gaskin** (CTR D):
  - hard rules: off-kick ≥ 65; GR match@1 ≤ 0.20 (CALL_ANSWER_MISSING); stab on-beat ≤ 50; stab clash not checked; sounding ≤ 0.97; swing up to 60 with no SWING_90S;
  - own lane envelope: top-2 ≥ 55, pcs 2–5, length 1.5–2.6; loop = rep1 ≤ 0.10 + rep2 ≥ 0.70 + dotted-grid share ≥ 55% + b2 ≥ 10% of time; sustained stabs ≥ 8 sixteenths; duck ≤ 10 dB.

## 3. Hard-fail rules (GENERATOR_RULES v2 + Maestro, 2026-10-17; v2.1 5:12 PM ET)
A roll FAILS only on these codes (default `--hard-profile maestro`):

| Rule | Code(s) |
|---|---|
| GR v2 hard 1: key | OUT_OF_KEY |
| GR v2 hard 2: kick/bass clash | BASS_ON_KICK (bass attacks on the kick > 35%, every style) |
| GR v2 hard 3: broken/empty bars | EMPTY_BAR (NO_BASS also emits it) |
| GR v2 hard 4: originality | COPY_OF_REF |
| Maestro: dialect | LANE_FIT_LOW |
| Maestro: duplicates | DUPLICATE_OF_ROLL, SAME_SKELETON_AS_ROLL, and the **batch skeleton gate** (batch-level FAIL) |
| v2.1 Maestro: render faults | CLIPPING, CLICKS, TAIL_INTO_BAR1, STUCK_NOTE |
| v2.1 Maestro: systematic bias | **soft-recurrence gate** (batch-level FAIL): any one soft code on > 40% of the batch's rolls, batches of ≥ 10. Informational codes (KICK_ASSUMED, NO_STAB, BONUS_STABS_IN_RESTS, WAV_UNREADABLE), REF_FLAG and STAB_OUT_OF_KEY don't count. The offending codes and rates are in the report verdict and the `soft_recurrence_gate` CSV column |

**Everything else is soft**, including all generic-house flags (organ 8ths, 16th rolls, reese, offbeat pump, piano offbeats, 16th hats, 90s swing, FX, Latin bed) and TAIL_PAST_LOOP. A single roll may break any soft rule; a batch may not break the same one on most rolls (soft-recurrence gate). Soft codes show as `soft:CODE` in the R1–R9 proxy columns (hard ones as `FAIL:CODE`). They are the generator's freedom budget: they cost points by ear, not a machine FAIL. `soft_count` per roll (excluding informational codes) is advisory.

**Ship gate (machine):** `passed / rolls ≥ 0.80` on hard codes **and** the batch skeleton gate holds (≤ 2 same-skeleton pairs at skel ≥ 0.55, ≤ 3 rolls involved) **and** the soft-recurrence gate holds. All three are in the report verdict, the exit code and `--tally`. The CSV also reports `solo_verdict` (the roll on its own, ignoring the duplicate codes) so the engineer can tell "bad roll" from "duplicate roll".

**Where this differs from GR v2 §5 (for Maestro / the Composer):**
- GR v2 §5 also lists NO_BASS, BASS_REGISTER, NO_GAPS, NO_HOLES, TAIL_PAST_LOOP and BANNED_FIGURE under its hard rules (STUCK_NOTE, TAIL_INTO_BAR1, CLIPPING, CLICKS are hard by default since v2.1). Per Maestro's instruction these six are **soft by default**. `--hard-profile gr_v2` makes them hard (opt-in, never the default).
- GR v2 H1 also covers stabs (≤ 5% out of key, none on a downbeat). The stab half is **soft** (STAB_OUT_OF_KEY), not part of OUT_OF_KEY: on the references, the stab reference MIDI are CQT peaks whose harmonics read 0–30% "out of key" (11/17 sections would fail). The bass half is validated (refs ≤ 3.0%, except REF_R6 b137 at 6.1%). Re-harden it once clean stab MIDI exists to calibrate on.
- "Chromatic neighbour" is enforced as ≤ 2% of bass time on non-neighbour out-of-scale notes. A strict "zero" fails REF_R3 main and REF_R1 main on single reference grace notes.
- The batch gate on **reference** folders is meaningless (two sections of one track share a skeleton by design); read the refs' `solo_verdict`.

## 4. Reason codes (engineer actions)
| Code | Hard? | Trigger | Engineer action |
|---|---|---|---|
| OUT_OF_KEY | **HARD** | GR v2 H1. Tonic = whichever of the 2 most-sounding bass pcs fits the dialect scale best (bass time + stab note-ons). FAIL if bass time out of scale > 5%, or out-of-scale bass notes that are **not** chromatic neighbours of an in-scale note > 2% of bass time. Scales: ukmin/long/pop {1 b2 2 b3 4 5 b6 6 b7}, gaskin {1 b2 2 b3 4 5 b6 b7}. `out_of_key_why` says which | keep the bass in the dialect scale; chromatic notes only as passing neighbours |
| EMPTY_BAR | **HARD** | GR v2 H3: a bar with no bass onset and no sounding bass note (`empty_bars` lists them); NO_BASS also counts | every bar needs a bass event |
| *(batch)* SOFT-RECURRENCE GATE | **HARD (batch)** (v2.1) | one soft code on > 40% of rolls (n ≥ 10); excludes informational codes, REF_FLAG, STAB_OUT_OF_KEY | the generator has a systematic bias: fix the named code at its source |
| *(batch)* BATCH SKELETON GATE | **HARD (batch)** | GR v2 §4.2: same-skeleton pair = skel_sim ≥ 0.55 (duplicates count); batch FAILS if > 2 pairs or > 3 rolls involved. Shown in the report verdict and the `batch_skeleton_gate` CSV column | more distinct skeletons per batch |
| ONKICK_HIGH | soft | on-kick 28–35% (the old lane value; above 35 = BASS_ON_KICK) | aim ≤ 28 |
| STAB_OUT_OF_KEY | soft (deviation from GR v2 H1, see §3) | stab out-of-scale > 5% of note-ons, or an out-of-scale stab on a downbeat | voice stabs from the scale |
| NO_BASS | soft (hard in `--hard-profile gr_v2`) [v1: hard] | no bass part, or < 4 notes (also emits hard EMPTY_BAR) | export the bass lane; check track/channel naming |
| BASS_ON_KICK | **HARD** | on-kick > 35% (off-kick < 65%), every style (GR v2 H2) | shift attacks off slots 1/5/9/13 (±1 slot within the beat); keep held-through-kick notes |
| TOP2_LOW | soft [v1: hard] | top-2 pcs < 55% of bass time | draw pitch from 2-pitch contour templates (root + one partner); cut passing tones |
| TOO_MANY_PCS | soft [v1: hard] | > 6 pcs each holding ≥ 5% of time | restrict pitch set to root + 1–3 partners |
| PCS_PER_BAR | soft [v1: hard] | mean distinct pitches per bar outside 1.3–5.2 | fewer pitches per bar (or more, if it's a one-note drone) |
| BASS_NOTES_TOO_SHORT / _TOO_LONG | soft [v1: hard] | median length outside the style range | change length buckets (UK 1–2.5/16; LONG up to 8) |
| NO_GAPS | soft (hard in `--hard-profile gr_v2`) [v1: hard] | bass sounding > 97.5% of the loop | add rest cells; shorten holds |
| NO_HOLES | soft (hard in `--hard-profile gr_v2`) [v1: hard] | not a single silent 16th | as above |
| NOTE_RUN | soft [v1: hard] | ≥ 8 consecutive 16th attacks | break runs; no machine-gun fills |
| OCTAVE_PUMP | soft [v1: hard] | > 26% octave moves | octave pops on ≤ 1 note per bar, or 0 (dialect) |
| BASS_REGISTER | soft (hard in `--hard-profile gr_v2`) [v1: hard] | median pitch outside MIDI 20–45 | transpose to octave 1 / low 2 |
| BASS_SWUNG | soft [v1: hard] | bass swing > 58.5% | straighten the bass (swing the hats, if anything) |
| DENSITY_LOW / _HIGH | soft [v1: hard] | notes per bar outside the style range | adjust cells per bar (2–4) |
| GRID_LOCKED | soft [v1: hard] | < 3 in-beat positions used ≥ 10%, or > half the bars are generic grids | vary opening slot and cell types (this is the v4 "same opening slot every bar" bug, GR §1) |
| CONSTANT_DENSITY | soft [v1: hard] | per-bar density sd < 0.5 or range < 2 | thin bar 4 / bar 8, add a busier answer bar |
| ROOT_DRONE | soft [v1: hard] | root > 80% of bass time | add partner degrees (b7/b3/5/2) |
| LEAP_TOO_BIG | soft [v1: hard] | a leap > 19 st | keep leaps ≤ an octave + a minor 3rd |
| SCALE_RUN | soft [v1: hard] | > 5 same-direction steps in a row | contour templates, not scale walks (CM "theory-generic") |
| LOOP_NO_REPEAT | soft [v1: hard] | GR match@2 < 0.50 and match@4 < 0.47 | build bars from a repeated A/B cell plan (GR §2 P1–P5) |
| STATIC_LOOP | soft [v1: hard] | every bar identical (match@1 > 0.95, grid rep1 > 0.90 or 1 distinct bar) | apply 1–3 safe mutations per derived bar; bar-8 turnaround |
| CALL_ANSWER_MISSING | soft [v1: hard (gaskin)] | GR match@1 > 0.20 | make bar B answer bar A (♭7→1 / ♭2→♭3) |
| STAB_ON_BEAT | soft [v1: hard] | stab on-beat > 33% (gaskin > 50) | move stabs to e/and/a |
| STAB_TOO_THICK | soft [v1: hard] | median > 4 pcs per hit | 1–3 note voicings (quartal 4 ok) |
| STAB_CLASH | soft [v1: hard] | > 80% of stab hits on bass attacks | offset stabs; use the bass's rests (bonus) |
| PIANO_OFFBEAT_CHORDS | soft [v1: hard] | ≥ 4-pc hits on ≥ 3 offbeats per bar in > half the bars | that's piano house: use dub stabs or a riff |
| KICK_NOT_4x4 | soft [v1: hard] | kick on < 80% of quarters | straight 4/4 kick |
| BASS_ORGAN_8THS | soft [v1: hard] | ≥ 6 of 8 eighths attacked (median bar) and ≥ 40% on the kick | 90s organ bass: remove kick-slot attacks, add rests |
| BASS_16TH_ROLL | soft [v1: hard] | run > 7, or > 25% of beats with e+and+a all attacked | rolling-techno density: thin to 4–8.6 per bar |
| BASS_REESE_LONG | soft [v1: hard (not long)] | ≥ 15% of notes ≥ 6/16 | shorter notes, or use the LONG dialect |
| OFFBEAT_PUMP_ONLY | soft [v1: hard] | ≥ 75% of beats "and"-only with one pitch | add e/a attacks and a partner pitch |
| HAT_16THS | soft [v1: hard] | > 10 closed hats per bar | offbeat hats; 16ths read tech-house |
| SWING_90S | soft [v1: hard (not gaskin)] | hat or bass swing 59.5–62% | straight to light swing (≤ 59); 62–66 only as a REF_R6/2-step mode |
| BUILD_FX_IN_LOOP | soft [v1: hard] | crash/riser/sweep/FX part or GM 49/52/55/57 in the loop | FX belong to arrangement, not the 8-bar drop loop |
| LATIN_PERC_BED | soft [v1: hard] | conga/bongo/timbale/clave hits ≥ 1 per bar, or a part named so | drop the Latin bed in UK mode |
| LANE_FIT_LOW | **HARD** | > 2 lane features out of range | read `lane_miss` (F1…F12) and fix those features |
| COPY_OF_REF:<ref> | **HARD** | OC FAIL vs a reference MIDI | regenerate; never ship literal reference cells (REV §0.5) |
| BANNED_FIGURE | soft (hard in `--hard-profile gr_v2`) [v1: hard] | REF_R5 2&4 root signature or b3-slot-15 tie in > half the bars | banned by CM 5.7 / DES |
| DUPLICATE_OF_ROLL:<n> | **HARD** (batch) | skeleton ≥ 0.80 and stab sim ≥ 0.75 (or exact transposition) vs an earlier roll | more cell types / plans / contours per roll (GR §7) |
| SAME_SKELETON_AS_ROLL:<n> | **HARD** (batch) | bass skeleton ≥ 0.55 vs an earlier roll | as above; the later roll of the pair is flagged |
| STUCK_NOTE | **HARD** (v2.1) | note-on without note-off | fix note-off export |
| TAIL_PAST_LOOP | soft (hard in `--hard-profile gr_v2`) [v1: hard] | a note ends after the loop end | gate notes at the loop end (DES rule 24) |
| TAIL_INTO_BAR1 | **HARD** (v2.1) | WAV energy after the loop end rings into bar 1 | shorten release/FX tail; render with a gated tail |
| CLIPPING | **HARD** (v2.1) | any sample ≥ 0.999 | lower render gain |
| CLICKS | **HARD** (v2.1) | step discontinuity in the WAV | add 2–5 ms attack/release ramps; fix buffer seams |
| KICK_ASSUMED | soft | no drum part | include drums for the full check |
| TEMPO_OUT | soft | tempo outside 128–134 | 130–132 (CTR A1) |
| VAR_NOT_IN_BACK_HALF | soft | < 50% of bar differences in slots 9–16 | move variation toward the back half |
| NO_BAR8_TURNAROUND | soft | 8-bar roll: bar 8 has no ≥ 4/16 rest, or equals bar 4 | add a bar-8 turnaround (GR §2) |
| ONE_VELOCITY | soft | < 2 velocity layers | accents + ghosts (~115 / 82) |
| NO_STAB | soft | no stab part | |
| REF_FLAG:<ref> | soft | OC FLAG | A&R listens A/B (CHK §3.4) |
| BONUS_STABS_IN_RESTS | soft (+) | ≥ 50% of stab hits in bass rests | Melak's taste: good |
| WAV_UNREADABLE | soft | WAV failed to load | re-render 16/24-bit PCM |

## 5. GENERATOR_RULES §8 rubric: one shared basis
- **Gate.** Any hard code (§3) is a machine FAIL: the roll scores 0/38 and isn't auditioned. Soft codes show as `soft:` and only inform the judges.
- **Proxies.** The CSV gives a per-criterion proxy so the judges see *why* a roll would score low.

| GR criterion (weight) | Machine proxy codes in score_rolls |
|---|---|
| R1 Groove (3) | GRID_LOCKED, NOTE_RUN, DENSITY_HIGH, BASS_SWUNG, CONSTANT_DENSITY, BASS_NOTES_TOO_SHORT, BASS_ORGAN_8THS, BASS_16TH_ROLL, OFFBEAT_PUMP_ONLY, HAT_16THS, SWING_90S, KICK_NOT_4x4 (GR: AV02/03/15) |
| R2 Bass hook (3) | NO_BASS, OUT_OF_KEY (hard), SCALE_RUN, ROOT_DRONE, TOP2_LOW, TOO_MANY_PCS, PCS_PER_BAR, LEAP_TOO_BIG, OCTAVE_PUMP, BASS_REGISTER (GR: AV01/06/07) |
| R3 Holes & space (2) | EMPTY_BAR (hard), NO_GAPS, NO_HOLES, DENSITY_LOW, BASS_NOTES_TOO_LONG, BASS_REESE_LONG (GR: AV05) |
| R4 Kick/bass lock (2) | BASS_ON_KICK (hard), soft ONKICK_HIGH, KICK_NOT_4x4; duck depth in lane F12 (GR §3.2) |
| R5 Phrase arc (2) | LOOP_NO_REPEAT, STATIC_LOOP, CALL_ANSWER_MISSING; soft NO_BAR8_TURNAROUND, VAR_NOT_IN_BACK_HALF (GR: AV08/09) |
| R6 Stab call-response (2) | STAB_ON_BEAT, STAB_TOO_THICK, STAB_CLASH, PIANO_OFFBEAT_CHORDS; bonus BONUS_STABS_IN_RESTS (GR: AV10/11) |
| R7 Dialect fit + surprise (2) (GR v2) | LANE_FIT_LOW (hard), LATIN_PERC_BED, BUILD_FX_IN_LOOP + any R1–R6 code under the chosen style. Surprise is judged by ear: judges mark **SURPRISE_GROOVES Y/N** per roll (CSV column); a set needs ≥ 4/20 Y |
| R8 Originality (2) | COPY_OF_REF, BANNED_FIGURE; soft REF_FLAG (GR: §7.3, AV13/14) |
| R9 Clean loop (1) | TAIL_PAST_LOOP, TAIL_INTO_BAR1, STUCK_NOTE, CLIPPING, CLICKS (GR: AV12) |
| Variety (set) | DUPLICATE_OF_ROLL, SAME_SKELETON_AS_ROLL (hard, later roll of a pair) + GR v2 §4.2 batch gate (> 2 pairs or > 3 rolls = batch FAIL) |

**GR AV checks not machine-checked here:**
- AV03 robotic hats (needs the hat velocity sd; only partly covered by F11).
- AV17 pad under drop (pads are ignored).
- The hits/vox lanes.

### 5.1 Where the measured references disagree with GR's thresholds (feedback for the Composer)
All the numbers below are this scorer's measurements of the 13 UK + 2 JACKIN + 2 REF_R6 drop sections (refs). "Fails" means a real reference would be rejected by the GR number.

| GR rule (GR value) | refs measured | Effect | This sheet uses |
|---|---|---|---|
| §5 UK_MIN on_kick_max 0.10; AV16 | on-kick 1.2–31.6%; only REF_B1 (1.2/2.3) and REF_R5 (9.5) ≤ 10 | fails 11 of 13 UK sections | lane ≤ 28 (REV), hard ≤ 35 (GR's own AV16 cap) |
| §5 UK_MIN stab_beat_max 0.10 | 3.6–28.4% | fails 9/13 | ≤ 30 lane, 33 hard (CTR A9) |
| §5 UK_MIN stab_pb 0.5–3 | 0.5–10.38 | only 3/13 inside (REF_R5, REF_D2 main, REF_R2 main) | two modes: dub 0.5–3, riff 4–10 (REV) |
| AV10 UK_MIN stab on bass onset forbidden | 23.9–74.8% of stab hits | fails 13/13 | > 80% fails; in-rests is a bonus |
| AV15 > 3 consecutive 16ths | max run 7 (REF_R1 7/6, REF_R3 4) | fails 3/13 | > 7 fails |
| AV05 rest ≥ 4/16 somewhere | longest rest 1–37; < 4 in REF_R1 ×2, REF_R4/R5 ×2, REF_D2 b49, JACKIN ref b1-15, REF_R6 ×2 | fails 8/17 | ≥ 1 silent 16th + sounding ≤ 0.975; bar-8 hole kept as soft NO_BAR8_TURNAROUND |
| AV07 max leap 15 | REF_D2 17, REF_B1 19 | fails 2/13 | ≤ 19 |
| §5 UK_MIN oct_max 0.10 | REF_R2 16.2/21.2, REF_R6 16.7 | fails REF_R2 ×2 | 0–26 (REV bimodal) |
| §5 UK_MIN match4_min 0.77 (GR metric) | REF_R3 b137 0.625, REF_R1 b145 0.682, REF_D2 main 0.755, REF_R2 b41 0.469 | fails 4/13 | AV09 floor (0.50 @2 or 0.47 @4) |
| §5 UK_MIN match2_min 0.60 | REF_R3 b137 0.562, REF_R1 b145 0.598 | fails 2/13 | as above |
| §4/§5 LONG sounding 0.75–0.90 | REF_D2 0.93/0.97 | fails the LONG anchor itself | ≤ 0.975 |
| §3.1 npb UK_pool 4.38–8.75 | 3.94–8.62 (REF_R3 b137 3.94) | small | 3.9–8.7 lane |
| §7.2 diversity mean onset Jaccard ≤ 0.35 | v4 mockup POP batch: mean 0.30 (passes GR) yet p90 0.54 and 16/20 rolls share a skeleton | GR's mean target misses the v4 problem | per-pair skeleton test (≥ 0.55 = same) |
| AV02, AV04, AV06, AV08, AV09, AV14, Gaskin match@1 ≤ 0.20 | refs pass (positions 3–4; sd 0.66–2.33 / range 2–7; root ≤ 67.9; match@1 ≤ 0.75; JACKIN refs 0.008/0.161) | agree | adopted as-is |

## 6. Per-style summary format (report header + this block in the hand-off)
```
STYLE <name>   rolls 20   auto PASS n/20 (xx%)   solo PASS n/20   gate 80%   batch skeleton gate PASS|FAIL (pairs p/2, rolls r/3)  -> SHIP-CANDIDATE | NO-SHIP
Top hard codes: CODE xN, ...   Top soft codes: CODE xN, ...  median soft/roll x   soft-recurrence gate PASS|FAIL (CODE k/20=xx%, ...)
SURPRISE_GROOVES (judges): k/20 (need >= 4)
Batch diversity: mean/max skeleton sim a/b; pairs >=0.55: k; GR onset Jaccard mean/p90 c/d; distinct bar-1 rhythms m/20
Lane fit: median x/12; rolls < 10: k
Originality: FAIL k, FLAG k (worst ref ...)
Technical: tails k, clicks k, clipping k
EAR (required): MELAK_KEEP Y on k of the auto-passing rolls; judges' 38-pt median x; keepers (>=30) k
--tally verdict: SHIP to Melak | NO-SHIP
```
**A style ships to Melak only when all of these hold:**
1. auto-pass ≥ 80% (hard codes only) **and** the batch skeleton gate holds **and** the soft-recurrence gate holds;
2. MELAK_KEEP = Y on ≥ 80% of the 20 rolls (auto-pass AND ear);
3. GR v2 §5 holds: median ≥ 26/38, ≥ 14/20 at ≥ 22, ≥ 5/20 at ≥ 30, judges agree, ≥ 4/20 SURPRISE_GROOVES = Y.

**Advisory line (not in any verdict):** `advisory_rolls_with_4plus_soft` (rolls carrying ≥ 4 counted soft codes; a backstop to read, not a gate).

`score_rolls.py --tally` checks every condition.
