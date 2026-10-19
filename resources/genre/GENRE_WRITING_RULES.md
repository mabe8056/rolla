# GENRE WRITING RULES v1.2

The rulebook for ROLLA's rule composer. Its machine-readable twin is
`genre_writing_model.json` (schema `rolla.genre_writing_model/1`, version 1.2).
`tools/gen_rule_tables.py` compiles the JSON into `src/Generator/RuleTables.h`,
and `src/Generator/RuleComposer.cpp` samples from that header only.

**What this is.** Pooled, smoothed aggregate statistics per style: first-order
degree tables, per-16th onset weights, length and interval distributions,
density curves, stab-versus-bass conditional weights, and a chord-quality
palette. It stores **no phrases**: no note sequences, rhythm cells, motifs,
n-grams above first order, per-source tables, source counts or names. Every
roll is composed fresh from these numbers, and nothing is cached between rolls.

Style keys: ROLLER = `ukmin`, DEEP = `long`, BOUNCY = `pop`, JACKIN = `jackin`.
Slots are 0-15 in the JSON (0 = beat 1; kick steps 0/4/8/12).

## Changelog
- **v1.2**: built on v1.1 (the de-peaked stab grids and the songwriting layer).
  The changes, by JSON field:
  - **Bass density** (`styles.S.bass.density_curve`, `density_median`): the means
    are rescaled to the audio medians: ROLLER 5.0, JACKIN 4.4, DEEP 5.2 (dark
    5.5, sparse 4.3), and BOUNCY 5.0 (bounce 4.5, pulse 5.5) notes a bar. The
    shape and sd are kept. `onset_prob` is used as slot weights only.
  - **Length control** (`bass.sounding`): each note holds `gap_fraction` of the
    gap to the next onset (ROLLER 0.81, JACKIN 0.83, DEEP 0.79, DEEP sparse
    0.70, BOUNCY pulse 0.93). Median-length guards (`len_median_16ths`):
    ROLLER 1.0-2.4, JACKIN 1.5-2.6, DEEP 1.8-4.0 (dark 1.4-4.0), BOUNCY pulse
    1.8-3.2. Notes of 6 sixteenths or more stay under 15% outside DEEP. BOUNCY
    bounce has its own length band of 0.9-1.4 sixteenths. The 0.85-0.975
    sounding band applies to pulse only.
  - **Key safety** (`modes.weights`, `modes.tonic_guard`): only Aeolian, Dorian and
    Phrygian are allowed, all inside the scorer's scale. JACKIN uses Aeolian or
    Phrygian, never Dorian. Major and Mixolydian are removed. Tonic guard: the
    root is the most-sounding bass pitch class, and the root plus the next
    pitch class hold at least 0.55 of bass time (JACKIN 0.45). The ROLLER b2
    cap is 6% outside Phrygian (was 12%).
  - **BOUNCY pulse** (`submodes.pulse.batch_share`, `quota`): pulse is a batch
    quota of 30-40% (6-8 rolls of 20), set through the seed index. It is no
    longer an independent 25% draw.
  - **Stabs** (`stab.hits_per_bar_cap`, `on_beat_ceiling_pct`, `modes`): the cap
    is 3 hits a bar at every knob value, and there is no 4-10 hit riff mode.
    On-beat ceilings: DEEP 20%, JACKIN 30%, ROLLER and BOUNCY 30%.
  - **JACKIN loop** (`bass.repeat`): the 1-bar repeat stays at or under 0.10, and
    the 2-bar repeat at or over 0.70.
  - **Bass on the kick** (`bass.kick.target_pct`): kick steps are weighted
    toward the per-style medians: ROLLER 17, DEEP 7, BOUNCY 30, JACKIN 19
    (percent of bass attacks). A roll over 35% is composed again.
  - **Variation** (`global_rules.variation`, `thin_cell_guard`, `batch`): at least
    60% of bar-to-bar mutations fall in the back half (slots 8-15), and the
    notes-per-bar range is 2 or more over 8 bars. Each onset carries one note.
    No two rolls of a batch may be twins (Jaccard of 0.80 or more on the
    bars 1-2 grid). In bars 5-8, a bar of 4 or fewer attacks on one pitch
    class may not occur in 2 or more bars, and each bar carries at least 2
    pitch classes.
  - **Swing** (`global_rules.hat_swing_pct_band`, `drums.hat_swing_pct`): the
    style's default 16th hat swing is clamped to 53-57%. The bass sits 0 to
    -2 swing points from the hats, with a floor of 51.
  - **Originality** (`stab.voicings`, `stab.transitions`, the grids):
    - Chord shapes held by a single source are removed.
    - The ROLLER share of the shared stab-voicing prior is subtracted from the
      other styles.
    - Stab transition rows where one source held more than 35% of the row are
      replaced by the style's chord-bottom unigram, then the cell ceiling and
      entropy floor are applied again.
    - The ROLLER stab conversation grids are mixed 30% toward flat, and the
      DEEP, JACKIN and BOUNCY bass body grids 15% toward flat (the expected
      count is kept).
    - The open-fifth-only DEEP voicing is capped so it stays under 25% of hits.
  - **Stripped**: provenance, source counts and ids, names, raw grids, the
    timing and arrangement tables of the songwriting layer, and the pad and vox
    tables (the generator does not read them yet).
- **v1.1**: songwriting layer (groove timing, note choice by beat strength,
  arrangement event map, lane conversation) and de-peaked stab grids.
- **v1.0**: core tables, entropy floors.

## 1. Generation order (what the composer does)
1. **One mode per roll** (`styles.S.modes.weights`, tilted by DARK: Dorian
   brighter, Phrygian darker), shared by every pitched lane. BOUNCY picks
   bounce or pulse (pulse on its batch quota), and DEEP picks dark (65%) or
   sparse (35%).
2. **Bass rhythm.**
   - Each bar draws a note count from `bass.density_curve[b]` (mean, sd), scaled
     to the submode target.
   - Onset slots are drawn without replacement from `bass.onset_prob`
     (body / bar4 / bar8), used as weights. Kick steps are weighted so the
     expected on-kick share is `kick.target_pct`. There are no runs over 2
     adjacent 16ths, and kicks stay under 35% bar by bar.
   - Bar 1 is the call. Bar 2 answers it: it keeps the front half and redraws
     the back half, or for JACKIN it is drawn into the call's silence.
   - Bars 3-8 restate the 2-bar cell. Bars 5-7 mutate their back half toward
     the density curve.
   - Bar 4 answers bar 2 in its back half with P `phrase.bar4_answer_prob`. The
     density change comes from `call_answer.density_delta_bar4_vs_bar2_dist`,
     at least 3 slots change, and the last note follows
     `call_answer.ending_degree_bar4_dist`.
   - The last bar rests from slot 12, then plays one `phrase.bar8_palette` move
     with P `bar8_answer_prob`: drop-out, truncation, fill, displacement, pitch
     answer, octave lift, or a pickup. The bar-8 pickup itself is
     `global_rules.pickup`: P 0.25 a roll, at least 15% of a batch.
3. **Bass pitch.**
   - Pitch follows a first-order chain: `bass.transitions[prev]` times
     `bass.note_choice[strength of the slot]`, masked to the mode.
   - The 5th and above may sit under the root, as `bass.interval_dist` decides.
   - Natural 2 is masked out in Phrygian. Outside Phrygian, b2 never comes from
     the chain.
4. **Bass length**: `gap_fraction` times the gap to the next onset, then the
   median-length and long-note guards. A note never reaches the next attack,
   the turn rest, or (a b3 late in a bar) the barline.
5. **Guards**, each a recompose: a kick share over 35%; an 8-bar onset
   histogram whose entropy falls under `onset_grid` floor minus 0.15; the
   tonic guard; the thin-cell guard; a duplicate onset. As a last resort, the
   tonic guard turns the longest non-root notes into roots.
6. **Stabs.**
   - Per bar, at most 3 hits are split over bass gaps, bass sustains and (only
     BOUNCY) bass attacks. Kick steps are never used.
   - Inside each class, slots are weighted by `stab.p_onset_given_bass` (rest /
     sus / onset). Gap slots get `gap_weight` (1.6), toward a batch median of
     about 22% of stabs in gaps.
   - A slot gets 3x weight where the stab answers a bass phrase end
     (`answer_after_bass_phrase_end_dist`).
   - Each roll has two voicings, one per bar parity. The chord bottom comes
     from `stab.transitions` and `note_choice`. The voicing comes from
     `stab.voicings`, keeping only shapes whose tones are all in the mode and
     that make no m2 / m9 with the bass sounding under the hits. Shapes are
     weighted by `consonance_ic_dist`.
7. **Originality**: every roll goes through the originality gate (built-in
   banned figures plus the private hashed index), and a FAIL is redrawn
   before output. `rolla-rolls` logs `redraws=N` per roll.

## 2. Anti-copy design
- Aggregate only, first order only, pitch as scale degrees (key-independent).
- **Pooling**: each style table is blended with the all-style table. No single
  source may carry more than 35% of any table (`anti_copy.single_source_share_cap`).
- **Transition cell ceiling** 0.60: the excess is spread over the row in
  proportion to the unigram.
- **Entropy floors** (normalised Shannon entropy, H / log K), from
  `anti_copy.entropy_floor`:
  - onset grids 0.55;
  - transition rows 0.45;
  - interval distributions 0.40 (bass) and 0.35 (stab);
  - degree unigrams 0.45.

  The floors hold offline and are enforced again at run time: before the mode
  mask, a table under its floor is mixed toward its prior in 0.02 steps. Onset
  grids mix toward their own mean, and transition rows toward the style unigram.

## 3. Hard rules (all styles)
- One mode per roll.
- **b2 bass-time caps**: ROLLER 6%, DEEP 6%, JACKIN 6%, BOUNCY 3%. Phrygian
  allows up to 30%.
- **No m2 / m9** between the bass and a stab / pad tone at any sounding moment.
- **Stab**: at most 3 hits a bar at every STAB value, register up to MIDI 88.
  The top of the STAB knob adds density through more full bars, not more hits
  in a bar.
- **Bass on the kick**: 35% hard cap.
- **Bar-8 pickup**: slots 12-15, 1-3 notes, approaching the root by step or
  from the 5th. P 0.25 a roll, at least 15% of a batch.
- **DEEP**: open-fifth-only voicings stay under 25% of hits.
- **Swing**: style defaults inside 53-57%.

## 4. Per-style summary (v1.2 numbers)
| style | modes (Dorian / Aeolian / Phrygian) | bass notes/bar | on-kick target | gap fraction | median length band | stab on-beat ceiling |
|---|---|---|---|---|---|---|
| ROLLER | 0.38 / 0.51 / 0.10 | 5.0 | 17% | 0.81 | 1.0-2.4 | 30% |
| DEEP | 0.35 / 0.53 / 0.12 | 5.2 (dark 5.5, sparse 4.3) | 7% | 0.79 (sparse 0.70) | 1.8-4.0 (dark 1.4-4.0) | 20% |
| BOUNCY | 0.63 / 0.38 / 0 | 5.0 (bounce 4.5, pulse 5.5) | 30% | pulse 0.93 | pulse 1.8-3.2, bounce 0.9-1.4 | 30% |
| JACKIN | 0 / 0.58 / 0.42 | 4.4 | 19% | 0.83 | 1.5-2.6 | 30% |

The pooled data behind DEEP and BOUNCY is thin and leans toward the all-style
tables, so treat it as low confidence. Pad tables are still estimates and are
not shipped in v1.2.

## 5. Rebuilding
The model is rebuilt from private material that stays outside this repository.
After any change to `genre_writing_model.json`, run
`python3 tools/gen_rule_tables.py`. Use `--check` to verify that the header
matches the model. `tools/check_repo_clean.py` fails the build if a file
under `resources/genre/` carries a forbidden key (provenance, source ids or
counts, names, files, raw grids), a reference name, or a reference id.
