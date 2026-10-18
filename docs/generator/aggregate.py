"""Aggregate measured drop stats (transcriptions/<track>/drop_*.json) into one table for GENERATOR_RULES.md."""
import json, glob, os, statistics as st
OUT={}
for f in sorted(glob.glob('/workspace/music-team/transcriptions/*/drop_*.json')):
    d=json.load(open(f)); tr=f.split('/')[-2]; key=f"{tr}:{os.path.basename(f)[5:-5]}"
    b=d['bass_stats']; nb=d['bars'][1]-d['bars'][0]+1
    deg=b.get('degree_time_share_pct',{}); top=sorted(deg.values(),reverse=True)
    oc=b['onset_count_by_slot_1to16']; tot=sum(oc) or 1
    # per-bar onset count spread
    bpm=d['bpm']; bar=240/bpm; t0=min(n['t'] for n in d['bass_notes']) if d['bass_notes'] else 0
    per=[0]*nb
    for n in d['bass_notes']:
        k=int((n['t'])//bar)
        if 0<=k<nb: per[k]+=1
    so=d['stabs_other']['stats']; dr=d['drums']
    sil=d.get('bass_silent_slot_frac_lt-20dB') or []
    OUT[key]=dict(bars=nb,bpm=bpm,tonic=d['tonic'],
      bass_notes_per_bar=b['notes_per_bar'],bass_per_bar_min_max=[min(per),max(per)],bass_per_bar_sd=round(st.pstdev(per),2),
      bass_len16_median=b['len16_median'],bass_len16_p25_p75=b['len16_p25_p75'],bass_len16_hist=b['len16_hist'],
      bass_onset_slot_pct=[round(100*x/tot,1) for x in oc],bass_in_beat_pct=b['onset_in_beat_pct'],bass_on_kick_pct=b['attacks_on_kick_pct'],
      bass_degree_share=deg,bass_root_share=deg.get('1'),bass_top2_share=round(sum(top[:2]),1),bass_distinct_per_bar=b['distinct_pitches_per_bar_mean'],
      bass_range=b['range'],bass_octave_jump_pct=b['octave_jump_pct'],bass_same_pitch_repeat_pct=b['same_pitch_repeat_pct'],bass_intervals=b['interval_counts'],
      bass_glide_pct=b['glide_pct_of_notes'],bass_swing=b['bass_timing'].get('swing_pct'),bass_sounding_frac=d.get('bass_sounding_frac_time_gt-20dB'),
      bass_silent_slot_frac_mean=round(sum(sil)/len(sil),2) if sil else None,bass_repeat_match=d['bass_repeat_match_by_lag_bars'],
      sidechain_bass=(d['sidechain'].get('bass') or {}).get('dip_db_median'),sidechain_rec_ms=(d['sidechain'].get('bass') or {}).get('recovery_to_minus3dB_ms_median'),
      stab_per_bar=so.get('per_bar'),stab_onset_slot=so.get('onset_count_by_slot_1to16'),stab_in_beat=so.get('in_beat_pct'),stab_len16_median=so.get('len16_median'),
      stab_len16_p25_p75=so.get('len16_p25_p75'),stab_top_sets=so.get('top_degree_sets'),
      stab_bass_same_slot_overlap=round(sum(min(a/(sum(oc) or 1),bb/(sum(so.get('onset_count_by_slot_1to16') or [1]) or 1)) for a,bb in zip(oc,so.get('onset_count_by_slot_1to16') or [0]*16)),2) if so.get('onset_count_by_slot_1to16') else None,
      chops=d['chops_vocals'].get('stats'),swing=d['swing'],
      drums={k:[round(x,2) for x in v['hit_frac_by_slot']] for k,v in dr.items() if isinstance(v,dict) and 'hit_frac_by_slot' in v},
      hat_decay_ms=d.get('hat_decay15dB_ms'))
json.dump(OUT,open('/workspace/music-team/plugin-design/generator_rules_data/measured_drops.json','w'),indent=1)
for k,v in OUT.items():
    print(k,'| npb',v['bass_notes_per_bar'],v['bass_per_bar_min_max'],'sd',v['bass_per_bar_sd'],'| len',v['bass_len16_median'],v['bass_len16_p25_p75'],'| onkick',v['bass_on_kick_pct'],'| root',v['bass_root_share'],'top2',v['bass_top2_share'],'| dist',v['bass_distinct_per_bar'],'| rng',v['bass_range'],'| oct',v['bass_octave_jump_pct'],'| rep',v['bass_same_pitch_repeat_pct'],'| sound',v['bass_sounding_frac'],'silent',v['bass_silent_slot_frac_mean'],'| match',v['bass_repeat_match'],'| sc',v['sidechain_bass'],v['sidechain_rec_ms'])
    print('   stab/bar',v['stab_per_bar'],'inbeat',v['stab_in_beat'],'len',v['stab_len16_median'],'ovl',v['stab_bass_same_slot_overlap'],'| slot%',v['bass_onset_slot_pct'],'| chops',(v['chops'] or {}).get('per_bar'),(v['chops'] or {}).get('in_beat_pct'),(v['chops'] or {}).get('len16_median'))
