# ROLLA corpus rolls

Vocabulary extracted by tools/extract_corpus.py into data/corpus.json and src/Generator/CorpusData.h.

| cells | count |
| --- | ---: |
| bass | 470 |
| contours | 159 |
| stab | 244 |
| voicings | 658 |
| drums | 235 |
| chops | 53 |
| banned 2-bar windows | 685 |

Each style is 6000 seeds. A pattern counts as distinct when onset, length, and pitch differ (chord tones for stab, hits, and pad; a per-step voice mask for drums). Edit distance is the mean pairwise Levenshtein of the first 48 patterns (1128 pairs).

| style | lane | unique | mean edit | rules |
| --- | --- | ---: | ---: | ---: |
| ROLLER | bass | 5898 | 71.65 | 6000/6000 |
| ROLLER | stab | 5999 | 35.17 | 6000/6000 |
| ROLLER | hits | 6000 | 19.87 | 6000/6000 |
| ROLLER | pluck | 5994 | 38.22 | 6000/6000 |
| ROLLER | pad | 5998 | 20.08 | 6000/6000 |
| ROLLER | vox | 6000 | 9.92 | 6000/6000 |
| ROLLER | drums | 6000 | 44.49 | 6000/6000 |
| DEEP | bass | 5156 | 75.42 | 6000/6000 |
| DEEP | stab | 5993 | 33.75 | 6000/6000 |
| DEEP | hits | 6000 | 20.86 | 6000/6000 |
| DEEP | pluck | 5999 | 32.07 | 6000/6000 |
| DEEP | pad | 5974 | 9.88 | 6000/6000 |
| DEEP | vox | 6000 | 9.86 | 6000/6000 |
| DEEP | drums | 6000 | 45.54 | 6000/6000 |
| BOUNCY | bass | 5980 | 88.00 | 6000/6000 |
| BOUNCY | stab | 6000 | 33.39 | 6000/6000 |
| BOUNCY | hits | 5996 | 7.25 | 6000/6000 |
| BOUNCY | pluck | 5999 | 48.08 | 6000/6000 |
| BOUNCY | pad | 5997 | 20.16 | 6000/6000 |
| BOUNCY | vox | 6000 | 9.86 | 6000/6000 |
| BOUNCY | drums | 6000 | 45.94 | 6000/6000 |
| JACKIN | bass | 6000 | 66.39 | 6000/6000 |
| JACKIN | stab | 5604 | 16.98 | 6000/6000 |
| JACKIN | hits | 6000 | 20.43 | 6000/6000 |
| JACKIN | pluck | 5998 | 51.68 | 6000/6000 |
| JACKIN | pad | 5958 | 9.77 | 6000/6000 |
| JACKIN | vox | 6000 | 9.89 | 6000/6000 |
| JACKIN | drums | 5999 | 16.61 | 6000/6000 |

Macros measured on UK MIN, 200 rolls at each end. BUSY moves notes per bar, BOUNCE moves the share of bass onsets on odd sixteenths, DARK moves the dark pitch-class share and the mean interval, STAB moves the stab count.

| knob | notes/bar | odd 16ths | dark pc | mean iv | off kick | stabs |
| --- | --- | --- | --- | --- | --- | --- |
| BUSY 0.05→0.95 | 3.22 → 5.53 | 0.58 → 0.47 | 0.72 → 0.73 | 2.11 → 1.64 | 1.00 → 1.00 | 6.34 → 6.44 |
| BOUNCE 0.05→0.95 | 4.27 → 4.33 | 0.46 → 0.71 | 0.74 → 0.76 | 1.49 → 1.69 | 1.00 → 0.99 | 6.42 → 6.33 |
| DARK 0.05→0.95 | 4.28 → 4.29 | 0.48 → 0.47 | 0.24 → 0.91 | 4.16 → 1.54 | 1.00 → 1.00 | 6.42 → 6.38 |
| STAB 0→1 | 4.29 → 4.28 | 0.47 → 0.47 | 0.77 → 0.77 | 1.52 → 1.52 | 1.00 → 1.00 | 0.00 → 11.26 |

Twenty rolls per style are under tools/out/<style>/roll_XX/ as type-0 lane MIDIs, one type-1 combined MIDI, and a 48 kHz 24-bit WAV at 130 BPM. Swing is 52% UK MIN, 51% LONG, 50% POP and GASKIN 90s. WAV files are gitignored.
