#!/usr/bin/env python3
"""Validate plugin-design/mix_profiles.json: parses, has schema_version, equals the ```json block embedded in MIX_SPEC.md §8,
matches tests/targets.json, and matches the MIX_SPEC §2.2 / §2.3 / §3 tables. Prints JSON {pass, n_checks, fails}; exit 0/1."""
import json,os,re,sys
H=os.path.dirname(os.path.abspath(__file__)); PD=os.path.dirname(H); ROOT=os.path.dirname(PD)
fails=[]; n=[0]
def ok(c,msg):
    n[0]+=1
    if not c: fails.append(msg)
num=lambda t: float(t.replace('−','-').replace('**','').replace('≤','').replace('+','').strip())
P=json.load(open(os.path.join(ROOT,'resources','mix_profiles.json'))); T=json.load(open(os.path.join(H,'targets.json'))); md=open(os.path.join(ROOT,'docs','mix','MIX_SPEC.md')).read()
ok('schema_version' in P,'schema_version missing')
m=re.search(r'## 8\..*?```json\n(.*?)\n```',md,re.S); ok(m is not None,'no json block in §8')
if m: ok(json.loads(m.group(1))==P,'embedded §8 JSON != mix_profiles.json')
ST=['UKMIN','LONG','POP','GASKIN']
def lane(s,k):
    L=P['dialects'][s]['lanes']; return L['DRUMS']['voices'][k.split(':')[1]] if k.startswith('DRUMS:') else L[k]
# vs targets.json
for s in ST:
    ok(abs(P['dialects'][s]['top_trim_db']-T['styles'][s]['top_trim_db'])<1e-9,f'{s} top_trim')
    for k,v in T['styles'][s]['lanes'].items():
        if k in ('KICK',): k='DRUMS:KICK'
        try: L=lane(s,k)
        except KeyError: ok(False,f'{s} {k} missing in profiles'); continue
        if v.get('st_max_abs') is not None: ok(abs(L['st_target_lufs']-v['st_max_abs'])<=0.051,f'{s} {k} st_target {L["st_target_lufs"]} vs {v["st_max_abs"]}')
        if v.get('re_kick') is not None: ok(abs(L['lufs_offset_vs_kick_db']-v['re_kick'])<=0.051,f'{s} {k} offset {L["lufs_offset_vs_kick_db"]} vs {v["re_kick"]}')
for k,v in T['lanes'].items():
    if k in ('SUM','KICK','BASS:REESE'): continue
    L=lane('UKMIN',k); ok(abs(L['st_target_lufs']-v['st_max_abs'])<=0.051,f'UKMIN {k} st_target vs targets.lanes')
# vs §2.2
sec=lambda a,b: md[md.index(a):md.index(b)]
s22=sec('### 2.2','### 2.3'); map22={'KICK (DRUMS:KICK)':'DRUMS:KICK','BASS':'BASS','STAB':'STAB','PLUCK':'PLUCK','DRUMS:CLAP':'DRUMS:CLAP','DRUMS:SNARE':'DRUMS:SNARE','DRUMS:RIM':'DRUMS:RIM',
  'DRUMS:OH':'DRUMS:OH','DRUMS:CH':'DRUMS:CH','DRUMS:RIDE':'DRUMS:RIDE','DRUMS:PERC':'DRUMS:PERC','VOX':'VOX','HITS':'HITS','PAD':'PAD'}
for row in re.findall(r'^\| ([^|]+?) \| ([^|]+?) \|',s22,re.M):
    if row[0] in map22: ok(abs(num(row[1])-lane('UKMIN',map22[row[0]])['st_target_lufs'])<=0.051,f'§2.2 {row[0]} ST {row[1]}')
# vs §2.3
s23=sec('### 2.3','## 3.'); map23={'KICK':['DRUMS:KICK'],'BASS':['BASS'],'STAB / PLUCK':['STAB','PLUCK'],'CLAP / SNARE':['DRUMS:CLAP','DRUMS:SNARE'],'HATS OH (CH −12, RIDE −18)':['DRUMS:OH'],
  'PERC':['DRUMS:PERC'],'VOX':['VOX'],'RIM':['DRUMS:RIM'],'PAD':['PAD'],'HITS':['HITS']}
for line in s23.splitlines():
    c=[x.strip() for x in line.strip().strip('|').split('|')]
    if not c or c[0] not in map23 or len(c)<5: continue
    for si,s in enumerate(ST):
        parts=[x.strip() for x in c[si+1].split(' · ')]
        rk=re.match(r'[−+\-]?[\d.]+',parts[0].replace('**',''))
        for k in map23[c[0]]:
            L=lane(s,k)
            if rk: ok(abs(num(rk.group(0))-L['lufs_offset_vs_kick_db'])<=0.051,f'§2.3 {c[0]} {s} reK {parts[0]} vs {L["lufs_offset_vs_kick_db"]}')
            if len(parts)>1:
                g=re.match(r'\**(\d+)',parts[1]); 
                if g: ok(int(g.group(1))==L['comp']['target_gr_db'],f'§2.3 {c[0]} {s} GR {parts[1]} vs {L["comp"]["target_gr_db"]}')
            if len(parts)>=5:
                w=re.search(r'[−\-+]?[\d.]+',parts[4])
                if w and L['width'].get('side_mid_db') is not None: ok(abs(num(w.group(0))-L['width']['side_mid_db'])<=0.6,f'§2.3 {c[0]} {s} width {parts[4]} vs {L["width"]["side_mid_db"]}')
                t=re.match(r'([−\-+]?[\d.]+)',parts[3])
                if t and L['tilt']['measured_style_tilt_db_oct'] is not None: ok(abs(num(t.group(1))-L['tilt']['measured_style_tilt_db_oct'])<=0.051,f'§2.3 {c[0]} {s} tilt {parts[3]}')
                tt=re.search(r'\(([−\-+]?[\d.]+)\)',parts[3])
                if tt: ok(abs(num(tt.group(1))-L['tilt']['db_per_oct'])<=0.051,f'§2.3 {c[0]} {s} tilt setting {parts[3]} vs {L["tilt"]["db_per_oct"]}')
# vs §3 attack/release (UK MIN base)
s3=sec('## 3.','## 4.')
def ar(k,a,r): L=lane('UKMIN',k)['comp']; ok(L['attack_ms']==a and L['release_ms']==r,f'§3 {k} attack/release {a}/{r} vs {L["attack_ms"]}/{L["release_ms"]}')
for k,pat in [('DRUMS:KICK',r'### KICK.*?attack \*\*(\d+) ms\*\*, release \*\*(\d+) ms\*\*'),('BASS',r'### BASS.*?attack \*\*(\d+) ms\*\*, release \*\*(\d+) ms\*\*'),
              ('STAB',r'### STAB.*?attack \*\*(\d+) ms\*\*, release \*\*(\d+) ms\*\*'),('VOX',r'### VOX.*?attack (\d+) ms, release (\d+) ms'),
              ('HITS',r'### HITS.*?attack (\d+) ms, release (\d+) ms'),('PAD',r'### PAD.*?attack (\d+) ms, release (\d+) ms')]:
    mm=re.search(pat,s3,re.S); ok(mm is not None,f'§3 {k} comp row not found')
    if mm: ar(k,int(mm.group(1)),int(mm.group(2)))
for v in ['CLAP','SNARE','RIM','CH','OH','RIDE','PERC']:
    mm=re.search(r'^\| '+v+r' \|[^|]*\|[^|]*\| [^/]+/ [^/]+/ \**(\d+)(?: ms)?\** / \**(\d+)(?: ms)?\** /',s3,re.M); ok(mm is not None,f'§3 {v} comp cell not found')
    if mm: ar('DRUMS:'+v,int(mm.group(1)),int(mm.group(2)))
# v1.3a: MAIN auto-trim (E21) and sample prep acceptance (Q15/Q16)
import os
A=P.get('main_auto_trim'); ok(A is not None,'main_auto_trim block missing')
if A:
    # The profile stores "presets/main_sum_trim.json"; this repo keeps the table in resources/.
    tp=os.path.join(ROOT,'resources',os.path.basename(A['table'])); ok(os.path.exists(tp),f'trim table {A["table"]} missing')
    ok(A['ramp']['ms']==50 and A['gate']['sum_st_lufs']==-19.0 and A['gate']['tol_lu']==0.5 and A['gate']['sum_tp_dbtp']==-6.0,'E21 ramp/gate values')
    if os.path.exists(tp):
        TT=json.load(open(tp)); bits=A['lane_bit_order']
        for d,x in P['dialects'].items():
            dm=sum(1<<bits.index(l) for l in x['default_lanes'] if l in bits); e=TT['dialects'][d]['by_mask'].get(str(dm),{})
            ok(abs(e.get('trim_db',99))<=0.005,f'E21 {d} default mask {dm} trim {e.get("trim_db")} != 0 (double-counts output_trim_db)')
ok('sample_prep_acceptance' in P,'sample_prep_acceptance block missing')
ok('9.4 MAIN auto-trim' in md and 'Q16: TP-capped files stay as they are' in md,'MIX_SPEC §9.4 / §4.7 text missing')
# E21 option (a) (DECIDED Maestro 2026-10-17) and the P0 mute-leak contract
TJ=json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'targets.json')))
ok(tuple(int(v) for v in P['schema_version'].split('.'))>=(1,2,0),f'schema_version {P["schema_version"]} < 1.2.0')
if A:
    G=A['gate']; TG=TJ.get('main_auto_trim',{})
    ok(G.get('dense',{}).get('min_active_lanes')==3 and G['dense']['st_window_lufs']==[-19.5,-18.5],'E21 dense gate != -19 +-0.5 for >=3 lanes')
    ok(G.get('sparse',{}).get('max_active_lanes')==2 and G['sparse']['st_window_lufs']==[-20.5,-18.5],'E21 sparse gate != -20.5..-18.5 for 1-2 lanes')
    ok(G.get('tp_cap_dbtp')==-5.0 and G.get('sum_limiter')=='none','E21 TP cap -5 everywhere / no sum limiter')
    ok(TG.get('gate_sparse',{}).get('st_lo')==-20.5 and TG.get('gate_sparse',{}).get('st_hi')==-18.5 and TG.get('gate_sparse',{}).get('max_active_lanes')==2 and TG.get('gate_dense',{}).get('st_lo')==-19.5 and TG.get('tp_guard_dbtp')==-5.0 and TG.get('sum_limiter') is False,'targets.json main_auto_trim gate != profiles')
ML=P.get('engine_contracts',{}).get('mute_leak'); TM=TJ.get('mute_leak',{})
ok(ML is not None and ML['priority']=='P0','engine_contracts.mute_leak missing')
if ML: ok(ML['pass']['resid_max_dbfs_rms']==TM.get('resid_max_dbfs_rms')==-90.0 and ML['pass']['resid_max_re_lane_db']==TM.get('resid_max_re_lane_db')==-60.0,'mute_leak thresholds profiles vs targets')
ok('gate DECIDED by Maestro 2026-10-17, option (a)' in md and '### 5.1 P0 mute-leak test' in md,'MIX_SPEC §9.4 DECIDED / §5.1 text missing')
# tp_guard (MIX_SPEC §9.4b): master true-peak safety, separate from the E21 sum limiter rule.
TPG=P.get('tp_guard'); ok(TPG is not None,'tp_guard missing in mix_profiles.json')
if TPG:
    ok(TPG.get('counts_as_sum_limiter') is False,'tp_guard must not count as the E21 sum limiter')
    ok(TPG.get('ceiling_dbtp')==-6.0 and TPG.get('oversample')==4,'tp_guard ceiling -6 dBTP / 4x oversample')
    ok(0.0<TPG.get('max_gr_db_factory',99)<=1.0,'tp_guard factory GR bar must be <= 1 dB')
    hdr=open(os.path.join(ROOT,'src','Mix','TruePeakGuard.h')).read()
    def cnum(pat):
        m=re.search(pat,hdr); return float(m.group(1)) if m else None
    ok(cnum(r'kDefaultCeilingDbtp\s*=\s*(-?[0-9.]+)f')==TPG['ceiling_dbtp'],'TruePeakGuard.h ceiling != tp_guard.ceiling_dbtp')
    ok(cnum(r'kMarginDb\s*=\s*([0-9.]+)f')==TPG['margin_db'],'TruePeakGuard.h margin != tp_guard.margin_db')
    ok(cnum(r'kTaps\s*=\s*([0-9]+);')==TPG['detector_taps_per_phase'],'TruePeakGuard.h taps != tp_guard.detector_taps_per_phase')
    ok(cnum(r'float lookaheadMs\s*=\s*([0-9.]+)f')==TPG['lookahead_ms'] and cnum(r'float releaseMs\s*=\s*([0-9.]+)f')==TPG['release_ms'],'TruePeakGuard.h lookahead/release != tp_guard')
    ok('### 9.4b Master true-peak guard' in md,'MIX_SPEC §9.4b tp_guard clause missing')
# Gain staging lane slots (resources/lane_targets.json, #36): re_kick follows from the inputs.
LTP=os.path.join(ROOT,'resources','lane_targets.json'); ok(os.path.exists(LTP),'resources/lane_targets.json missing')
if os.path.exists(LTP):
    LT=json.load(open(LTP))
    for s in ST:
        row=LT['dialects'].get(s); ok(row is not None,f'lane_targets {s} missing')
        if not row: continue
        i=row['inputs']; r=row['re_kick_db']; bass=i['bass_vs_kick_db']; other=i['other_vs_mix_db']-i['bass_vs_mix_db']+bass
        for k,want in [('BASS',bass),('STAB',other),('PLUCK',other),('HITS',bass-3.0),('PAD',bass-8.0)]:
            ok(k in r and abs(r[k]-want)<=0.051,f'lane_targets {s} {k} {r.get(k)} != {want:.2f}')
        ok(set(r)<={'BASS','STAB','PLUCK','HITS','PAD'},f'lane_targets {s} sets lanes outside the music lanes: {sorted(r)}')
out={'pass':not fails,'n_checks':n[0],'fails':fails}; print(json.dumps(out,indent=1,ensure_ascii=False)); sys.exit(0 if not fails else 1)
