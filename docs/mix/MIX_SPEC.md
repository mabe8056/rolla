# ROLLA MIX_SPEC v1: baked lane processing, loudness targets, sample prep, render test

Owner: Mix Engineer. Written Sat 17 Oct 2026, about 12:10 PM ET, for Maestro on Melak's behalf.
Inputs: the 8 reference artists (9 tracks: Local Dub *Reaction* and *V-Line/Cruiza*, Stef Davidse, Laidlaw, LIGHTLEAK, M-High, Josh Baker, Gaskin, Eliyajaya/Ellia), plus transcriptions/CROSS_TRACK_RULES.md, presets/PRESET_SPEC.md and AR_SOUND_REVIEW.md.
Test: `tests/mix_check.py` with `tests/targets.json`. Results are in `tests/results/*.json`.

**Rules this spec keeps.**
- Every number is either measured or tagged with its source.
- Reference audio was used **for measurement only**. No reference audio, slice or stem is in plugin-design or the plugin; only numbers are.
- Processing is **non-destructive**. The chain runs on an in-memory copy at load time or render time, and the original sample file is never modified.

**Source tags**
- **[M]** = measured on the reference stems by me for this spec. Shown as median (min–max) over n tracks; n = 9 unless stated.
- **[M-melak] / [M-ref]** = measured one-shot data quoted from PRESET_SPEC §9.
- **[A&R]** = AR_SOUND_REVIEW target.
- **[CTR]** = CROSS_TRACK_RULES.
- **[D]** = derived by stated arithmetic from [M] data.
- **[N]** = genre-norm fallback: not measurable from the refs. Each one is backed by a test window that *is* measured.

---

## 0. Method and confidence (reference measurement)

**Window.** Each ref was measured over the ~29.5 s drop block that matches its `drums.mid` (Reaction: my drop block 251.04–280.54 s).

**Separation.** htdemucs stems (drums/bass/other/vocals). MIDI was aligned by cross-correlating kick notes against the drums-stem onset envelope below 150 Hz. Reaction has no drums.mid, so its kicks come from a beat grid snapped ±30 ms to the onset maximum, claps from beats 2/4, and hats from HP 6 kHz onsets.

**Scripts** (scratch, not shipped): `/workspace/mixeng-scratch/tools/elements.py`, `agg.py`, `style_agg.py`, `tilt.py`, `mixrel.py`, `make_targets.py`. Data is in `/workspace/mixeng-scratch/{elem,data}/`. All jobs ran one at a time with OMP_NUM_THREADS=2; available memory stayed at 1.6–1.9 GB throughout.

| Element | Isolation | Confidence | Caveat |
|---|---|---|---|
| kick | drums stem, LP 200 Hz | HIGH | Bass bleed is minimal (demucs). The kick *attack* figure is not usable, because the 20 ms smoothing that kick and bass need for decay inflates it; kick attack is taken from [M-melak]. |
| bass | bass stem | HIGH | Deep sidechain dips are partly a demucs artefact. |
| stab (= "other") | other stem | MED | Contains stab + pad + pluck/lead together. It is used as the **STAB/PLUCK budget**. |
| clap | drums stem, HP 500 Hz, gated −5/+200 ms around note 39 | MED | Hat bleed raises the centroid, so clap centroid is not used as a target. |
| hats | drums stem, HP 5 kHz (CH/OH shapes from notes 42/46) | MED-HIGH | CH and OH are summed for loudness. OH has n = 4–5 clean hits. |
| perc | drums stem, BP 250 Hz–5 kHz, with kick/clap/hat windows gated out | LOW | Residual. |
| vox | vocals stem; counted absent if more than 25 dB under kick | MED | Absent on M-High and Gaskin (n = 7). |
| pad, hits, snare, rim, ride | not isolable in these drops | n/a | Fallbacks: pad = PRESET_SPEC; hits = A&R; snare/rim/ride = PRESET_SPEC §9 one-shot data. |

**Metrics.**
- ST LUFS = BS.1770 over 3 s windows at a 100 ms hop.
- TP = 4× oversampled peak. PLR = TP − ST max. Crest = sample peak − RMS of the active 10 ms frames.
- Width: S/M = side/mid energy; corr = L/R correlation. Mono point = the first 1/3-octave band (≥ −30 dB re the loudest band) with corr < 0.9.
- Shape (per hit, 1 ms envelope): attack 10→90 %, and decay from the peak to −20 dB.
- Dynamic range = p95 − p10 of the 10 ms frame levels.
- Tilt = least-squares slope of the 1/3-octave levels vs log2 f. Bands: 200 Hz–8 kHz for other/vocals, 1–16 kHz for drums.

## 1. Reference element measurements (all 9 tracks)

| Element | ST re kick dB | Crest dB | PLR dB | Centroid Hz | Sub+low % (<200 Hz) | S/M dB | Corr | Attack ms | Decay to −20 dB, ms | Dyn range dB |
|---|---|---|---|---|---|---|---|---|---|---|
| kick | 0 (abs −13.4, −14.6…−12.5) | 9.3 (7.5–10.5) | 10.0 (9.1–10.7) | 80 (75–86) | 99.5 (99.2–99.7) | −34.6 (−43.1…−19.2) | 1.00 (0.98–1.00) | n/a (see §0) | 166 (85–261) | 30.0 (23.2–34.4) |
| bass | −1.3 (−4.2…+1.9) | 12.5 (10.5–14.2) | 12.5 (9.5–13.8) | 89 (77–102) | 94.5 (87.7–99.3) | −30.0 (−44.9…−23.8) | ≥0.992 | 7.5 (0–23), n = 8 | 157 (52–251), n = 8 | 22.9 (11.5–29.8) |
| stab/other | −3.0 (−8.6…+1.6) | 20.6 (16.7–27.3) | 13.6 (10.2–17.7) | 674 (432–1204) | 12.5 (1.6–21.5) | −5.9 (−9.4…−0.8) | 0.59 (0.09–0.79) | 12.8 (7–28), n = 8 | 168 (98–254), n = 8 | 20.4 (15.0–31.6) |
| clap | −3.3 (−6.5…−2.0) | 21.4 (20.4–25.0) | 16.1 (13.8–18.3) | (bleed) | 0.0 | −10.6 (−21.3…−5.2) | 0.84 (0.54–0.99) | 2.3 (0–6), n = 8 | 58 (31–167), n = 8 | 27.2 (22.1–32.7) |
| hats (CH+OH) | −5.2 (−9.6…−1.9) | 21.6 (20.3–23.3) | 14.4 (10.8–16.6) | 8726 (8091–9459) | 0.0 | −8.2 (−23.1…−3.2) | 0.74 (0.35–0.99) | CH 1.5 (0–5.5), OH 1.0 (0–8.5) | CH 44 (24–149), n = 7; OH 144 (62–248), n = 4 | 27.2 (22.2–31.6) |
| perc (LOW) | −9.2 (−15.2…−6.1) | 22.2 (19.2–25.4) | 18.7 (16.7–22.5) | 1952 (1024–2937) | 0.4 | −15.3 (−20.3…−9.1) | 0.94 (0.78–0.98) | n/a | n/a | 29.0 (25.3–33.7) |
| vox (n = 7) | −3.2 (−13.2…+0.4) | 21.3 (17.0–22.8) | 13.0 (10.7–23.4) | 1606 (843–2805) | 0.0 (0–5.3) | −6.8 (−13.1…+0.9) | 0.65 (−0.10–0.91) | n/a | n/a | 28.6 (25.6–33.8) |

**Low end.**
- Bass mono point is ≥ 224 Hz on all 9 tracks; 6 of 9 stay mono to ≥ 1 kHz.
- Kick corr is ≥ 0.995 on 8 of 9 tracks. Reaction is 0.976, with S/M −19.2.

**Kick level vs the stem sum** (drop window, [M] `mixrel.py`):
- Kick ST max = mix ST max −4.7 dB, median (−6.6…−3.6).
- By style: UK MIN −4.9, LONG −4.7, POP −5.5, GASKIN −3.6.
- The refs are mastered: mix PLR 7.1–9.3.

**Mix tilt (stem sum, 100 Hz–10 kHz).** UK MIN −1.64, LONG −1.64, POP −1.65, GASKIN −1.66 dB/oct.
- Overall median −1.64 (−2.22…−1.18) [M].
- So the whole-mix tilt is the same across styles. Styles differ in how they spread it across lanes (below).

### 1.1 Per-style medians (UK MIN = Reaction, V-Line, Stef, Laidlaw; LONG = LIGHTLEAK; POP = M-High + Josh; GASKIN 90s = Gaskin; Ellia is an outlier and is excluded from the style rows)

| Style | Bass re kick | Stab re kick | Clap re kick | Hats re kick | Perc re kick | Vox re kick | Stab crest | Stab S/M | Stab tilt dB/oct | Hats tilt | Vox tilt | Bass sub+low % | Kick crest |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| UK MIN | +0.2 (−3.9…+1.9) | −3.2 | −2.5 | −4.3 | −7.6 | −5.0 | 21.2 | −5.2 | −4.4 | +0.2 | −0.2 | 94.9 | 9.6 |
| LONG | −0.1 | −8.6 | −2.0 | −1.9 | −8.4 | −3.2 | 27.3 | −3.2 | −5.2 | +0.9 | +2.7 | 93.3 | 10.4 |
| POP | −1.8 | −0.5 | −3.5 | −4.5 | −10.7 | +0.3 | 19.0 | −4.6 | −4.4 | +1.7 | −1.8 | 93.0 | 9.4 |
| GASKIN 90s | **−4.2** | −1.6 | −6.2 | −6.8 | −12.3 | absent | 16.8 | −9.4 | −5.5 | −1.6 | n/a | 99.0 | 7.5 |

**The refs disagree on the low end.** Bass re kick ranges from +0.2 (UK MIN) to −4.2 (GASKIN), a 4.4 dB spread. Per the brief, it is **clamped** (§2.1).

---

## 2. Loudness architecture (all lanes at unity fader)

### 2.1 Anchor and invariants

1. **Summed output target: −19.0 LUFS-S max ±0.5, true peak −6 dBTP ±1** [D].
   - Melak's pre-master bounces measured PLR 11.6 (sample_0916: TP −1.3 / ST max −12.9) and 14.2 (sketch bars 9–16: TP −0.2 / ST −14.4).
   - With a −6 dBTP peak, that gives an ST max between −17.6 and −20.2. The centre is −19.
2. **KICK = −23.7 LUFS-S max in every style** (render: a ≥ 6 s loop of the default kick pattern) [D].
   - A power-sum model of the UK MIN lane balance, calibrated against the measured stem sums, puts the sum 4.65 dB over the kick.
   - The model's calibration offset is 1.03 dB: the power sum of the lane ST maxima over-reads the real sum, because the lanes don't peak together. Per style it was 1.03 / 1.30 / 1.02 / 0.48.
   - −19.0 − 4.65 = −23.65, rounded to −23.7.
3. **BASS = KICK −1.0 dB (−24.7 LUFS-S) in every style, ±0.5** [D clamp].
   - −1.0 is the median of the 4 style medians (+0.18, −0.13, −1.77, −4.24) = −0.95.
   - This departs from the GASKIN ref by +3.2 dB and from UK MIN by −1.2 dB. Flagged as the clamp the brief asks for. **DECIDED (Maestro, 2026-10-17): keep the clamp in every style, GASKIN 90s included, so the bus chain needs no re-tuning between styles.**
4. **Kick and bass must not differ by more than 1 dB across styles** in ST max, sub+low energy and <120 Hz energy. Both must hold corr <120 Hz ≥ 0.99.
   - Bass is mono to ≥ 220 Hz [M: mono point ≥ 224 Hz on 9/9].
   - Kick and bass presets may not change level, sub EQ or mono processing per style. Style colour on kick/bass is limited to crest (compression) and >200 Hz content.
5. **Style differences live in the TOP lanes** (STAB, PLUCK, CLAP, SNARE, RIM, HATS, RIDE, PERC, VOX, PAD).
   - Each top lane uses its style's measured level re kick.
   - One **per-style TOP trim** then holds the sum constant: **UK MIN 0.0, LONG −0.3, POP −1.8, GASKIN +1.0 dB** [D, `make_targets.py`].
   - HITS are pinned to bass −3 LU [A&R] and are not trimmed.
   - The predicted sum is identical across styles by construction; the render test (`styles` mode) verifies it.
6. **Per-style output trim** (MAIN out): 0 dB by design. It exists only to absorb a residual measured on the style test renders, kept within ±1 dB. The summed-ST spread must be ≤ 1.0 LU, and each style must be within −19 ±0.5.

### 2.2 Per-lane targets (UK MIN defaults; ST LUFS max of a solo loop render; TP max = ST + ref PLR max + 2 dB pre-master headroom [D])

| Lane / voice | ST LUFS max | Tol | TP max dBTP | Width (S/M dB) | Mono rule | Basis |
|---|---|---|---|---|---|---|
| KICK (DRUMS:KICK) | **−23.7** | ±0.5 | −11.0 | ≤ −19.2 | corr <120 ≥ 0.99, mono to ≥ 141 Hz | [D] anchor |
| BASS | **−24.7** | ±0.5 | −8.9 | ≤ −23.8 (effectively mono) | corr <120 ≥ 0.99, mono to 220 Hz | [D clamp] |
| STAB | −26.8 | ±1 | −7.1 | −11.4…+1.2 | <120 Hz energy ≤ −15 dB re total; corr <120 ≥ 0.9 | [M] UK MIN −3.15 re kick |
| PLUCK | −26.8 | ±1 | −7.1 | same as STAB | same as STAB | [D] shares the "other" budget. If STAB and PLUCK both play, each −3 dB. |
| DRUMS:CLAP | −26.2 | ±1 | −5.9 | −23.3…−3.2 | <120 ≤ −25 dB | [M] −2.5 re kick |
| DRUMS:SNARE | −26.2 | ±1 | −5.9 | as CLAP | as CLAP | [N] no snare in refs; takes the clap slot |
| DRUMS:RIM | −31.2 | ±1.5 | −6.7 | (none) | <120 ≤ −25 dB | [M-ref via PRESET_SPEC] ghost = clap −5 dB |
| DRUMS:OH | −28.3 | ±1.5 | −9.7 | −25.1…−1.2 | <120 ≤ −40 dB | [M] hats −4.33 re kick, split as OH 0 / CH "a" −12 / "e" −22 [M-ref] |
| DRUMS:CH | −40.3 | ±1.5 | −21.7 | −25.1…−1.2 | <120 ≤ −40 dB | [D] OH −12 |
| DRUMS:RIDE | −46.3 | ±1.5 | −27.7 | −25.1…−1.2 | <120 ≤ −40 dB | [PRESET_SPEC R] OH −18 |
| DRUMS:PERC | −31.3 | ±1.5 | −6.8 | −22.3…−7.1 | <120 ≤ −25 dB | [M LOW] −7.63 re kick |
| VOX | −28.7 | ±1 | −3.2 | −15.1…+2.9 | <120 ≤ −20 dB | [M] −4.98 re kick. The TP cap is loose because of Laidlaw's vox PLR of 23.4; the SUM TP check governs. |
| HITS | −27.7 | ±1 | −8.0 | −11.4…+1.2 | corr <120 ≥ 0.98 [A&R]; <120 ≤ −15 dB | [A&R] bass −3 LU |
| PAD | −32.7 | ±1.5 | −20.7 | −12…0 [N] | <120 ≤ −20 dB | [PRESET_SPEC D] bass −8 (−26 vs −18) |
| **SUM** | **−19.0** | ±0.5 | −5.0 (target −6 ±1) | (none) | corr <120 ≥ 0.99 | [D] |

**PRESET_SPEC conflict for Maestro.** PRESET_SPEC's absolute anchors cannot give a −6 dBTP summed peak at Melak's pre-master PLR:
- bass −18, MAIN/DRUMS buses −15 LUFS-S (sum ≈ −12.1), kick solo peak −6.
- Those numbers sum about 7 dB hotter than this spec.
- This spec **keeps PRESET_SPEC's relative ideas but rescales the absolute anchor** (BASS −18 → −24.7).
- The per-preset harness trims in PRESET_SPEC §5 should be re-targeted to §2.2/§2.3 here.

### 2.3 Style × lane summary (ST re kick dB · comp GR dB · saturation · EQ tilt dB/oct · width S/M dB)

How the cells are derived:
- **re kick** = style median [M] + TOP trim (kick 0 and bass −1.0 in every style).
- **GR** = base GR [N] − (style crest − UK MIN crest) [D: 1 dB less crest ≈ 1 dB more peak GR], floored at 0.
- **Tilt** = the measured style tilt of the stem the lane lives in. A tilt EQ pivots at 1 kHz; its setting is (style tilt − UK MIN tilt) on top of the lane EQ.
- **Width** = style S/M median [M]; tested at ±2.5 dB.
- **Saturation** is [N]. Its direction comes from measured proxies: bass >200 Hz share and stab centroid.

| Lane | UK MIN | LONG | POP | GASKIN 90s |
|---|---|---|---|---|
| KICK | 0 · 3 · soft-clip +3 dB · n/a · ≤ −28 | 0 · 2 · +3 · n/a · ≤ −31 | 0 · 3 · +3 · n/a · ≤ −38 | 0 · **5** (crest 7.5 vs 9.6) · +5 dB · n/a · ≤ −35 |
| BASS | −1.0 · 4 · tape +4 dB / 50 % · n/a · ≤ −27 | −1.0 · 4 · +4 / 50 % · n/a · ≤ −44 | −1.0 · 5 (crest 11.8 vs 13.2) · +5 / 50 % · n/a · ≤ −35 | −1.0 · 6 (11.0 vs 13.2) · +2 / 20 % (sub+low 99.0 %) · n/a · ≤ −34 |
| STAB / PLUCK | −3.1 · 3 · tube +3 / 30 % · −4.4 · −5.2 | −8.9 · 0 (crest 27.3) · +2 / 20 % · −5.2 (tilt EQ −0.9) · −3.2 | −2.3 · 5 (19.0) · +3 / 30 % · −4.4 (0) · −4.6 | −0.6 · 7 (16.8) · +5 / 40 % · −5.5 (−1.2) · −9.4 |
| CLAP / SNARE | −2.5 · 4 · soft-clip +2 · n/a · −10.0 | −2.3 · 5 (20.4 vs 21.3) · +2 · n/a · −9.4 | −5.3 · 2 (23.0) · +2 · n/a · −10.0 | −5.2 · 0 (25.0) · +1 · n/a · −10.6 |
| HATS OH (CH −12, RIDE −18) | −4.6 · 2 · none · +0.2 · −8.8 | −2.5 · 3 · none · +0.9 (+0.7) · −8.2 | −6.6 · 2 · none · +1.7 (+1.5) · −9.6 | −6.1 · 3 · tape +2 / 20 % · −1.6 (−1.8) · −8.0 |
| PERC | −7.6 · 2 · none · n/a · −12.1 | −8.7 · 4 (20.3 vs 22.5) · none · n/a · −19.4 | −12.5 · 2 · none · n/a · −12.3 | −11.2 · 0 (25.4) · none · n/a · −15.3 |
| VOX | −5.0 · 3 · tape +2 / 20 % · −0.2 · −8.7 | −3.5 · 4 · +2 / 20 % · +2.7 (+2.8) · +0.9 | −1.5 · 8 (17.0 vs 22.0) · +3 / 30 % · −1.8 (−1.6) · −11.6 | −4.0 [fallback UK MIN] · 3 · +2 / 20 % · n/a · (UK MIN) |
| RIM | −7.5 | −7.3 | −10.3 | −10.2 |
| PAD | −9.0 · 2 [N] | −9.3 | −10.8 | −8.0 |
| HITS | −4.0 (= bass −3) · 4 | −4.0 | −4.0 | −4.0 · 4 (default ON) |

**Default lanes per style:**
- UK MIN / LONG / POP: KICK, BASS, STAB, CLAP, HATS, PERC, VOX.
- GASKIN: the same, minus VOX (absent in the ref) and plus HITS.

---

## 3. Per-lane baked chains (order: prep → HPF/LPF → EQ → comp → saturation → width → output trim)

Compressor conventions:
- Feed-forward RMS detector with a 5 ms window. Threshold is in dBFS *after* sample prep (§4 normalises every source to −24 LUFS, active part).
- "Target GR" is the peak GR on the default pattern. The threshold value given is the start point, and the harness nudges it to land on that GR.
- Attack and release are grounded in the measured transient attack and decay-to-−20 dB, so the compressor passes the measured onset and recovers within the measured note body. Ratio and knee are [N].

### KICK (DRUMS:KICK; synth 909 circuit and/or sample)

| Stage | Setting | Ref median (range) / basis |
|---|---|---|
| HPF | 28 Hz, 24 dB/oct | [D] about half of kick f0 50.1–55.9 Hz [M-melak IQR]; keeps f0 |
| LPF | none on the body. Click level per PRESET_SPEC (UK −15 dB, GASKIN −8 dB re kick peak) | [M-melak] |
| EQ 1 | +1.5 dB @ 55 Hz, Q 1.0 | [D] f0 A1 default; centroid target 80 Hz (75–86) [M] |
| EQ 2 | −2 dB @ 250 Hz, Q 1.4 | [D] 200–800 Hz share ≤ 2 % (Reaction 0.5 %) [M] |
| Comp | thr −18 dBFS, 4:1, attack **15 ms**, release **160 ms**, knee 6 dB, target GR 3 dB (GASKIN 5) | attack: refs keep the body at +50 ms within 2.5 dB (−9.3…+0.2) of the hit peak [M], so the first 15 ms pass; release = decay to −20 dB, 166 ms (85–261) [M]; crest window 7.5–10.5 [M]; ratio/knee/GR [N] |
| Sat | soft-clip (tanh), drive +3 dB (GASKIN +5), mix 100 % | [N]. Test: crest 5.5–14.5 dB, PLR 9.1–14.7 |
| Width | mono (sum to M below 141 Hz; S/M ≤ −19.2) | [M] mono point 141 Hz to ≥ 1 kHz |
| Trim | → −23.7 LUFS-S | §2.1 |

### BASS

| Stage | Setting | Ref median (range) / basis |
|---|---|---|
| HPF | 30 Hz, 24 dB/oct | [D] below root D1 36.7 Hz |
| LPF | SUB/ROUND: 2.5 kHz, 12 dB/oct | [M] 800 Hz–5 kHz share ≤ 1.4 % ([CTR] ≤ 1.4 % above 400 Hz; Reaction 0.09 %) |
| EQ 1 | +1.5 dB @ 90 Hz, Q 0.9 | [M] centroid 89 Hz (77–102). The ROLLA render measures 62 Hz |
| EQ 2 | low shelf −2 dB @ 45 Hz | [M/A&R] the render has 72.6 % below 60 Hz; A&R target about 50 %, Reaction 53.4 % |
| Comp | thr −20 dBFS, 4:1, attack **8 ms**, release **150 ms**, knee 6 dB, target GR 4 dB (POP 5, GASKIN 6) | attack 7.5 ms (0–23) [M]; release = decay to −20 dB, 157 ms (52–251) [M], inside [CTR] recovery 102–228; crest 12.5 (10.5–14.2); dyn range 22.9 [M]; GR [N] |
| Sat | tape, drive +4 dB, mix 50 % (GASKIN +2 / 20 %) | [N]. Bounded by the test: mid ≤ 1.4 %, high ≤ 0.5 %; GASKIN ref sub+low 99.0 % vs UK 94.9 % [M] |
| Width | mono below 220 Hz (hard), S/M ≤ −23.8 full band | [M] mono point ≥ 224 Hz on 9/9; corr ≥ 0.992 |
| Trim | → −24.7 LUFS-S | §2.1 |

**Per-preset overrides**

| Preset | Changes | Basis |
|---|---|---|
| SUB / ROUND (B1/B3) | as above | n/a |
| REESE (sub layer + reese layer) | sub layer = BASS chain. Reese layer: HPF **220 Hz** 24 dB/oct; mono below 150 Hz (corr ≥ 0.98); comp 3:1, attack 10 ms, release 120 ms (1/16 at 126 BPM ≈ 119 ms); sat drive +6 dB / 40 %. Lane test: centroid 77–160 Hz, mid ≤ 6 % | [A&R] reese HPF 110→220, hoover low end mono; the rest [N] |
| CLICK (gate DUCK) | release 100 ms | [M] POP bass decay to −20 dB = 96 ms |
| GASKIN dialect | GR 6 dB, sat +2 dB / 20 %, release 160 ms | [M] Gaskin crest 11.0, dyn range 20.1, decay 163 ms |
| LONG dialect | release 210 ms | [M] LIGHTLEAK decay 214.5 ms |

### STAB (and PLUCK)

| Stage | Setting | Ref / basis |
|---|---|---|
| HPF | 120 Hz, 12 dB/oct | [D] keeps <120 Hz for kick/bass; ref <200 Hz share 12.5 % (1.6–21.5) [M] |
| LPF | 10 kHz, 12 dB/oct | [M] "other" tilt −5.0 dB/oct (−7.9…−0.7) |
| EQ 1 | +1.5 dB @ 1.2 kHz, Q 0.8 | [M] centroid 674 Hz (432–1204); the ROLLA stab render measures 581 Hz; [A&R] stab centroid about 1–1.5 kHz |
| EQ 2 | −1.5 dB @ 300 Hz, Q 1.0 | [D] the render has 84.7 % in 200–800 Hz |
| Comp | thr −22 dBFS, 3:1, attack **12 ms**, release **170 ms**, knee 6 dB, target GR 3 (LONG 0, POP 5, GASKIN 7) | attack 12.8 (7–28) [M]; release = decay to −20 dB, 168 (98–254) [M]; crest 20.6 (16.7–27.3) [M] |
| Sat | tube (asymmetric), +3 dB / 30 % | [N] |
| Width | S/M −5.9 (−9.4…−0.8), corr 0.59; mono below 120 Hz | [M]. [A&R] corr about 0.55 |
| Trim | → §2.2/2.3 | n/a |

**Stab variants**

| Variant | Changes | Basis |
|---|---|---|
| DUB / LONG stab | release 110 ms | [M] LIGHTLEAK decay 106 ms |
| CHORD / UK stab | release 180 ms | [M] V-Line 180.5 ms, Ellia 180 ms |
| GASKIN rave stab | release 250 ms, GR 7 | [M] Gaskin decay 254 ms, crest 16.8, dyn range 15.0 |
| POP / CLICK stab | release 125 ms, GR 5 | [M] POP decay 126 ms, crest 19.0 |
| [A&R] tail rule | −10 dB at about 850 ms | Applies to the long chord stab |

PLUCK uses the STAB chain with release 110 ms [M: shortest "other" decay group].

### DRUMS voices (909 synth and/or sample)

| Voice | HPF / LPF | EQ | Comp (thr / ratio / attack / release / knee / GR) | Sat | Width | Basis |
|---|---|---|---|---|---|---|
| CLAP | 200 Hz 24 dB/oct; LPF 14 kHz 12 dB/oct | +2 dB @ 1.3 kHz Q 1.2 (GASKIN 1.5 kHz); −2 dB @ 400 Hz Q 1.5 | −20 dBFS / 4:1 / **3 ms** / **60 ms** / 3 dB / 4 | soft-clip +2 dB | S/M −10.6 | attack 2.3 (0–6), decay 58 (31–167) [M]; spectral peak 1195 / 1594 [M-melak]; HPF [M] <200 Hz = 0.0 % |
| SNARE | 150 Hz 24; LPF 14 kHz | +2 dB @ 200 Hz Q 1.2 | −20 / 4:1 / 3 / 80 / 3 / 4 | soft-clip +2 | as clap | [N] |
| RIM | 300 Hz 24 | +2 dB @ 1760 Hz Q 2 | −22 / 3:1 / 2 / **150** / 3 / 2 | none | mono-ish | [M-ref] ghost 1850 Hz, −20 dB @ 148 ms |
| CH | 2 kHz 24 (bank HP) | high shelf +1.5 dB @ 10 kHz; −2 dB @ 4 kHz Q 2 | −24 / 3:1 / **2 ms** / **45 ms** / 3 / 2 | none (GASKIN tape +2 / 20 %) | S/M −8.2 | attack 1.5 (0–5.5), decay 44 (24–149) [M]; centroid 8726 (8091–9459) [M] |
| OH | 2 kHz 24 | as CH | −24 / 3:1 / **2 ms** / **145 ms** / 3 / 2 | as CH | S/M −8.2 | attack 1.0 (0–8.5), decay 144 (62–248) [M] (2 ms floor [N]) |
| RIDE | 2 kHz 24 | high shelf +1 dB @ 8 kHz | −24 / 2:1 / 2 / **175** / 3 / 1 | none | S/M −8.2 | [M-melak] ride −20 dB @ 174 ms |
| PERC | 250 Hz 24; LPF 12 kHz | +1.5 dB @ 2 kHz Q 1 | −22 / 3:1 / 3 / **75** / 3 / 2 | none | S/M −15.3 | centroid 1952 (1024–2937) [M LOW]; [M-melak] perc −20 dB @ 73 ms; attack [N] |
| KICK | see KICK | n/a | n/a | n/a | n/a | n/a |

### VOX

| Stage | Setting | Basis |
|---|---|---|
| HPF / LPF | 150 Hz 24 dB/oct / 12 kHz 12 dB/oct | [M] vox <200 Hz share 0.0 % (0–5.3); LPF [N] |
| EQ 1 / 2 | −2.5 dB @ 300 Hz Q 1.2; +2 dB @ 3 kHz Q 1.0 | [M] centroid 1606 (843–2805); the renders measure 659 Hz (stem_vox) and 1054 Hz (demo_vox_cuts) |
| Comp | thr −22 dBFS, 3:1, attack 5 ms, release 80 ms, knee 6 dB, target GR **3** (POP 8) | [N] attack/release; GR set by crest 21.3 (17.0–22.8) [M]. stem_vox crest 13.4 = over-compressed |
| Sat | tape +2 dB / 20 % | [N] |
| Width | S/M −6.8 (−13.1…+0.9): needs a stereo send/doubler. The renders are mono (−28.8 / −41.4) | [M] |

### HITS (no rave hits in the refs)

| Stage | Setting | Basis |
|---|---|---|
| HPF | 220 Hz 24 dB/oct | [A&R] |
| EQ | −2 dB @ 500 Hz Q 1 | [D] the hit renders sit at 50–71 % in 200–800 Hz |
| Comp | thr −20 dBFS, 4:1, attack 11 ms, release 250 ms, knee 6 dB, GR 4 | [M borrowed] Gaskin stab attack 11 ms, decay 254 ms; GR [N] |
| Sat | +4 dB / 40 % | [N] |
| Width | mono below 150 Hz (corr <120 ≥ 0.98) | [A&R] |
| Level | bass −3 LU = −27.7 LUFS-S, measured as LUFS, not RMS | [A&R] |

### PAD (no clean pad section in the drop windows)

| Stage | Setting | Basis |
|---|---|---|
| HPF / LPF | 150 Hz 12 dB/oct / 8 kHz 12 dB/oct | [N] |
| Comp | 2:1, attack 30 ms, release 300 ms, GR 2 | [N] |
| Width | S/M −12…0 | [N] |
| Level | −32.7 | [PRESET_SPEC D: bass −8] |

---

## 4. Sample prep on load (non-destructive: applied to an in-memory copy; the file on disk is never written)

1. **Format.** Convert to float32 at the engine rate (polyphase resample). Mono stays mono until the width stage.
2. **DC removal.** Subtract the mean, then a 2nd-order Butterworth HPF at 10 Hz, zero-phase (offline). [D] This is below the lowest measured kick f0 (47.3 Hz [M-melak] min) and the bass root D1 36.7 Hz. Test: DC ≤ −60 dBFS.
3. **Leading-silence trim.**
   - Threshold **−54 dB re the sample peak** (= −60 dBFS once the peak is at −6).
   - Keep a **1 ms pre-roll**, with a 0.5 ms raised-cosine fade-in on the pre-roll only. [D] The measured attacks are clap 0–6 ms and CH 0–5.5 ms, so the onset is preserved.
   - Test: lead silence ≤ 2 ms at −60 dBFS.
4. **End handling.**
   - Trim trailing material below −60 dBFS (re the normalised level).
   - Then apply a **12 ms raised-cosine (Hann half-window) fade-out**. [A&R: 10–15 ms fade; stop ≥ attack + 7τ.]
   - Test: last 10 ms ≤ −60 dBFS on the prepped sample, and ≤ −45 dBFS on renders [A&R].
5. **Gain normalisation.**
   - Measure = **BS.1770 integrated loudness of the active part**: K-weighted, absolute gate −70 LUFS, relative gate −10 LU (pyloudnorm / libebur128 equivalent).
   - Normalise to **−24 LUFS**, then cap so the true peak is ≤ −6 dBTP (4× oversampled). If the cap bites, it is logged.
   - The −24 reference is internal [N]. It makes the compressor thresholds in §3 mean the same thing for every sample. The lane's output trim then lands the §2.2 target.
   - Kick, bass and drum one-shots shorter than 400 ms are measured over the whole file (the BS.1770 400 ms block minimum is padded).
6. **Layering (synth + sample).** Both sources are prepped and summed (sample at −3 dB, synth at −3 dB by default) *before* the lane chain. The summed lane is what is tested.
7. **Accepted exceptions (PRESET_SPEC v1.3a, Maestro).** <!-- v1.3a-s4 -->
   - **Q16: TP-capped files stay as they are.** No per-file limiter. 25 files are capped at −6.1 dBTP (the prep leaves 0.1 dB margin), and their LUFS-I sits at −24.0 … −42.4 (quietest: PAD/CD_SFX_Vinyl_Texture −42.4, PAD/JF_128_In_The_City_Background −34.4). The test passes a file at the cap when LUFS-I ≤ −23.9 and TP ≤ −5.95. Being at the cap is never a fail on its own. Two more files sit at the cap without being capped (DRUMS/CLAP/AR_Clap_09 −6.10, PLUCK/AR_Synth_31_C −6.12), both at −24.0, and they pass too. The lane chain's output trim lands the level. A capped file plays quieter than −24 into the compressor, so its GR is lower than §3 assumes. That is accepted.
   - **Q15: tail fades longer than 12 ms are accepted.** 71 files use 24 ms (27), 48 ms (25), 96 ms (18) or 192 ms (1), because 12 ms could not bring the last 10 ms to ≤ −60 dBFS. The measured spot-check of 10 of them is in §6.3.
   - **Mono files are measured mono.** This uses BS.1770 single-channel weight 1.0, the same as `prep.py`; 15 files are mono. If the engine plays a mono file to both outputs at unity, the stereo out reads +3.01 LU. Lane trims are set from stereo renders, so that offset is absorbed there. The engine must use one pan law for every lane.

---

## 5. Automated render test: `tests/mix_check.py` + `tests/targets.json`

Run with the scratch venv, or with any Python that has numpy, scipy, soundfile and pyloudnorm:

```
mix_check.py render <wav> --lane LANE [--voice VOICE] [--style UKMIN|LONG|POP|GASKIN] [--kind loop|oneshot] [--json out.json]
mix_check.py sample <wav> --lane LANE [--voice VOICE]      # prepped one-shot: DC, lead silence, tail, TP, normalisation
mix_check.py styles <dir>    # <dir>/<STYLE>/KICK.wav, BASS.wav, SUM.wav (+ optional STAB.wav, VOX.wav, DRUMS_OH.wav …)
mix_check.py scan   <dir>    # render-check every wav, lane guessed from the path
mix_check.py mute <render.wav> (--full F --stem S | --expected E | --silent) [--lane-ref R ...]   # P0 mute-leak test (§5.1)
mix_check.py stimulus <out.wav> [--seconds 8]                     # synthetic switch stimulus (§9.3)
mix_check.py switch <wav> --switch-at S [--ramp-ms 50] [--dialect FROM>TO]   # dialect-switch click / gain-jump test (§9.3)
mix_check.py combos [<dir>] [--dialect D] [--trim-table presets/main_sum_trim.json] [--json out.json]
#   verify the MAIN auto-trim table (§9.4); with <dir>, also gate <dir>/<D>/COMBO_<mask>.wav renders (SUM gate, any lane set)
# --dialect is an alias of --style: UKMIN | LONG | POP | GASKIN (also accepts "UK MIN", UK_MIN, "GASKIN 90s")
python3 validate_profiles.py   # mix_profiles.json parses, equals the block embedded in §8, and matches targets.json and the §2.2/§2.3/§3 tables
```

- Output is JSON (`pass`, per-check value, range and why), and the exit code is 0 for pass, 1 for fail, 2 for a usage error.
- Measurements come from the file only; the script never writes audio.
- `targets.json` is generated by `make_targets.py` from the reference data. Do not hand-edit it: regenerate it.

**Render protocol.**
- One lane solo, at unity fader, with the style's default preset and pattern.
- Loops are ≥ 6 s (2 bars at 126 BPM ≈ 7.6 s). One-shots use `--kind oneshot`.
- The engine may write a `<wav>.gr.json` sidecar (`gr_db_max`, `gr_db_mean`) for the GR check.

**Pass/fail per lane** (from `targets.json`):
- **Every lane**: ST max within target ±tol; TP ≤ cap; DC ≤ −60 dBFS.
  - Loops: last 10 ms ≤ −45 dBFS.
  - One-shots: lead silence ≤ 2 ms.
- **KICK / BASS**: corr <120 ≥ 0.99; S/M ≤ ref max. Centroid: kick 67–95 Hz, bass 77–102 Hz. Crest and PLR windows: ref min −2 … ref max +4 for crest, and ref min … ref max +4 for PLR. Bands: kick 200–800 Hz ≤ 2 %; bass mid ≤ 1.4 %, high ≤ 0.5 %.
- **STAB / PLUCK / CLAP / HATS / PERC / VOX**: S/M window (ref min −2 … ref max +2); <120 Hz energy cap; corr <120 ≥ 0.9; crest window. Centroid windows: stab 389–1325, hats 6473–10878, vox 759–3086 Hz. Tilt windows: stab 200–8k −5.9…−2.9, OH 1–16k −1.8…+2.2.
- **HITS**: ST bass −3 ±1, corr <120 ≥ 0.98.
- **`styles` mode (per-style invariants)**:
  - KICK and BASS spread across styles ≤ 1.0 dB in ST max, in sub+low energy and in <120 Hz energy.
  - KICK and BASS corr <120 ≥ 0.99.
  - BASS re KICK −1.0 ±0.5 per style.
  - SUM ST −19.0 ±0.5 and TP −6 ±1 per style; SUM spread ≤ 1.0 LU.
  - Each other lane present: ST re KICK = style target ±1.0, S/M = style median ±2.5, tilt within the style window.
- **Any lane combination (Q13, rule E21; DECIDED option (a)).** In `styles` mode, every `SUM_*.wav` / `COMBO_*.wav` gets the SUM gate: ST −19.0 ±0.5 for ≥ 3 active MAIN lanes, −20.5…−18.5 for ≤ 2 (the count is read from `COMBO_<mask>`; `SUM_*` uses ±0.5), TP −6 ±1, corr <120 ≥ 0.99, and limiter GR 0 when the engine writes `limiter_gr_db_max` in the sidecar. `combos` mode lists the masks to render (§9.4).
- **`sample` mode (§4 prep rules).** DC ≤ −60 dBFS, lead ≤ 2 ms at −60 dBFS, last 10 ms ≤ −60 dBFS, TP ≤ −6 (+0.05), and LUFS-I −24 ±0.1 (≤ −23.9 when at the TP cap, Q16). LUFS is measured on the native channel count. The per-lane spectral and width windows are reported as **informational pre-chain** values (`info_prechain`) because the lane strip is not applied to a raw file.
- A synthetic self-test confirmed every code path runs and fails where it should (`/workspace/mixeng-scratch/out/selftest`).

---

### 5.1 P0 mute-leak test (`mix_check.py mute`), required by Maestro 2026-10-17
**Rule.** A muted lane contributes nothing to any output.

**Usage.**
```
mix_check.py mute MUTE_<LANE>.wav --full FULL.wav --stem SOLO_<LANE>.wav [--lane LANE] [--dialect D] --json out.json   # (a)
mix_check.py mute <render.wav> --expected <expected.wav> --lane-ref <muted lane(s) unmuted ...>                        # (b)
mix_check.py mute ONLYMUTED_<LANE>.wav --silent --lane-ref SOLO_<LANE>.wav                                              # (c)
```

**Residual.** residual = render − expected, sample-aligned (lag 0). The expected render is one of:
- (a) FULL − SOLO_LANE, for a full render with only that lane muted;
- (b) a given expected render;
- (c) silence, for a render of only the muted lane with the lane muted.

**Pass**, both overall and in every band (20–60, 60–250, 250–1k, 1k–4k, 4k–12k, 12k–20k Hz; 4th-order Butterworth, zero-phase):
- residual RMS **≤ −90 dBFS**, and
- residual RMS **≤ the muted lane's unmuted RMS − 60 dB**. The lane level comes from `--stem`, or from `--lane-ref` summed.

RMS is 10·log10(mean x²) over all samples and channels. The following are reported as information only:
- the max 400 ms window;
- the best lag in ±2048 samples, since a misaligned render can hide a leak or fake one;
- the residual's correlation with each `--lane-ref` file, which points at the leaking lane;
- a warning for 16-bit files.

**Render set the engine team must produce** (per dialect; UKMIN at minimum, all four preferred):
- `FULL.wav`: the default preset and pattern, all lanes unmuted, fixed length (e.g. 8 bars at 126 BPM), same start sample.
- For each lane `L` in KICK, CLAP, SNARE, RIM, CH, OH, RIDE, PERC, BASS, STAB, PLUCK, HITS, PAD and VOX:
  - `MUTE_<L>.wav`: the same render with only L muted;
  - `SOLO_<L>.wav`: L alone, with the other lanes muted. E21 must be **bypassed or frozen at the FULL mask** for both, otherwise the MAIN trim changes and the null fails for a legitimate reason. Solo already bypasses E21.
- Files are float32 WAV, no dither, no random humanise or seed differences between renders (same seed), and the same sample rate and length.
- Optional (c): `ONLYMUTED_<L>.wav`, L's pattern playing with L muted and every other lane muted. It must be digital silence.

That is 1 + 2 × 14 renders per dialect. Run `mute` once per lane with mode (a).

---

## 6. Current status (measured Sat 17 Oct 2026, 12:45–1:14 PM ET; JSON in `tests/results/`)

### 6.1 Staged samples (252 files, `sample` rules of §4 / §5 with Q15 and Q16)
- **Prep gate: 252 / 252 PASS** (the 4 files re-staged under v1.3c were re-checked and pass, §6.3; checksums 252/252 OK). DC, lead, last 10 ms, TP cap and LUFS-I all pass, including the 25 TP-capped files (Q16) and the 71 long-fade files (Q15).
- A first pass reported 15 false fails. The harness measured mono files as dual mono and read −21.0 instead of −24.0, a +3.01 LU error. The harness is fixed to measure mono as mono, matching `prep.py`, and the files themselves were fine. The originals and the staged files were only read, never written.
- **Informational pre-chain fit** (raw source vs the lane's post-chain window, so these are not fails). Count of files outside the window:

| Lane (n) | centroid | S/M | corr <120 |
|---|---|---|---|
| BASS (24) | 16 | 1 | 0 |
| KICK (20) | 5 | 0 | 0 |
| CLAP (16) | 0 | 8 | 0 |
| SNARE (10) | 0 | 5 | 0 |
| RIM (8) | 0 | 0 | 1 |
| CH (14) | 5 | 7 | 0 |
| OH (14) | 3 | 2 | 2 |
| RIDE (6) | 0 | 2 | 0 |
| PERC (12) | 0 | 9 | 0 |
| STAB (26) | 19 | 8 | 5 |
| PLUCK (22) | 8 | 7 | 6 |
| HITS (24) | 0 | 10 | 11 |
| PAD (30) | 0 | 8 | 11 |
| VOX (26) | 6 | 13 | 7 |

  The chains in §3 cover these: mono-below for corr <120, the width stage for S/M, and EQ/tilt for centroid. Each one is confirmed only by the post-chain render test.

### 6.2 Engine renders (22 files: 17 earlier plus 5 new in `ar-backup/rolla_render/leaky_mute/`). All 22 FAIL.
The 3 files in `ar-backup/rolla_mockup_v4_received/` are byte-identical to `mockup_v4/` (cmp), so they were not measured twice. The 17 earlier renders give the same numbers as before (table below). They are the pre-re-anchor harness, about 6–10 dB hot.

| File | Lane | ST max | TP | Fails (why) |
|---|---|---|---|---|
| ar-backup/rolla_render/rerender_all16_kick.wav | SUM | −16.4 | −1.3 | 2.6 LU hot vs −19 ±0.5; TP 4.7 dB over −6 |
| …/rerender_all16_kick_full.wav | SUM | −16.1 | −1.2 | 2.9 LU hot; TP 4.8 dB over |
| mockup_v4/demo_full_track_loop_ukmin.wav | SUM | −16.2 | −1.5 | 2.8 LU hot; TP 4.5 dB over (PLR 14.7, so a gain issue) |
| …/stem_bass.wav | BASS | −18.5 | −2.7 | 6.2 dB hot; TP over −8.9; centroid 62 Hz < 77 |
| …/stem_stab.wav | STAB | −19.5 | −3.1 | 7.3 dB hot; TP; S/M −12.5 < −11.4 (too narrow) |
| …/stem_pad.wav | PAD | −26.3 | −14.8 | 6.4 dB hot; TP |
| …/stem_vox.wav | VOX | −21.1 | −10.0 | 7.6 dB hot; S/M −28.8 (mono); centroid 659 < 759; crest 13.4 < 15.0 |
| mockup_v4/demo_vox_cuts.wav | VOX | −19.2 | −5.2 | 9.5 dB hot; S/M −41.4 (mono) |
| mockup_v4/demo_90s_hits.wav | HITS (GASKIN) | −18.7 | −0.6 | 9.0 dB hot; TP; corr <120 0.89 < 0.98 |
| stem_hit0 … hit7 | HITS (GASKIN) | −17.2…−24.6 | −7.1…−0.5 | all hot vs −27.7 ±1; corr <120 0.80 / 0.979 / 0.97 (hit2/4/5); S/M too narrow on hit1/5/6/7 |
| leaky_mute/stem_bass.wav | BASS | −18.4 | −2.4 | 6.3 dB hot; TP; centroid 67 Hz < 77 |
| leaky_mute/stem_stab.wav | STAB | −22.5 | −3.1 | 4.3 dB hot; TP |
| leaky_mute/stem_pad.wav | PAD | −26.1 | −11.5 | 6.6 dB hot; TP |
| leaky_mute/stem_vox.wav | VOX | −21.6 | −8.3 | 7.1 dB hot; centroid 662 < 759 |
| leaky_mute/stem_hits.wav | HITS (GASKIN) | −18.1 | −2.9 | 9.6 dB hot; TP |

**leaky_mute vs the earlier stems** (same length, 17.77 s). These are sample-aligned differences, a measurement only. The residual (leaky − earlier) sits below each earlier stem's RMS by: bass −20.6 dB, stab −9.4 dB, pad −7.4 dB, vox −9.3 dB. The waveform correlation is 0.996 / 0.947 / 0.914 / 0.946. The leaky stems therefore carry extra material at about −7…−21 dB re the lane, which fits a mute that leaks. **The §5.1 mute test confirms the leak (§6.4).** The leaky_mute stab and vox pass S/M where the earlier ones failed, so the leak adds width.

**Fix order:** unchanged. (1) Re-anchor the harness to §2.2; (2) bass centroid; (3) vox compression and width; (4) stab width; (5) hits −2…−4 dB re bass, mono below 150 Hz; (6) **P0: fix the mute leak (§6.4)** and deliver the §5.1 render set, then render the per-style sets and the §9.4 combo set.

### 6.3 Q15 fade spot-check (10 long-fade files; measurement only, no listening)
Method: each staged file is compared with its original, reconstructed through the manifest prep (gain, lead trim, dual-mono collapse) and sample-aligned. The alignment residual is ≤ −55 dB, except DF_Perc_09 at −23.9 dB (corr 0.9995), where the energy figure is less exact.
- **End click:** last-sample step and HF (> 8 kHz) burst over the last 20 ms vs the body median, and vs the original over the same span.
- **Tail cut:** energy lost = 1 − E(staged) ÷ E(aligned original), whole file.
- **DC, lead.**

All 10 pass the click, DC and lead checks: last sample ≤ −165 dBFS, last 10 ms −62.0…−70.5 dBFS, DC ≤ −217 dBFS, lead ≤ 1.0 ms. Where the HF burst is high, the original has the same HF at that point (Δ ≤ +7 dB), so it is content, not an artefact.

| File (lane) | Fade ms | HF burst re body dB (Δ vs orig) | Energy removed | Verdict |
|---|---|---|---|---|
| HITS/JC_Keys_MPC_123 | 192 | −4.3 (+7.0) | **16.2 % (−0.76 dB)** | **Cuts a musical event:** a new chop starts inside the fade (+73 dB rise, the original ends at −17 dBFS). Suggest ending the file before that onset instead of fading it |
| TAT_SFX_07 | 96 | +26.0 (+3.9) | 1.6 % | OK. The original ends hot (−25 dBFS), so the fade shortens the tail slightly |
| PAD/JF_Phased_Funky_Pad | 96 | −2.0 | 0.8 % | OK |
| THMN_Sweep_Up_1 | 96 | +12.4 (0.0) | 1.1 % | OK (riser HF is content) |
| STAB/TAT_SFX_05 | 96 | +38.2 (−0.1) | **16.0 % (−0.75 dB)** | **Truncates a sounding tail:** the file's peak (−10.4 dBFS) is inside the fade span, and the original ends at −25 dBFS |
| BASS/VER_CleanSub_Bass_C | 96 | −0.5 | 2.7 % | OK |
| DRUMS/PERC/CCH_Perc_07 | 96 | −3.9 | **7.3 % (−0.33 dB)** | Shortens the decay (fade = about 38 % of the file). Borderline; 48 ms would keep more |
| DRUMS/RIM/DF_Perc_09 | 96 | +12.5 (−0.5) | **22.6 % (−1.10 dB)** | **Fades a second hit/flam** (+15.5 dB rise inside the fade). Lower confidence (alignment −23.9 dB) |
| DRUMS/RIDE/PHR_Ride_3 | 48 | +1.2 | 0.4 % | OK |
| PLUCK/Square_Pluck_Csmin_142 | 48 | −2.3 (+4.2) | 0.08 % | OK. The original ends hot (last sample −41 dBFS), so the fade removes a click that the original would have. Last 10 ms −62.0 on the mono mean (SD's prep log: −60.5) |

**Re-staged by Sound Designer (PRESET_SPEC v1.3c, 1:07 PM ET; old copies are in `samples/_backup_v1.3c/`, not staged). Re-checked about 1:12 PM ET** (`tests/results/fade_check_restaged_v13c.json`, same method). All 4 PASS the prep gate (LUFS-I −24.0, DC ≤ −220 dBFS, lead ≤ 1.0 ms):

| File | New end | last sample / last 10 ms dBFS | HF burst re body (Δ vs orig) | Energy lost in fade | Verdict |
|---|---|---|---|---|---|
| HITS/JC_Keys_MPC_123 | ends in the gap before the next chop; 15 ms fade | −205 / −95.0 | −45.5 (−2.0) | **0.0 %** (the next chop, 16.6 % of the source energy, is now outside the file and not faded) | **Fixed.** No onset in the fade |
| STAB/TAT_SFX_05 | source end kept; 60.8 ms raised-cosine² from 3 ms after the peak | −240 / −82.7 | +38.0 (−0.3, content) | **11.2 % (−0.52 dB)**, was 16.0 % | Improved and accepted. The source itself ends hot (−26 dBFS, no material after the end), so some fade loss can't be avoided |
| DRUMS/RIM/DF_Perc_09 | 12 ms fade | −191 / −87.6 | −4.5 (−0.5) | **0.0 %** (was 22.6 %) | **Fixed.** The second hit/flam is kept whole. Alignment −25.5 dB, same caveat as before |
| DRUMS/PERC/CCH_Perc_07 | 48 ms fade | −226 / −127.8 | −94.6 (+0.2) | **0.0 %** (was 7.3 %) | **Fixed** |

**Checksums.** `samples/CHECKSUMS.sha256` verifies **252/252 OK** (`sha256sum -c`, about 1:12 PM ET). Its file list matches the 252 staged files exactly, and the only other WAVs are the 4 in `_backup_v1.3c/`.

### 6.4 Mute-leak test on the existing `leaky_mute` stems (§5.1; `tests/results/mute_leak_existing.json`)
**Method.** Mode (b).
- render = `leaky_mute/stem_X`, which is lane X solo with the other lanes muted.
- expected = the earlier clean `stem_X`.
- lane-ref = the muted lanes' clean stems (the other three of bass/stab/pad/vox). The hits and drums clean stems are not available.
- All files are 16-bit and sample-aligned (best lag 0).
- A positive control (clean stem_bass against itself) **passes** at −200 dBFS. So the test does not false-fail a perfect null, and it **catches all 5 leaky stems**:

| Leaky stem | residual RMS dBFS (≤ −90) | re muted lanes dB (≤ −60) | loudest bands (dBFS) | checks failed |
|---|---|---|---|---|
| bass | −39.4 | −13.0 | 250–1k −40.5, 1–4k −48.6, 60–250 −55.6 | 11 / 16 |
| stab | −42.3 | −24.1 | 250–1k −43.6, 1–4k −50.3 | 10 / 16 |
| pad | −38.4 | −20.2 | 250–1k −39.4, 1–4k −48.0 | 10 / 16 |
| vox | −39.4 | −21.0 | 250–1k −40.6, 1–4k −48.0 | 10 / 16 |
| hits (vs stem_hit0, its best match at corr 0.888; low confidence that this is the right expected render) | −39.5 | −21.4 | 250–1k −40.6 | 11 / 16 |

**What leaks** (measured, not identified):
- In every stem the leak sits about 50 dB above the threshold, mostly at 250 Hz–4 kHz.
- The pad, vox and hits residuals correlate 0.75–0.81 with each other. So one common signal leaks into every soloed lane.
- That signal correlates only 0.13–0.24 with the clean stab stem and ≤ 0.03 with bass/pad/vox. So it is mostly a lane I have no clean stem for (drums, PLUCK or HITS are candidates), with some stab in it.
- The §5.1 render set (one mute per lane) will pin it down.

---

## 7. Open items / caveats

- **Compressor values are [N].** Ratio, knee, threshold and target GR are genre-norm start points. Their correctness is judged by the measured windows the test enforces (crest, PLR, ST re kick, width), not by the numbers themselves.
- **Saturation type and drive are [N].** No reference measurement isolates harmonics. The measured guards are bass mid/high share and centroid/tilt windows.
- **Pre-master assumption.** The refs are mastered stems (mix PLR 7.1–9.3); ROLLA output is pre-master. That is why PLR and crest windows allow +4 dB above the ref max.
- **The "other" stem mixes stab, pad and lead,** so STAB/PLUCK levels are a combined budget.
- **Clamp: DECIDED (Maestro, 2026-10-17).** GASKIN bass sits 3.2 dB louder vs kick than its ref; kept at kick −1 dB in every style so the bus chain behaves the same across styles.
- **The PRESET_SPEC absolute anchors conflict with −6 dBTP summed (§2.2).** Maestro should re-target the PRESET_SPEC §5 trims.
- **E21 gate: DECIDED (Maestro, 2026-10-17), option (a).** ±0.5 for ≥ 3 lanes, −20.5…−18.5 for 1–2 (drums-only treated as sparse), −5 dBTP cap everywhere, no sum limiter. 20 of 256 combos are still predicted to miss, by 0.03–0.18 LU: 16 in LONG, 2 in POP, 2 in GASKIN (§9.4). These are open until real COMBO renders exist; the fix is the set's PLR, not the trim.
- **Mute leak (P0).** The existing leaky_mute renders fail by about 50 dB (§6.4). The engine team must fix it and deliver the §5.1 render set.

## 8. `mix_profiles.json` (machine-readable dialect profiles)

**Path:** `plugin-design/mix_profiles.json`, `schema_version` **1.2.0** (1.1.0 added `main_auto_trim` and `sample_prep_acceptance`; 1.2.0 adds the option (a) lane-count gate in `main_auto_trim.gate` and `engine_contracts.mute_leak`; every lane value is unchanged from 1.0.0).
- Generated by `mixeng-scratch/tools/make_profiles.py` from `tests/targets.json` and the reference data. Do not hand-edit it; regenerate it.
- Validated by `tests/validate_profiles.py`.

**Shape:** `dialects.<UKMIN|LONG|POP|GASKIN>`
- `.output_trim_db`, `.top_trim_db`, `.default_lanes`
- `.lanes.<BASS|STAB|PLUCK|HITS|PAD|VOX>` → parameters
- `.lanes.DRUMS.voices.<KICK|CLAP|SNARE|RIM|CH|OH|RIDE|PERC>` → parameters

Lanes are **fully expanded** per dialect, so the engine never has to resolve inheritance. Top-level blocks: `anchor`, `preset_overrides` (§9.1), `dialect_switch` (§9.2), `main_auto_trim` (§9.4, E21), `sample_prep_acceptance` (§4.7, Q15/Q16), `engine_contracts` (§5.1), and `_doc` (units and source tags).

**Per-lane / per-voice keys and units**

| Key | Unit | Meaning |
|---|---|---|
| `hpf`, `lpf` | n/a | `{type, freq_hz (Hz), slope_db_oct (12 or 24, TPT/SVF Butterworth cascade), src}`, or `null` |
| `eq[]` | n/a | 1–2 moves `{type bell/lowshelf/highshelf, freq_hz, gain_db (dB), q (RBJ Q), src}` |
| `comp` | n/a | `{threshold_dbfs (dBFS after §4 prep at −24 LUFS), ratio (x:1), attack_ms, release_ms, knee_db (total width), target_gr_db (peak GR on the default pattern), src{per field}}` |
| `sat` | n/a | `{type softclip_tanh / tape / tube_asym / none, drive_db, mix_pct (% wet), src}` |
| `tilt` | n/a | `{pivot_hz 1000, db_per_oct (tilt-EQ setting re UK MIN), measured_style_tilt_db_oct, src}` |
| `width` | n/a | `{mono_below_hz, side_mid_db (target S/M), side_mid_max_db / side_mid_min_db (window), corr_lt120_min, src}` |
| `lufs_offset_vs_kick_db` | dB | lane ST max − KICK ST max |
| `st_target_lufs` | LUFS | absolute ST max of the solo loop render (= kick −23.7 + offset) |
| `output_trim_db` | dB | style TOP trim applied to this lane (0 for KICK, BASS, HITS) |
| `click_db_re_kick` | dB | KICK only: 909 click level re the kick peak |
| `level_src` | n/a | source tag of the level |
| dialect `output_trim_db` / `top_trim_db` | dB | MAIN out trim (0 by design) / per-style trim on top lanes |

**Source tags.** `M` measured on the refs; `M-melak` / `M-ref` from PRESET_SPEC §9 one-shots; `D` derived; `A&R`; `PRESET_SPEC`; `N` genre norm; `N+crest-rule` (base [N], per-dialect change = −Δcrest [D]); `M-LOW`.

**Validation** (`tests/validate_profiles.py`, run 17 Oct 2026; output in §8.1). It checks that:
- the file parses and has `schema_version`;
- the block below parses and is identical to the file;
- every ST target, offset and trim equals `targets.json`;
- §2.2 ST targets, the §2.3 re-kick / GR / width cells, and the §3 attack/release values match the JSON.

```json
{
 "schema_version": "1.2.0",
 "_doc": {
  "what": "ROLLA baked per-lane mix chains per dialect (MIX_SPEC.md §8). Lanes are fully expanded per dialect (no inheritance needed at load). Preset overrides (§9) are applied on top.",
  "generated_by": "/workspace/mixeng-scratch/tools/make_profiles.py from tests/targets.json + reference measurements",
  "chain_order": [
   "sample_prep",
   "hpf",
   "lpf",
   "eq[0..1]",
   "comp",
   "sat",
   "tilt",
   "width",
   "output_trim"
  ],
  "units": {
   "freq_hz": "Hz",
   "slope_db_oct": "dB/octave (12 or 24; Butterworth/TPT SVF cascade)",
   "gain_db": "dB",
   "q": "bandwidth Q (RBJ)",
   "threshold_dbfs": "dBFS after sample prep (§4 normalises sources to -24 LUFS)",
   "ratio": "x:1",
   "attack_ms": "ms",
   "release_ms": "ms",
   "knee_db": "dB (total knee width)",
   "target_gr_db": "dB peak gain reduction on the default pattern; harness nudges threshold to hit it",
   "drive_db": "dB into the shaper",
   "mix_pct": "% wet (dual-path crossfade)",
   "db_per_oct": "dB/octave tilt around pivot_hz (tilt EQ setting, relative to the lane EQ)",
   "measured_style_tilt_db_oct": "measured slope of the ref stem in that style (200-8k for other/vocals, 1-16k for drums)",
   "side_mid_db": "side/mid energy ratio target (dB)",
   "side_mid_min_db/side_mid_max_db": "test window (dB)",
   "mono_below_hz": "stereo collapsed to mono below this frequency",
   "lufs_offset_vs_kick_db": "lane ST LUFS max minus KICK ST LUFS max (dB)",
   "st_target_lufs": "absolute ST LUFS max of a solo loop render (LUFS)",
   "output_trim_db": "lane: style TOP trim applied to this lane (dB); dialect: MAIN output trim (dB)",
   "top_trim_db": "per-style trim on all non-kick/bass/hits lanes (dB)",
   "click_db_re_kick": "909 kick click level re kick peak (dB)"
  },
  "src_tags": {
   "M": "measured on the reference stems (this spec)",
   "M-melak/M-ref": "PRESET_SPEC §9 one-shot measurements",
   "D": "derived arithmetic from measured data",
   "A&R": "AR_SOUND_REVIEW target",
   "PRESET_SPEC": "from PRESET_SPEC",
   "N": "genre-norm fallback, not measurable from refs (guarded by a measured test window)",
   "N+crest-rule": "base value [N], per-dialect change = -(style crest - UK MIN crest) [D]",
   "M-LOW": "measured, LOW confidence isolation"
  }
 },
 "anchor": {
  "kick_st_lufs": -23.7,
  "sum_st_lufs": -19.0,
  "sum_tp_dbtp": -6.0,
  "bass_re_kick_db": -1.0,
  "src": "D"
 },
 "dialects": {
  "UKMIN": {
   "output_trim_db": 0.0,
   "top_trim_db": -0.0,
   "default_lanes": [
    "DRUMS.KICK",
    "BASS",
    "STAB",
    "DRUMS.CLAP",
    "DRUMS.CH+OH",
    "DRUMS.PERC",
    "VOX"
   ],
   "lanes": {
    "BASS": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 30,
      "slope_db_oct": 24,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 2500,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 90,
       "gain_db": 1.5,
       "q": 0.9,
       "src": "M"
      },
      {
       "type": "lowshelf",
       "freq_hz": 45,
       "gain_db": -2.0,
       "q": 0.7,
       "src": "M/A&R"
      }
     ],
     "comp": {
      "threshold_dbfs": -20,
      "ratio": 4,
      "attack_ms": 8,
      "release_ms": 150,
      "knee_db": 6,
      "target_gr_db": 4,
      "src": {
       "attack_ms": "M",
       "release_ms": "M",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tape",
      "drive_db": 4,
      "mix_pct": 50,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 220,
      "side_mid_max_db": -23.8,
      "src": "M",
      "side_mid_db": -26.9
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -1.0,
     "st_target_lufs": -24.7,
     "output_trim_db": 0.0,
     "level_src": "D clamp"
    },
    "STAB": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 120,
      "slope_db_oct": 12,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 10000,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 1200,
       "gain_db": 1.5,
       "q": 0.8,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -1.5,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 12,
      "release_ms": 170,
      "knee_db": 6,
      "target_gr_db": 3,
      "src": {
       "attack_ms": "M",
       "release_ms": "M",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 3,
      "mix_pct": 30,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -5.2
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": -4.36,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -3.1,
     "st_target_lufs": -26.8,
     "output_trim_db": -0.0,
     "level_src": "M"
    },
    "VOX": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 150,
      "slope_db_oct": 24,
      "src": "M"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 12000,
      "slope_db_oct": 12,
      "src": "N"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -2.5,
       "q": 1.2,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 3000,
       "gain_db": 2.0,
       "q": 1.0,
       "src": "M"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 5,
      "release_ms": 80,
      "knee_db": 6,
      "target_gr_db": 3,
      "src": {
       "attack_ms": "N",
       "release_ms": "N",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tape",
      "drive_db": 2,
      "mix_pct": 20,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -8.7
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": -0.18,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -5.0,
     "st_target_lufs": -28.7,
     "output_trim_db": -0.0,
     "level_src": "M"
    },
    "HITS": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 220,
      "slope_db_oct": 24,
      "src": "A&R"
     },
     "lpf": null,
     "eq": [
      {
       "type": "bell",
       "freq_hz": 500,
       "gain_db": -2.0,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -20,
      "ratio": 4,
      "attack_ms": 11,
      "release_ms": 250,
      "knee_db": 6,
      "target_gr_db": 4,
      "src": {
       "attack_ms": "M(borrowed Gaskin stab)",
       "release_ms": "M(borrowed Gaskin stab)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 4,
      "mix_pct": 40,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 150,
      "corr_lt120_min": 0.98,
      "src": "A&R"
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -4.0,
     "st_target_lufs": -27.7,
     "output_trim_db": 0.0,
     "level_src": "A&R"
    },
    "PAD": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 150,
      "slope_db_oct": 12,
      "src": "N"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 8000,
      "slope_db_oct": 12,
      "src": "N"
     },
     "eq": [],
     "comp": {
      "threshold_dbfs": -24,
      "ratio": 2,
      "attack_ms": 30,
      "release_ms": 300,
      "knee_db": 6,
      "target_gr_db": 2,
      "src": {
       "attack_ms": "N",
       "release_ms": "N",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N"
      }
     },
     "sat": {
      "type": "none",
      "drive_db": 0,
      "mix_pct": 0,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "side_mid_min_db": -12,
      "side_mid_max_db": 0,
      "src": "N"
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -9.0,
     "st_target_lufs": -32.7,
     "output_trim_db": -0.0,
     "level_src": "PRESET_SPEC D"
    },
    "PLUCK": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 120,
      "slope_db_oct": 12,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 10000,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 1200,
       "gain_db": 1.5,
       "q": 0.8,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -1.5,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 12,
      "release_ms": 110,
      "knee_db": 6,
      "target_gr_db": 3,
      "src": {
       "attack_ms": "M",
       "release_ms": "M(LONG other decay 106)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 3,
      "mix_pct": 30,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -5.2
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": -4.36,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -3.1,
     "st_target_lufs": -26.8,
     "output_trim_db": -0.0,
     "level_src": "M"
    },
    "DRUMS": {
     "_doc": "909 lane: per-voice chains; DRUMS bus has no extra processing in v1",
     "voices": {
      "CLAP": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 200,
        "slope_db_oct": 24,
        "src": "M"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 14000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 1300,
         "gain_db": 2.0,
         "q": 1.2,
         "src": "M-melak"
        },
        {
         "type": "bell",
         "freq_hz": 400,
         "gain_db": -2.0,
         "q": 1.5,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -20,
        "ratio": 4,
        "attack_ms": 3,
        "release_ms": 60,
        "knee_db": 3,
        "target_gr_db": 4,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 2,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -9.9
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -2.5,
       "st_target_lufs": -26.2,
       "output_trim_db": -0.0,
       "level_src": "M"
      },
      "SNARE": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 150,
        "slope_db_oct": 24,
        "src": "N"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 14000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 200,
         "gain_db": 2.0,
         "q": 1.2,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -20,
        "ratio": 4,
        "attack_ms": 3,
        "release_ms": 80,
        "knee_db": 3,
        "target_gr_db": 4,
        "src": {
         "attack_ms": "N",
         "release_ms": "N",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 2,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M(clap)",
        "side_mid_db": -9.9
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -2.5,
       "st_target_lufs": -26.2,
       "output_trim_db": -0.0,
       "level_src": "N(clap slot)"
      },
      "RIM": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 300,
        "slope_db_oct": 24,
        "src": "N"
       },
       "lpf": null,
       "eq": [
        {
         "type": "bell",
         "freq_hz": 1760,
         "gain_db": 2.0,
         "q": 2.0,
         "src": "M-ref"
        }
       ],
       "comp": {
        "threshold_dbfs": -22,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 150,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-ref",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "N"
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -7.5,
       "st_target_lufs": -31.2,
       "output_trim_db": -0.0,
       "level_src": "M-ref"
      },
      "CH": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 10000,
         "gain_db": 1.5,
         "q": 0.7,
         "src": "M"
        },
        {
         "type": "bell",
         "freq_hz": 4000,
         "gain_db": -2.0,
         "q": 2.0,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 45,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -8.8
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -16.6,
       "st_target_lufs": -40.3,
       "output_trim_db": -0.0,
       "level_src": "M split"
      },
      "OH": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 10000,
         "gain_db": 1.5,
         "q": 0.7,
         "src": "M"
        },
        {
         "type": "bell",
         "freq_hz": 4000,
         "gain_db": -2.0,
         "q": 2.0,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 145,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -8.8
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": 0.17,
        "src": "M"
       },
       "lufs_offset_vs_kick_db": -4.6,
       "st_target_lufs": -28.3,
       "output_trim_db": -0.0,
       "level_src": "M"
      },
      "RIDE": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 8000,
         "gain_db": 1.0,
         "q": 0.7,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 2,
        "attack_ms": 2,
        "release_ms": 175,
        "knee_db": 3,
        "target_gr_db": 1,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-melak",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M(hats)",
        "side_mid_db": -8.8
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -22.6,
       "st_target_lufs": -46.3,
       "output_trim_db": -0.0,
       "level_src": "PRESET_SPEC R"
      },
      "PERC": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 250,
        "slope_db_oct": 24,
        "src": "D"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 12000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 2000,
         "gain_db": 1.5,
         "q": 1.0,
         "src": "M-LOW"
        }
       ],
       "comp": {
        "threshold_dbfs": -22,
        "ratio": 3,
        "attack_ms": 3,
        "release_ms": 75,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-melak",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M-LOW",
        "side_mid_db": -12.1
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -7.6,
       "st_target_lufs": -31.3,
       "output_trim_db": -0.0,
       "level_src": "M"
      },
      "KICK": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 28,
        "slope_db_oct": 24,
        "src": "D"
       },
       "lpf": null,
       "eq": [
        {
         "type": "bell",
         "freq_hz": 55,
         "gain_db": 1.5,
         "q": 1.0,
         "src": "D"
        },
        {
         "type": "bell",
         "freq_hz": 250,
         "gain_db": -2.0,
         "q": 1.4,
         "src": "D"
        }
       ],
       "comp": {
        "threshold_dbfs": -18,
        "ratio": 4,
        "attack_ms": 15,
        "release_ms": 160,
        "knee_db": 6,
        "target_gr_db": 3,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 3,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 141,
        "side_mid_max_db": -19.2,
        "src": "M",
        "side_mid_db": -27.8
       },
       "click_db_re_kick": -15,
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": 0.0,
       "st_target_lufs": -23.7,
       "output_trim_db": 0.0,
       "level_src": "D anchor"
      }
     }
    }
   }
  },
  "LONG": {
   "output_trim_db": 0.0,
   "top_trim_db": -0.3,
   "default_lanes": [
    "DRUMS.KICK",
    "BASS",
    "STAB",
    "DRUMS.CLAP",
    "DRUMS.CH+OH",
    "DRUMS.PERC",
    "VOX"
   ],
   "lanes": {
    "BASS": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 30,
      "slope_db_oct": 24,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 2500,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 90,
       "gain_db": 1.5,
       "q": 0.9,
       "src": "M"
      },
      {
       "type": "lowshelf",
       "freq_hz": 45,
       "gain_db": -2.0,
       "q": 0.7,
       "src": "M/A&R"
      }
     ],
     "comp": {
      "threshold_dbfs": -20,
      "ratio": 4,
      "attack_ms": 8,
      "release_ms": 210,
      "knee_db": 6,
      "target_gr_db": 4,
      "src": {
       "attack_ms": "M",
       "release_ms": "M(style decay)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tape",
      "drive_db": 4,
      "mix_pct": 50,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 220,
      "side_mid_max_db": -23.8,
      "src": "M",
      "side_mid_db": -44.2
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -1.0,
     "st_target_lufs": -24.7,
     "output_trim_db": 0.0,
     "level_src": "D clamp"
    },
    "STAB": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 120,
      "slope_db_oct": 12,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 10000,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 1200,
       "gain_db": 1.5,
       "q": 0.8,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -1.5,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 12,
      "release_ms": 170,
      "knee_db": 6,
      "target_gr_db": 0,
      "src": {
       "attack_ms": "M",
       "release_ms": "M",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 2,
      "mix_pct": 20,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -3.2
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": -0.9,
      "measured_style_tilt_db_oct": -5.22,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -8.9,
     "st_target_lufs": -32.6,
     "output_trim_db": -0.3,
     "level_src": "M"
    },
    "VOX": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 150,
      "slope_db_oct": 24,
      "src": "M"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 12000,
      "slope_db_oct": 12,
      "src": "N"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -2.5,
       "q": 1.2,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 3000,
       "gain_db": 2.0,
       "q": 1.0,
       "src": "M"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 5,
      "release_ms": 80,
      "knee_db": 6,
      "target_gr_db": 4,
      "src": {
       "attack_ms": "N",
       "release_ms": "N",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tape",
      "drive_db": 2,
      "mix_pct": 20,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": 0.9
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 2.8,
      "measured_style_tilt_db_oct": 2.66,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -3.5,
     "st_target_lufs": -27.2,
     "output_trim_db": -0.3,
     "level_src": "M"
    },
    "HITS": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 220,
      "slope_db_oct": 24,
      "src": "A&R"
     },
     "lpf": null,
     "eq": [
      {
       "type": "bell",
       "freq_hz": 500,
       "gain_db": -2.0,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -20,
      "ratio": 4,
      "attack_ms": 11,
      "release_ms": 250,
      "knee_db": 6,
      "target_gr_db": 4,
      "src": {
       "attack_ms": "M(borrowed Gaskin stab)",
       "release_ms": "M(borrowed Gaskin stab)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 4,
      "mix_pct": 40,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 150,
      "corr_lt120_min": 0.98,
      "src": "A&R"
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -4.0,
     "st_target_lufs": -27.7,
     "output_trim_db": 0.0,
     "level_src": "A&R"
    },
    "PAD": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 150,
      "slope_db_oct": 12,
      "src": "N"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 8000,
      "slope_db_oct": 12,
      "src": "N"
     },
     "eq": [],
     "comp": {
      "threshold_dbfs": -24,
      "ratio": 2,
      "attack_ms": 30,
      "release_ms": 300,
      "knee_db": 6,
      "target_gr_db": 2,
      "src": {
       "attack_ms": "N",
       "release_ms": "N",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N"
      }
     },
     "sat": {
      "type": "none",
      "drive_db": 0,
      "mix_pct": 0,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "side_mid_min_db": -12,
      "side_mid_max_db": 0,
      "src": "N"
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -9.3,
     "st_target_lufs": -33.0,
     "output_trim_db": -0.3,
     "level_src": "PRESET_SPEC D"
    },
    "PLUCK": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 120,
      "slope_db_oct": 12,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 10000,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 1200,
       "gain_db": 1.5,
       "q": 0.8,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -1.5,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 12,
      "release_ms": 110,
      "knee_db": 6,
      "target_gr_db": 0,
      "src": {
       "attack_ms": "M",
       "release_ms": "M(LONG other decay 106)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 2,
      "mix_pct": 20,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -3.2
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": -0.9,
      "measured_style_tilt_db_oct": -5.22,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -8.9,
     "st_target_lufs": -32.6,
     "output_trim_db": -0.3,
     "level_src": "M"
    },
    "DRUMS": {
     "_doc": "909 lane: per-voice chains; DRUMS bus has no extra processing in v1",
     "voices": {
      "CLAP": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 200,
        "slope_db_oct": 24,
        "src": "M"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 14000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 1300,
         "gain_db": 2.0,
         "q": 1.2,
         "src": "M-melak"
        },
        {
         "type": "bell",
         "freq_hz": 400,
         "gain_db": -2.0,
         "q": 1.5,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -20,
        "ratio": 4,
        "attack_ms": 3,
        "release_ms": 60,
        "knee_db": 3,
        "target_gr_db": 5,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 2,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -9.4
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -2.3,
       "st_target_lufs": -26.0,
       "output_trim_db": -0.3,
       "level_src": "M"
      },
      "SNARE": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 150,
        "slope_db_oct": 24,
        "src": "N"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 14000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 200,
         "gain_db": 2.0,
         "q": 1.2,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -20,
        "ratio": 4,
        "attack_ms": 3,
        "release_ms": 80,
        "knee_db": 3,
        "target_gr_db": 5,
        "src": {
         "attack_ms": "N",
         "release_ms": "N",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 2,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M(clap)",
        "side_mid_db": -9.4
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -2.3,
       "st_target_lufs": -26.0,
       "output_trim_db": -0.3,
       "level_src": "N(clap slot)"
      },
      "RIM": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 300,
        "slope_db_oct": 24,
        "src": "N"
       },
       "lpf": null,
       "eq": [
        {
         "type": "bell",
         "freq_hz": 1760,
         "gain_db": 2.0,
         "q": 2.0,
         "src": "M-ref"
        }
       ],
       "comp": {
        "threshold_dbfs": -22,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 150,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-ref",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "N"
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -7.3,
       "st_target_lufs": -31.0,
       "output_trim_db": -0.3,
       "level_src": "M-ref"
      },
      "CH": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 10000,
         "gain_db": 1.5,
         "q": 0.7,
         "src": "M"
        },
        {
         "type": "bell",
         "freq_hz": 4000,
         "gain_db": -2.0,
         "q": 2.0,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 45,
        "knee_db": 3,
        "target_gr_db": 3,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -8.2
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -14.5,
       "st_target_lufs": -38.2,
       "output_trim_db": -0.3,
       "level_src": "M split"
      },
      "OH": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 10000,
         "gain_db": 1.5,
         "q": 0.7,
         "src": "M"
        },
        {
         "type": "bell",
         "freq_hz": 4000,
         "gain_db": -2.0,
         "q": 2.0,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 145,
        "knee_db": 3,
        "target_gr_db": 3,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -8.2
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.7,
        "measured_style_tilt_db_oct": 0.85,
        "src": "M"
       },
       "lufs_offset_vs_kick_db": -2.5,
       "st_target_lufs": -26.2,
       "output_trim_db": -0.3,
       "level_src": "M"
      },
      "RIDE": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 8000,
         "gain_db": 1.0,
         "q": 0.7,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 2,
        "attack_ms": 2,
        "release_ms": 175,
        "knee_db": 3,
        "target_gr_db": 1,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-melak",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M(hats)",
        "side_mid_db": -8.2
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -20.5,
       "st_target_lufs": -44.2,
       "output_trim_db": -0.3,
       "level_src": "PRESET_SPEC R"
      },
      "PERC": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 250,
        "slope_db_oct": 24,
        "src": "D"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 12000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 2000,
         "gain_db": 1.5,
         "q": 1.0,
         "src": "M-LOW"
        }
       ],
       "comp": {
        "threshold_dbfs": -22,
        "ratio": 3,
        "attack_ms": 3,
        "release_ms": 75,
        "knee_db": 3,
        "target_gr_db": 4,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-melak",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M-LOW",
        "side_mid_db": -19.4
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -8.7,
       "st_target_lufs": -32.4,
       "output_trim_db": -0.3,
       "level_src": "M"
      },
      "KICK": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 28,
        "slope_db_oct": 24,
        "src": "D"
       },
       "lpf": null,
       "eq": [
        {
         "type": "bell",
         "freq_hz": 55,
         "gain_db": 1.5,
         "q": 1.0,
         "src": "D"
        },
        {
         "type": "bell",
         "freq_hz": 250,
         "gain_db": -2.0,
         "q": 1.4,
         "src": "D"
        }
       ],
       "comp": {
        "threshold_dbfs": -18,
        "ratio": 4,
        "attack_ms": 15,
        "release_ms": 160,
        "knee_db": 6,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 3,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 141,
        "side_mid_max_db": -19.2,
        "src": "M",
        "side_mid_db": -31.5
       },
       "click_db_re_kick": -15,
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": 0.0,
       "st_target_lufs": -23.7,
       "output_trim_db": 0.0,
       "level_src": "D anchor"
      }
     }
    }
   }
  },
  "POP": {
   "output_trim_db": 0.0,
   "top_trim_db": -1.8,
   "default_lanes": [
    "DRUMS.KICK",
    "BASS",
    "STAB",
    "DRUMS.CLAP",
    "DRUMS.CH+OH",
    "DRUMS.PERC",
    "VOX"
   ],
   "lanes": {
    "BASS": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 30,
      "slope_db_oct": 24,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 2500,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 90,
       "gain_db": 1.5,
       "q": 0.9,
       "src": "M"
      },
      {
       "type": "lowshelf",
       "freq_hz": 45,
       "gain_db": -2.0,
       "q": 0.7,
       "src": "M/A&R"
      }
     ],
     "comp": {
      "threshold_dbfs": -20,
      "ratio": 4,
      "attack_ms": 8,
      "release_ms": 150,
      "knee_db": 6,
      "target_gr_db": 5,
      "src": {
       "attack_ms": "M",
       "release_ms": "M",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tape",
      "drive_db": 5,
      "mix_pct": 50,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 220,
      "side_mid_max_db": -23.8,
      "src": "M",
      "side_mid_db": -34.8
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -1.0,
     "st_target_lufs": -24.7,
     "output_trim_db": 0.0,
     "level_src": "D clamp"
    },
    "STAB": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 120,
      "slope_db_oct": 12,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 10000,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 1200,
       "gain_db": 1.5,
       "q": 0.8,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -1.5,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 12,
      "release_ms": 170,
      "knee_db": 6,
      "target_gr_db": 5,
      "src": {
       "attack_ms": "M",
       "release_ms": "M",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 3,
      "mix_pct": 30,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -4.6
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": -4.41,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -2.3,
     "st_target_lufs": -26.0,
     "output_trim_db": -1.8,
     "level_src": "M"
    },
    "VOX": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 150,
      "slope_db_oct": 24,
      "src": "M"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 12000,
      "slope_db_oct": 12,
      "src": "N"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -2.5,
       "q": 1.2,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 3000,
       "gain_db": 2.0,
       "q": 1.0,
       "src": "M"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 5,
      "release_ms": 80,
      "knee_db": 6,
      "target_gr_db": 8,
      "src": {
       "attack_ms": "N",
       "release_ms": "N",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tape",
      "drive_db": 3,
      "mix_pct": 30,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -11.6
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": -1.6,
      "measured_style_tilt_db_oct": -1.77,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -1.5,
     "st_target_lufs": -25.2,
     "output_trim_db": -1.8,
     "level_src": "M"
    },
    "HITS": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 220,
      "slope_db_oct": 24,
      "src": "A&R"
     },
     "lpf": null,
     "eq": [
      {
       "type": "bell",
       "freq_hz": 500,
       "gain_db": -2.0,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -20,
      "ratio": 4,
      "attack_ms": 11,
      "release_ms": 250,
      "knee_db": 6,
      "target_gr_db": 4,
      "src": {
       "attack_ms": "M(borrowed Gaskin stab)",
       "release_ms": "M(borrowed Gaskin stab)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 4,
      "mix_pct": 40,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 150,
      "corr_lt120_min": 0.98,
      "src": "A&R"
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -4.0,
     "st_target_lufs": -27.7,
     "output_trim_db": 0.0,
     "level_src": "A&R"
    },
    "PAD": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 150,
      "slope_db_oct": 12,
      "src": "N"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 8000,
      "slope_db_oct": 12,
      "src": "N"
     },
     "eq": [],
     "comp": {
      "threshold_dbfs": -24,
      "ratio": 2,
      "attack_ms": 30,
      "release_ms": 300,
      "knee_db": 6,
      "target_gr_db": 2,
      "src": {
       "attack_ms": "N",
       "release_ms": "N",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N"
      }
     },
     "sat": {
      "type": "none",
      "drive_db": 0,
      "mix_pct": 0,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "side_mid_min_db": -12,
      "side_mid_max_db": 0,
      "src": "N"
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -10.8,
     "st_target_lufs": -34.5,
     "output_trim_db": -1.8,
     "level_src": "PRESET_SPEC D"
    },
    "PLUCK": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 120,
      "slope_db_oct": 12,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 10000,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 1200,
       "gain_db": 1.5,
       "q": 0.8,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -1.5,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 12,
      "release_ms": 110,
      "knee_db": 6,
      "target_gr_db": 5,
      "src": {
       "attack_ms": "M",
       "release_ms": "M(LONG other decay 106)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 3,
      "mix_pct": 30,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -4.6
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": -4.41,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -2.3,
     "st_target_lufs": -26.0,
     "output_trim_db": -1.8,
     "level_src": "M"
    },
    "DRUMS": {
     "_doc": "909 lane: per-voice chains; DRUMS bus has no extra processing in v1",
     "voices": {
      "CLAP": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 200,
        "slope_db_oct": 24,
        "src": "M"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 14000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 1300,
         "gain_db": 2.0,
         "q": 1.2,
         "src": "M-melak"
        },
        {
         "type": "bell",
         "freq_hz": 400,
         "gain_db": -2.0,
         "q": 1.5,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -20,
        "ratio": 4,
        "attack_ms": 3,
        "release_ms": 60,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 2,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -10.0
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -5.3,
       "st_target_lufs": -29.0,
       "output_trim_db": -1.8,
       "level_src": "M"
      },
      "SNARE": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 150,
        "slope_db_oct": 24,
        "src": "N"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 14000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 200,
         "gain_db": 2.0,
         "q": 1.2,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -20,
        "ratio": 4,
        "attack_ms": 3,
        "release_ms": 80,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "N",
         "release_ms": "N",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 2,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M(clap)",
        "side_mid_db": -10.0
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -5.3,
       "st_target_lufs": -29.0,
       "output_trim_db": -1.8,
       "level_src": "N(clap slot)"
      },
      "RIM": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 300,
        "slope_db_oct": 24,
        "src": "N"
       },
       "lpf": null,
       "eq": [
        {
         "type": "bell",
         "freq_hz": 1760,
         "gain_db": 2.0,
         "q": 2.0,
         "src": "M-ref"
        }
       ],
       "comp": {
        "threshold_dbfs": -22,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 150,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-ref",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "N"
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -10.3,
       "st_target_lufs": -34.0,
       "output_trim_db": -1.8,
       "level_src": "M-ref"
      },
      "CH": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 10000,
         "gain_db": 1.5,
         "q": 0.7,
         "src": "M"
        },
        {
         "type": "bell",
         "freq_hz": 4000,
         "gain_db": -2.0,
         "q": 2.0,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 45,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -9.6
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -18.6,
       "st_target_lufs": -42.3,
       "output_trim_db": -1.8,
       "level_src": "M split"
      },
      "OH": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 10000,
         "gain_db": 1.5,
         "q": 0.7,
         "src": "M"
        },
        {
         "type": "bell",
         "freq_hz": 4000,
         "gain_db": -2.0,
         "q": 2.0,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 145,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -9.6
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 1.5,
        "measured_style_tilt_db_oct": 1.71,
        "src": "M"
       },
       "lufs_offset_vs_kick_db": -6.6,
       "st_target_lufs": -30.3,
       "output_trim_db": -1.8,
       "level_src": "M"
      },
      "RIDE": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 8000,
         "gain_db": 1.0,
         "q": 0.7,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 2,
        "attack_ms": 2,
        "release_ms": 175,
        "knee_db": 3,
        "target_gr_db": 1,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-melak",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M(hats)",
        "side_mid_db": -9.6
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -24.6,
       "st_target_lufs": -48.3,
       "output_trim_db": -1.8,
       "level_src": "PRESET_SPEC R"
      },
      "PERC": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 250,
        "slope_db_oct": 24,
        "src": "D"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 12000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 2000,
         "gain_db": 1.5,
         "q": 1.0,
         "src": "M-LOW"
        }
       ],
       "comp": {
        "threshold_dbfs": -22,
        "ratio": 3,
        "attack_ms": 3,
        "release_ms": 75,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-melak",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M-LOW",
        "side_mid_db": -12.3
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -12.5,
       "st_target_lufs": -36.2,
       "output_trim_db": -1.8,
       "level_src": "M"
      },
      "KICK": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 28,
        "slope_db_oct": 24,
        "src": "D"
       },
       "lpf": null,
       "eq": [
        {
         "type": "bell",
         "freq_hz": 55,
         "gain_db": 1.5,
         "q": 1.0,
         "src": "D"
        },
        {
         "type": "bell",
         "freq_hz": 250,
         "gain_db": -2.0,
         "q": 1.4,
         "src": "D"
        }
       ],
       "comp": {
        "threshold_dbfs": -18,
        "ratio": 4,
        "attack_ms": 15,
        "release_ms": 160,
        "knee_db": 6,
        "target_gr_db": 3,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 3,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 141,
        "side_mid_max_db": -19.2,
        "src": "M",
        "side_mid_db": -38.3
       },
       "click_db_re_kick": -15,
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": 0.0,
       "st_target_lufs": -23.7,
       "output_trim_db": 0.0,
       "level_src": "D anchor"
      }
     }
    }
   }
  },
  "GASKIN": {
   "output_trim_db": 0.0,
   "top_trim_db": 1.0,
   "default_lanes": [
    "DRUMS.KICK",
    "BASS",
    "STAB",
    "DRUMS.CLAP",
    "DRUMS.CH+OH",
    "DRUMS.PERC",
    "HITS"
   ],
   "lanes": {
    "BASS": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 30,
      "slope_db_oct": 24,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 2500,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 90,
       "gain_db": 1.5,
       "q": 0.9,
       "src": "M"
      },
      {
       "type": "lowshelf",
       "freq_hz": 45,
       "gain_db": -2.0,
       "q": 0.7,
       "src": "M/A&R"
      }
     ],
     "comp": {
      "threshold_dbfs": -20,
      "ratio": 4,
      "attack_ms": 8,
      "release_ms": 160,
      "knee_db": 6,
      "target_gr_db": 6,
      "src": {
       "attack_ms": "M",
       "release_ms": "M(style decay)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tape",
      "drive_db": 2,
      "mix_pct": 20,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 220,
      "side_mid_max_db": -23.8,
      "src": "M",
      "side_mid_db": -34.4
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -1.0,
     "st_target_lufs": -24.7,
     "output_trim_db": 0.0,
     "level_src": "D clamp"
    },
    "STAB": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 120,
      "slope_db_oct": 12,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 10000,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 1200,
       "gain_db": 1.5,
       "q": 0.8,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -1.5,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 12,
      "release_ms": 170,
      "knee_db": 6,
      "target_gr_db": 7,
      "src": {
       "attack_ms": "M",
       "release_ms": "M",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 5,
      "mix_pct": 40,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -9.4
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": -1.2,
      "measured_style_tilt_db_oct": -5.52,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -0.6,
     "st_target_lufs": -24.3,
     "output_trim_db": 1.0,
     "level_src": "M"
    },
    "VOX": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 150,
      "slope_db_oct": 24,
      "src": "M"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 12000,
      "slope_db_oct": 12,
      "src": "N"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -2.5,
       "q": 1.2,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 3000,
       "gain_db": 2.0,
       "q": 1.0,
       "src": "M"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 5,
      "release_ms": 80,
      "knee_db": 6,
      "target_gr_db": 3,
      "src": {
       "attack_ms": "N",
       "release_ms": "N",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tape",
      "drive_db": 2,
      "mix_pct": 20,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "fallback UKMIN",
      "side_mid_db": -8.7
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "fallback UKMIN"
     },
     "lufs_offset_vs_kick_db": -4.0,
     "st_target_lufs": -27.7,
     "output_trim_db": 1.0,
     "level_src": "fallback UKMIN"
    },
    "HITS": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 220,
      "slope_db_oct": 24,
      "src": "A&R"
     },
     "lpf": null,
     "eq": [
      {
       "type": "bell",
       "freq_hz": 500,
       "gain_db": -2.0,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -20,
      "ratio": 4,
      "attack_ms": 11,
      "release_ms": 250,
      "knee_db": 6,
      "target_gr_db": 4,
      "src": {
       "attack_ms": "M(borrowed Gaskin stab)",
       "release_ms": "M(borrowed Gaskin stab)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 4,
      "mix_pct": 40,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 150,
      "corr_lt120_min": 0.98,
      "src": "A&R"
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -4.0,
     "st_target_lufs": -27.7,
     "output_trim_db": 0.0,
     "level_src": "A&R"
    },
    "PAD": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 150,
      "slope_db_oct": 12,
      "src": "N"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 8000,
      "slope_db_oct": 12,
      "src": "N"
     },
     "eq": [],
     "comp": {
      "threshold_dbfs": -24,
      "ratio": 2,
      "attack_ms": 30,
      "release_ms": 300,
      "knee_db": 6,
      "target_gr_db": 2,
      "src": {
       "attack_ms": "N",
       "release_ms": "N",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N"
      }
     },
     "sat": {
      "type": "none",
      "drive_db": 0,
      "mix_pct": 0,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "side_mid_min_db": -12,
      "side_mid_max_db": 0,
      "src": "N"
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": 0.0,
      "measured_style_tilt_db_oct": null,
      "src": "n/a"
     },
     "lufs_offset_vs_kick_db": -8.0,
     "st_target_lufs": -31.7,
     "output_trim_db": 1.0,
     "level_src": "PRESET_SPEC D"
    },
    "PLUCK": {
     "hpf": {
      "type": "hpf",
      "freq_hz": 120,
      "slope_db_oct": 12,
      "src": "D"
     },
     "lpf": {
      "type": "lpf",
      "freq_hz": 10000,
      "slope_db_oct": 12,
      "src": "M"
     },
     "eq": [
      {
       "type": "bell",
       "freq_hz": 1200,
       "gain_db": 1.5,
       "q": 0.8,
       "src": "M"
      },
      {
       "type": "bell",
       "freq_hz": 300,
       "gain_db": -1.5,
       "q": 1.0,
       "src": "D"
      }
     ],
     "comp": {
      "threshold_dbfs": -22,
      "ratio": 3,
      "attack_ms": 12,
      "release_ms": 110,
      "knee_db": 6,
      "target_gr_db": 7,
      "src": {
       "attack_ms": "M",
       "release_ms": "M(LONG other decay 106)",
       "threshold_dbfs": "N",
       "ratio": "N",
       "knee_db": "N",
       "target_gr_db": "N+crest-rule"
      }
     },
     "sat": {
      "type": "tube_asym",
      "drive_db": 5,
      "mix_pct": 40,
      "src": "N"
     },
     "width": {
      "mono_below_hz": 120,
      "src": "M",
      "side_mid_db": -9.4
     },
     "tilt": {
      "pivot_hz": 1000,
      "db_per_oct": -1.2,
      "measured_style_tilt_db_oct": -5.52,
      "src": "M"
     },
     "lufs_offset_vs_kick_db": -0.6,
     "st_target_lufs": -24.3,
     "output_trim_db": 1.0,
     "level_src": "M"
    },
    "DRUMS": {
     "_doc": "909 lane: per-voice chains; DRUMS bus has no extra processing in v1",
     "voices": {
      "CLAP": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 200,
        "slope_db_oct": 24,
        "src": "M"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 14000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 1500,
         "gain_db": 2.0,
         "q": 1.2,
         "src": "M-melak"
        },
        {
         "type": "bell",
         "freq_hz": 400,
         "gain_db": -2.0,
         "q": 1.5,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -20,
        "ratio": 4,
        "attack_ms": 3,
        "release_ms": 60,
        "knee_db": 3,
        "target_gr_db": 0,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 1,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -10.6
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -5.2,
       "st_target_lufs": -28.9,
       "output_trim_db": 1.0,
       "level_src": "M"
      },
      "SNARE": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 150,
        "slope_db_oct": 24,
        "src": "N"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 14000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 200,
         "gain_db": 2.0,
         "q": 1.2,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -20,
        "ratio": 4,
        "attack_ms": 3,
        "release_ms": 80,
        "knee_db": 3,
        "target_gr_db": 0,
        "src": {
         "attack_ms": "N",
         "release_ms": "N",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 1,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M(clap)",
        "side_mid_db": -10.6
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -5.2,
       "st_target_lufs": -28.9,
       "output_trim_db": 1.0,
       "level_src": "N(clap slot)"
      },
      "RIM": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 300,
        "slope_db_oct": 24,
        "src": "N"
       },
       "lpf": null,
       "eq": [
        {
         "type": "bell",
         "freq_hz": 1760,
         "gain_db": 2.0,
         "q": 2.0,
         "src": "M-ref"
        }
       ],
       "comp": {
        "threshold_dbfs": -22,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 150,
        "knee_db": 3,
        "target_gr_db": 2,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-ref",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "N"
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -10.2,
       "st_target_lufs": -33.9,
       "output_trim_db": 1.0,
       "level_src": "M-ref"
      },
      "CH": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 10000,
         "gain_db": 1.5,
         "q": 0.7,
         "src": "M"
        },
        {
         "type": "bell",
         "freq_hz": 4000,
         "gain_db": -2.0,
         "q": 2.0,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 45,
        "knee_db": 3,
        "target_gr_db": 3,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "tape",
        "drive_db": 2,
        "mix_pct": 20,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -8.0
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -18.1,
       "st_target_lufs": -41.8,
       "output_trim_db": 1.0,
       "level_src": "M split"
      },
      "OH": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 10000,
         "gain_db": 1.5,
         "q": 0.7,
         "src": "M"
        },
        {
         "type": "bell",
         "freq_hz": 4000,
         "gain_db": -2.0,
         "q": 2.0,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 3,
        "attack_ms": 2,
        "release_ms": 145,
        "knee_db": 3,
        "target_gr_db": 3,
        "src": {
         "attack_ms": "M",
         "release_ms": "M",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "tape",
        "drive_db": 2,
        "mix_pct": 20,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M",
        "side_mid_db": -8.0
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": -1.8,
        "measured_style_tilt_db_oct": -1.63,
        "src": "M"
       },
       "lufs_offset_vs_kick_db": -6.1,
       "st_target_lufs": -29.8,
       "output_trim_db": 1.0,
       "level_src": "M"
      },
      "RIDE": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 2000,
        "slope_db_oct": 24,
        "src": "PRESET_SPEC"
       },
       "lpf": null,
       "eq": [
        {
         "type": "highshelf",
         "freq_hz": 8000,
         "gain_db": 1.0,
         "q": 0.7,
         "src": "N"
        }
       ],
       "comp": {
        "threshold_dbfs": -24,
        "ratio": 2,
        "attack_ms": 2,
        "release_ms": 175,
        "knee_db": 3,
        "target_gr_db": 1,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-melak",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "tape",
        "drive_db": 2,
        "mix_pct": 20,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M(hats)",
        "side_mid_db": -8.0
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -24.1,
       "st_target_lufs": -47.8,
       "output_trim_db": 1.0,
       "level_src": "PRESET_SPEC R"
      },
      "PERC": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 250,
        "slope_db_oct": 24,
        "src": "D"
       },
       "lpf": {
        "type": "lpf",
        "freq_hz": 12000,
        "slope_db_oct": 12,
        "src": "N"
       },
       "eq": [
        {
         "type": "bell",
         "freq_hz": 2000,
         "gain_db": 1.5,
         "q": 1.0,
         "src": "M-LOW"
        }
       ],
       "comp": {
        "threshold_dbfs": -22,
        "ratio": 3,
        "attack_ms": 3,
        "release_ms": 75,
        "knee_db": 3,
        "target_gr_db": 0,
        "src": {
         "attack_ms": "N",
         "release_ms": "M-melak",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "none",
        "drive_db": 0,
        "mix_pct": 0,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 120,
        "src": "M-LOW",
        "side_mid_db": -15.3
       },
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": -11.2,
       "st_target_lufs": -35.0,
       "output_trim_db": 1.0,
       "level_src": "M"
      },
      "KICK": {
       "hpf": {
        "type": "hpf",
        "freq_hz": 28,
        "slope_db_oct": 24,
        "src": "D"
       },
       "lpf": null,
       "eq": [
        {
         "type": "bell",
         "freq_hz": 55,
         "gain_db": 1.5,
         "q": 1.0,
         "src": "D"
        },
        {
         "type": "bell",
         "freq_hz": 250,
         "gain_db": -2.0,
         "q": 1.4,
         "src": "D"
        }
       ],
       "comp": {
        "threshold_dbfs": -18,
        "ratio": 4,
        "attack_ms": 15,
        "release_ms": 150,
        "knee_db": 6,
        "target_gr_db": 5,
        "src": {
         "attack_ms": "M",
         "release_ms": "M(style decay)",
         "threshold_dbfs": "N",
         "ratio": "N",
         "knee_db": "N",
         "target_gr_db": "N+crest-rule"
        }
       },
       "sat": {
        "type": "softclip_tanh",
        "drive_db": 5,
        "mix_pct": 100,
        "src": "N"
       },
       "width": {
        "mono_below_hz": 141,
        "side_mid_max_db": -19.2,
        "src": "M",
        "side_mid_db": -34.6
       },
       "click_db_re_kick": -8,
       "tilt": {
        "pivot_hz": 1000,
        "db_per_oct": 0.0,
        "measured_style_tilt_db_oct": null,
        "src": "n/a"
       },
       "lufs_offset_vs_kick_db": 0.0,
       "st_target_lufs": -23.7,
       "output_trim_db": 0.0,
       "level_src": "D anchor"
      }
     }
    }
   }
  }
 },
 "preset_overrides": {
  "_doc": "Applied on top of dialects[d].lanes[lane]. Only keys in allowed_keys may be overridden; level keys (lufs_offset_vs_kick_db, st_target_lufs, output_trim_db) and KICK/BASS width.mono_below_hz are locked. eq entries are replaced by index.",
  "allowed_keys": [
   "hpf.freq_hz",
   "hpf.slope_db_oct",
   "lpf.freq_hz",
   "lpf.slope_db_oct",
   "eq[i].freq_hz",
   "eq[i].gain_db",
   "eq[i].q",
   "comp.attack_ms",
   "comp.release_ms",
   "comp.ratio",
   "comp.knee_db",
   "comp.target_gr_db",
   "sat.type",
   "sat.drive_db",
   "sat.mix_pct",
   "width.side_mid_db",
   "click_db_re_kick",
   "layers"
  ],
  "locked_keys": [
   "lufs_offset_vs_kick_db",
   "st_target_lufs",
   "output_trim_db",
   "KICK/BASS width.*",
   "KICK/BASS hpf.freq_hz"
  ],
  "presets": {
   "BASS:SUB": {},
   "BASS:ROUND": {},
   "BASS:CLICK": {
    "comp.release_ms": 100,
    "src": "M POP bass decay 96 ms"
   },
   "BASS:REESE": {
    "layers": {
     "sub": "BASS chain",
     "reese": {
      "hpf.freq_hz": 220,
      "hpf.slope_db_oct": 24,
      "width.mono_below_hz": 150,
      "comp.ratio": 3,
      "comp.attack_ms": 10,
      "comp.release_ms": 120,
      "sat.drive_db": 6,
      "sat.mix_pct": 40
     }
    },
    "src": "A&R HPF 220 / mono; rest N; 120 ms = 1/16 at 126 BPM"
   },
   "STAB:DUB": {
    "comp.release_ms": 110,
    "src": "M LIGHTLEAK decay 106"
   },
   "STAB:CHORD": {
    "comp.release_ms": 180,
    "src": "M V-Line 180.5 / Ellia 180"
   },
   "STAB:GASKIN_RAVE": {
    "comp.release_ms": 250,
    "comp.target_gr_db": 7,
    "src": "M Gaskin decay 254, crest 16.8"
   },
   "STAB:POP_CLICK": {
    "comp.release_ms": 125,
    "comp.target_gr_db": 5,
    "src": "M POP decay 126, crest 19.0"
   }
  }
 },
 "dialect_switch": {
  "_doc": "MIX_SPEC §9.2. On a dialect switch every parameter ramps from old to new value; no instantaneous coefficient swap anywhere. All ramps start at the switch sample and use a raised-cosine shape.",
  "shape": "raised_cosine",
  "ramp_ms": {
   "output_trim_db": 50,
   "top_trim_db": 50,
   "lufs_offset_vs_kick_db": 50,
   "hpf.freq_hz": 50,
   "lpf.freq_hz": 50,
   "eq.freq_hz": 50,
   "eq.gain_db": 50,
   "eq.q": 50,
   "tilt.db_per_oct": 50,
   "width.side_mid_db": 50,
   "click_db_re_kick": 50,
   "comp.threshold_dbfs": 120,
   "comp.ratio": 120,
   "comp.knee_db": 120,
   "comp.attack_ms": 120,
   "comp.release_ms": 120,
   "comp.target_gr_db": 120,
   "sat.drive_db": 80,
   "sat.mix_pct": 80,
   "sat.type": 80,
   "hpf.slope_db_oct": 80,
   "lpf.slope_db_oct": 80,
   "eq.type": 80
  },
  "interpolation": {
   "gain-like (dB keys)": "linear in dB",
   "freq_hz": "linear in log2(Hz); filters are TPT/SVF recomputed per sample (or per 16-sample block with per-sample coefficient interpolation)",
   "q": "linear in ln(Q)",
   "comp.*": "the parameter value itself is ramped (detector state kept); ratio ramped in 1/ratio",
   "tilt/width": "linear",
   "discrete (sat.type, slope, eq.type)": "dual-path: old and new paths both run for the ramp, output = correlation-compensated equal-power crossfade"
  },
  "equal_power_law": "g_old=cos(theta), g_new=sin(theta), theta 0..pi/2 raised-cosine, output /= sqrt(1 + 2*rho*sin(theta)*cos(theta)); rho = correlation of the two paths over the last 50 ms (rho~1 for dry vs saturated -> behaves like equal-gain; rho~0 -> plain equal-power)",
  "sources": {
   "50 ms": "D: settles inside one 1/16 at 126 BPM (119 ms); peak slope pi/2 x 8.3 dB (largest lane-offset change, STAB LONG->GASKIN) / 50 ms is a ramp, not a step (self-test PASS)",
   "120 ms": "D: about 1/16 at 126 BPM and inside the measured comp release range (45-250 ms)",
   "80 ms": "N"
  },
  "test": {
   "mode": "mix_check.py switch",
   "max_gain_jump_db": 0.5,
   "max_click_excess_db": 12.0,
   "frame_ms": 20,
   "stimulus": "mix_check.py stimulus"
  }
 },
 "main_auto_trim": {
  "_doc": "PRESET_SPEC v1.3a §0.5.1 (Q13, Maestro) + engine rule E21; MIX_SPEC §9.4. A fixed sum trim at the MAIN output keyed by dialect x active-MAIN-lane mask.",
  "table": "presets/main_sum_trim.json",
  "key": "dialects.<D>.by_mask.<mask>.trim_db",
  "lane_bit_order": [
   "BASS",
   "STAB",
   "PLUCK",
   "HITS",
   "PAD",
   "VOX"
  ],
  "apply_at": "MAIN output, after dialects.<D>.output_trim_db; moves every lane equally (kick/bass invariants and lane balance untouched)",
  "default_set_trim_db": 0.0,
  "no_double_count": "trim is 0 at each dialect default set; output_trim_db alone tunes that set to -19.0 from renders. The table field default_model_residual_db is informational and must NOT be applied (PRESET_SPEC v1.3c).",
  "active": "lane unmuted and its pattern has notes in the current bar",
  "ramp": {
   "ms": 50,
   "shape": "raised_cosine",
   "interp": "linear in dB",
   "restart": "from current value"
  },
  "lane_off": "start the ramp when the lane output has fallen 40 dB below its level, max 500 ms after note-off",
  "lane_on": "start the ramp at the first note-on",
  "solo_render": "bypassed (per-lane targets are measured without it)",
  "gate": {
   "sum_st_lufs": -19.0,
   "tol_lu": 0.5,
   "sum_tp_dbtp": -6.0,
   "tp_tol_db": 1.0,
   "applies_to": "any active-lane combination",
   "dense": {
    "min_active_lanes": 3,
    "st_window_lufs": [
     -19.5,
     -18.5
    ]
   },
   "sparse": {
    "max_active_lanes": 2,
    "st_window_lufs": [
     -20.5,
     -18.5
    ],
    "note": "1-2 active MAIN lanes; drums-only (0 lanes) also uses the sparse window"
   },
   "tp_cap_dbtp": -5.0,
   "tp_cap_scope": "every lane set",
   "sum_limiter": "none",
   "decided": "Maestro 2026-10-17, E21 option (a)"
  },
  "headroom_guard": "a positive trim that would push the sum over -5.0 dBTP is capped at the largest passing value and logged (limiter GR must stay 0)",
  "verify": "mix_check.py combos (MIX_SPEC §9.4)",
  "src": "PRESET_SPEC v1.3a"
 },
 "engine_contracts": {
  "_doc": "Engine behaviour the render tests enforce. MIX_SPEC §5.1.",
  "mute_leak": {
   "priority": "P0",
   "rule": "a muted lane contributes nothing to any output",
   "test": "mix_check.py mute",
   "pass": {
    "resid_max_dbfs_rms": -90.0,
    "resid_max_re_lane_db": -60.0,
    "every_band": true
   },
   "render_set": "per dialect (at minimum UKMIN): FULL.wav + MUTE_<LANE>.wav + SOLO_<LANE>.wav for each lane, float32, no dither, sample-aligned",
   "src": "Maestro 2026-10-17"
  }
 },
 "sample_prep_acceptance": {
  "_doc": "PRESET_SPEC v1.3a Q15/Q16 (Maestro). MIX_SPEC §4.",
  "tail_fade_ms_allowed": [
   12,
   24,
   48,
   96,
   192
  ],
  "tail_fade_note": "12 ms default; longer (24-192 ms) accepted where 12 ms cannot bring the last 10 ms <= -60 dBFS (Q15)",
  "tp_capped": "files at the -6 dBTP cap stay as-is, no per-file limiter; LUFS may be below -24; not a fail (Q16)",
  "tp_cap_dbtp": -6.0,
  "tp_tol_db": 0.05
 },
 "tp_guard": {
  "stage": "MAIN, after the E21 trim; safety only, not the E21 sum limiter",
  "counts_as_sum_limiter": false,
  "oversample": 4,
  "detector_taps_per_phase": 24,
  "lookahead_ms": 1.5,
  "release_ms": 80.0,
  "ceiling_dbtp": -6.0,
  "margin_db": 0.2,
  "max_gr_db_factory": 1.0,
  "sidecar_key": "tp_guard_gr_db_max"
 }
}
```

### 8.1 Validation result
`python3 tests/validate_profiles.py` → **pass = true, 328 checks, 0 fails** (17 Oct 2026, about 1:14 PM ET, schema 1.2.0). The checks now include the E21 option (a) gate (profiles vs targets), the trim table's default-mask zero, the P0 mute-leak contract, and the §5.1 / §9.4 text. The first run on 17 Oct (311 checks) caught 2 mismatches, both since fixed: a GASKIN PERC rounding difference (0.1 LU) and a §2.3 VOX LONG tilt-setting typo (+2.9 → +2.8).

## 9. Preset override layering and dialect-switch crossfade

### 9.1 Layering

`effective(lane) = dialects[d].lanes[lane]` **⊕ `preset_overrides.presets[preset]`** **⊕ user macro offsets**, where ⊕ means a key-wise replace.

**Load-time check.** A preset may only set keys listed in `preset_overrides.allowed_keys`:
- filter freq / slope
- `eq[i].*` (replaced by index)
- comp attack / release / ratio / knee / target GR
- sat type / drive / mix
- `width.side_mid_db`, `click_db_re_kick`, `layers`

**Locked keys** (the level and low-end invariants of §2.1): `lufs_offset_vs_kick_db`, `st_target_lufs`, `output_trim_db`, every KICK/BASS `width.*`, and KICK/BASS `hpf.freq_hz`.
- A preset that sets one of these is rejected at load.
- Preset loudness differences are removed by the harness trim, so a preset changes tone, never balance.

**Order on a dialect switch.**
1. Recompute `effective()` with the new dialect, keeping the same preset.
2. Ramp every changed value per §9.2.

The preset therefore stays on top of whichever dialect is active. Example: `STAB:GASKIN_RAVE` keeps release 250 ms and GR 7 on any dialect, while its level follows the dialect's STAB offset.

### 9.2 Crossfade on dialect switch (no instantaneous coefficient swaps)

| Parameter group | Ramp | Curve / method | Source |
|---|---|---|---|
| Gains: `output_trim_db`, `top_trim_db`, `lufs_offset_vs_kick_db`, `eq.gain_db`, `click_db_re_kick` | **50 ms** | raised-cosine, linear in dB | [D] settles inside one 1/16 at 126 BPM (119 ms). The largest per-lane jump (STAB LONG −8.9 → GASKIN −0.6, 8.3 dB) is still a ramp; the self-test passes |
| Filter / EQ frequencies (`hpf/lpf/eq.freq_hz`) | **50 ms** | linear in log2 f (exponential in Hz). **TPT/SVF topology**, cutoff recomputed every sample (or per 16-sample block with per-sample coefficient interpolation). DF1/DF2 biquads may not be modulated | [D] self-test: an SVF log-sweep shows no click; a biquad or SVF hard swap clicks |
| Q | 50 ms | linear in ln Q | [N] |
| Tilt, width S/M | 50 ms | linear | [N] |
| Compressor threshold, ratio (ramped as 1/ratio), knee, attack, release, target GR | **120 ms** | Raised-cosine *parameter* smoothing. The detector and gain-computer state carry through, so the GR envelope is never reset | [D] ≈ 1/16 at 126 BPM; inside the measured release range 45–250 ms |
| Saturation drive and mix | 80 ms | parameter ramp | [N] |
| Discrete changes (sat type, filter slope 12↔24, EQ type) | **80 ms** | **Dual-path:** old and new paths run in parallel; the output is an **equal-power crossfade, correlation-compensated**: g_old = cos θ, g_new = sin θ, output ÷ √(1 + 2ρ·sinθ·cosθ), with ρ = path correlation over the last 50 ms | [D] self-test: plain cos/sin on dry vs saturated paths (ρ = 0.9998) **bumps 1.8 dB above the ramp** and fails. The compensated law passes (−0.09 dB) |
| Kick/bass invariants | n/a | No change on a switch: their levels and mono are dialect-independent (§2.1). Only GR, sat and click ramp | n/a |

**Behaviour.**
- The switch applies at the next sample (no bar quantise).
- All ramps start together; the output has settled 120 ms after the switch.
- A switch arriving mid-ramp starts a new ramp from the current values. There is no jump back to the target.

### 9.3 Click test (`mix_check.py switch`)

**Protocol**
1. `mix_check.py stimulus stim.wav --seconds 8`. This writes a synthetic 48 kHz line-spectrum stimulus: 50 Hz grid, 50 Hz–4 kHz, −3 dB/oct, peak −12 dBFS, exactly periodic every 20 ms. It is the only audio the script ever writes, and it is not reference audio.
2. Load it as the lane's sample (every lane can play a sample), solo the lane, start in dialect A, switch to B at S ≥ 0.6 s, and render ≥ S + ramp + 0.6 s.
3. Run `mix_check.py switch render.wav --switch-at S --ramp-ms 50 --dialect "A>B" --json out.json`.

Run it for every lane/voice × every dialect pair, both directions. Also run it on the SUM with the 7 default lanes.

**Checks**

| Check | Method | Threshold |
|---|---|---|
| Gain jump | 20 ms frame level (1 ms hop): the stimulus makes the steady level exactly constant. Max level change between frames 20 ms apart, minus the declared ramp's own peak slope (π/2 × total change × 20 ms / ramp) | **≤ 0.5 dB** |
| Click / sample-step discontinuity / coefficient-swap transient | The stimulus has nothing above 4 kHz, and smooth ramps create nothing there. Energy above 8 kHz (8th-order Butterworth HP, zero-phase) in 5 ms windows during the switch, re the stimulus RMS, minus max(steady pre/post floor, −100 dB). The floor absorbs the steady saturation harmonics | **≤ 12 dB** above the floor |

**Self-test** (synthetic, `mixeng-scratch/tools/switch_selftest2.py`/`3.py`, 50 ms declared ramp). The 12 dB threshold sits between the worst smooth case (+5.6) and the mildest hard case (+26.0):

| case | total dB | gain-jump excess dB | click excess dB | verdict |
|---|---|---|---|---|
| none | 0.00 | 0.00 | -31.4 | PASS |
| gain_lin50 | -6.00 | -1.37 | -4.9 | PASS |
| gain_smooth50 | -6.00 | -0.51 | -31.4 | PASS |
| svf_sweep_log50 | -0.18 | 0.00 | -31.4 | PASS |
| sat_eqpow_corrcomp80 | -0.28 | -0.09 | -0.1 | PASS |
| sat_eqpow80 | -0.28 | 1.81 | -0.0 | FAIL |
| all_spec | -6.25 | -1.76 | -3.9 | PASS |
| ramped_all | -5.57 | -1.46 | 5.6 | PASS |
| gainstep_2dB | -2.00 | 0.74 | 54.6 | FAIL |
| gainstep_0.3dB | -0.30 | 0.11 | 39.0 | FAIL |
| coefswap | -0.18 | 0.09 | 67.6 | FAIL |
| svf_swap | -0.18 | 0.07 | 40.6 | FAIL |
| sat_hardswitch | 0.40 | 0.15 | 26.0 | FAIL |

### 9.4 MAIN auto-trim (rule E21, PRESET_SPEC v1.3a §0.5.1, Q13; gate DECIDED by Maestro 2026-10-17, option (a))

**What it is.** A fixed trim at the MAIN output, looked up from `presets/main_sum_trim.json` (`dialects.<D>.by_mask.<mask>.trim_db`).
- The key is dialect × the 6-lane active mask. The bit order is BASS, STAB, PLUCK, HITS, PAD, VOX, which gives 64 entries per dialect. Drums are always on.
- The trim is **0 dB at each dialect's default set**: UKMIN / LONG / POP use BASS+STAB+VOX (mask 35), and GASKIN uses BASS+STAB+HITS (mask 11).
- The table is generated from `mix_profiles.json` `st_target_lufs`: trim = model(default) − model(active), with a power-sum of the lane targets plus the per-style TOP trims. When STAB and PLUCK are both on, each is −3 dB.
- The profile pointer is `mix_profiles.json` → `main_auto_trim`.

**Layering.** `MAIN out = Σ lanes → dialect output_trim_db → E21 trim → (limiter, GR must stay 0)`.
- `output_trim_db` alone lands the default set at −19.0 from renders. E21 adds 0 there, so nothing is counted twice.
- The table's `default_model_residual_db` field (PRESET_SPEC v1.3c; UKMIN −0.04, LONG +0.33, POP −0.35, GASKIN −0.51) is the model's residual. It is informational and **never applied** by the engine.
- E21 moves every lane equally, so the kick/bass invariants (§2.1, including the DECIDED clamp) and the lane balance are untouched.

**Timing.**
- "Active" means the lane is unmuted and its pattern has notes in the current bar.
- The trim moves on a **50 ms raised-cosine ramp, linear in dB**, restarting from its current value.
- **Lane off:** the ramp starts when that lane's output has fallen 40 dB below its level, at most 500 ms after note-off, so a release tail is never boosted.
- **Lane on:** the ramp starts at the first note-on.
- **Solo renders bypass E21**, so the per-lane targets in §2.2 are measured without it.
- This is the same ramp family as §9.2, so the §9.3 click test applies. Render a mask change with the §9.3 stimulus and run `switch` with `--ramp-ms 50`.

**Gate (any lane set): DECIDED (Maestro, 2026-10-17), E21 option (a).**
- **3 or more active MAIN lanes:** SUM ST −19.0 ±0.5 LUFS-S (−19.5 … −18.5).
- **Sparse sets of 1–2 active MAIN lanes:** −19 +0.5/−1.5 LUFS-S (−20.5 … −18.5). Drums-only (0 MAIN lanes) is even sparser, so I applied the sparse window to it as well. That is my reading; under a strict ±0.5 for drums-only, 2 more combos would fail (UKMIN −20.26, LONG −19.62).
- **TP:** −6 ±1 dBTP, with the **−5 dBTP cap kept for every lane set**. There is **no sum limiter**, and limiter GR stays 0.
- **Headroom guard:** a trim that would push the sum over −5 dBTP is capped at the largest value that passes, and the cap is logged. The ST that results must still sit inside the window for its lane count.

**Table verification** (`combos` mode, re-run 17 Oct 2026 about 1:12 PM ET on Sound Designer's v1.3c table (1:04 PM); `tests/results/combos_verify_optionA.json`, full rows in `tests/results/trim_conflicts_optionA.csv`).
- Sound Designer's table (v1.3c) now takes the per-lane-set calibration from `targets.json` `combo_model` (`calibration_db_by_class`). UKMIN drums-only is now +1.94, which matches the measured-cal value.
- **Recompute:** I recompute class-aware. **256/256 entries match** (|table − recomputed| ≤ 0.005 dB; max 0.00 dB).
- **Double count:** the default-mask trim is 0.00 in every dialect, so nothing double-counts.
- **Method for the predicted sum:** ST_pre = −19 + (table − recomputed). TP_pre = −6 + PLR(class) − PLR(default class) + (ST_pre + 19). The guard cut is max(0, TP_pre + 5), and ST_after = ST_pre − cut.

| Dialect | guard bites (of 64) | gate fails (of 64) | failing combos (ST after the guard vs window) |
|---|---|---|---|
| UKMIN | 32 | **0** | none |
| LONG | 32 | **16** | the 3–5-lane sets with STAB/PLUCK/HITS/PAD and no BASS: 5 sets at −19.53 (TP_pre −4.47) and 11 sets with VOX at −19.58 (TP_pre −4.42), vs ≥ −19.5 |
| POP | 19 | **2** | drums-only −20.68, VOX alone −20.54, vs ≥ −20.5 |
| GASKIN | 4 | **2** | drums-only −20.58, VOX alone −20.57, vs ≥ −20.5 |

**Total: 20 of 256 combos fail; under the old ±0.5 gate it was 40.** Every remaining fail misses by 0.03–0.18 LU. Each one is a peak-limited set: its ST is set by the −5 dBTP cap minus the set's PLR, and the trim cannot fix that. A fix has to lower the PLR of those sets (for example, transient control on the top lanes in LONG) or come from real renders that show a lower PLR than the reference stems.
**Caveat:** the PLRs come from mastered reference stems. These are estimates, and real `COMBO_*.wav` renders decide (`mix_check.py combos <dir>` gates them with the lane-count window).

### 9.4b Master true-peak guard (`tp_guard`; DECIDED by Maestro 2026-10-18)

A safety stage on MAIN after the E21 trim: `MAIN out = Σ lanes → output_trim_db → E21 trim → tp_guard`. It is **not** the E21 sum limiter. The E21 rule above ("no sum limiter, limiter GR stays 0") is unchanged, and `tp_guard` is reported on its own.
- **What it does:** 4× oversampled true-peak detection (24-tap Kaiser sinc per phase), 1.5 ms lookahead, 80 ms release, ceiling **−6 dBTP** with a 0.2 dB internal margin. Below the ceiling the gain is exactly 1, so the sum passes bit for bit, only delayed.
- **Latency:** fixed, 95 samples at 48 kHz, the same with the guard off. The plugin reports it to the host, and the DRUMS bus rides the same delay.
- **Code:** `src/Mix/TruePeakGuard.h`, `EngineParams.tpGuard` (default on), `Engine::takeTpGuardGrDb()`. Settings: `mix_profiles.json` → `tp_guard`. `validate_profiles.py` checks that the JSON and the header agree.
- **Bar:** on the factory renders, peak guard GR stays ≤ 1 dB (`max_gr_db_factory`). When a render writes `tp_guard_gr_db_max` to its `.gr.json` sidecar, `mix_check.py` gates on it. E21 (§9.4) is what holds the sum under −6 dBTP; the guard should rarely engage.
- **Test:** `rolla-tp-test` covers 4 styles × INTENSITY 0/25/50/75/100 plus a +12 dB hot pass, all ≤ −6 dBTP on the `mix_check.py` meter (scipy `resample_poly` 4×), along with inter-sample, noise, square, impulse, burst and bypass cases.
- **Mute null (§9.5):** the guard is nonlinear by design, so the FULL / MUTE / SOLO null takes run with `tpGuard = false`. That line goes into `src/MixRender.cpp` after #12 lands.

### 9.5 Tests that exist
`render` / `sample` / `scan` (§5), `styles` (§5, including the any-combo SUM gate with the lane-count window), `mute` (§5.1, P0), `switch` (§9.3), `combos` (§9.4).

`--dialect` (alias `--style`) selects the per-dialect targets in `render` / `sample` and labels `switch` runs.
