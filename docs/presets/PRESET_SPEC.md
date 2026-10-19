# ROLLA factory presets: PRESET_SPEC v1.3 (2026-10-17)

Owner: Sound Designer. For: Maestro and the JUCE engineer. Status: **target values, not yet rendered**. Every preset has to pass the render-and-measure harness (DESIGN §14.5) before it ships.

**v0 scope (Maestro, 2026-10-17):**
- **Synth lanes** (6 parts × 3 presets = 18 presets) go to the **MAIN** output: BASS · STAB · HITS · PLUCK · PAD · VOX CUTS.
- **DRUMS lane** goes to the **DRUMS** output. It has 2 kits, and Melak's one-shots are the primary voice for every drum sound. The v1.2 circuit / metallic-bank voice stays as an optional synth layer.
- **Every lane is sample-driven (v1.3, Melak):**
  - ROLLA is Melak's personal instrument, not for sale.
  - His own Sample Market / Tech House Market sounds fuel every lane. The synth engine shapes them (pitch to key, filter / amp envelopes, formant, granular, stretch, reverse, slicing) and layers them with its oscillators.
  - Sample sets live in `~/Music/ROLLA/Samples/` (§12.3). The curated set is listed in `plugin-design/samples/SAMPLE_MANIFEST.json`.
- Everything else is in §10 Post-v0.

**Dialects.** Each synth preset is tied to one dialect (DESIGN §4 / mockup `DIALECTS`): **UK MIN · LONG · POP · GASKIN 90s**. The dialect picks which preset (and kit) loads by default (§1).

**Naming.** Preset names follow DESIGN §14.2: plain, uppercase, no brand names. A preset's full ID is `PART · DIALECT · NAME`.

## 0. Conventions

### 0.1 Labels and sources
- **[M]** = measured, with the measurement named.
- **[D]** = derived by arithmetic from measured values.
- **[R]** = recommended (my judgement, grounded in the measured target it serves).
- **[L]** = locked by the Composer or A&R, taken from an actual render. It counts as measured: the value was rendered and A&R passed it.
- **Drum-measurement sub-labels (v1.2):**
  - **[M-melak]** = measured on Melak's one-shot packs (`references/melak_oneshots/`);
  - **[M-909free]** = measured on the CC0 909 set (`references/909_free/`);
  - **[M-ref]** = measured on the reference tracks (SB / CTR / sum).
- **[M-bank]** (v1.3) = measured on Melak's soundbank (`references/melak_soundbank/`) by `plugin-design/samples/_tools/catalog.py`. One row per file in `samples/CATALOG.csv`.
- **CAT** (source tag) = `plugin-design/samples/CATALOG.csv`, `CATALOG.md` and `SAMPLE_MANIFEST.json`.
- **DM** (source tag) = `<private refs>/drum_oneshots/` (`drum_measure.py`, `drum_measure.json`, `drum_kit_picks.json`, `bank_check.py`).

| Tag | Source file |
|---|---|
| SB | `music-team/rulebook/SOUND_BIBLE.md` v1 |
| PT | `music-team/writing/sketch_v1/PATCHES.md` |
| SR | `music-team/writing/sketch_v1/direction_A_full/sound-recipes.md` v1.1 |
| RR | `writing/sketch_v1/render_report.json` (Composer render: bass −18.75 LUFS, stab −26.5 LUFS) and `direction_A_full/renders/render_report.json` (swell −24.4, string −28.5 LUFS) |
| CTR | private cross-track rules (`<private refs>`) |
| sum | per-track summaries (`<private refs>/*/summary.md`) |
| DS | `plugin-design/DESIGN.md` |
| PTD | `direction_A_full/PATCHES_TODO.md` |
| MIX | `plugin-design/MIX_SPEC.md` v1 + `mix_profiles.json` 1.0.0 + `tests/targets.json` (Mix Engineer, 2026-10-17): lane loudness, TP caps, sample prep §4. Its own [M] / [D] / [N] tags carry over |

### 0.2 Tempo grid (130 BPM) [D]
| Division | Time |
|---|---|
| 1/64 | 28.8 ms |
| 1/32 | 57.7 ms |
| 1/16 | 115.4 ms |
| 1/8T | 153.8 ms |
| 1/8 | 230.8 ms |
| 3/16 (dotted 1/8) | 346.2 ms |
| 1/4 | 461.5 ms |
| dotted 1/4 | 692.3 ms |
| 1/2 | 923.1 ms |
| 1 bar | 1846.2 ms |

All delay times are **tempo-synced**. The ms value applies at 130 BPM only.

### 0.3 Pitch
All notes are written for **D minor**. The bass root is **D1 = 36.71 Hz** [M, SR §0]. Everything else is key-relative: ROLLA transposes with the root.

### 0.4 Tail rule, every part
- Delay and reverb returns are gated at the barline (DS §14.3 tail gate): a **squared fade over the last 1/16 (115.4 ms), fully closed for the last 10 ms**.
- The Composer's render measures **−73 to −80 dBFS in the last 10 ms** [L, PTD 5]. The target is **≤ −45 dBFS** [L].
- On the bar-8 slot-16 hit, delay sends are muted [L, SR §7].

### 0.5 Outputs, loudness and headroom
**v0 has two stereo outputs** [Maestro decision]:
- **MAIN** = BASS + STAB + HITS + PLUCK + PAD + VOX CUTS, summed inside ROLLA;
- **DRUMS** = the 909 lane.

**v1.3: absolute levels re-targeted to MIX_SPEC v1 §2.2 / §2.3** (Mix Engineer, `plugin-design/MIX_SPEC.md` + `mix_profiles.json`) [MIX].
- The v1.2 anchor (bass −18 LUFS-S, MAIN / DRUMS buses −15, kick solo peak −6 dBFS) summed to ≈ −12.1 LUFS and could not give a −6 dBTP summed peak at Melak's pre-master PLR (MIX_SPEC §2.2). It sat **6–7 dB too hot**.
- New anchor: **KICK −23.7 LUFS-S** in every dialect; **BASS = KICK −1.0 = −24.7 LUFS-S** in every dialect (Maestro-approved clamp, GASKIN 90s included). Every other lane takes its dialect's measured level re kick from MIX_SPEC §2.3 (TOP trim included); HITS are pinned to bass −3 LU.
- **Level keys are locked** in `mix_profiles.json` (`lufs_offset_vs_kick_db`, `st_target_lufs`, `output_trim_db`): a preset cannot move its lane's level. So the v1.2 per-preset offsets (SWELL STAB −2, TICK +1, HOOVER −1, WIDE SWELL −1) are **dropped**; the lane × dialect cell is the target.

**Per-preset rule (v1.3).** Each preset's **level trim** (DS §14.2) is set by the harness so that:
- the solo test-pattern render hits the **lane target of the active dialect** (table below; ST LUFS max = loudest 3 s window, 100 ms hop, DUCK on, returns included) within the MIX_SPEC tolerance;
- **true peak ≤ the lane's TP cap** from MIX_SPEC §2.2 (TP = ST + ref PLR max + 2 dB; `tests/mix_check.py` applies the same cap in every dialect).
- **The v1.2 "sample peak ≤ −6 dBFS per preset" rule is retired.** MIX_SPEC does not use a per-lane −6 dBFS peak: its lane caps run −27.7 to −3.2 dBTP. The −6 figure survives where MIX_SPEC agrees: the **summed output true peak −6 dBTP ±1** and the **sample-prep true-peak cap of −6 dBTP** (§0.8, §12.3 S16).
- A preset borrowed by another dialect (e.g. B3 ROUND as the GASKIN 90s bass) takes that dialect's target.

| Lane | UK MIN | LONG | POP | GASKIN 90s | Tol | TP cap dBTP | Basis |
|---|---|---|---|---|---|---|---|
| DRUMS:KICK | **−23.7** | −23.7 | −23.7 | −23.7 | ±0.5 | −11.0 | [MIX D] anchor |
| BASS | **−24.7** | −24.7 | −24.7 | −24.7 | ±0.5 | −8.9 | [MIX D clamp] kick −1.0 |
| STAB | −26.8 | −32.6 | −26.0 | −24.3 | ±1 | −7.1 | [MIX M] "other" re kick + TOP trim |
| PLUCK | −26.8 | −32.6 | −26.0 | −24.3 | ±1 | −7.1 | [MIX D] shares the "other" budget; **−3 dB each when STAB and PLUCK both play** |
| HITS | −27.7 | −27.7 | −27.7 | −27.7 | ±1 | −8.0 | [MIX A&R] bass −3 LU, not trimmed |
| PAD | −32.7 | −33.0 | −34.5 | −31.7 | ±1.5 | −20.7 | [MIX] bass −8 (PRESET_SPEC D) + TOP trim |
| VOX CUTS | −28.7 | −27.2 | −25.2 | −27.7 | ±1 | −3.2 | [MIX M] (GASKIN: UK MIN fallback; VOX absent in the ref) |
| DRUMS:CLAP / SNARE | −26.2 | −26.0 | −29.0 | −28.9 | ±1 | −5.9 | [MIX M] |
| DRUMS:RIM (ghost) | −31.2 | −31.0 | −34.0 | −33.9 | ±1.5 | −6.7 | [MIX] clap −5 |
| DRUMS:OH | −28.3 | −26.2 | −30.3 | −29.8 | ±1.5 | −9.7 | [MIX M] |
| DRUMS:CH | −40.3 | −38.2 | −42.3 | −41.8 | ±1.5 | −21.7 | [MIX D] OH −12 |
| DRUMS:RIDE | −46.3 | −44.2 | −48.3 | −47.8 | ±1.5 | −27.7 | [MIX] OH −18 |
| DRUMS:PERC (perc / tom / shaker) | −31.3 | −32.4 | −36.2 | −35.0 | ±1.5 | −6.8 | [MIX M LOW] |

All values are ST LUFS max of a **solo** render at unity fader (MIX_SPEC §5 render protocol: ≥ 6 s loop, the style's default preset and pattern). Source of truth: `mix_profiles.json` → `dialects.<D>.lanes.<LANE>.st_target_lufs` and `…DRUMS.voices.<V>.st_target_lufs`; if this table and the JSON ever disagree, the JSON wins.

**Summed output (MAIN + DRUMS), the governing gate** [MIX §2.1]:
- **−19.0 LUFS-S max ±0.5 and true peak −6 dBTP ±1 (≤ −5.0)** in every dialect, with the dialect's default lanes (UK MIN / LONG / POP: kick, bass, stab, clap, hats, perc, vox; GASKIN 90s: the same, minus VOX, plus HITS). Spread across dialects ≤ 1.0 LU (`mix_check.py styles`).
- The per-dialect **MAIN output trim** is 0 dB by design and only absorbs a measured residual within ±1 dB. If the sum is off, the harness moves the output trim, never the per-preset trims, so the lane balance stays fixed (rule kept from v1.1).
- ROLLA's safety limiter (DS §14.4) must show **0 dB gain reduction** on the summed test.

**Bus targets** [D from the MIX lane targets: power sum of the default lanes minus MIX_SPEC's per-dialect calibration offset (lanes don't peak together): UK MIN 1.03, LONG 1.30, POP 1.02, GASKIN 0.48 dB]. MIX_SPEC itself gates only the sum, so these are **report-and-diagnose** values (±1 LU), not extra gates:

| Bus | UK MIN | LONG | POP | GASKIN 90s |
|---|---|---|---|---|
| MAIN (default lanes: bass + stab + vox; GASKIN bass + stab + hits) | −22.7 | −23.6 | −21.5 | −21.0 |
| DRUMS (kit default pattern: kick + clap + CH + OH + perc) | −21.5 | −21.3 | −22.7 | −22.0 |
| **SUM** (model) | **−19.0** | −19.3 | −19.1 | −18.5 |

- The model's LONG and GASKIN sums sit at the edge of ±0.5; the render test decides (MIX_SPEC §2.1.5).
- **v1.3c note:** this table still uses the v1.3a single per-dialect calibration. With the measured class calibration (§0.5.1), the drums-only bus models at −20.9 / −21.5 / −22.2 / −22.2 and the default-set sum at −19.0 / −19.3 / −18.7 / −18.5 LUFS-S. These stay report-only; `output_trim_db` from renders sets the default sum.
- **All six MAIN lanes on at once** models about 1 LU over −19 (UK MIN −18.0) before the MAIN auto-trim. §0.5.1 trims it back (−0.6 dB, all dialects).
- No per-bus peak cap: the sum's −6 ±1 dBTP governs (MIX_SPEC §2.2 SUM row).

**MAIN + DRUMS ≈ −19 LUFS-S pre-master** [MIX D]. That leaves Melak's master chain about 6 dB of true-peak headroom (Melak's own pre-master bounces measured PLR 11.6–14.2). For comparison, the refs are mastered to −7 to −9 LUFS [M, CTR E8].

### 0.5.1 MAIN auto-trim (v1.3a, Maestro decision on Q13)
**Decision.** A fixed, data-driven **sum trim** keeps the summed output (MAIN + DRUMS) at **−19.0 ±0.5 LUFS-S** whatever MAIN lanes are active. There is **no user knob**. Table: `presets/main_sum_trim.json`, generated by `presets/_tools/main_sum_trim.py`.

**Model** [D from MIX]: summed ST max = power sum of the active lanes' `st_target_lufs` (`mix_profiles.json`) plus the kit-default drum voices (kick, clap, CH, OH, perc), minus a **calibration offset that depends on the lane-set class** (v1.3c; measured by the Mix Engineer, `tests/targets.json` → `combo_model.<D>.cal`). Class = D (drums) + B (bass) + O (stab / pluck / hits / pad) + V (vox). UK MIN values: D 0.43, DB 0.66, DO 0.63, DV 0.81, DBO 0.69, DBV 1.02, DOV 1.35, DBOV 0.95 dB. When STAB and PLUCK both play, each is −3 dB (§0.5).
- **Why v1.3a was 0.52 dB high on UK MIN drums-only** (+2.46 vs the measured +1.94): v1.3a used one calibration per dialect (UK MIN 1.03, the full-mix value). Calibration is how much the power sum of the lanes' ST maxima over-reads the real sum, because lanes don't peak in the same 3 s window. The drum voices share one grid and peak together, so drums alone over-read by only 0.43 dB, against 0.95 dB for the default class (DBOV: bass + stab + vox + drums). The drums-only sum was therefore modelled 0.52 dB too quiet, and the trim came out 0.52 dB too big. This is a calibration-by-class effect, not a fixed drum-bus offset: a constant bus offset would cancel in trim = model(default) − model(active).
- With the class calibration, all 256 entries match the Mix Engineer's `trim_with_measured_cal_db` (`tests/results/trim_conflicts.csv`) exactly (max difference 0.00 dB).
- **trim_dB = model(dialect default MAIN lanes) − model(active MAIN lanes).** It is **0 dB at each dialect's default lane set**. That set is the one MIX_SPEC tunes to −19.0 with the render-measured `output_trim_db`, so the two trims never double-count. (`trim_abs_db` = −19.0 − model is in the JSON for information only. Per-dialect **`default_model_residual_db`** (renamed in v1.3c from `default_trim_db`): UK MIN 0.0 dB · LONG +0.3 dB · POP −0.3 dB · GASKIN 90s −0.5 dB. It is **INFORMATIONAL ONLY and must NEVER be applied by the engine**: the render-measured `output_trim_db` already sets the default lane set to −19.0, so applying the residual too would double-count.)
- Positive trims mean fewer or quieter lanes than the default; negative trims mean more.

**Trim by number of active MAIN lanes** (dB; median (min … max) over the lane combinations of that count):

| Active MAIN lanes | combos | UK MIN | LONG | POP | GASKIN 90s | max dev within count |
|---|---|---|---|---|---|---|
| 0 (drums only, info) | 1 | **+1.9** | **+2.1** | **+3.5** | **+3.7** | 0.00 dB |
| 1 | 6 | +1.3 (+0.8 … +1.9) | +1.7 (+0.5 … +2.0) | +2.1 (+1.8 … +3.1) | +2.3 (+1.8 … +3.4) | 1.22 dB |
| 2 | 15 | +1.0 (+0.1 … +2.0) | +1.3 (+0.1 … +2.0) | +1.3 (+0.7 … +2.2) | +1.3 (+0.5 … +2.5) | 1.25 dB |
| 3 | 20 | +0.4 (−0.3 … +1.4) | +0.7 (−0.3 … +1.8) | +0.7 (0.0 … +1.8) | +0.8 (0.0 … +1.8) | 1.15 dB |
| 4 | 15 | 0.0 (−0.5 … +1.3) | 0.0 (−0.4 … +1.2) | +0.1 (−0.5 … +1.1) | 0.0 (−0.4 … +1.1) | 1.30 dB |
| 5 | 6 | −0.5 (−0.6 … +0.7) | −0.3 (−0.6 … +0.7) | −0.3 (−0.5 … +0.3) | −0.3 (−0.6 … +0.5) | 1.20 dB |
| 6 | 1 | **−0.6** | **−0.6** | **−0.5** | **−0.6** | 0.00 dB |

**What the data shows.**
- **Dialect matters:** at 1 active lane the medians run +1.3 (UK MIN) to +2.3 dB (GASKIN 90s), because the drum bus sits lower there (drums-only model: POP −22.2 and GASKIN 90s −22.2, vs UK MIN −20.9 LUFS-S), so MAIN carries more of the sum.
- **The combination matters more than the count:** within one count the trim spreads by up to **1.30 dB** from the median (UK MIN, 4 lanes: BASS + PLUCK + HITS + PAD −0.5 vs STAB + PLUCK + PAD + VOX +1.3). That is over the ±0.5 LU tolerance, so a count-only table would miss the target.
- **So the normative key is dialect × active-lane set:** a 6-bit mask (bit order BASS, STAB, PLUCK, HITS, PAD, VOX), 64 entries per dialect, 256 in all (`dialects.<D>.by_mask`). The count table above is the summary.
- Range over all non-empty sets: −0.6 dB (all six on) to +3.4 dB (GASKIN 90s, PAD alone). Drums only: +1.9 (UK MIN), +2.1 (LONG), +3.5 (POP), +3.7 (GASKIN 90s).

**Engine rule (E21).**
- **Active** = lane unmuted and its pattern has notes in the current bar. Muting, soloing or emptying a lane changes the mask.
- On a mask change, the trim ramps to the new table value over **50 ms, raised-cosine, linear in dB**: the same ramp as `output_trim_db` in MIX_SPEC §9.2. A change mid-ramp restarts from the current value.
- **Lane off:** start the ramp when the lane's output has fallen 40 dB below its level (or after its release, max 500 ms), so a ringing tail isn't boosted.
- **Lane on:** start the ramp at the lane's first note-on.
- The trim sits at the MAIN output stage, after the per-dialect `output_trim_db`. It moves every lane equally, so the kick / bass invariants and lane balance are untouched.
- **Solo-lane harness renders bypass it**, so the §0.5 per-lane targets are measured without it.
- **Headroom guard** (the peak-cap / ST-floor conflict on 87 combos estimated over −5 dBTP is **pending Maestro's decision**; unchanged in v1.3c): positive trims (up to +3.7 dB, drums only) must not push the sum over −5.0 dBTP. The safety limiter must still show 0 dB GR on the `styles` renders of the 1-lane sets. If a positive trim fails that, cap it at the largest value that passes and log it.
- **Harness:** for each dialect, render the default set, all six lanes, and the loudest and quietest set of each count. Pass = summed ST −19.0 ±0.5.

**Test patterns (harness):**
- BASS: the dialect's default roll, 8 bars, D minor.
- STAB: the generator stab for the dialect's voicing.
- PLUCK: a 1-bar hook of 6 off-kick notes, D4–D5.
- HITS: slot 16 of every bar, plus slot 7 of bar 2.
- PAD: a 2-bar chord, note-on 1/32 early.
- VOX: slots 3/7/11/15, cycling through the 4 vowels.
- DRUMS: the kit's default 1-bar pattern (§9), 8 bars.

### 0.6 Width and low end, every part
- **Bass:** mono, full band.
- **Every other part** is mono below its "mono-below" frequency and HPF'd at or above it. SB §9 rule: everything under 150 Hz mono [M, mix corr below 120 Hz = 0.993].
- **Harness gate [D]:** the mix of all 6 parts must keep **L/R corr ≥ 0.99 below 120 Hz** and **≥ 0.99 for the bass**.

### 0.7 DUCK (DS §14.3)
- Host-synced ghost kick on every quarter: attack 0.1 ms, hold 20 ms, then **depth / recovery per preset**.
- "Gate" shape (POP): the depth is reached in 0.1 ms, held 40 ms, and recovers in 60 ms [R from DS "gated at kick"].
- **Ghost-trigger values from Direction A** [L, SR lock 2026-10-17]: reveal 8 dB, drop 13 dB, stab 3 dB.

---

### 0.8 Sample-driven lanes (v1.3): conventions
Every lane preset has a **`sample` block in Maestro's format**:

```json
"sample": {"mode": "SAMPLE|LAYER|SYNTH", "file": "BASS/name.wav", "root": "D1", "start": 0.0, "reverse": false,
           "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["BASS/alt1.wav", "..."]}
```

**Fields:**
- **`mode`:**
  - `SAMPLE` = the sample is the only source. It still runs through the preset's filter, envelopes, FX, DUCK and width.
  - `LAYER` = the sample plus the preset's synth oscillators.
  - `SYNTH` = the v1.2 synth patch alone. This is also the fallback if the file is missing.
- **`file`:** relative to the Samples root (`~/Music/ROLLA/Samples/` on the Mac, user-overridable).
- **`root`:**
  - The note at which the file plays unshifted: the measured pYIN pitch, or the filename note plus the lane octave when pYIN was unvoiced. `root_src` in `presets_samples.json` says which.
  - The engine repitches from `root` to the played note, so everything stays in key.
- **`start`:** seconds from the file start; it skips silence and pre-roll (measured: first point at −40 dB re peak, minus 0.5 ms).
- **`loop.start` / `loop.end` / `loop.xfade`:** sample frames at the file's own rate. The end points were chosen at rising zero crossings with the best waveform match (`loop_corr` in `presets_samples.json`).
- **`layerMix`** (my reading of Maestro's field; to be confirmed, §14):
  - In LAYER mode: **sample share**, 0 = synth only, 1 = sample only.
  - In SAMPLE mode: 1.0 (or ignored).
- **`pool`:** alternative files of the same role. SHUFFLE / round-robin picks from them (E16 / §12.3 S8). The pool's `root` values come from the manifest.

**Proposed extensions** (outside Maestro's block, for the engineer to accept or reject, §14):
- `slices_s`: slice start times in seconds, for VOX phrases and the HITS phrase chops;
- `granular`: window start / end in frames, grain size in ms, density, scan speed and spray in ms, for pads;
- `sampleLayers`: extra sample layers (atmos bed, reverse swell, slice source), each a full sample block plus a `role`.

**Gates are unchanged.** The harness measures the rendered output of SAMPLE / LAYER presets against the same "Must measure" rows, loudness rules, mix gates and DUCK values as v1.2. The sample's own numbers [M-bank] are only the starting point.

**Sample prep (v1.3, MIX_SPEC §4).** Every staged file has been prepped once on the box, **on the copies only** (the originals under `references/` are never written):
- float32 WAV at the file's native rate (44.1 / 48 kHz); DC removed (mean + 2nd-order 10 Hz HPF, zero-phase);
- leading silence trimmed at −54 dB re peak (never below −60 dBFS) with a 1 ms pre-roll and 0.5 ms fade-in; tail trimmed below −60 dBFS and a 12 ms half-Hann fade-out (longer only where a file ends hot);
- **normalised to −24 LUFS integrated** (BS.1770, gated; whole-file for files < 400 ms), **capped at −6 dBTP**.
- The engine still runs MIX_SPEC §4 on load (so a user's own replacement file gets the same treatment); on the staged files it is a no-op.

**Gain.** Maestro's block has no gain field. The per-file prep gain (dB), source LUFS, trims, fade and cap flag are recorded per file in `samples/SAMPLE_MANIFEST.json` → `files[].prep`; each preset in `presets_samples.json` also carries `prepGain_dB` for its primary. Because every source now enters the lane chain at −24 LUFS, the MIX_SPEC §3 compressor thresholds mean the same thing for every sample, and the lane output trim lands the §0.5 target.

**Positions address the prepped files.** `start`, `loop.*`, `granular.*` and `slices_s` were shifted by each file's lead trim (`_tools/remap.py`). Copy the **prepped** box files to the Mac, not the raw originals (§12.3 S13, open question 14).

---

## 1. Lanes, outputs and dialect defaults (v0)

### 1.1 Lanes
| Lane | Output | Presets / kits | Mute |
|---|---|---|---|
| BASS, STAB, PLUCK, PAD, VOX CUTS | MAIN | 3 each | on by default in every dialect [R] |
| **HITS** | MAIN | 3 | **normal lane** [Maestro]. Default **muted in UK MIN, LONG and POP**, **on in GASKIN 90s** |
| **DRUMS** | DRUMS | 2 kits | on by default [R]. **The generator writes the drum part** from rules, with SHUFFLE / KEEP [Maestro] (§9.6) |

**POP HITS: muted (my choice)** [R]:
1. **No measured basis.** Neither POP reference (REF_B1, REF_R3), nor any of the 9 refs, contains a rave hit [M].
2. **The POP mid/high space is already full.** CLICK STAB and TICK are the hottest stab and pluck (v1.3: −26.0 LUFS-S each, POP kick −2.3 [MIX §2.3]). REF_B1's lead riff equals the bass level [M sum], so a hit would fight it.
3. **Melak's brief is "not 90s-house"** [A&R].

Unmuting HITS in POP loads PIANO HIT, POP's own HITS preset.

### 1.2 Dialect → default preset (every lane)
| Dialect | BASS | STAB | HITS (state) | PLUCK | PAD | VOX CUTS | DRUMS kit | SWING |
|---|---|---|---|---|---|---|---|---|
| **UK MIN** | PLUCK | DUB STAB | HOOVER* (muted) | HOLLOW | SUS SWELL | HEY | UK MIN | 52% |
| **LONG** | ROUND | SWELL STAB | HOOVER (muted) | BLOOM | WIDE SWELL | OH | UK MIN | 51% |
| **POP** | CLICK | CLICK STAB | PIANO HIT (muted) | TICK | SUS SWELL* | AH UH | UK MIN | 50% |
| **GASKIN 90s** | ROUND* | SWELL STAB* | ORGAN HIT (**on**) | HOLLOW* | SWELL PAD | AH UH* | GASKIN | 50% |

\* = borrowed from another dialect, because the lane has no preset for that dialect. **Rule (Maestro-approved):** a lane with no preset for the dialect loads the **nearest** preset (below), never a muted lane. **Dialect overrides** (§12.1) can then adjust that borrowed preset.

**Why each borrow:**
- **UK MIN HITS → HOOVER.** The UK MIN hit (RAVE SAW) is post-v0. HOOVER is the least house-coded v0 hit, which fits the "not 90s-house" brief. Its HPF 150 and LONG-style pump also keep it off the bass.
- **POP PAD → SUS SWELL.** It has the shortest pad tail (R 700, 3.0 s verb, against WIDE SWELL's 1500 / 4.0 s), which suits POP's dry staccato [R].
- **GASKIN BASS → ROUND.** ROUND is the nearest tone to ORGAN:
  - both are near-sine with the energy in h1/h2 (ROUND h2 −11 dB, ORGAN h2 −3 dB);
  - both are low-passed close together (110 vs 130 Hz).

  PLUCK is a saw (odd emphasis −0.6 dB [M]) and CLICK rests at 350 Hz, so both are further away. On GASKIN 90s the **dialect overrides** (engine E17) apply: DUCK **7 dB / 110 ms** [M REF_J1/J2 7.3 / 110] instead of 18 / 180, and **glide 0**, so octave jumps snap.
- **GASKIN STAB → SWELL STAB.** The dialect's stab is a sustained ♭VI/♭VII (about 14/16 [M CTR D6]). SWELL STAB is the only slow-attack stab, and the generator supplies the ♭VI/♭VII voicing.
- **GASKIN PLUCK → HOLLOW.** It is short (−20 dB in about 120 ms), so it sits in the 2/16 dotted-8th grid [M CTR D1]. BLOOM's 2–8/16 notes would smear that grid [D].
- **GASKIN VOX → AH UH.** The JACKIN ref preview has no vocal (stem −60 dB [M]). AH UH's 1–2/16 cuts fit the 2/16 grid [D].

---

## 2. v0 preset list (3 per part)
| # | Part | Name | Dialect | One line |
|---|---|---|---|---|
| B1 | BASS | **PLUCK** | UK MIN | Locked Direction A bass: saw MID 12 dB over the sine SUB, LP24 170 Hz, +24 st env τ 25 ms, duck 13 dB / 115 ms. |
| B2 | BASS | **CLICK** | POP | Short gated saw pluck: LP24 rest 350 Hz, τ 17 ms env, about 1.1/16 notes, gate duck. Mid-bass weighted, like REF_B1. |
| B3 | BASS | **ROUND** | LONG | Triangle plus sine sub, LP24 110 Hz, slower bloom, 50 ms glide, 18 dB / 180 ms pump. GASKIN's default via overrides. |
| S1 | STAB | **DUB STAB** | UK MIN | Quartal A3–D4–G4–C5, no 3rd, LP12 2.2 kHz, 3/16 dub delay plus 1.2 s plate, about 875 ms tail, corr about 0.55. |
| S2 | STAB | **SWELL STAB** | LONG | 5+2 colour (A4+E5), 4-voice unison, 90 ms swell attack played 1/32 early, LP12 1.4 kHz, 1.8 s plate. |
| S3 | STAB | **CLICK STAB** | POP | ♭3+♭7 dyad (F4+C5), bright and short (3.2–5.5 kHz, −20 dB in about 120 ms), gated at the kick. |
| H1 | HITS | **ORGAN HIT** | GASKIN 90s | 90s house organ stab, M1-style: 3 sines (8'/4'/2⅔') plus tube drive and key click, ♭VI/♭VII, HPF 250. |
| H2 | HITS | **PIANO HIT** | POP | 90s house piano chord, M1-style: 2-op FM with a velocity-driven index plus hammer noise, rootless ♭3–5–♭7. |
| H3 | HITS | **HOOVER** | LONG | PWM pulse plus a saw an octave down, 5-voice unison plus heavy ensemble, −7 st pitch dive, 150 ms glide. |
| P1 | PLUCK | **HOLLOW** | UK MIN | Square-led hook pluck, −20 dB in about 120 ms, 3/16 ping-pong. |
| P2 | PLUCK | **TICK** | POP | Click pluck (τ 12 ms, like REF_B1's 1374→352 Hz), staccato about 1.1/16, gated. |
| P3 | PLUCK | **BLOOM** | LONG | Delayed filter bloom (peak at 10–50 ms, like REF_D2), 2–8/16 notes, 1/4 delay plus 2.4 s hall. |
| A1 | PAD | **SUS SWELL** | UK MIN | REF_R5 breakdown swell: saw ×3 unison plus an octave sine, LP12 1.8 kHz, A 150 / R 700, ensemble plus width for corr about 0.6. |
| A2 | PAD | **WIDE SWELL** | LONG | Second-breakdown atmos: 3 s-plus decay, corr about 0.38 from ensemble plus width, centroid about 2.4 kHz, 1-bar filter rise. |
| A3 | PAD | **SWELL PAD** | GASKIN 90s | Sustained ♭VI/♭VII: saw plus pulse ×2 unison, ensemble, LP12 1.8 kHz, about 14/16 long. |
| V1 | VOX CUTS | **HEY** | UK MIN | Spoken, unpitched hey/uh formant cut, dark (LPF 8 k), dry centre, dub throw. |
| V2 | VOX CUTS | **OH** | LONG | Sung, pitched oh→ah at alto size, vibrato, about 4/16 notes, long reverb. |
| V3 | VOX CUTS | **AH UH** | POP | Staccato pitched ah/uh barks, round-robin vowels, about 1–2/16, gated. |
| K1 | DRUMS | **UK MIN KIT** | UK MIN (also LONG, POP) | Tight 909: A1 55 Hz kick (+21 st, τ 25 ms, −40 dB @ 360 ms), 4-burst clap 10 ms apart, measured-ratio hat bank, 52% swing. |
| K2 | DRUMS | **GASKIN KIT** | GASKIN 90s | Looser 909: short clicky kick (−40 dB @ 150 ms, click −8 dB), 3-burst clap, darker 808-like hat bank, open "&" to −20 dB @ 230 ms, straight. |

### 2.1 Sample picks per preset (v1.3) [M-bank]
All picks are real files from Melak's packs, copied to `plugin-design/samples/<LANE>/`. Original paths are in `samples/STAGE_MAP.tsv`.

**Column key:**
- **c** = spectral centroid, Hz.
- **<200** = energy share under 200 Hz.
- **corr** = stereo correlation.
- **−20 dB** = decay to −20 dB, ms.
- **src LUFS** = source integrated loudness (BS.1770, as measured by the prep tool).
- **prep dB** = gain the MIX_SPEC §4 prep applied to reach −24 LUFS (¹ = the −6 dBTP cap bit first).

| # | Preset | Mode | Primary file | root | c | <200 | corr | −20 dB | src LUFS | prep dB | Pool | Transform (engine) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| B1 | bass.ukmin.pluck | LAYER | `BASS/THMAG_Bass_One_Shot_4_Fm.wav` | F1 | 87 | 0.97 | 0.99 | 596 | -13.9 | -10.8 | 6 | sample = transient + mid body through the same LP24 170 Hz +24 st env (tau 25 ms) as the synth saw MID; synth sine SUB carries < 60 Hz; sample HPF 45 Hz so the sub stays synth-pure |
| B2 | bass.pop.click | LAYER | `BASS/DCB_Bass_01_C.wav` | C#1 | 100 | 0.96 | 1.00 | 99 | -18.1 | -13.5 | 6 | short sampled click-bass (−20 dB @ 99 ms) over the gated saw; amp env gates both at 1.1/16 |
| B3 | bass.long.round | LAYER | `BASS/THMAG_Bass_One_Shot_1_Fm.wav` | F1 | 74 | 1.00 | 1.00 | 864 | -19.4 | -5.0 | 6 | round sampled body, sustain-looped so held notes and 50 ms glide work; triangle osc adds h2; GASKIN override (7 dB / 110 ms, glide 0) unchanged |
| S1 | stab.ukmin.dub | SAMPLE | `STAB/AR_Synth_06_C.wav` | C4 | 1563 | 0.00 | 0.76 | 1188 | -12.9 | -11.1 | 6 | RESAMPLED QUARTAL STACK: one clean single-note shot (3rd content 0.11) played by CHORD memory at 0/+5/+10/+15 st = A3–D4–G4–C5; LP12 2.2 kHz, 3/16 dub delay + 1.2 s plate as v1.2 |
| S2 | stab.long.swell | LAYER | `STAB/JF_Synth_12_C.wav` | C3 | 1268 | 0.00 | 0.46 | 740 | -12.6 | -11.4 | 6 | 5+2 colour (A4+E5) by CHORD memory from a no-3rd shot (0.07); amp A 90 ms swell + 4-voice unison synth layer; sustain loop lets the 1.8 s plate ring |
| S3 | stab.pop.click | SAMPLE | `STAB/VER_Stab_04_C.wav` | E5 | 4657 | 0.03 | 1.00 | 162 | -11.0 | -13.3 | 6 | ♭3+♭7 dyad (F4+C5) by CHORD memory; sample-start jitter 0–4 ms + round-robin over the pool; gated at the kick |
| H1 | hits.gaskin.organ | LAYER | `HITS/VER_DX7Organ_Bass_C.wav` | C2 | 93 | 1.00 | 1.00 | 882 | -10.6 | -14.2 | 6 | organ timbre from a single-note DX7-organ bass shot played +2 oct, one hit per generator note (♭VI/♭VII B♭3–D4–F4), HPF 250; synth 3-sine organ + key click layered at 0.4; pool[0] is a full minor chord-stab one-shot |
| H2 | hits.pop.piano | SAMPLE | `HITS/THMN_Instruments_Dance_Piano_1_Csmin_142.wav` | F#2 | 643 | 0.09 | 0.26 | — | -15.1 | -8.9 | 6 | SLICE MODE: a real 90s dance-piano phrase chopped at onsets; each slice is a chord hit, repitched +1 st (C#m → Dm); SHUFFLE picks slices |
| H3 | hits.long.hoover | LAYER | `HITS/THMN_Instruments_Rave_Stabber_Dmin_142.wav` | D4 | 1552 | 0.00 | 0.91 | 8321 | -15.1 | -8.9 | 6 | rave stab phrase already in D minor, sliced; synth PWM hoover layer keeps the −7 st dive and 150 ms glide; reese bass shot (+2 oct) as an alternative body |
| P1 | pluck.ukmin.hollow | SAMPLE | `PLUCK/AR_Synth_15_C.wav` | C5 | 1635 | 0.00 | 0.90 | 146 | -16.1 | -9.6 | 6 | measured −20 dB in 146 ms, centroid 1.6 kHz (inside the 93–150 ms / 1.2–2.0 kHz gate); 3/16 ping-pong; round-robin over the pool |
| P2 | pluck.pop.tick | SAMPLE | `PLUCK/VER_Stab_06_C.wav` | C6 | 1955 | 0.00 | 0.28 | 95 | -13.1 | -10.9 | 6 | click pluck (−20 dB @ 95 ms); filter env τ 12 ms on top gives the 1.8-oct centroid fall; staccato gate |
| P3 | pluck.long.bloom | LAYER | `PLUCK/VER_Synth_Hit_Flutter_C.wav` | C4 | 1417 | 0.00 | 0.29 | 389 | -21.3 | -4.2¹ | 6 | delayed filter bloom (env peak 10–50 ms) applied to the sampled flutter shot; 1/4 delay + 2.4 s hall |
| A1 | pad.ukmin.susswell | LAYER | `PAD/JF_128_Fm_Phased_Funky_Pad_1_Pad.wav` | F2 | 1158 | 0.05 | 0.80 | — | -13.8 | -10.2 | 6 | GRANULAR FREEZE on the first chord of the pad loop (window below), pitched to key; synth saw ×3 + sine adds the A 150 / R 700 shape; vinyl texture bed |
| A2 | pad.long.wideswell | SAMPLE | `PAD/THMN_Instruments_Pad_5th_Cmin_142.wav` | C4 | 2359 | 0.03 | 0.08 | — | -26.0 | +2.0 | 6 | 5th pad (no 3rd) stretched by granular scan (0.25× speed) for the 3 s-plus decay; corr 0.08 sample is pulled to ≤ 0.5 by M/S width; reversed chord tail as the swell |
| A3 | pad.gaskin.swellpad | LAYER | `PAD/TAT_128_Pad_11_Fm.wav` | D#1 | 1056 | 0.20 | 0.52 | — | -16.8 | -7.2 | 6 | sustained ♭VI/♭VII: granular hold of the first chord, re-voiced as B♭/C by the generator's chord notes (one repitched grain voice per note; chord memory is STAB-only, E10); saw + pulse synth layer |
| V1 | vox.ukmin.hey | SAMPLE | `VOX/UHV_125_YEAH.wav` | B0 | 2676 | 0.00 | 1.00 | 212 | -12.0 | -15.7 | 6 | spoken shot through the 3-band FORMANT filter set to "hey" (/ɛ/→/ɪ/ over 120 ms) and LPF 8 kHz; dry centre, dub throw |
| V2 | vox.long.oh | SAMPLE | `VOX/THMN_Female_Vox_Lover_Amin_145_Lead_DRY.wav` | A3 | 1430 | 0.00 | 1.00 | — | -14.2 | -9.9 | 6 | SLICE MODE: sung female hook (A minor = v of D minor) chopped at onsets into ~4/16 vowel slices; formant filter oh→ah morph at alto size; vibrato LFO; long reverb |
| V3 | vox.pop.ahuh | SAMPLE | `VOX/UHV_125_Chiki_Ah_2.wav` | B0 | 2405 | 0.00 | 1.00 | 340 | -13.5 | -12.4 | 6 | staccato ah/uh barks, ROUND-ROBIN across the pool + sample-start jitter; gated 1–2/16 |

**Full blocks** (Maestro's format; proposed extensions after `//`; v1.3 positions address the MIX_SPEC-prepped files). `presets_samples.json` has the same data, machine-readable:
```json
"bass.ukmin.pluck": {"sample": {"mode": "LAYER", "file": "BASS/THMAG_Bass_One_Shot_4_Fm.wav", "root": "F1", "start": 0.0006, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.35, "pool": ["BASS/THMAG_Bass_One_Shot_8_Fm.wav", "BASS/TAT_Bass_11_Ds.wav", "BASS/THMAG_Bass_One_Shot_6_Fm.wav", "BASS/TAT_Bass_09_Ds.wav", "BASS/VER_MoodyOrgan_Bass_C.wav", "BASS/SD_Bass_20_C.wav"]}}
"bass.pop.click": {"sample": {"mode": "LAYER", "file": "BASS/DCB_Bass_01_C.wav", "root": "C#1", "start": 0.0005, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["BASS/THMAG_Bass_One_Shot_3_Fm.wav", "BASS/DCB_Bass_09_C.wav", "BASS/THMN_Bass_Shot_12_Fmin.wav", "BASS/TAT_Bass_05_As.wav", "BASS/AR_Bass_07_C.wav", "BASS/MPH_Bass_03_C.wav"]}}
"bass.long.round": {"sample": {"mode": "LAYER", "file": "BASS/THMAG_Bass_One_Shot_1_Fm.wav", "root": "F1", "start": 0.006, "reverse": false, "loop": {"on": true, "start": 23097, "end": 45367, "xfade": 4410}, "layerMix": 0.6, "pool": ["BASS/VER_CleanSub_Bass_C.wav", "BASS/THMAG_Bass_One_Shot_6_Fm.wav", "BASS/THMN_Bass_Shot_14_Fmin.wav", "BASS/TAT_Bass_16_F.wav", "BASS/TAT_Bass_15_F.wav", "BASS/JF_Bass_15_C.wav"]}}
"stab.ukmin.dub": {"sample": {"mode": "SAMPLE", "file": "STAB/AR_Synth_06_C.wav", "root": "C4", "start": 0.0005, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["STAB/MA_C_Powerchord_Synth.wav", "STAB/THMN_Stab_1_Cmin.wav", "STAB/THMN_Stab_3_Cmin.wav", "STAB/THMN_Stab_2_Cmin.wav", "STAB/JF_Synth_12_C.wav", "STAB/THMAG_Synth_One_Shot_29_Fm.wav"]}}
"stab.long.swell": {"sample": {"mode": "LAYER", "file": "STAB/JF_Synth_12_C.wav", "root": "C3", "start": 0.0, "reverse": false, "loop": {"on": true, "start": 22638, "end": 55460, "xfade": 8205}, "layerMix": 0.6, "pool": ["STAB/THMN_Stab_8_Cmin.wav", "STAB/THMN_Stab_1_Cmin.wav", "STAB/CD_Synth_Hit_09_C.wav", "STAB/THMN_Stab_4_Cmin.wav", "STAB/AR_Synth_06_C.wav", "STAB/SD_Synth_05_C.wav"], "_loop_refound_corr": 0.825}}
"stab.pop.click": {"sample": {"mode": "SAMPLE", "file": "STAB/VER_Stab_04_C.wav", "root": "E5", "start": 0.0005, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["STAB/VER_Stab_02_C.wav", "STAB/VER_Synth_Hit_Buzz_C.wav", "STAB/JF_Synth_09_C.wav", "STAB/CD_Synth_Hit_17_Cm.wav", "STAB/SD_Synth_04_C.wav", "STAB/TAT_SFX_05.wav"]}}
"hits.gaskin.organ": {"sample": {"mode": "LAYER", "file": "HITS/VER_DX7Organ_Bass_C.wav", "root": "C2", "start": 0.0006, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.6, "pool": ["HITS/THMN_Chord_Stab_8_Cmin.wav", "HITS/THMN_Chord_Stab_7_Cmin.wav", "HITS/MPH_Synth_14_C.wav", "HITS/THMAG_Synth_One_Shot_23_Fm.wav", "HITS/AR_Synth_03_C.wav", "HITS/SD_Synth_14_C.wav"]}}
"hits.pop.piano": {"sample": {"mode": "SAMPLE", "file": "HITS/THMN_Instruments_Dance_Piano_1_Csmin_142.wav", "root": "F#2", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["HITS/JC_Keys_MPC_123_A_Minor.wav", "HITS/VER_Synth_Hit_Tines_C.wav", "HITS/TAT_SFX_20.wav", "HITS/SD_Synth_14_C.wav", "HITS/TAT_SFX_01.wav", "HITS/THMAG_Synth_One_Shot_23_Fm.wav"]}}
//   extensions: {"slices_s": [0.0232, 0.418, 0.8475, 1.0565, 1.2655, 1.4512, 2.1014, 2.3104, 2.9489, 3.1695, 3.3785, 3.8081, 4.2144, 4.4466, 4.6324, 4.8646]}
"hits.long.hoover": {"sample": {"mode": "LAYER", "file": "HITS/THMN_Instruments_Rave_Stabber_Dmin_142.wav", "root": "D4", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["HITS/VER_Synth_Hit_Rave_C.wav", "HITS/VER_HyperReese_Bass_C.wav", "HITS/TAT_SFX_01.wav", "HITS/TAT_SFX_20.wav", "HITS/TAT_SFX_07.wav", "HITS/THMN_Chord_Stab_7_Cmin.wav"]}}
//   extensions: {"slices_s": [0.0232, 0.2206, 0.4296, 0.6269, 0.8243, 1.0449, 1.2771, 1.4745, 1.6834, 1.8924, 2.1014, 2.322, 2.531, 2.74, 2.9605, 3.1579]}
"pluck.ukmin.hollow": {"sample": {"mode": "SAMPLE", "file": "PLUCK/AR_Synth_15_C.wav", "root": "C5", "start": 0.0014, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["PLUCK/CD_Synth_Hit_20_C.wav", "PLUCK/VER_Stab_06_C.wav", "PLUCK/VER_Synth_Hit_Defuse_C.wav", "PLUCK/AR_Synth_31_C.wav", "PLUCK/AR_Synth_21_C.wav", "PLUCK/CD_Synth_Hit_15_C.wav"]}}
"pluck.pop.tick": {"sample": {"mode": "SAMPLE", "file": "PLUCK/VER_Stab_06_C.wav", "root": "C6", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["PLUCK/VER_Synth_Hit_Formant_C.wav", "PLUCK/CD_Synth_Hit_15_C.wav", "PLUCK/CD_Synth_Hit_20_C.wav", "PLUCK/AR_Synth_15_C.wav", "PLUCK/VER_Synth_Hit_Defuse_C.wav", "PLUCK/AR_Synth_21_C.wav"]}}
"pluck.long.bloom": {"sample": {"mode": "LAYER", "file": "PLUCK/VER_Synth_Hit_Flutter_C.wav", "root": "C4", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.6, "pool": ["PLUCK/TAT_SFX_06.wav", "PLUCK/AR_Synth_01_C.wav", "PLUCK/AR_Synth_22_C.wav", "PLUCK/SD_Synth_01_C.wav", "PLUCK/SD_Synth_06_C.wav", "PLUCK/DCB_Synth_06_C.wav"]}}
"pad.ukmin.susswell": {"sample": {"mode": "LAYER", "file": "PAD/JF_128_Fm_Phased_Funky_Pad_1_Pad.wav", "root": "F2", "start": 0.0, "reverse": false, "loop": {"on": true, "start": 11989, "end": 70561, "xfade": 11989}, "layerMix": 0.6, "pool": ["PAD/TAT_128_Pad_11_Fm.wav", "PAD/JF_128_Gsm_Strung_Up_Pad.wav", "PAD/JF_128_Robot_Pad_Background.wav", "PAD/THMAG_Background_Noise_1_Cmin_132.wav", "PAD/THMN_Instruments_Pad_5th_Cmin_142.wav", "PAD/DCB_132_Neb_Background.wav"]}}
//   extensions: {"granular": {"window_start": 11989, "window_end": 70561, "grain_ms": 80, "density": 16, "scan_speed": 0.0, "spray_ms": 20}, "sampleLayers": [{"role": "atmos bed (noise, loop, −18 dB)", "mode": "SAMPLE", "file": "PAD/CD_SFX_Vinyl_Texture.wav", "root": "A#3", "start": 0.0006, "reverse": false, "loop": {"on": true, "start": 11022, "end": 72893, "xfade": 11022}, "layerMix": 1.0, "pool": [], "loop_corr": 0.286, "granular": {"window_start": 11022, "window_end": 72893, "grain_ms": 80, "density": 16, "scan_speed": 0.0, "spray_ms": 20}}]}
"pad.long.wideswell": {"sample": {"mode": "SAMPLE", "file": "PAD/THMN_Instruments_Pad_5th_Cmin_142.wav", "root": "C4", "start": 0.0157, "reverse": false, "loop": {"on": true, "start": 9757, "end": 62983, "xfade": 9757}, "layerMix": 0.5, "pool": ["PAD/JF_128_Robot_Pad_Background.wav", "PAD/JF_128_In_The_City_Background.wav", "PAD/THMAG_Background_Noise_4_Cmin_132.wav", "PAD/JF_128_Swampy_Background.wav", "PAD/DCB_131_Frost_Background.wav", "PAD/DCB_132_Gm_Echo_Pad.wav"]}}
//   extensions: {"granular": {"window_start": 9757, "window_end": 62983, "grain_ms": 80, "density": 16, "scan_speed": 0.25, "spray_ms": 20}, "sampleLayers": [{"role": "REVERSE SWELL into the bar (reversed chord tail)", "mode": "SAMPLE", "file": "PAD/VER_134_EndOfBarChord_Asm_FX.wav", "root": "A#3", "start": 0.0353, "reverse": true, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": []}]}
"pad.gaskin.swellpad": {"sample": {"mode": "LAYER", "file": "PAD/TAT_128_Pad_11_Fm.wav", "root": "D#1", "start": 0.0, "reverse": false, "loop": {"on": true, "start": 11043, "end": 63436, "xfade": 11043}, "layerMix": 0.6, "pool": ["PAD/JF_128_Fm_Hey_Soundscape_Pads.wav", "PAD/JF_128_Fm_Phased_Funky_Pad_1_Pad.wav", "PAD/THMN_Instruments_Pad_5th_Cmin_142.wav", "PAD/JF_128_Gsm_Strung_Up_Pad.wav", "PAD/THMAG_Background_Noise_4_Cmin_132.wav", "PAD/DCB_132_Neb_Background.wav"]}}
//   extensions: {"granular": {"window_start": 11043, "window_end": 63436, "grain_ms": 80, "density": 16, "scan_speed": 0.0, "spray_ms": 20}}
"vox.ukmin.hey": {"sample": {"mode": "SAMPLE", "file": "VOX/UHV_125_YEAH.wav", "root": "B0", "start": 0.0008, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["VOX/THMN_Rap_One_Shot_UH_DRY.wav", "VOX/UHV_125_UH.wav", "VOX/CD_130_Vox_Shot_03.wav", "VOX/UHV_125_Chiki_Ah_1.wav", "VOX/UHV_125_Chiki_Ah_2.wav", "VOX/CD_130_Vox_Shot_02.wav"]}}
"vox.long.oh": {"sample": {"mode": "SAMPLE", "file": "VOX/THMN_Female_Vox_Lover_Amin_145_Lead_DRY.wav", "root": "A3", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["VOX/VER_134_VoxBuild_E_FX.wav", "VOX/MTV_128_Voc_FX_03.wav", "VOX/MTV_128_Voc_FX_05.wav", "VOX/THMAG_Vocal_Effect_Into_Your_Mind_132.wav", "VOX/MTV_128_Voc_FX_02.wav", "VOX/THMAG_Vocal_Effect_The_Rhythm_132.wav"]}}
//   extensions: {"slices_s": [0.0, 0.2892, 0.6375, 0.8465, 1.0323, 1.2529, 1.4386, 1.6708, 1.8566, 1.9611, 2.0423, 2.3094, 2.4719, 2.6112, 2.8202, 2.8666]}
"vox.pop.ahuh": {"sample": {"mode": "SAMPLE", "file": "VOX/UHV_125_Chiki_Ah_2.wav", "root": "B0", "start": 0.0005, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 0.5, "pool": ["VOX/UHV_125_UH.wav", "VOX/THMN_Rap_One_Shot_UH_DRY.wav", "VOX/CD_130_Vox_Shot_04.wav", "VOX/UHV_125_Chiki_Ah_1.wav", "VOX/UHV_125_YEAH.wav", "VOX/CD_130_Vox_Shot_02.wav"]}}
//   extensions: {"sampleLayers": [{"role": "slice source: glitch \"ta-ta-uh\" chopped for SHUFFLE", "mode": "SAMPLE", "file": "VOX/UHV_125_Ta_Ta_Uh.wav", "root": "C3", "start": 0.0123, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": [], "slices_s": [0.0, 0.0649, 0.3319, 0.5873, 0.8079, 1.1794, 1.5161, 1.9805, 2.2475, 2.503, 2.7352, 3.0718, 3.4318]}]}
```


**Dialect coverage (own presets):**

| Dialect | Own presets |
|---|---|
| UK MIN | B1, S1, P1, A1, V1, K1 |
| LONG | B3, S2, H3, P3, A2, V2 |
| POP | B2, S3, H2, P2, V3 |
| GASKIN 90s | H1, A3, K2 |

**Why ROUND over ORGAN for BASS's third slot** [D / R]:
1. **Fit to the brief.** LONG is a UK-minimal dialect, and Melak's brief is UK minimal, "not 90s-house". GASKIN 90s is a single-track hypothesis (CTR D, "kept separate").
2. **Coverage.** With ROUND, three of the four dialects get their own bass hero. GASKIN 90s is the only one that borrows, and ROUND is its nearest tone (§1.2).
3. **Engine.** ORGAN needs 3 sine "drawbars" plus tube drive and nothing ROUND lacks, so it can come back post-v0 as a preset only, with no new DSP.

ORGAN moves to §10 with its numbers intact.

---

## 3. BASS (mono legato voice, DS §14.1)

Common to all three v0 BASS presets (PLUCK, CLICK, ROUND):
- Output mono, HPF 25 Hz or higher on the output.
- Velocity curve (v/112)^2.06: vel 112 = 0 dB, 80 = −6 dB [L, PT `vel_gain`].
- No delay or reverb send (FX = 0) [R; SB §1: "none on the dry bass"].
- **Mix gates:**
  - ≥ 91% of energy < 200 Hz [M REF_R5 91.8%, SR 95.3%];
  - corr ≥ 0.997 [M];
  - > 1 kHz ≤ 0.5% [M 0.1%].

### B1 · BASS · UK MIN · PLUCK (hero)
| Block | Target |
|---|---|
| **Sample** (v1.3) | **LAYER**, `BASS/THMAG_Bass_One_Shot_4_Fm.wav`, root **F1**, start 0.0028 s, no loop, layerMix 0.35; pool of 6 (§2.1) [M-bank] |
| Osc 1 (MID) | PolyBLEP **saw**, oct 0, 0 ct, level ref 0 dB [L SR §1] |
| Osc 2 | off |
| Sub | **sine**, oct 0, **−12 dB re MID osc** [L: render gains MID +11 / SUB −1 on unit oscillators, `patches_v1.py`]. Own **LP24 120 Hz**, never env-filtered [L] |
| Unison | 1 voice, 0 ct [R from M: no beating] |
| Filter (MID) | **Ladder LP24, 170 Hz** [M settle 163–171], res **0–5%** [R from M: no peak], key track 0–20% [R] |
| Filter env | **+24 st** (peak about 680 Hz) [D from M 660→170 Hz]. **Exponential τ 25 ms**, settled by about 60 ms [L]. ADSR equivalent A 0 / D 60 / S 0 / R 40 ms [D] |
| MID HPF | 80 Hz 12 dB [L] |
| Amp env | A **3** / D **250** / S **−8 dB** / R **40** ms [R from M; note median 245 ms] |
| Voice / glide | mono legato, **35 ms** constant-time, overlaps only [R from M 30–40 ms] |
| Drive | **0 dB** [M: no distortion signature]. TONE's drive range for this preset is capped at **+3 dB** (SR: "200 Hz settle or 2–3 dB drive, not both") [R] |
| EQ | HPF 25 Hz 24 dB, LPF 1 kHz 12 dB [R from M 0.1% > 1 kHz] |
| Delay / reverb | none |
| Width | mono, corr ≥ 0.997 [M 0.997–0.998] |
| DUCK | **13 dB**, recovery **115 ms** [M 13.4 dB / 115 ms] |
| Level | **−24.7 LUFS-S ±0.5** (BASS, every dialect), TP ≤ −8.9 dBTP [MIX §2.2]. v1.2's −18 retired (6.7 dB too hot) |
| Key / vel | vel → amp 100%, → env amount 20% [R] |
| Must measure | <60 Hz **≈ 54%**, <200 Hz **91–95%**, 200–400 Hz ≤ 5.1% [M / L] |

*Cutoff note:* the 170 Hz rest may need about +10–20 Hz on the ladder. This is **an item to measure after render** (§11.1 item 1), not a change.

### B2 · BASS · POP · CLICK (hero, DS §14.2)
| Block | Target |
|---|---|
| **Sample** (v1.3) | **LAYER**, `BASS/DCB_Bass_01_C.wav`, root **C#1**, start 0.0005 s, no loop, layerMix 0.5; pool of 6 (§2.1) [M-bank] |
| Osc 1 | PolyBLEP **saw**, oct 0, 0 ct, 0 dB [R DS] |
| Sub | sine oct 0, **−9 dB**, LP24 120 Hz [R from M: REF_B1 has only 14% of energy < 60 Hz and 70% at 60–100 Hz, so h2 carries the note] |
| Unison | 1 |
| Filter | Ladder LP24 **rest 350 Hz** [M DS mockup value], res 10% [R] |
| Filter env | **+24 st**, exponential **τ 17 ms** [R DS]. Fits REF_B1's above-150 Hz centroid 1374 Hz (0–10 ms) → 352 Hz (25–50 ms), −2 oct in about 25 ms [M] |
| Amp env | A 1 / **D 120** / S 0 / R 20 ms. Note ≈ **1.1/16 = 127 ms** [M REF_B1 1.12/16, sounding 34%] |
| Voice / glide | mono, **glide 0** [R]. Octave pops about 20% from the generator [M CTR] |
| Drive | soft clip 1 dB [R] |
| EQ | HPF 30 Hz, LPF 1.5 kHz [R from M 0.6% > 400 Hz] |
| Width | mono, corr ≥ 0.997 [M REF_B1 0.997] |
| DUCK | **gate shape** (0.1 ms to depth, hold 40 ms, recover 60 ms), depth **20 dB** [R from DS "gated at kick"]. Only 2% of REF_B1 bass attacks land on the kick [M], so the gate mostly trims tails |
| Level | **−24.7 LUFS-S ±0.5**, TP ≤ −8.9 dBTP [MIX §2.2]; MIX_SPEC CLICK override: comp release 100 ms |
| Key / vel | vel → amp, → env amount 30% [R] |
| Must measure | < 200 Hz **≥ 95%** [M REF_B1 97.5%], 60–100 Hz is the largest band [M 70%], > 400 Hz ≤ 1% [M 0.6%], centroid falls ≥ 1.8 oct within 50 ms [D] |

### B3 · BASS · LONG · ROUND (hero; also the GASKIN 90s default, with dialect overrides DUCK 7 dB / 110 ms and glide 0)
| Block | Target |
|---|---|
| **Sample** (v1.3) | **LAYER**, `BASS/THMAG_Bass_One_Shot_1_Fm.wav`, root **F1**, start 0.0073 s, loop 23154–45424 fr, xfade 4410, layerMix 0.6; pool of 6 (§2.1) [M-bank] |
| Osc 1 | **triangle**, oct 0, 0 dB [R from M h 0/−11/−15/−33 dB: near sine plus h2/h3]. Alternative: sine plus saw at −18 dB |
| Sub | sine oct 0, **−6 dB**, LP24 120 Hz [R] |
| Unison | 1 [R] |
| Filter | Ladder LP24 **110 Hz** [D from M settle 108 Hz], res 0 |
| Filter env | **+26 st** (≈ +2.2 oct, about 500 Hz) [D from M 495→108 Hz]. **τ 47 ms** [M], about 90% closed by 99 ms [M]. ADSR equivalent A 0 / D 100 / S 0 [R] |
| Amp env | A **10** / D **400** / S **−3 dB** / R **80** ms [R from M: 30 ms rise includes sidechain] |
| Voice / glide | mono legato, **50 ms** [R from D: stepwise moves] |
| Drive | soft clip **1.5 dB** [R, lifts h2 −11 dB for small speakers] |
| EQ | HPF 28 Hz, LPF 600 Hz [R from M 2.2% > 400 Hz] |
| Width | mono, corr 1.000 [M] |
| DUCK | **18 dB / 180 ms** [R from M 28 dB / 200 ms, demucs-deepened; DS hero] |
| Level | **−24.7 LUFS-S ±0.5** in LONG and as the GASKIN 90s borrow, TP ≤ −8.9 dBTP [MIX §2.2] |
| Must measure | <60 Hz **≈ 68%**, >400 Hz ≤ 2.2% [M REF_D2] |

---

## 4. STAB (poly, DS §14.1)

Common to all three STAB presets:
- Poly 4 notes (6 voices allowed for tails).
- **Chord memory (v1.3b, Maestro; E10).** Each STAB preset stores a `chordMemory` block with a **main** and a **lift** set. The generator (or the player) sends **one key per hit**, and chord memory expands it to the stored voicing. The reference voicings are `sketch_v1/PATCHES.md` §2: **QA (main) A3–D4–G4–C5** (57-62-67-72) and **QB (bar-8 lift) G3–C4–F4–B♭4** (55-60-65-70), the same stacked 4ths a tone lower.
  - **Mode PARALLEL:** intervals in semitones above the played key, no diatonic re-spelling. Played at its `refKey`, a set gives exactly its stored notes.
  - **Lift select:** the generator's bar-8 lift flag selects the **lift** set and plays its `refKey`. When the lift intervals equal the main ones (S1, S2), playing the lift ref key by hand gives the same result.
  - **Range fold:** any chord note above C6 (84) drops an octave, and any below the preset's HPF corner + 1 oct rises one. No v0 voicing triggers either; they guard KEY / transpose moves.
  - **Strum 0 ms** (all notes on the same sample). Velocity is per chord, not per note. Voices = set size; the poly-6 tail allowance covers overlaps.
  - **CHORD off** = plays the keys as received (the generator then sends the full voicing, as in v1.2).
  - Format: `"chordMemory": {"on": true, "mode": "PARALLEL", "main": {"refKey": "A3", "intervals": [0,5,10,15]}, "lift": {"refKey": "G3", "intervals": [0,5,10,15]}, "liftSelect": "GENERATOR_BAR8", "foldAbove": "C6", "strum_ms": 0}`.

| Preset | main: refKey + intervals → notes | lift: refKey + intervals → notes | Relation to QA / QB |
|---|---|---|---|
| S1 DUB STAB | **A3** + [0, 5, 10, 15] → **A3–D4–G4–C5** | **G3** + [0, 5, 10, 15] → **G3–C4–F4–B♭4** | = QA / QB exactly [L PT] |
| S2 SWELL STAB | **A4** + [0, 7] → **A4–E5** | **G4** + [0, 7] → **G4–D5** | 5+2 colour on QA's top-register A, lift a tone lower like QB [M CTR C] |
| S3 CLICK STAB | **F4** + [0, 7] → **F4–C5** | **G4** + [0, 5] → **G4–C5** | ♭3+♭7 dyad (F4 from QB, C5 from QA); the lift G4–C5 is QA's top two notes [mockup `dyad`] |
- Mono below 200 Hz.
- No stab energy below 160 Hz [M stem band 160–2080 Hz].

### S1 · STAB · UK MIN · DUB STAB
| Block | Target |
|---|---|
| **Sample** (v1.3) | **SAMPLE**, `STAB/AR_Synth_06_C.wav`, root **C4**, start 0.0016 s, no loop, layerMix 0.5; pool of 6 (§2.1) [M-bank] |
| Voicing / chord memory | **QUARTAL A3–D4–G4–C5** (220 / 293.66 / 392 / 523.25 Hz), lift G3–C4–F4–B♭4. **No 3rd** [L, DS]. `chordMemory` main A3 [0,5,10,15], lift G3 [0,5,10,15], PARALLEL, lift by generator bar 8 (§4) |
| Osc | **saw 0 dB + square −6 dB**, oct 0 [L] |
| Sub | off |
| Unison | **2 voices ±5 ct**, spread 40% [L] |
| Filter | SVF **LP12 2.2 kHz**, res 15% [L / R] |
| Filter env | **+18 st** (+1.5 oct), A 0 / **D 90** / S 0 ms [L] |
| Amp env | A **1** / D **120** / S 0 / R **50** ms [L] |
| Drive | 0 |
| EQ | **HPF 250 Hz** 12 dB [L] |
| Delay | **3/16 = 346.2 ms** ping-pong, fb **40%**, HPF 300 / LPF 2.5 k in the loop, send **−8 dB** [L]. Off on the bar-8 slot-16 hit [L] |
| Reverb | **plate 1.2 s**, pre-delay 1/64 = 28.8 ms [R], return **−14 dB**, HPF 250 [L] |
| Width | target **corr 0.55** (0.48–0.63) [M REF_R5]. If the harness reads > 0.65, go to 4 voices ±15 ct, spread 80% (SB swell stab) [R] |
| DUCK | **3 dB / 115 ms** [L] |
| Level | **−26.8 LUFS-S ±1** (UK MIN STAB), TP ≤ −7.1 dBTP [MIX §2.2]; −29.8 when PLUCK also plays |
| Key / vel | vel → amp (curve 2.06), vel 127 → +0.5 oct cutoff [R] |
| Must measure | centroid **≈ 1.5 kHz** (1.0–2.3 k), tail −10 dB at **850–900 ms**, −20 dB at 920–1400 ms incl. returns [M] |

### S2 · STAB · LONG · SWELL STAB
| Block | Target |
|---|---|
| **Sample** (v1.3) | **LAYER**, `STAB/JF_Synth_12_C.wav`, root **C3**, start 0.0 s, loop 62994–151805 fr, xfade 4800, layerMix 0.6; pool of 6 (§2.1) [M-bank] |
| Voicing / chord memory | **5+2: A4 + E5** (440 / 659.26 Hz), lift G4 + D5 [M CTR C: REF_D2 5+2 on slots 14–15; mockup `fivetwo`]. `chordMemory` main A4 [0,7], lift G4 [0,7], PARALLEL (§4) |
| Osc | 2 saws **±10 ct** (or saw + square −6 dB), oct 0 [R SB §4] |
| Unison | **4 voices, 15 ct**, spread 80% [R from M corr target] |
| Filter | SVF LP12 **1.4 kHz**, res 15% [R from M centroid] |
| Filter env | +12 st, A 0 / D 500 / S 0 [R SB] |
| Amp env | **A 90** / D 400 / S 0 / R 150 ms. The generator plays it **1/32 (57.7 ms) early** [R from M onset 25–45 ms before the barline, onset→peak 75–160 ms]. Dry decay to −15 dB is about 2.5–3/16 [D, matches REF_D2 2.3/16 [M sum]] |
| EQ | HPF 200 Hz [R from M 160 Hz band edge] |
| Delay | 3/16 = 346.2 ms ping-pong, fb 40%, HPF 300 / LPF 2.5 k, send −10 dB [R SB] |
| Reverb | plate **1.8 s**, pre-delay **28.8 ms**, HPF 250, return −10 dB [R SB] |
| Width | corr **0.50** (0.48–0.63) [M] |
| DUCK | 3 dB / **157 ms** (LONG recovery) [R from DS] |
| Level | **−32.6 LUFS-S ±1** (LONG STAB = kick −8.9), TP ≤ −7.1 dBTP [MIX §2.3: REF_D2 "other" −8.6 re kick + LONG TOP trim −0.3] |
| Must measure | centroid 1.0–1.6 kHz, corr ≤ 0.63 [M / R] |

### S3 · STAB · POP · CLICK STAB
| Block | Target |
|---|---|
| **Sample** (v1.3) | **SAMPLE**, `STAB/VER_Stab_04_C.wav`, root **E5**, start 0.0019 s, no loop, layerMix 0.5; pool of 6 (§2.1) [M-bank] |
| Voicing / chord memory | **♭3+♭7 dyad: F4 + C5** (349.23 / 523.25 Hz), lift G4 + C5 [mockup `dyad`; CTR C REF_R3 dyad on the "a"]. `chordMemory` main F4 [0,7], lift G4 [0,5], PARALLEL (§4) |
| Osc | saw 0 dB + square −6 dB, **plus a noise click**: white, HPF 2 kHz, A 0 / D 15 ms, −12 dB [R] |
| Unison | 2 voices ±4 ct, spread 30% [R] |
| Filter | SVF LP12 **6 kHz**, res 20% [R from M centroid 3.2–5.5 kHz] |
| Filter env | +18 st, A 0 / D **40** / S 0 [R] |
| Amp env | A 0.5 / **D 100** / S 0 / R 30 ms. Reaches −20 dB in about 100–150 ms [D from M 93–150 ms] |
| EQ | **HPF 600 Hz** [R: keeps the centroid up] |
| Delay | **1/8 = 230.8 ms**, fb 20%, send −16 dB [R] |
| Reverb | plate 0.8 s, pre-delay 0, return −18 dB [R] |
| Width | corr 0.65 [R], mono below 300 Hz |
| DUCK | **gate shape**, 6 dB [R from DS POP "gated at kick"] |
| Level | **−26.0 LUFS-S ±1** (POP STAB = kick −2.3), TP ≤ −7.1 dBTP [MIX §2.3] |
| Must measure | centroid **3.2–5.5 kHz**, −20 dB in **93–150 ms** dry [M REF_R5 short hit] |

---

## 5. HITS: 90s rave sources (normal lane; default mute per §1)

**Reference data.** None of the 9 reference tracks contains a hoover, orchestral hit or piano stab. So the HITS synthesis is **[R] throughout**, and only the *mix targets* are measured.

**Measured targets every HIT must meet:**
- centroid 1–5 kHz;
- mono below 150 Hz;
- correlation 0.45–0.75 [M REF_R5 other 0.48–0.63 / swells 0.38–0.80].

**Hit behaviour (v1.3b, Maestro).** HITS are **one-shot 90s hits with no chord memory**: each key plays one hit at that note. Chord memory lives on STAB (§4, E10). The voicings below are the **notes the generator's HITS pattern sends** (or the player plays); they are not stored in the preset.

**Coverage.** v0 covers organ stab, piano chord and hoover. ORCH HIT and RAVE SAW are post-v0 (§10).

### H1 · HITS · GASKIN 90s · ORGAN HIT (90s house organ stab, M1-style), on by default in GASKIN 90s
| Block | Target |
|---|---|
| **Sample** (v1.3) | **LAYER**, `HITS/VER_DX7Organ_Bass_C.wav`, root **C2**, start 0.0007 s, no loop, layerMix 0.6; pool of 6 (§2.1) [M-bank] |
| Voicing (generator notes) | **♭VI/♭VII: B♭3–D4–F4**, lift **C4–E4–G4** [mockup `pad` voice; CTR D6 REF_J1/J2 ♭6+♭7 sets]. Direction-B style: E4–G4–B♭4 / E4–A4–C5 [L, PT] |
| Osc (3, re-voiced for E2) | **Osc 1 sine ×1 (8') 0 dB · Osc 2 sine +12 st (4') −3 dB · Osc 3 sine +19 st (2⅔', ×2.997) −9 dB** [R]. The even-harmonic lean follows the REF_J1/J2 organ bass (h2 −3 dB) [M SB §3]. The v1 2' sine and pulse are replaced by drive |
| Key click | noise (E4), BP 2.5 kHz Q 1, A 0 / D 6 ms, −18 dB [R] |
| Unison | 1 (organ: no detune) |
| Filter | SVF LP12 **5 kHz**, res 0, no env [R] |
| Amp env | A 1 / **D 350** / S −12 dB / R 120 ms [R] |
| Drive | **tube 3 dB**: puts back the ×4 / ×6 grit [R] |
| EQ | **HPF 250 Hz** [L PT organ lock], −2 dB bell at 500 Hz Q 1 [R] |
| Delay / reverb | 1/8 = 230.8 ms, fb 20%, send −16 dB · plate 1.2 s, pre-delay 28.8 ms, return −14 dB [R] |
| Width | ensemble 0.5 Hz, 25% → corr **0.70**; mono < 150 Hz [R] |
| DUCK | 3 dB / 110 ms [R from M REF_J1/J2 3.6–8 dB] |
| Level | **−27.7 LUFS-S ±1** (HITS = bass −3 LU, every dialect), TP ≤ −8.0 dBTP [MIX §2.2, A&R] |
| Key / vel | vel → amp only [R] |
| Must measure | centroid 1.0–2.0 kHz, < 1% of energy below 200 Hz [R] |

### H2 · HITS · POP · PIANO HIT (90s house piano chord, M1-style), muted by default in POP
| Block | Target |
|---|---|
| **Sample** (v1.3) | **SAMPLE**, `HITS/THMN_Instruments_Dance_Piano_1_Csmin_142.wav`, root **F#2**, start 0.0 s, no loop, layerMix 0.5; pool of 6 (§2.1) [M-bank] |
| Voicing (generator notes) | **rootless ♭3–5–♭7: F4–A4–C5**, optional +D5 [R from DS rule 20] |
| Osc | **2-op FM** (E3): carrier sine ×1, **modulator ratio 1:1**, index env **3.0 → 0.4**, D 400 ms. The v1 14:1 tine operator is replaced by the hammer noise [R] |
| Hammer | noise (E4), BP 3 kHz, A 0 / D 12 ms, −20 dB [R] |
| Unison | 2 voices ±3 ct, spread 40% [R] |
| Filter | SVF LP12 8 kHz, key track 50% [R] |
| Amp env | A 0 / **D 1200** / S 0 / R 250 ms [R] |
| EQ | HPF 200 Hz, +2 dB bell at 3 kHz Q 1 [R] |
| Delay / reverb | 1/8 = 230.8 ms, fb 25%, send −14 dB · plate 1.0 s, pre-delay 10 ms, return −16 dB [R] |
| Width | corr 0.65; mono < 150 Hz [R] |
| DUCK | gate shape, 4 dB [R] |
| Level | **−27.7 LUFS-S ±1** (HITS = bass −3 LU), TP ≤ −8.0 dBTP [MIX §2.2, A&R] |
| Key / vel | **vel → FM index ±50% and amp** [R] |
| Must measure | centroid 1.5–3 kHz, −20 dB by about 900 ms [R] |

### H3 · HITS · LONG · HOOVER (muted by default in LONG and UK MIN)
| Block | Target |
|---|---|
| **Sample** (v1.3) | **LAYER**, `HITS/THMN_Instruments_Rave_Stabber_Dmin_142.wav`, root **D4**, start 0.0 s, no loop, layerMix 0.5; pool of 6 (§2.1) [M-bank] |
| Voicing | mono lead, one note per hit [R]. (The v1.2 CHORD option 1–5–8 is removed: no chord memory on HITS) |
| Osc | **Osc 1 pulse PW 50%, PWM** (LFO 0.8 Hz, ±30%), 0 dB · **Osc 2 saw −1 oct**, −3 dB [R] |
| Unison | **5 voices (cap), 30 ct**, spread 100% (v1 had 7) [R] |
| Width make-up | **ensemble 3-tap, rate 0.6 Hz, depth 40%, mix 60%** (v1: 30 / 50) + M/S width 120% above 150 Hz, replacing the 2 lost unison voices [R] |
| Pitch env | **−7 st → 0**, exponential, τ 70 ms (lands by about 200 ms). Mod wheel 0–200% [R] |
| Filter / env | SVF LP12 **3.5 kHz**, res 10%, key track 50% · env +12 st, A 30 / D 400 / S 50% / R 300 [R] |
| Amp env | A 20 / S 0 dB / R 300 ms [R] |
| Voice / glide | mono legato, **150 ms** [R] |
| Drive / EQ | soft clip 3 dB · **HPF 150 Hz 24 dB**, −3 dB bell at 400 Hz [R] |
| Delay / reverb | 1/4 = 461.5 ms, fb 25%, send −16 dB · hall 2.0 s, pre-delay 28.8 ms, return −14 dB [R] |
| Width | corr **0.45**; mono < 150 Hz [R from M swells 0.38–0.80] |
| DUCK | 6 dB / 157 ms [R] |
| Level | **−27.7 LUFS-S ±1** (HITS = bass −3 LU; v1.2's −1 dB HOOVER offset dropped: level keys are locked), TP ≤ −8.0 dBTP [MIX §2.2] |
| Osc count | 1 voice × (5 + 5) = **10** |
| Must measure | corr ≤ 0.5 above 150 Hz and ≥ 0.99 below, centroid 1.2–2.5 kHz [R] |

---

## 6. PLUCK: hook voice

Common to all three PLUCK presets:
- Poly 4; register **D4–D5** (293.7–587.3 Hz) by default.
- HPF at or above 200 Hz, mono below 200 Hz.
- Hooks play off the kick: the generator rule is that ≥ 72% of attacks fall on the e / and / a [M CTR E1].

### P1 · PLUCK · UK MIN · HOLLOW
| Block | Target |
|---|---|
| **Sample** (v1.3) | **SAMPLE**, `PLUCK/AR_Synth_15_C.wav`, root **C5**, start 0.0014 s, no loop, layerMix 0.5; pool of 6 (§2.1) [M-bank] |
| Osc | **pulse 50% (square)** 0 dB + saw −12 dB, oct 0 [R: odd-harmonic "hollow" tone, distinct from the saw stab] |
| Unison | 1 [R] |
| Filter | SVF LP12 **2.5 kHz**, res 20% [R from M stab centroid 1.5 kHz] |
| Filter env | +24 st, exponential τ 30 ms (D 90 equivalent) [R from L stab env D 90] |
| Amp env | A 1 / **D 110** / S 0 / R 60 ms. Reaches −20 dB in about 120 ms [D from M short hits 93–150 ms] |
| EQ | HPF 250 Hz [R, matches the stab] |
| Delay | **3/16 = 346.2 ms ping-pong**, fb **35%**, HPF 300 / LPF 2.5 k, send −10 dB [R from L stab delay] |
| Reverb | plate 1.2 s, pre-delay 28.8 ms, return −16 dB [R] |
| Width | dry centre; returns bring the corr to **0.65–0.75** [R] |
| DUCK | 4 dB / 115 ms [R] |
| Level | **−26.8 LUFS-S ±1** (UK MIN PLUCK; −29.8 with STAB on), TP ≤ −7.1 dBTP [MIX §2.2] |
| Key / vel | vel → amp + cutoff (+0.5 oct at 127) [R] |
| Must measure | dry −20 dB in 93–150 ms, centroid 1.2–2.0 kHz [M / R] |

### P2 · PLUCK · POP · TICK
| Block | Target |
|---|---|
| **Sample** (v1.3) | **SAMPLE**, `PLUCK/VER_Stab_06_C.wav`, root **C6**, start 0.0 s, no loop, layerMix 0.5; pool of 6 (§2.1) [M-bank] |
| Osc | saw 0 dB + pulse 25% −6 dB [R] |
| Unison | 2 voices ±6 ct, spread 30% [R] |
| Filter | Ladder LP24 rest **900 Hz**, res 10% [R: above a D4–D5 fundamental] |
| Filter env | **+36 st**, exponential **τ 12 ms** [D from M REF_B1 above-150 Hz centroid 1374 → 1149 → 352 Hz at 0–10 / 10–25 / 25–50 ms, about 2 octaves in about 20 ms] |
| Amp env | A 0.5 / **D 140** / S 0 / R 40 ms. Note ≈ **1.1/16 = 127 ms** [M REF_B1 1.12/16] |
| Octave pops | about 20% (generator) [M CTR] |
| EQ | HPF 300 Hz [R] |
| Delay | 1/8 = 230.8 ms, fb 20%, send −16 dB [R] |
| Reverb | plate 0.8 s, return −20 dB [R] |
| Width | corr 0.75 [R] |
| DUCK | **gate shape, 6 dB** [R from DS POP] |
| Level | **−26.0 LUFS-S ±1** (POP PLUCK; −29.0 with STAB on), TP ≤ −7.1 dBTP [MIX §2.3] |
| Key / vel | vel → env amount ±12 st [R] |
| Must measure | centroid falls by ≥ 1.8 oct within 50 ms [D from M] |

### P3 · PLUCK · LONG · BLOOM
| Block | Target |
|---|---|
| **Sample** (v1.3) | **LAYER**, `PLUCK/VER_Synth_Hit_Flutter_C.wav`, root **C4**, start 0.0 s, no loop, layerMix 0.6; pool of 6 (§2.1) [M-bank] |
| Osc | triangle 0 dB + saw −12 dB + sine +12 st −15 dB [R, round like B2] |
| Unison | 3 voices, 8 ct, spread 50% [R] |
| Filter | Ladder LP24 **1.2 kHz**, res 10% [R] |
| Filter env | +18 st, **A 15** / D 250 / S 0 ms: the "delayed bloom" [D from M REF_D2 above-150 Hz centroid 220 → 312 → 283 → 236 Hz at 0–10 / 10–25 / 25–50 / 50–100 ms, peaking 10–50 ms after onset] |
| Amp env | A 5 / D 600 / S −18 dB / R 250 ms. Notes are 2–8/16 [M DS LONG] |
| EQ | HPF 200 Hz [R] |
| Delay | 1/4 = 461.5 ms, fb 30%, send −12 dB [R] |
| Reverb | hall **2.4 s**, pre-delay 28.8 ms, return −12 dB [R] |
| Width | corr 0.60 [R] |
| DUCK | 6 dB / **157 ms** [R from DS LONG recovery] |
| Level | **−32.6 LUFS-S ±1** (LONG PLUCK; −35.6 with STAB on), TP ≤ −7.1 dBTP [MIX §2.3] |
| Must measure | centroid peak at 10–50 ms, not at 0 ms [D from M] |

---

## 7. PAD / atmos swell (re-voiced in v1.1: worst case ≤ 40 oscillators)

Common to all three PAD presets:
- Mono below 150 Hz; HPF at or above 250 Hz.
- The generator plays swells **1/32 (57.7 ms) early**, with length and release set to end on the barline [L SR §4]:
  - 1 bar: 1503.9 ms, R 400;
  - 2 bars: 2992.3 ms, R 700;
  - half bar: 623.1 ms, R 300.
- The tail gate (§0.4) is mandatory.

### 7.1 Oscillator budget
**Voice limit = 8.** The largest voicing is a 4-note chord (QUARTAL). The next chord starts 57.7 ms before the barline, while the previous one is still releasing, so 4 + 4 voices can overlap. A 9th voice steals the oldest releasing voice with a 5 ms fade [R].

Count = voices × Σ (oscillators × unison), with noise counted as one source:

| Preset | Per voice | × 8 voices | v1 per voice | v1 total |
|---|---|---|---|---|
| A1 SUS SWELL | saw × 3 + sine × 1 = **4** | **32** | (2 saws + sine) × 6 | 6 notes × 18 = 108 |
| A2 WIDE SWELL | saw × 3 + pulse × 1 + noise × 1 = **5** | **40** ← worst case | (2 saws + pulse + noise) × 7 | 6 × 28 = 168 |
| A3 SWELL PAD | saw × 2 + pulse × 2 = **4** (3-note chords, so really 6 voices = 24) | **32** | (saw + pulse) × 4 | 6 × 8 = 48 |

**PAD worst case: 8 × 5 = 40 oscillators** ✔. Measured targets stay the same. The width that unison used to make now comes from **ensemble plus M/S width above 150 Hz**. The harness tunes the WIDTH amount until corr hits the target, and the centroid is set by the unchanged LP12 cutoffs [R].

### A1 · PAD · UK MIN · SUS SWELL
| Block | Target |
|---|---|
| **Sample** (v1.3) | **LAYER**, `PAD/JF_128_Fm_Phased_Funky_Pad_1_Pad.wav`, root **F2**, start 0.0 s, loop 11989–70561 fr, xfade 14643, layerMix 0.6; + layer `PAD/CD_SFX_Vinyl_Texture.wav` (atmos bed (noise, loop, −18 dB)); pool of 6 (§2.1) [M-bank] |
| Voicing | quartal A3–D4–G4–C5 (as S1) [L] |
| Osc | **saw × unison 3** (±10 ct, spread 100%) + **sine +12 st** −12 dB (unison 1) [R from SB §5] |
| Filter | SVF LP12 **1.8 kHz**, res 10% [R from M centroid 1.4–2.3 k]. LFO ±0.5 oct, sync 2 bars [R] |
| Amp env | **A 150** / D 0 / S 100% / **R 700** ms [R from M onset→peak 93–163 ms] |
| Width make-up | **ensemble 35% mix** (v1 chorus 20%) + **M/S width 130%** above 150 Hz [R] |
| EQ | **HPF 350 Hz** [R from M band 400–1790 Hz] |
| Reverb | **3.0 s**, pre-delay 28.8 ms, HPF 300, wet 35% [R SB]. No delay |
| Width target | corr **0.60** (0.4–0.8) [M breakdown 1 = 0.80; R middle of range] |
| DUCK | 3 dB / 115 ms [R] |
| Level | **−32.7 LUFS-S ±1.5** (UK MIN PAD = bass −8), TP ≤ −20.7 dBTP [MIX §2.2] |
| Key / vel | vel → amp 50%; mod wheel → cutoff ±0.5 oct [R] |
| Must measure | **centroid 1.4–2.3 kHz** [M], **corr 0.4–0.8** [M], −20 dB about 1.0–1.1 s after a 2-bar-spaced onset (short note) [M] |

### A2 · PAD · LONG · WIDE SWELL
| Block | Target |
|---|---|
| **Sample** (v1.3) | **SAMPLE**, `PAD/THMN_Instruments_Pad_5th_Cmin_142.wav`, root **C4**, start 0.0445 s, loop 11026–64252 fr, xfade 13306, layerMix 0.5; + layer `PAD/VER_134_EndOfBarChord_Asm_FX.wav` (REVERSE SWELL into the bar (reversed chord tail), reversed); pool of 6 (§2.1) [M-bank] |
| Voicing | 5+2 stack D4–A4–E5 (no 3rd) [R from mockup `fivetwo`] |
| Osc | **saw × unison 3** (±14 ct, spread 100%) + **pulse 30%** −8 dB + **pink noise** BP 3 kHz −30 dB (own env, E4) [R] |
| Filter / env | SVF LP12 **2.4 kHz**, res 5% [R from M centroid 2.4 k]. Env +12 st, **A 1846 ms (1 bar)**, S 100% [R] |
| Amp env | **A 600** / S 100% / **R 1500** ms [R] |
| Width make-up | **ensemble 50% mix** + **M/S width 160%** above 150 Hz (v1: unison 7) [R] |
| EQ | HPF 400 Hz [R] |
| Delay / reverb | dotted 1/4 = 692.3 ms, fb 35%, send −14 dB · **hall 4.0 s**, pre-delay 28.8 ms, wet 45% [R from M decay > 3 s] |
| Width target | corr **0.38** (0.3–0.5) [M REF_R5 breakdown 2] |
| DUCK | 2 dB / 157 ms [R] |
| Level | **−33.0 LUFS-S ±1.5** (LONG PAD), TP ≤ −20.7 dBTP [MIX §2.3] |
| Must measure | **decay > 3 s** incl. returns [M], **corr ≤ 0.5** [M], **centroid ≈ 2.4 kHz** (2.0–2.8) [M] |

### A3 · PAD · GASKIN 90s · SWELL PAD
| Block | Target |
|---|---|
| **Sample** (v1.3) | **LAYER**, `PAD/TAT_128_Pad_11_Fm.wav`, root **D#1**, start 0.0 s, loop 11043–63436 fr, xfade 13098, layerMix 0.6; pool of 6 (§2.1) [M-bank] |
| Voicing | **♭VI/♭VII: B♭3–D4–F4 → C4–E4–G4** [M CTR D6, mockup `pad`] |
| Osc | **saw × unison 2** (±12 ct) 0 dB + **pulse 40% × unison 2** (±8 ct) −4 dB [R] |
| Filter | SVF LP12 **1.8 kHz**, res 15% [R DS] |
| Amp env | **A 150** / S 100% / **R 700** ms [R DS]. Typical note **≈ 14/16 = 1615 ms** [M CTR D6: 14.4/16 to −15 dB]; onsets on the "e" / "and" [M] |
| Width make-up | ensemble 40% mix (v1 30%) + M/S width 110% [R] |
| EQ | HPF 250 Hz [R] |
| Reverb | **3.0 s**, pre-delay 28.8 ms, wet 30% [R DS]. No delay |
| Width target | corr 0.60 [R] |
| DUCK | 2 dB / 110 ms [R from M REF_J1/J2 light duck] |
| Level | **−31.7 LUFS-S ±1.5** (GASKIN 90s PAD), TP ≤ −20.7 dBTP [MIX §2.3] |
| Must measure | sustained, −15 dB at about 14/16 after onset [M]; centroid 1.2–2.0 kHz [R] |

---

## 8. VOX CUTS: synthesized formant vowels

**Source.** A glottal pulse (PolyBLEP saw, or pulse 12% for a thinner voice), plus aspiration noise, into a **3-formant filter**: parallel band-passes F1 / F2 / F3 with morph between vowels (engine req. E5).

**Formant table.** These are textbook adult averages from the standard vowel-formant measurements (Peterson & Barney 1952) [D]. ROLLA's **SIZE** control scales between them.

| Vowel | Male F1 / F2 / F3 (Hz) | Female F1 / F2 / F3 (Hz) | Bandwidths [R] |
|---|---|---|---|
| **ah** /ɑ/ | 730 / 1090 / 2440 | 850 / 1220 / 2810 | 80 / 100 / 120 |
| **oh** /ɔ/ → /ʊ/ | 570 / 840 / 2410 → 440 / 1020 / 2240 | 590 / 920 / 2710 → 470 / 1160 / 2680 | 70 / 90 / 120 |
| **uh** /ʌ/ | 640 / 1190 / 2390 | 760 / 1400 / 2780 | 80 / 100 / 130 |
| **hey** /h/ + /ɛ/ → /ɪ/ | noise 40 ms, then 530 / 1840 / 2480 → 390 / 1990 / 2550 over 120 ms | 610 / 2330 / 2990 → 430 / 2480 / 3070 | 60 / 100 / 150 |

**Key and velocity mapping (all VOX presets) [R]:**
- **Vowel keyswitches are post-v0** [Maestro, v1.2] (§10.4). In v0 the vowel comes from the preset's vowel or round-robin set, and the mod wheel morphs it.
- Playable range C3–C6.
- Mod wheel = vowel morph to the next vowel.
- Velocity → amp + F3 level (+6 dB at 127 = brighter).

**Measured mix targets for every VOX preset (REF_R5 vocal) [M]:**
- energy 5–95% within **242–3935 Hz**, HPF edge about 124 Hz;
- **≤ 1.2% above 6 kHz**;
- centroid about **2.35 kHz**;
- corr **0.79–0.85** in a drop, **about 0.40** in a breakdown;
- band split 150–400 / 400–1 k / 1–3 k / 3–6 k = 29 / 31 / 24 / 12%.

**Processing chain (SB §7, all VOX presets) [R from M]:**
- EQ: HPF 140 Hz 24 dB, LPF 8 kHz 12 dB, +2 dB bell at 2.5 kHz Q 1.
- Compressor 4:1, A 5 ms, R 60 ms, about 6 dB GR.
- Saturation 2 dB.

### V1 · VOX CUTS · UK MIN · HEY
| Block | Target |
|---|---|
| **Sample** (v1.3) | **SAMPLE**, `VOX/UHV_125_YEAH.wav`, root **B0**, start 0.0028 s, no loop, layerMix 0.5; pool of 6 (§2.1) [M-bank] |
| Default vowels | **hey**, alternating with **uh** (round-robin) [R] |
| Source | **pitch follow 0% (spoken)**: f0 fixed at 120 Hz with ±0.3% jitter, and a −2 st intonation fall over 150 ms [R from M "spoken, unpitched, same 16th positions"]. SIZE = male |
| Aspiration | noise −18 dB re voice (the "h" of hey: −6 dB for the first 40 ms) [R] |
| Amp env | A 1 / D 0 / S 100% / R **40** ms; length = note (typically 1–2/16) [R SB §7] |
| Delay | **3/16 = 346.2 ms ping-pong**, fb 35%, HPF 400 / LPF 4 k, send −14 dB. Throw on the phrase end [R SB §7] |
| Reverb | **3.0 s**, pre-delay 28.8 ms, HPF 300 / LPF 6 k, return **−12 dB** [R SB §7] |
| Width | dry centre; corr **0.80** with returns [M] |
| DUCK | dry 0 dB; returns 3 dB / 115 ms [R from M] |
| Level | **−28.7 LUFS-S ±1** (UK MIN VOX = kick −5.0), TP ≤ −3.2 dBTP [MIX §2.2] |
| Must measure | centroid 2.0–2.6 kHz, > 6 kHz ≤ 1.2% [M] |

### V2 · VOX CUTS · LONG · OH
| Block | Target |
|---|---|
| **Sample** (v1.3) | **SAMPLE**, `VOX/THMN_Female_Vox_Lover_Amin_145_Lead_DRY.wav`, root **A3**, start 5.5857 s, no loop, layerMix 0.5; pool of 6 (§2.1) [M-bank] |
| Default vowels | **oh**, morphing to **ah** over 600 ms (env → morph) [R] |
| Source | pitch follow **100%**. **SIZE = female/alto** [R from M REF_D2 vocal median pitch A4, 91% pitched] |
| Vibrato | 5.5 Hz, ±15 ct, delay 300 ms [R] |
| Amp env | A **25** / S 100% / R **300** ms. Typical note **4.2/16 ≈ 485 ms** [M REF_D2] |
| Voice / glide | poly 2, legato glide 60 ms [R] |
| Delay | dotted 1/4 = 692.3 ms, fb 30%, send −14 dB [R] |
| Reverb | **3.5 s**, pre-delay 28.8 ms, HPF 300 / LPF 6 k, return **−8 dB** [R from SB "2–5 s"] |
| Width | corr **0.60** (breakdown use 0.40) [M range] |
| DUCK | returns 3 dB / 157 ms [R] |
| Level | **−27.2 LUFS-S ±1** (LONG VOX = kick −3.5), TP ≤ −3.2 dBTP [MIX §2.3] |
| Must measure | > 6 kHz ≤ 1.2%, corr 0.4–0.85 [M] |

### V3 · VOX CUTS · POP · AH UH
| Block | Target |
|---|---|
| **Sample** (v1.3) | **SAMPLE**, `VOX/UHV_125_Chiki_Ah_2.wav`, root **B0**, start 0.1976 s, no loop, layerMix 0.5; + layer `VOX/UHV_125_Ta_Ta_Uh.wav` (slice source: glitch "ta-ta-uh" chopped for SHUFFLE); pool of 6 (§2.1) [M-bank] |
| Default vowels | **ah** / **uh** round-robin [R] |
| Source | pitch follow 100%, SIZE = male. Pitch env **+1 st → 0 in 30 ms** (the "bark") [R] |
| Amp env | A 1 / **D 120** / S 0 / R 30 ms. Notes **about 1–2/16** [M POP staccato 1.1/16; REF_R6 chops 2.2/16] |
| EQ | HPF 180 Hz (over the chain HPF 140) [R] |
| Delay | 1/8 = 230.8 ms, fb 15%, send −18 dB [R] |
| Reverb | plate 0.9 s, return −18 dB [R] |
| Width | corr **0.85** (dry centre) [M drop upper value] |
| DUCK | **gate shape, 4 dB** [R from DS POP] |
| Level | **−25.2 LUFS-S ±1** (POP VOX = kick −1.5), TP ≤ −3.2 dBTP [MIX §2.3] |
| Must measure | centroid 1.8–2.6 kHz, −20 dB within 150 ms [R] |

---

## 9. DRUMS: 909 lane (DRUMS output), grounded in measured one-shots (v1.2)

**What ships (v1.3, Melak: sample-driven):**
- **Every drum voice plays one of Melak's one-shots as its primary source.** The 100 curated files are in `Samples/DRUMS/{KICK,CLAP,SNARE,RIM,CH,OH,RIDE,PERC}/`, and each voice has a SHUFFLE pool (§9.8).
- **The v1.2 synth voices are an optional layer**, set per voice with mode `LAYER` and `layerMix` = sample share:
  - circuit-modelled kick, clap, rim/ghost and toms;
  - the metallic oscillator bank for hats and ride (§9.5).
- Voice targets, tuning, swing and gates (§9.2–9.7, §11) are unchanged. They now apply to the sample-plus-layer render.

**Labels used in this section:**
- **[M-melak]** = measured on Melak's one-shots;
- **[M-909free]** = measured on the CC0 909 set;
- **[M-ref]** = measured on the reference tracks (SOUND_BIBLE / CROSS_TRACK);
- [D] / [R] as in §0.1.

**Sources:**
- Script: `<private refs>/drum_oneshots/drum_measure.py`, outputs `drum_measure.json` / `drum_measure_files.csv`.
- `drum_pick.py` picks the kit samples; `bank_check.py` renders the oscillator banks.
- `plugin-design/samples/_tools/curate_drums.py` picks the 100-file drum library: distance to the UK MIN targets, MD5 dedupe, at most n/4 files per pack and voice.

**Conventions (kept from v1.1):**
- **Absolute levels (v1.3): MIX_SPEC §2.2 / §2.3.** Kick solo **−23.7 LUFS-S ±0.5, TP ≤ −11.0 dBTP** in every dialect; every other voice takes its dialect's LUFS-S target from the §0.5 table (e.g. UK MIN clap −26.2, OH −28.3, CH −40.3, perc −31.3). The kit is used by several dialects (K1 = UK MIN, LONG, POP), so the voice trims follow the **active** dialect.
- The per-slot **groove levels** in the kit tables (hat "&" / "a" / "e" = 0 / −12 / −22, ghost −5 re clap, shaker accents) stay as **relative dB re each voice's loudest hit**. The old "re the kick peak, kick solo −6 dBFS" anchor is retired.
- **Mono below 150 Hz** for the whole kit [M-ref: drums < 150 Hz corr 0.995].
- Slots are 16ths, 1–16.

### 9.0 What was measured
**Files:**
- **Melak:** 1122 WAVs; the 1122 macOS "._" metadata files were skipped.
- **Measured:** 1104. 18 were skipped as vocals, FX or loops. **0 files were unreadable, empty or silent**: the files that "failed to download" weren't in the archive, so nothing corrupt turned up.
- **CC0 909 set:** 27 files measured.

**By type (Melak):**

| Type | Files |
|---|---|
| kick | 212 |
| clap | 215 |
| snare | 149 (4 failed the spectral check) |
| closed hat | 96 |
| open hat | 160 |
| ride | 14 |
| shaker | 14 |
| perc / tom | 244 |

Rims sit inside "perc". Bare "hat" files were split into open or closed by decay (−20 dB later than 150 ms = open).

**By style (from folder names):**
- **min/tech (933 files):** Phantom Minimal/Deep Tech, UNDRGRND Minimal Tech Nation, Hyper Tripy Acid Tech, Sine Artist Series ×6, Julian Fijma, THM templates.
- **90s/Chicago (55):** Classic Chicago House.
- **Acid (64):** Acid Experiments.
- **Prog (52):** Modern Progressive House.
- **909free (27):** sampled from a JD-Xi's 909 kit (24) or a ReBirth 909 (1); 2 are unstated.

**Methods:**
- **Decay** = last time the 5 ms RMS envelope is above −6 / −20 / −40 dB re its peak.
- **Kick f0** = spectral peak of the 60–300 ms tail.
- **Kick pitch envelope** = zero-crossing frequency of the < 400 Hz band, with an exponential fit over 3–80 ms.
- **Click** = peak of the > 2 kHz band in the first 10 ms, in dB re the full peak.
- **Clap bursts** = peaks with ≥ 8 dB prominence in the first 45 ms of the 0.8–4 kHz envelope.
- **Metal partials** = up to 8 spectral peaks at 2–18 kHz in the first 60 ms (≥ 6 dB prominence), pooled into 250 Hz clusters.

**Caveats:**
- The 909free files are 128 kbps MP3 previews, so values above about 16 kHz are unreliable.
- Many pack kicks are cut at 461.5 ms, so their −40 dB point is capped.

### 9.1 Measured distributions: median (IQR), n
| Measure | min/tech [M-melak] | 90s/Chicago [M-melak] | 909free [M-909free] | REF_R5 [M-ref] |
|---|---|---|---|---|
| **Kick f0** | **52.8 Hz (50.1–55.9), 173** | 53.2 (47.3–68.8), 15 | 55.7 (55.5–62.1), 7 | 50.7 |
| Kick f @ 5 / 20 / 80 ms | 281 / 166 / 70 Hz | 210 / 95 / 59 Hz | 407 / 184 / 69 Hz | 146 / 95 / 51 Hz |
| Kick sweep | 31.3 st (25.8–37.6) | 25.2 st (23.1–29.3) | 37.3 st | about 18 st |
| **Kick pitch τ** | **28.9 ms (24.0–33.5)** | 24.5 ms (19.3–33.9) | 21.0 ms (20.6–24.6) | 19 ms |
| **Kick −6 / −20 / −40 dB** | **79 / 137 / 174 ms** | 57 / 112 / 178 ms | 41 / 79 / 295 ms | 24 / 203 / 385 ms |
| Kick click (> 2 kHz, first 10 ms) | −14.4 dB (−19.4 to −9.8) | **−8.3 dB** (−11.1 to −2.0) | −28.4 dB | — |
| **Clap bursts** | **3 (3–4), 185** | 2.5 (1–3), 10 | 3.5 (3–4), 4 | — |
| **Clap burst spacing** | **11.4 ms (10.0–14.1)** | 10.4 ms (9.7–18.1) | 12.8 ms (12.5–13.7) | — |
| Clap spectral peak | 1195 Hz (1055–1430) | 1594 Hz (1432–2336) | 879 Hz | ghost hit 1850 Hz |
| Clap −6 / −12 / −20 dB | 31 / 49 / 72 ms | 36 / 59 / 107 ms | 105 / 172 / 320 ms (JD-Xi, long tail) | 14 / 55 / 82 ms |
| Closed hat centroid | 7476 Hz (6328–9441), 72 | 4871 Hz (4780–5263), 5 | 7457 Hz, 3 | — |
| Closed hat −6 / −20 / −40 dB | 14 / 44 / 82 ms | 38 / 102 / 126 ms | 10 / 33 / — ms | — |
| Open hat centroid | 6830 Hz (6189–7755), 141 | 5193 Hz (4880–5804), 5 | 7276 Hz, 2 | — |
| Open hat −6 / −20 / −40 dB | 30 / 94 / 175 ms | 89 / 296 / 468 ms | 42 / 242 / — ms | "&" hat 15 / 138 / — ms; REF_J1/J2 −15 dB @ 166 ms |
| Ride centroid / −20 / −40 dB | 7749 Hz / 174 / 343 ms, 14 | — | crash 8053 Hz / 370 ms, 5 | — |
| Shaker band 5–95% | 4673–13442 Hz, centroid 8633, 14 | — | — | — |
| Shaker −20 dB | 72 ms (47–120) | — | — | 69 ms |
| Perc pitch / −20 dB | 740 Hz (327–2396) / 73 ms, 205 | 734 Hz / 60 ms, 10 | rim 217 Hz / 22 ms, 4 | ghost 1850 Hz / 148 ms |

**Reading the table:**
- **Kick:** the packs confirm the reference window. Kick f0 medians are 52.8–55.7 Hz, against 50–55 Hz in the refs, and A1 = 55.0 Hz sits inside the min/tech IQR.
- **The one-shots sweep further and faster than REF_R5's in-mix kick.** At 5 ms they read 281 Hz against 146 Hz. REF_R5 was measured on a demucs stem from a mastered mix, which smears the attack.
- **Kick bodies:** the one-shots' bodies are shorter (−20 dB at 137 against 203 ms).
- **Claps:** they run 3–4 bursts about 11 ms apart in the packs and in the CC0 909 set alike.
- **Hats:** 90s/Chicago hats are darker (centroid about 5 kHz) and longer than min/tech hats (about 7.5 kHz). This supports the two-kit split.

### 9.2 Kick tuning (both kits; Maestro: 40–70 Hz with KEY FOLLOW)
- **Default A1 = 55.0 Hz in D minor** [R]. It is inside the measured kick-f0 IQR (50.1–55.9 Hz [M-melak]) and the reference window (49–55 Hz [M-ref]).
- **KEY FOLLOW** (on by default): ROLLA picks the root / 4th / 5th nearest to 52.8 Hz (the measured median) inside 49–56 Hz. A tie goes to the 5th, and the tritone is never used [M-ref rule]. Keys F, F♯, A♯ and B have no root / 4th / 5th in that window, so ROLLA widens the search to 40–70 Hz and still takes the one nearest 52.8 Hz: F → B♭1 58.3 Hz, F♯ → F♯1 46.2 Hz, A♯ → A♯1 58.3 Hz, B → F♯1 46.2 Hz. It never falls back to an out-of-key 55 Hz.
- **TUNE range:** 40–70 Hz continuous, fine in cents.
- **Direction A** uses Melak's own kick, about 52.5 Hz ≈ A♭1 (the tritone of D) [L SR]. That's Melak's call; ROLLA doesn't override it.

### 9.3 K1 · DRUMS · UK MIN KIT (tight; default for UK MIN, LONG and POP)

**Primary samples (picked by `drum_pick.py`; Samples-relative names are in §9.8).** Score = mean |log(value / target)| against the REF_R5 targets.

| Voice | Reference file (under `references/melak_oneshots/Sample Market/`) | Why | Key numbers [M-melak] |
|---|---|---|---|
| Kick | `Sine_Artist_Series_16_Candidate/One Shots/Kicks/SN_CD_Kick_06.wav` | closest body to REF_R5 (−20 / −40 dB 192 / 349 vs 203 / 385 ms) | f0 57.5 Hz, start 189 Hz, sweep 20.6 st, τ 28.8 ms, −6 / −20 / −40 = 61 / 192 / 349 ms, click −15.1 dB |
| Clap | `Hyper - Tripy Acid Tech/SINGLE HITS/DRUMS/CLAPS/HY_TAT_Clap_19.wav` | decay almost identical to REF_R5 (−6 / −12 / −20 = 17 / 50 / 82 vs 14 / 55 / 82 ms) | 4 bursts at 1.9 / 9.4 / 18.0 / 35.2 ms (spacing 8.6), peak 1464 Hz |
| Open hat | `UNDRGRND Sounds - Minimal Tech Nation - Wav/…/US_MTN_Hats_Open Samples/US_MTN_Hat_Open_Rave.wav` | REF_R5 "&" hat shape (−6 / −20 / −30 = 17 / 127 / 161 vs 15 / 138 / 187 ms) | centroid 8202 Hz, band 4436–13491 Hz |
| Closed hat | `Sine_Artist_Series_Dan Fresco/One Shots/Hats/Closed Hats/SN_DF_Closed_Hat_03.wav` | −20 dB at 48 ms (target 50) | centroid 8471 Hz, −40 dB at 95 ms |
| Shaker | `Sine - Artist Series - Veritas /One Shots/Shakers & Tambourines/SN_VER_Shaker_06.wav` | matches REF_R5 (−6 / −12 / −20 = 19 / 39 / 73 vs 20 / 39 / 69 ms) | band 3962–13889 Hz, centroid 7470 Hz |
| Ghost (rim) | `Sine_Artist_Series_12_Abdul Raeva/One Shots/Percussion/SN_AR_Perc_22.wav` | −20 dB at 153 ms (REF_R5 ghost 148) | peak 2340 Hz (REF_R5 1850) |
| Tom | `Hyper - Tripy Acid Tech/SINGLE HITS/DRUMS/PERC/HY_TAT_Perc_20.wav` | low tom with a long body | 122.5 Hz, −20 dB at 212 ms |

**Voice targets (the circuit models must hit these):**

| Voice | Slots | Tune | Pitch env | Decay (−6 / −20 / −40 dB) | Level | Pan |
|---|---|---|---|---|---|---|
| **Kick** | 1, 5, 9, 13 | **55.0 Hz** (§9.2) | **+21 st (≈ 185 Hz) → f0, τ 25 ms** [D: ref sample 20.6 st / 28.8 ms; REF_R5 18 st / 19 ms [M-ref]] | **60 / 195 / 360 ms** [D: ref sample 61 / 192 / 349 [M-melak], REF_R5 203 / 385 [M-ref]] | 0 dB (solo −23.7 LUFS-S, TP ≤ −11.0 dBTP [MIX]) | C |
| Kick click | with kick | > 2 kHz transient | — | 5 ms | **−15 dB re kick peak** [M-melak min/tech median −14.4, ref sample −15.1] | C |
| **Clap** | 5, 13 | BP **1.3 kHz** [M-melak median 1195, ref 1464] | none | 4 bursts, **spacing 10 ms** [D: median 11.4, ref 8.6 [M-melak]]; tail **15 / 52 / 82 ms** to −6 / −12 / −20 [M-ref REF_R5, matched by the ref sample] | −6 dB [R] | C |
| **Ghost (rim)** | 8, 16 | **A6 1760 Hz** [R from M-ref 1850] | none | −20 dB @ **150 ms** [M-ref 148, M-melak ref 153] | clap −5 dB [M-ref] | R 10% |
| **Open hat** ("&") | 3, 7, 11, 15 | bank §9.5 (UK) | none | **15 / 135 ms**, −30 dB @ 180 ms [M-ref, matched by the ref sample] | −10 dB [R]; "&" = 0 dB hat reference [M-ref] | C ±10% |
| **Closed hat** ("a" / "e") | 4, 8, 12, 16 / 2, 6, 10, 14 | bank §9.5 (UK) | none | **13 / 45 / 85 ms** [M-melak median 14 / 44 / 82, ref 13 / 48 / 95] | "a" −12, "e" −22 dB re "&" [M-ref] | C |
| **Shaker** | 1–16 | noise BP **4–14 kHz** [M-melak band 4.7–13.4 k] | none | **20 / 70 ms** (−12 dB @ 39) [M-ref, M-melak ref] | per beat −5 / −17 / 0 / −7 dB [M-ref] | L 20% |
| Tom | slot 15, bars 4 / 8 | **D3 146.8 Hz** [R] (ref sample 122.5 Hz) | +5 st → 0, τ 40 ms [R] | −20 dB @ **200 ms** [D: ref 212] | −14 dB [R] | L 30% |

### 9.4 K2 · DRUMS · GASKIN KIT (looser, 90s; default for GASKIN 90s)

**Primary samples** (Samples-relative names are in §9.8). Picked from the 90s/Chicago and Acid packs, except the ride: the only rides are in min/tech packs.

| Voice | Reference file (under `references/melak_oneshots/Sample Market/`) | Why | Key numbers [M-melak] |
|---|---|---|---|
| Kick | `Sample Market Studio - Classic Chicago House/One Shots/Kicks/SMS_CCH_Kick_06.wav` | short body near REF_J1/J2 (−40 dB 177 vs 129 ms [M-ref]), slower sweep | f0 56.9 Hz, τ 33.9 ms, −6 / −20 / −40 = 89 / 126 / 177 ms, **click −1.0 dB** |
| Clap | `Sample Market Studio - Classic Chicago House/One Shots/Claps/SMS_CCH_Clap_04.wav` | looser tail (−20 dB at 118 ms) | 3 bursts at 8.5 / 21.3 / 29.3 ms (spacing 10.4), peak 1400 Hz, −40 dB at 220 ms |
| Open hat | `Sample Market Studio - Acid Experiments/One Shots/Hats/Open Hats/AE_Open_Hat_02.wav` | −20 dB at 214 ms, the REF_J1/J2 "&" length | centroid 10.4 kHz (brighter than the 90s median, so it is a reference for decay only) |
| Closed hat | `Sample Market Studio - Classic Chicago House/One Shots/Hats/SMS_CCH_Hat_08.wav` | −20 dB at 61 ms, dark | centroid 5263 Hz, band 1680–8538 Hz |
| Ride | `Phantom - Rapture - Minimal:Deep Tech Pack/Drum One Shots/Cymbals/Rides/Phantom - Rapture Ride 3.wav` | longest ride body (no 90s ride in the packs) | −20 dB at 442 ms, centroid 7509 Hz |
| Tom | `Sample Market Studio - Classic Chicago House/One Shots/Perc/SMS_CCH_Perc_01.wav` | 90s tom | 199 Hz, −20 dB at 157 ms |

**Voice targets:**

| Voice | Slots | Tune | Pitch env | Decay (−6 / −20 / −40 dB) | Level | Pan |
|---|---|---|---|---|---|---|
| **Kick** | 1, 5, 9, 13 | **55.0 Hz** | **+25 st (≈ 235 Hz) → f0, τ 30 ms** [D: 90s median 25.2 st / 24.5 ms [M-melak]; REF_J1/J2 preview τ 32–51 [M-ref]] | **60 / 110 / 150 ms** [D: 90s median 57 / 112 / 178 [M-melak], REF_J1/J2 −40 @ 129 [M-ref]] | 0 dB | C |
| Kick click | with kick | > 2 kHz | — | 5 ms | **−8 dB** [M-melak 90s median −8.3]. The 90s attack is about 7 dB hotter than UK's | C |
| **Clap** | 5, 13 | BP **1.5 kHz** [M-melak 90s median 1594, ref 1400] | none | **3 bursts, spacing 10.5 ms** [M-melak]; tail **45 / 115 / 215 ms** [D: ref 50 / 118 / 220, median 36 / 107 / 211] | −4 dB [R from M-ref: clap band about +4 dB stronger on 2/4] | C |
| Ghost (rim) | 16 only | A6 1760 Hz | none | −20 dB @ 150 ms | clap −8 dB [R] | R 10% |
| **Open hat** ("&") | 3, 7, 11, 15 | bank §9.5 (GASKIN, [LOW-CONF]) | none | **50 / 230 / 420 ms** [D: ref 5 / 214 / 418 [M-melak]; REF_J1/J2 −15 dB @ 166 [M-ref]; 90s median 89 / 296 / 468] | −8 dB [R] | C |
| **Closed hat** (on the beat) | 1, 5, 9, 13 | bank §9.5 (GASKIN, [LOW-CONF]) | none | **25 / 60 / 110 ms** [M-melak ref 22 / 61 / 65, 90s median 38 / 102 / 126] | −12 dB re open [R]; no "e" / "a" hats [M-ref] | C |
| Shaker | off | — | — | — | [M-ref: no off-16th hits] | — |
| **Ride** | 3, 7, 11, 15, under the open hat | bank §9.5 (RIDE) | none | −20 dB @ **440 ms** [M-melak ref 442] | −18 dB [R] | R 20% |
| Tom | slot 15, bar 8 | **G3 196 Hz** [D: nearest in-key note (the 4th) to the measured 199 Hz] | +5 st → 0, τ 40 ms [R] | −20 dB @ **160 ms** [M-melak ref 157] | −14 dB | L 30% |

### 9.5 Metallic oscillator banks (hats and ride; v1.3: optional layer under the sampled hats and ride)
**Design.** Each bank is **6 square oscillators** summed into an HPF (4-pole) and an LPF (2-pole), with an amp envelope per voice (OH/CH choke) and **white noise at −12 dB** for air [R].

**Ratios.** These come from the measured partial clusters [M-melak], pooled across all files of the type: the 6 strongest 250 Hz clusters, as ratios to the lowest.

| Bank | Oscillator Hz (ratio) | Filter | Rendered centroid vs measured |
|---|---|---|---|
| **UK hats** (CH + OH), from 213 min/tech hat files, 1684 partials | **3743 (1) · 4750 (1.269) · 5974 (1.596) · 6966 (1.861) · 8493 (2.269) · 9672 (2.584)** | HP 5 kHz / LP 12 kHz | **7515 Hz** vs 7476 Hz [D, `bank_check.py`] |
| **GASKIN hats** **[LOW-CONF, ships in v0; A&R re-check after the first build]**, from 10 90s/Chicago hat files, 78 partials (low n) | **3346 (1) · 4938 (1.476) · 5886 (1.759) · 6868 (2.052) · 9214 (2.753) · 10379 (3.102)**. The 6th ratio is borrowed from the ride: only 5 clusters were found | HP 2 kHz / LP **7 kHz** | 6003 Hz with LP 9 kHz. LP 7 kHz is the [R] step toward the measured **4871–5193 Hz**; verify in the harness |
| **RIDE**, from 14 ride files, 110 partials | **3695 (1) · 4915 (1.33) · 5963 (1.614) · 7178 (1.943) · 9599 (2.598) · 11462 (3.102)** | HP 4 kHz / LP 14 kHz | **7849 Hz** vs 7749 Hz [D] |
| CC0 909 hats (comparison) [M-909free], n = 5 hats, lossy | 4894 · 5725 · 7906 · 9060 · 9790 · 10916 (1 · 1.17 · 1.615 · 1.851 · 2.0 · 2.231) | — | measured centroid 7276–7457 Hz |

**GASKIN hat ratios: v0, low confidence (v1.3b, Maestro).** They ship as the GASKIN bank default, tagged **[LOW-CONF]**: n = 10 files / 78 partials, the 6th ratio borrowed from the ride, the LP 7 kHz an [R] step, and no 90s ride in the packs. **A&R re-checks them after the first build** (§11.1 item 8). If they fail, fall back to BANK MODE 808 for K2 (its lowest three ratios match within 0.5–2.3%) until more 90s hats are measured. In v1.3 the bank is an optional layer under the sampled K2 hats (`DRUMS/CH/CCH_Hat_08.wav`, `DRUMS/OH/AE_Open_Hat_02.wav`), so the risk is confined to LAYER mode.

**Compared with the classic six-square approach.**
- The 808-style bank runs six squares at **205.3 / 304.4 / 369.6 / 522.7 / 540 / 800 Hz** (ratios **1 : 1.483 : 1.800 : 2.546 : 2.630 : 3.897**). These are published circuit values, not measured here.
- Its oscillators sit about 4 octaves below the measured clusters. The metallic sound comes from the dense odd harmonics, band-passed high: rendered with HP 5 k / LP 12 k it reaches a 7505 Hz centroid [D].
- Notably, the **lowest three 90s/Chicago cluster ratios (1 : 1.476 : 1.759) match the 808 ratios (1 : 1.483 : 1.800) within 0.5–2.3%**.
- The original 909's hats and cymbals were sampled, not circuit-generated, so there is no "909 oscillator ratio" to copy.

**Recommendation [R].**
- Ship a **BANK MODE** switch: **MEASURED** (the table above, default per kit) / **808** (six squares at the ratios above, base 205.3 Hz, TONE scales the base ±1 oct).
- Both modes are cheap: 6 BLEP squares per bank.

### 9.6 Generator, SHUFFLE / KEEP and DUCK (Maestro-approved)
**The generator writes the drum part from rules.** These are fixed and never shuffled:
- **4/4 kick** on 1 / 5 / 9 / 13;
- **clap on 2 and 4** (slots 5 / 13);
- **the hat pattern of the kit:**
  - UK: open on "&", closed on "a" / "e" at −12 / −22, shaker 16ths at −5 / −17 / 0 / −7;
  - GASKIN: open on "&", closed on the beat, ride under the "&".

The **sparse layer** is: the ghost on 8 / 16 (dropped under a stab hit), the tom fill on slot 15 of bars 4 / 8, an open-hat lift on bar 8, and hat accent jitter ±1 dB.
- **SHUFFLE** rerolls only the sparse layer, inside those rules.
- **KEEP** locks the current drum part.

**DUCK** follows **ROLLA's own kick note-ons** when DRUMS is on. With DRUMS off it follows the host grid (quarter notes) [Maestro].

### 9.7 SWING (hats, shaker, ghost and percs only; kick, clap, bass and stab stay straight)
Formula [D]: **delay of each off-16th = (S − 50)/100 × 230.8 ms** at 130 BPM.

| Context | SWING | 16th delay @ 130 BPM | Label |
|---|---|---|---|
| **K1 UK MIN KIT** (default) | **52%** | **4.6 ms** | [M-ref] REF_R4/R5 hats 52.8% |
| **K2 GASKIN KIT** (default) | **50%** | **0 ms** | [M-ref] CTR D5: straight; the ~67% figure is not confirmed |
| Dialect UK MIN | 52% | 4.6 ms | [M-ref] |
| Dialect LONG | 51% | 2.3 ms | [R] mockup LONG value |
| Dialect POP | 50% | 0 ms | [M-ref] REF_B1 50.4% |
| Dialect GASKIN 90s | 50% | 0 ms | [M-ref] |
| User range | 50–57% | 0–16.2 ms | [M-ref] UK hats 50.4–57.1% |
| Direction A drums (for reference) | 56% | 13.8 ms | [L] PTD 6 |

**Returns and bus.**
- K1 clap room 1.2 s, return −12 dB. K2 clap room 1.5 s, return −10 dB [R from M-ref ghost T60 1.2 s].
- Bus: HPF 30 Hz, soft clip 1 dB (K1) / 1.5 dB (K2) [R].
- Bus targets in §0.5.
- The hats get 1–2 dB of saturation [R SB].
- In Direction A the slot-16 ghost is dropped under a stab hit [L].

### 9.8 Kit sample blocks (v1.3)
Each kit preset holds one Maestro sample block per voice.
- **Default:** mode `SAMPLE`, layerMix 1.0. Switching a voice to `LAYER` adds its v1.2 synth voice; layerMix 0.7–0.85 is a good start [R].
- **Root** = measured pitch (kick: tail f0 [M-melak]). Unpitched voices are marked C3, meaning play unshifted.
- **KICK TUNE / KEY FOLLOW** (§9.2) repitch the kick sample from its root. CD_Kick_06 measured 57.5 Hz [M-melak] and plays −0.77 st to reach 55.0 Hz.
- **Pools** are the next-best files of the same voice (UK MIN score). SHUFFLE / round-robin picks from them.
- **Prep:** every drum file is MIX_SPEC §4-prepped (−24 LUFS, −6 dBTP cap, ≤ 2 ms lead, 12 ms fade-out; 15 bit-identical dual-mono files stored mono). Per-file gain is in `SAMPLE_MANIFEST.json`.

| Kit | Voice | Primary (Samples-relative) | Original (Drum Packs-relative) |
|---|---|---|---|
| drums.ukmin.kit | kick | `DRUMS/KICK/CD_Kick_06.wav` | `Sample Market/Sine_Artist_Series_16_Candidate/One Shots/Kicks/SN_CD_Kick_06.wav` |
| drums.ukmin.kit | clap | `DRUMS/CLAP/TAT_Clap_19.wav` | `Sample Market/Hyper - Tripy Acid Tech/SINGLE HITS/DRUMS/CLAPS/HY_TAT_Clap_19.wav` |
| drums.ukmin.kit | perc_ghost | `DRUMS/RIM/AR_Perc_22.wav` | `Sample Market/Sine_Artist_Series_12_Abdul Raeva/One Shots/Percussion/SN_AR_Perc_22.wav` |
| drums.ukmin.kit | hat_closed | `DRUMS/CH/DF_Closed_Hat_03.wav` | `Sample Market/Sine_Artist_Series_Dan Fresco/One Shots/Hats/Closed Hats/SN_DF_Closed_Hat_03.wav` |
| drums.ukmin.kit | hat_open | `DRUMS/OH/MTN_Hat_Open_Rave.wav` | `Sample Market/UNDRGRND Sounds - Minimal Tech Nation - Wav/US_MTN_One-Shots/US_MTN_Drums/US_MTN_Drum_Formats/Kontakt/US_MTN_Hats_Open Samples/US_MTN_Hat_Open_Rave.wav` |
| drums.ukmin.kit | perc_tom | `DRUMS/PERC/TAT_Perc_20.wav` | `Sample Market/Hyper - Tripy Acid Tech/SINGLE HITS/DRUMS/PERC/HY_TAT_Perc_20.wav` |
| drums.ukmin.kit | shaker | `DRUMS/PERC/VER_Shaker_06.wav` | `Sample Market/Sine - Artist Series - Veritas /One Shots/Shakers & Tambourines/SN_VER_Shaker_06.wav` |
| drums.gaskin.kit | kick | `DRUMS/KICK/CCH_Kick_06.wav` | `Sample Market/Sample Market Studio - Classic Chicago House/One Shots/Kicks/SMS_CCH_Kick_06.wav` |
| drums.gaskin.kit | clap | `DRUMS/CLAP/CCH_Clap_04.wav` | `Sample Market/Sample Market Studio - Classic Chicago House/One Shots/Claps/SMS_CCH_Clap_04.wav` |
| drums.gaskin.kit | hat_closed | `DRUMS/CH/CCH_Hat_08.wav` | `Sample Market/Sample Market Studio - Classic Chicago House/One Shots/Hats/SMS_CCH_Hat_08.wav` |
| drums.gaskin.kit | hat_open | `DRUMS/OH/AE_Open_Hat_02.wav` | `Sample Market/Sample Market Studio - Acid Experiments/One Shots/Hats/Open Hats/AE_Open_Hat_02.wav` |
| drums.gaskin.kit | ride | `DRUMS/RIDE/PHR_Ride_3.wav` | `Sample Market/Phantom - Rapture - Minimal:Deep Tech Pack/Drum One Shots/Cymbals/Rides/Phantom - Rapture Ride 3.wav` |
| drums.gaskin.kit | perc_tom | `DRUMS/PERC/CCH_Perc_01.wav` | `Sample Market/Sample Market Studio - Classic Chicago House/One Shots/Perc/SMS_CCH_Perc_01.wav` |

```json
"drums.ukmin.kit": {"voices": {"kick": {"mode": "SAMPLE", "file": "DRUMS/KICK/CD_Kick_06.wav", "root": "A#1", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/KICK/PHR_Kick_5.wav", "DRUMS/KICK/CD_Kick_12.wav", "DRUMS/KICK/MPH_Kick_07.wav", "DRUMS/KICK/MTN_Kick_Rave.wav", "DRUMS/KICK/MTN_Kick_Rave_2.wav", "DRUMS/KICK/CD_Kick_04.wav"]}, "clap": {"mode": "SAMPLE", "file": "DRUMS/CLAP/TAT_Clap_19.wav", "root": "C3", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/CLAP/VER_Clap_15.wav", "DRUMS/CLAP/TAT_Clap_11.wav", "DRUMS/CLAP/VER_Clap_14.wav", "DRUMS/CLAP/DCB_Talk_Clap.wav", "DRUMS/CLAP/AR_Clap_02.wav", "DRUMS/CLAP/TAT_Clap_06.wav"]}, "perc_ghost": {"mode": "SAMPLE", "file": "DRUMS/RIM/AR_Perc_22.wav", "root": "D7", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/RIM/DF_Perc_15.wav", "DRUMS/RIM/DCB_Mino_Perc.wav", "DRUMS/RIM/TAT_Perc_27.wav", "DRUMS/RIM/CD_Perc_21.wav", "DRUMS/RIM/DF_Perc_09.wav", "DRUMS/RIM/JF_Perc_09.wav"]}, "hat_closed": {"mode": "SAMPLE", "file": "DRUMS/CH/DF_Closed_Hat_03.wav", "root": "C3", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/CH/AR_Closed_Hat_10.wav", "DRUMS/CH/AR_Closed_Hat_02.wav", "DRUMS/CH/AR_Closed_Hat_06.wav", "DRUMS/CH/PHR_Closed_Hihat_17.wav", "DRUMS/CH/PHR_Closed_Hihat_7.wav", "DRUMS/CH/PHR_Closed_Hihat_5.wav"]}, "hat_open": {"mode": "SAMPLE", "file": "DRUMS/OH/MTN_Hat_Open_Rave.wav", "root": "C3", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/OH/MTN_Hat_Open_Rave_2.wav", "DRUMS/OH/AR_Open_Hat_16.wav", "DRUMS/OH/SD_Hat_28.wav", "DRUMS/OH/DF_Open_Hat_15.wav", "DRUMS/OH/AR_Open_Hat_13.wav", "DRUMS/OH/AR_Open_Hat_01.wav"]}, "perc_tom": {"mode": "SAMPLE", "file": "DRUMS/PERC/TAT_Perc_20.wav", "root": "B2", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/PERC/PHR_Percussion_4.wav", "DRUMS/PERC/TAT_Perc_03.wav", "DRUMS/PERC/VER_Perc_13.wav", "DRUMS/PERC/CCH_Perc_07.wav", "DRUMS/PERC/TAT_Perc_32.wav", "DRUMS/PERC/VER_Perc_17.wav"]}, "shaker": {"mode": "SAMPLE", "file": "DRUMS/PERC/VER_Shaker_06.wav", "root": "C3", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/PERC/VER_Shaker_08.wav", "DRUMS/PERC/VER_Shaker_09.wav"]}}}
"drums.gaskin.kit": {"voices": {"kick": {"mode": "SAMPLE", "file": "DRUMS/KICK/CCH_Kick_06.wav", "root": "A#1", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/KICK/CD_Kick_06.wav", "DRUMS/KICK/PHR_Kick_5.wav", "DRUMS/KICK/CD_Kick_12.wav", "DRUMS/KICK/MPH_Kick_07.wav", "DRUMS/KICK/MTN_Kick_Rave.wav", "DRUMS/KICK/MTN_Kick_Rave_2.wav"]}, "clap": {"mode": "SAMPLE", "file": "DRUMS/CLAP/CCH_Clap_04.wav", "root": "C3", "start": 0.0002, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/CLAP/TAT_Clap_19.wav", "DRUMS/CLAP/VER_Clap_15.wav", "DRUMS/CLAP/TAT_Clap_11.wav", "DRUMS/CLAP/VER_Clap_14.wav", "DRUMS/CLAP/DCB_Talk_Clap.wav", "DRUMS/CLAP/AR_Clap_02.wav"]}, "hat_closed": {"mode": "SAMPLE", "file": "DRUMS/CH/CCH_Hat_08.wav", "root": "C3", "start": 0.0005, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/CH/AR_Closed_Hat_10.wav", "DRUMS/CH/AR_Closed_Hat_02.wav", "DRUMS/CH/DF_Closed_Hat_03.wav", "DRUMS/CH/AR_Closed_Hat_06.wav", "DRUMS/CH/PHR_Closed_Hihat_17.wav", "DRUMS/CH/PHR_Closed_Hihat_7.wav"]}, "hat_open": {"mode": "SAMPLE", "file": "DRUMS/OH/AE_Open_Hat_02.wav", "root": "C3", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/OH/MTN_Hat_Open_Rave.wav", "DRUMS/OH/MTN_Hat_Open_Rave_2.wav", "DRUMS/OH/AR_Open_Hat_16.wav", "DRUMS/OH/SD_Hat_28.wav", "DRUMS/OH/DF_Open_Hat_15.wav", "DRUMS/OH/AR_Open_Hat_13.wav"]}, "ride": {"mode": "SAMPLE", "file": "DRUMS/RIDE/PHR_Ride_3.wav", "root": "C3", "start": 0.0, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/RIDE/PHR_Ride_5.wav", "DRUMS/RIDE/PHR_Ride_4.wav", "DRUMS/RIDE/VER_Ride_04.wav", "DRUMS/RIDE/VER_Ride_03.wav", "DRUMS/RIDE/VER_Ride_05.wav"]}, "perc_tom": {"mode": "SAMPLE", "file": "DRUMS/PERC/CCH_Perc_01.wav", "root": "G3", "start": 0.0004, "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/PERC/PHR_Percussion_4.wav", "DRUMS/PERC/TAT_Perc_03.wav", "DRUMS/PERC/TAT_Perc_20.wav", "DRUMS/PERC/VER_Perc_13.wav", "DRUMS/PERC/CCH_Perc_07.wav", "DRUMS/PERC/TAT_Perc_32.wav"]}}}
```

---

## 10. Post-v0 (not shipped in v0; numbers kept so they can return without new research)

### 10.1 BASS · GASKIN 90s · ORGAN (DS §14.2 hero, dropped from v0 for ROUND, see §2)
| Block | Target |
|---|---|
| Osc 1 / 2 / 3 | **sine 16' 0 dB · sine 8' (+1 oct) −3 dB · sine 4' (+2 oct) −17 dB** [D from M h2 −3, h4 −17 dB]. Optional pulse 50% +1 oct −20 dB [R] |
| Sub / unison | off / 1 |
| Filter / env | Ladder LP24 **130 Hz** [D from M settle 126 Hz] · **+29 st** (about 690 Hz) [D from M 694→126 Hz], **τ 31 ms** [M] |
| Amp env | A 2 / D 200 / S −6 dB / R 30 ms [R] |
| Voice | mono, glide 0 [R] |
| Drive / EQ | tube 2–3 dB [R] · HPF 35 Hz, optional +2 dB at 110 Hz [R] |
| Width / DUCK | mono corr ≥ 0.999 [M] · **7 dB / 110 ms** [M preview 7.3 / 110] |
| Level / checks | −24.7 LUFS-S ±0.5 (BASS, MIX §2.2) · < 200 Hz ≥ 92% [M], 200–400 Hz ≤ 1% [M CTR] |

### 10.2 HITS extras
- **ORCH HIT (GASKIN 90s)** [R]:
  - Voicing: power chord D4–A4–D5, sent as generator notes (no chord memory).
  - Osc: saws at oct 0 / +1 / +2 (0 / −4 / −9 dB), unison 3 at 12 ct.
  - Noise burst: pink, BP 1.5 kHz, D 60 ms, −6 dB.
  - Pitch env −1 st → 0 in 40 ms.
  - LP12 7 kHz with env +12 st, D 150.
  - Amp A 0 / D 450 / S 0 / R 200. HPF 120.
  - Hall 2.2 s, return −10 dB.
  - Corr 0.6. DUCK 3 dB. −27.7 LUFS-S (HITS, MIX §2.2).
  - The v1 −1 oct square is dropped so it fits E2's 3 oscillators + noise.
- **RAVE SAW (UK MIN)**:
  - Voicing: quartal A3–D4–G4–C5 [L], sent as generator notes (HITS have no chord memory).
  - Osc: saw, **unison 5 (cap)** at 22 ct, spread 90%, + ensemble 30% (v1 had unison 7).
  - LP24 3 kHz, res 25%, env +24 st, τ 40 ms.
  - Amp A 0 / D 250 / S 0 / R 120. Drive 3 dB. HPF 200.
  - Delay 3/16 = 346.2 ms, fb 35%, send −12. Plate 1.2 s, return −14 [L returns].
  - Corr 0.5. DUCK 3 dB. −27.7 LUFS-S (HITS, MIX §2.2).

### 10.3 DS §14.2 BASS variants
These are unchanged from DS §14.2:
- SLOW PLUCK and SYNC LINE (UK MIN; named differently in DS §14.2);
- PUMP and SOFT (LONG);
- BRIGHT and SUB POP (POP);
- ORGAN GATE and 90s DIRT (GASKIN 90s).

### 10.5 Extra presets suggested by Melak's packs (post-v0, v1.3) [M-bank / R]
| Proposed preset | Source files (Samples-relative) | Why |
|---|---|---|
| BASS · GASKIN 90s · **DONK** | `BASS/VER_Donk_Bass_C.wav` | A real donk bass shot. GASKIN 90s has no bass of its own (it borrows ROUND); this gives it a 90s-native one. |
| BASS · LONG · **REESE** | `HITS/VER_HyperReese_Bass_C.wav` (also the HOOVER pool), `BASS/VER_VeritasReese_Bass_C.wav` | Moving reese bodies (attack 486–748 ms, corr 0.91–0.93); mono below 120 Hz by width crossover. |
| BASS · GASKIN 90s · **ORGAN** (from §10.1) | `HITS/VER_DX7Organ_Bass_C.wav` | Melak's DX7-organ bass shot (<200 Hz 1.00, centroid 93 Hz) replaces the 3-sine drawbar plan. |
| BASS · POP · **303 SQUARE** | `BASS/MA_C_303Square_Synth.wav` | <200 Hz 0.99, centroid 133 Hz, clean mono. A POP / acid alternative to CLICK. |
| STAB · GASKIN 90s · **CHORD STAB** | `STAB/THMN_Chord_Stab_1_Cmin.wav` … `_5_` (5 files) | A real minor-chord stab set; round-robin across the 5 voicings. The GASKIN STAB borrow goes away. |
| HITS · UK MIN · **ACID** | `HITS/AR_Acid_01_C.wav`, `_05_`, `_09_`, `HITS/JF_128_Dm_WowAcid_FX.wav` (already in D minor) | Acid hits in place of RAVE SAW, which is post-v0. |
| HITS · LONG · **SIREN** | `HITS/VER_134_Siren_01.wav`, `HITS/MTN_FX_Siren.wav` | Rave sirens (corr 0.07–0.38) for fills. |
| PAD · UK MIN · **VOCAL PAD** | `PAD/JF_128_Gsm_Vocal_Texture_Pad.wav` | A choir-like texture (corr 0.08), granular-frozen. It ties the PAD lane to VOX CUTS. |
| PAD · LONG · **TENSION STRING** | `PAD/THMN_Tension_String_C_142.wav` | No-3rd string (♭3 / 3 ≈ 0.02) in C, so it pitches cleanly to D. |
| PAD · ALL · **RISER / REVERSE** | `PAD/VER_134_GatedUplifter_FX.wav`, `PAD/THMN_Sweep_Up_1.wav` | Build tools: a gated uplifter and a sweep. Reversed or stretched to bar length by the generator. |
| VOX · GASKIN 90s · **HOUSE PHRASE** | `VOX/UHV_125_A_House_Thing.wav`, `VOX/UHV_125_Deep_Inside_My_Soul.wav`, `VOX/UHV_125_Love_Will_Bring_Us_Back_Together.wav` | Classic house phrases at 125 BPM, played in slice mode and time-stretched to 130. |
| VOX · ALL · **VOCAL BASS** | `VOX/JF_128_VocalBass_FX.wav` | A vocal-bass texture for VOX layered under BASS. |

### 10.4 VOX vowel keyswitches (post-v0, Maestro v1.2)
- **Planned mapping:** C1 = oh, D1 = ah, E1 = hey, F1 = uh (MIDI 24 / 26 / 28 / 29). They latch and make no sound.
- The numbers are kept from v1.1 so the feature can return without new design work.

---

## 11. Mix-fit checklist (harness gates)
| Check | Gate | Basis |
|---|---|---|
| Bass energy < 200 Hz | ≥ 91% (PLUCK 91–95, CLICK ≥ 95) | [M] REF_R5 91.8%, render 95.3%, REF_B1 97.5% |
| Bass corr | ≥ 0.997 | [M] 0.997–1.000 |
| Bass > 1 kHz | ≤ 0.5% | [M] 0.1% |
| Bass filter env settle | PLUCK 50–70 ms, CLICK τ 12–22 ms, ROUND τ 40–55 ms | [M / L] |
| Stab corr / centroid / tail | DUB STAB 0.48–0.63 (target 0.55), about 1.5 kHz, −10 dB at 850–900 ms | [M] |
| PAD corr / centroid | SUS SWELL 0.4–0.8 / 1.4–2.3 kHz; WIDE SWELL ≤ 0.5 / about 2.4 kHz | [M] |
| Non-bass synth parts < 120 Hz | ≤ −30 dB re part total, and mono | [D] mix corr below 120 Hz = 0.993 [M] |
| SUM (MAIN + DRUMS), dialect default lanes **and, with the §0.5.1 auto-trim, any active-lane set** | −19.0 LUFS-S ±0.5 and TP −6 ±1 dBTP per dialect; spread ≤ 1.0 LU; corr < 120 Hz ≥ 0.99; limiter GR = 0 (`mix_check.py styles`) | §0.5 / MIX §2.1 |
| MAIN bus | report only: −22.7 / −23.6 / −21.5 / −21.0 LUFS-S (UK MIN / LONG / POP / GASKIN) ±1 | §0.5 [D] |
| DRUMS bus | report only: −21.5 / −21.3 / −22.7 / −22.0 LUFS-S ±1; kick solo −23.7 ±0.5 LUFS-S, TP ≤ −11.0 dBTP; corr < 150 Hz ≥ 0.995 | §0.5 [D] / MIX §2.2 / [M] 0.995 |
| Kick / bass invariants | BASS re KICK −1.0 ±0.5 per dialect; kick and bass spread across dialects ≤ 1 dB in ST max, sub+low and < 120 Hz energy | MIX §2.1 |
| Kick | tail f0 within ±5 ct of TUNE; f @ 5 ms: K1 185 ±25 Hz, K2 235 ±30 Hz; K1 −6 / −20 / −40 dB at 60 / 195 / 360 ms ±15%; K2 at 60 / 110 / 150 ms ±15%; click K1 −15 ±3 dB, K2 −8 ±3 dB | §9.3 / §9.4 [M-melak / M-ref] |
| Clap | burst count K1 4, K2 3; spacing 10 ±1.5 ms; spectral peak K1 1.1–1.5 kHz, K2 1.4–1.7 kHz; −20 dB K1 82 ±10 ms, K2 115 ±15 ms | [M-melak / M-ref] |
| Hats / ride (bank) | closed-hat centroid K1 7476 ±10%, K2 4.9–5.2 kHz [LOW-CONF ratios, §11.1 item 8]; ride 7749 ±10%; closed −20 dB K1 45 ±10 ms, K2 60 ±10 ms; open "&" −20 dB K1 135 ±20 ms, K2 230 ±30 ms; ride −20 dB 440 ±60 ms | [M-melak / M-ref] |
| Shaker | energy 5–95% inside 4–14 kHz; −20 dB at 70 ±10 ms | [M-melak / M-ref] |
| User sample slots | 14 slots load, root-detect, prep to −24 LUFS / −6 dBTP and hit the lane target; fallback on a missing file (§11.1 item 10) | S17 / E22 |
| Chord memory | S1–S3 render the §4 main / lift notes from one key, ±10 ct; HITS play one hit per key | §4 / E10 |
| Sample files | every `file` / `pool` / layer path resolves under the Samples root; a missing file falls back to mode SYNTH and logs a warning (no silence) | §12.3 |
| Sample pitch | rendered note f0 within ±10 ct of the played note after repitch from `root` (tonal lanes) | §0.8 |
| Loop / slice clicks | no step > −40 dBFS at loop and slice boundaries; crossfade as specified | §12.3 |
| Memory / streaming | RAM for the full curated set ≤ 128 MB in the hybrid mode; no streaming dropouts at 64-sample buffers | §12.3 |
| Groove levels | K1 hat "&" / "a" / "e" = 0 / −12 / −22 dB ±1.5; shaker −5 / −17 / 0 / −7 ±1.5; ghost −5 ±1 re clap | [M] |
| Swing | off-16th delay = (S − 50)% × 230.8 ms ±0.5 ms | [D] |
| Tail at barline | ≤ −45 dBFS in the last 10 ms | [L] |
| Loudness | lane × active-dialect LUFS-S target within the MIX tolerance (±0.5 kick/bass, ±1 stab/pluck/hits/vox/clap, ±1.5 pad/hats/rim/perc); TP ≤ lane cap | §0.5 / MIX §2.2 |
| Sample prep (staged files) | DC ≤ −60 dBFS; lead silence ≤ 2 ms at −60 dBFS; last 10 ms ≤ −60 dBFS; TP ≤ −6 dBTP; −24 LUFS ±0.1 unless TP-capped. **252 / 252 pass** (`prep_log.json`, re-checked with `mix_check.py` sample checks) | MIX §4 |
| Duck | dip within ±2 dB of preset depth, recovery ±20 ms | [R] |
| Aliasing | no alias above −80 dBFS for notes ≤ C6 with the filter open | [R, DS §14.4] |
| Oscillator count | PAD ≤ 40, unison ≤ 5 everywhere | Maestro / §7.1 |
| A&R listen | each dialect default beside the locked Direction A render | DS §14.5 |

### 11.1 Items to measure after the first engine render
1. **Ladder vs Butterworth cutoff (BASS PLUCK).** The locked numbers come from a Butterworth LP24 render (`patches_v1.py`). A zero-resonance ladder has a softer knee, so the 170 Hz rest **may need about +10–20 Hz** to hit 95.3% < 200 Hz. This is an estimate, not measured. Measure < 200 Hz % on the engine render and set the cutoff from that. ROUND (110 Hz) and CLICK (350 Hz) get the same check.
2. **Pad width after re-voicing.** With unison down to 3, read corr and adjust WIDTH until the A1 / A2 targets are met.
3. **Hoover width.** Unison 5 plus ensemble 40% has to reach corr 0.45.
4. **Hat / ride banks.** Render both BANK MODEs and check centroid and decay against §9.5 and §11. The GASKIN LP 7 kHz value is [R]: at 9 kHz the bank rendered a 6003 Hz centroid against a 4.9–5.2 kHz target.
5. **Kick sweep.** Both kits start from one-shot values ([M-melak]), which are faster and wider than REF_R5's in-mix kick ([M-ref], a demucs stem). Compare a mix render with REF_R5's 146 Hz @ 5 ms before locking.
6. **CPU.** Measure the E19 budget table (§12.2, now including samplers) on the first full build.
7. **Sample presets vs v1.2 targets.** Render every SAMPLE / LAYER preset and compare with its "Must measure" row. Where a pick misses, change layerMix, the filter or the pool pick, never the target. Watch these three:
   - A1 SUS SWELL source centroid is 1158 Hz against the 1.4–2.3 kHz target, so the filter must open;
   - A2's source corr is 0.08, so width must bring it to ≤ 0.5;
   - S1 needs the quartal stack rendered (now by chord memory, §4).
8. **GASKIN hat ratios, A&R re-check [LOW-CONF].** After the first build, render K2's CH / OH / ride in the bank modes MEASURED (GASKIN) and 808, both solo and layered under the K2 samples, at the GASKIN 90s default pattern.
   - Measure: centroid vs the 4.9–5.2 kHz target (§11); −20 dB at CH 60 ±10 ms and OH 230 ±30 ms.
   - A&R listen beside the REF_J1/J2 reference and the UK bank.
   - **Pass:** the centroid is in range and A&R signs off → the ratios lose the [LOW-CONF] tag. **Fail:** K2 defaults to BANK MODE 808, and the ratios get re-measured on more 90s hats.
9. **Chord memory (STAB).** Render S1 / S2 / S3 from single keys at refKey and with the bar-8 lift flag. The rendered notes must be the §4 table's (pitch ±10 ct per note), with no extra voices and no voice-steal clicks at the 1/32-early overlap.
10. **User sample slots.** Drop a test file into each of the 14 slots (a tonal file in the lanes, a matching one-shot per drum voice). Check:
   - the detected root (±10 ct on the render);
   - the prepped copy passes the MIX §4 checks (DC, lead ≤ 2 ms, last 10 ms ≤ −60 dBFS, TP ≤ −6 dBTP, −24 LUFS);
   - the lane / voice solo lands on its §0.5 target;
   - SAMPLE / LAYER / off switch without clicks;
   - the slot survives a dialect switch and a session reload; a missing file falls back with a warning.

---

## 12. Engine requirements (for the JUCE engineer; Maestro to check against the build)
"v0" = in scope per Maestro, 2026-10-17. "DS" = already in DESIGN §14.

| # | Requirement | Used by | Status |
|---|---|---|---|
| E1 | **Band-limited oscillators:** PolyBLEP (or mip-mapped WT) saw, pulse with **PW 5–95% and PWM**, triangle, sine | all synth | saw/square DS; **PW/PWM v0** |
| E2 | **3 oscillators per voice + separate sine sub** (own LP24 120 Hz, outside the env filter); per osc level dB, octave ±3, semitone ±24, fine ±100 ct | BASS, H1, PADs | DS |
| E3 | **2-op FM:** carrier + 1 modulator, ratio 0.5–16, index 0–10 with its own envelope, velocity → index | H2 | **v0 (Maestro)** |
| E4 | **Noise:** white + pink, its own HPF/BP and **its own AD envelope** (0–100 ms, or follow amp for pads) | S3, H1, H2, A2, VOX | **v0 (Maestro)** |
| E5 | **3-band formant filter with vowel morph:** 3 parallel resonant BPs (F1–F3, bandwidths per §8), vowel tables oh/ah/hey/uh, morph with a time glide (hey: /ɛ/→/ɪ/ over 120 ms), SIZE scaling male↔female | VOX CUTS | **v0 (Maestro)** |
| E6 | **Unison ≤ 5 voices (hard cap)**, detune 0–50 ct, spread 0–100%, phase randomise | S1/S2, H2, H3, P2/P3, PADs | **cap v0 (Maestro)**; DS had 2 |
| E7 | **Width:** M/S width 0–200% with a **mono-below crossover** (100–300 Hz) + **chorus/ensemble** (3-tap, rate, depth, mix) | all synth; replaces unison width | **v0** |
| E8 | **Filters:** Ladder LP24/LP12 with drive (2× OS); SVF LP/HP/BP 12 dB; key track; resonance | all | DS |
| E9 | **Envelopes:** amp ADSR; filter env as **exponential τ** and as ADSR with attack (P3 bloom, A2 1-bar rise); **pitch env** ±24 st with τ (H3, drums); mod env → formant morph / FM index | all | τ DS; **pitch env and env attack v0** |
| E10 | **CHORD memory** on STAB only: per-preset `chordMemory` with main + lift sets (refKey, PARALLEL intervals), lift selected by the generator's bar-8 flag, fold above C6, strum 0 (§4). HITS have none. Vowel keyswitches → **post-v0** (§10.4) | STAB | **v0 (Maestro v1.3b: STAB DUB / SWELL / CLICK)**; keyswitches post-v0 |
| E11 | **Voice modes:** mono legato with constant-time glide 0–200 ms (overlap-only option); poly with a per-lane voice limit (PAD 8) and oldest-release stealing (5 ms fade) | all | mono DS; **poly limit v0** |
| E12 | **LFO:** 2 per voice, tempo sync (to 2 bars), delay/fade; targets pitch / PW / cutoff / formant morph | H3, A1, V2 | v0 |
| E13 | **Velocity curve** (v/112)^2.06 → amp, cutoff, env amount, FM index, F3 | all | DS |
| E14 | **FX per lane:** drive (soft clip, tube/asym); ping-pong delay synced (1/16 to dotted 1/4) with HP/LP in the loop; **plate and hall** 0.8–4.0 s with pre-delay; EQ (HPF/LPF 12/24, bell, shelf); tail gate | all | delay, plate, tail gate DS; **hall to 4 s, per-lane chains v0** |
| E15 | **Two stereo outputs:** MAIN (6 synth lanes summed after per-lane trims) and DRUMS. Per-lane mute; **MAIN output trim**; safety limiter on each bus (should never engage on factory presets) | all | **v0 (Maestro)** |
| E16 | **909 DRUMS lane:** a **sampled voice per drum sound** (Melak's one-shots, §9.8), each with an optional synth layer (circuit kick / clap / rim / toms, metallic bank for hats / ride); rule-based generator with SHUFFLE / KEEP; SHUFFLE also picks from each voice's pool. See below | DRUMS | **v0 (Maestro / Melak v1.3)** |
| E17 | **DUCK:** per-lane depth / recovery and gate shape. The trigger follows **ROLLA's kick when DRUMS is on**, and the **grid / host when it is off** [Maestro]. **Per-dialect overrides** (format in §12.1) | all | DS; **per-lane, kick-follow, overrides v0 (Maestro)** |
| E18 | **Level trim** per preset (±12 dB; v1.3 targets = MIX_SPEC §2.2 / §2.3 via `mix_profiles.json`) + the **harness** (`tests/mix_check.py`: LUFS-S, true peak, band %, centroid, corr, duck, tail, swing timing, oscillator count) | all | DS + v0 additions |
| E19 | **CPU budget: < 10% of one modern core at 48 kHz with all 7 lanes on** [Maestro]. The per-lane budget is in §12.2, now including samplers (estimate ≈ 9.2%) | all | **v0 (Maestro)**; measure on the first build |
| E20 | **Per-lane sampler** (full requirements in §12.3) | all | **v0 (Melak v1.3)** |
| E21 | **MAIN auto-trim:** fixed lookup by dialect × active-MAIN-lane mask from `main_sum_trim.json`; 50 ms raised-cosine ramp in dB on change; lane-off ramp starts when the lane's output is −40 dB down (max 500 ms); no user control; bypassed for solo renders (§0.5.1) | MAIN out | **v0 (Maestro v1.3a)** |
| E22 | **User sample slots:** 14 slots (6 lanes + 8 drum voices); drag-and-drop with copy into `Samples/USER/`; root auto-detect + edit; MIX_SPEC §4 level match (−24 LUFS, −6 dBTP cap) on load; SAMPLE / LAYER / off; per-session, survives dialect / preset changes (§12.3 S17) | all + DRUMS voices | **v0 (Maestro v1.3b; Melak asked for a slot per drum)** |

**E16 in detail (909 DRUMS lane):**
- **Kick (circuit model):**
  - sine-shaped VCO with a pitch env (+21–25 st, τ 25–30 ms);
  - amp env with independent −6 / −20 / −40 dB shaping (K1 60 / 195 / 360 ms, K2 60 / 110 / 150 ms);
  - a click (> 2 kHz noise / impulse, 5 ms, −15 to −8 dB);
  - TUNE 40–70 Hz with KEY FOLLOW (§9.2).
- **Clap (circuit model):** noise through a BP (1.2–1.6 kHz) with a **3–4 burst retrigger envelope**, spacing set in ms (8–15), plus a tail envelope.
- **Rim / ghost and toms:** resonant bridged-T models with a pitch env.
- **Hats and ride (synthesized, not samples):**
  - 6-square metallic oscillator banks, each square through a 4-pole HPF and a 2-pole LPF, plus white noise;
  - a **BANK MODE** switch: MEASURED / 808 (ratios and filters in §9.5);
  - an OH/CH choke; the ride has its own bank.
- **Sampled voices (v1.3):**
  - every voice plays its `file`, from `start`, repitched from `root` by TUNE / KEY FOLLOW;
  - DECAY drives the amp env on the sample;
  - the synth model is the optional layer;
  - the user can swap any voice's file from the `Samples/DRUMS/<VOICE>/` browser.
- **Per voice:** TUNE, DECAY, pitch env, LEVEL, PAN, and accent via velocity.
- **SWING** 50–67% on off-16ths of the selected voices.
- **Generator** (§9.6): fixed 4/4 kick and clap on 2 and 4, the kit's hat pattern, and a sparse rim / perc layer. SHUFFLE rerolls the sparse layer; KEEP locks the part.
- **Samples:** loaded from `~/Music/ROLLA/Samples/DRUMS/…`, as listed in `SAMPLE_MANIFEST.json`.

### 12.1 Per-dialect override format (draft for the engineer)
A preset loads first. Then the active dialect's override block (if any) is applied, before the TONE / PUNCH macros.

```json
{
  "dialectOverrides": {
    "GASKIN 90s": {
      "BASS": {
        "preset": "bass.long.round",
        "params": { "duck.depth_dB": 7, "duck.recovery_ms": 110, "voice.glide_ms": 0 },
        "reason": "[M] REF_J1/J2 duck 7.3 dB / 110 ms; octave jumps snap"
      },
      "DRUMS": { "preset": "drums.gaskin.kit", "params": { "swing.pct": 50 } }
    },
    "LONG": {
      "DRUMS": { "preset": "drums.ukmin.kit", "params": { "swing.pct": 51 } }
    }
  }
}
```

**Rules [R]:**
1. **Key** = dialect → lane. `preset` is the preset ID that lane loads for that dialect. It must match the §1.2 table, including borrows.
2. **`params`** lists only the parameters that change. Values are in engine units (provisional IDs until the schema arrives, §13). Every value is range-checked on load, and a load fails loudly if an ID is unknown.
3. **Order:** preset → dialect override → macros (TONE / PUNCH) → user edits.
4. The session stores **base preset ID + override ID**, not the merged values. Switching dialect re-applies the new block.
5. **A user edit to an overridden parameter wins** and marks the lane "modified". "Revert" restores preset + override.
6. **Allowed parameters in v0:** duck depth / recovery / shape, glide, swing, level trim, and filter cutoff ±1 oct. Anything broader needs a new preset, not an override.

### 12.2 CPU budget per lane (estimate; target < 10% of one modern core at 48 kHz, all 7 lanes on)
Assumptions [R]:
- 1 PolyBLEP oscillator voice ≈ 0.025% of one core at 48 kHz.
- Filters, envelopes and per-voice drive are folded into the per-oscillator figure (≈ 2×).
- Reverbs are **2 shared send buses** (plate + room/hall), not one per lane.

These figures are not measured (E19 / §11.1).

| Lane | Worst-case load | Budget (% core) |
|---|---|---|
| BASS | mono: 3 osc + sub, ladder 2× OS, drive | 0.5 |
| STAB | 4 notes × 2 saws × unison ≤ 5 with release overlap ≈ 48 osc | 1.3 |
| HITS | HOOVER 10 osc / FM 2-op × 5 notes, noise | 0.6 |
| PLUCK | poly with delay tails ≈ 54 osc | 1.4 |
| PAD | ≤ 40 osc (Maestro cap) + ensemble | 1.2 |
| VOX CUTS | 1–2 voices: 3 BP formants + noise | 0.2 |
| DRUMS | sampled voices (≤ 12 simultaneous, cubic interpolation) + optional synth layers (models + 2 × 6-square banks) | 0.7 |
| Samplers, synth lanes (v1.3) | ≤ 64 interpolated sample voices across BASS / STAB / HITS / PLUCK / VOX (8-point sinc when repitched > 2 st) + PAD granular (2 pads × 8 voices × 12 grains = 192 grains, windowed) + disk-stream thread | 1.4 |
| Shared FX | 2 reverbs 1.0 · lane delays 0.35 · ensembles 0.3 · meters / limiters 0.2 | 1.85 |
| **Total** | | **≈ 9.2% (headroom ≈ 0.8%)** |

**If the first build measures over 10%** [R]:
- cut PAD granular density from 12 to 8 grains (−0.3%);
- drop the PLUCK / STAB release overlap to 1 note;
- then halve unison on PLUCK delay tails;
- never cut the DRUMS or BASS budgets.

---

### 12.3 Sample engine requirements (v1.3, for the engineer)
| # | Requirement | Used by |
|---|---|---|
| S1 | **One sampler per lane** (BASS, STAB, HITS, PLUCK, PAD, VOX) **plus one per drum voice**. It runs in front of the lane's existing filter → amp env → drive → FX → DUCK → width chain, so every synth shaping block also works on samples. A **LAYER** mixer adds the lane's synth oscillators (`layerMix` = sample share) | all |
| S2 | **Multisample / key-zones:** a zone list per preset, each zone `{file, root, loKey, hiKey, loVel, hiVel}`. v0 presets use one zone (the `file`) stretched across the keyboard and repitched from `root`. Zones are ready for multi-root sets such as the 5 THM chord stabs (§10.5) | all |
| S3 | **Root key and pitch:** note → ratio from `root`; fine tune ±100 ct per preset. Interpolation: cubic for ±2 st, 8-point windowed sinc beyond that; anti-alias filtering when shifting up > 12 st. KEY FOLLOW / KICK TUNE (§9.2) drive drum repitch | all |
| S4 | **Start / end:** `start` in seconds plus an end point. Sample-start modulation by velocity / random 0–10 ms, for "start jitter" variation | all |
| S5 | **Loop and crossfade:** sustain loop `{start, end, xfade}` in frames; equal-power crossfade; loop while the key is held, then play to the end or release on the amp env | BASS ROUND, STAB SWELL, PADs |
| S6 | **Reverse:** per sample block (`reverse: true`), rendered into a reversed RAM copy at load. Used for reverse swells (A2 layer) | PAD, HITS, VOX |
| S7 | **Stretch / granular** (pads and atmos):<br>• grain player with a window inside the file, grain 20–200 ms, density 4–16, scan speed 0 (freeze) to 1, spray 0–50 ms, pitch from the key;<br>• plus a **time-stretch mode** (granular overlap-add, or a phase vocoder if CPU allows) to fit phrases / risers to the host tempo (125 / 128 / 132 / 142 / 145 → 130 BPM) | PAD, VOX phrases, risers |
| S8 | **Round-robin / pool:**<br>• each block's `pool` is a round-robin set. Modes: cycle / random-no-repeat / SHUFFLE-on-press;<br>• SHUFFLE re-picks from the pool and KEEP locks it (as for the drum generator);<br>• round-robin state is saved with the session | all (V3, S3, drums) |
| S9 | **Slice mode** (vocal phrases, piano / rave phrase chops):<br>• slices from `slices_s` (onset-detected, `librosa.onset_detect` with backtrack) or from the user's edits;<br>• slices map chromatically from C3 up, or play in generator order;<br>• each slice has a 2 ms fade-in and a 10 ms fade-out; slices obey the formant filter and amp env | VOX, HITS H2 / H3 |
| S10 | **Formant path for samples:** the v1.1 3-band formant filter (E5) can process a sample (vowel tables, morph, SIZE). This is the "vocal shot through formant" path | VOX, PAD (vocal pad) |
| S11 | **Streaming vs RAM:**<br>• files ≤ 2 s load fully into RAM as float32;<br>• longer files preload the first 65,536 frames (≈ 1.5 s at 44.1 kHz) and stream the rest from disk on a background thread with a 2-buffer read-ahead;<br>• reverse and granular blocks load fully | all |
| S12 | **Memory budget** (re-measured on the prepped set: 252 files, **240.3 MB on disk after the v1.3c re-prep, float32**; 230 at 44.1 kHz, 22 at 48 kHz; 15 mono):<br>• all in RAM as float32 = **240.4 MB** (PAD 128.2, HITS 39.0, VOX 34.3, STAB 12.3, PLUCK 11.2, DRUMS 8.5, BASS 6.8); trimming silence saved ≈ 48 MB against the raw set;<br>• hybrid S11 mode = **88.9 MB** (57 files streamed; the 5 reverse / granular sources load fully);<br>• **budget: ≤ 128 MB resident** with hybrid streaming | all |
| S13 | **Sample folder and paths:**<br>• root = **`~/Music/ROLLA/Samples/`** on macOS (overridable in Settings, stored in the user prefs, not in presets);<br>• subfolders `BASS`, `STAB`, `HITS`, `PLUCK`, `PAD`, `VOX` and `DRUMS/{KICK,CLAP,SNARE,RIM,CH,OH,RIDE,PERC}`;<br>• presets store **paths relative to the root** with forward slashes (e.g. `BASS/THMAG_Bass_One_Shot_4_Fm.wav`), never absolute paths;<br>• on load the engine resolves root + relative path. A missing file means SYNTH fallback plus a warning badge on the lane | all |
| S14 | **Sample-rate conversion:** files are 44.1 / 48 kHz float32. Resample to the host rate at load (polyphase, MIX_SPEC §4.1) or fold the rate into the S3 ratio. Frame positions in blocks are at the **file** rate | all |
| S15 | **Harness additions:** render SAMPLE / LAYER presets through the same LUFS-S / peak / band / centroid / corr / duck / tail gates, plus the §11 sample gates (pitch ±10 ct, loop / slice clicks, memory) | all |
| S16 | **Load-time prep = MIX_SPEC §4** on an in-memory copy, never writing the file: float32 → DC (mean + 10 Hz HPF) → lead trim (−54 dB re peak, ≥ −60 dBFS, 1 ms pre-roll, 0.5 ms fade-in) → tail trim < −60 dBFS + 12 ms fade-out → −24 LUFS integrated, TP cap −6 dBTP (logged) → then the lane chain (MIX_SPEC §3) and lane output trim. Layering: sample −3 dB + synth −3 dB before the chain (MIX §4.6). The staged files are already prepped, so this is a no-op on them; it matters for user-swapped files. If a user file's lead trim is non-zero, shift that block's positions by the trim | all |
| S17 | **User sample slots (v1.3b, Melak / Maestro): 14 slots** = one per lane (**BASS, STAB, HITS, PLUCK, PAD, VOX**) + one per 909 drum voice (**KICK, CLAP, SNARE, RIM, CH, OH, RIDE, PERC**).<br>• **Load:** drag-and-drop a file onto the lane strip or the drum-voice pad (or a file browser). WAV / AIFF, 16 / 24-bit or 32-bit float, 44.1–96 kHz, mono or stereo (more channels are folded to stereo); up to 60 s (longer files are refused with a message). The file is **copied** into `Samples/USER/<LANE>/` or `Samples/USER/DRUMS/<VOICE>/` (the name is kept; a numeric suffix avoids clashes), so the stored path stays relative (S13).<br>• **Root note:** auto-detected on drop. Tonal lanes use pYIN over the first 3 s (needs ≥ 25% voiced frames), then the filename note (e.g. `_Dm`, `_C#`), then C3. KICK uses the tail f0, so KICK TUNE / KEY FOLLOW (§9.2) work. Other drum voices are C3 (play unshifted). Shown and **editable** (note + cents).<br>• **Level match (feasible: it is the S16 load-time prep):** the in-memory copy goes through MIX_SPEC §4: DC removal, lead / tail trim and fades, **−24 LUFS integrated, −6 dBTP cap**. It then feeds the unchanged lane chain and lane output trim, so a user file lands at the §0.5 lane target like a factory one. Analysis runs on a background thread (≈ 5–50 ms for ≤ 60 s); the slot sounds once it's done and never plays un-prepped. The gain and cap flag are shown read-only. A user **slot gain ±12 dB** sits after the prep.<br>• **Mode:** **SAMPLE** (the slot replaces the preset's sample; the preset's filter / env / FX / DUCK / width still apply) or **LAYER** (the slot plus the preset's synth voice, `layerMix` default 0.5; drums: the v1.2 synth voice, default 0.75). **Off** = the preset's own sample block. For SAMPLE / LAYER the preset `sample.file` and `pool` are bypassed; SHUFFLE on that voice keeps start jitter only.<br>• **Edits:** start (auto = 0 after trim), reverse, one-shot / loop (loop points auto-found as in curation, with the crossfade editable). Lanes with granular or slice presets (PAD, VOX, HITS phrases) apply them to the slot file with auto slice points (onsets).<br>• **Persistence:** slots live in the **session state** (host chunk) and in user presets, not in factory presets. They **survive dialect and preset changes**, because the slot is per lane / voice. A missing file on reload → the preset's own sample + a warning (no silence).<br>• **Budget:** slot audio counts toward S12 (≤ 128 MB resident); files over 2 s stream (S11). | all 6 lanes + 8 drum voices |

## 13. presets.json conversion (placeholder; v1.3 per-preset format)
Waiting for the engineer's **parameter IDs, ranges/skews (NormalisableRange) and schema**. Once they arrive I'll write a converter that reads this spec's targets and emits presets.json, then re-render through the harness.

Planned mapping (field names provisional, not the schema). **v1.3: every lane preset carries Maestro's `sample` block.** Drum kits carry one block per voice.
```
{
  "schemaVersion": "TBD",
  "sampleRoot": "~/Music/ROLLA/Samples/",          // default; user-overridable, not stored per preset
  "presets": [
    {
      "id": "bass.ukmin.pluck",                     // PART.DIALECT.NAME
      "part": "BASS", "dialect": "UK MIN", "name": "PLUCK",
      "sample": {"mode": "LAYER", "file": "BASS/THMAG_Bass_One_Shot_4_Fm.wav", "root": "F1", "start": 0.0028,
                 "reverse": false, "loop": {"on": false, "start": 0, "end": 0, "xfade": 0},
                 "layerMix": 0.35, "pool": ["BASS/...", "..."]},
      "sampleExt": {"slices_s": [], "granular": null, "sampleLayers": []},   // proposed extensions (§0.8)
      "chordMemory": null,                          // STAB presets only (§4), e.g. stab.ukmin.dub: {"on": true, "mode": "PARALLEL", "main": {"refKey": "A3", "intervals": [0,5,10,15]}, "lift": {"refKey": "G3", "intervals": [0,5,10,15]}, "liftSelect": "GENERATOR_BAR8", "foldAbove": "C6", "strum_ms": 0}
      // user sample slots are session / user-preset state, not factory-preset fields (§12.3 S17)
      "params": { "<paramId>": "<value in engine units>" },
      "levelTrim_dB": "<set by harness>",
      "targets": { "lufsS": "<from mix_profiles.json: dialects.<D>.lanes.BASS.st_target_lufs = -24.7>", "tp_dBTP": -8.9, "corrMin": 0.997 }
    },
    {
      "id": "drums.ukmin.kit", "part": "DRUMS", "dialect": "UK MIN", "name": "UK MIN KIT",
      "voices": { "kick": {"mode": "SAMPLE", "file": "DRUMS/KICK/CD_Kick_06.wav", "root": "A#1", "start": 0.0, "reverse": false,
                           "loop": {"on": false, "start": 0, "end": 0, "xfade": 0}, "layerMix": 1.0, "pool": ["DRUMS/KICK/..."]},
                  "clap": {"...": "..."} },
      "params": { "<paramId>": "<value>" }
    }
  ]
}
```
- **Source of truth:** `plugin-design/samples/_tools/presets_samples.json` holds the 18 lane blocks and the 2 kit blocks in this shape. The converter will merge them with `params` once the engineer's IDs arrive.
- **Unit conversions to confirm:**
  - Hz → normalised (log skew);
  - ms → seconds;
  - dB → linear gain;
  - "st" env amount → engine units (semitones or octaves);
  - τ → ADSR, if the engine has no τ mode (D ≈ 2.3·τ to 10%, or 4.6·τ to −40 dB).
- **[R] values tagged "Must measure"** become harness assertions.

---

## 14. Open questions
Resolved in v1.2 (Maestro):
- drum generator;
- DUCK follow;
- hats and ride synthesized (v1.3: optional layer under sampled voices);
- kick 40–70 Hz with KEY FOLLOW;
- nearest borrow;
- chord memory / keyswitches;

Resolved in v1.3b (Maestro):
- **Q3 chord memory:** on the STAB presets (DUB / SWELL / CLICK) with main + lift sets (§4, E10). HITS are one-shot hits with no chord memory (§5).
- **User sample slots:** 14, one per lane and one per drum voice (§12.3 S17, E22).
- **Q10 GASKIN hats:** ship in v0 tagged [LOW-CONF]; A&R re-check after the first build (§9.5, §11.1 item 8).

- dialect overrides;
- CPU budget.

1. **DS §14.1** ("ROLLA never outputs a kick") still needs editing in DESIGN.md to match the v0 DRUMS lane.
2. **Kick in D.** Should KEY FOLLOW be allowed to pick the tritone (Melak's own 52.5 Hz kick in Direction A), or keep the root / 4th / 5th rule?
3. ~~"Chord memory for STAB."~~ **Resolved v1.3b (Maestro):** STAB presets carry chord memory (§4); HITS have none (§5).
4. **`layerMix` meaning** (Maestro's block): I've used it as the sample share (1 = sample only). Confirm, or flip it.
5. **Units in the block:** `start` in seconds and loop points in frames (at the file rate) are my reading. Confirm, or switch to normalised 0–1.
6. **Extensions** outside the block: `slices_s`, `granular` and `sampleLayers` (§0.8). Accept them into the format, or carry them in a separate `sampleExt` object?
7. **Gain:** resolved by MIX_SPEC §4: every staged file is prepped to −24 LUFS, and the per-file gain is recorded in `SAMPLE_MANIFEST.json` (`files[].prep.gain_dB`, range −18.1 to +22.1 dB). Maestro's block needs no gain field unless Maestro wants it visible per preset (`prepGain_dB` is already alongside the block in `presets_samples.json`).
8. **Tempo-stamped sources:** the phrases and pads are at 125 / 128 / 132 / 142 / 145 BPM against ROLLA's 130. Should time-stretch follow the host tempo (S7)?
9. **Root confidence:** 385 of 1438 catalogue files have chroma-only roots. All 18 lane primaries have a measured or filename root, but the harness pitch gate (§11) should confirm them.
10. ~~GASKIN hats.~~ **Resolved v1.3b (Maestro):** ship as v0, tagged [LOW-CONF], with an A&R re-check after the first build (§9.5, §11.1 item 8).
11. **Default lane states other than HITS.** Should PAD and VOX CUTS sound from the first ROLL, or wait for the generator / arrangement to place them?
12. **Melak's brief vs HITS.** A&R should hear GASKIN 90s's default with ORGAN HIT on.
13. ~~All lanes on.~~ **Resolved v1.3a (Maestro):** MAIN auto-trim yes; fixed, data-driven, no user knob. Keyed by dialect × active-lane mask, because the spread within a lane count (1.25 dB) exceeds the tolerance (§0.5.1, E21, `main_sum_trim.json`).
14. **Which files go to the Mac.** All block positions now address the **prepped** staged files. `STAGE_MAP.tsv` still maps the raw originals (provenance, Maestro's format); staging from it would put unprepped files on the Mac, and positions would be off by each file's lead trim (up to 5.8 s on `VOX/THMN_Female_Vox_Lover_Amin_145_Lead_DRY.wav`). Proposal: ship the prepped `plugin-design/samples/<LANE>/` tree (240 MB) as the Samples root, and keep STAGE_MAP as the source record.
15. ~~Tail fades longer than 12 ms.~~ **Resolved v1.3a (Maestro):** the longer fades (24–192 ms) on the 71 files are accepted. **Spot-check for the Mix Engineer** (by ear and measurement), chosen for the longest fades and a spread across lanes: **v1.3c:** after the spot-check, 4 files were re-prepped with an explicit end and fade so no musical material sits in the fade: JC_Keys_MPC_123_A_Minor (ends before the next chop, 15 ms), TAT_SFX_05 (fade after the peak, 60.8 ms raised-cosine²), DF_Perc_09 (kept whole, 12 ms), CCH_Perc_07 (48 ms). See the v1.3c changelog.
    - `HITS/JC_Keys_MPC_123_A_Minor.wav`: 192 ms (H2 pool)
    - `HITS/TAT_SFX_07.wav`: 96 ms (H3 pool)
    - `PAD/JF_128_Fm_Phased_Funky_Pad_1_Pad.wav`: 96 ms (A1 primary, A3 pool)
    - `PAD/THMN_Sweep_Up_1.wav`: 96 ms (§10.5 RISER)
    - `STAB/TAT_SFX_05.wav`: 96 ms (S3 pool)
    - `BASS/VER_CleanSub_Bass_C.wav`: 96 ms (B3 pool)
    - `DRUMS/PERC/CCH_Perc_07.wav`: 96 ms (K1/K2 perc_tom pool)
    - `DRUMS/RIM/DF_Perc_09.wav`: 96 ms (K1 ghost pool)
    - `DRUMS/RIDE/PHR_Ride_3.wav`: 48 ms (K2 ride primary)
    - `PLUCK/THMN_Instruments_Square_Pluck_Csmin_142.wav`: 48 ms (library; last 10 ms −60.5 dBFS, the closest to the limit)
16. ~~TP-capped sources.~~ **Resolved v1.3a (Maestro):** the 25 capped files stay as they are, with no per-file limiter. The lane strip (MIX_SPEC §3 comp / saturation / output trim) handles peaks, and the lane trim makes up the level.

## Changelog
- **v1.3c — 2026-10-17**: Mix Engineer review of `main_sum_trim.json` and the fade spot-check.
  - `default_trim_db` renamed **`default_model_residual_db`**, marked informational only and never applied by the engine (§0.5.1, `main_sum_trim.json`, `_tools/main_sum_trim.py`).
  - **MAIN auto-trim recalibrated by lane-set class** (`tests/targets.json` combo_model): UK MIN drums-only +2.46 → **+1.94**. The cause was one full-mix calibration (1.03) applied to a drums-only sum, which over-reads by only 0.43. All 256 entries now equal the Mix Engineer's measured-calibration trims. Shifts vs v1.3a: UK MIN 49 of 64 masks change (−0.52 … +0.40; only drums-only beyond 0.5), LONG 49 (−0.23 … +0.37), POP 49 (−0.28 … +0.21), GASKIN 34 (−0.07 … +0.23); default masks stay 0.00. §0.5.1 table regenerated.
  - **4 samples re-prepped** with explicit end / fade (`samples/_tools/reprep_fix.py`; previous versions in `samples/_backup_v1.3c/`): `HITS/JC_Keys_MPC_123_A_Minor.wav` (ends before the next chop, 15 ms fade), `STAB/TAT_SFX_05.wav` (fade starts after the peak, 60.8 ms raised-cosine²), `DRUMS/RIM/DF_Perc_09.wav` (kept whole, 12 ms fade after the source gate), `DRUMS/PERC/CCH_Perc_07.wav` (48 ms fade after the decay). All pass the MIX §4 sample checks. SAMPLE_MANIFEST, prep_log and the new `samples/CHECKSUMS.sha256` (sha256 for all 252 files) updated.
  - The −5 dBTP peak-cap / ST-floor conflict (87 combos) is left open for Maestro.
- **v1.3b — 2026-10-17**: Maestro answered the three questions left from v1.2.
  - **Chord memory on STAB** (Q3): DUB / SWELL / CLICK STAB each get a `chordMemory` block (main + lift, PARALLEL, generator bar-8 lift, fold above C6, strum 0), matching QA A3–D4–G4–C5 / QB G3–C4–F4–B♭4 from `sketch_v1/PATCHES.md` (§4, S1–S3 voicing rows, §13, E10). **HITS** (ORGAN HIT, PIANO HIT, HOOVER) are one-shot hits with no chord memory; every HITS chord-memory reference is removed (§5, §2.1 H1, §10.2). The A3 PAD transform now takes its B♭/C re-voicing from the generator's notes (§2.1).
  - **User sample slots:** 14, one per lane (6) and one per 909 voice (8): drag-and-drop, root auto-detect + edit, MIX_SPEC §4 level match (−24 LUFS, −6 dBTP cap), SAMPLE / LAYER / off (§12.3 **S17**, **E22**, §11, §11.1 item 10).
  - **GASKIN hat ratios** (Q10) ship in v0 tagged [LOW-CONF]; A&R re-checks them after the first build, with BANK MODE 808 as the fallback (§9.4, §9.5, §11, §11.1 item 8). Chord-memory render test added (§11.1 item 9).
- **v1.3a — 2026-10-17**: Maestro closed Q13, Q15 and Q16.
  - **Q13:** MAIN auto-trim added (§0.5.1, E21, `presets/main_sum_trim.json`): a fixed sum trim by dialect × active-MAIN-lane mask, from the MIX loudness model; 0 dB at each dialect default; range −0.6 to +3.4 dB; 50 ms raised-cosine ramp; no user knob. §11 SUM gate extended to any lane set.
  - **Q15:** the long tail fades are accepted; 10 files listed for the Mix Engineer's spot-check.
  - **Q16:** the TP-capped files are accepted with no per-file limiter.
- **v1.3 — 2026-10-17**: every lane is sample-driven from Melak's own packs (Melak: personal use, "get creative with our bank of resources"). The earlier sample-policy wording is removed everywhere.
  - Catalogue of 1438 soundbank files [M-bank] (`samples/CATALOG.csv` / `.md`).
  - Curated **252 files** (217.1 MB raw; 240.4 MB after the float32 prep): BASS 24, STAB 26, HITS 24, PLUCK 22, PAD 30, VOX 26, DRUMS 100. Copied into `samples/<LANE>/` with `STAGE_MAP.tsv` and `SAMPLE_MANIFEST.json`.
  - Maestro's sample block on every preset (§0.8, §2.1, and a row in each preset table). Kits use samples as the primary voices, with the synth as an optional layer (§9, §9.8).
  - Sample engine requirements S1–S15 (§12.3), E20, sampler CPU (≈ 9.2% total), and the presets.json per-preset format (§13).
  - Post-v0 extra presets from the packs (§10.5). Sample gates in §11. New open questions 4–9.
  - All measured targets, mix gates and the v1.1 / v1.2 decisions are unchanged, **except loudness**, below.
  - **Loudness re-targeted to MIX_SPEC v1 §2.2 / §2.3** (Mix Engineer, via Maestro): kick −23.7, bass −24.7 LUFS-S in every dialect, top lanes per dialect, HITS bass −3; the old bass −18 / buses −15 anchor retired (≈ 6.7 dB too hot). Per-preset v1.2 offsets dropped (level keys locked). The per-preset −6 dBFS peak rule is replaced by the MIX lane TP caps; −6 dBTP remains for the summed output and for sample prep. Bus targets are report-only; the sum −19.0 ±0.5 LUFS-S / −6 ±1 dBTP governs (§0.5, §9, §10, §11, §13).
  - **Samples prepped per MIX_SPEC §4** on the staged copies (originals untouched): float32, DC, lead / tail trim and fades, −24 LUFS, −6 dBTP cap. 252 / 252 pass the sample checks. Per-file gain in `SAMPLE_MANIFEST.json`; block positions shifted to the prepped files; one bad STAB loop (`STAB/JF_Synth_12_C.wav`, old loop sat below −60 dBFS) re-found. Staged set now 240.4 MB float32 (S12, S14, S16). Open questions 13–16.
- **v1.2 — 2026-10-17**: DRUMS grounded in measured one-shots, and Maestro's answers applied.
  - §9 rewritten with distributions from 1104 of Melak's one-shots [M-melak] and 27 CC0 909 one-shots [M-909free], compared with REF_R5 [M-ref]. It adds reference-sample picks per kit, new K1 / K2 targets, and hat / ride oscillator-bank ratios with an 808 comparison.
  - The drum-sample policy at the time was recorded; it was superseded in v1.3.
  - Generator SHUFFLE / KEEP; duck follows ROLLA's kick or the host grid; synthesized hats and ride (E16; v1.3 makes them an optional layer under the samples).
  - Nearest-preset borrow confirmed. Chord memory kept (E10); vowel keyswitches moved to post-v0 (§10.4).
  - Override format drafted (§12.1). CPU < 10% budget per lane (§12.2).
  - Drum gates added to §11. Open questions trimmed.
- **v1.1 — 2026-10-17**: Maestro's decisions applied.
  - HITS is a normal lane: muted by default in UK MIN / LONG / POP (POP is my choice, justified in §1.1), on in GASKIN 90s.
  - v0 = 3 presets per part, plus a dialect → default-preset table (§1.2).
  - CLICK (POP) is a main BASS preset. ROUND is kept, ORGAN moves to post-v0 (§2, §10). The HITS extras and the DS BASS variants also move to post-v0.
  - Two outputs: MAIN and DRUMS. MAIN full-stack target −15 LUFS-S, peak ≤ −3 dBFS; per-preset −6 dBFS kept (§0.5). *(Superseded in v1.3 by MIX_SPEC §2.2.)*
  - Unison capped at 5. HOOVER and RAVE SAW re-voiced. PAD re-voiced to a worst case of 8 × 5 = 40 oscillators, with width moved to ensemble + M/S (§7.1).
  - H1 re-voiced to 3 oscillators and H2 to a single FM modulator.
  - 909 DRUMS lane added, with UK MIN and GASKIN kits, kick tuning, and swing per kit and per dialect (§9).
  - Engine requirements updated (E1–E19).
  - The +10–20 Hz ladder cutoff note is now an item to measure after render (§11.1).
- **v1 — 2026-10-17**: first spec. 18 presets + 2 HITS extras, loudness/headroom rule, mix gates, engine requirements, presets.json placeholder.
