#!/usr/bin/env python3
"""ROLLA render measurement test (MIX_SPEC.md §5, §9.3). Measurement only: reads WAVs; writes audio only in 'stimulus' mode (synthetic test signal).

usage:
  mix_check.py render  <wav> --lane LANE [--voice VOICE] [--style STYLE] [--kind loop|oneshot]
  mix_check.py sample  <wav> --lane LANE [--voice VOICE]        # sample-prep checks on a prepped (engine-output) one-shot
  mix_check.py styles  <dir>                                   # per-style invariants: <dir>/<STYLE>/{KICK,BASS,SUM}.wav + optional <LANE>.wav / DRUMS_<VOICE>.wav
                                                               #   checks: kick & bass ST / sub+low / <120 energy spread <=1 dB, BASS re KICK, SUM ST & TP per style,
                                                               #   SUM spread, each lane's ST re KICK, side/mid and tilt vs the style table
  mix_check.py scan    <dir> [--lane-from-path]                # run 'render' on every wav under dir, guessing the lane from the path
  mix_check.py combos [<dir>] [--dialect D] [--trim-table presets/main_sum_trim.json]
                       # MAIN auto-trim (PRESET_SPEC v1.3a Q13 / E21): re-derives the trim table and flags conflicts, lists the harness
                       # combo set (default, all six, loudest/quietest per lane count); with <dir>, gates <dir>/<D>/COMBO_<mask>.wav
                       # (mask bits BASS,STAB,PLUCK,HITS,PAD,VOX) at SUM -19.0 +-0.5 LUFS-S (>= 3 lanes) or -20.5..-18.5 (<= 2 lanes, E21 option (a)),
                       # TP -6 +-1 (cap -5 everywhere), corr<120 >= 0.99, limiter GR 0 (no sum limiter)
  mix_check.py mute <render.wav> (--full F --stem S | --expected E | --silent) [--lane-ref R ...] [--lane L] [--dialect D]
                       # P0 mute-leak test (MIX_SPEC §5.1): residual vs the expected render <= -90 dBFS RMS and <= -60 dB re the muted
                       # lane's unmuted level, overall and in every band (20-60/60-250/250-1k/1k-4k/4k-12k/12k-20k Hz)
  mix_check.py stimulus <out.wav> [--seconds 8]               # writes the synthetic switch stimulus (50 Hz line grid, 48 kHz); the ONLY audio this script writes
  mix_check.py switch  <wav> --switch-at S [--ramp-ms 50] [--dialect FROM>TO]
                       # dialect-switch click / gain-jump test (MIX_SPEC §9.3) on a lane render of the stimulus, switch at S seconds
  --dialect / --style  UKMIN | LONG | POP | GASKIN (aliases: 'UK MIN', UK_MIN, 'GASKIN 90s', GASKIN90S); selects style overrides
options: --targets tests/targets.json (default: next to this script)  --json out.json
Exit code 0 = all PASS, 1 = any FAIL, 2 = usage / unreadable.
Measures (all from the file):
  st_max / st_med  short-term loudness, 3 s window, 100 ms hop (pyloudnorm K-weighted, ungated per window)
  integrated       pyloudnorm integrated
  tp_dbtp          4x oversampled peak (scipy resample_poly), dBTP
  sample_peak      dBFS;  plr = tp - st_max;  crest = sample peak - RMS of frames within 40 dB of the loudest 10 ms frame
  corr_lt120       L/R correlation of the signal low-passed at 120 Hz (4th-order Butterworth, zero-phase)
  side_mid_db      side/mid energy ratio, full band and <120 Hz
  dc_dbfs          20log10(|mean|) per channel, worst channel
  lead_silence_ms  time until |x| first exceeds -60 dBFS (sample) ; tail_last10ms_dbfs (RMS of the last 10 ms)
  centroid_hz      power-weighted spectral centroid 20 Hz-20 kHz; band % sub<60 / low 60-200 / lowmid 200-800 / mid 800-5k / high >5k
  tilt_db_oct      least-squares slope of 1/3-octave band powers vs log2(f), bands 200-8k / 1k-16k / 40-400 Hz
  gr               read from optional sidecar <wav>.gr.json {"gr_db_max":x,"gr_db_mean":y} written by the engine; else 'n/a' (not a fail)
"""
import sys, os, json, glob, argparse
import numpy as np, soundfile as sf, scipy.signal as ss
try:
    import pyloudnorm as pyln
except ImportError:
    print('pip install pyloudnorm', file=sys.stderr); sys.exit(2)
HERE=os.path.dirname(os.path.abspath(__file__))
def db(x): return 10*np.log10(np.maximum(x,1e-20))
def load(p):
    x,sr=sf.read(p,dtype='float64',always_2d=True)
    if x.shape[1]==1: x=np.repeat(x,2,1)
    return x[:,:2],sr
def st_loudness(x,sr):
    m=pyln.Meter(sr); n=int(3*sr); h=int(0.1*sr); v=[]
    if len(x)<n:   # one-shots shorter than 3 s: zero-pad to 3 s (= what a 3 s meter window sees)
        x=np.vstack([x,np.zeros((n-len(x),2))])
    for s in range(0,len(x)-n+1,h):
        seg=x[s:s+n]
        if np.mean(seg**2)<1e-14: continue
        L=m.integrated_loudness(seg)
        if np.isfinite(L) and L>-70: v.append(L)
    try: I=m.integrated_loudness(x)
    except Exception: I=None
    return (max(v) if v else None),(float(np.median(v)) if v else None),I
def measure(p):
    x,sr=load(p); r={'file':p,'sr':sr,'dur_s':len(x)/sr}
    smax,smed,I=st_loudness(x,sr); r.update(st_max=smax,st_med=smed,integrated=I if I is None or np.isfinite(I) else None)
    up=ss.resample_poly(x,4,1,axis=0); r['tp_dbtp']=float(20*np.log10(np.abs(up).max()+1e-12)); del up
    sp=float(20*np.log10(np.abs(x).max()+1e-12)); r['sample_peak_dbfs']=sp
    mo=x.mean(1); hop=max(1,int(0.01*sr)); fr=np.mean(mo[:len(mo)//hop*hop].reshape(-1,hop)**2,1) if len(mo)>=hop else np.array([np.mean(mo**2)])
    act=fr>fr.max()*1e-4; r['crest_db']=float(sp-db(fr[act].mean())) if act.any() else None
    r['plr_db']=float(r['tp_dbtp']-smax) if smax is not None else None
    lo=ss.sosfiltfilt(ss.butter(4,120,'low',fs=sr,output='sos'),x,axis=0)
    def corr(z): return float(np.sum(z[:,0]*z[:,1])/np.sqrt(np.sum(z[:,0]**2)*np.sum(z[:,1]**2)+1e-30))
    def sm(z): return float(db(np.sum(((z[:,0]-z[:,1])/2)**2))-db(np.sum(((z[:,0]+z[:,1])/2)**2)))
    r['corr_lt120']=corr(lo); r['side_mid_lt120_db']=sm(lo); r['corr']=corr(x); r['side_mid_db']=sm(x)
    r['lt120_energy_db_re_total']=float(db(np.sum(lo**2))-db(np.sum(x**2)))
    r['dc_dbfs']=float(max(20*np.log10(abs(x[:,c].mean())+1e-12) for c in range(2)))
    nz=np.where(np.abs(x).max(1)>10**(-60/20))[0]; r['lead_silence_ms']=float(nz[0]/sr*1000) if len(nz) else None
    t=x[-max(1,int(0.01*sr)):]; r['tail_last10ms_dbfs']=float(db(np.mean(t**2)))
    f,P=ss.welch(x,fs=sr,nperseg=min(8192,len(x)),axis=0); P=P.sum(1); m=(f>=20)&(f<=20000); tot=P[m].sum()+1e-30
    r['centroid_hz']=float((f[m]*P[m]).sum()/tot)
    r['bands_pct']={k:float(100*P[(f>=a)&(f<b)].sum()/tot) for k,a,b in [('sub',20,60),('low',60,200),('lowmid',200,800),('mid',800,5000),('high',5000,20000)]}
    def tilt(lo,hi):
        cs=lo*2**(np.arange(0,np.log2(hi/lo)*3+1)/3); bp=[db(P[(f>=c*2**(-1/6))&(f<c*2**(1/6))].sum()) for c in cs]
        return float(np.polyfit(np.log2(cs),bp,1)[0])
    r['tilt_db_oct']={'200_8k':tilt(200,8000),'1k_16k':tilt(1000,16000),'40_400':tilt(40,400)}
    g=p+'.gr.json'; r['gr']=json.load(open(g)) if os.path.exists(g) else 'n/a'
    return r
def chk(name,val,lo=None,hi=None,unit=''):
    if val is None: return {'check':name,'value':None,'pass':False,'why':'not measurable'}
    lo=None if lo is None else round(lo,2); hi=None if hi is None else round(hi,2)
    ok=(lo is None or val>=lo) and (hi is None or val<=hi)
    return {'check':name,'value':round(val,2),'range':[lo,hi],'pass':bool(ok),'why':'' if ok else f'{name} {val:.2f}{unit} outside [{lo},{hi}]'}
def lane_target(T,lane,voice,style):
    key=f'{lane}:{voice}' if voice else lane
    if key not in T['lanes']: raise SystemExit(f'no targets for {key}; known: {sorted(T["lanes"])}')
    t=dict(T['lanes'][key]); st=(T.get('styles',{}).get(style,{}) if style else {})
    t.update(st.get('lanes',{}).get(key,{}))
    return t
def render_checks(r,t,kind):
    C=[]
    ab=t.get('st_max_abs')
    if ab is not None: C.append(chk('st_max_LUFS',r['st_max'],ab-t.get('st_tol',1.0),ab+t.get('st_tol',1.0),' LUFS'))
    C.append(chk('true_peak_dBTP',r['tp_dbtp'],None,t.get('tp_max',-6.0)))
    if t.get('corr_lt120_min') is not None and r['lt120_energy_db_re_total']>-40:
        C.append(chk('corr_<120Hz',r['corr_lt120'],t['corr_lt120_min'],None))
    if t.get('lt120_max_db') is not None: C.append(chk('energy_<120Hz_re_total_dB',r['lt120_energy_db_re_total'],None,t['lt120_max_db']))
    if t.get('side_mid_max_db') is not None: C.append(chk('side/mid_dB',r['side_mid_db'],t.get('side_mid_min_db'),t['side_mid_max_db']))
    if t.get('centroid') : C.append(chk('centroid_Hz',r['centroid_hz'],*t['centroid']))
    if t.get('crest'): C.append(chk('crest_dB',r['crest_db'],*t['crest']))
    if t.get('plr') and kind=='loop': C.append(chk('PLR_dB',r['plr_db'],*t['plr']))
    if t.get('tilt'): C.append(chk(f"tilt_{t['tilt']['band']}_dB/oct",r['tilt_db_oct'][t['tilt']['band']],*t['tilt']['range']))
    for b,rng in t.get('bands_pct',{}).items(): C.append(chk(f'band_{b}_%',r['bands_pct'][b],*rng))
    C.append(chk('DC_dBFS',r['dc_dbfs'],None,-60))
    if kind=='oneshot': C.append(chk('lead_silence_ms',r['lead_silence_ms'],0,t.get('lead_max_ms',2.0)))
    if kind=='loop': C.append(chk('tail_last10ms_dBFS',r['tail_last10ms_dbfs'],None,-45))
    if isinstance(r['gr'],dict) and t.get('gr'):
        C.append(chk('GR_max_dB',r['gr'].get('gr_db_max'),*t['gr']))
    return C
def lufs_i(x,sr):
    """BS.1770 integrated (gated). Files < 400 ms: whole-file measure = pad to one 400 ms block and correct for the padding
    (same as samples/_tools/prep.py and MIX_SPEC §4.5)."""
    m=pyln.Meter(sr); nb=int(0.4*sr); n=len(x)
    if n<nb: return float(m.integrated_loudness(np.vstack([x,np.zeros((nb-n,x.shape[1]))]))+10*np.log10(nb/n))
    return float(m.integrated_loudness(x))
PREP={'lufs_target':-24.0,'lufs_tol':0.1,'tp_cap':-6.0,'tp_tol':0.05,'dc_max':-60.0,'lead_max_ms':2.0,'tail_last10_max':-60.0}
def sample_checks(r,t,p=None):
    """MIX_SPEC §4 prep-on-load rules on a staged (prepped) file. Q16 (PRESET_SPEC v1.3a): a file whose TP sits at the -6 dBTP cap
    is accepted as-is (no per-file limiter): its LUFS may be below -24, and TP at the cap (<= -6 + 0.05) is a PASS, never a fail."""
    # LUFS on the file's NATIVE channel count (mono stays mono, BS.1770 weight 1.0), exactly like samples/_tools/prep.py.
    # (load() duplicates mono to 2 ch for the stereo metrics; measuring that would read +3.01 dB on the 15 mono files.)
    xn,sr=sf.read(p,dtype='float64',always_2d=True); L=lufs_i(xn,sr); r['lufs_i']=L; r['channels']=int(xn.shape[1])
    capped=r['tp_dbtp']>=PREP['tp_cap']-0.15; r['tp_capped']=bool(capped)
    C=[chk('DC_dBFS',r['dc_dbfs'],None,PREP['dc_max']),chk('lead_silence_ms@-60dBFS',r['lead_silence_ms'],0,PREP['lead_max_ms']),
       chk('tail_last10ms_dBFS',r['tail_last10ms_dbfs'],None,PREP['tail_last10_max']),chk('true_peak_dBTP (cap)',r['tp_dbtp'],None,PREP['tp_cap']+PREP['tp_tol'])]
    if capped: C.append(chk('LUFS_I (TP-capped: <= -24, Q16 accepted)',L,None,PREP['lufs_target']+PREP['lufs_tol']))
    else: C.append(chk('LUFS_I normalisation',L,PREP['lufs_target']-PREP['lufs_tol'],PREP['lufs_target']+PREP['lufs_tol']))
    return C
def source_fit_checks(r,t):
    """Informational, pre-chain: per-lane spectral/width windows on the raw source (the lane strip is not applied, so these do
    not gate the sample; they show what the strip must correct)."""
    C=[]
    if t.get('centroid'): C.append(chk('src centroid_Hz (pre-chain)',r['centroid_hz'],*t['centroid']))
    if t.get('corr_lt120_min') is not None and r['lt120_energy_db_re_total']>-40: C.append(chk('src corr_<120Hz (pre-chain)',r['corr_lt120'],t['corr_lt120_min'],None))
    if t.get('side_mid_max_db') is not None: C.append(chk('src side/mid_dB (pre-chain)',r['side_mid_db'],t.get('side_mid_min_db'),t['side_mid_max_db']))
    for b,rng in t.get('bands_pct',{}).items(): C.append(chk(f'src band_{b}_% (pre-chain)',r['bands_pct'][b],*rng))
    return C
def styles_checks(d,T):
    out={'dir':d,'styles':{}}; C=[]
    S=sorted([s for s in T['styles'] if os.path.isdir(os.path.join(d,s))])
    if not S: return {'error':f'no style folders under {d}; expected {sorted(T["styles"])}'},[]
    M={s:{} for s in S}
    for s in S:
        for p in sorted(glob.glob(os.path.join(d,s,'*.wav'))):
            lane=os.path.basename(p)[:-4].replace('DRUMS_','DRUMS:')
            if True:
                r=measure(p); b=r['bands_pct']; r['sub_low_db']=float(10*np.log10((b['sub']+b['low'])/100+1e-12))
                M[s][lane]=r
    out['styles']={s:{l:{k:M[s][l][k] for k in ['st_max','tp_dbtp','sub_low_db','corr_lt120','lt120_energy_db_re_total','side_mid_db','tilt_db_oct']} for l in M[s]} for s in S}
    tol=T['invariants'].get('lane_re_kick_tol',1.0)
    for s in S:
        if 'KICK' not in M[s]: continue
        k=M[s]['KICK']['st_max']
        if 'BASS' in M[s]:
            bi=T['invariants']; C.append(chk(f'{s} BASS re KICK dB',M[s]['BASS']['st_max']-k,bi['bass_re_kick']-bi['bass_re_kick_tol'],bi['bass_re_kick']+bi['bass_re_kick_tol']))
        for lane,r in M[s].items():
            if lane in ('KICK','BASS','SUM') or lane.startswith('SUM_') or lane.startswith('COMBO_'): continue
            st=T['styles'][s]['lanes'].get(lane)
            if not st: C.append({'check':f'{s} {lane} known lane','pass':False,'why':f'no style target for {lane}'}); continue
            if st.get('re_kick') is not None: C.append(chk(f'{s} {lane} ST re KICK dB',r['st_max']-k,st['re_kick']-tol,st['re_kick']+tol))
            if st.get('side_mid_max_db') is not None: C.append(chk(f'{s} {lane} side/mid dB',r['side_mid_db'],st['side_mid_min_db'],st['side_mid_max_db']))
            if st.get('tilt'): C.append(chk(f"{s} {lane} tilt {st['tilt']['band']} dB/oct",r['tilt_db_oct'][st['tilt']['band']],*st['tilt']['range']))
    inv=T['invariants']
    for lane in ['KICK','BASS']:
        have=[s for s in S if lane in M[s]]
        if len(have)<2: C.append({'check':f'{lane} present in >=2 styles','pass':False,'why':'missing renders'}); continue
        for key,lab in [('st_max','ST LUFS max'),('sub_low_db','sub+low energy dB'),('lt120_energy_db_re_total','<120 Hz energy dB')]:
            v=[M[s][lane][key] for s in have]; spread=max(v)-min(v)
            C.append(chk(f'{lane} {lab} spread across styles',spread,None,inv['kick_bass_spread_db']))
        C.append(chk(f'{lane} min corr<120 across styles',min(M[s][lane]['corr_lt120'] for s in have),inv['corr_lt120_min'],None))
    for s_ in S:   # any lane combination (PRESET_SPEC v1.3a §0.5.1 / E21): every SUM_*.wav or COMBO_*.wav gets the summed gate
        for nm,r in M[s_].items():
            if nm!='SUM' and (nm.startswith('SUM_') or nm.startswith('COMBO_')): C.extend(sum_gate(r,T,f'{s_} {nm}',mask_n(nm)))
    have=[s for s in S if 'SUM' in M[s]]
    for s in have:
        C.append(chk(f'{s} SUM ST LUFS max',M[s]['SUM']['st_max'],inv['sum_st_max']-inv['sum_st_tol'],inv['sum_st_max']+inv['sum_st_tol']))
        C.append(chk(f'{s} SUM true peak',M[s]['SUM']['tp_dbtp'],inv['sum_tp']-inv['sum_tp_tol'],inv['sum_tp']+inv['sum_tp_tol']))
    if len(have)>=2:
        v=[M[s]['SUM']['st_max'] for s in have]; C.append(chk('SUM ST LUFS spread across styles',max(v)-min(v),None,inv['sum_spread_lu']))
    return out,C
def st_window(T,n_active=None):
    """E21 option (a), DECIDED Maestro 2026-10-17: -19.0 +-0.5 for >= 3 active MAIN lanes; sparse sets (<= 2 active MAIN lanes,
    drums-only included) -19 +0.5/-1.5. n_active None (unknown lane set, e.g. the default SUM) -> dense window."""
    inv=T['invariants']; g=T.get('main_auto_trim',{})
    if n_active is not None and 'gate_sparse' in g and n_active<=g['gate_sparse']['max_active_lanes']: return g['gate_sparse']['st_lo'],g['gate_sparse']['st_hi'],'sparse'
    return inv['sum_st_max']-inv['sum_st_tol'],inv['sum_st_max']+inv['sum_st_tol'],'dense'
def mask_n(nm):
    import re as _re; m=_re.match(r'COMBO_(\d+)$',nm); return bin(int(m.group(1))).count('1') if m else None
def sum_gate(r,T,label,n_active=None):
    inv=T['invariants']; lo,hi,kind=st_window(T,n_active)
    C=[chk(f'{label} SUM ST LUFS max ({kind} gate)',r['st_max'],lo,hi),
       chk(f'{label} SUM true peak',r['tp_dbtp'],inv['sum_tp']-inv['sum_tp_tol'],inv['sum_tp']+inv['sum_tp_tol'])]
    if r['lt120_energy_db_re_total']>-40: C.append(chk(f'{label} SUM corr<120',r['corr_lt120'],inv['corr_lt120_min'],None))
    if isinstance(r.get('gr'),dict) and r['gr'].get('limiter_gr_db_max') is not None: C.append(chk(f'{label} safety limiter GR',r['gr']['limiter_gr_db_max'],None,0.0))
    return C
MAIN_LANES=['BASS','STAB','PLUCK','HITS','PAD','VOX']; DRUM_DEFAULT=['DRUMS:KICK','DRUMS:CLAP','DRUMS:CH','DRUMS:OH','DRUMS:PERC']
def _ps(v): return float(10*np.log10(sum(10**(x/10) for x in v)))
def combo_class(act): return 'D'+('B' if 'BASS' in act else '')+('O' if any(l in act for l in ('STAB','PLUCK','PAD','HITS')) else '')+('V' if 'VOX' in act else '')
def combo_verify(T,tab,dialects):
    """Independent re-derivation of the MAIN auto-trim table (main_sum_trim.json) from targets.json lane targets
    (styles.<D>.lanes.*.st_max_abs, which already include the per-style TOP trim) + drum default voices, the MIX calibration,
    STAB+PLUCK each -3 dB. Flags: |table - recomputed| > 0.5 dB; default-set trim != 0 (double-counts output_trim_db);
    measured lane-set dependence of the calibration (targets.combo_model) moving the right trim by > 0.5 dB;
    estimated summed TP after trim > -5.0 dBTP (TP_est = -6 + PLR(class) - PLR(default class), measured ref PLRs).
    Gate (E21 option (a), DECIDED 2026-10-17): the predicted ST after the -5 dBTP guard (no sum limiter) must sit in -19 +-0.5 for
    >= 3 active MAIN lanes, -20.5..-18.5 for <= 2 (drums-only included). Cal shift and TP_est are reported per row, gated through it."""
    CM=T.get('combo_model',{}); out={}; C=[]; tol=T['invariants']['sum_st_tol']
    for D in dialects:
        L=T['styles'][D]['lanes']; cal=CM.get(D,{}).get('cal_default',T['anchor'].get('model_calibration_db',1.0))
        lv=lambda l: (T['lanes']['KICK']['st_max_abs'] if l=='DRUMS:KICK' else (L.get(l,{}).get('st_max_abs') if L.get(l,{}).get('st_max_abs') is not None else T['anchor']['kick_st_max']+L[l]['re_kick']))
        drums=[lv(v) for v in DRUM_DEFAULT]
        dflt=[l for l in MAIN_LANES if l in T['styles'][D]['default_lanes']]
        def model(act): return _ps([lv(l)-(3 if ('STAB' in act and 'PLUCK' in act and l in ('STAB','PLUCK')) else 0) for l in act]+drums)
        md=model(dflt); dc=combo_class(dflt); rows=[]
        tabD=tab['dialects'][D]['by_mask']
        for mask in range(64):
            act=[l for i,l in enumerate(MAIN_LANES) if mask>>i&1]; exp=round(md-model(act),2); tt=tabD[str(mask)]['trim_db']
            cls=combo_class(act); calc=CM.get(D,{}).get('cal',{}); dcal=(calc.get(cls,cal)-calc.get(dc,cal)) if calc else 0.0
            plr=CM.get(D,{}).get('plr',{}); tp_est=-6.0+((plr.get(cls,0)-plr.get(dc,0)) if plr else 0.0)
            exp_u=exp; exp=round(exp_u+dcal,2)   # class-aware recompute (targets.combo_model cal per class); uniform-cal value kept as info
            row={'mask':mask,'lanes':act,'n':len(act),'table_trim_db':tt,'recomputed_trim_db':exp,'recomputed_uniform_cal_db':exp_u,'diff_db':round(tt-exp,2),
                 'measured_cal_shift_db':round(dcal,2),'tp_est_dbtp':round(tp_est,2)}
            # predicted sum with the table trim: ST_pre = -19 + (table - class-aware recomputed trim); TP_pre = TP_est + (ST_pre + 19)
            st_pre=-19.0+(tt-exp); tp_pre=tp_est+(st_pre+19.0); cap=T.get('main_auto_trim',{}).get('tp_guard_dbtp',-5.0)
            ex=max(0.0,tp_pre-cap); st_post=st_pre-ex; lo,hi,kind=st_window(T,len(act))
            row.update({'st_pred_lufs':round(st_pre,2),'tp_pred_dbtp':round(tp_pre,2),'guard_cut_db':round(ex,2),'trim_after_guard_db':round(tt-ex,2),
                        'st_after_guard_lufs':round(st_post,2),'tp_after_guard_dbtp':round(tp_pre-ex,2),'gate':kind,'gate_window':[lo,hi],'gate_pass':bool(lo-1e-9<=st_post<=hi+1e-9)})
            rows.append(row)
            if abs(tt-exp)>tol: C.append({'check':f'{D} mask {mask} {act} table vs recomputed','value':row['diff_db'],'pass':False,'why':f'table {tt} vs recomputed {exp} dB (> {tol})'})
            C.append(chk(f'{D} mask {mask} {"+".join(act) or "drums-only"} predicted SUM ST after -5 dBTP guard ({kind} gate)',round(st_post,2),lo,hi))
        dm=sum(1<<MAIN_LANES.index(l) for l in dflt)
        C.append(chk(f'{D} default-set trim (must be 0: output_trim_db covers it)',tabD[str(dm)]['trim_db'],-0.005,0.005))
        out[D]={'default_lanes':dflt,'default_mask':dm,'model_default':round(md,2),'rows':rows}
    return out,C
def combo_plan(tab,D):
    """E21 harness set: default, all six, loudest (min trim) and quietest (max trim) combo per lane count."""
    bm=tab['dialects'][D]['by_mask']; plan={}
    dm=[int(k) for k,v in bm.items() if v['trim_db']==0.0 and v['lanes']==[l for l in MAIN_LANES if l in tab['dialects'][D]['default_main_lanes']]]
    plan['default']=dm[0] if dm else None; plan['all_six']=63
    for n in range(0,7):
        c=[(v['trim_db'],int(k)) for k,v in bm.items() if len(v['lanes'])==n]
        plan[f'n{n}_loudest']=min(c)[1]; plan[f'n{n}_quietest']=max(c)[1]
    return plan
def combos_mode(d,T,tabp,dialects):
    tab=json.load(open(tabp)); out={'trim_table':tabp}; C=[]
    v,Cv=combo_verify(T,tab,dialects); out['verify']=v; C+=Cv; out['plan']={}; out['renders']={}
    for D in dialects:
        plan=combo_plan(tab,D); out['plan'][D]={k:{'mask':m,'lanes':tab['dialects'][D]['by_mask'][str(m)]['lanes'],'file':f'{D}/COMBO_{m}.wav'} for k,m in plan.items()}
        if d:
            for k,m in plan.items():
                p=os.path.join(d,D,f'COMBO_{m}.wav')
                if os.path.exists(p): r=measure(p); out['renders'][f'{D}/{m}']={x:r[x] for x in ('st_max','tp_dbtp','corr_lt120')}; C+=sum_gate(r,T,f'{D} COMBO_{m} ({k})',bin(m).count('1'))
                else: C.append({'check':f'{D} COMBO_{m} ({k}) render present','pass':False,'why':f'missing {p}'})
    return out,C
def _rms_db(x): return float(db(np.mean(np.asarray(x,dtype=np.float64)**2)))
def _read_exact(p):
    x,sr=sf.read(p,dtype='float64',always_2d=True); return x,sr,sf.info(p).subtype
def _sum_files(ps):
    acc=None; sr0=None
    for q in ps:
        x,sr,_=_read_exact(q); sr0=sr0 or sr
        if sr!=sr0: raise ValueError(f'sample-rate mismatch {q}')
        if acc is None: acc=x.copy()
        else:
            n=max(len(acc),len(x)); c=max(acc.shape[1],x.shape[1]); a=np.zeros((n,c)); a[:len(acc),:acc.shape[1]]+=acc; a[:len(x),:x.shape[1]]+=x; acc=a
    return acc,sr0
def mute_leak(render,T,full=None,stem=None,expected=None,silent=False,lane_ref=None,lane=None,max_lag=2048):
    """P0 mute-leak test (MIX_SPEC §5.1). A muted lane must contribute nothing.
    residual = render - expected, sample-aligned (lag 0), where expected is
      (a) --full F --stem S : F - S  (full render minus the muted lane's solo stem; render = full mix with that lane muted), or
      (b) --expected E      : E      (e.g. the clean solo of another lane when every other lane is muted), or
      (c) --silent          : 0      (render of only the muted lane, with the lane muted).
    Lane level (the muted lane's unmuted level) = RMS of --stem (a) or of --lane-ref (sum of the given files, (b)/(c)).
    PASS: residual RMS <= -90 dBFS AND <= lane level - 60 dB, overall and in every band of targets.mute_leak.bands_hz."""
    M=T.get('mute_leak',{'resid_max_dbfs_rms':-90.0,'resid_max_re_lane_db':-60.0,'bands_hz':[[20,60],[60,250],[250,1000],[1000,4000],[4000,12000],[12000,20000]]})
    x,sr,sub=_read_exact(render); C=[]; info={'render':render,'subtype':sub,'sr':sr,'frames':len(x)}
    if full and stem:
        f,srf,_=_read_exact(full); st,srs,_=_read_exact(stem); mode='full_minus_stem'; refs=[stem]
        C.append(chk('sample rates equal',float(len({sr,srf,srs})==1),1,1)); C.append(chk('lengths equal (frames)',float(len(x)==len(f)==len(st)),1,1))
        n=min(len(x),len(f),len(st)); e=f[:n]-st[:n]; lv=st[:n]
    elif expected:
        e,sre,_=_read_exact(expected); mode='expected'; C.append(chk('sample rates equal',float(sr==sre),1,1)); C.append(chk('lengths equal (frames)',float(len(x)==len(e)),1,1))
        n=min(len(x),len(e)); e=e[:n]; lv=None; refs=lane_ref or []
    elif silent: mode='silent'; n=len(x); e=np.zeros_like(x); lv=None; refs=lane_ref or []
    else: raise ValueError('mute needs --full+--stem, --expected or --silent')
    x=x[:n]; c=max(x.shape[1],e.shape[1])
    if x.shape[1]<c: x=np.repeat(x,c,1)
    if e.shape[1]<c: e=np.repeat(e,c,1)
    if lv is None and refs:
        lv,_=_sum_files(refs); lv=lv[:n]
    res=x-e; R=_rms_db(res); info.update({'mode':mode,'lane':lane,'lane_ref':refs,'resid_rms_dbfs':round(R,2)})
    L=_rms_db(lv) if lv is not None else None; info['lane_level_rms_dbfs']=round(L,2) if L is not None else None
    C.append(chk('residual RMS dBFS',round(R,2),None,M['resid_max_dbfs_rms']))
    if L is not None: C.append(chk('residual re muted-lane level dB',round(R-L,2),None,M['resid_max_re_lane_db']))
    else: C.append({'check':'residual re muted-lane level dB','value':None,'pass':False,'why':'no lane level: give --stem or --lane-ref (the muted lane unmuted)'})
    bands={}
    for lo,hi in M['bands_hz']:
        if lo>=sr/2: continue
        so=ss.butter(4,[lo,min(hi,0.45*sr)],btype='band',fs=sr,output='sos'); rb=_rms_db(ss.sosfiltfilt(so,res,axis=0))
        bands[f'{lo}-{hi}']=round(rb,2); C.append(chk(f'band {lo}-{hi} Hz residual RMS dBFS',round(rb,2),None,M['resid_max_dbfs_rms']))
        if L is not None: C.append(chk(f'band {lo}-{hi} Hz residual re lane dB',round(rb-L,2),None,M['resid_max_re_lane_db']))
    info['bands_resid_dbfs']=bands
    w=int(0.4*sr); h=int(0.1*sr); fr=[_rms_db(res[i:i+w]) for i in range(0,max(1,n-w+1),h)]; info['resid_max_400ms_dbfs']=round(max(fr),2)
    # info: best lag (a misaligned render can mask or fake a leak) and residual there
    a=x.mean(1); b=e.mean(1)
    if max_lag and np.any(b):
        cc=ss.correlate(a,b,mode='full',method='fft'); mid=len(b)-1; seg=cc[mid-max_lag:mid+max_lag+1]; lag=int(np.argmax(seg)-max_lag); info['best_lag_samples']=lag
    if sub=='PCM_16': info['warning']='16-bit file: the quantisation floor (~-101 dBFS RMS) and any dither limit the null; render float32, no dither'
    # info: which muted lane leaks (correlation of the residual with each --lane-ref file)
    if refs and len(refs)>1:
        att={}
        for q in refs:
            y,_=_sum_files([q]); y=y[:n]
            if y.shape[1]<c: y=np.repeat(y,c,1)
            att[os.path.basename(os.path.dirname(q))+'/'+os.path.basename(q)]=round(float(np.sum(res*y)/np.sqrt(np.sum(res**2)*np.sum(y**2)+1e-30)),3)
        info['resid_corr_with_lane_ref']=att
    return info,C
def guess_lane(p,T):
    u=p.upper()
    for k in sorted(T['lanes'],key=len,reverse=True):
        lane,_,voice=k.partition(':')
        if (voice and voice in os.path.basename(u)) and lane in u: return lane,voice
    for k in T['lanes']:
        if ':' not in k and k in u: return k,None
    return None,None
def norm_dialect(d):
    if d is None: return None
    u=d.upper().replace(' ','').replace('_','').replace('-','')
    m={'UKMIN':'UKMIN','LONG':'LONG','POP':'POP','GASKIN':'GASKIN','GASKIN90S':'GASKIN','GASKIN90':'GASKIN'}
    if u not in m: raise SystemExit(f'unknown dialect {d}; use UKMIN|LONG|POP|GASKIN')
    return m[u]
STIM_F0=50.0   # switch stimulus: line spectrum on multiples of 50 Hz -> exactly periodic every 20 ms
def stimulus(sr,sec):
    k=np.unique(np.round(np.geomspace(1,80,24)).astype(int))           # 50 Hz .. 4 kHz, log-spaced harmonics (nothing above 4 kHz)
    t=np.arange(int(sec*sr))/sr; ph=np.random.default_rng(7).uniform(0,2*np.pi,len(k))
    x=sum((k_**-0.5)*np.sin(2*np.pi*STIM_F0*k_*t+p_) for k_,p_ in zip(k,ph))   # -3 dB/oct (pink-like)
    x=x/np.abs(x).max()*10**(-12/20); return np.c_[x,x]
def switch_checks(p,sw,ramp_ms,T):
    """Dialect-switch click / gain-jump test (MIX_SPEC §9.3) on a render of the periodic switch stimulus through a lane
    (stimulus loaded as the lane's sample), dialect switched at sw seconds. Steady state repeats exactly every 20 ms, so
    (a) 20 ms frame level (1 ms hop) is constant except for the processing change -> gain jump = max frame-to-frame (20 ms apart)
        step minus the declared ramp slope; (b) energy >=2 kHz that lies >=18.75 Hz off the 50 Hz line grid (160 ms Hann FFT, sliding 2 ms, dB re on-grid energy)
        = clicks / discontinuities / coefficient-swap transients; reported as dB above max(steady pre/post floor, -90 dB).
        Smooth ramps (>=50 ms) only put sidebands within ~20 Hz of the lines; distortion products stay on the grid."""
    x,sr=load(p); inv=T.get('switch_test',{}); thr_click=inv.get('max_click_excess_db',6.0); thr_jump=inv.get('max_gain_jump_db',0.5)
    Pn=int(round(sr/STIM_F0)); s0=int(round(sw*sr)); rn=int(ramp_ms/1000*sr); g=int(0.03*sr); n=len(x)
    if s0<int(0.6*sr) or s0+rn+int(0.6*sr)>n or abs(sr/STIM_F0-Pn)>1e-9:
        return {'error':'need >=0.6 s before the switch and ramp+0.6 s after it; sr must be a multiple of 50 Hz'},[{'check':'switch render usable','pass':False,'why':'too short or bad sr'}]
    mo=x.mean(1); c=np.cumsum(np.r_[0,mo**2]); E=lambda a: 10*np.log10(np.maximum((c[a+Pn]-c[a])/Pn,1e-14))
    hop=max(1,int(0.001*sr)); idx=np.arange(s0-g,s0+rn+g,hop); L=E(idx); kf=Pn//hop
    steps=np.abs(L[kf:]-L[:-kf]); pre=E(np.arange(s0-int(0.4*sr),s0-g,hop)); post=E(np.arange(s0+rn+g,s0+rn+int(0.4*sr),hop))
    total=float(np.median(post)-np.median(pre)); expected=(np.pi/2)*abs(total)*(Pn/sr*1000)/max(ramp_ms,1.0)   # raised-cosine ramp: peak slope = pi/2 x linear
    jump=float(steps.max()-expected)
    # (b) click: the stimulus has no energy above 4 kHz, and smooth gain/filter/comp ramps cannot create any; clicks, sample
    #     steps and coefficient-swap transients are broadband. HF = 8th-order Butterworth HP 8 kHz (zero-phase); level in 5 ms
    #     windows (1 ms hop) re the stimulus RMS. Steady saturation harmonics set the floor (max of the pre/post steady windows).
    hp=ss.sosfiltfilt(ss.butter(8,8000,'high',fs=sr,output='sos'),mo); ref=db(np.mean(mo[s0-int(0.4*sr):s0-g]**2))
    c2=np.cumsum(np.r_[0,hp**2]); w5=int(0.005*sr); H=lambda a: db((c2[a+w5]-c2[a])/w5)-ref
    floor=max(H(np.arange(s0-int(0.4*sr),s0-g-w5,hop)).max(),H(np.arange(s0+rn+g,s0+rn+int(0.4*sr)-w5,hop)).max(),-100.0)
    worst=float(H(np.arange(s0-g,s0+rn+g,hop)).max())
    click=float(worst-floor)
    r={'file':p,'switch_at_s':sw,'ramp_ms':ramp_ms,'total_level_change_db':total,'max_20ms_level_step_db':float(steps.max()),'expected_ramp_step_db':expected,
       'gain_jump_excess_db':jump,'hf_floor_db':float(floor),'hf_worst_in_switch_db':worst,'click_excess_db':click}
    C=[chk('gain jump beyond declared ramp (dB, 20 ms frames)',jump,None,thr_jump),
       chk('click: >8 kHz burst above steady floor (dB, 5 ms windows)',click,None,thr_click)]
    return r,C
def main():
    ap=argparse.ArgumentParser(); ap.add_argument('mode',choices=['render','sample','styles','scan','switch','stimulus','combos','mute']); ap.add_argument('path',nargs='?')
    ap.add_argument('--lane'); ap.add_argument('--voice'); ap.add_argument('--style','--dialect',dest='style'); ap.add_argument('--switch-at',type=float); ap.add_argument('--seconds',type=float,default=8.0); ap.add_argument('--trim-table',default=os.path.join(os.path.dirname(HERE),'presets','main_sum_trim.json')); ap.add_argument('--ramp-ms',type=float,default=120.0); ap.add_argument('--kind',default='loop',choices=['loop','oneshot'])
    ap.add_argument('--full'); ap.add_argument('--stem'); ap.add_argument('--expected'); ap.add_argument('--silent',action='store_true'); ap.add_argument('--lane-ref',nargs='+'); ap.add_argument('--max-lag',type=int,default=2048)
    ap.add_argument('--targets',default=os.path.join(HERE,'targets.json')); ap.add_argument('--json')
    A=ap.parse_args(); T=json.load(open(A.targets)); res=[]
    if A.mode=='stimulus':
        sf.write(A.path,stimulus(48000,A.seconds),48000,subtype='FLOAT'); print(json.dumps({'wrote':A.path,'sr':48000,'seconds':A.seconds})); sys.exit(0)
    if A.mode!='switch': A.style=norm_dialect(A.style)
    if A.mode in('render','sample'):
        t=lane_target(T,A.lane,A.voice,A.style); r=measure(A.path)
        C=render_checks(r,t,A.kind) if A.mode=='render' else sample_checks(r,t,A.path)
        res.append({'file':A.path,'lane':A.lane,'voice':A.voice,'style':A.style,'measure':r,'checks':C})
    elif A.mode=='combos':
        Ds=[A.style] if A.style else ['UKMIN','LONG','POP','GASKIN']
        o,C=combos_mode(A.path,T,A.trim_table,Ds); res.append({'combos':o,'checks':C})
    elif A.mode=='mute':
        r,C=mute_leak(A.path,T,A.full,A.stem,A.expected,A.silent,A.lane_ref,A.lane,A.max_lag); res.append({'file':A.path,'lane':A.lane,'dialect':A.style,'measure':r,'checks':C})
    elif A.mode=='switch':
        if A.switch_at is None: print('switch needs --switch-at',file=sys.stderr); sys.exit(2)
        r,C=switch_checks(A.path,A.switch_at,A.ramp_ms,T); res.append({'file':A.path,'dialect_switch':A.style,'measure':r,'checks':C})
    elif A.mode=='styles':
        o,C=styles_checks(A.path,T); res.append({'styles':o,'checks':C})
    else:
        for p in sorted(glob.glob(os.path.join(A.path,'**','*.wav'),recursive=True)):
            lane,voice=guess_lane(p,T)
            r=measure(p)
            if lane is None: res.append({'file':p,'lane':None,'measure':r,'checks':[{'check':'lane identifiable from path','pass':False,'why':'cannot map file to a lane; measured only'}]}); continue
            t=lane_target(T,lane,voice,None); kind='oneshot' if r['dur_s']<2.5 else 'loop'
            res.append({'file':p,'lane':lane,'voice':voice,'kind':kind,'measure':r,'checks':render_checks(r,t,kind)})
    allpass=all(c['pass'] for x in res for c in x['checks'])
    out={'pass':allpass,'results':res}
    s=json.dumps(out,indent=1,default=float)
    if A.json: open(A.json,'w').write(s)
    fl=[(x.get('file'),c['why']) for x in res for c in x['checks'] if not c['pass']]
    print(s if not A.json else json.dumps({'pass':allpass,'n_fails':len(fl),'fails':fl[:200]},indent=1))
    sys.exit(0 if allpass else 1)
if __name__=='__main__': main()
