# A&R Checklist: Melak's Music team quality gate
*Owner: A&R. Written 2026-10-16. Run this on every delivery before anything reaches Melak.
Sources: `rulebook/taste-profile.md`, `rulebook/house-master-playbook.md` (§0, §9, §12), `rulebook/structure-and-instrumentation.md`, and the history in `/workspace/track-analysis/melodies*`.*

A delivery reaches Melak only with **PASS** on every section below. If any section has a FAIL item, the delivery goes back to the Composer with reasons.

---

## 0. Intake (before listening)
- [ ] The delivery sits in its own versioned folder (`writing/sketch_vN/`). It contains a MIDI per part (plus `_FULL.mid`) and a short note naming the brief and what changed since the previous version.
- [ ] **2–3 options maximum.** 4 or more is an automatic FAIL. v1 sent 4, v3 sent 8, and both drowned the good idea.
- [ ] Each option is named by what it does ("R1 quartal stab response"), not by theory jargon ("mixolydian pluck").

## 1. Taste fit (Melak-specific, measured from history)
**What Melak loved:**
- Local Dub transcription accuracy.
- v2 C's quartal stab A3–D4–G4–C5.
- The v5 R1 stab answering the bass.

**What he rejected:**
- v1.
- Theory-generic lines.
- The v3 rhythms.
- The borrowed v4 Local Dub skeleton and vocal.

The thresholds below are the measured difference between the two lists.

| Check | PASS looks like | FAIL looks like (history) |
|---|---|---|
| **Bass is the hook** | The bass alone is hummable, and the other parts are "flares" (playbook §0.1, §4.3) | A lead or pluck carries the idea (v1 #2 vocal motif, #4 mixolydian pluck) |
| **Bass register** | Fundamentals around octave 1–low 2 (Local Dub: C#1–F#1, 35–46 Hz, playbook §12) | Rollers spanning A1–G3 (v1/v2/v3 basses, 2 octaves) |
| **Leaps** | Mostly steps ≤ 5 semitones, at most 1–2 deliberate leaps per 4 bars (playbook §0.2 #6). Octave moves 0–25% of moves are lane-valid: the refs are bimodal, 0–8% (LIGHTLEAK, Josh, Reaction, Local Dub, Laidlaw) or 15–25% (Stef, M-High) (AR_REVIEW §0.3) | Octave moves on more than ~26% of moves, i.e. an "octave roller" (v2/v3 basses, max leaps 12–17 st) |
| **Pitch vocabulary** | 3–5 pitch classes, root ≥ ~50% of sounding time (Local Dub: 3 pcs, root 68%) | Scale tours, mode names as the concept (v1 "mixolydian", v2/v3 "phrygian creep"), 7 pcs (v1 #4) |
| **Loop length** | A 4-bar loop inside an 8-bar phrase, bars sharing an opening with the variation **tending** to the back half of the bar (refs: median 63% of bar-to-bar differences in slots 9–16, range 32–88%), and a bar-8 dropout/fill (playbook §12 rules 3, 5) | 2-bar repeats (every v2/v3 bass repeats every 2 bars, which feels short and generic) |
| **Rhythm density** | Bass about 4–8.6 onsets per bar, attacks mostly off the kick (bass on the kick may run **up to 28%** of onsets: UK refs 1.2–28%, median 22%; AR_REVIEW §0.1), holes left | Constant 16ths (v3 R3 "straight ghost 16ths", 11 onsets/bar), even 8th/16th rollers |
| **Groove** | Repeatable and a little dark, minor or sus colour, with straight 16ths or light swing (54–58%) | 90s-house cheese: bright major organ riffs, piano-house chords, obvious shuffle |
| **Stabs for rhythm** | One voicing used as a rhythm instrument (v2 C / R1: a single quartal A3–D4–G4–C5), on off-kick 16ths. **Two density modes** (AR_REVIEW §0.2): **dub** 0.5–3 hits per bar (Reaction, LIGHTLEAK main, Stef main; Melak's R1 ≈1.5–2) or **riff** 4–10 per bar (Josh, Local Dub counter-riff, M-High). About 3–4 hits per bar of mixed placement reads as generic | Chord progressions, pads moving every bar, stabs on the kick, piano chords on every offbeat |
| **Call and response** | Bar 2 answers bar 1 (playbook §0.2 #2, §4). Stab hits landing in the bass rests (as in R1) is a **taste bonus** for Melak, **not a hard rule**: the references put only ~25% of stab hits in bass rests (0–57%, median 25%) | Stabs doubling >80% of the bass attacks. No answer phrase |
| **Element count** | 2–4 pitched parts in total. No lead or string in a drop unless asked (structure §4.1, §4.3) | 5+ pitched layers per idea |
| **"Would he play it?"** | You'd still feel the loop after 20 minutes (playbook §9) | It sounds like a theory exercise or a tutorial preset |

A taste-fit FAIL means 2 or more rows in the FAIL column, or the "bass is the hook" row failing alone.

## 2. Rulebook compliance (measurable)
Cite the rule in the verdict when a rule fails.
- [ ] **Tempo 124–130 BPM.** Default 130, the measured Local Dub tempo (playbook §9, §12).
- [ ] **Bass attacks avoid the first 16th of each beat**, or the delivery notes it relies on sidechain ducking of about 12 dB (playbook §0.2 #4, §12 rule 4).
- [ ] **Gaps:** at least one fully silent beat per 2 bars in the bass, plus a dropout or fill at the end of the 8-bar phrase (§0.2 #5, §12 rule 5).
- [ ] **Two velocity layers:** accent plus ghost notes about 6–7 dB softer, roughly velocity 115 vs 82 (§12 rule 7).
- [ ] **Bar 2 answers bar 1** in the bass, with at least one leap or octave somewhere in the 4-bar loop *or* a deliberate Local Dub-style no-leap choice that is stated in the note (§9 "Kick and bass").
- [ ] **Chords leave the root to the bass** and swap 5ths for 9/11/13. Sus/quartal voicings are fine (§3, §12 rule 8).
- [ ] **Arrangement maps** (when delivered): sections in multiples of 8, a 16–32-bar DJ intro with 2–3 elements, something changing every 8 bars, and no more than one switch-up per 8 bars (structure §1, §3.1).
- [ ] **Sound spec** (when audio is delivered): bass mono, energy peaking around 100 Hz or below with about 9% above 200 Hz in the Local Dub model, filter-envelope pluck, clap reverb about 12 dB under (playbook §2, §12).


**2b. Cross-track rules** (`transcriptions/CROSS_TRACK_RULES.md` §E, 8 UK refs, added 2026-10-16 22:15; corrected 2026-10-17 ET from `research/genre_wide/AR_REVIEW.md`). A miss counts as a rulebook deviation and needs a stated reason:
- [ ] 72–98% of bass attacks land off the kick (more than 0.3 of a 16th away), i.e. bass on the kick up to 28% (AR_REVIEW §0.1; score_rolls hard-fails only above 35%, the GENERATOR_RULES AV16 cap, because this scorer reads LIGHTLEAK 29.7% and Stef's 2nd drop 31.6%).
- [ ] The top two pitches carry 63–98% of bass sounding time, and the root is one of them. Measure the top two, not only the root.
- [ ] Median bass note length is 1.1–2.3 sixteenths.
- [ ] The loop repeats at 2 or 4 bars (pitch-grid match ≥0.77 in CTR's metric; ≥0.50 at 2 bars or ≥0.47 at 4 bars in score_rolls' GR match). Variation is a **back-half tendency** (refs median 63% of bar differences in slots 9–16), not a strict last-2–4-sixteenths rule (refs put only 17–66%, median 29%, in the last 4).
- [ ] Stabs use 1–3 pitch classes (quartal 4 allowed), ≤30% of hits on the beat, and are short. Full 4–5-note chords only appear as long pads. Density is dub (0.5–3/bar) or riff (4–10/bar). Stabs in the bass rests = bonus, not required.
- [ ] Octave moves 0–25% of bass moves (bimodal: 0–8% or 15–25%).
- [ ] Kick ducking is relatively heavy: UK refs 17–38 dB on demucs stems, against Reaction's 11.8 dB with the same code. A light pump of about 9 dB or less reads as Gaskin/Ellia style.

**2c. Lane fit and generic-house flags** (from `research/genre_wide/AR_REVIEW.md` §8, added 2026-10-17 ET; automated in `tools/score_rolls.py`, sheet `tools/ROLLA_ROLL_SCORING.md`):
- [ ] **Lane fit ≥ 10 of 12** measured features in the reference range (at most 2 misses among the measurable ones): off-kick, top-2/pcs, note length, density, register, loop repeat, octaves, stab placement + density mode, stab vs bass attacks, kick/clap, hats, sidechain depth. **Range membership only. Never score similarity to a reference.** Gaskin has its own ranges (dotted-grid share ≥55%, rep1 ≤0.1 and rep2 ≥0.7, b2 ≥10% of bass time, sustained stabs ≥8 sixteenths, duck ≤10 dB).
- [ ] **No generic-house flag:** BASS_ORGAN_8THS, BASS_16TH_ROLL, BASS_REESE_LONG, OFFBEAT_PUMP_ONLY, STAB_ON_BEAT, PIANO_OFFBEAT_CHORDS, HAT_16THS, SWING_90S, BUILD_FX_IN_LOOP, LATIN_PERC_BED. *(Changed 2026-10-17 for GENERATOR_RULES v2 / Maestro: these are now **soft** in score_rolls.py. They don't fail a roll on their own. Name each one in the review, and treat several on one roll, or one on most rolls of a batch, as a taste-fit problem. Hard machine fails (v2.1) are only OUT_OF_KEY, BASS_ON_KICK > 35%, EMPTY_BAR, COPY_OF_REF, LANE_FIT_LOW, CLIPPING, CLICKS, TAIL_INTO_BAR1, STUCK_NOTE, the duplicate checks/batch skeleton gate, and the soft-recurrence gate (any one soft code, generic flags included, on > 40% of a batch of ≥ 10 = batch FAIL); see ROLLA_ROLL_SCORING.md §3. v1 wording was "Any one is a FAIL.")*
- [ ] ROLLA 20-roll sets: run `python3 tools/score_rolls.py <dir> --style ukmin|long|pop|gaskin`. A style ships only at ≥80% auto-pass **and** the MELAK_KEEP ear column (Composer + A&R) filled in.

## 3. Originality (hard gate)
1. Run on every part and on the FULL file:
   ```
   python3 /workspace/music-team/tools/originality_check.py writing/sketch_vN/<file>.mid          # human report
   python3 /workspace/music-team/tools/originality_check.py <file>.mid --json > <file>.orig.json   # attach to verdict
   ```
   It compares against every `.mid` under `/workspace/music-team/transcriptions/`, recursively. Exit code 0 means PASS, 1 means FLAG, 2 means FAIL.
2. **Thresholds** (calibrated, see `originality_check_tests.md`):
   - skel_win ≥ 0.55 → FAIL (≥ 0.40 FLAG)
   - skel_ngram ≥ 0.45 → FAIL (≥ 0.30 FLAG)
   - voicing ≥ 0.75 → FAIL (≥ 0.50 FLAG)
   - combo int_ngram ≥ 0.60 AND rhythm_cov ≥ 0.70 → FAIL
   - FLAG only: int_ngram ≥ 0.50, int_lcsub ≥ 8, pc_ngram ≥ 0.60, rhythm_cov ≥ 0.75
3. **FAIL means the delivery is rejected.** Quote the matching bars from the report, for example "cand bars 1–2 ≈ Local Dub bars 1–2, −1 st". The rejected v4 Idea 3 scored skel_win 0.692 and skel_ngram 0.467.
4. **FLAG means A&R listens A/B against the reference.** PASS only if the shared material is generic: a neighbour tone, a root pulse or a common house rhythm. Write down why.
5. **The reference set is MIDI only.** If the Composer worked from an audio reference that has no MIDI in `transcriptions/`, ask for (or make) a transcription before passing.
6. **Audio / sample originality.** The script doesn't cover this, so check by hand:
   - [ ] List every sample or one-shot used, with its source. Nothing may be cut from the reference WAVs or stems in `transcriptions/*/orig.*` or `sep/htdemucs/*`. Grep the project/bounce notes for those paths.
   - [ ] Vocals must not be the reference vocal, re-pitched or re-chopped. The v4 vocal-chop triggers were rejected for this reason even though their MIDI PASSes, because the audio was lifted.
   - [ ] For any suspicious sound, cross-correlate it against the reference stem, e.g. `numpy.correlate` on onset envelopes, or eyeball it in `librosa`/Audacity. Over 0.8 normalised correlation on a clip of 0.5 s or more counts as lifted, which is a FAIL.
   - [ ] Synth recipes may follow a reference's *sound spec*, such as square/saw with an LP pluck. That is allowed. Notes and recordings are not.

## 4. Technical checks
**4.1 No tails wrapping into bar 1 when looped** (swells, reverb, release)

The rejected v4/v5 swells ran over the loop end and smeared bar 1.

- MIDI check: no note-off may sit past the loop end, and no note may start in the last 16th and ring into the downbeat unless that is intended as a pickup:
  ```python
  import mido; m=mido.MidiFile(F); ppq=m.ticks_per_beat; LOOP_BARS=8; end=LOOP_BARS*4*ppq
  for tr in m.tracks:
      t=0
      for msg in tr:
          t+=msg.time
          if msg.type in('note_off','note_on') and getattr(msg,'velocity',0)==0 and t>end:
              print('TAIL PAST LOOP END', tr.name, msg.note, (t-end)/ppq,'beats')
  ```
  Any hit is a FAIL for pads, swells and strings. For the bass, a tie across the barline that is *meant* to land on beat 1 (Local Dub F#→ downbeat) is fine when it is noted. Also make sure the MIDI `end_of_track` isn't padded: check the file length against the loop length.
- Audio check, on bounces and previews: compare the RMS of the first 1/16 of bar 1 in the *looped* render with a *cold-start* render, or with the bar-1 RMS when nothing should be sounding (for example, with that part muted):
  ```python
  import soundfile as sf, numpy as np; y,sr=sf.read(F); y=y.mean(1) if y.ndim>1 else y
  s16=int(sr*60/BPM/4); head=y[:s16]; tail=y[-s16:]
  print('bar1 head dBFS', 20*np.log10(np.sqrt((head**2).mean())+1e-12), ' last 16th', 20*np.log10(np.sqrt((tail**2).mean())+1e-12))
  ```
  - Bar-1 head energy from that part more than −40 dBFS, when it is meant to start silent → FAIL.
  - A wrap-around (`wrap()` in the render scripts adds the tail back to the start) is only allowed if the bar-1 chord or swell is meant to be heard there.
  - Spectrally, look for reverb wash: a broadband decay in the first 200 ms of bar 1.

**4.2 2–3 options max per delivery.** Count the distinct ideas, not the files. Stems belonging to one idea count as one option.

**4.3 Only the requested changes were made.** Diff against the previous version:
- Read the brief or feedback and list the requested changes.
- Diff per part with:
  ```
  python3 - <<'X'
  import sys; sys.path.insert(0,'/workspace/music-team/tools'); import originality_check as oc
  a=oc.load_streams('PREV.mid'); b=oc.load_streams('NEW.mid')
  for k in b: 
      ea=set(a.get(k,([],))[0]); eb=set(b[k][0]); print(k,'removed',sorted(ea-eb)[:20],'added',sorted(eb-ea)[:20])
  X
  ```
  Slots are 16th indices from bar 1. Also diff tempo, track list, note lengths and velocities (`mido` dump).
- Any changed part, bar or parameter that wasn't requested is a FAIL. One example is "fixed" bars Melak didn't mention. Another is changing the loved stab voicing A3–D4–G4–C5 when only the rhythm was asked about.
- Loved material must come back byte-identical unless a change was requested. That covers the quartal stab voicing and R1's response placement.

**4.4 File hygiene:**
- Every MIDI loads in mido, 4/4, at the stated tempo.
- Bar 1 is at tick 0, the loop is a multiple of 4 bars, there are no stuck notes (note-on without note-off), and track names are meaningful.
- Previews are normalised to similar loudness, within ±1 LU, so the A/B is fair.

## 5. Pass/fail rules
- **FAIL (back to the Composer):** any one of these:
  - an originality FAIL
  - an unresolved originality FLAG
  - a taste-fit FAIL (see §1)
  - any §2 rule broken without a stated reason
  - a tail wrapping into bar 1
  - more than 3 options
  - an unrequested change
  - a broken file
- **PASS WITH NOTES:** everything passes, but there are minor issues, such as a velocity layer missing or a naming fix. It goes to Maestro with the notes, and the Composer fixes them in the next round.
- **PASS:** every box is ticked. It goes to Maestro, and then Melak.

## 6. Verdict template
```
A&R VERDICT: <sketch_vN> / <option name>        Date/time (ET):
Overall: PASS | PASS WITH NOTES | FAIL
1. Taste fit:        PASS/FAIL. <bass register, leaps, pcs, loop length, density, stab role; 1 line each where relevant>
2. Rulebook:         PASS/FAIL. <rule cited (playbook §x / structure §y) + measured value>
3. Originality:      PASS/FLAG/FAIL. script exit <n>; worst ref <file>: skel_win <x>, skel_ngram <x>, voicing <x>, int_lcsub <n>
                     matching bars: <cand bars a-b ~ ref bars c-d, ±st>; audio/sample check: <clean / issue>
4. Technical:        tails <ok / note X ends +n beats past loop>; options <n>; changes vs <prev>: <requested only / unrequested: ...>; files <ok>
Required fixes (if FAIL): 1) ... 2) ...
Keep (do not touch): <loved elements>
```
