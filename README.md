# ROLLA

ROLLA is a JUCE 8 instrument that writes and plays a UK minimal / deep tech groove. Seven lanes — bass, quartal stab, hits, pluck, pad, vox cuts, and a circuit-style 909 drum lane — are synthesized. Nothing in the plugin is a Roland sample. Drop a WAV on a drum voice to replace that model. A lane preset can also play files from your own sample folder (see Samples).

Host transport and BPM drive an 8-bar (or 2/4) loop locked to PPQ. Swing (50–57%) shifts every lane's off-sixteenths. Fill rewrites the last bar the next time you roll.

## Build

JUCE 8.0.15 is fetched by CMake. You need a C++17 compiler, CMake 3.22+, and the usual JUCE Linux packages (ALSA, FreeType, X11, OpenGL).

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
cmake --build build --target ROLLA_VST3 ROLLA_Standalone rolla-render
```

`rolla-render` writes an 8-bar 48 kHz WAV per dialect at 130 BPM:

```bash
./build/rolla-render renders
```

Linux VST3: `build/ROLLA_artefacts/Release/VST3/ROLLA.vst3`

If `/usr/bin/c++` is Clang and the link fails with `cannot find -lstdc++`, pass `-DCMAKE_CXX_COMPILER=g++`.

### pluginval

Strictness 5 passed locally on the Linux VST3 (editor, audio at 44.1/48/96 kHz, state, automation). The VST3 validator step inside pluginval is skipped unless you pass a validator binary; that is pluginval's own behaviour.

```bash
xvfb-run -a pluginval --strictness-level 5 --timeout-ms 300000 --validate-in-process \
  build/ROLLA_artefacts/Release/VST3/ROLLA.vst3
```

## Install in FL Studio

The Mac and Windows binaries are produced by `.github/workflows/build.yml`. This environment cannot push to GitHub (see below), so those Actions artifacts are not on the repo yet. After the macOS job is green, download **ROLLA-macOS-universal**.

### Mac

1. Unzip `ROLLA-macOS-universal.zip`. It contains `ROLLA.vst3` and `ROLLA.component` (universal arm64 + x86_64, ad-hoc signed).
2. Copy `ROLLA.vst3` to `~/Library/Audio/Plug-Ins/VST3/`.
3. Copy `ROLLA.component` to `~/Library/Audio/Plug-Ins/Components/`.
4. Clear quarantine and re-sign if macOS blocks the scan:

```bash
xattr -cr ~/Library/Audio/Plug-Ins/VST3/ROLLA.vst3 ~/Library/Audio/Plug-Ins/Components/ROLLA.component
codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/VST3/ROLLA.vst3
codesign --force --deep --sign - ~/Library/Audio/Plug-Ins/Components/ROLLA.component
```

5. In FL Studio: **Options > Manage plugins > Find plugins**. Rescan, then add **ROLLA** as an instrument.
6. Press play. The pattern follows FL's transport and tempo. The plugin's Preview button is for the standalone app; FL's play button is the one that matters.
7. AU check, if you want it: `auval -v aumu Roll Rola` (subtype `Roll`, manufacturer `Rola`).

### Windows

1. Unzip `ROLLA-Windows-x64.zip`.
2. Copy `ROLLA.vst3` to `C:\Program Files\Common Files\VST3\`.
3. FL Studio: **Options > Manage plugins > Find plugins**.

## Play

- **ROLL** writes a new pattern for every lane that is not kept. **SHUFFLE** rerolls one lane against the lanes you kept.
- **KEEP** leaves that lane in place on the next roll.
- **SWING** is global, 50% straight through 57%, and it moves the e and a of every lane including drums.
- **Length** is 2, 4, or 8 bars and changes the loop immediately. **FILL** is baked on the next roll: the last bar gets a variation (busier drums, a cut in the bass).
- **DUCK** dips bass, pad, and stab from the kick lane, even if the drums are muted.
- **EXPORT MIDI** writes `.mid` files (480 PPQ, per lane plus a type-1 combined file) to `~/Documents/ROLLA/`. **DRAG MIDI** drags those files out. FL likes `.mid`, not `.midi`. If the clip does not sit on the bar, use Realign — the file starts with a ROLLA marker and a 1-tick anchor.
- Drum voices accept a dropped WAV (kick, snare, clap, rim, low tom, mid tom, closed hat, open hat, ride). Click a loaded voice to clear it and return to the model. Samples are remembered in the plugin state. They are never bundled.

Dialects: UK MIN, LONG, POP, GASKIN 90s. Key defaults to D. Macros DARK, BUSY, BOUNCE, and STAB shape the next roll. TONE, PUNCH, and level shape the sound of the current preset without writing a new pattern.

The editor is the locked D-simple dark layout. Seven lane rows (colour, pattern strip, mute, shuffle, keep) sit above one shared SOUND panel for the lane you click: preset, TONE, PUNCH, LEVEL. DRUMS adds seven kit voices — kick, snare, clap, closed hat, open hat, ride, low tom — plus rim and mid tom, so a dropped WAV can still replace those two models. The right column is the ROLL face (space does the same thing), the four macros shown on a 0–10 scale, swing and duck, then DRAG MIDI and EXPORT. Colours and the face are provisional in `src/UiTokens.h` and `paintRollFace()` (`src/RollFace.h`). When HANDOFF.md and face.svg arrive, those two files take the skin. The geometry stays where it is.

## Samples

The plugin plays samples only. No preset, saved state or missing file brings back a synth, an arp or a test tone: a lane with nothing loaded is silent, and a red banner reads `Samples not loaded: <path> (<n> files)`. The logo menu shows the active folder and its file count. Audio is never committed.

Where ROLLA looks, in order:

1. The pack built into the plugin: `ROLLA.vst3/Contents/Resources/Samples` (or `ROLLA.component/…`). Nothing is ever written there.
2. The folder saved as `samplesRoot` in `ROLLA.settings` (logo menu **Samples folder…**), if it exists, holds audio and is not under a temp folder.
3. `~/Music/ROLLA/Samples/` (Windows `%USERPROFILE%\Music\ROLLA\Samples\`).

Building a pack into the plugin (the pack stays outside the repo):

```
cmake -B build -DROLLA_SAMPLE_PACK_DIR=/path/to/pack     # copied into the bundles after every build
tools/inject_pack.sh build/ROLLA_artefacts/Release/VST3/ROLLA.vst3 /path/to/pack   # or into one built bundle
```

On macOS both re-sign the bundle ad hoc. `Samples/.rolla-pack.json` records the file count and the manifest hash. A `SAMPLE_MANIFEST_v2.json` at the pack root replaces the embedded manifest.

Tests never touch a real profile: `tests/common/ScratchProfile.h` points `ROLLA_SETTINGS_DIR`, `ROLLA_CACHE_DIR`, `ROLLA_DEFAULT_ROOT` and `ROLLA_BUNDLE_ROOT` at a temp folder (on macOS `$HOME` alone does not move JUCE's files). The last two are ignored by plugin builds. It also sets `ROLLA_TEST_MODE=1`: in that mode `src/Engine/RollaSettings.h` moves any settings, cache or default-root path that would land in a real `Music/ROLLA`, `Application Support/ROLLA`, `Library/Caches/ROLLA`, `~/.cache/ROLLA` (and so on) into the scratch folder, with a log line (`rolla-profile-test` checks this). Every CI binary runs on a scratch profile, including `rolla-render`, `rolla-mix-render` and `rolla-rolls`. To render with an installed pack, pass it explicitly: `rolla-render --samples DIR renders` (the pack is read only, and caches stay in the scratch folder). Python scripts launch binaries with `tests/common/scratch_env.py`.

Folder layout:

```
BASS/  STAB/  HITS/  PLUCK/  PAD/  VOX/
DRUMS/KICK  DRUMS/CLAP  DRUMS/SNARE  DRUMS/RIM
DRUMS/CH    DRUMS/OH    DRUMS/RIDE   DRUMS/PERC
```

`CH` is the closed hat, `OH` the open hat, `PERC` is storage for one-shots you reference from `pool` or `file`. Paths in `presets.json` are relative to that root and use forward slashes, for example `BASS/pluck_d1.wav`. Pitch names are scientific, C4 = 60, so D1 = 26. `start` and `loop` times are seconds. `loop.end` of 0 means the end of the file. `layerMix` is 0 (synth) to 1 (sample). `pool` is what SHUFFLE / ROLL rotates. `zones` is an optional multisample map. `slice: true` (meant for vox) starts each note on the next detected transient. Drum kits use `voices.kick` and the other eight voice keys. Loading happens on a background thread. The audio thread only reads a published buffer and resamples with a 16-tap windowed sinc. See `resources/presets.json` for the block on every preset. `SYNTH`, and the synth half of `LAYER`, only sound in the headless tools and tests; the plugin plays the sample.

## Parameter IDs

All of these are host-automatable except `roll` and `*Shuffle`, which are triggers (the UI sets them 0 then 1).

| ID | Range | Default |
| --- | --- | --- |
| `dark` `busy` `bounce` `stab` | 0–1 | 0.55, 0.40, 0.45, 0.55 |
| `dialect` | UK MIN, LONG, POP, GASKIN 90s | UK MIN |
| `key` | C … B | D |
| `swing` | 0.50–0.57 | 0.52 |
| `length` | 2, 4, 8 | 8 |
| `fill` | bool | off |
| `duck` | bool | on |
| `drumsToMain` | bool | on |
| `drumsAccent` | 0–1 | 0.60 |
| `output` | −24…+6 dB | 0 |
| `roll` | bool, not automatable | off |

For each lane `bass`, `stab`, `hits`, `pluck`, `pad`, `vox`, `drums`:

| ID | Range | Default |
| --- | --- | --- |
| `{lane}Mute` `{lane}Keep` | bool | off |
| `{lane}Preset` | 3 presets from `presets.json` | first |
| `{lane}Tone` `{lane}Punch` | 0–1 | 0.50 |
| `{lane}Level` | −24…+6 dB | 0 |
| `{lane}Shuffle` | bool, not automatable | off |

Drum sample slots are state, not parameters. State keys: `sample0`…`sample8` (base64 float32 mono), `sampleN0`…`sampleN8`, `sampleRate0`…`sampleRate8`. Voice order is kick, snare, clap, rim, low tom, mid tom, closed hat, open hat, ride. The pattern itself is `groove` / `grooveBytes` on the same tree.

MIDI channels: bass 1, stab 2, hits 3, pluck 4, pad 5, vox 6, drums 10. Drum notes: 36 kick, 38 snare, 39 clap, 37 rim, 45 low tom, 47 mid tom, 42 closed hat, 46 open hat, 51 ride.

## presets.json (schemaVersion 2)

`resources/presets.json` is embedded at build time. Host parameters stay the lane macros above. The sound-design values live in this file, in the spec's units (Hz, ms, dB, semitones). τ stays milliseconds: the filter envelope is exponential, not an ADSR conversion. `filterEnvSt` / `envSt`, when present, override `filterEnvOct` / `envOct` as semitones ÷ 12. Omitted keys keep the engine defaults. `unison` is an integer clamped to **1–5**.

Top level:

| Key | Meaning |
| --- | --- |
| `schemaVersion` | `2` |
| `parts.<part>.presets[]` | Three presets. `part` is `bass`, `stab`, `hits`, `pluck`, `pad`, `vox`, `drums` |
| `dialects.<id>` | `ukmin`, `long`, `pop`, `gaskin`. Each names a preset id per lane, plus `hitsMute`, `swing` (0.50–0.57), and optional `overrides.bass` |

`overrides.bass` applies only when the loaded bass preset id equals `whenPreset`. Gaskin on Round uses duck **7 dB / 110 ms** and glide **0**. Changing Dialect in the UI loads that row (preset, swing, hits mute). It does not reroll the pattern.

Shared synth keys (preset root, or an `ex` object that overrides the root):

| Key | Range | Default | Unit |
| --- | --- | --- | --- |
| `pw` | 0.05–0.95 | 0.5 | cycle |
| `pwmHz` | ≥ 0 | 0 | Hz |
| `pwmDepth` | 0–0.45 | 0 | cycle |
| `unison` | 1–5 | 1 | voices |
| `detuneCents` | 0–50 | 0 | cents |
| `spread` | 0–1 | 0 | |
| `wave` | saw, tri, sine, organ, pulse, fm | inherit | |
| `engine` | timbre, organ, fm, pwm | timbre | hits voice |
| `fmRatio` | 0.5–16 | 1 | |
| `fmIndex` `fmIndexEnd` | 0–10 | 0 | index |
| `fmDecayMs` | ≥ 1 | 40 | ms |
| `noiseDb` | | −99 (off) | dB |
| `noiseHpfHz` | ≥ 20 | 2000 | Hz |
| `noiseBpHz` | ≥ 0 | 0 (use HPF) | Hz |
| `noiseDecayMs` | 0–100 | 20 | ms |
| `pitchEnvSt` | ±24 | 0 | semitones |
| `pitchEnvTauMs` | ≥ 1 | 40 | ms |
| `filterAttackMs` | ≥ 0 | 0 | ms |
| `subLpfHz` | ≥ 0 | 0 (unfiltered sub) | Hz |
| `osc2Db` | | −99 (off) | dB |
| `osc2Oct` | ±3 | 0 | octaves |
| `osc2Wave` | saw, pulse, tri, sine | saw | |
| `chorusMix` | 0–1 | 0 | |
| `chorusRateHz` | ≥ 0.01 | 0.6 | Hz |
| `monoBelowHz` | ≥ 0 | 0 (off) | Hz |
| `reverbSeconds` | 0.2–4 | 2.6 | seconds |
| `delaySendDb` `reverbSendDb` | | −99 (keep linear send) | dB |
| `duckDb` | −1 or 0–30 | −1 (part default) | dB |
| `duckRecoverMs` | 20–250 | 115 | ms |
| `duckGate` | bool | false | 40 ms hold when true |
| `vowelTo` | −1–3 | −1 | oh, ah, hey, uh |
| `morphMs` | ≥ 0 | 0 | ms |
| `size` | 0–1 | 0 | 0 male, 1 female |
| `targets.lufsS` `targets.peakDb` | | | harness notes, not applied |
| `chord` `chordOn` | up to 4 semitone intervals | off | stab chord memory |

Bass-only keys still in use: `osc` (`saw`, `tri`, `sine2`, `organ`), `subDb`, `cutoffHz`, `resonance`, `filterEnvOct` or `filterEnvSt`, `filterTauMs`, `ampAMs`, `ampDMs`, `ampSDb`, `ampRMs`, `glideMs`, `driveDb`, `duckDb`, `duckRecoverMs`, `hpfHz`, `gate`, `trimDb`, `octave`.

Stab-only keys: `mode` (`dub`, `swell`, `click`), `envOct` or `envSt`, `tauMs`, `delaySend`, `delayFeedback`, `delayBeats`, `reverbSend`, `hold`. A stored `chord` expands a one-note stab, including live MIDI on channel 2. Generator chords are left as written.

Hits `engine` `organ` is three sines (8'/4'/2⅔'). `fm` is 2-op FM with velocity on the index. `pwm` is a pulse plus a saw an octave down, unison capped at 5. Factory preset 0 stays `timbre`. Hoover and the reese hit (timbre 4) high-pass at 220 Hz, 24 dB. Piano Hit and Hoover are presets 1 and 2. Warehouse trim is set from loudness so the hits sit about 3 LU under the bass.

Drums stay on the generator's rules, not a fixed one-bar grid. Hats and ride are the synthesized six-square metal bank. A dropped WAV replaces that one voice and fades out over the last 12 ms. While DRUMS is unmuted, DUCK follows ROLLA's kick note-ons. While DRUMS is muted, DUCK falls back to every quarter note. The clap is four Hann bursts into one exponential tail, and it is not cut off on a timer.

Shipped bass presets: Pluck, Round, Click. Stab: Dub, Swell, Click. Hits: Warehouse, Piano Hit, Hoover. Pluck: Hook, Soft, Bright. Pad: Air, Wide, Dark. Vox: Close, Booth, Chop. Drums: 909 Tight, 909 Room, 909 Dry.

Pluck (the UK MIN bass) is cutoff 220 Hz, filter envelope +1.15 oct, sub −18 dB, drive 2.5. Dub is four detuned saws at ±12 cents, spread 0.62, low-pass resting at 3.5 kHz, delay send 0.8 into a 3/16 ping-pong, plate send 0.7. Close vox uses a brighter formant mix, a breath layer, a slow pitch drift, and a stronger plate send.

An 8-bar render at 48 kHz with every lane on stays under **10% of one core** (measured about 0.04–0.06×).

A voice is not hard-stopped while it is still audible. Retriggers and steals crossfade over about 12 ms, and envelopes run down past −60 dB. Delay and plate returns fade across the last 16th of every bar and the tank is cleared on the downbeat, so a loop does not wash bar 1. A stab that starts on the last 16th of the pattern sends no delay. Muting a lane multiplies its sends by that lane's level, so a muted lane cannot feed the delay or the plate.

`rolla-render` checks this. It solos bass, stab, hits, and vox, plus a dub chord (A3, channel 2) and a single clap, and it fails the run if a lane has a sample-level hard cut, the bass centroid leaves 73–200 Hz, bass energy under 60 Hz exceeds 70%, the dub chord misses centroid 900–2500 Hz / tail 500–1600 ms / correlation 0.40–0.70, hits sit outside about 2–4.5 LU under the bass, the bar-line gate is still audible, or a fully muted arrangement leaks.

Keys the loader accepts and the voice already uses: `engine`, `pw` / `pwmHz` / `pwmDepth`, `fmRatio` / `fmIndex` / `fmIndexEnd` / `fmDecayMs`, `noiseDb` and its filter, `pitchEnvSt` / `pitchEnvTauMs`, `filterAttackMs`, `subLpfHz`, `unison` (stab stacks of 3–5, and the Hoover path), `chord`, and the dialect override block. `chorusMix`, `monoBelowHz`, `reverbSeconds`, `delaySendDb`, `reverbSendDb`, `vowelTo`, `morphMs`, and `size` are stored for the converter. They do not change the current mix yet. The shared plate is a fixed room (JUCE room size 0.90, width 0.68), not `reverbSeconds`.

## What was checked here

- Linux Release build of the VST3, the standalone, and `rolla-render` (g++, JUCE 8.0.15).
- `rolla-render`: mulberry32 matches the mockup for seed `0x0B17`. Each dialect is deterministic, finite, free of subnormals, and not silent. UK MIN bass attacks are off the kick (measured 100% on the default seed). Stab on-beat fraction is 0. CPU for an 8-bar render is about 0.05× realtime. The A&R pass on this build: bass centroid 155 Hz with 49% of the energy under 60 Hz, hits 3.4 LU under the bass, dub chord centroid 1.58 kHz, correlation 0.57, time above −10 dB 731 ms, bar-line gate −44 dB, muted lanes silent, clap tail present and click-free.
- pluginval 1.0.4 strictness 5 on the Linux VST3: **SUCCESS** (editor open, editor automation, audio, state; about 1 s under Xvfb). The standalone was opened on this layout: selecting DRUMS shows the seven kit voices, KEEP holds a lane across ROLL, and space / the face reroll the other lanes.

WAV renders from that run are the four `ROLLA-*.wav` files under the artifact renders directory (8 bars, 48 kHz, 24-bit, 130 BPM, fill on).

## Licence

Plugin code in this repo is the project source. JUCE 8 is AGPLv3, or a commercial JUCE licence if you ship a closed plugin. The splash flag is set off; current JUCE 8 builds do not draw the splash anyway. IBM Plex Sans is under the SIL Open Font License (`resources/fonts/OFL.txt`).

## Known gaps

- macOS universal and Windows binaries are not built in this environment. There is no MinGW toolchain here (`x86_64-w64-mingw32-g++` is absent), and JUCE's VST3 GUI build is not a reliable MinGW target. The Windows job in GitHub Actions uses Visual Studio 2022 x64, which is the path that should produce `ROLLA-Windows-x64`.
- The macOS job (macos-14, universal VST3+AU, ad-hoc codesign, pluginval, `auval -v aumu Roll Rola`, artifact `ROLLA-macOS-universal`) is in the workflow but has not gone green, because this environment cannot push to `https://github.com/mabe8056/rolla`.
- LONG and GASKIN can flag a demo fingerprint (`Way Back`, `Wax Master`) when the score sits between 0.40 and 0.55. A score at or above 0.55 is rerolled. Gaskin is not held to the UK off-kick rule; it uses its own dotted grid.
- Chorus, mono-below, per-lane reverb time, vowel morph, and the two general-purpose LFOs are in the schema and not yet in the mix. PWM on Hoover, 2-op FM on Piano Hit, stab chord memory, unison stacks, the bar-line tail gate, and the dialect table are in.
- Pluck's centroid lands at 155 Hz, with 49% of the energy under 60 Hz. That is inside the 73–200 Hz reference window and under the 70% sub cap. It is brighter than the 90 Hz reference median: on this ladder the open filter, not the sine sub, carries the note.
- The panel is the D-simple dark layout with a provisional skin. `src/UiTokens.h` and `src/RollFace.h` are the seam for the designer's HANDOFF.md and face.svg, which have not arrived. `drumsToMain` and `drumsAccent` stay host-only.
- pluginval's external VST3 validator is not invoked; the in-process strictness 5 suite is.
