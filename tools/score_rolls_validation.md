# score_rolls.py validation log
*A&R, 2026-10-17 ET. Every number here is a MIDI/WAV measurement. **Nobody listened for this validation.***
- **Re-run:** `bash tools/_validation/run_all.sh`. It runs one set at a time, takes about 6 min, and peaks under 200 MB RAM.
- **Outputs:** `tools/_validation/out/<set>/`.
- **Generators:**
  - `_validation/make_generic_set.py`
  - `_validation/make_technical_set.py`
  - the random set: inline in this log's history, seed 7; files in `neg_random/`
  - the ROLLA mockup rolls: exported from `mockup_v4/index.html` `rollAll()` + `midiParts(['bass','stab'])` through headless Chrome (`/home/box/ar-backup/rolla_render/cdp_eval.mjs`)

## Result
| Set | What | Rolls | Auto PASS (gate) | Solo PASS (no batch codes) | Target | Met? |
|---|---|---|---|---|---|---|
| pos_refs_ukmin | UK refs: 13 drop sections, 7 tracks, plus Ellia ×2 (CTR outlier), `--exclude-self` | 15 | 6 | **12** | refs pass ≥ 85% | see §1 |
| pos_refs_gaskin | Gaskin Wax Master ×2, style gaskin | 2 | 1 | **2** | | ✅ |
| pos_anchor_long | LIGHTLEAK ×2, style long | 2 | 1 | **2** | | ✅ |
| pos_anchor_pop | M-High ×2, style pop | 2 | 1 | **2** | | ✅ |
| neg_rejects | v1 #1, v2 A/B/C, v3 C2–C5 + R1–R4, v4 idea1–3 (bass + stab) | 15 | 0 | 0 | all fail | ✅ |
| neg_random | random-note bass + stab over a 4/4 kick | 20 | 0 | 0 | all fail | ✅ |
| neg_rolla_mockup_ukmin | v4 generator, UK MIN dialect | 20 | 2 | 3 | style no-ship | ✅ (10%) |
| neg_rolla_mockup_longnote | v4 generator, LONG | 20 | 0 | 2 | no-ship | ✅ |
| neg_rolla_mockup_pop | v4 generator, POP | 20 | 0 | 0 | no-ship | ✅ |
| neg_rolla_mockup_gaskin | v4 generator, GASKIN | 20 | 0 | 0 | no-ship | ✅ |
| neg_generic | 1 control + 9 generic-house rolls (one per REV §8 flag) | 10 | 1 (control) | 1 | each flag fires | ✅ |
| neg_technical | the control roll + WAV render, with one fault each | 7 | 1 (T1) | 2 (T1, T2) | each fault fires, clean ones pass | ✅ |

**Why auto PASS is low on the reference sets.** It's expected and correct:
- Each track's main drop and second drop share a bass skeleton, so the later drop of each pair gets SAME_SKELETON_AS_ROLL.
- Those 7 UK pairs + 1 Gaskin pair + LONG/POP anchor pairs are the **only** pairs at ≥ 0.40. No two *different* records reach 0.40.
- So the diversity detector separates "same song" from "same genre", and the solo column is the positive-control number.

## 1. Positive control (references)
**Per section (solo verdict, lane fit, originality with self-exclusion):**

| Section | Solo | Lane fit | Hard codes | Originality |
|---|---|---|---|---|
| Josh b137-152 / main | PASS / PASS | 10/11, 11/11 | – | PASS / PASS |
| Laidlaw b145-160 / main | PASS / PASS | 11/11, 11/11 | – | FLAG (M-High bass) / PASS |
| LIGHTLEAK b49-55 / main | PASS / PASS | 11/11, 11/11 | – | FLAG / FLAG |
| Local Dub V Line b52-64 / main | PASS / PASS | 11/11, 11/11 | – | PASS / PASS |
| Local Dub Reaction | FAIL | 9/9 | BANNED_FIGURE (by design: the 2&4 root figure *is* Reaction, banned verbatim in CM 5.7), TAIL_PAST_LOOP (excerpt cut mid-chord) | FLAG |
| M-High b161-176 / main | PASS / PASS | 11/11, 11/11 | – | FLAG (Melak's Unity clip) / FLAG |
| Stef b41-48 / main | PASS / PASS | 9/11, 11/11 | – | FLAG / FLAG |
| Gaskin b1-15 / main (gaskin) | PASS / PASS | 11/11, 11/11 | – | FLAG / PASS |
| Ellia b137-152 / main (ukmin) | FAIL / FAIL | 10/11, 9/11 | NO_GAPS (bass sounding 0.99–1.00) | PASS / PASS |

(Lane fit is out of 11: there's no reference WAV, so F12 is n/a.)

**Pass rates:**
- **In-scope references (13 UK + 2 Gaskin = 15): 14/15 solo = 93%.** The only fail is Reaction, which trips the copy-ban rule written against it.
- On the musical rules alone (no originality-class or excerpt-technical codes): **15/15 = 100%**.
- **All 17 sections including Ellia:** 14/17 = 82% solo, **15/17 = 88% on musical rules**. Ellia fails only NO_GAPS. CTR lists her as a swung bass-house outlier, and no ROLLA dialect targets her.
- **Lane fit (REV §8):** 17/17 sections have ≥ 10 of 12 features in range (at most 2 misses).
- **Originality vs other references:** 0 FAIL, 9 FLAG. Reference-vs-reference skeleton flags are normal genre overlap; FLAG is soft.
- **Dialect anchors:** LIGHTLEAK under `long` 2/2; M-High under `pop` 2/2.

## 2. Negative control
- **v1–v4 rejects: 15/15 fail.**
  - Top codes: BASS_NOTES_TOO_SHORT 12, LANE_FIT_LOW 10, GRID_LOCKED 9, OCTAVE_PUMP 8 (the "octave roller"), TAIL_PAST_LOOP 7, BASS_16TH_ROLL 3.
  - The three v4 Local Dub ideas all get **COPY_OF_REF:localdub_reaction** + BANNED_FIGURE. That's the documented reason Melak rejected them.
  - v3 R3/R4 also get DUPLICATE_OF_ROLL of v2 C (same driving skeleton).
- **Random notes: 20/20 fail** on TOP2_LOW 20, LEAP_TOO_BIG 20, LOOP_NO_REPEAT 20, LANE_FIT_LOW 20 and TOO_MANY_PCS 19.
- **ROLLA v4 mockup generator: 2/80 auto-pass (solo 5/80).** Every dialect is NO-SHIP. This is Melak's "minimal variation", measured:
  - SAME_SKELETON_AS_ROLL on 12/20 (UK), 13/20 (LONG), 16/20 (POP) and 19/20 (GASKIN) rolls.
  - Pairs ≥ 0.55 skeleton similarity: 29, 46, 53 and 66 of 190. The reference set has **0** cross-track pairs ≥ 0.40.
  - Only 2 distinct stab voicings per dialect, and 1–2 bass partner pitch classes.
  - GRID_LOCKED on 7/15/7/0 rolls: the same opening slot every bar, as GR §1 diagnoses.
  - ROOT_DRONE on 4/4/7/0.
  - **COPY_OF_REF on 12/80.** POP copies M-High in 6 rolls, UK MIN hits Laidlaw/M-High in 3, LONG hits Stef/LIGHTLEAK in 2, and one POP roll hits Melak's Unity clip.
  - The GASKIN dialect fails CALL_ANSWER_MISSING 20/20 (GR match@1 0.26–0.42 vs refs 0.008/0.161).
  - Most UK MIN rolls pass lane fit on their own (lane fails 0/20). Their failures are diversity, note length and grid-locking, not lane features.
- **Generic-house set:** the control passes. Each flag fires on its own roll:
  - Organ 8ths → BASS_ORGAN_8THS
  - 16th roll → BASS_16TH_ROLL
  - Reese → BASS_REESE_LONG
  - Offbeat pump → OFFBEAT_PUMP_ONLY
  - Piano offbeats → PIANO_OFFBEAT_CHORDS **only**
  - 16th hats → HAT_16THS **only**
  - 90s swing (60.4%) → SWING_90S (+ BASS_SWUNG)
  - Crash + riser → BUILD_FX_IN_LOOP **only**
  - Conga/clave bed → LATIN_PERC_BED **only**
- **Technical set** (control roll, numpy render, one fault each):
  - T1 clean and T2 clean + kick/hats PASS the technical checks.
  - T3 → CLIPPING, T4 (no attack/release ramps) → CLICKS, T5 (note ringing past the loop) → TAIL_INTO_BAR1, T6 → TAIL_PAST_LOOP, T7 → STUCK_NOTE.
  - (Their auto verdicts also carry DUPLICATE_OF_ROLL, because they're the same MIDI.)
- **Click false positives on real renders:** 0 clicks on 4 WebAudio renders from the mockup_v4 review (full-loop demo, bass stem, stab stem, all-16 + kick re-render).

## 3. Tuning history (what changed and why)
1. **v0 → kick cleaning.** Transcribed drum MIDI has stray off-beat "kicks" (detector false positives). Off-beat kick hits are now kept only if the slot recurs in ≥ 50% of bars.
   - Off-kick then reproduces CTR A2: M-High 97.7 (CTR 97.7), Laidlaw 72.0 (72.0), Local Dub 74.8 (74.2), Stef 80.2 (79.0), Josh 84.1 (86.2), LIGHTLEAK 70.3 (73.9).
2. **Pitch-class count** = pcs holding ≥ 5% of bass time, so transcription grace notes don't count. Refs 2–6.
3. **Loop repeat → GR match@L** (sounding pitch or rest equal L bars later) with GR AV09 floors.
   - My first grid metric gave Stef b41-48 0.405 although it loops (GR metric 0.76).
4. **Thresholds widened only to the measured reference envelope**, with every value cited in the sheet:
   - top-2 ≥ 55 (Josh 2nd drop 58.6)
   - pitches per bar ≤ 5.2 (Local Dub 5.0)
   - 16th run ≤ 7 (Laidlaw 7)
   - octaves ≤ 26 (M-High 25.6)
   - sounding ≤ 0.975 (LIGHTLEAK 0.97)
   - kick 4/4 ≥ 0.80 (transcription misses; refs 0.83–0.98)
   - leap ≤ 19 (M-High 19)
   - stab clash ≤ 80 (Local Dub 74.8)
5. **Originality:** a skeleton-only FAIL against a LOW-confidence CQT stream is downgraded to FLAG.
   - Gaskin's stabs had "failed" vs LIGHTLEAK's chops at skel_win 0.60.
   - Real copies (the v4 ideas vs Reaction's bass, the mockup POP rolls vs M-High's bass) still FAIL.
6. **Click detector:** switched from the second difference to a first-difference step test.
   - The second difference flagged every kick attack in T2 (6 false clicks). The step test gives 0 on T2 and on real renders, and still catches T4.
7. **Part detection:** a single-file roll's file name (e.g. "…_with_kick.mid") no longer decides drum/bass parts.
8. **GENERATOR_RULES AV checks added** as codes: GRID_LOCKED, CONSTANT_DENSITY, NO_HOLES, ROOT_DRONE, LEAP_TOO_BIG, SCALE_RUN, BANNED_FIGURE, CALL_ANSWER_MISSING. Each was calibrated on the refs; disagreements with GR are in sheet §5.1.
9. **AR_REVIEW §8 folded in:**
   - 12-feature lane fit (≥ 10/12) and the 10 generic flags (all hard).
   - Stab density modes; stabs-in-rests as a bonus; back-half variation as a tendency (soft at < 50%); bass on kick up to 28% in lane F1; octaves 0–25 (bimodal).
   - Where REV's own drops fall outside a REV number, the range is widened to the measured envelope (from `ar_vocab_measure.json`):
     - top-2 58 (58.6)
     - pcs ≥ 5% up to 6
     - length 1.0–2.4 (1.07–2.31)
     - density 3.9–8.7 (3.94–8.62)
     - hat swing ≤ 59.5 (49.1–59.1; Laidlaw 58.2 would have tripped SWING_90S at REV's 58)
     - stab pcs ≤ 4
     - riff-mode "≥ 70% on e/a" reported, not required (refs 27.8–75%)
   - Without these widenings, 7 of the 15 ukmin-scored sections failed lane fit (Ellia ×2, Josh b137, Laidlaw b145, Local Dub main, M-High b161, Stef b41).

## 4. Known limits
- **Ear:** "sounds bad" (timbre, mix, feel) is not measured. The MELAK_KEEP column is required.
- **Reference stabs are LOW–MED confidence CQT peaks.** Stab rules are deliberately loose (on-beat, pcs, clash ≤ 80, mode) and only tighten via the ear.
- **F12 duck depth** is an approximate proxy (low band after the beat vs the beat's loudest 40 ms). It's measured only on WAVs without a drum part and wasn't calibrated on reference audio. The v4 bass stem reads 4.6 dB. If the engineer's WAVs have no sidechain, F12 will miss on every roll, which uses one of the 2 allowed misses.
- **TAIL_INTO_BAR1 needs the WAV rendered as exactly the loop plus a free tail.** A WAV of a longer arrangement will false-flag (the v4 full-track demo did).
- **Bar phase on refs is low-confidence for Stef and Laidlaw** (CTR). Generated rolls start at tick 0, so this only affects the positive control.
- **The genre vocabulary (`PATTERN_VOCAB.json`) is not used**, per Maestro. Only the reviewed REV §8 recommendations are used.

---
# v2: GENERATOR_RULES v2 + Maestro's fail list (2026-10-17, ~5:30 PM ET)

## What changed in score_rolls.py
- **Hard set (default `--hard-profile maestro`):**
  - OUT_OF_KEY, BASS_ON_KICK (> 35% on-kick, now every style; pop was 70% off-kick, now 65%), EMPTY_BAR, COPY_OF_REF;
  - plus LANE_FIT_LOW, DUPLICATE_OF_ROLL, SAME_SKELETON_AS_ROLL;
  - plus the **batch skeleton gate** (GR v2 §4.2: > 2 pairs at skel ≥ 0.55, or > 3 rolls involved, is a batch-level FAIL; included in the summary verdict and the exit code).
  
  Every other code is soft, including all generic-house flags and the technical faults.
- `--hard-profile gr_v2` (opt-in only) adds GR v2 §5's own extra hard list: NO_BASS, BASS_REGISTER, NO_GAPS, NO_HOLES, STUCK_NOTE, TAIL_PAST_LOOP, TAIL_INTO_BAR1, CLIPPING, CLICKS, BANNED_FIGURE. This is a **discrepancy between GR v2 §5 and Maestro's list**, flagged for Maestro rather than resolved silently.
- **New codes:**
  - OUT_OF_KEY (hard): the bass half of GR v2 H1. The tonic is whichever of the top-2 bass pcs fits the scale best. It fails if bass time out of scale is > 5%, or if out-of-scale bass notes that are not chromatic neighbours of an in-scale note exceed 2% of bass time.
  - EMPTY_BAR (hard): a bar with no bass onset or sounding bass note. NO_BASS also emits it.
  - ONKICK_HIGH (soft): on-kick 28–35%.
  - STAB_OUT_OF_KEY (soft): the stab half of H1.
- **Output changes:**
  - R1–R9 proxy columns: `FAIL:` for hard hits, `soft:` for the rest.
  - R7 is now `R7_dialect_surprise`.
  - New CSV columns: `soft_count`, `batch_skeleton_gate`, `SURPRISE_GROOVES (Y/N, judges)`, plus the key/empty-bar metrics.
  - `--tally` now checks the batch gate and ≥ 4/20 SURPRISE_GROOVES (the old "pairs ≤ 2" line is replaced by the gate).
  - Report: advisory-only lines `advisory_soft_recurrence` and `advisory_rolls_with_4plus_soft`. These are not in any verdict.
- **Two deviations from the GR v2 text, both softer than written:**
  1. **Stab key check is soft.** Reference stabs are CQT transcriptions, and their harmonics read 0–30% out of key (11/17 refs would fail), so the rule can't be calibrated yet.
  2. **"Chromatic neighbour" is ≤ 2% of bass time, not zero.** Josh main and Laidlaw main each have a single non-neighbour grace note from transcription.
- AR_CHECKLIST.md §2c: "any one generic flag is a FAIL" is now soft (annotated in place, v1 wording kept).

## v2 results (full run, sequential, about 7 min, < 200 MB)
| set | n | batch pass | **solo pass** | batch gate | hard codes that catch | v1 solo |
|---|---|---|---|---|---|---|
| refs UK (incl. Ellia, LIGHTLEAK, M-High) | 15 | 6* | **13 (87%)** | FAIL* | OUT_OF_KEY 1 (Ellia b137, 6.1% out of scale), EMPTY_BAR 1 (Josh b137 bar 14, a real 37/16 drop-out) | 12 (all 17 refs 82%) |
| refs Gaskin | 2 | 1* | **2** | PASS | n/a | 2 |
| anchor LONG / POP | 2 / 2 | 1 / 1* | **2 / 2** | PASS | n/a | 2 / 2 |
| **all 17 refs** | 17 | n/a | **15 (88%)** | n/a | n/a | 82% |
| v1–v4 rejects | 15 | 2 | 2 | FAIL (11 pairs, 8 rolls) | LANE_FIT_LOW 10, SAME_SKEL 5, DUP 3, COPY 3 | 0 |
| random | 20 | 0 | 0 | PASS | OUT_OF_KEY 20, LANE_FIT_LOW 20 | 0 |
| v4 mockup ukmin | 20 | 7 | **17** | FAIL (29 pairs, 19 rolls) | SAME_SKEL 12, COPY 3 | 3 |
| v4 mockup long | 20 | 1 | 6 | FAIL (46 / 19) | LANE 12, SAME_SKEL 13, COPY 2 | 1 |
| v4 mockup pop | 20 | 0 | 3 | FAIL (53 / 20) | LANE 17, SAME_SKEL 16, COPY 7 | 1 |
| v4 mockup gaskin | 20 | 0 | **15** | FAIL (66 / 20) | SAME_SKEL 19, BASS_ON_KICK 5 | 0 |
| technical T1–T7 | 7 | 1 | **7** | FAIL (synthetic: all share one bass) | DUP 6 only | 2 |
| generic G0–G9 | 10 | 2 | **7** | FAIL (synthetic: G5–G9 share G0's bass) | LANE 3 (G1 organ, G2 16th roll, G4 pump), BASS_ON_KICK 1 (G1), DUP 5 | 1 |

\* Reference folders hold two sections of the same track, so SAME_SKELETON and the batch gate fire by design. Read `solo` for refs.

## Honest read
- **References still pass:** 15/17 solo (88%, up from 82%), and 13/15 UK. The two misses are real GR v2 hard-rule hits on real tracks:
  - Ellia b137 is 6.1% out of the minor-modal scale. That's 2-step garage harmony, outside the UK lane.
  - Josh b137 has a deliberate one-bar bass drop-out.
  
  Both are defensible as "the generator shouldn't do this".
- **What still fails, and why:**
  - **Random:** still 0/20 (OUT_OF_KEY + LANE_FIT_LOW on every roll).
  - **Rejects:** 13/15 still fail (LANE_FIT_LOW, originality, duplicates). **Slipping through:** v3 C4 phrygian creep and C5 octave-fifth (soft only: GRID_LOCKED, BASS_NOTES_TOO_SHORT, NO_BAR8_TURNAROUND). In v1 both failed on GRID_LOCKED and BASS_NOTES_TOO_SHORT.
  - **v4 mockups:** all 4 styles still NO-SHIP, but **only because of the duplicate checks and the batch gate** for ukmin and gaskin. On their own, 17/20 ukmin and 15/20 gaskin rolls now pass. In v1 they failed on BASS_NOTES_TOO_LONG ×10, GRID_LOCKED ×7 and ROOT_DRONE ×4 (ukmin), and CALL_ANSWER_MISSING ×20 (gaskin). Those codes are all soft now.
  - **The risk:** a generator that only diversifies skeletons and changes nothing else would pass ukmin at 85% (machine SHIP-CANDIDATE), with gaskin at 75%. Long and pop are still held by LANE_FIT_LOW.
- **Generic-house flags no longer block anything by themselves.** G1 organ 8ths, G2 16th roll and G4 offbeat pump still fail, through LANE_FIT_LOW (and BASS_ON_KICK for G1). These pass solo: G3 reese-long (9 soft codes), G5 piano offbeats, G6 16th hats, G7 90s swing, G8 FX and G9 Latin bed. They fail in the batch only because the synthetic set reuses G0's bass (DUPLICATE). This is the intended behaviour under "generic flags are soft".
- **Static/duplicate rolls still block a ship.** DUPLICATE/SAME_SKELETON are hard per roll, and the batch gate fails every mockup batch (29–66 pairs vs the 2 allowed). STATIC_LOOP on its own (a one-bar loop with a fresh skeleton) is soft now. In the generic set, all three STATIC_LOOP rolls were also caught by LANE_FIT_LOW.
- **Technical faults never fail now:** clipping, clicks, tail-into-bar-1, MIDI tail and stuck note are all soft (T3–T7 pass solo). GR v2 §5 lists these as hard. `--hard-profile gr_v2` restores them, but it also fails 4 refs on transcription artefacts (Ellia NO_GAPS ×2, Reaction BANNED_FIGURE/TAIL_PAST_LOOP): refs 11/15 UK.

## Is too much junk passing? Yes, for one case. Smallest fix proposal (NOT implemented in any verdict)
1. **Soft-recurrence batch rule (recommended).** A batch fails if one soft code fires on > 40% of its rolls (n ≥ 10). Any single roll keeps its freedom; a generator breaking the same soft rule on most rolls has a style bug, not freedom. Measured (advisory column, already in every report):
   - Would fail: mockup ukmin (BASS_NOTES_TOO_LONG 10/20), longnote (GRID_LOCKED 15/20), gaskin (CALL_ANSWER_MISSING 20/20), rejects (BASS_NOTES_TOO_SHORT 12/15), random (LEAP_TOO_BIG 20/20).
   - Wouldn't fail: pop (7/20, but already held by LANE_FIT_LOW 17/20).
   - Refs max: 27% (VAR_NOT_IN_BACK_HALF 4/15), so there's headroom.
   
   It counts toward the batch verdict just like the skeleton gate.
2. **A per-roll soft cap alone does NOT work.** Excluding informational codes and REF_FLAG:
   - Ref solo-passers carry 0–2 soft codes.
   - Mockup passers carry 0–4 soft codes.
   
   A cap at ≥ 4 catches only G3 reese, 2 gaskin rolls and 1 pop roll; a cap at ≥ 5 catches only G3. It's a useful backstop for pile-ups (G3 had 9), not a fix.
3. **Render-only technical faults** (CLIPPING, CLICKS, TAIL_INTO_BAR1, STUCK_NOTE): these are bugs, not musical freedom. They fire on 0 refs and 0 real renders. Maestro to decide whether to align with GR v2 §5 by making just these four hard (or use `--hard-profile gr_v2`). Not done here.

---
# v2.1: soft-recurrence gate + 4 technical hard codes (Maestro, 2026-10-17 5:12 PM ET)

## Changes
- **Soft-recurrence gate (hard, batch-level):** a batch FAILS if any one soft code fires on > 40% of its rolls (batches of ≥ 10).
  - Not counted: KICK_ASSUMED, NO_STAB, BONUS_STABS_IN_RESTS, WAV_UNREADABLE, REF_FLAG, STAB_OUT_OF_KEY.
  - Where it shows: the report verdict (naming every offending code with its rate), the `soft_recurrence_gate` CSV column, the exit code and `--tally`.
  - It replaces the v2 advisory line.
- **New hard roll codes by default:** CLIPPING, CLICKS, TAIL_INTO_BAR1, STUCK_NOTE. TAIL_PAST_LOOP stays soft.
- **Kept softenings:** stabs are out of OUT_OF_KEY (soft STAB_OUT_OF_KEY), and non-neighbour chromatic bass notes are allowed up to 2% of bass time.
- **Updated:**
  - ROLLA_ROLL_SCORING.md §3/§4/§5/§6.
  - AR_CHECKLIST.md: the generic-flag note now lists the v2.1 hard set and the recurrence gate.
  - The v2 files are backed up as `*.v2_pre_gate.*`, and the v2 outputs are in `_validation/out_v2/`.

## Results (full run, sequential, ~6 min)
| set | n | solo pass | skeleton gate | soft-recurrence gate | verdict | caught by |
|---|---|---|---|---|---|---|
| refs UK | 15 | 13 | FAIL* | PASS (max VAR_NOT_IN_BACK_HALF 27%) | n/a* | Ellia b137 OUT_OF_KEY, Josh b137 EMPTY_BAR (as in v2) |
| refs Gaskin / LONG / POP anchors | 2/2/2 | 2/2/2 | PASS | n/a (n < 10) | n/a* | n/a |
| **all 17 refs** | 17 | **15 (88%)** | n/a | n/a | n/a | **0 technical flags** |
| random | 20 | 0 | PASS | FAIL (TOP2_LOW, LEAP_TOO_BIG, LOOP_NO_REPEAT 100%, TOO_MANY_PCS 95%, NO_BAR8 60%) | NO-SHIP | rate (OUT_OF_KEY 20, LANE 20) + recurrence |
| v1–v4 rejects | 15 | 2 | FAIL (11 pairs) | FAIL (BASS_NOTES_TOO_SHORT 80%, NO_BAR8 73%, GRID_LOCKED 60%, OCTAVE_PUMP 53%, TAIL_PAST_LOOP 47%) | NO-SHIP | all three |
| mockup ukmin | 20 | 17 | FAIL (29) | FAIL (BASS_NOTES_TOO_LONG 50%) | NO-SHIP | skeleton + recurrence (+ rate 7/20) |
| mockup long | 20 | 6 | FAIL (46) | FAIL (GRID_LOCKED 75%) | NO-SHIP | all three |
| mockup pop | 20 | 3 | FAIL (53) | PASS (ROOT_DRONE 35%) | NO-SHIP | rate (LANE 17, COPY 7) + skeleton |
| mockup gaskin | 20 | 15 | FAIL (66) | FAIL (CALL_ANSWER_MISSING 100%, ONKICK_HIGH 75%) | NO-SHIP | all three (75% solo, below 80) |
| technical T1–T7 | 7 | 3 | FAIL (synthetic) | n/a (n < 10) | NO-SHIP | T3 CLIPPING, T4 CLICKS, T5 TAIL_INTO_BAR1, T7 STUCK_NOTE now hard; T6 TAIL_PAST_LOOP soft |
| generic G0–G9 | 10 | 7 | FAIL (synthetic) | PASS (ONE_VELOCITY 4/10 = 40%, at the limit) | NO-SHIP | LANE (G1/G2/G4), DUPLICATE (G5–G9) |

\* Reference folders hold two sections of one track, so the skeleton gate fires by design. Read `solo` for refs.

**Key points:**
- Without the skeleton gate, three mockup batches would still be NO-SHIP on recurrence: ukmin (which would otherwise reach 85% solo), long and gaskin. Pop is still held by LANE_FIT_LOW.
- `--tally` was smoke-tested on the ukmin mockup CSV with filled judge columns: both gates are listed as FAIL, and the result is SET VERDICT NO-SHIP.
- **Caveats:**
  - The mixed generic set sits exactly at the 40% limit (ONE_VELOCITY 4/10); it passes only because the rule is "> 40%".
  - Batches under 10 rolls aren't checked for recurrence.
