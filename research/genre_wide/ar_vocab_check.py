#!/usr/bin/env python3
"""ar_vocab_check.py -- A&R sanity check of PATTERN_VOCAB.json against the reference MIDI transcriptions.
Low memory: only mido + stdlib, reads small MIDI files one at a time, never touches audio.
Usage: python3 ar_vocab_check.py [--json out.json]
Measures every reference drop (bass, stabs, chops, drums) and checks each vocab cell grid that makes a
rhythmic claim against: (a) the measured cross-track ranges, (b) best attack-set match vs every reference bar.
"""
import json, os, sys, statistics as st
from collections import Counter, defaultdict
import mido

T = "/workspace/music-team/transcriptions"
V = os.path.join(os.path.dirname(os.path.abspath(__file__)), "PATTERN_VOCAB.json")
GROUP = {"lightleak_memphis": "UK", "local_dub_v_line_cruiza": "UK", "localdub_reaction": "UK",
         "stef_davidse_way_back": "UK", "josh_baker_something_to_me": "UK", "laidlaw_house_of_dub": "UK",
         "m_high_guess_you_know": "UK", "gaskin_wax_master": "GASKIN", "ellia_jaya_lovers_in_the_dance": "ELLIA"}
KICK, CLAP, CH, OH = 36, 39, 42, 46

def load(path):
    m = mido.MidiFile(path); ppq = m.ticks_per_beat; out = []
    for tr in m.tracks:
        t = 0; on = defaultdict(list)
        for msg in tr:
            t += msg.time
            if msg.type == "note_on" and msg.velocity > 0: on[msg.note].append((t, msg.velocity))
            elif msg.type in ("note_on", "note_off") and on[msg.note]:
                s, v = on[msg.note].pop(0); out.append((s / ppq * 4, t / ppq * 4, msg.note, v))
    return sorted(out)

def drops(track):
    d = os.path.join(T, track); fs = sorted(os.listdir(d))
    if track == "localdub_reaction":
        return [("main", {"bass": os.path.join(d, "bass.mid"), "stabs": os.path.join(d, "stabs.mid")}, 8)]
    res = []
    for tag in ["main"] + sorted({f.split("_", 1)[1][:-4] for f in fs if f.startswith("bass_") and f.endswith(".mid")}):
        suf = "" if tag == "main" else "_" + tag
        parts = {p: os.path.join(d, p + suf + ".mid") for p in ("bass", "stabs", "chops", "drums")
                 if os.path.exists(os.path.join(d, p + suf + ".mid"))}
        res.append((tag, parts, None))
    return res

def mono(notes):
    by = {}
    for s, e, p, v in notes:
        k = round(s)
        if k not in by or p < by[k][2]: by[k] = (s, e, p, v)
    ev = [by[k] for k in sorted(by)]
    return [(s, e, p, v, max(0.05, min(e, ev[i + 1][0]) - s) if i + 1 < len(ev) else max(0.05, e - s))
            for i, (s, e, p, v) in enumerate(ev)]

def swing(times):
    odd = [t - round(t) for t in times if round(t) % 2 == 1]; even = [t - round(t) for t in times if round(t) % 2 == 0]
    if len(odd) < 4 or len(even) < 4: return None
    return round(50 + 50 * (st.median(odd) - st.median(even)), 1)

def bar_sets(times, bars):
    s = [set() for _ in range(bars)]
    for t in times:
        k = int(round(t))
        if 0 <= k < bars * 16: s[k // 16].add(k % 16)
    return s

def rep(tok, lag):
    na = lambda x: "_" if x in ".-" else x
    pr = [(na(tok[i]), na(tok[i + lag])) for i in range(len(tok) - lag) if not (na(tok[i]) == "_" == na(tok[i + lag]))]
    return round(sum(a == b for a, b in pr) / len(pr), 2) if pr else None

NN = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
def name(p): return f"{NN[int(p) % 12]}{int(p) // 12 - 1}"
DEG = {0: "1", 1: "b2", 2: "2", 3: "b3", 4: "3", 5: "4", 6: "b5", 7: "5", 8: "b6", 9: "6", 10: "b7", 11: "7"}

def measure_bass(notes, bars, kicks):
    bn = [n for n in mono(notes) if n[0] < bars * 16 - 0.5]
    att = [n[0] for n in bn]; L = bars * 16
    near = lambda t: any(abs(t - k) <= 0.3 for k in kicks)
    m = {"n": len(bn), "notes_per_bar": round(len(bn) / bars, 2)}
    m["on_kick_pct"] = round(100 * sum(map(near, att)) / len(att), 1)
    pos = Counter(int(round(t)) % 4 for t in att)
    m["in_beat_pct"] = {k: round(100 * pos[i] / len(att), 1) for i, k in enumerate(["on", "e", "and", "a"])}
    tpc = Counter()
    for s, e, p, v, ln in bn: tpc[p % 12] += ln
    tot = sum(tpc.values()); top = tpc.most_common(); root = top[0][0]
    m["top2_pct"] = round(100 * (top[0][1] + (top[1][1] if len(top) > 1 else 0)) / tot, 1)
    m["degrees_time_pct"] = {DEG[(pc - root) % 12]: round(100 * v / tot, 1) for pc, v in top[:5]}
    m["len_med16"] = round(st.median(n[4] for n in bn), 2)
    lens = [n[4] for n in bn]
    m["len_p10_p90"] = [round(sorted(lens)[int(0.1 * (len(lens) - 1))], 2), round(sorted(lens)[int(0.9 * (len(lens) - 1))], 2)]
    m["long_notes_ge6_pct"] = round(100 * sum(1 for x in lens if x >= 6) / len(lens), 1)
    ps = [n[2] for n in bn]
    m["register"] = f"{name(min(ps))}-{name(max(ps))} (median {name(st.median(ps))})"
    m["median_midi"] = st.median(ps)
    mv = [ps[i + 1] - ps[i] for i in range(len(ps) - 1)]
    m["octave_move_pct"] = round(100 * sum(1 for x in mv if abs(x) in (12, 24)) / max(1, len(mv)), 1)
    nz = [x for x in mv if x]
    m["step_moves_pct_of_changes"] = round(100 * sum(1 for x in nz if abs(x) <= 2) / max(1, len(nz)), 1)
    snd = [False] * L
    for s, e, p, v, ln in bn:
        for k in range(int(round(s)), min(L, int(round(s + ln)))): snd[k] = True
    m["sounding_frac"] = round(sum(snd) / L, 2)
    sl = sorted({int(round(t)) for t in att}); run = best = 1
    for a, b in zip(sl, sl[1:]): run = run + 1 if b == a + 1 else 1; best = max(best, run)
    m["max_16th_run"] = best
    S = set(sl); beats = range(0, L, 4)
    m["beats_with_e_and_a_all_pct"] = round(100 * sum(1 for b in beats if {b + 1, b + 2, b + 3} <= S) / len(beats), 1)
    m["beats_with_and_a_pair_pct"] = round(100 * sum(1 for b in beats if {b + 2, b + 3} <= S) / len(beats), 1)
    m["beats_offbeat_only_pct"] = round(100 * sum(1 for b in beats if b + 2 in S and not ({b, b + 1, b + 3} & S)) / len(beats), 1)
    m["dotted_grid_share_pct"] = round(100 * sum(1 for k in sl if k % 16 in (0, 3, 6, 9, 12, 15)) / len(sl), 1)
    m["held_through_kick_pct_of_kicks"] = round(100 * sum(1 for k in kicks if k < L and any(s < k - 0.3 and s + ln > k + 0.5 for s, e, p, v, ln in bn)) / max(1, sum(1 for k in kicks if k < L)), 1)
    vel = [n[3] for n in bn]; m["vel_range"] = [min(vel), max(vel)]; m["vel_layers8"] = len({v // 8 for v in vel})
    m["swing"] = swing(att)
    tok = ["."] * L
    for s, e, p, v, ln in bn:
        a = int(round(s))
        if 0 <= a < L:
            tok[a] = str(p % 12)
            for k in range(a + 1, min(L, int(round(s + ln)))):
                if tok[k] == ".": tok[k] = "-"
    m["rep1"], m["rep2"], m["rep4"] = rep(tok, 16), rep(tok, 32), rep(tok, 64) if bars >= 8 else None
    diff = [i % 16 for i in range(16, L) if tok[i] != tok[i - 16]]
    m["bar_diff_in_last4_pct"] = round(100 * sum(1 for d in diff if d >= 12) / len(diff), 1) if diff else None
    m["bar_diff_back_half_pct"] = round(100 * sum(1 for d in diff if d >= 8) / len(diff), 1) if diff else None
    m["slot_hist_pct_bars"] = [round(100 * sum(1 for b in bar_sets(att, bars) if i in b) / bars) for i in range(16)]
    return m, bar_sets(att, bars), snd, S

def measure_hits(notes, bars, bass_snd=None, bass_att=None, label="stab"):
    by = defaultdict(list)
    for s, e, p, v in notes:
        if s < bars * 16 - 0.5: by[int(round(s))].append((s, e, p, v))
    hits = sorted(by.items())
    if not hits: return {"hits_per_bar": 0}, [set() for _ in range(bars)]
    m = {"hits_per_bar": round(len(hits) / bars, 2)}
    m["on_beat_pct"] = round(100 * sum(1 for k, _ in hits if k % 4 == 0) / len(hits), 1)
    m["on_downbeat_slot1_pct"] = round(100 * sum(1 for k, _ in hits if k % 16 == 0) / len(hits), 1)
    pos = Counter(k % 4 for k, _ in hits)
    m["in_beat_pct"] = {n: round(100 * pos[i] / len(hits), 1) for i, n in enumerate(["on", "e", "and", "a"])}
    m["pcs_per_hit_med"] = st.median(len({p % 12 for _, _, p, _ in ns}) for _, ns in hits)
    m["dur_med16"] = round(st.median(max(e - s for s, e, p, v in ns) for _, ns in hits), 2)
    m["median_pitch"] = name(st.median(p for _, ns in hits for _, _, p, _ in ns))
    bars_with = sum(1 for b in range(bars) if any(b * 16 <= k < b * 16 + 16 for k, _ in hits))
    m["bars_with_hit_pct"] = round(100 * bars_with / bars, 1)
    per = Counter(k // 16 for k, _ in hits); m["hits_per_bar_max"] = max(per.values())
    if bass_snd:
        m["in_bass_rest_pct"] = round(100 * sum(1 for k, _ in hits if k < len(bass_snd) and not bass_snd[k]) / len(hits), 1)
        m["on_bass_attack_pct"] = round(100 * sum(1 for k, _ in hits if k in bass_att) / len(hits), 1)
    m["back_half_pct"] = round(100 * sum(1 for k, _ in hits if k % 16 >= 8) / len(hits), 1)
    m["slot_hist_pct_bars"] = [round(100 * sum(1 for b in range(bars) if (b * 16 + i) in by) / bars) for i in range(16)]
    return m, bar_sets([k for k, _ in hits], bars)

def measure_drums(notes, bars):
    out, sets = {}, {}
    for v, nm in ((KICK, "kick"), (CLAP, "clap"), (CH, "closed_hat"), (OH, "open_hat")):
        ts = [s for s, e, p, vel in notes if p == v and s < bars * 16 - 0.5]
        vels = [vel for s, e, p, vel in notes if p == v]
        if not ts: out[nm] = None; sets[nm] = [set() for _ in range(bars)]; continue
        bs = bar_sets(ts, bars); sets[nm] = bs
        hist = [round(100 * sum(1 for b in bs if i in b) / bars) for i in range(16)]
        out[nm] = {"hits_per_bar": round(len(ts) / bars, 2), "slot_hist_pct_bars": hist, "swing": swing(ts),
                   "odd16_share_pct": round(100 * sum(1 for t in ts if int(round(t)) % 2 == 1) / len(ts), 1),
                   "vel_range": [min(vels), max(vels)], "dur_med16": round(st.median(e - s for s, e, p, vel in notes if p == v), 2)}
    return out, sets

# --------------------------------------------------------------- vocab cell parsing / checks
def cell_bars(g):
    g = g if isinstance(g, list) else [g]
    res = []
    for b in g:
        att = [i for i, c in enumerate(b) if c in "xXoO"]
        hold = [i for i, c in enumerate(b) if c == "="]
        res.append((set(att), set(hold), b))
    return res

def cell_len16(b):
    """lengths implied by the grid: attack + following '=' holds; '-' ends a note"""
    lens = []
    for i, c in enumerate(b):
        if c in "xXoO":
            j = i + 1
            while j < len(b) and b[j] == "=": j += 1
            lens.append(j - i)
    return lens

def jacc(a, b): return len(a & b) / len(a | b) if (a | b) else 1.0

def best_match(cellsets, refsets):
    """mean over cell bars of best Jaccard vs any reference bar; plus share of ref bars >=0.6"""
    best = [max((jacc(c, r) for r in refsets), default=0) for c in cellsets]
    close = sum(1 for r in refsets if max(jacc(c, r) for c in cellsets) >= 0.6)
    return round(st.mean(best), 2), round(100 * close / max(1, len(refsets)), 1)

def main():
    vocab = json.load(open(V))
    R = {"tracks": {}}
    refs = defaultdict(lambda: defaultdict(list))   # refs[group][lane] -> list of (track, barset)
    for tr, grp in GROUP.items():
        for tag, parts, fixed_bars in drops(tr):
            key = f"{tr}:{tag}"; rec = {"group": grp}
            dn = load(parts["drums"]) if "drums" in parts else []
            bnotes = load(parts["bass"])
            end = max([n[0] for n in bnotes] + [n[0] for n in dn])
            bars = fixed_bars or max(4, int(end // 16) + 1)
            rec["bars"] = bars
            kicks = sorted(s for s, e, p, v in dn if p == KICK) or [float(i) for i in range(0, bars * 16, 4)]
            # use grid kicks (4/4 is measured >=94% per quarter, CTR A11) to avoid missed-detection bias
            gk = [float(i) for i in range(0, bars * 16, 4)]
            bm, bsets, snd, batt = measure_bass(bnotes, bars, gk); rec["bass"] = bm
            for b in bsets: refs[grp]["bass"].append((tr, b))
            if "stabs" in parts:
                sm, ssets = measure_hits(load(parts["stabs"]), bars, snd, batt); rec["stabs"] = sm
                for b in ssets: refs[grp]["stab"].append((tr, b))
            if "chops" in parts:
                cm, csets = measure_hits(load(parts["chops"]), bars, snd, batt); rec["chops"] = cm
            if dn:
                dm, dsets = measure_drums(dn, bars); rec["drums"] = dm
                for lane, bs in dsets.items():
                    for b in bs: refs[grp][lane].append((tr, b))
            R["tracks"][key] = rec
            del dn, bnotes
    # ---- cell checks
    lane_of = {"kick": "kick", "clap": "clap", "snare": "clap", "closed_hat": "closed_hat", "open_hat": "open_hat",
               "shaker": "closed_hat", "tamb": "closed_hat", "ride": "closed_hat"}
    C = {}
    for sec in ("BASS", "STAB", "DRUMS", "PLUCK", "VOX", "HITS", "PAD"):
        for c in vocab["cells"][sec]:
            cb = cell_bars(c["grid"]); sets = [a for a, h, b in cb]
            n_att = [len(a) for a in sets]
            info = {"sec": sec, "attacks_per_bar": round(st.mean(n_att), 2),
                    "on_beat_pct": round(100 * sum(1 for a in sets for i in a if i % 4 == 0) / max(1, sum(n_att)), 1),
                    "inference": c.get("inference")}
            lens = [l for a, h, b in cb for l in cell_len16(b)]
            if lens: info["grid_len16_med"] = st.median(lens)
            run = 0; best = 0
            for a in sets:
                r = 0
                for i in range(16):
                    r = r + 1 if i in a else 0; best = max(best, r)
            info["max_16th_run"] = best
            lane = None
            if sec == "BASS": lane = "bass"
            elif sec in ("STAB",): lane = "stab"
            elif sec == "DRUMS": lane = lane_of.get(c.get("voice"))
            if lane and any(sets):
                for grp in ("UK", "GASKIN", "ELLIA"):
                    rs = [b for t, b in refs[grp][lane] if b]
                    if rs:
                        info[f"match_{grp}"] = best_match([s for s in sets if s] or [set()], rs)
                if lane in ("bass", "stab"):
                    # which UK track has the closest bar
                    pt = defaultdict(float)
                    for t, b in refs["UK"][lane]:
                        if b: pt[t] = max(pt[t], max(jacc(s, b) for s in sets if s))
                    if pt: info["closest_UK_track"] = max(pt.items(), key=lambda x: x[1])
            C[c["id"]] = info
    R["cells"] = C
    # ---- summary ranges for UK group
    def rng(path):
        vals = []
        for k, r in R["tracks"].items():
            if r["group"] != "UK": continue
            x = r
            try:
                for p in path: x = x[p]
            except (KeyError, TypeError): continue
            if isinstance(x, (int, float)): vals.append(x)
        return [min(vals), max(vals), round(st.median(vals), 2)] if vals else None
    S = {}
    for p in [("bass", "notes_per_bar"), ("bass", "on_kick_pct"), ("bass", "top2_pct"), ("bass", "len_med16"),
              ("bass", "median_midi"), ("bass", "octave_move_pct"), ("bass", "sounding_frac"), ("bass", "max_16th_run"),
              ("bass", "beats_with_e_and_a_all_pct"), ("bass", "beats_with_and_a_pair_pct"), ("bass", "beats_offbeat_only_pct"),
              ("bass", "dotted_grid_share_pct"), ("bass", "held_through_kick_pct_of_kicks"), ("bass", "long_notes_ge6_pct"),
              ("bass", "rep1"), ("bass", "rep2"), ("bass", "rep4"), ("bass", "bar_diff_in_last4_pct"), ("bass", "bar_diff_back_half_pct"),
              ("bass", "step_moves_pct_of_changes"), ("bass", "vel_layers8"),
              ("stabs", "hits_per_bar"), ("stabs", "on_beat_pct"), ("stabs", "pcs_per_hit_med"), ("stabs", "dur_med16"),
              ("stabs", "in_bass_rest_pct"), ("stabs", "on_bass_attack_pct"), ("stabs", "bars_with_hit_pct"), ("stabs", "hits_per_bar_max"),
              ("chops", "hits_per_bar"), ("chops", "on_beat_pct"), ("chops", "back_half_pct"), ("chops", "bars_with_hit_pct"),
              ("drums", "closed_hat", "swing"), ("drums", "open_hat", "swing"), ("drums", "clap", "swing"),
              ("drums", "closed_hat", "hits_per_bar"), ("drums", "open_hat", "hits_per_bar"), ("drums", "clap", "hits_per_bar"),
              ("drums", "kick", "hits_per_bar"), ("drums", "closed_hat", "odd16_share_pct"), ("drums", "open_hat", "odd16_share_pct")]:
        S[".".join(p)] = rng(p)
    R["uk_ranges_min_max_median"] = S
    out = sys.argv[sys.argv.index("--json") + 1] if "--json" in sys.argv else None
    if out: json.dump(R, open(out, "w"), indent=1)
    print(json.dumps(S))
    return R

if __name__ == "__main__":
    main()
