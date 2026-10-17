# GENRE_KNOWLEDGE.md: UK minimal / deep tech / rollers, plus the 90s US/UK house that feeds it
Researcher: Grok Bot (executor), 2026-10-17 ET. For the ROLLA generator rebuild. It sits alongside `GENERATOR_RULES.md` and does not replace it.
**How to read it.** `[KEY]` refers to SOURCES.md. **[INF]** marks my own inference: not quoted and not measured. In-house measurements are CTR/GR. Everything here is abstract patterns and statistics only. No melody from any record is reproduced.

---
## 0. SIMILAR_LABELS: ranked and evidence-first (for Melak to confirm)
**Method.**
- I took 20 core artists from Melak's taste and the priority labels: Gaskin, Chris Stussy, Kolter, M-High, Josh Baker, Max Dean, Luke Dean, Stef Davidse, Danny Snowden, Local Dub, LIGHTLEAK, Archie Hamilton, Sidney Charles, East End Dubs, Burnski, Prunk, S.A.M, Locklead, Rossi and Dennis Quin.
- I pulled every label each of them has released on from trax.house, which builds on Beatport catalogue data, and counted how many core artists each label shares. The data is in `raw/cooc.json`.
- I then checked each label's genre-tag share and BPM band, and its "path" links into and out of the priority labels.
- The priority labels themselves (Nervous, PIV, Up The Stuss, Bass Jamz, DFTD, Hot Creations, neXup, DX3N, You&Me, Baker's Dozen) are excluded from the ranking.

| # | Label | Core artists shared (of 20) | Who | Catalogue fingerprint (trax.house) | Why it's similar |
|---|---|---|---|---|---|
| 1 | **MicroHertz** (Archie Hamilton) | 9 | Archie Hamilton, Chris Stussy, Danny Snowden, Gaskin, Josh Baker, Local Dub, Luke Dean, Sidney Charles, Stef Davidse | 66% Minimal/Deep Tech, typical 128 BPM (123–131), 137 releases since 2010 | Highest overlap with the You&Me / neXup / Up The Stuss circle. Its signature is the "spacious bassline" [BP-MHZ] |
| 2 | **Locus** (London) | 9 | Chris Stussy, Danny Snowden, Gaskin, Josh Baker, Locklead, Luke Dean, M-High, Rossi, Sidney Charles | 44% M/DT, then deep house; 130 BPM (128–133) | 4 Up The Stuss artists released on Locus first (trax "path into UTS"). Stussy & Toman's LCS002 is "hazy chords, filtered vocals, rolling basslines, off-kilter percussion" [EVL-LOCUS] |
| 3 | **Eastenderz** (East End Dubs) | 7 | Chris Stussy, East End Dubs, Gaskin, Max Dean, Rossi, Sidney Charles, Stef Davidse | 85% M/DT; 129 BPM (127–132); #3 top-selling M/DT label of 2024 [BP-2024] | The most purely minimal / deep tech of the group. Rolling, dubby, consistent grooves [ENDZ] |
| 4 | **Heavy House Society** | 8 | Archie Hamilton, Danny Snowden, Dennis Quin, Kolter, Locklead, Luke Dean, M-High, Sidney Charles | 72% M/DT; 130 BPM (127–133); #5 M/DT label of 2024 [BP-2024] | The Kolter/Locklead/Luke Dean axis. It feeds Up The Stuss: it's the top "path into UTS" label with 5 artists |
| 5 | **PVCLB** | 4 | Gaskin, LIGHTLEAK, Prunk, Stef Davidse (+ Firza, Julian Fijma, Mathias Kaden) | 57% M/DT; 130 BPM (128–132); since 2024 | New label sitting exactly where Melak's transcribed artists (LIGHTLEAK, Stef Davidse, Gaskin) meet the PIV founder |
| 6 | **WYLD** | 4 | Danny Snowden, Gaskin, Max Dean, Stef Davidse (+ Robbie Doherty, Finky, Julian Fijma) | 76% M/DT; 129 BPM (127–131) | neXup roster overlap (Snowden, Stef Davidse, Robbie Doherty, Finky) |
| 7 | **ROSSI.HOME//GRXWN.** (Rossi) | 5 | Burnski, Kolter, Local Dub, M-High, Rossi (+ Ranger Trucco, Hidde Van Wee) | 54% M/DT; 130 BPM (129–138) | Local Dub + M-High overlap, two transcribed artists. Up The Stuss shares Hidde Van Wee |
| 8 | **tszr** | 6 | Archie Hamilton, Josh Baker, LIGHTLEAK, Local Dub, Locklead, M-High | mixed House/Tech House/M-DT; 130 BPM (124–138); shares 5 producers with Baker's Dozen | Josh Baker's satellite. 3 transcribed artists (LIGHTLEAK, Local Dub, M-High) |
| 9 | **Factory 93 Records** | 8 | Archie Hamilton, Dennis Quin, Gaskin, LIGHTLEAK, M-High, Prunk, Sidney Charles, Stef Davidse | 130 BPM (125–135); also shares artists with Drumcode/Armada | High overlap, but leans more crossover/festival [INF from shared-label list]. It's where UTS, Hot Creations and Hottrax artists go next |
| 10 | **Dansu Discs** | 3 | Archie Hamilton, Danny Snowden, Gaskin | House + **UK Garage/Bassline** tags; 130 BPM (127–134); shares 3 producers with Bass Jamz | The closest label to the Bass Jamz / GASKIN 90s flavour (garage, bassline) |
| 11 | **Rutilance Recordings** (DJ Steaw) | 3 | Chris Stussy, Kolter, M-High | 91% deep house; 125 BPM (123–129) | Stussy calls it a personal favourite for "deep dub house" [INF-PIV]. The LONG / dub end |
| 12 | **Kaoz Theory** (Kerri Chandler) | 2 | Chris Stussy, Dennis Quin (+ Kerri Chandler, DJ Sneak, Dam Swindle) | soulful/deep house; 125 BPM (121–128); shares 3 producers each with Nervous and Hot Creations | US-house root of PIV ("Kerri Chandler kind of style") [IS-PRUNK] |
| 13 | **Koltrax** (Kolter) | 3 | Kolter, M-High, Sidney Charles | deep house/M-DT; 130 BPM | Kolter's own label, from the PIV and Up The Stuss alumni |
| 14 | **Berg Audio** | 3 | Chris Stussy, Kolter, M-High (+ Traumer) | 67% M/DT; 127 BPM | Shares 3 producers with Up The Stuss |
| 15 | **Moxy Muzik** | 7 | Danny Snowden, Dennis Quin, Gaskin, M-High, Max Dean, Prunk, Sidney Charles | 28% M/DT; 128 BPM | Broad, but overlaps Nervous (4) and Hottrax (4) |
| 16 | **Rawsome Deep** | 3 | Archie Hamilton, Kolter, Prunk | 70% M/DT; 128 BPM | Also on Josh Baker's list as "Rawsome" |

**Overlap the data shows but Melak has already rejected.** He gets the final say. These are listed for transparency, not as recommendations:
- Fuse London: 7 core artists (Stussy, Archie Hamilton, East End Dubs, Burnski, Locklead, Rossi, S.A.M).
- Hottrax: 7 (Archie Hamilton, Snowden, Dennis Quin, East End Dubs, LIGHTLEAK, Max Dean, Stef Davidse).
- Solid Grooves: 5 (Dennis Quin, East End Dubs, Gaskin, Kolter, Sidney Charles).

**Deliberately left out as big-room or generic tech house**, despite artist overlap: Variety Music (8), Defected (6), Toolroom (4) and Repopulate Mars. Their catalogues centre on 124 BPM tech house or festival house.

**Josh Baker's other labels:**
- Automatic Writing: 129 BPM, Minimal/Deep Tech.
- Rawsome (Recordings / Deep).
- Politics Of Dancing (Digital), Constant Sound / Constant Black, META, Tamango, Moan, OLDiViBES, SYNTHO.
- Ovnie, ISM, Mindhelmet, Back 2 Black and Subsequent weren't in the trax.house data, so they're **unverified** in this pass.

---
## L. Priority labels: sound, artists, groove, BPM, and how each maps onto ROLLA's styles
BPM and genre shares below come from trax.house catalogue pages (SOURCES §catalogue). Style weights use the order UK MIN / LONG / POP / GASKIN 90s, in [0..1]. They're [INF] and are stored as `label_style_weights` in PATTERN_VOCAB.json.

| Label | Sound signature | Key artists | Arrangement / groove traits | BPM | UK MIN / LONG / POP / G90 |
|---|---|---|---|---|---|
| **Nervous Records** (NYC, 1991) | Raw yet soulful East Coast house. "Soulful, flavorful beats with old disco samples" [DJM-NERV]. Later garage-house revival (Byron Stingily, Todd Edwards, Kerri Chandler) [VF-NERV]. Today it's a high-volume Beatport house/tech house label (Mau P, Loofy, Vinter) [WIKI-NERV] | MAW/Nuyorican Soul, Kenny Dope, Todd Terry, Kerri Chandler, Mood II Swing; recent: Chris Stussy (first step-up label, 2015), Prunk (2015), LIGHTLEAK (2026), Archie Hamilton, M-High | 909 swing, offbeat open hats, organ/piano stabs, vocal hooks [AM-90S, INF]. Soulful tag 47% | 125 typical (120–130) | .2/.1/.6/.9 |
| **PIV** (Amsterdam, Prunk, 2015) | "Soft spot for US classic house… with a modern European touch" [GA-PRUNK]. Started soulful (Kerri Chandler style), now "deeper, a little bit more tech", always 120–130 [IS-PRUNK]. Smooth melodies, quality drums, disco touch, vocal-led [PIV-ABOUT] | Prunk, S.A.M ("Fury's Laughter"), Chris Stussy, Kolter (first label), M-High (first label), Gaskin (step-up 2023), DJOKO, Ray Mono, Burnski, Sidney Charles, Dennis Quin | Vocal motifs, percussive flourishes, piano-laden deep house [GA-PRUNK]. The **launchpad** for 13 of the 20 core artists | 120–130 [IS-PRUNK] | .6/.3/.6/.7 |
| **Up The Stuss** (Chris Stussy, 2020) | "Warm, effortlessly groovy, deep and dubby, glimmering pads, skippy breakdowns", "skippy, swinging beats and gliding melodies" [DJM-STUSSY]. Stussy: "warm pads and catchy synth lines… a groovy bassline played like a funky guitar and filter that up" [SM-STUSSY] | Chris Stussy, Locklead / Across Boundaries, Kolter, S.A.M, East End Dubs, Gaskin (UTS20), M-High, Sidney Charles, Burnski, Hidde Van Wee | Gaskin's UTS20 tracks run 6:05–6:58 at 132–134 BPM [UTS20]. UTSOFF singles series is vocal-hook led ("All Night Long": phone vocal through a vocoder). 71% deep house tag | 131 (126–135) | .8/.6/.6/.6 |
| **Bass Jamz** (Gaskin, Newcastle, 2021) | "Raw, groove-led sounds of the 90s reimagined with a fresh, modern edge" [BJ-GASKIN]. Gaskin: "more energy… old school 90s house sounds" [6AM-GASKIN] | Gaskin, Ellia Jaya, Ellam, Wildish, WAL, Caul.e, Malarkey, JK Rollin, Watchers UK | Measured Gaskin traits: dotted-8th bass grid, 2-bar call/answer, light sidechain, open 8th hats, sustained bVI/bVII [CTR-D]. Ellia Jaya: 65% swing, chops [CTR-C] | 131 (130–132), tight | .6/.2/.5/1 |
| **DFTD** (Defected sister label, 2013) | "Dancefloor-focused house… a more diverse musical remit" [EVL-DFTD, BC-DFTD] | Recent: Tommy Bones, Sorley/Mike Dunn, ANOTR, Sidney Charles, Ahmed Spins, Stacey Pullen, Late Replies | Festival & crossover house scene. Big breakdowns, vocal/acapella culture (many acapella releases) [INF from catalogue] | 124 (120–128) | .1/.1/.9/.6 |
| **Hot Creations** (Jamie Jones / Lee Foss, 2010) | Dark, funky, groove-led house/techno with disco, soul and funk [WIKI-HC]. Jones: "I love quirkiness"; samples that sound 90s with a modern twist [MMA-HC] | Jamie Jones, Lee Foss, Richy Ahmed, Patrick Topping, Miguel Campbell, Steve Lawler, Kolter, Max Dean, M-High, East End Dubs, Burnski | Spoken one-liners and vocal gimmicks (the Portuguese countdown on "Planets & Spaceships", the phone call on "Big Ass Biscuit"). Hooky mid-tempo [MMA-HC] | 126 (122–130) | .3/.2/.8/.6 |
| **neXup recz** (Max Dean, 2023) | Open door, "no fixed sound policy" [search: electronicgroove]. Hard-hitting house with 90s stabs/chords, rap and vocal elements [DJM-MAXDEAN, search summaries]. #6 M/DT label of 2024 [BP-2024] | Max Dean, Luke Dean, Danny Snowden, Stef Davidse, Gaskin, Robbie Doherty, Cam Stockman, Julian Fijma, Finky, Watchers UK | Peak-time UK minimal with vocal hooks [INF] | 131 (128–134) | .7/.3/.8/.5 |
| **DX3N Records** (Dean family) | Only Max, Luke and Joss Dean release on it. The debut EP had Max's vocals on all four tracks [DJM-MAXDEAN] | Max Dean, Luke Dean, Joss Dean | Sung vocals; crossover. "Can't Decide" (Max/Luke/Locky) went UK #16 [DJM-MAXDEAN] | n/a in data (neXup family 128–134 [INF]) | .4/.2/1/.4 |
| **You&Me Records** (Josh Baker, Manchester) | Minimal tech house with deep grooves, basslines, acid overtones and vocal hooks [BC-YM]. **#1 top-selling Minimal/Deep Tech label of 2024** [BP-2024]. Josh: drums sampled from old records, acapellas, Minilogue leads [FLOW-JB] | Josh Baker, Local Dub, Stef Davidse, Danny Snowden, Gaskin, M-High, Sidney Charles, Dennis Quin, Cloonee, Sosa UK, Marsolo, Job De Jong | Josh Baker's "Something To Me" measured: 4-bar loop, stepwise 1-2-b3 dorian bass, sung vocal up front [CTR-C]. He sketches 32/64-bar ideas [search: weraveyou] | 130 (126–132) | 1/.6/.6/.5 |
| **Baker's Dozen** (Josh Baker, 2025) | Pop-up extension of his brand [search: ibiza-spotlight]. House / Dance-Pop / Deep House tags | Josh Baker, James Poole, MK, Omar, Riordan | Crossover singles [INF] | 132 (127–134) | .6/.3/.8/.4 |

**Genre-level observations from the label data [INF]:**
- The core scene sits in a tight **129–132 BPM** band (Up The Stuss, Bass Jamz, You&Me, neXup, Locus, Eastenderz, HHS, PVCLB).
- The US-house parents sit lower: Nervous 125, PIV 120–130, Kaoz 125, DFTD 124, Hot Creations 126.
- So **GASKIN 90s should take the 90s *vocabulary* at the modern 130 BPM tempo, not 90s tempos.** That matches Gaskin's 131–134 releases.
- **PIV is the launchpad label.** It was the first or step-up label for Gaskin, Kolter and M-High, and an early label for Stussy, Prunk and Burnski. So PIV's US-house DNA runs through the whole UK minimal roster.

---
## 1. Scene map: 40+ artists (verified via trax.house release paths unless marked)
- **UK minimal / rollers core:**
  - Gaskin, Josh Baker, Max Dean, Luke Dean, Joss Dean, Danny Snowden, Local Dub, LIGHTLEAK, M-High.
  - Archie Hamilton (MicroHertz: "groove from the space… where the notes aren't" [BP-MHZ]), East End Dubs (Eastenderz; 303 user [MT-303]).
  - Sidney Charles (drums-first; early-90s raw jacking beats [search: per-vurt]), Rossi, Robbie Doherty (#3 M/DT artist 2024 [BP-2024]), Watchers UK, Julian Fijma, Cam Stockman, Finky, Murphys Law UK, JK Rollin, Ellia Jaya, Laidlaw*.
- **Dutch / European deep-groove side:** Chris Stussy, Locklead, Kolter (ex-DJOKO), S.A.M, Prunk, Hidde Van Wee, Toman, Job De Jong, Marsolo, Burnski, Dennis Quin, Stef Davidse (NL), Traumer, Firza.
- **Elders and the US root:** Kerri Chandler, Masters At Work, Todd Edwards, Mood II Swing, DJ Sneak, Mike Dunn, Stacey Pullen, Jamie Jones, Lee Foss, Richy Ahmed, Patrick Topping, Enzo Siragusa, Peace Division, Doc Martin.
- **Big-room adjacent (ROLLA should avoid as a target):** Michael Bibi and PAWSA (Solid Grooves: "minimal, micro tech-house… sharp percussion, distorted samples, wobbly basslines", 120–130 [DJM-PAWSA]), Mau P, Cloonee.
\*Laidlaw identity is medium confidence [CTR caveats]; no trax.house page.

## 2. Arrangement and song structure
- **8-bar blocks everywhere.** "Every 8 bars, something comes in, drops out or changes." The common template is intro 32 → break/build 16 → drop 32 (four 8-bar blocks: groove, +hook, mute/filter one thing, variation) → break 16 → build 8 → drop 32 → outro 32. That's 168 bars, about 5:20 at 126 BPM [MX-TH].
- **DJ intro and outro:** at least 16 bars of beat-only material with no harmony at each end, ideally 32 [RS-STRUCT, EDMGP].
- **Rollers and minimal: subtract and add one thing at a time.** Bring one element in or take one out every 8 or 16 bars. Filter, decay and delay automation drift over 16/32/64 bars. Breakdowns remove the kick and bring it back on a phrase boundary [BRIZM-MIN].
- **Track length:** the scene's 130+ BPM tracks run 6–7 minutes (UTS20: 6:05 / 6:25 / 6:58 [UTS20]). That's about 200–230 bars, so there's more groove time than the 168-bar template [INF].
- **Deep-tech anatomy** (Mr G, Attack) [AM-MRG]:
  - 16-bar intro.
  - The ride cymbal "only ever stays for eight bars at a time".
  - Mono parts and no clever automation.
  - Kick and bongos leave for the breakdown while bass and piano stay, then come back in stages, not all at once.
- **Breakdown recipe** [AM-BREAK]: a 32-bar section. The bass keeps its line with the lows cut. Chords and arp start filtered and open up. Bass fills arrive at bar 9, a soft offbeat hat creeps in, and a snare roll rises over bars 16–24 and cuts right before the drop.
- **Vocal drops and fills** [INF from MX-TH, MR-TH]: fills every 8 or 16 bars, phrases in the back half of bars 4 and 8, risers in the last 2 bars before a section.
- **DJ-tool vs song:**
  - LONG and UK MIN lean DJ-tool: long intros, no big breakdown, micro-changes.
  - POP is song-like: a vocal hook works as the chorus. "All Night Long" was finished by adding a vocal and melodies to a sketch [DJM-STUSSY]. Max Dean's records chart in the UK Top 40 [DJM-MAXDEAN].
  - GASKIN 90s is in between.
- **Josh Baker's workflow:** sketch 32- or 64-bar ideas while travelling, then finish them [search: weraveyou]. That supports 32/64-bar loop templates [INF].

## 3. Drums
- **Kick and clap.** A straight four-to-the-floor kick with claps on 2 and 4 is universal; it's measured in every UK track [CTR-A11, MR-TH]. The kick is short and punchy with its tail cut to leave room for the bass [MR-TH, MR-RETRO].
- **Hats.** Offbeat closed hats (slots 3/7/11/15) are the base [MR-TH]. Open offbeat hats show up in 88%+ of bars for Local Dub, Josh Baker and M-High, while Stef uses short closed hats [CTR-B]. "Skippy, swinging beats" are Stussy's trademark [DJM-STUSSY]. The 90s recipe layers transient-less closed hats with 4 syncopated hits per bar at varied lengths [AM-90S].
- **Swing amounts by idiom:**
  - UK minimal measured at 50.4–57.1% on hats, with the clap/perc band 1–4 points later (Laidlaw 63%) [CTR-A8].
  - Generic house/minimal: 52–58% [BRIZM-RG].
  - 90s house: 55–60%, with MPC 16 Swing-67 as the classic template [AM-90S].
  - 2-step garage: 60–65% [AM-2STEP].
  - Ellia Jaya: 65% [CTR-C].
  - Swing only moves the in-between 16ths, so an 8th-note hat line won't change [AM-SWING, BRIZM-RG].
  - **FL Studio note** (Melak's DAW): FL 0% equals MPC 50%, and FL 100% equals 66.6% [AM-SWING].
- **Ghosts and velocity.** Ghosts sit at about one third of full velocity [BRIZM-RG]. Velocity accent patterns create groove; a shaker at constant velocity "is a metronome" [BRIZM-RG]. Velocity can also modulate length or cutoff, not just volume [SB-TECHNO].
- **Percussion:**
  - Rim and conga are high-passed, panned apart and placed around the main hits, not on them [MR-PERC search summary].
  - Bongos and cowbell carry deep tech [AM-MRG].
  - A 707 tom fills the low-mid gap in "skippy" grooves [SB-HOUSE].
  - Stussy builds drums from FL's 909/808/707, pitching and compressing them and EQing out the peaks [SM-STUSSY].
  - PAWSA: "all built from hats and claps… not overly produced" [DJM-PAWSA].
- **2-step option** (GASKIN 90s / Dansu Discs side) [AM-2STEP]: kick on 1 and the and-of-3, no kick on the downbeat of bar 2, snare on 2 and 4, shaker and tambourine at 60–65% swing.
- **909 vs 808 vs sampled:**
  - 90s house is 909 [AM-90S].
  - Stussy uses 909/808/707 [SM-STUSSY].
  - Josh Baker samples drums from old records [FLOW-JB].
  - Sidney Charles uses a 909 and Digitakt [search: per-vurt].
  - For ROLLA, **Melak's own packs replace all of this.** The "909" lanes just mean a 909-*role* sample [INF].

## 4. Bass
- **What makes it roll.** Notes sit between kicks, leaving the first 16th of each beat empty [AM-ROLLTECHNO, CB-ROLL]. Velocity gallops (115/85/75 on the 3 notes after a kick) [CB-ROLL]. Heavy sidechain "glues" even full-16th lines [TPS-BASS]. And space: "funk and groove come from the space and where the notes aren't" [BP-MHZ].
- **Archetypes in the vocabulary:**
  - Offbeat pump: "one of the most widely used" [TPS-BASS].
  - 3-per-beat roller, 2-before-beat, long/short [SH-ROLL].
  - Full 16ths [TPS-BASS].
  - Hold-and-lean and fixed-rhythm syncopation [CTR-C].
  - Dotted 3-3-3 [CTR-D1].
  - Funky-guitar style that you then filter [SM-STUSSY].
  - Acid 303 [SM-STUSSY uses ABL3; MT-303].
  - M1 organ 8ths and garage square bounce [MR-M1, AM-GARAGEBASS].
  - Octave bounce [SH-ROLL].
  - Sparse space bass [BP-MHZ].
- **Register and keys.** Bass lives in the C1–C2 range (in-house E1–E3 notation varies). Tech house is "generally written in minor keys" and 1–2-note lines work well [MR-TH]. The in-house measurements add dorian (Josh Baker) and phrygian b2 (Gaskin) [CTR-C/D].
- **Kick call and response.** UK minimal puts 72–98% of bass attacks off the kick [CTR-A2]. Gaskin keeps the bass present through the kick with light sidechain [CTR-D4].

## 5. Stabs, chords and pads
- **Dub chord.** A minor 7th voicing, kept low and close for a darker sound, with simple chords "because the movement and effects processing create most of the interest" [TS-DUB]. Scoop the mids around 600 Hz. Add a dotted-1/8 delay at about 20% feedback plus an unsynced tape delay around 264 ms at high feedback [AM-DUB]. A short, plucky envelope gives a stab; a slow attack gives a swell [PM-DUB].
- **UK minimal stabs.** 1–3 pitch classes, off the downbeat, with at most 30% on the beat [CTR-A9]. 90s house: mostly syncopated chord hits plus one offbeat [AM-90S]. Organ stabs follow the Robin S lineage [MR-ROBINS].
- **Pads.** Warm Juno-style pads [SM-STUSSY]. The PIV side is piano-laden [GA-PRUNK]. Gaskin uses sustained bVI/bVII [CTR-D6].
- **Chord movement.** Minimal often has none: one chord with filter and delay movement [TS-DUB, BRIZM-MIN]. Colour changes every 2–4 bars [GR-3.3].

## 6. Hits, plucks and vox
- **Vocals.** Samples and acapellas are central:
  - Josh Baker keeps "a massive pack of acapellas and vocal snippets" [FLOW-JB].
  - PAWSA's craft came from "transforming acapellas into club tools" [DJM-PAWSA].
  - Spoken phrases are used as rhythm: short, dry, placed around the kick, clap or offbeat [MX-VOX].
  - Hot Creations' quirky spoken devices include a countdown and a phone call [MMA-HC].
  - Stussy's "All Night Long" hook is his own voice recorded on his phone, through a vocoder [DJM-STUSSY].
  - Max Dean sings and brings rap [DJM-MAXDEAN].
- **Plucks.** One stab or blip with a dotted-1/8 delay, "only once or twice per bar" [BRIZM-MIN]. Odd-length sequences of 3, 5 or 7 notes [GR-POLY]. Gaskin's melodies often mirror the bass [COSH-GASKIN].
- **Hits.** Riser plus metallic hit to signal each new section [AM-BREAK]. Vinyl crackle as a low texture bed [MR-TH]. "Metallic textures" are an Eastenderz trait [ENDZ].

## 7. Variation: what keeps a loop alive, and the common mistakes
**What works:**
- Polymeter: 3, 5 or 7-step loops slide against the bar and "can take dozens of bars to line up again" [BRIZM-MIN, MR-POLY].
- Probability on steps [BRIZM-MIN].
- Microtiming and swing [BRIZM-RG].
- Velocity-to-cutoff accents on a fixed rhythm [TPS-BASS].
- Varying hat length [SB-TECHNO].
- Filter journeys [BRIZM-MIN, SM-STUSSY].
- Bar-end micro-variation [CTR-A7].
- Bringing a top loop (the ride) in for 8 bars and out for 8 [AM-MRG].
- Muting a lane and slamming it back [RD-ENGAGE].

**Common mistakes:**
- Treating the 8-bar loop as the whole track [MONODY, RD-ENGAGE].
- Changing two things at once [BRIZM-MIN].
- Flat velocities [BRIZM-RG, CB-ROLL].
- Applying 16th swing to 8th patterns [AM-SWING].
- Intros under 16 bars [RS-STRUCT].
- Midrange clutter and low-end clash [RD-ENGAGE summary].
- Copying a label's sound. Gaskin: referencing tracks "often has the opposite effect… the best music is the most unique" [6AM-GASKIN]. Stussy signs "something new" [DJM-STUSSY].

## 8. Per-style contrast (ROLLA)
| | UK MIN | LONG | POP | GASKIN 90s |
|---|---|---|---|---|
| Label anchors | You&Me, Eastenderz, Locus, HHS, MicroHertz, neXup | MicroHertz, Rutilance, Berg Audio, Locus, Rawsome Deep | neXup, DX3N, Baker's Dozen, Hot Creations, DFTD, PIV (vocal), UTSOFF | Bass Jamz, Nervous, PIV (US roots), Kaoz Theory, Dansu Discs |
| BPM | 129–132 | 126–131 | 126–132 | 128–133 (90s vocab at modern tempo) |
| Hat swing | 51–57 | 52–58 | 50–56 | 54–60 (2-step option 60–65) |
| Kick | 4/4 | 4/4; bar dropouts rare | 4/4 + dropouts and pickups | 4/4, pickup on 16, optional 2-step |
| Bass | off-kick, 2 pitches, holes, heavy duck | one note, roller / 3-step poly, slow filter | octave bounce, offbeat pump | dotted grid, 2-bar call/answer, b2 neighbour, organ 8ths, light duck |
| Stab | 0–3 per bar, in bass holes | dub stab + delay throw, rare | offbeat/piano chords, catchy | syncop chord hits, sustained bVI/bVII, organ |
| Pad | mostly none in the drop | atmospheres and long swells | warm bed under the hook | sustained chords |
| Vox | sparse phrase in bar 4/8 | texture only | sung/vocoded hook, rap bar | chops, shouts, acapella |
| Arrangement | 8-bar A B A B′, bar-8 turn | 32-bar roller, one change per 8 | 32-bar song with hook | 2-bar call/answer, 8/16 phrases |
| Polymeter | ≤1 lane | ≤2 lanes | rare | rare |

## 9. What's new vs GENERATOR_RULES v1 [INF]
1. **Label lens.** PIV and Nervous US-house DNA runs through the whole UK minimal roster. GASKIN 90s should use 90s vocabulary at 130+ BPM. Its label anchors are Bass Jamz, Dansu Discs (UK garage/bassline) and Kaoz Theory.
2. **Polymeter is the main engine for "thousands of variations" without randomness.** 3/5/7-step loops on perc and pluck give LCM-long cycles. GR v1 has no polymeter at all.
3. **Groove comes from velocity, length and swing per lane, not just onsets.** Velocity→cutoff, hat-length variation and lane-specific swing make the same grid sound like many grooves. GR v1 has 2 velocity levels and no per-lane swing.
4. **2-step / garage kick and percussion** as a GASKIN 90s option (Dansu Discs / Bass Jamz overlap, Ellia Jaya's 65% swing).
5. **Arrangement is subtraction:** one change per 8 bars, top loops in 8-bar spells, kick dropouts at 16/32. That adds 32-bar templates beyond the 8-bar plans.
6. **Vocal culture is central for POP and GASKIN 90s** (acapellas, spoken one-liners, vocoded phone hooks). GR v1 treats VOX lightly.
7. **Space is a feature.** Archie Hamilton's "where the notes aren't" supports rest cells and sparse bass as first-class options, not failures.
8. **Rules softened as Melak asked.** Only 4 hard rules remain (key, low-end clash, no empty bars, originality). Everything else is a weighted tendency that scales with a freedom setting (see PATTERN_VOCAB.json `soft_rules`).

## 10. Gaps and caveats
- Reddit pages block fetches, so those are cited from search summaries. Several sources are search syntheses, marked "search summary" in SOURCES.
- These labels weren't in the trax.house data: DX3N, Politics Of Dancing, Constant Black, Ovnie, ISM, Mindhelmet, Back 2 Black and Subsequent. Their sound descriptions aren't verified.
- No audio analysis was done in this pass. Label sound traits are from press and catalogue tags. Cells marked `inference:true` should be checked by ear with A&R.
