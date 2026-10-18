#!/usr/bin/env python3
"""
originality_check.py  --  A&R originality gate for Melak's Music team.

Usage:  python3 originality_check.py candidate.mid [--refs DIR] [--json]
        (DIR default /workspace/music-team/transcriptions, scanned recursively for .mid/.midi)

Compares the candidate against every reference MIDI and flags borrowed material.
Everything is TRANSPOSITION-INVARIANT and works on a 16th-note grid in 4/4
(time comes from MIDI ticks/PPQ, so tempo differences do not matter).

Streams compared (every candidate stream x every reference stream):
  * each pitched track/channel (drums on MIDI ch10 ignored) -> lowest note per 16th onset
  * MERGED_LOW: all pitched tracks merged, lowest voice per onset (= the bass skeleton)

Metrics per stream pair:
  pc_ngram    pitch-class 4-gram containment, best of 12 transpositions
  pc_lcs      longest-common-subsequence ratio of pitch-class sequences (best transposition) [info only:
              root-heavy basslines share long subsequences by nature, so it is never thresholded]
  rhythm      16th onset grid, 1/2/4-bar windows, best Jaccard vs any ref window (cyclic, bar offsets),
              chance-corrected (J-E)/(1-E) for the two densities (added 2026-10-16 22:xx for dense CQT refs);
              'rhythm_cov' = mean best-match over the candidate's 2/4-bar windows
  int_ngram   interval n-gram containment, n=3..5 (max over n)
  int_lcsub   longest common substring of the interval sequences (counted in intervals)
  skel_win    SKELETON: (16th slot, relative pitch class) token sets per 2/4-bar window, best Jaccard over
              12 transpositions x ref bar offsets x candidate shift of -2..+2 16ths
  skel_ngram  SKELETON: (interval, inter-onset gap) 3-gram containment: the pitch+rhythm motif,
              independent of position and key; gaps may differ by 1 16th (catches 'nudged' copies)

Triviality guards (a one-note root pulse or plain 8ths never trips the gate):
  * a stream needs >= 6 onsets; pitch metrics need >= 2 distinct pitch classes
  * pitch/interval n-grams only count if they contain >= 2 non-zero moves
  * generic rhythm windows (all 16ths, all 8ths, 4-on-floor, offbeat 8ths) and windows with < 3 onsets are skipped
  * skeleton windows need >= 4 tokens and >= 2 pitch classes, and are skipped when every bar is a generic
    rhythm or a one-pitch-class pulse (pedal strings / 8th pulses)
  * n-gram containment is divided by max(#candidate n-grams, 5): a 2-note alternation can't score high by chance

  voicing     CHORD hits (stabs/swells, works on sparse parts with >= 3 hits): share of candidate chord hits
              within 1 16th of a ref chord hit with the same pitch-class set (Jaccard >= 0.75), best of
              12 transpositions x whole-bar offsets. One-to-one matching (fixed 2026-10-16 22:xx: before, two
              cand hits 2 16ths apart could both count against the same ref hit)

Verdict per reference = worst stream pair (FAIL > FLAG > PASS).
Exit code: 0 PASS, 1 FLAG (human listen needed), 2 FAIL.
"""
import argparse, json, os, sys
from collections import defaultdict
try:
    import mido
except ImportError:
    sys.exit("needs mido:  pip install mido")

DEFAULT_REFS = "/workspace/music-team/transcriptions"
MIN_EVENTS = 6
MIN_NGRAMS = 5         # containment denominators never go below this (distinctiveness weight)
MAX_BARS = 64          # longer files are truncated (loops/sketches are 4-32 bars)

# ---- thresholds: (FLAG, FAIL). Calibrated 2026-10-16, see originality_check_tests.md ----
TH = {
    "skel_win":   (0.40, 0.55),
    "skel_ngram": (0.30, 0.45),
    "int_ngram":  (0.50, None),
    "int_lcsub":  (8,    None),
    "pc_ngram":   (0.60, None),
    "rhythm_cov": (0.75, None),   # rhythm alone never FAILs: house rhythms overlap by design
    "voicing":    (0.50, 0.75),   # chord hits: same voicing at same position (needs >= 3 matched hits)
}
COMBO_FAIL = {"int_ngram": 0.60, "rhythm_cov": 0.70}   # melody AND rhythm both borrowed -> FAIL

GENERIC_BAR_RHYTHMS = {frozenset(range(16)), frozenset(range(0, 16, 2)), frozenset(range(1, 16, 2)),
                       frozenset(range(0, 16, 4)), frozenset(range(2, 16, 4)), frozenset()}

# ------------------------------------------------------------------ parsing
def load_streams(path):
    mid = mido.MidiFile(path)
    ppq = mid.ticks_per_beat
    tracks, names = defaultdict(list), {}
    for ti, tr in enumerate(mid.tracks):
        t, on, tname = 0, {}, None
        for msg in tr:
            t += msg.time
            if msg.type == "track_name":
                tname = msg.name
            if msg.type not in ("note_on", "note_off") or msg.channel == 9:
                continue
            k = (msg.channel, msg.note)
            if msg.type == "note_on" and msg.velocity > 0:
                on[k] = (t, msg.velocity)
            elif k in on:
                s, v = on.pop(k)
                tracks[(ti, msg.channel)].append((s / ppq, t / ppq, msg.note, v))
        for key in tracks:
            if key[0] == ti:
                names[key] = (tname or f"track{ti}") + (f"/ch{key[1]+1}" if key[1] else "")
    streams, allnotes = {}, []
    for key, notes in tracks.items():
        if notes:
            streams[names[key]] = notes
            allnotes += notes
    if len(streams) > 1:
        streams["MERGED_LOW"] = allnotes
    return {n: (to_events(v), to_chords(v)) for n, v in streams.items()}

def to_chords(notes):
    """onsets with >= 2 simultaneous notes -> [(slot, pitch-class set)]"""
    by = {}
    for s, e, p, v in notes:
        slot = int(round(s * 4))
        if 0 <= slot < MAX_BARS * 16:
            by.setdefault(slot, set()).add(p % 12)
    return [(s, frozenset(pcs)) for s, pcs in sorted(by.items()) if len(pcs) >= 2]

def to_events(notes):
    """lowest note per 16th onset slot -> sorted [(slot, pitch)]"""
    by = {}
    for s, e, p, v in notes:
        slot = int(round(s * 4))
        if 0 <= slot < MAX_BARS * 16:
            by[slot] = min(p, by.get(slot, 999))
    return [(s, by[s]) for s in sorted(by)]

# ------------------------------------------------------------------ helpers
def intervals(ev): return [ev[i+1][1] - ev[i][1] for i in range(len(ev) - 1)]
def gaps(ev):      return [ev[i+1][0] - ev[i][0] for i in range(len(ev) - 1)]
def nontrivial(ints): return sum(1 for x in ints if x != 0) >= 2
def pc_ok(g): return sum(1 for a, b in zip(g, g[1:]) if a != b) >= 2
def nbars(ev): return (ev[-1][0] // 16) + 1 if ev else 1
def jac(a, b): return len(a & b) / len(a | b) if (a or b) else 0.0

def ngram_containment(cand, ref, n, ok=lambda g: True):
    cg = {tuple(cand[i:i+n]) for i in range(len(cand) - n + 1)}
    cg = {g for g in cg if ok(g)}
    if not cg:
        return 0.0, []
    rg = {tuple(ref[i:i+n]) for i in range(len(ref) - n + 1)}
    hit = cg & rg
    # distinctiveness weight: a candidate with only a handful of qualifying n-grams (e.g. a 2-note
    # alternation) can't reach a high score by chance -- divide by at least MIN_NGRAMS
    return len(hit) / max(len(cg), MIN_NGRAMS), sorted(hit)

def lcs_len(a, b):
    prev = [0] * (len(b) + 1)
    for x in a:
        cur = [0]
        for j, y in enumerate(b):
            cur.append(prev[j] + 1 if x == y else max(prev[j+1], cur[j]))
        prev = cur
    return prev[-1]

def lcsubstr(a, b, ok):
    """longest common substring that passes ok(); DP, then validate best candidates."""
    best, where = 0, None
    prev = [0] * (len(b) + 1)
    for i in range(1, len(a) + 1):
        cur = [0] * (len(b) + 1)
        for j in range(1, len(b) + 1):
            if a[i-1] == b[j-1]:
                cur[j] = prev[j-1] + 1
                k = cur[j]
                if k > best and ok(a[i-k:i]):
                    best, where = k, (i - k, j - k)
        prev = cur
    return best, where

def bar_sets(ev, nb, shift=0, t=None):
    bars = [set() for _ in range(nb)]
    for slot, p in ev:
        s = slot + shift
        bars[(s // 16) % nb].add((s % 16, (p + t) % 12) if t is not None else s % 16)
    return bars

def window(bars, start, w):
    nb, out = len(bars), set()
    for k in range(w):
        out |= {(k, x) for x in bars[(start + k) % nb]}
    return out

# ------------------------------------------------------------------ metrics
def rhythm_metric(c, r):
    cb, rb = bar_sets(c, nbars(c)), bar_sets(r, nbars(r))
    res = {}
    for w in (1, 2, 4):
        if w > len(cb) or w > len(rb):
            continue
        rws = [window(rb, j, w) for j in range(len(rb))]
        bests = []
        for i in range(len(cb) - w + 1):
            if all(frozenset(cb[i+k]) in GENERIC_BAR_RHYTHMS for k in range(w)):
                continue
            cw = window(cb, i, w)
            if len(cw) < 3:
                continue
            bj, bo = max((jac(cw, rw), j) for j, rw in enumerate(rws))
            # chance correction: two dense grids overlap a lot by accident. E[J] for random grids with the
            # same densities p,q is pq/(p+q-pq); report (J - E)/(1 - E)
            p, q = len(cw) / (16 * w), len(rws[bo]) / (16 * w)
            ej = p * q / (p + q - p * q) if (p + q - p * q) > 0 else 0
            bj = max(0.0, (bj - ej) / (1 - ej)) if ej < 1 else 0.0
            bests.append((bj, i, bo))
        if bests:
            m = max(bests)
            res[w] = {"max": round(m[0], 3), "coverage": round(sum(b[0] for b in bests) / len(bests), 3),
                      "cand_bars": f"{m[1]+1}-{m[1]+w}", "ref_bars": f"{m[2]+1}-{m[2]+w}"}
    return res

def skeleton_window(c, r):
    if len({p % 12 for _, p in c}) < 2:
        return {}
    rnb, cnb = nbars(r), nbars(c)
    rbars = {t: bar_sets(r, rnb, t=t) for t in range(12)}
    cbars = {sh: bar_sets(c, cnb, shift=sh, t=0) for sh in (-2, -1, 0, 1, 2)}
    res = {}
    for w in (1, 2, 4):
        if w > cnb or w > rnb:
            continue
        rws = {t: [window(rbars[t], j, w) for j in range(rnb)] for t in range(12)}
        best, cover = (0, None), []
        for i in range(cnb - w + 1):
            bi = (0, None)
            # guard: a pedal/pulse window (every bar a generic rhythm, or one pitch class per bar) is not a skeleton
            if all(frozenset(cbars[0][i+k] and {x[0] for x in cbars[0][i+k]}) in GENERIC_BAR_RHYTHMS or
                   len({x[1] for x in cbars[0][i+k]}) <= 1 for k in range(w)):
                continue
            for sh, cb in cbars.items():
                cw = window(cb, i, w)
                if len(cw) < 4 or len({x[1][1] for x in cw}) < 2:
                    continue
                for t in range(12):
                    for j, rw in enumerate(rws[t]):
                        jj = jac(cw, rw)
                        if jj > bi[0]:
                            bi = (jj, (i, j, t, sh))
            if bi[1] is not None:
                cover.append(bi[0])
                if bi[0] > best[0]:
                    best = bi
        if best[1]:
            i, j, t, sh = best[1]
            res[w] = {"max": round(best[0], 3), "coverage": round(sum(cover) / len(cover), 3),
                      "cand_bars": f"{i+1}-{i+w}", "ref_bars": f"{j+1}-{j+w}",
                      "semitones_cand_to_ref": t if t <= 6 else t - 12, "cand_shift_16ths": sh}
    return res

def skel_ngram(c, r, n=3, tol=1):
    """(interval, gap) n-gram containment. Intervals must match exactly; each inter-onset gap may differ
    by <= tol 16ths (catches 'nudged by a 16th' copies). Only n-grams with >= 2 non-zero moves count."""
    ci, cg, ri, rg = intervals(c), gaps(c), intervals(r), gaps(r)
    cgr = {(tuple(ci[i:i+n]), tuple(cg[i:i+n])) for i in range(len(ci) - n + 1)}
    cgr = {g for g in cgr if nontrivial(g[0])}
    if not cgr:
        return 0.0, []
    rgr = defaultdict(list)
    for i in range(len(ri) - n + 1):
        rgr[tuple(ri[i:i+n])].append(tuple(rg[i:i+n]))
    hits = [g for g in cgr if any(all(abs(a - b) <= tol for a, b in zip(g[1], rgap)) for rgap in rgr.get(g[0], []))]
    return len(hits) / max(len(cgr), MIN_NGRAMS), [{"intervals": list(h[0]), "gaps16": list(h[1])} for h in sorted(hits)]

def voicing_match(cc, rc, rlen_bars):
    """chord stabs/swells: fraction of candidate chord hits that land within 1 16th of a reference chord
    hit with the same voicing (pitch-class-set Jaccard >= 0.75), best over 12 transpositions and
    whole-bar offsets. Works for sparse parts (3+ chord hits) where the other metrics can't."""
    if len(cc) < 3 or len(rc) < 3:
        return None
    L = rlen_bars * 16
    best = (0.0, 0, 0, 0)
    for t in range(12):
        ct = [(s, frozenset((p + t) % 12 for p in pcs)) for s, pcs in cc]
        for o in range(rlen_bars):
            m, used = 0, set()   # one-to-one: each ref hit can vouch for only one cand hit
            for s, pcs in ct:
                s2 = (s + 16 * o) % L
                for k, (rs, rp) in enumerate(rc):
                    if k not in used and min(abs(s2 - rs), L - abs(s2 - rs)) <= 1 and jac(pcs, rp) >= 0.75:
                        used.add(k); m += 1; break
            if m > best[1] or (m == best[1] and m / len(cc) > best[0]):
                best = (m / len(cc), m, t, o)
    return {"score": round(best[0], 3), "matched": best[1], "of": len(cc),
            "semitones_cand_to_ref": best[2] if best[2] <= 6 else best[2] - 12, "bar_offset": best[3]}

def compare(cs, rs):
    (c, cch), (r, rch) = cs, rs
    vm = voicing_match(cch, rch, max(nbars(r), (rch[-1][0] // 16 + 1) if rch else 1))
    if len(c) < MIN_EVENTS or len(r) < MIN_EVENTS:
        if vm and vm["matched"] >= 3:
            return {"voicing": vm["score"], "voicing_detail": vm, "note": f"sparse part ({len(c)}/{len(r)} onsets): voicing check only"}
        return {"skipped": f"too few onsets (cand {len(c)}, ref {len(r)})"}
    out = {}
    if vm:
        out["voicing"] = vm["score"] if vm["matched"] >= 3 else 0.0
        out["voicing_detail"] = vm
    ci, ri = intervals(c), intervals(r)
    cpc = [p % 12 for _, p in c]
    # (a) pitch-class sequence, best of 12 transpositions
    if len(set(cpc)) >= 2:
        best_ng, best_lcs = 0.0, 0.0
        for t in range(12):
            rpc = [(p + t) % 12 for _, p in r]
            best_ng = max(best_ng, ngram_containment(cpc, rpc, 4, ok=pc_ok)[0])
            best_lcs = max(best_lcs, lcs_len(cpc, rpc) / min(len(cpc), len(rpc)))
        out["pc_ngram"], out["pc_lcs"] = round(best_ng, 3), round(best_lcs, 3)
    else:
        out["pc_ngram"], out["pc_lcs"] = 0.0, None
        out["note"] = "single pitch class: pitch metrics skipped"
    # (b) rhythm
    rh = rhythm_metric(c, r)
    out["rhythm"] = rh
    out["rhythm_cov"] = max([v["coverage"] for k, v in rh.items() if k in (2, 4)] or [0])
    # (c) intervals
    best, ex = 0.0, None
    for n in (3, 4, 5):
        v, hits = ngram_containment(ci, ri, n, ok=nontrivial)
        if v > best:
            best, ex = v, {"n": n, "shared": [list(h) for h in hits[:6]]}
    out["int_ngram"] = round(best, 3)
    if ex: out["int_ngram_examples"] = ex
    L, where = lcsubstr(ci, ri, nontrivial)
    out["int_lcsub"] = L
    if where:
        out["int_lcsub_at"] = {"cand_bar": c[where[0]][0] // 16 + 1, "ref_bar": r[where[1]][0] // 16 + 1,
                               "intervals": ci[where[0]:where[0] + L]}
    # skeleton (pitch + rhythm together)
    sw = skeleton_window(c, r)
    out["skeleton"] = sw
    out["skel_win"] = max([v["max"] for k, v in sw.items() if k in (2, 4)] or [0])
    v, hits = skel_ngram(c, r, 3)
    out["skel_ngram"] = round(v, 3)
    if hits: out["skel_ngram_examples"] = hits[:4]
    return out

def verdict(m):
    if "skipped" in m:
        return "PASS", []
    level, reasons = "PASS", []
    for k, (fl, fa) in TH.items():
        v = m.get(k)
        if v is None:
            continue
        if fa is not None and v >= fa:
            level = "FAIL"; reasons.append(f"{k}={v} >= FAIL {fa}")
        elif v >= fl:
            level = "FAIL" if level == "FAIL" else "FLAG"; reasons.append(f"{k}={v} >= FLAG {fl}")
    if "skel_win" not in m:   # sparse voicing-only comparison
        return level, reasons
    if all((m.get(k) or 0) >= th for k, th in COMBO_FAIL.items()):
        level = "FAIL"; reasons.append("combo FAIL: " + " AND ".join(f"{k}>={v}" for k, v in COMBO_FAIL.items()))
    return level, reasons

RANK = {"PASS": 0, "FLAG": 1, "FAIL": 2}

def run(cand, refs_dir):
    cs = load_streams(cand)
    refs = sorted(os.path.join(root, f) for root, _, files in os.walk(refs_dir)
                  for f in files if f.lower().endswith((".mid", ".midi")))
    rep = {"candidate": cand, "refs_dir": refs_dir, "thresholds": TH, "combo_fail": COMBO_FAIL,
           "references": [], "verdict": "PASS"}
    if not refs:
        rep["warning"] = "no reference MIDI found"
    for rp in refs:
        if os.path.abspath(rp) == os.path.abspath(cand):
            continue
        rs = load_streams(rp)
        pairs = []
        for cn, ce in cs.items():
            for rn, re_ in rs.items():
                m = compare(ce, re_)
                lv, why = verdict(m)
                pairs.append({"cand_stream": cn, "ref_stream": rn, "verdict": lv, "reasons": why, "metrics": m})
        worst = max([p["verdict"] for p in pairs] or ["PASS"], key=RANK.get)
        rep["verdict"] = max(rep["verdict"], worst, key=RANK.get)
        rep["references"].append({"reference": rp, "verdict": worst, "pairs": pairs})
    return rep

def fmt(rep):
    L = [f"ORIGINALITY CHECK  candidate: {rep['candidate']}",
         "refs: " + rep["refs_dir"],
         "thresholds (FLAG, FAIL): " + ", ".join(f"{k}={v}" for k, v in TH.items()) + f" | combo FAIL {COMBO_FAIL}", ""]
    if rep.get("warning"): L.append("WARNING: " + rep["warning"])
    for ref in rep["references"]:
        L.append(f"== [{ref['verdict']}] {ref['reference']}")
        for p in sorted(ref["pairs"], key=lambda p: (-RANK[p["verdict"]], -(p["metrics"].get("skel_win") or p["metrics"].get("voicing") or 0))):
            m = p["metrics"]
            if "skipped" in m:
                L.append(f"   {p['cand_stream']} vs {p['ref_stream']}: skipped ({m['skipped']})"); continue
            L.append(f"   [{p['verdict']}] {p['cand_stream']}  vs  {p['ref_stream']}")
            if m.get("voicing_detail"):
                v = m["voicing_detail"]
                L.append(f"      voicing={m['voicing']}  ({v['matched']}/{v['of']} chord hits match a ref hit, cand {v['semitones_cand_to_ref']:+d} st, bar offset {v['bar_offset']})")
            if "skel_win" not in m:
                if p["reasons"]: L.append("      -> " + "; ".join(p["reasons"]))
                continue
            L.append(f"      skel_win={m['skel_win']} skel_ngram={m['skel_ngram']} int_ngram={m['int_ngram']} "
                     f"int_lcsub={m['int_lcsub']} pc_ngram={m['pc_ngram']} pc_lcs={m['pc_lcs']} rhythm_cov={m['rhythm_cov']}")
            for w, v in sorted(m["skeleton"].items()):
                L.append(f"      skeleton {w}-bar: max {v['max']} cov {v['coverage']} | cand bars {v['cand_bars']} ~ ref bars "
                         f"{v['ref_bars']} (cand {v['semitones_cand_to_ref']:+d} st -> ref key, shift {v['cand_shift_16ths']} 16ths)")
            for w, v in sorted(m["rhythm"].items()):
                L.append(f"      rhythm   {w}-bar: max {v['max']} cov {v['coverage']} | cand bars {v['cand_bars']} ~ ref bars {v['ref_bars']}")
            if m.get("int_lcsub_at"):
                a = m["int_lcsub_at"]; L.append(f"      longest shared interval run {a['intervals']} at cand bar {a['cand_bar']} / ref bar {a['ref_bar']}")
            if p["reasons"]: L.append("      -> " + "; ".join(p["reasons"]))
        L.append("")
    L.append(f"OVERALL: {rep['verdict']}")
    return "\n".join(L)

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("candidate")
    ap.add_argument("--refs", default=DEFAULT_REFS)
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args()
    rep = run(a.candidate, a.refs)
    print(json.dumps(rep, indent=1, default=str) if a.json else fmt(rep))
    sys.exit(RANK[rep["verdict"]])

if __name__ == "__main__":
    main()
