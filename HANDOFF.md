# ROLLA handoff

Stopped before the mix-strip request. Nothing from that request is in the tree. HEAD builds. Last commit with code is `d9c99fd` (sample playback). `026f49d` is an empty snapshot. Do not keep implementing the strip in this branch until MIX_SPEC.md arrives; another engineer owns it.

Do not commit `uploads/`, WAVs, or concept screenshots. `*.wav`, `*.aif`, `*.aiff`, and `*.flac` are gitignored. Local compiler must stay gcc/g++ (`/usr/bin/c++` is Clang and fails to link `-lstdc++`). Configure already exists in `build/`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --target ROLLA_VST3 rolla-render rolla-sample-check
./build/rolla-sample-check
./build/rolla-render renders
```

Linux VST3: `build/ROLLA_artefacts/Release/VST3/ROLLA.vst3`

JUCE 8.0.15 via FetchContent. C++17. VST3 + Standalone; AU on Apple. `EDITOR_WANTS_KEYBOARD_FOCUS` stays false.

## Module layout

| Path | Role |
| --- | --- |
| `src/PluginProcessor.*` | APVTS, state, generation trigger, MIDI export, drum/vox file load, sample-folder root, pool pick |
| `src/PluginEditor.*` | 1024×640 scaled editor, logo menu (scale, reduce motion, samples folder), lane warning triangle |
| `src/UiTokens.h` | Colour tokens and lane helpers |
| `src/RollFace.h` | Face paint, DARK brow ±6°, reduce-motion snap |
| `src/Engine/Engine.*` | Realtime synth, two buses (MAIN + DRUMS), dropped drum WAVs, calls the sampler |
| `src/Engine/SampleSpec.h` | Preset `sample` block: mode, file, root, pool, zones, loop, drum `voices` |
| `src/Engine/SampleLibrary.*` | Background loader. Audio thread only reads atomics. No samples in the binary |
| `src/Engine/Sampler.*` | Melodic + drum sample voices. 16-tap windowed sinc, ADSR, ladder, steal fade |
| `src/Generator/Generator.*` | Seeded patterns, dialect table |
| `src/Generator/Presets.*` | `presets.json` schema v3, `Extras`, sample block |
| `src/Generator/Pattern.h` | Lanes, drum voice order, part ids |
| `src/Generator/MidiFile.*` | Per-part + combined MIDI, 480 PPQ |
| `src/DspUtil.h` | Swing, polyBLEP, SVF, ladder, DC block |
| `src/RenderMain.cpp` | Headless `rolla-render` (dialect WAVs + A&R checks) |
| `src/SampleCheck.cpp` | Headless `rolla-sample-check` (note names, load, click, missing-file fallback) |
| `resources/presets.json` | Factory presets, schema v3, `sample` block on every preset (`mode` SYNTH) |
| `resources/fonts/` | Instrument Sans + Geist Mono, embedded as BinaryData |
| `.github/workflows/build.yml` | macOS universal / Windows / Linux. Do not rewrite it in this stop |

## Done

- Seven lanes: BASS, STAB, HITS, PLUCK, PAD, VOX, 909 DRUMS. Synthesized. A dropped WAV still replaces one drum voice (linear resample into the old drum slot).
- Host transport / BPM via `AudioPlayHead`. Loop phase-locked to PPQ. `producesMidi`.
- Swing 50–57% on playback and MIDI export (not baked). Duck follows the kick, or the quarter grid when drums are muted.
- Dialect table, macros, LENGTH, FILL, seeds in plugin state. Automatable params do not reroll. Generation is `callAsync` and epoch-guarded.
- Editor draws in a 1024×640 space with an affine scale. Logo menu: 75/100/125/150/200%. Corner resizer keeps 1024:640, min 768×480, max 2048×1280. Size is `editorWidth` / `editorHeight` in the project and in `ROLLA.settings`. Default is 75% when the display `userBounds` height is under 1100.
- `roll` is automatable (rising edge). Space rolls when the editor has keyboard focus. ROLL paints at 35% and ignores input when every lane is kept.
- Per-voice drum params `dTune{v}` `dDecay{v}` `dLevel{v}` `dAccent{v}` `dAccAmt{v}` (v = 0..8). Identity at the defaults (tune 0 st, decay 0.5, level 0 dB, accent off). `processBlock` must keep writing all 9 `drumDecay` values.
- Sample playback (`d9c99fd`), personal packs, not embedded:
  - Lane preset mode `SYNTH`, `SAMPLE`, or `LAYER`.
  - Root key and pitch tracking (scientific pitch, C4 = 60, D1 = 26). Start offset in seconds. Amp envelope and ladder from the lane patch. Reverse. Loop with equal-power crossfade. `slice: true` on vox starts at the next detected transient.
  - `pool` rotates on SHUFFLE and ROLL (`samplePick{n}` in the project state). Optional `zones` for multisamples. Drum kits use `voices.kick` … `voices.ride`.
  - Missing file: synth stays, lane draws a warning triangle. No silence, no crash.
  - Load root: macOS `~/Music/ROLLA/Samples/`, Windows `%USERPROFILE%\Music\ROLLA\Samples\`, Linux `~/Music/ROLLA/Samples/`. Logo menu **Samples folder…** stores `samplesRoot` in `ROLLA.settings`. Subfolders `BASS STAB HITS PLUCK PAD VOX` and `DRUMS/{KICK,CLAP,SNARE,RIM,CH,OH,RIDE,PERC}` are created on launch. `CH` closed hat, `OH` open hat, `PERC` is storage only.
  - Background thread only. Fixed slot table, retired buffers kept until the library dies. Cap 16 seconds per file.
  - `rolla-sample-check` passed: D1/C4 names, sine peak 0.063 with sample-to-sample jump 0.0036, missing `SAMPLE` preset still produced synth energy 11.5.
  - `ROLLA_VST3` linked after that commit.

Factory `sample.mode` is `SYNTH` with an empty `file`. `rolla-render` does not set `EngineParams.library`, so a headless render never opens the sample folder.

## Stubbed

- **Mix strip (the request that was just stopped).** Not started. No HPF/LPF, 2-band EQ, compressor, lookahead, soft clip, or fixed output trim. No per-909-voice strip. No loudness JSON and no per-lane LUFS/true-peak render test. MIX_SPEC.md was not in the tree. Build it data-driven from preset JSON, no new knobs. Defaults must be real processing, then the mix engineer replaces the numbers.
- **Sample prep.** `SampleLibrary::loadOne` and `loadDrumFile` keep the raw file. No DC removal, no leading-silence trim, no end fade, no loudness gain. Do this on the loader thread, never on the audio thread.
- **Vox drop.** `loadVoxFile` stores `voxSampleName` and does not play. The sampler path is the one that plays vox, from `presets.json`, not from that slot.
- **MIX_SPEC extras.** `Extras` parses `chorusMix`, `chorusRateHz`, `monoBelowHz`, `reverbSeconds`, `delaySendDb`, `reverbSendDb`, `vowelTo`, `morphMs`, `size`. The mix does not read them. The plate is hardcoded (`roomSize` 0.90, damping 0.30, width 0.68). `delaySendDb` / `reverbSendDb` of −99 means “use the legacy linear send”.
- **`hitsInStab`.** Parameter and switch only. The generator ignores it.
- **909 panel.** Tom low/mid are generated and stored, not shown. Rim and mid tom can still take a dropped WAV. Hats and ride are synthesized unless a WAV or a kit `voices` entry is loaded.

## Known issues

- A&R in `rolla-render` (`analyseSound`: bass centroid 73–200 Hz, under-60 Hz share, stab centroid/tail/correlation, hits about 3 LU under bass, bar-line gate, clap tail, hard-cut detector) was last measured at `ddebb80`. It was not re-run after the drum-panel multipliers or after `d9c99fd`. Do not claim those numbers still pass.
- pluginval strictness 5 was last run on the provisional editor (`4bfe6fb`). It was not re-run after the scaler or the sampler. `roll` is a rising edge; pluginval will randomize it.
- Dropped drum WAVs still use the old linear resample slot. Preset kit samples use the sinc sampler. A dropped WAV wins over `voices.*`.
- The sinc is a custom 16-tap windowed kernel, not JUCE `WindowedSincInterpolator` (that one adds about 100 samples of latency).
- Sample steal fades the new voice in over 64 samples. The stolen voice is cut. Good enough for the sine check, not a crossfade of the old tail.
- `SampleLibrary` never frees a published buffer until the plugin is destroyed. Changing the samples folder does not unload the previous set.
- An empty `SAMPLE`/`LAYER` path is “missing” immediately and shows the warning. A path that is still loading does not.
- This VM has not produced a green macOS or Windows build. Workflow is `.github/workflows/build.yml` (`macos-14`, universal `arm64;x86_64`, deployment 11.0). Push to `github.com/mabe8056/rolla` failed here (`GH_TOKEN` / `GITHUB_TOKEN` unset). The bundle is the handoff artifact.

## Schema the sound designer already has

`resources/presets.json` is schema 3. Paths are relative to the samples root, forward slashes.

```json
"sample": {
  "mode": "SYNTH",
  "file": "BASS/pluck_d1.wav",
  "root": "D1",
  "start": 0.0,
  "reverse": false,
  "slice": false,
  "loop": { "on": false, "start": 0.0, "end": 0.0, "xfade": 0.0 },
  "layerMix": 0.5,
  "pool": ["BASS/a.wav", "BASS/b.wav"],
  "zones": [{ "file": "BASS/low.wav", "root": "D1", "lo": "C1", "hi": "F1" }]
}
```

Drums add `voices.kick`, `snare`, `clap`, `rim`, `tomLow`, `tomMid`, `hatC`, `hatO`, `ride`. Factory roots: bass D1, stab D3, hits C3, pluck D4, pad D3, vox C4 (`slice` true), drums C3. `loop.end` 0 means the file end. `layerMix` 0 is synth, 1 is sample.

## Queued (other engineers, in parallel)

- **Mix strip + sample loudness prep + JSON loudness render test.** Spec above. No UI knobs. Per lane after the voice and before sends/outputs. Same chain per 909 voice. MIX_SPEC.md will replace the default numbers.
- **MIX_SPEC extras** already parsed in `Extras` (chorus, mono-below, plate seconds, dB sends). Wire them without moving the `ddebb80` render until the new strip targets exist.
- **CI / Mac.** Do not invent an Actions URL. `auval -v aumu Roll Rola` (subtype `Roll`, manufacturer `Rola`).
- **Tests.** Re-run `rolla-render` and pluginval 5 on the current tree before calling A&R done.
