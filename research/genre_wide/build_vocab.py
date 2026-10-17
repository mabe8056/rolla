# Builds PATTERN_VOCAB.json. Grid legend: X accent, x hit, o ghost(~35% vel), - rest, = hold previous note, O open hat.
import json, collections
S = ["UK_MIN","LONG","POP","GASKIN_90S"]
def w(a,b,c,d): return dict(zip(S,[a,b,c,d]))
cells = collections.defaultdict(list)
def C(lane, id, grid, use, weights, labels=(), src=(), deg=None, len16=None, inferred=True, sub=None, bars=1):
    g = grid if isinstance(grid,list) else [grid]
    for row in g: assert len(row)==16, (id,row,len(row))
    d = dict(id=id, grid=g if len(g)>1 else g[0], use=use, style_w=weights, labels=list(labels), src=list(src), inference=inferred)
    if sub: d["voice"]=sub
    if deg: d["deg"]=deg
    if len16: d["len16"]=len16
    if len(g)>1: d["bars"]=len(g)
    cells[lane].append(d)

# ---------------- DRUMS ----------------
K="kick"; C("DRUMS","K_four","x---x---x---x---","default four-to-the-floor",w(1,1,1,1),src=["CTR-A11","MR-TH"],inferred=False,sub=K)
C("DRUMS","K_four_pickup","x---x---x---x-o-","ghost kick on slot 15 pushes into next bar; use 1 bar in 4",w(.4,.3,.3,.5),["NERV","BJ"],["AM-90S"],sub=K)
C("DRUMS","K_drop_last_beat","x---x---x-------","bar-8 turnaround: kick drops on beat 4 for a breath",w(.6,.4,.6,.5),src=["GR-2.bar8"],sub=K)
C("DRUMS","K_drop_bar","----------------","1-bar kick mute before a phrase (DJ-tool drop)",w(.3,.2,.5,.3),src=["BRIZM-MIN"],sub=K)
C("DRUMS","K_2step",["x---------x-----","----------x-----"],"UKG 2-step kick: 1 + and-of-3, bar 2 omits the one",w(0,0,.2,.6),["DANSU","BJ"],["AM-2STEP"],inferred=False,sub=K)
C("DRUMS","K_four_double","x---x---x---x--x","extra kick on slot 16 (bouncy 90s pickup)",w(.2,.1,.3,.6),["NERV"],sub=K)
CL="clap"
C("DRUMS","CP_2_4","----x-------x---","clap/snare on 2 and 4 (core)",w(1,1,1,1),src=["CTR-A11","AM-90S","MR-TH"],inferred=False,sub=CL)
C("DRUMS","CP_2_4_flam","----x------ox---","ghost clap before beat 4",w(.4,.3,.3,.5),src=["MR-RETRO"],sub=CL)
C("DRUMS","CP_4_only","------------x---","sparse: clap only on beat 4 (sometimes every second bar in minimal)",w(.2,.4,0,.1),src=["BRIZM-MIN"],sub=CL)
C("DRUMS","CP_2_4_tail","----x-------x-x-","extra clap on slot 15 as bar-end answer",w(.3,.2,.5,.4),sub=CL)
C("DRUMS","SN_ghost_909","----x--o----x-o-","909 snare under clap + ghosts on 8/15",w(.3,.2,.2,.8),["NERV","PIV","KAOZ"],["AM-90S","MR-RETRO"],sub="snare")
C("DRUMS","SN_ghost_syncop",["----x------o----","----x--o---o--o-"],"Attack 90s step 5: 1 syncopated snare in bar 1, 3 in bar 2",w(.2,.1,.2,.8),["NERV","BJ"],["AM-90S"],inferred=False,sub="snare")
C("DRUMS","SN_roll_bar8","----x---x-x-xxxx","snare build in last bar of 8/16 (riser substitute)",w(.2,.1,.6,.4),["DFTD","NEXUP"],["AM-BREAK"],sub="snare")
CH="closed_hat"
C("DRUMS","CH_off8","--x---x---x---x-","offbeat closed hat",w(1,1,.9,.8),src=["MR-TH","AM-90S"],inferred=False,sub=CH)
C("DRUMS","CH_16_accent","oxXxoxXxoxXxoxXx","16ths with offbeat accent; swing audible",w(.6,.7,.5,.5),src=["AM-SWING","BRIZM-RG"],sub=CH)
C("DRUMS","CH_skippy","--xx--x---xx--xo","'skippy' swung hats (Stussy-style term), pairs on and/a",w(.8,.6,.5,.6),["UTS","LOCUS","RUT"],["DJM-STUSSY"],sub=CH)
C("DRUMS","CH_gallop","-xx--xx--xx--xx-","e+and gallop between kicks",w(.5,.6,.3,.3),sub=CH)
C("DRUMS","CH_e_a","-x-x-x-x-x-x-x-x","every e and a; swing moves all of them",w(.4,.5,.3,.3),sub=CH)
C("DRUMS","CH_syncop4","--x--o-x--x---xo","Attack 90s step 4: 4 syncopated soft hats per bar w/ varied length",w(.4,.3,.3,.8),["NERV","PIV"],["AM-90S"],sub=CH)
C("DRUMS","CH_sparse_click","------x-------x-","sparse tick for LONG intros",w(.3,.7,.1,.1),["RUT","BERG"],sub=CH)
OH="open_hat"
C("DRUMS","OH_off8","--O---O---O---O-","open offbeat hat (Local Dub/Josh/M-High in >=88% of bars; Gaskin 69-94%)",w(.7,.6,.8,1),src=["CTR-B","CTR-D5","AM-90S"],inferred=False,sub=OH)
C("DRUMS","OH_2_4_off","------O-------O-","open hat only on and-of-2/and-of-4",w(.4,.5,.3,.3),sub=OH)
C("DRUMS","OH_last","--------------O-","single open hat bar-end lift",w(.3,.4,.3,.2),sub=OH)
SH="shaker"
C("DRUMS","SH_16_vel","oxoXoxoXoxoXoxoX","16th shaker, alternating velocity; the groove engine of 2-step/house",w(.6,.6,.6,.8),src=["AM-2STEP","MR-SHAKER"],inferred=False,sub=SH)
C("DRUMS","SH_8th_off","--x---x---x---x-","shaker doubling offbeat hat (widen)",w(.4,.4,.6,.4),src=["MR-TH"],inferred=False,sub=SH)
C("DRUMS","TAMB_16","oooXoooXoooXoooX","16th tambourine low in mix, velocity is everything",w(.1,.1,.3,.7),["NERV","DANSU"],["AM-2STEP","MR-RETRO"],inferred=False,sub="tamb")
P="perc"
C("DRUMS","RIM_3step","x--x--x--x--x--x","rim on a 3/16 loop (polymeter; realigns after 3 bars)",w(.6,.8,.2,.3),["MHZ","RUT","LOCUS"],["BRIZM-MIN","SB-TECHNO"],inferred=False,sub=P)
C("DRUMS","RIM_syncop","---x--x----x--x-","rim on a/and pockets around clap",w(.6,.6,.4,.5),src=["MR-PERC"],sub=P)
C("DRUMS","CLAVE_son32",["x--x--x---------","----x---x-------"],"son clave 3-2 (2 bars) for perc lane",w(.3,.4,.4,.4),["HC","PIV"],sub=P)
C("DRUMS","CLAVE_rumba",["x--x---x--------","----x---x-------"],"rumba clave variant",w(.3,.4,.3,.3),sub=P)
C("DRUMS","CONGA_tumbao","---x--xo---x--xo","conga low/high pairs answering kick (panned)",w(.4,.4,.6,.5),["HC","DFTD","PIV"],["MR-PERC","AM-MRG"],sub=P)
C("DRUMS","BONGO_run","-xx-x-xx-xx-x-xx","bongo bed (Mr G 'Daily Prayer'-type deep tech)",w(.3,.5,.3,.4),["MHZ"],["AM-MRG"],sub=P)
C("DRUMS","COWBELL_off","--x----x--x-----","analogue cowbell syncop (Attack MrG) / 808 cowbell",w(.2,.3,.3,.4),src=["AM-MRG"],sub=P)
C("DRUMS","TOM_fill_bar8","----x-------x-xx","707/909 tom answer at bar end",w(.3,.3,.4,.5),src=["SB-HOUSE"],sub=P)
C("DRUMS","TOM_low_707","x-----x---x-----","low tom filling bass gaps (Studio Brootle 707 skippy groove)",w(.3,.3,.2,.5),src=["SB-HOUSE"],inferred=False,sub=P)
C("DRUMS","PERC_3_5_7_poly","x----x----x----x","5/16 blip loop (shifts vs bar, LCM 80 steps = 5 bars)",w(.3,.7,.1,.1),["RUT","BERG","MHZ"],["BRIZM-MIN","MR-POLY"],inferred=False,sub=P)
C("DRUMS","RIDE_off","--x---x---x---x-","ride on offbeats, 8 bars at a time then out (Attack MrG)",w(.3,.4,.5,.6),src=["AM-MRG"],sub="ride")
C("DRUMS","CRASH_phrase","x---------------","crash on bar 1 of a new 8/16 (use with HITS lane off)",w(.2,.1,.6,.4),sub="ride")

# ---------------- BASS ----------------
B="BASS"
C(B,"B_off8_pump","--x---x---x---x-","classic offbeat bass, one note; velocity->cutoff keeps it alive",w(.7,.6,.8,.4),["HC","DFTD","HOTTRAX"],["TPS-BASS","BRIZM-MIN","CTR-B"],inferred=False,len16=[1,1.5])
C(B,"B_roll3","-xxx-xxx-xxx-xxx","3 notes after each kick (16th roller); sidechain glues it",w(.3,.5,.3,.1),["F93","TSZR"],["SH-ROLL","CB-ROLL","TPS-BASS"],inferred=False,len16=[1])
C(B,"B_roll3_vel","-Xxo-Xxo-Xxo-Xxo","roller with 115/85/75 velocity gallop",w(.4,.6,.3,.1),src=["CB-ROLL"],inferred=False,len16=[1])
C(B,"B_pre_kick_pair","--xx--xx--xx--xx","2 notes right before each beat (Screech House 'famous' pattern)",w(.4,.5,.4,.3),src=["SH-ROLL"],inferred=False)
C(B,"B_long_short","-x=x-x=x-x=x-x=x","alternating long/short after kick",w(.5,.5,.4,.3),src=["SH-ROLL"],inferred=False)
C(B,"B_octave_bounce","--x-X---x-X---x-","root on and, octave on following e (bounce)",w(.3,.2,.7,.4),["NEXUP","DX3N"],["SH-ROLL","CTR-B"],deg=["1","8","1","8","1"])
C(B,"B_hold_lean","--x=====x=====x-","hold through kick from and/e, re-attack; Direction A/LIGHTLEAK archetype",w(.9,.8,.4,.3),["UTS","YM"],["GR-3.1","CTR-C"],inferred=False)
C(B,"B_root_b7_halfbar","x=====x=======xx","1 held 6, b7 held 8, 2-slot tag (pitch-shape of LIGHTLEAK family; rhythm abstracted)",w(.6,.6,.3,.2),["NERV","F93"],["CTR-C"],deg=["1","b7","5","1"],inferred=False)
C(B,"B_sparse_space","------x-----x---","Archie Hamilton 'groove from where the notes aren't': 2-3 notes per bar",w(.8,.9,.3,.3),["MHZ","FUSE"],["BP-MHZ"])
C(B,"B_mhigh_staccato","--x--x-x---x----","staccato attacks on 3/6/8/12 family, root+5 only",w(.8,.6,.5,.3),["PIV","UTS","YM"],["CTR-C"],deg=["1","5","1","8"],inferred=False,len16=[1])
C(B,"B_syncop8","x-x--x-x-xx-x-x-","8-attack syncopated rhythm (V Line family): fixed rhythm, pitches move",w(.6,.5,.5,.4),["YM","RHG"],["CTR-C"],inferred=False)
C(B,"B_dotted","x--x--x--x--x--x","dotted-8th 3-3-3-3-3-1 grid (Gaskin Wax Master measured)",w(.1,.2,.3,.9),["BJ","UTS"],["CTR-D1"],inferred=False,len16=[2])
C(B,"B_dotted_offset","-x--x--x--x--x--","dotted grid shifted 1 slot off the kick",w(.4,.4,.3,.6),["BJ"],["CTR-D1"])
C(B,"B_tresillo",["x--x--x-x--x--x-"],"3+3+2 per half bar (house/garage bass); starts on kick: only with low sidechain styles",w(.1,.1,.4,.7),["NERV","DANSU","BJ"],["MR-ORGAN"])
C(B,"B_tresillo_off","-x--x--x-x--x--x","tresillo displaced to avoid kick",w(.5,.4,.5,.5))
C(B,"B_callans_2bar",["--x--x-x-x------","----------x--xxx"],"2-bar call/answer: busy bar A, sparse bar B with tag",w(.6,.5,.5,.8),["BJ","UTS"],["CTR-D2","GR-2"])
C(B,"B_answer_tag","--x---x-------xx","pedal with 2-note tag on 15-16",w(.7,.6,.5,.4),src=["CTR-A7"],inferred=False)
C(B,"B_e_a","-x-x-x-x-x-x-x-x","every e and a: driving, ducked under kick",w(.3,.5,.3,.2),["TSZR","F93"],["SH-ROLL"])
C(B,"B_16_full","oxxxoxxxoxxxoxxx","full 16ths, low velocity, sidechain does the work (TPS variation 3)",w(.2,.5,.3,.1),src=["TPS-BASS","AM-ROLLTECHNO"],inferred=False)
C(B,"B_acid_303","-x-xx-x--xx-x-xx","303-style w/ slides+accents (Stussy uses ABL3 for acid lines; You&Me 'acid overtones')",w(.4,.5,.3,.4),["YM","UTS","EENDZ"],["SM-STUSSY","BC-YM","MT-303"])
C(B,"B_funky_guitar","-x-x--xx-x-x--xo","'played like a funky guitar', then filter it up (Stussy quote)",w(.5,.4,.6,.6),["UTS","PIV"],["SM-STUSSY"])
C(B,"B_organ_8ths","x-x-x-x-x-x-x-x-","M1 organ bass on 8ths w/ octave leaps (90s NY/garage)",w(0,0,.3,.7),["NERV","KAOZ","PIV"],["MR-ORGAN","MR-M1"],deg=["1","8","1","b7","1","8","5","b7"])
C(B,"B_garage_bounce","x--x--x---x--x--","Todd-Edwards-era bouncy square bass w/ filter env",w(0,0,.3,.6),["DANSU","NERV"],["AM-GARAGEBASS"])
C(B,"B_one_note_pedal","--x---x---x--xx-","one-note groove; all variation from velocity/filter",w(.6,.7,.4,.3),src=["MR-TH","BRIZM-MIN"],inferred=False)
C(B,"B_step_up","--x--x---x--x---","stepwise 1-2-b3 (Josh Baker dorian) over 4 slots",w(.6,.5,.6,.3),["YM","BD"],["CTR-C"],deg=["1","2","b3","1"],inferred=False)
C(B,"B_neighbour","---x---x---x--x-","1 <-> 2 (Laidlaw) or 1 <-> b2 (Gaskin) neighbour",w(.6,.5,.4,.6),["BJ"],["CTR-C","CTR-D3"],deg=["1","2","1","2"],inferred=False)
C(B,"B_ladder_down","--x--x--x--x--x-","5-b7?-4-b3-1 descending ladder over a bar",w(.4,.4,.5,.5),src=["GR-3.1"],deg=["5","4","b3","2","1"])
C(B,"B_glide_pair","---x=x-----x=x--","pair with 30-40 ms glide into 2nd note (sparingly, <=11% notes)",w(.3,.3,.5,.4),src=["CTR-B","GR-3.1"],inferred=False)
C(B,"B_reese_long","--x=========x===","long detuned/reese note (bassline/UKG crossover; Dansu Discs)",w(0,.2,.2,.4),["DANSU"],[])
C(B,"B_poly_3","x--x--x--x--x--x","3/16 pitched bass loop (2-note cycle) for LONG; realigns in 3 bars",w(.2,.6,0,.1),["RUT","MHZ"],["BRIZM-RG"])
C(B,"B_rest_bar","----------------","rest cell (bass hole for stab/vox answer)",w(.5,.5,.4,.3),src=["GR-3.1"],inferred=False)

# ---------------- STAB ----------------
T="STAB"
C(T,"ST_a4","--------------x-","single dub stab on and-of-4 (Stef Davidse 'and of 4')",w(.8,.7,.5,.5),["YM","NEXUP"],["CTR-C"],inferred=False)
C(T,"ST_e2_a4","-----x--------x-","two off-beat stabs (e of 2, and of 4)",w(.7,.6,.5,.5),src=["CTR-A9"])
C(T,"ST_dub_dotted","---x------------","one stab + dotted-1/8 delay throw does the rhythm",w(.7,.9,.3,.3),["RUT","BERG","MHZ"],["AM-DUB","TS-DUB"],inferred=False)
C(T,"ST_offbeat8","--x---x---x---x-","organ/chord stab on every offbeat (90s house)",w(.1,.1,.5,.7),["NERV","PIV","KAOZ"],["MR-ROBINS"])
C(T,"ST_syncop_90s","---x--x---x---x-","mostly syncopated chord hits + one offbeat (Attack 90s house step 5)",w(.3,.2,.5,.9),["NERV","BJ"],["AM-90S"],inferred=False)
C(T,"ST_a_pockets","---x---x---x---x","stab on every 'a' (Josh Baker dyad-on-the-a)",w(.6,.4,.5,.4),["YM","BD"],["CTR-C"],inferred=False)
C(T,"ST_counter_riff","x-x--x-x-xx-x-x-","stab doubles bass rhythm (V Line counter-riff; LONG/POP opt-in)",w(.1,.4,.4,.3),["YM"],["CTR-C","GR-3.3"],inferred=False)
C(T,"ST_bar_end_pair","-------------xx-","14-15 colour pair (LIGHTLEAK 5+2 stabs on 14-15)",w(.7,.6,.5,.4),src=["CTR-C"],inferred=False)
C(T,"ST_2bar_call",["------x---------","-----------x--x-"],"call in bar 1, 2-hit answer in bar 2",w(.7,.6,.6,.6))
C(T,"ST_sustain_bVI_bVII","-x==============","held bVI/bVII chord from the e (Gaskin: median 14 sixteenths)",w(.1,.3,.3,.8),["BJ","UTS"],["CTR-D6"],inferred=False)
C(T,"ST_stab_on1_rare","x---------------","downbeat stab: only as phrase marker (<=30% on-beat cap)",w(.2,.2,.4,.4),src=["CTR-A9"])
C(T,"ST_rave_stab","x-------x-------","lead 'rave' stab resonating over beat (Mr G)",w(.2,.3,.4,.3),src=["AM-MRG"])
C(T,"ST_piano_offbeat","--x--x----x--x--","house piano chord syncop (PIV piano-laden deep house)",w(0,0,.6,.6),["PIV","DFTD","KAOZ"],["GA-PRUNK"])
C(T,"ST_tresillo","x--x--x---------","3-3-2 head of bar only",w(.3,.3,.5,.6))
C(T,"ST_rest","----------------","stab rest bar (most bars in UK MIN have 0-3 hits)",w(.6,.5,.3,.3),src=["GR-3.3"],inferred=False)

# ---------------- PLUCK ----------------
L="PLUCK"
C(L,"PL_3step_arp","x--x--x--x--x--x","3/16 arp loop over chord tones (polymeter)",w(.4,.8,.4,.2),["RUT","BERG"],["GR-POLY","BRIZM-MIN"],inferred=False)
C(L,"PL_5step","x----x----x----x","5/16 loop: lands differently for 5 bars",w(.3,.7,.2,.1),src=["BRIZM-MIN","MR-POLY"],inferred=False)
C(L,"PL_7step","x------x------x-","7/16 sequence (deadmau5-style odd length) for LONG",w(.2,.6,.2,.1),src=["GR-POLY"],inferred=False)
C(L,"PL_blip_1x","-------x--------","single blip/stab with dotted-8th delay, once or twice a bar",w(.6,.8,.3,.3),src=["BRIZM-MIN"],inferred=False)
C(L,"PL_bass_mirror","--x--x-x---x----","pluck mirrors bass rhythm an octave+ up (Gaskin: melody mirrors bass)",w(.4,.3,.6,.8),["BJ"],["COSH-GASKIN"])
C(L,"PL_call_in_hole","----------xx-x--","pluck phrase placed in bass rest cell",w(.7,.6,.6,.6),src=["GR-3.3"])
C(L,"PL_offbeat_8","--x---x---x---x-","offbeat pluck chord (bouncy POP)",w(.2,.1,.7,.5),["NEXUP","DX3N","HC"])
C(L,"PL_16_gate","xoxoxoxoxoxoxoxo","16th gated pluck w/ accent pattern, filter automated",w(.2,.5,.5,.3))
C(L,"PL_catchy_2bar",["--x--x--x-x-----","--x--x--x---x-x-"],"catchy 2-bar synth line (Stussy: 'sucker for catchy synth lines')",w(.3,.2,.8,.5),["UTS","NEXUP"],["SM-STUSSY"])
C(L,"PL_question_answer",["-x-x--x---------","---------x-x--x-"],"front-half question, back-half answer",w(.5,.4,.6,.5))
C(L,"PL_ea_tick","-x-------x------","two ticks on the e's (micro-house click)",w(.5,.6,.2,.2),["MHZ","RHG"])
C(L,"PL_rest","----------------","rest",w(.6,.4,.3,.3))

# ---------------- PAD ----------------
D="PAD"
C(D,"PD_bed_bar","x===============","full-bar bed (Juno-style warm pad; Stussy 'sucker for warm pads')",w(.5,.7,.6,.5),["UTS","LOCUS","PIV"],["SM-STUSSY","DJM-STUSSY"],inferred=False)
C(D,"PD_bed_2bar",["x===============","================"],"2-bar held chord; change every 2-4 bars",w(.5,.7,.5,.5),src=["GR-3.3"])
C(D,"PD_swell_4bar",["x===============","================","================","================"],"4-bar swell, slow attack, filter opens",w(.4,.8,.4,.3),["RUT","BERG"],["PM-DUB","AM-BREAK"])
C(D,"PD_offset_e","-x==============","bed entering on the e (avoids kick transient)",w(.6,.6,.4,.6),src=["CTR-D6"])
C(D,"PD_breath","x=======--------","half-bar pad with air (call/response with vox)",w(.5,.5,.5,.4))
C(D,"PD_atmos_tex","x===============","non-tonal atmosphere/texture loop (vinyl, room, field rec) at low level",w(.6,.8,.3,.3),["MHZ","RUT"],["MR-TH"])
C(D,"PD_chord_stab_pad","x===----x===----","rhythmic pad pulses (filtered chord-pad)",w(.3,.4,.6,.5))
C(D,"PD_rest","----------------","no pad (UK MIN drops usually pad-free)",w(.7,.3,.4,.4),src=["CTR-A9"])

# ---------------- HITS ----------------
H="HITS"
C(H,"HT_bar4_a4","--------------x-","tonal hit on and-of-4 of bar 4/8 (signpost)",w(.7,.6,.6,.6),src=["GR-3.4"],inferred=False)
C(H,"HT_phrase_one","x---------------","impact on bar 1 of phrase (after a mute)",w(.3,.2,.7,.5),["DFTD","NEXUP"],["AM-BREAK"])
C(H,"HT_reverse_in","========xxxxxxxx","reverse cymbal / riser into next phrase (last half of bar 8/16)",w(.3,.2,.7,.4),["DFTD"],["AM-BREAK","MX-TH"])
C(H,"HT_metal","---x------------","metallic hit (Eastenderz 'metallic textures')",w(.6,.5,.3,.3),["EENDZ","F93"],["SRCH-ENDZ"])
C(H,"HT_vinyl_tex","x===============","vinyl crackle/texture bed at low volume",w(.5,.6,.3,.6),src=["MR-TH"],inferred=False)
C(H,"HT_shot_e3","---------x------","FX one-shot on e-of-3 (off-grid surprise)",w(.5,.5,.4,.4))
C(H,"HT_zap_bar8","------------x-x-","zap/laser pair in bar 8",w(.3,.3,.5,.5),["BJ"])
C(H,"HT_sweep_up",["x===============","================"],"2-bar white-noise sweep up into drop",w(.2,.1,.6,.3),src=["MX-TH"])
C(H,"HT_downlifter","x=======--------","downlifter after drop's first hit",w(.2,.1,.5,.3))
C(H,"HT_rest","----------------","no hit (most bars)",w(.8,.8,.6,.6),src=["GR-3.4"])

# ---------------- VOX ----------------
V="VOX"
C(V,"VX_phrase_bar4","--------x=====--","spoken phrase in back half of bar 4/8",w(.7,.5,.7,.6),["HC","BJ"],["MX-VOX"])
C(V,"VX_word_a4","--------------x-","single word/shout on and-of-4 ('hey'-type)",w(.6,.4,.7,.7),src=["MX-VOX"])
C(V,"VX_stutter","x-x-xx----------","stutter chop at bar head",w(.3,.2,.6,.5),["NEXUP","SOLID?"],["NOLVRA"])
C(V,"VX_chop_offbeat","--x---x---x---x-","pitched chop on offbeats (Ellia Jaya-type busy chops)",w(.3,.2,.6,.6),["BJ"],["CTR-B"])
C(V,"VX_chop_syncop","---x--x---x--x--","chop riff in syncopated pockets; call-response with bass gaps",w(.5,.4,.7,.6))
C(V,"VX_hook_2bar",["x=====--x===----","x=====--x=======-"[:16]],"sung/vocoded hook over 2 bars (All Night Long = phone voice + vocoder)",w(.1,.1,.9,.4),["UTS","DX3N","BD","PIV"],["DJM-STUSSY","DJM-MAXDEAN"])
C(V,"VX_acap_4bar",["x===============","================","x===============","========--------"],"acapella phrase across 4 bars (sample-led tech house)",w(.2,.2,.8,.6),["NERV","DFTD","PIV"],["DJM-PAWSA","FLOW-JB"])
C(V,"VX_breath_tex","x===----x===----","filtered breath/ooh texture",w(.5,.6,.3,.3))
C(V,"VX_rap_bar","x-x-x--x-x-x--x-","rap/spoken rhythmic bar (Max Dean hip-hop/rap influence)",w(.1,0,.6,.3),["NEXUP","DX3N"],["DJM-MAXDEAN"])
C(V,"VX_countdown","x---x---x---x---","spoken countdown on beats (Planets & Spaceships device)",w(.1,.1,.5,.3),["HC"],["MMA-HC"])
C(V,"VX_answer_bass_rest","----------x=x---","short vocal in a bass rest cell",w(.6,.5,.6,.5),src=["GR-3.3"])
C(V,"VX_rest","----------------","no vocal",w(.7,.8,.3,.4))

# fix stray
for c in cells["VOX"]:
    if c["id"]=="VX_stutter": c["labels"]=["NEXUP","DX3N"]
    if c["id"]=="VX_hook_2bar": c["grid"]=["x=====--x===----","x=====--x======-"]

labels = {
 "NERV":"Nervous Records (NYC, 1991)","PIV":"PIV Records (Amsterdam, Prunk)","UTS":"Up The Stuss (Chris Stussy)","BJ":"Bass Jamz (Gaskin, Newcastle)",
 "DFTD":"DFTD (Defected sister label)","HC":"Hot Creations (Jamie Jones / Lee Foss)","NEXUP":"neXup recz (Max Dean)","DX3N":"DX3N Records (Max/Luke/Joss Dean)",
 "YM":"You&Me Records (Josh Baker, Manchester)","BD":"Baker's Dozen (Josh Baker)","MHZ":"MicroHertz (Archie Hamilton)","LOCUS":"Locus (London)",
 "EENDZ":"Eastenderz (East End Dubs)","HHS":"Heavy House Society","RHG":"ROSSI.HOME//GRXWN. (Rossi)","PVCLB":"PVCLB","WYLD":"WYLD","FUSE":"Fuse London (Enzo Siragusa)",
 "RUT":"Rutilance Recordings (DJ Steaw)","BERG":"Berg Audio","TSZR":"tszr","F93":"Factory 93 Records","DANSU":"Dansu Discs","KAOZ":"Kaoz Theory (Kerri Chandler)",
 "MOXY":"Moxy Muzik","HOTTRAX":"Hottrax (Jamie Jones)","LW":"LW Recordings","RSD":"Rawsome Deep"}
for lane in cells:
    for c in cells[lane]:
        c["labels"]=[l for l in c["labels"] if l in labels]
        if "EENDZ" in c["labels"]: pass

label_style = {  # weights label -> style (inference from trax.house genre/BPM stats + press; see GENRE_KNOWLEDGE §L)
 "NERV":w(.2,.1,.6,.9),"PIV":w(.6,.3,.6,.7),"UTS":w(.8,.6,.6,.6),"BJ":w(.6,.2,.5,1),"DFTD":w(.1,.1,.9,.6),"HC":w(.3,.2,.8,.6),
 "NEXUP":w(.7,.3,.8,.5),"DX3N":w(.4,.2,1,.4),"YM":w(1,.6,.6,.5),"BD":w(.6,.3,.8,.4),"MHZ":w(.9,.9,.2,.3),"LOCUS":w(.9,.7,.3,.4),
 "EENDZ":w(.9,.7,.3,.4),"HHS":w(.9,.6,.4,.4),"RHG":w(.9,.7,.2,.3),"PVCLB":w(.9,.6,.4,.4),"WYLD":w(.9,.5,.5,.4),"FUSE":w(.8,.8,.2,.4),
 "RUT":w(.5,1,.1,.5),"BERG":w(.6,.9,.1,.3),"TSZR":w(.7,.6,.6,.4),"F93":w(.7,.5,.5,.4),"DANSU":w(.4,.2,.5,.8),"KAOZ":w(.2,.2,.5,.9),
 "MOXY":w(.6,.4,.5,.5),"HOTTRAX":w(.7,.4,.5,.4),"LW":w(.5,.4,.5,.4),"RSD":w(.8,.6,.4,.4)}

polymeter = [
 dict(id="PM_3_16",steps=3,realign_bars=3,lanes=["DRUMS.perc","PLUCK","BASS(LONG only)"],style_w=w(.5,.9,.2,.2),src=["BRIZM-MIN","BRIZM-RG"],note="3 divides 48 -> realigns every 3 bars"),
 dict(id="PM_5_16",steps=5,realign_bars=5,lanes=["DRUMS.perc","PLUCK"],style_w=w(.3,.8,.1,.1),src=["BRIZM-MIN","MR-POLY"]),
 dict(id="PM_7_16",steps=7,realign_bars=7,lanes=["PLUCK","DRUMS.perc"],style_w=w(.2,.6,.1,.1),src=["GR-POLY","MR-POLY"]),
 dict(id="PM_6_16",steps=6,realign_bars=3,lanes=["PLUCK","VOX"],style_w=w(.3,.5,.4,.3),src=["GR-POLY"]),
 dict(id="PM_12_16",steps=12,realign_bars=3,lanes=["DRUMS.clap(alt)","STAB"],style_w=w(.2,.5,.1,.2),src=["MR-POLY"]),
 dict(id="PM_20_16",steps=20,realign_bars=5,lanes=["STAB","HITS"],style_w=w(.2,.4,.1,.1),src=["MR-POLY"]),
 dict(id="PM_hat_len_3or5",steps="3|5 loop restarted each bar",realign_bars=1,lanes=["DRUMS.closed_hat"],style_w=w(.5,.6,.3,.3),src=["SB-TECHNO"],note="Studio Brootle: copy a 3/5-step hat loop across the bar, restart at bar 1"),
 dict(id="PM_dotted_delay",steps=3,realign_bars=3,lanes=["STAB(delay throw)","PLUCK"],style_w=w(.7,.9,.4,.4),src=["AM-DUB","TS-DUB"],note="dotted-1/8 delay = implicit 3/16 polymeter"),
]
rules = dict(
 rule_on_polymeter="Max 2 polymetric lanes at once; never on KICK/CLAP; LCM(lane lengths) defines the 'true' loop length; reset polymeter at 8/16/32-bar phrase start (INFERENCE).",
)

templates = [
 dict(id="T8_A_B_A_Bp",bars=8,plan="A B A B' A B A C",lanes_active="all drop lanes",use="core 8-bar drop loop; bar 8 = turnaround with >=4-sixteenth hole",style_w=w(.9,.6,.8,.7),src=["GR-2"]),
 dict(id="T8_1bar_mutate",bars=8,plan="A A' A A'' A A' A C",use="1-bar loop that mutates only slots 13-16",style_w=w(.7,.9,.5,.3),src=["GR-2","CTR-A7"]),
 dict(id="T8_2bar_callans",bars=8,plan="A B A B A B A C",use="2-bar call/answer (Gaskin)",style_w=w(.4,.3,.5,1),src=["CTR-D2"]),
 dict(id="T8_4bar_cycle",bars=8,plan="A B C D A B C D'",use="4-bar loop",style_w=w(.6,.5,.6,.4),src=["GR-2"]),
 dict(id="T8_add_one",bars=8,plan="groove; +1 lane at bar 5",entries={"1":"DRUMS,BASS","5":"+STAB or +PLUCK"},use="DJ-tool build: change one thing at a time",style_w=w(.7,.9,.4,.5),src=["BRIZM-MIN"]),
 dict(id="T16_intro_dj",bars=16,entries={"1":"KICK,HATS","5":"+SHAKER/PERC","9":"+CLAP","13":"+BASS(filtered)"},use="DJ intro: bars 1-16 beat-only, no harmony",style_w=w(.8,.9,.6,.7),src=["RS-STRUCT","MX-TH"]),
 dict(id="T16_groove_mute",bars=16,plan="8 full + 4 with BASS muted + 4 full with tag",use="subtraction move: mute bass for 4 then slam back",style_w=w(.8,.7,.6,.6),src=["MONODY","RD-ENGAGE"]),
 dict(id="T16_filter_rise",bars=16,plan="BASS LPF 300Hz->open over 16; hats HPF down",use="filter journey (LONG: over 32-64)",style_w=w(.6,.9,.5,.5),src=["BRIZM-MIN","SM-STUSSY"]),
 dict(id="T16_breakdown",bars=16,entries={"1":"KICK off; PAD,VOX,STAB,CLAP stay","9":"+bass fills/+soft offbeat hat","13":"+snare roll/riser","16":"cut everything slot 13-16"},use="breakdown/build (pre-drop)",style_w=w(.4,.2,.9,.6),src=["AM-BREAK","MX-TH"]),
 dict(id="T16_vocal_drop",bars=16,plan="drums+bass 8, VOX phrase on bars 4/8/12/16 back halves",use="vocal-drop groove (Hot Creations/PIV)",style_w=w(.5,.3,.9,.7),src=["MX-VOX"]),
 dict(id="T32_drop_4x8",bars=32,plan="[groove][+hook][filter/mute one lane][variation]",use="Mix Elite drop split into four 8-bar blocks",style_w=w(.8,.6,.9,.7),src=["MX-TH"]),
 dict(id="T32_roller",bars=32,plan="A x8 bars; at 9 swap bass cell sibling; at 17 add polymeter perc; at 25 mute clap 4 bars + bar-32 dropout",use="long roller: no breakdown, micro-changes",style_w=w(.7,1,.2,.3),src=["BRIZM-MIN","ADB-GENRE"]),
 dict(id="T32_ride_cycle",bars=32,plan="ride/top-loop in for 8, out for 8 (Mr G)",use="top-end breathing",style_w=w(.5,.7,.5,.5),src=["AM-MRG"]),
 dict(id="T32_dj_tool",bars=32,plan="drums only 8 -> +bass 8 -> +perc 8 -> +stab/vox 8",use="DJ-tool, all intro",style_w=w(.6,.8,.2,.4),src=["RS-STRUCT"]),
 dict(id="T32_song_pop",bars=32,plan="8 verse-ish groove (vox phrase) / 8 hook (sung) / 8 breakdown-lite / 8 hook",use="POP: vocal hook as chorus",style_w=w(.1,0,1,.4),src=["DJM-STUSSY","DJM-MAXDEAN"]),
]
full_track_maps = {
 "UK_MIN":"intro 32 (16 beat-only) | drop1 32 | mini-break 8-16 (no kick, stab/vox) | drop2 32 (bass variation) | outro 32 ~ 6:00-6:30 at 130-132 (INFERENCE from Beatport track lengths 6:05-6:58 for UTS20)",
 "LONG":"intro 32-64 | groove 64-96 with one change per 8/16 | optional 16 'breath' w/o kick | groove 64 | outro 32 ~ 7-8 min",
 "POP":"intro 16-32 | drop 32 | breakdown 16 + build 8 | drop2 32 | outro 16-32 ~ 5:20 at 126 (Mix Elite 168-bar template)",
 "GASKIN_90S":"intro 16-32 | groove 32 (2-bar call/answer bass) | break 8-16 with chords/organ | groove 32 | outro 32",
}

operators = [
 ("OP_tail_mutate","change 1-4 events in slots 13-16 only",w(1,1,1,1),["CTR-A7"]),
 ("OP_bar4_answer","bar 4 may rewrite slots 9-16",w(.8,.6,.8,.8),["GR-2"]),
 ("OP_bar8_turn","bar 8: rewrite 4-12 slots, end with >=4-sixteenth hole, <=3 lanes change",w(1,.8,1,1),["GR-2"]),
 ("OP_shift_1","rotate a cell by +/-1 sixteenth (keep off-kick for bass)",w(.6,.6,.5,.5),["GR-3.1"]),
 ("OP_rotate_cell","rotate whole bar by 2/4/8 slots",w(.3,.4,.3,.3),[]),
 ("OP_drop_note","remove 1 onset (space > notes)",w(.8,.9,.5,.6),["BP-MHZ"]),
 ("OP_add_ghost","add ghost (~35% vel) on an empty e/a",w(.6,.6,.5,.7),["BRIZM-RG"]),
 ("OP_octave_flip","move 1 bass note +12 (only styles with octave share >0)",w(.2,0,.6,.2),["CTR-B"]),
 ("OP_partner_swap","swap partner degree (b7<->5<->b3<->2) for one bar",w(.6,.5,.5,.6),["CTR-A3"]),
 ("OP_len_bucket","change note lengths within bucket targets",w(.6,.6,.5,.5),["GR-3.1"]),
 ("OP_vel_to_cutoff","velocity->filter accent pattern rotation (same notes, new groove)",w(.8,.8,.7,.6),["TPS-BASS"]),
 ("OP_mute_lane_n","mute one lane for 2/4/8 bars at a phrase boundary, never two lanes at once",w(.8,.9,.6,.6),["BRIZM-MIN","MONODY"]),
 ("OP_filter_sweep","LPF/HPF automation over 8/16/32/64 bars",w(.7,1,.6,.5),["BRIZM-MIN","SM-STUSSY"]),
 ("OP_polymeter_overlay","switch one lane to 3/5/7-step loop for N bars",w(.4,.8,.2,.2),["BRIZM-MIN"]),
 ("OP_probability","per-step probability 0.6-0.95 on ghosts/perc only (never kick/clap)",w(.5,.7,.3,.4),["BRIZM-MIN"]),
 ("OP_swing_lane","per-lane swing: hats/perc 54-58 UK, 56-63 clap band, 55-60 90s, 60-65 2-step",w(1,1,1,1),["CTR-A8","AM-90S","AM-2STEP","BRIZM-RG"]),
 ("OP_microtime","+/-3-15 ms humanize on hats/perc/vox",w(.6,.6,.4,.6),["BRIZM-RG"]),
 ("OP_callresp_swap","move stab/vox/pluck into the newest bass rest",w(.8,.6,.7,.7),["GR-3.3"]),
 ("OP_delay_throw","1-bar dotted-1/8 delay throw on last stab/vox of phrase",w(.6,.8,.6,.5),["AM-DUB","TS-DUB"]),
 ("OP_cell_sibling","replace cell with a sibling of same type (same density bucket)",w(.8,.8,.8,.8),["GR-1"]),
 ("OP_half_density","thin a lane to half its onsets for 'reveal' bars",w(.6,.7,.5,.5),[]),
 ("OP_kick_breath","drop kick on beat 4 of bar 8 or full bar 16/32",w(.6,.4,.7,.5),["BRIZM-MIN"]),
 ("OP_hat_length","vary hat decay/gate per step (groove from length)",w(.6,.6,.4,.5),["SB-TECHNO","AM-90S"]),
 ("OP_sample_swap","swap one-shot in a lane from the same category of Melak's packs",w(.8,.8,.8,.8),[]),
]
operators=[dict(id=a,desc=b,style_w=c,src=d) for a,b,c,d in operators]

style_params = {
 "UK_MIN":dict(bpm=[129,132],swing_hats=[51,57],swing_clap=[53,63],bass_on_kick_max=0.10,sidechain="heavy (17-38 dB stem dips)",stab_hits_per_bar=[0,3],vocal="sparse phrase/none",octave_share=[0,.1]),
 "LONG":dict(bpm=[126,131],swing_hats=[52,58],swing_clap=[53,60],bass_on_kick_max=0.30,sidechain="heavy",stab_hits_per_bar=[0,2],vocal="texture only",octave_share=[0,0.05],polymeter_lanes_max=2),
 "POP":dict(bpm=[126,132],swing_hats=[50,56],swing_clap=[50,58],bass_on_kick_max=0.05,sidechain="medium-heavy",stab_hits_per_bar=[1,6],vocal="hook/phrase every 4-8 bars",octave_share=[.1,.3]),
 "GASKIN_90S":dict(bpm=[128,133],swing_hats=[54,60],swing_clap=[54,62],bass_on_kick_max=0.35,sidechain="light (3.6-8 dB)",stab_hits_per_bar=[1,8],vocal="chops/shouts",octave_share=[0,.1],two_step_option=True),
}

hard_rules = [
 dict(id="H1_key",rule="all pitched notes in the roll's scale (minor/dorian/phrygian set chosen per roll); chromatic only as a b2 neighbour in GASKIN_90S"),
 dict(id="H2_lowend_clash",rule="no bass onset within 1/32 of a kick unless style allows (GASKIN_90S/LONG caps) AND bass sustain below 120 Hz must be ducked by sidechain; bass mono below 220 Hz"),
 dict(id="H3_no_empty_bars",rule="every bar of every active lane has >=1 event unless the lane is in a planned mute/rest cell; never an all-silent bar; never kick+bass both absent outside a planned breakdown"),
 dict(id="H4_originality",rule="no bar of any lane matches a reference transcription bar at >=0.9 pitch+rhythm similarity (tools/originality_check.py)"),
]
soft = [
 ("S_off_kick_bass","bass onsets prefer e/and/a",0.9),("S_two_pitch_core","top-2 pitches carry most bass time",0.7),
 ("S_bar8_hole","bar 8 ends in a hole",0.7),("S_tail_variation","variation in last 2-4 sixteenths",0.8),
 ("S_stab_offbeat","stabs off the downbeat (<=30% on-beat)",0.7),("S_stab_in_bass_holes","stab in bass rests (UK MIN strong, others weak)",0.5),
 ("S_one_change_at_time","one lane changes per 8 bars in LONG/UK MIN",0.6),("S_swing_light","hats 50-58% in UK styles",0.6),
 ("S_density_contrast","lanes alternate busy/sparse (not all busy)",0.7),("S_space",">=10% silent bass slots",0.6),
 ("S_vocal_in_back_half","vox phrases start in back half of bars 4/8",0.5),("S_root_home","root in top-2 pitches",0.8),
 ("S_polymeter_cap","<=2 polymetric lanes",0.7),("S_octave_style","octave leaps per style share",0.5),
]
soft=[dict(id=a,rule=b,weight=c,freedom_scaled=True) for a,b,c in soft]

out = dict(
 version="genre_wide_v1", created="2026-10-17", grid_legend={"X":"accent","x":"hit","o":"ghost ~35% vel","-":"rest","=":"hold previous note","O":"open hat"},
 slot_convention="16 slots per bar, slot 1 = downbeat, kick slots 1/5/9/13 (same as GENERATOR_RULES)",
 styles=S, style_params=style_params, labels=labels, label_style_weights=label_style,
 cells=dict(cells), polymeter=polymeter, polymeter_rules=rules, templates=templates, full_track_maps=full_track_maps,
 operators=operators, hard_rules=hard_rules, soft_rules=soft,
 notes=["style_w are priors (0-1), multiply with label_style_weights of the user's chosen label flavour if any",
        "inference=true means pattern is an abstraction/ESTIMATE not directly measured or quoted; see GENRE_KNOWLEDGE.md",
        "src keys resolve in SOURCES.md"],
)
json.dump(out, open("/workspace/music-team/research/genre_wide/PATTERN_VOCAB.json","w"), indent=1)
print({k:len(v) for k,v in cells.items()}, "total", sum(len(v) for v in cells.values()), "templates",len(templates),"ops",len(operators),"poly",len(polymeter))
