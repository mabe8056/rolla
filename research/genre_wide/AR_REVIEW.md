# A&R review of the genre_wide research (GENRE_KNOWLEDGE.md, PATTERN_VOCAB.json, SIMILAR_LABELS.md)
A&R, 2026-10-17 ET. For Maestro and the ROLLA generator rebuild.

**What this is and isn't.** I measured the reference MIDI and added genre knowledge. **Nobody listened to anything for this review.**
- **Evidence used:** 26 reference drops from the 9 transcribed tracks (72 MIDI files), `transcriptions/CROSS_TRACK_RULES.md` (CTR), and `tools/AR_CHECKLIST.md`.
- **Script:** `research/genre_wide/ar_vocab_check.py`. It uses only stdlib and mido, needs about 20 MB of RAM, and never opens audio. The raw numbers are in `ar_vocab_measure.json`.
- **Groups:**
  - **UK** is 13 drops: LIGHTLEAK ×2, Local Dub V Line ×2, Local Dub Reaction, Stef ×2, Josh ×2, Laidlaw ×2 and M-High ×2.
  - **GASKIN** is Wax Master ×2.
  - **ELLIA** is ×2.

**Limits of the transcriptions:**
- Stabs and chops are CQT peaks from the other stem (LOW–MED confidence). They mix pads, riffs and stabs.
- Drums are band-split: kick 36, clap band 39, closed hat 42, open hat 46. The clap band picks up bleed from hats and percussion, so I only trust it for beats 2 and 4.
- There's **no rim, perc, conga, shaker, snare, pluck, FX or pad lane**. Every cell in those lanes is *unverified*, not confirmed.
- Downbeat phase is low confidence for Stef and Laidlaw.

**Verdict codes:**
- ✅ **CONFIRMED**: the references show it.
- ❌ **CONTRADICTS**: the references show otherwise.
- 🏠 **GENERIC**: generic house, tech house, big-room, techno or DnB vocabulary rather than this lane.
- 🌫 **VAGUE**: the cell can't be generated as written.
- 🐞 **GRID BUG**: the grid doesn't match its own description.
- ⚠ **COPY RISK**: the cell is a literal reference skeleton.
- ❔ **UNVERIFIED**: plausible, but no reference covers it.

Slots run 1–16, kick on 1/5/9/13.

---
## 0. Headline findings (fix these first)
1. **`style_params.UK_MIN.bass_on_kick_max = 0.1` contradicts the references.**
   - UK bass on the kick measures **1.2–28%** (median 22%). Only M-High (1–2%) and Reaction (9.5%) are under 10%.
   - With 0.1, ROLLA would reject LIGHTLEAK, Local Dub, Stef, Josh and Laidlaw.
   - **Set it to 0.28** (that's "72–98% off the kick").
2. **`UK_MIN.stab_hits_per_bar = [0,3]` and `ST_rest` UK 0.6 contradict the references (medium confidence).**
   - UK other-stem hits per bar run **0.5–10.4** (median 5.2), and bars with a hit run 50–100% (median 100%).
   - Only Reaction (0.5), LIGHTLEAK main (2.8) and Stef main (2.75) fit [0,3].
   - Local Dub (8.4–9.2, the counter-riff), M-High (9.6–10.4, a lead stab riff on the e/a) and Josh (4.2–6.1) are a second mode.
   - **Use two stab modes:** "dub" at 0.5–3 hits per bar and "riff" at 4–10 per bar, with ≥70% of riff hits on the e/a. Drop `ST_rest` UK to about 0.25.
3. **`UK_MIN.octave_share = [0,0.1]` is too tight.**
   - It's bimodal in the references: 0% (LIGHTLEAK, Josh, Reaction), 6–8% (Local Dub, Laidlaw) and **16–26%** (Stef 16–21, M-High 20–26).
   - **Use [0, 0.25]**, picking per roll either 0 or 0.15–0.25.
4. **`B_dotted` UK_MIN 0.1, and the claim that "UK-minimal basslines don't use this grid" (GENRE §4, CTR D1), is contradicted by LIGHTLEAK.**
   - LIGHTLEAK's bass attacks sit on slots 1, 4, 7, 10, 13, 15. The dotted-grid share is **62–67%**, the same as Gaskin's 65–73%.
   - Its "root held 1–6, b7 held 7–14" in CTR C is the *pitch* being held, re-attacked on a dotted grid. The measured median note length is 2.3 sixteenths.
   - **Raise `B_dotted` UK_MIN to 0.5 and LONG to 0.5**, and use the 3-3-3-3-2-2 ending. The difference from Gaskin is the pitch content (root/b7 against a chromatic b2 cluster) and the sidechain depth, not the grid.
5. **Two cells are literal reference skeletons (⚠ COPY RISK).**
   - `B_syncop8` (`x-x--x-x-xx-x-x-`) is **exactly** Local Dub V Line's 8-attack rhythm. It matches 1.0, and 25% of UK reference bars sit within Jaccard 0.6. That's the rhythm I failed sketch_v1 Direction B for.
   - `ST_counter_riff` is the same rhythm in the stab lane (0.89).
   - `B_mhigh_staccato` (`--x--x-x---x----` with degrees 1/5/1/8) is **exactly** M-High's slot set at 1.0, *with* its root+5/octave pitch shape.
   - Keep the *family* (a fixed 7–8 attack syncopated rhythm, or 4 staccato off-kick attacks) but never ship the literal grid. Generate siblings with `OP_shift_1`, `OP_drop_note` and a slot permutation, and require the cell itself to pass `originality_check.py` (H4).
6. **Grid bugs (🐞) put bass on the kick in cells that say they avoid it:**
   - `B_octave_bounce`: the "octave on following e" is on slots 5 and 9 (both kicks), so 40% of its attacks land on the kick.
   - `B_tresillo_off`: "displaced to avoid kick", but it hits slots 5 and 13.
   - `B_sparse_space`: 1 of its 2 notes is on slot 13.
   - `B_hold_lean`: re-attacks on slot 9.
   - `HT_reverse_in` starts with holds (`=`) and no attack.
   - The fixes are in §1.
7. **Rolling-techno and DnB density cells contradict every reference** (bass notes per bar 3.9–8.6, max 16th run 7):
   - `B_16_full`: 16 per bar, a 16-note run, 25% on the kick.
   - `B_roll3` / `B_roll3_vel`: 12 per bar. Beats with e+and+a all attacked are only 0–19% in the references.
   - `B_pre_kick_pair`: and+a on every beat, against 0–20% in the references.
8. **Rejected or off-lane labels still drive cells.**
   - FUSE (rejected) tags `B_sparse_space`, and HOTTRAX (rejected) tags `B_off8_pump`. Both keep UK_MIN weights of 0.8 and 0.7.
   - Factory 93 (techno/rave crossover) has UK_MIN 0.7.
   - DX3N isn't in Melak's lane list but is treated as a priority label (POP 1.0).
   - Nervous gets UK_MIN 0.2 / G90 0.9, yet the measured LIGHTLEAK track is a 2026 Nervous signing (see §6).
9. **`GASKIN_90S` swing 54–60 isn't supported by the only Gaskin reference.** Wax Master is straight (bass 47.7–51.8%; the hats have no off-16ths, so they can't be measured). CTR D5 says "~67% NOT confirmed".
   - The swung Bass Jamz reference is **Ellia Jaya at 64–66%**.
   - **Use a 50–56 default plus a 62–66 "Ellia/2-step" switch.** Don't use a 90s-house middle value.

---
## 1. BASS cells (31): measured against 13 UK, 2 Gaskin and 2 Ellia drops
**UK reference ranges** (min–max, median):

| Measure | Range | Median |
|---|---|---|
| Attacks per bar | 3.9–8.6 | 5.5 |
| On kick | 1.2–28% | 22 |
| Top-2 pitch time | 58.6–98.4% | 90 |
| Median length | 1.07–2.31 sixteenths | 1.8 |
| Notes ≥6 sixteenths | 0–7.4% | 0 |
| Max 16th run | 1–7 | 3 |
| Sounding | 0.36–0.97 | 0.70 |
| Octave moves | 0–25.6% | 6.9 |
| Register (median pitch) | C#1–C2 (range A0–C3) | |
| Beats with e+and+a all attacked | 0–18.8% | 3.1 |
| Beats with only the "and" | 1.6–28.6% | 22 |
| Bass held through a kick | 5.8–56% of kicks | 27 |
| Bass swing | 45.9–57.7% | |
| Velocity layers (of 16) | 2–10 | 7 |

**Most-used slots across UK** (percent of bars with an attack): **15** 65, **8** 63, **10** 54, **1** 51, **3** 48, **6** 47, 13 45. Slot 16 is the least used at 17%, which leaves a natural hole before the downbeat.

| Cell | Verdict | What's wrong / evidence | Suggested value |
|---|---|---|---|
| B_off8_pump | 🏠 + ❌ (for UK) | Pure "and"-only pump. No UK bar does this: beats with only the "and" are 1.6–29%, and the best match is 0.5. It's "one of the most widely used" generic tech-house basslines, tagged HC/DFTD/HOTTRAX. `inference:false` rests on articles, not the references | UK_MIN 0.7→**0.3**, POP 0.8→0.5. Allow it only with a partner pitch (top-2 rule) and a tail mutation |
| B_roll3, B_roll3_vel | ❌ 🏠 | 12 attacks per bar (refs max 8.6); e+and+a on every beat (refs 0–19%) | UK_MIN 0.3/0.4→**0.1**; LONG→0.2. Better: a "2 of 3 after the kick" variant (`-xx--x-x-xx--x-x`, 8 per bar) |
| B_pre_kick_pair | ❌ | and+a on every beat; refs show 0–20% of beats (median 3%) | Use it on 1–2 beats per bar, not all 4 (`------xx------xx`). UK 0.4→0.3 |
| B_long_short | ✅ (density at the edge) | 8 per bar on e/a, 0% on the kick; nearest is M-High (0.78) | Keep. Add len16 [1,2] |
| B_octave_bounce | 🐞 ❌ | Its attacks on slots 5 and 9 are kicks (40% on kick), and 100% of its moves are octaves (refs max 25.6%) | Grid `--x--X----x--X--` (root on the "and", octave on the following "a"/"e"), with at most 1–2 octave notes per bar. POP 0.7→0.5 |
| B_hold_lean | 🐞 ❌ | Its notes are 6 sixteenths long, but refs have 0–7% of notes ≥6 (median length 1.1–2.3). It re-attacks on the slot-9 kick. "Direction A" is our own sketch, not a reference, so `inference:false` is circular. Holding through a kick *is* real (6–56% of kicks), but in 2–3 sixteenth notes that straddle the kick | `--x==-x==---x==-` style, len16 [2,3], attacks off the kick. UK 0.9→0.7 |
| B_root_b7_halfbar | ❌ rhythm, ✅ pitch | LIGHTLEAK's 1→b7 shape is confirmed (b7 44 / root 36%), but LIGHTLEAK re-attacks on a dotted grid. It doesn't hold 6 and 8 (median 2.3, dotted share 62–67%). Copying both grid and pitch would be ⚠ LIGHTLEAK | Pitch shape 1 (first half) → b7 (second half) over a dotted or other off-kick grid with len16 [2,3]. Run it through originality_check |
| B_sparse_space | 🐞 ❔ | 2 notes per bar, 1 on the slot-13 kick (50%). Refs never go below 3.9 per bar. Archie Hamilton/MicroHertz has no transcription. FUSE label is rejected | Grid `------x------x--`. Use it for 1–2 bars of 4 ("reveal" bars), not as the loop. UK 0.8→**0.4**; drop the FUSE tag |
| B_mhigh_staccato | ✅ ⚠ | It's M-High's exact slots 3/6/8/12 (100% of bars), with 1/5/1/8 pitches | Keep it as a family seed, not an emitted grid; it must pass H4. Add len16 [1,1.3] |
| B_syncop8 | ✅ ⚠ | It's Local Dub V Line's exact 8-attack rhythm (1.0) | Same: family seed only, and siblings must differ by ≥2 slots |
| B_dotted | ❌ (UK weight) | UK 0.1 is wrong: LIGHTLEAK is dotted (0.71 match, dotted share 62–67%). Gaskin is confirmed (1.0) | UK_MIN **0.5**, LONG 0.5, GASKIN 0.9. len16 2 is confirmed (Gaskin 1.98–2.18, LIGHTLEAK 2.25–2.31). Its on-kick 33% exceeds 28%, so for UK use the 3-3-3-3-2-2 ending or `B_dotted_offset` |
| B_dotted_offset | ✅ (description wrong) | It hits the slot-5 kick (20% on kick). A 3-step grid always meets one kick per bar, and 20% is inside the 1–28% range | Keep the grid and fix the text ("1 kick per bar max"). UK 0.4→0.5 |
| B_tresillo | 🏠 | House/garage. 33% on the kick (slots 1 and 9) | UK 0.1 is OK. GASKIN 0.7→0.4: the Gaskin reference is dotted, not tresillo (0.5 match) |
| B_tresillo_off | 🐞 | Lands on the slot-5 and slot-13 kicks | Grid `-x-x--x--x-x--x-` (tresillo rotated by 3: 0% on kick) |
| B_callans_2bar | ❌ vs its own source | Cites CTR-D2 (Gaskin), but Gaskin's call/answer is *pitch*-based (b7→1 / b2→b3) over the same dotted rhythm. The match to the Gaskin reference is 0.24. The "busy bar A, sparse bar B" rhythm isn't measured anywhere | Redefine it as 2-bar pitch call/answer on a constant rhythm (rep1 ≈0, rep2 ≥0.7, which Gaskin has: 0.0–0.01 / 0.70–0.83). Keep a rhythm variant as ❔ |
| B_answer_tag | ✅ (weak) | Pedal plus tag on 15–16. Slot 15 is the most-used bass slot (65%). Slot 16 is only used heavily by Laidlaw (81–94%) and Gaskin (40–50%) | Keep. Add len16 |
| B_e_a | ✅ | 8 per bar on e/a, 0% on the kick; nearest is M-High (0.78) | Keep. len16 [1,1.3] |
| B_16_full | ❌ 🏠 | 16 per bar, a 16-note run (refs ≤7), 25% on the kick. Rolling techno, not this lane | UK_MIN 0.2→**0**, LONG 0.5→0.1, or delete it |
| B_acid_303 | ❔ | 9 per bar is just over the refs' max of 8.6, and 2 attacks land on kicks. No acid reference exists | Drop one note on a kick (the slot-5 attack): `-x-x--x--xx-x-xx` → 8 per bar. Keep it ❔ |
| B_funky_guitar | 🌫 ❔ | "Played like a funky guitar" doesn't specify pitch, length or articulation. Gaskin matches 0.75, but that's an accident | Specify it: len16 [0.5,1.5] staccato, 2–3 pcs, a ghost on "o", filter envelope. Otherwise leave it ❔ |
| B_organ_8ths | 🏠 ❌ | M1 organ on 8ths: 50% on the kick, 90s NY house. Melak rejected the M1 organ (sketch_v1 B), and the Gaskin reference doesn't do it (0.42) | UK 0 OK. GASKIN 0.7→**0.15** |
| B_garage_bounce | ❔ | Closest is Ellia (0.71), the Bass Jamz garage outlier | Keep it for GASKIN/Ellia mode. UK 0 OK |
| B_one_note_pedal | ❌ | No reference is one-note. Max root share is 68% (Reaction); every track has a partner pitch at 22–46% | Re-spec it: root ≥60–70% plus one partner (b7/b3/2/5), or limit pure pedal to ≤2 bars of 4 |
| B_step_up | ✅ | Josh confirmed: step moves 60–75%, degrees 1/2/b3, match 0.8 | Keep. Watch copy risk with Josh (add H4) |
| B_neighbour | ✅ | Laidlaw 1↔2 (top-2 96–98%); Gaskin's b2 holds 17–27% of time | Keep |
| B_ladder_down | ❌ as a loop | A 5-pitch equal-length ladder gives top-2 ≈40%, below the refs' 58.6% minimum | Use it only as a bar-4/8 fill in slots 11–16 |
| B_glide_pair | ✅ | Glides are 2–11% of notes (CTR B) | Keep, with a ≤11% cap |
| B_reese_long | 🏠 ❌ | Reese/UKG bassline: 7–16th notes, while refs have 0–7% of notes ≥6 sixteenths. Gaskin's median is 2 | GASKIN 0.4→**0.1**; LONG 0.2→0 |
| B_poly_3 | ❌ (duplicate) | Same grid as `B_dotted`. A 3-bar realignment contradicts the measured 2- or 4-bar loops (every reference repeats at 2 or 4) | Delete it, or merge it into `B_dotted` with a pitch cycle that resets every 2 or 4 bars |
| B_rest_bar | ✅ (arrangement only) | No reference drop has a silent bass bar | Use it only through `T16_groove_mute` |

**Missing in BASS:**
- **len16 is missing on 22 of 31 cells.** Add len16 to every bass cell, with targets of 1.1–2.3 (UK), 2.0 (Gaskin) and 1.0–1.3 (the M-High staccato family).
- **No bass swing parameter.** Stef swings the bass itself (55.5–57.7%), while everyone else is 46–55. Add `bass_swing: [48,58]` to UK_MIN.
- **No "riff-lane-louder-than-bass" option.** M-High has its lead stab riff on the e/a.

## 2. STAB cells (15)
**UK reference ranges:**

| Measure | Range | Median |
|---|---|---|
| On-beat | 3.6–28.4% | 20 |
| Pitch classes per hit | 2–4 (CQT noise inflates this; CTR says 1–3) | 2 |
| In a bass rest | 0–57% | 25 |
| On a bass attack | 24–75% | 50 |
| Hits per bar | 0.5–10.4 | 5.2 |

**Most-used slots:** **15** (70% of bars), 4 (52%), 8 (50%), 10 (50%), 7, 6.

| Cell | Verdict | Evidence | Suggested value |
|---|---|---|---|
| ST_a4 | ✅ lane staple | Slot 15 is the #1 stab slot in UK refs (70% of bars): Stef 75–88%, LIGHTLEAK 69–100% | Raise UK 0.8→0.9 |
| ST_e2_a4 | ✅ (partly) | Slot 6 at 36%, slot 15 at 70% | Keep |
| ST_dub_dotted | ✅ / ❔ | Reaction-style sparse single hit (0.5 hits per bar). The delay can't be measured from MIDI | Keep |
| ST_offbeat8 | 🏠 | Organ on every offbeat is the Robin S lineage. Gaskin's stabs are sustained and the offbeats aren't all used | UK 0.1→0; GASKIN 0.7→**0.3** |
| ST_syncop_90s | ❌ for Gaskin | `inference:false` cites an Attack article. The Gaskin reference has sustained chords (median 12–21 sixteenths), not short hits | GASKIN 0.9→0.5 |
| ST_a_pockets | ✅ | Josh: the "a" of beat 1 at 88%, the others at 19–50% | Keep |
| ST_counter_riff | ✅ ⚠ | Local Dub V Line rhythm (0.89) | Family seed only, with H4 required |
| ST_bar_end_pair | ✅ | LIGHTLEAK slots 14–15 (81/69%) | Keep |
| ST_2bar_call | ❔ | No 2-bar stab answer was measured; the best match is 0.42–0.5 | Keep ❔ |
| ST_sustain_bVI_bVII | ✅ (Gaskin) | Median 12–21 sixteenths, onsets on the "e" | Keep |
| ST_stab_on1_rare | ✅ as a rule | Slot 1 holds 0.7–11% of UK hits | Keep at 0.2 |
| ST_rave_stab | ❌ 🏠 | 100% on the beat (cap is 30%). It's a 90s "rave stab" | Move it off the beat (`---x-------x----`) or set UK 0.2→0.05 |
| ST_piano_offbeat | 🏠 | House piano. UK 0 is fine | POP 0.6→0.3 (Melak: no 90s-house piano) |
| ST_tresillo | ❌ slightly | 33% on the beat | Shift it off the beat: `---x--x---x-----` (3-3-4 starting on the "a", 0% on the beat) |
| ST_rest | ❌ | Bars with a hit are 94–100% in 11 of 13 UK drops | UK 0.6→**0.25** |
| *(missing)* | **ST_ea_riff** | M-High's lead stab riff runs 9.6–10.4 hits per bar, 72–80% on the e/a, 1–2 pcs, and sits in bass rests 56% of the time | Add it: about 6–9 hits per bar, ≥70% on e/a, ≤10% on the beat, 1–2 pcs, UK 0.5 |

**Stab placement isn't all call-and-response.** The "stab in bass rests" idea (soft rule `S_stab_in_bass_holes`, `OP_callresp_swap`, `PL_call_in_hole`, `VX_answer_bass_rest`, and my own AR_CHECKLIST lesson) is **Melak's taste rather than what the references do**.
- In-rest share is 0–57% (median 25%), and **50% of stab hits land on a bass attack** (Local Dub 57–75%, LIGHTLEAK 56–67%, Stef 56–61%).
- Only M-High (56%) and Laidlaw (35–45%) lean into the rests.
- Keep the soft weight at 0.5, but score it as a preference, not a fail. The scorer's existing `STAB_CLASH` cap (>80% on bass attacks) is the right hard line.

## 3. DRUM cells (38)
**Measurement notes:** these numbers come from the kick, clap band, closed-hat and open-hat lanes only. The clap band carries bleed, so I only trust it on beats 2 and 4.

| Cell | Verdict | Evidence | Suggested value |
|---|---|---|---|
| K_four | ✅ | Kick on 1/5/9/13 in 81–100% of bars in every drop | Keep |
| K_four_pickup / K_four_double | ❔ | Kick on slot 15 or 16 in 0–31% of bars, which is indistinguishable from bass bleed. Laidlaw, M-High and Josh are strictly 4/4 | UK 0.4→0.2; UK double 0.2→0.1 |
| K_drop_last_beat / K_drop_bar | ❔ | Arrangement moves; the 16-bar drops can't show them | Keep ❔ |
| K_2step | ❌ for Gaskin, ❔ for Dansu | The Gaskin and Ellia references are 4/4 (88–100%) | GASKIN 0.6→**0.2** (opt-in switch) |
| CP_2_4 | ✅ | Clap band on slots 5 and 13 in 94–100% of bars everywhere | Keep |
| CP_4_only | ❌ (UK) | Beat 2 is never missing: 94–100% | UK 0.2→0.1, LONG 0.4→0.2 |
| CP_2_4_flam | ❔ (weak support) | Slot 12 shows up in 94–100% of bars for Local Dub, but it's the bleed-prone clap band | Keep ❔ |
| CP_2_4_tail, SN_* | ❔ | No snare lane | Keep ❔ |
| SN_roll_bar8 | 🏠 | Big-room build device | POP 0.6→0.3 |
| CH_off8 | ✅ (Stef) | Stef's closed hats are on offbeats in 69–94% of bars. The others use **open** offbeat hats | Keep. Make CH_off8 and OH_off8 mutually exclusive per roll |
| OH_off8 | ✅ lane staple | Josh, M-High, Local Dub and LIGHTLEAK b49: 81–100% of bars | UK 0.7→**0.85** |
| CH_16_accent, SH_16_vel, TAMB_16 | ❌ / ❔ | No reference has 16th hats: closed hats run 0.7–6.9 hits per bar. Quiet shakers may be under the detector, though | UK CH_16 0.6→0.3, SH_16 0.6→0.4. `SH_16_vel` and `TAMB_16` `inference:false` cite 2-step articles; mark them inference:true |
| CH_skippy, CH_gallop, CH_e_a, CH_syncop4 | ❔ | Best UK matches 0.44–0.56; no reference bar is ≥0.6 | Keep ❔. CH_syncop4 is the 90s recipe: UK 0.4→0.2 |
| OH_2_4_off, OH_last, CH_sparse_click, RIDE_off, CRASH_phrase | ❔ | Partial at best | Keep ❔. CRASH_phrase is big-room: POP 0.6→0.3 |
| RIM_3step, RIM_syncop, PERC_3_5_7_poly, TOM_* | ❔ | No perc lane exists. A 3/5-step loop slides against the bar; refs loop at 2 or 4 bars, but perc may still do that | Keep ❔. Cap polymeter at 1 perc lane in UK |
| CLAVE_son32, CLAVE_rumba, CONGA_tumbao, BONGO_run, COWBELL_off | 🏠 ❔ | Latin tech house / Hot Creations / Solid Grooves vocabulary. The clave cells are 60% on the beat | UK ≤0.2. Melak rejected Solid Grooves |

**Swing (`style_params`, `OP_swing_lane`):**
- **UK measured:** closed hats 49.1–59.1% (median 51.8), open hats 49.1–58.2%, clap band 52.7–63.6%.
- **`OP_swing_lane` says "hats/perc 54–58 UK", but the median UK reference is 51.8.** M-High, Josh and Local Dub are 49–54. **Change it to 50–57, default 52**, with the clap band 1–4 points later (≤63, Laidlaw).
- `swing_hats` UK [51,57] ✅.
- GASKIN [54,60]: see headline 9.

## 4. PLUCK, PAD, HITS and VOX cells (42)
No transcription covers plucks, FX or pads directly, so these are mostly ❔. Chops cover vox only weakly.
- **UK chops:** 0.25–2.75 hits per bar (median 1.4), present in 6–94% of bars, 0–57% on the beat. That's LOW–MED confidence.
- **❔ plausible (keep):**
  - PL_blip_1x, PL_call_in_hole, PL_question_answer, PL_ea_tick, PL_bass_mirror (Gaskin quote), PL_3step_arp / PL_5step (LONG).
  - PD_bed_bar, PD_bed_2bar, PD_offset_e (✅ Gaskin: onsets on the "e" 37%), PD_breath.
  - HT_bar4_a4 (slot 15 is the lane's busiest slot), HT_shot_e3, HT_rest.
  - VX_phrase_bar4, VX_word_a4, VX_answer_bass_rest, VX_rest, VX_hook_2bar (POP), VX_acap_4bar.
- **🏠 generic, down-weight:**
  - **PL_7step:** "deadmau5-style" is progressive. LONG 0.6→0.3.
  - **PL_16_gate:** a 16th gated pluck is trance / melodic techno. LONG 0.5→0.2.
  - **PL_offbeat_8:** generic bouncy tech house. POP 0.7→0.4.
  - **HT_sweep_up, HT_downlifter, HT_reverse_in, HT_phrase_one:** EDM/big-room build kit. POP 0.5–0.7→≤0.3. Melak's lane is DJ-tool.
  - **VX_countdown, VX_rap_bar:** gimmicks. Keep them as low as they are.
- **❌:**
  - **PD_rest** UK 0.7, "UK MIN drops usually pad-free": weakly contradicted. Laidlaw's drop has a 4–5 note pad (34–75 sixteenths), and Local Dub and M-High other-stem events are sustained (median 14–34). Set it to 0.5.
  - **VX_chop_offbeat** (4 per bar): fine for Ellia (5.25 per bar) but busier than any UK reference (≤2.75). UK 0.3 OK.
- **🌫 vague:** PD_atmos_tex ("vinyl, room, field rec"), HT_vinyl_tex, HT_metal ("metallic textures"), VX_breath_tex. Each grid is just "a bed" or "a hit"; the sound is the whole content. Each needs a one-shot category from Melak's packs (`OP_sample_swap`) and a level target, for example −24 to −30 dB under the bass.
- **🐞:** HT_reverse_in `========xxxxxxxx` holds before any attack. Use `x===============` with "reverse sample, ends at the next downbeat".

## 5. Style params, templates, operators and rules
- **bpm** UK 129–132: ✅ (130.00–132.00). GASKIN 128–133: ✅ (131.01, Ellia 133). LONG 126–131 and POP 126–132: ❔ below 130, since no reference is slower.
- **sidechain:** UK "heavy (17–38 dB)" ✅ CTR A5. Gaskin "light (3.6–8)" ✅. LONG "heavy" and POP "medium-heavy" are 🌫 with no number. Use dB ranges (LONG 17–30, POP 12–25 ❔).
- **vocal fields** ("sparse phrase/none", "texture only") are 🌫. Give hits per bar and bars-with-vox, for example UK 0–1.5 hits per bar in ≤50% of bars (refs median 1.4 per bar in 50% of bars).
- **T8_1bar_mutate** UK 0.7: ❌. The 1-bar repeat in refs is weak (rep1 0.0–0.67), and Josh and M-High are 4-bar loops (rep4 0.65–0.78 against rep2 0.03–0.37). Set it to 0.4. T8_4bar_cycle UK 0.6→**0.8**. T8_2bar_callans GASKIN 1.0 ✅ (rep1 0.0, rep2 0.70–0.83).
- **OP_tail_mutate** weight 1.0 everywhere, and "variation in last 2–4 sixteenths" (CTR A7, AR_CHECKLIST, soft rule `S_tail_variation`): **only partly supported.**
  - Measured bar-to-bar differences fall in slots 13–16 only 17–66% of the time (median 29%; 25% would be chance).
  - In the back half (slots 9–16) it's 33–88% (median 63%).
  - **Tail variation is a tendency, not the rule.** Widen the operator to "1–4 events in slots 9–16, favour 13–16", at weight 0.8.
- **Polymeter:** no reference shows a 3-, 5- or 7-bar cycle in bass or stab; all repeat at 2 or 4 bars.
  - PM_12_16 lists "DRUMS.clap(alt)", which **contradicts its own polymeter_rules** ("never on KICK/CLAP").
  - PM_20_16 on STAB is ❔.
  - Keep polymeter for perc and pluck only (❔), reset at 4/8 bars, max 1 lane in UK.
- **H2_lowend_clash** "no bass onset within 1/32 of a kick unless style allows": fine *if* the cap is 0.28 (headline 1).
- **H1_key** "chromatic only as b2 in GASKIN": ✅. UK b2 shows up at 0.2–4.8% of bass time (Stef, M-High), which is consistent with a passing note.
- **S_two_pitch_core** "most bass time" 🌫. Make it **top-2 ≥63%** (hard floor 58).
- **S_space** "≥10% silent bass slots": LIGHTLEAK b49 is 3% silent. Use ≥3% (hard) / ≥25% (soft).
- **S_root_home** ✅. S_off_kick_bass ✅. S_stab_offbeat ✅. S_swing_light ✅.
- **`inference:false` circularity:** cells sourced only from GR-* (our own GENERATOR_RULES) or from a single tutorial are marked as non-inference. Mark them `inference:true` unless a CTR measurement backs them. That applies to B_off8_pump, B_16_full, B_roll3*, B_pre_kick_pair, SH_16_vel, TAMB_16, PERC_3_5_7_poly, RIM_3step, PL_3/5/7step, TOM_low_707, ST_syncop_90s, B_hold_lean and B_rest_bar.

## 6. GENRE_KNOWLEDGE.md and SIMILAR_LABELS.md
- **Confirmed by the references:**
  - §2: 8-bar blocks are consistent.
  - §3: four-to-the-floor plus 2/4 clap; open offbeat hats (Local Dub/Josh/M-High); UK swing 50–57.
  - §4: register C1–C2; 72–98% off the kick; dorian (Josh) and phrygian b2 (Gaskin).
  - §5: stabs 1–3 pcs and ≤30% on the beat; Gaskin's sustained bVI/bVII.
  - §8 UK MIN bass: "off-kick, 2 pitches, heavy duck".
- **Contradicted:**
  - §4 "notes sit between kicks, leaving the first 16th of each beat empty": 1–40% of attacks are on the beat (median 26%). Read it as "mostly off the kick", not "empty".
  - §4 "Velocity gallops 115/85/75 [CB-ROLL]" is a rolling-techno recipe; the references aren't galloping 3-note rolls.
  - §8 UK MIN stab "0–3 per bar, in bass holes": see §2 above.
  - §8 GASKIN 90s "organ 8ths, hat swing 54–60": not in the Gaskin reference (see headline 9).
  - §8 UK MIN "Pad mostly none in the drop": weak, see §4.
- **The label lens is skewed toward generic house.**
  - **Nervous:** "today a high-volume Beatport house/tech house label (Mau P…)". For ROLLA, map Nervous to its UK-minimal signings, Stussy, Prunk, M-High and **LIGHTLEAK, which is transcribed and measures squarely UK minimal**. Raise NERV UK_MIN 0.2→0.6 and cut G90 0.9→0.6. Otherwise "Nervous" will pull 909 snare ghosts, organ stabs and piano.
  - **Hot Creations:** UK_MIN 0.3→0.5 (Kolter, M-High, EED, Max Dean are on it).
  - **DFTD:** POP 0.9 is fine, but its "big breakdowns, acapella culture" tags pull in big-room HITS. Keep its HITS weights low.
- **SIMILAR_LABELS:**
  - The short version says "ranked by how many of the 20 core artists also release there", but the order isn't monotonic: HHS 8 sits below Eastenderz 7, Factory 93 8 is #9, Moxy 7 is #15, and tszr 6 sits below ROSSI 5. Either rank strictly by count or say it's judgement-weighted.
  - **Factory 93** is a techno/rave crossover brand with Drumcode/Armada overlap. It's 🏠 for this lane: F93 UK_MIN 0.7→0.3. Drop its tags on `B_roll3`, `B_root_b7_halfbar`, `B_e_a` and `HT_metal`.
  - **Rejected labels still in the vocab:** FUSE (UK 0.8) and HOTTRAX (UK 0.7) are in `labels`, `label_style_weights` and cell tags, though the doc says Melak rejected them. Remove them, or zero them pending Melak.
  - **DX3N** is treated as a priority label, but it isn't in Melak's lane list. Move it to "similar".
  - **LW Recordings** is in `labels` but appears nowhere in either doc. Source it or remove it.

## 7. Confirmed vs. unverified (summary)
**✅ Confirmed by the references:**
- Style params: UK bpm, UK swing_hats, UK swing_clap, UK and Gaskin sidechain.
- Drums: K_four, CP_2_4, OH_off8, CH_off8 (Stef type).
- Bass: B_long_short, B_e_a, B_step_up, B_neighbour, B_glide_pair, B_dotted (Gaskin *and* LIGHTLEAK), B_dotted_offset, B_answer_tag (weak), B_mhigh_staccato ⚠, B_syncop8 ⚠.
- Stabs: ST_a4, ST_e2_a4, ST_a_pockets, ST_bar_end_pair, ST_sustain_bVI_bVII, ST_stab_on1_rare (as a cap), ST_counter_riff ⚠.
- Also: PD_offset_e (Gaskin), T8_2bar_callans (Gaskin), T8_4bar_cycle, hard rules H1/H3/H4, soft rules S_off_kick_bass, S_root_home, S_stab_offbeat and S_swing_light.

**❔ Plausible but unverified (no reference lane covers them):**
- All perc cells (rim, clave, conga, bongo, cowbell, tom, poly perc), plus snare and shaker.
- Every PLUCK cell, most PAD, HITS and VOX cells, the arrangement templates beyond 16 bars, full_track_maps, and the polymeter cells for perc and pluck.
- B_acid_303, B_funky_guitar, B_garage_bounce, ST_2bar_call, ST_dub_dotted's delay, the K_drop_* cells, and the LONG/POP BPM below 130.

## 8. Recommendation: how the 20-roll scoring should reward "sounds like this lane"
**What doesn't work:** I tested a bass slot-histogram cosine against the average UK reference. **It doesn't separate the lane.**
- Leave-one-out UK refs score 0.49–0.80, but Gaskin (0.69–0.73) and Ellia (0.76–0.79) score the same.
- `B_16_full` scores 0.94, the highest of all, because density inflates cosine.
- So **don't use a "nearest-reference pattern similarity" score as lane-likeness.** It rewards density and copying.

**Use range-membership on measured features instead.** Each feature scores in-range against the UK reference envelope, with the scorer's existing tolerances. A roll "sounds like the lane" when **≥10 of these 12 are in range** and none of the generic flags trip.
1. Bass off the kick: 72–98% (soft target about 78).
2. Top-2 pitch time ≥63% (hard 55), root in the top 2, 2–4 pcs holding ≥5% of the time.
3. Median bass length 1.1–2.3 sixteenths; notes ≥6 sixteenths ≤8%.
4. Bass attacks 4–8.6 per bar; max 16th run ≤7; e+and+a-all beats ≤20%.
5. Bass register median C#1–C2.
6. Loop: best of rep2/rep4 ≥0.55 *and* rep1 ≤0.67; back-half share of bar differences ≥50%.
7. Octave moves either 0–8% or 15–25% (bimodal, both lane-valid); flag >26%.
8. Stab on-beat ≤30%, slot 15 or slot 4/8/10 present in ≥50% of bars, 1–3 pcs. The density mode must be "dub" (0.5–3 per bar) or "riff" (4–10 per bar with ≥70% on e/a). In between, about 3–4 hits per bar of mixed placement, reads as generic.
9. Stab on bass attacks ≤80%. Stabs in bass rests is a *bonus* (Melak's taste), not a requirement.
10. Kick 4/4 ≥94%; clap or snare on 2 and 4.
11. Offbeat hat present (open *or* closed, not both as 16ths). Hat swing 50–57 (clap ≤63). >60 only in Ellia/2-step mode.
12. Bass sidechain depth ≥17 dB in UK mode (from WAV), ≤10 dB in Gaskin mode.

**Generic-flag penalties** (each is a fail reason the engineer can act on):

| Flag | Trigger |
|---|---|
| `BASS_ORGAN_8THS` | ≥6 of the 8 eighths attacked, ≥40% on the kick |
| `BASS_16TH_ROLL` | run >7, or e+and+a-all beats >25% |
| `BASS_REESE_LONG` | ≥15% of notes ≥6 sixteenths |
| `OFFBEAT_PUMP_ONLY` | ≥75% of beats are "and"-only, with one pitch |
| `STAB_ON_BEAT` | >30% |
| `PIANO_OFFBEAT_CHORDS` | ≥4-pc hits on ≥3 offbeats per bar |
| `HAT_16THS` | >10 closed-hat hits per bar |
| `SWING_90S` | 58–62% outside Gaskin/Ellia mode |
| `BUILD_FX_IN_LOOP` | riser/sweep/crash inside an 8-bar drop loop |
| `LATIN_PERC_BED` | clave/conga lane active in UK mode |

**Per style**, use the same features against a different envelope:
- **GASKIN:** dotted share ≥55%, rep1 ≤0.1 and rep2 ≥0.7, b2 ≥10% of time, sidechain ≤10 dB, sustained stabs ≥8 sixteenths.
- **LONG:** the LIGHTLEAK anchor; already in `score_rolls.py`.
- **POP:** the M-High anchor; already in `score_rolls.py`.

These plug into `tools/score_rolls.py` as soft codes. Most are already computed there (offkick, top2, len, notes_bar, run, octave, rep, stab_onbeat, stab_clash, hat swing). The new ones are the dotted share, e+and+a beats, stab density mode, slot-15 presence, long-note share and the generic flags.

The ≥80%-per-style ship rule stays. Lane-likeness doesn't replace ear judgement: the Composer and I still mark "would Melak keep it" by listening to each WAV.

---
*Files:*
- `research/genre_wide/ar_vocab_check.py` (the script)
- `research/genre_wide/ar_vocab_measure.json` (per-drop numbers plus per-cell checks)
- Backup copies in `/home/box/ar-backup/genre_wide/`
