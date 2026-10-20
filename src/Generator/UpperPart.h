#pragma once

#include "Generator.h"
#include "Pattern.h"
#include "RuleComposer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// UPPER PART over the bass riff (CHORDS knob). Two rejected rounds: round 8 (rootless chords on every
// bass note, played 15-28 cents off and octave-folded note by note) "dont sound like music"; round 9
// (a tonic hook and DEEP stabs with their own drawn rhythm, written without reading the bass) "the
// synths or stabs are supposed to add to the bassline. either groove, melodic vibe, or both ... these
// don't feel like they're melodically matched" (Oct 21).
//
// So the part is now written FROM the riff: every onset and every pitch is read off the bass note
// under it. Measured (learn only, pooled; nothing of any one source is stored) on 20 bass + upper
// MIDI pairs of one construction-kit pack in his library and the verified stem splits of five drops
// he loves, against round 9 (2.3x-3.7x less coupled to the bass than the pack):
//   LOCK (CHORDS 1, "both"): chords on the bass's own attacks, 3-5 a bar, the bass the chord's root;
//     a chord changes only where the bass changes pitch (pack 3.7x chance); the top voice a unison /
//     4th / 5th over the bass about half the time (1.65x a key-matched null), a 3rd / 6th about a
//     quarter, a 2nd rarely; it holds or moves 1-2 semitones against the bass, never in parallel; the
//     top 22-33 semitones over the bass; hits 1-3 16ths, cut where the bass changes.
//   ANSWER (CHORDS 2, "groove"): single notes in the bass's gaps (60%+ in rests, 30% or fewer with a
//     bass attack), 3-5 a bar, entering a 16th or two after a bass note, on an empty downbeat or on
//     the 16th before the bar; each pitch taken from the last bass note (octave 30-45%, 5th / 4th
//     15-25%, a 3rd / 6th 15%+, a 2nd / b7 15% or less); the first note after the bass moves sits on
//     the new bass note; 10-24 semitones over the bass, about 2 16ths, cut before the next bass attack.
// Both repeat exactly where the bass repeats (a bar the bass plays the same way gets the same notes).
// The notes carry NoteLine: one voice stays one voice, and the engine folds the lane as one block.

namespace rolla
{
namespace upper
{
inline int pcOf (int v) noexcept
{
    const int p = v % 12;
    return p < 0 ? p + 12 : p;
}

inline constexpr int kGhostVel = 60; // riff ghosts are written at 48-56

// One bass note as the upper part reads it.
struct BassNote
{
    float start = 0.f;
    float len = 1.f;
    int v = 0;    // semitones over the key root (the bass lane's written value)
    int vel = 100;
    bool ghost = false;
};

struct Bass
{
    BassNote n[kMaxNotes];
    int count = 0;
    int steps = 0;
    int wrap = -1; // the main note ringing into bar 1: the last one of the riff's first cycle
};

inline Bass bassOf (const Lane& lane, int bars) noexcept
{
    Bass b;
    b.steps = std::clamp (bars, 1, kMaxBars) * 16;
    for (int i = 0; i < lane.count && b.count < kMaxNotes; ++i)
    {
        const Note& x = lane.notes[i];
        if (x.vel <= 2)
            continue;
        BassNote& o = b.n[b.count++];
        o.start = x.start;
        o.len = x.length;
        o.v = x.chord[0];
        o.vel = x.vel;
        o.ghost = x.vel < kGhostVel;
    }
    std::sort (b.n, b.n + b.count, [] (const BassNote& p, const BassNote& q) { return p.start < q.start; });
    // the riff's period in bars (bar 8 may vary): bar 1 comes round after the cycle's last bar, so that
    // bar's last note rings into it (not bar 8's, which would make bar 1 differ from bar 5)
    const int nb = b.steps / 16;
    auto same = [&] (int x, int y) {
        int i = 0, j = 0;
        auto next = [&] (int& k, int bar) { while (k < b.count && (b.n[k].start < bar * 16.f - 0.01f || b.n[k].start >= bar * 16.f + 15.99f)) ++k; };
        for (;;)
        {
            next (i, x);
            next (j, y);
            if (i >= b.count || j >= b.count)
                return i >= b.count && j >= b.count;
            if (std::fabs ((b.n[i].start - x * 16.f) - (b.n[j].start - y * 16.f)) > 0.01f || b.n[i].v != b.n[j].v)
                return false;
            ++i;
            ++j;
        }
    };
    int period = nb;
    for (int p : { 1, 2, 4 })
    {
        bool ok = p < nb;
        for (int x = 0; x + p < nb - 1 && ok; ++x)
            ok = same (x, x + p);
        if (ok)
        {
            period = p;
            break;
        }
    }
    for (int i = 0; i < b.count; ++i)
        if (! b.n[i].ghost && b.n[i].start < period * 16.f - 0.01f)
            b.wrap = i;
    return b;
}

// The main bass note sounding (or last struck) at t: the context bass. Before the first one, the
// loop's last main note (it rings into bar 1 when the loop comes round). -1 for a bass of ghosts.
inline int ctxAt (const Bass& b, float t) noexcept
{
    int k = -1;
    for (int i = 0; i < b.count; ++i)
        if (! b.n[i].ghost && b.n[i].start <= t + 0.26f)
            k = i;
    return k >= 0 ? k : b.wrap;
}

inline bool sounding (const Bass& b, int step) noexcept
{
    const float t = (float) step + 0.01f;
    for (int i = 0; i < b.count; ++i)
        if (b.n[i].start <= t && t < b.n[i].start + b.n[i].len)
            return true;
    return false;
}

inline bool attackAt (const Bass& b, int step) noexcept
{
    for (int i = 0; i < b.count; ++i)
        if (! b.n[i].ghost && std::fabs (b.n[i].start - (float) step) < 0.3f)
            return true;
    return false;
}

// Where the next main bass note with a different pitch class than `v` starts after t (or `end`).
inline float nextChange (const Bass& b, float t, int v, float end) noexcept
{
    for (int i = 0; i < b.count; ++i)
        if (! b.n[i].ghost && b.n[i].start > t + 0.26f && pcOf (b.n[i].v) != pcOf (v))
            return std::min (b.n[i].start, end);
    return end;
}

inline float nextAttack (const Bass& b, float t, float end) noexcept
{
    for (int i = 0; i < b.count; ++i)
        if (b.n[i].start > t + 0.26f)
            return std::min (b.n[i].start, end);
    return end;
}

// A bar's bass as a signature: the same signature gets the same upper notes, so the part repeats
// exactly where the riff does (bar 8 follows the riff's own bar-8 change).
inline std::uint64_t barSig (const Bass& b, int bar) noexcept
{
    std::uint64_t h = 1469598103934665603ull;
    // the note ringing into the bar is part of it: notes before the bar's first attack read it
    const int into = ctxAt (b, (float) (bar * 16) - 0.3f);
    h = (h ^ (std::uint64_t) (into >= 0 ? b.n[into].v + 200 : 0)) * 1099511628211ull;
    for (int i = 0; i < b.count; ++i)
    {
        const BassNote& x = b.n[i];
        if (x.start < (float) (bar * 16) - 0.01f || x.start >= (float) (bar * 16 + 16) - 0.01f)
            continue;
        const int q = (int) std::lround ((x.start - (float) (bar * 16)) * 4.f);
        const std::uint64_t w = (std::uint64_t) (q * 131 + (x.v + 64) * 7 + (x.ghost ? 1 : 0) + (int) std::lround (x.len * 4.f) * 9973);
        h = (h ^ w) * 1099511628211ull;
    }
    return h;
}

// The stab plays from MIDI 60 + key; the riff's root sounds at MIDI 29-43 (BASSMATCH): this is how
// far over the bass's written value a stab value sounds, in semitones.
inline int stabOverBass (int key) noexcept
{
    const int k = pcOf (key);
    const int rootSounds = 24 + k + (24 + k < 29 ? 12 : 0);
    return (60 + k) - rootSounds;
}

inline bool chordableRoot (int pc, int mode) noexcept
{
    int deg = -1;
    for (int d = 0; d < 7 && deg < 0; ++d)
        if (phrase::degreeToSemis (d, mode) == pc)
            deg = d;
    if (deg < 0)
        return false;
    return phrase::degreeToSemis (deg + 4, mode) - phrase::degreeToSemis (deg, mode) == 7; // a perfect 5th
}

// ------------------------------------------------------------------ LOCK
// The chord on bass pitch class `root` (semitones over the key): tones over the root, the root left to
// the bass. Minor: m7 / m9 / m11. Major: 6/9 or add9 (a maj7 sits a semitone under the bass root and
// the upper lanes give way to the bass there); a major triad with a minor 7th in the mode: 9sus or add9.
struct Chord
{
    int root = 0;
    int n = 0;
    int iv[4] {};
};

inline Chord chordOn (int root, int mode, Rng& r, int force = -1) noexcept
{
    int deg = 0;
    for (int d = 0; d < 7; ++d)
        if (phrase::degreeToSemis (d, mode) == root)
            deg = d;
    auto above = [&] (int k) { return phrase::degreeToSemis (deg + k, mode) - phrase::degreeToSemis (deg, mode); };
    const int third = above (2), sixth = above (5), seventh = above (6), ninth = above (8);
    Chord c;
    c.root = root;
    auto set = [&] (std::initializer_list<int> t) { c.n = 0; for (int x : t) c.iv[c.n++] = x; };
    // force: 0 the plain chord (m7 / add9), 1 the other colour (m11 / 6/9 or 9sus)
    const double x = force == 0 ? 0.99 : (force == 1 ? 0.5 : r.next());
    if (third == 3)
    {
        if (ninth == 14 && x < 0.45)
            set ({ 3, 7, 10, 14 }); // m9
        else if (ninth == 14 && x < 0.70)
            set ({ 3, 10, 14, 17 }); // m11
        else
            set ({ 3, 7, 10 }); // m7
    }
    else if (seventh == 10)
    {
        if (x < 0.5)
            set ({ 5, 7, 10, 14 }); // 9sus
        else
            set ({ 4, 7, 14 }); // add9
    }
    else if (sixth == 9 && x < 0.5)
        set ({ 4, 9, 14 }); // 6/9
    else
        set ({ 4, 7, 14 }); // add9
    return c;
}

struct Voicing
{
    int n = 0;
    int p[4] {}; // ascending, stab values
};

// Close and drop-2 voicings with the top in [lo, hi]: no two tones a minor 2nd / 9th apart, the whole
// chord inside a 14-semitone hand.
inline int voicingsOf (const Chord& c, int lo, int hi, Voicing* out, int cap) noexcept
{
    int nv = 0;
    for (int ti = 0; ti < c.n; ++ti)
        for (int top = hi; top >= lo; --top)
        {
            if (pcOf (top) != pcOf (c.root + c.iv[ti]))
                continue;
            for (int drop = 0; drop < 2 && nv < cap; ++drop)
            {
                int p[4];
                int m = 0;
                p[m++] = top;
                for (int k = 0; k < c.n; ++k)
                {
                    if (k == ti)
                        continue;
                    int x = top - pcOf (top - (c.root + c.iv[k]));
                    if (x == top)
                        x -= 12;
                    p[m++] = x;
                }
                std::sort (p, p + m);
                if (drop == 1)
                {
                    if (m < 4)
                        continue;
                    p[m - 2] -= 12;
                    std::sort (p, p + m);
                }
                bool ok = p[m - 1] - p[0] <= 14;
                for (int i = 0; i < m && ok; ++i)
                    for (int j = i + 1; j < m && ok; ++j)
                        ok = (p[j] - p[i]) != 1 && (p[j] - p[i]) != 13;
                if (! ok)
                    continue;
                Voicing& v = out[nv++];
                v.n = m;
                for (int i = 0; i < m; ++i)
                    v.p[i] = p[i];
            }
        }
    return nv;
}

// The top voice over the bass, by interval class: a unison / 4th / 5th first, then a 3rd / 6th, a b7,
// a 2nd last; never a b9 / tritone / maj7.
inline float topWeight (int topOverBass) noexcept
{
    switch (pcOf (topOverBass))
    {
        case 0: case 5: case 7: return 1.f;
        case 3: case 4: case 8: case 9: return 0.75f;
        case 10: return 0.3f;
        default: return 0.f; // the 9th on top sounds unresolved; b9 / tritone / maj7 clash
    }
}

inline void writeLock (Lane& lane, const Lane& bassLane, int mode, int bars, int key, std::uint32_t seed) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    const Bass b = bassOf (bassLane, nb);
    const int over = stabOverBass (key);
    Rng r (seed ^ 0x4C4F434Bu);
    lane.count = 0;
    if (b.count == 0)
        return;
    // the chord roots the riff gives (in order of first attack), one chord and one voicing each
    int roots[8];
    int nRoots = 0;
    for (int i = 0; i < b.count && nRoots < 8; ++i)
    {
        const int pc = pcOf (b.n[i].v);
        if (b.n[i].ghost || ! chordableRoot (pc, mode))
            continue;
        bool seen = false;
        for (int k = 0; k < nRoots; ++k)
            seen = seen || roots[k] == pc;
        if (! seen)
            roots[nRoots++] = pc;
    }
    if (nRoots == 0)
        return;
    nRoots = std::min (nRoots, 4);
    auto idxOf = [&] (int pc) { for (int k = 0; k < nRoots; ++k) if (roots[k] == pc) return k; return -1; };
    // the bass value each root is first played at (its register decides the voicing's)
    int rootV[4];
    for (int k = 0; k < nRoots; ++k)
    {
        rootV[k] = 0;
        for (int i = 0; i < b.count; ++i)
            if (! b.n[i].ghost && pcOf (b.n[i].v) == roots[k])
            {
                rootV[k] = b.n[i].v;
                break;
            }
    }
    Chord chord[4];
    constexpr int kCap = 32;
    Voicing voic[4][kCap];
    int nv[4] {};
    for (int k = 0; k < nRoots; ++k)
    {
        // the drawn colour, unless no voicing of it puts a unison / 4th / 5th / 3rd / 6th on top inside
        // the hand (a minor 9 keeps its 9th on top there): then the plain chord, then the other colour
        bool good = false;
        for (int attempt = 0; attempt < 3 && ! good; ++attempt)
        {
            chord[k] = chordOn (roots[k], mode, r, attempt == 0 ? -1 : (attempt == 1 ? 0 : 1));
            chord[k].root = rootV[k]; // tones sit over the bass note as played
            // the top 22-33 semitones over the bass: stab value = bass value + (22..33) - over
            nv[k] = voicingsOf (chord[k], rootV[k] + 22 - over, rootV[k] + 33 - over, voic[k], kCap);
            for (int v = 0; v < nv[k] && ! good; ++v)
                good = topWeight (voic[k][v].p[voic[k][v].n - 1] - rootV[k]) >= 0.75f;
        }
        if (nv[k] == 0)
            return;
    }
    // the root changes in time order, cyclic (with which way the bass moves)
    int chA[64], chB[64], dir[64];
    int nch = 0;
    int prevIdx = -1, prevV = 0, firstIdx = -1, firstV = 0;
    for (int i = 0; i < b.count && nch < 63; ++i)
    {
        if (b.n[i].ghost)
            continue;
        const int k = idxOf (pcOf (b.n[i].v));
        if (k < 0)
            continue;
        if (firstIdx < 0)
        {
            firstIdx = k;
            firstV = b.n[i].v;
        }
        if (prevIdx >= 0 && k != prevIdx)
        {
            chA[nch] = prevIdx;
            chB[nch] = k;
            dir[nch] = b.n[i].v > prevV ? 1 : -1;
            ++nch;
        }
        prevIdx = k;
        prevV = b.n[i].v;
    }
    if (prevIdx >= 0 && firstIdx >= 0 && prevIdx != firstIdx)
    {
        chA[nch] = prevIdx;
        chB[nch] = firstIdx;
        dir[nch] = firstV > prevV ? 1 : -1;
        ++nch;
    }
    // every voicing combination: the top's interval over the bass, small motion, the top holding or
    // stepping 1-2 semitones against the bass (never with it); the cheapest few, one drawn
    constexpr int kKeep = 5;
    float bestCost[kKeep];
    int bestPick[kKeep][4];
    int kept = 0;
    int pick[4] {};
    for (;;)
    {
        float cost = 0.f;
        for (int k = 0; k < nRoots; ++k)
        {
            const Voicing& v = voic[k][pick[k]];
            const float w = topWeight (v.p[v.n - 1] - rootV[k]);
            cost += w <= 0.f ? 100.f : 6.f * (1.f - w);
        }
        for (int c = 0; c < nch; ++c)
        {
            const Voicing& a = voic[chA[c]][pick[chA[c]]];
            const Voicing& z = voic[chB[c]][pick[chB[c]]];
            float move = 0.f;
            for (int i = 0; i < z.n; ++i)
            {
                int best = 99;
                for (int j = 0; j < a.n; ++j)
                    best = std::min (best, std::abs (z.p[i] - a.p[j]));
                move += (float) best;
            }
            const int d = z.p[z.n - 1] - a.p[a.n - 1];
            const float topCost = d == 0 ? 0.f : (std::abs (d) <= 2 ? (d * dir[c] > 0 ? 3.f : 0.5f) : 6.f + (float) std::abs (d));
            cost += (0.25f * move + topCost) / (float) std::max (1, nch);
        }
        if (kept < kKeep || cost < bestCost[kept - 1])
        {
            int at = std::min (kept, kKeep - 1);
            while (at > 0 && bestCost[at - 1] > cost)
            {
                bestCost[at] = bestCost[at - 1];
                std::copy (bestPick[at - 1], bestPick[at - 1] + 4, bestPick[at]);
                --at;
            }
            bestCost[at] = cost;
            std::copy (pick, pick + 4, bestPick[at]);
            kept = std::min (kKeep, kept + 1);
        }
        int k = 0;
        while (k < nRoots && ++pick[k] >= nv[k])
            pick[k++] = 0;
        if (k == nRoots)
            break;
    }
    int chosen = 0;
    {
        float pw[kKeep], total = 0.f;
        for (int i = 0; i < kept; ++i)
            total += (pw[i] = std::exp (-2.f * (bestCost[i] - bestCost[0])));
        float y = (float) r.next() * total;
        for (; chosen < kept - 1; ++chosen)
            if ((y -= pw[chosen]) <= 0.f)
                break;
    }
    const int* use = bestPick[chosen];
    const float hitLen = r.next() < 0.5 ? 1.5f : 2.f;
    const int perBar = 3 + (int) (r.next() * 3.0); // 3-5 hits a bar
    // bar by bar; a bar the bass plays the same way gets the same hits
    std::uint64_t sigs[kMaxBars];
    int firstBarOf[kMaxBars];
    for (int bar = 0; bar < nb; ++bar)
    {
        sigs[bar] = barSig (b, bar);
        firstBarOf[bar] = bar;
        for (int e = 0; e < bar; ++e)
            if (sigs[e] == sigs[bar])
            {
                firstBarOf[bar] = e;
                break;
            }
    }
    for (int bar = 0; bar < nb; ++bar)
    {
        const int src = firstBarOf[bar];
        const float shift = (float) ((bar - src) * 16);
        // the source bar's attacks on chordable roots, scored: syncopated, short, accented first
        struct Cand { int i; float score; };
        Cand cand[16];
        int nc = 0;
        for (int i = 0; i < b.count && nc < 16; ++i)
        {
            const BassNote& x = b.n[i];
            if (x.ghost || x.start < (float) (src * 16) - 0.01f || x.start >= (float) (src * 16 + 16) - 0.01f)
                continue;
            if (idxOf (pcOf (x.v)) < 0 || x.v != rootV[idxOf (pcOf (x.v))])
                continue; // an octave jump of the root keeps the chord already sounding
            const int slot = (int) std::lround (x.start) - src * 16;
            float s = 0.f;
            s += (slot % 4) != 0 ? 2.f : 0.f;
            s += x.len <= 2.01f ? 1.f : 0.f;
            s += x.vel >= 110 ? 1.5f : 0.f;
            s += nc == 0 ? 1.f : 0.f; // the bar's first attack states its chord
            // a fixed per-roll tiebreak, so equal bars stay equal
            s += 0.01f * (float) ((std::uint32_t) (seed >> (slot & 15)) & 7u);
            cand[nc++] = { i, s };
        }
        // where the bass changes pitch the chord changes with it: those attacks always get a chord
        for (int c = 0; c < nc; ++c)
        {
            const int prev = ctxAt (b, b.n[cand[c].i].start - 0.3f);
            if (prev < 0 || pcOf (b.n[prev].v) != pcOf (b.n[cand[c].i].v))
                cand[c].score += 10.f;
        }
        int must = 0;
        for (int c = 0; c < nc; ++c)
            must += cand[c].score >= 10.f ? 1 : 0;
        std::sort (cand, cand + nc, [] (const Cand& p, const Cand& q) { return p.score > q.score; });
        const int take = std::min (nc, std::min (6, std::max (perBar, must)));
        std::sort (cand, cand + take, [] (const Cand& p, const Cand& q) { return p.i < q.i; });
        for (int h = 0; h < take && lane.count < kMaxNotes; ++h)
        {
            const BassNote& x = b.n[cand[h].i];
            const int k = idxOf (pcOf (x.v));
            const Voicing& v = voic[k][use[k]];
            const float at = x.start + shift;
            if (at >= (float) (nb * 16))
                continue;
            const float change = nextChange (b, x.start, x.v, (float) (src * 16 + 16)) + shift;
            Note n;
            n.start = at;
            // the release tail must not ring into a new bass note: end 3/4 of a 16th before it
            n.length = std::max (0.5f, std::min (hitLen, change - at - 0.75f));
            const int slot = (int) std::lround (x.start) - src * 16;
            n.vel = (std::uint8_t) (x.vel >= 110 ? 118 : ((slot % 4) != 0 ? 104 : 92));
            n.flags = NoteLine;
            n.nChord = (std::uint8_t) v.n;
            for (int c = 0; c < 4; ++c)
                n.chord[c] = (std::int8_t) (c < v.n ? v.p[c] : 0);
            lane.notes[lane.count++] = n;
        }
    }
}

// ------------------------------------------------------------------ ANSWER
inline void writeAnswer (Lane& lane, const Lane& bassLane, int mode, int bars, int key, std::uint32_t seed) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    const Bass b = bassOf (bassLane, nb);
    const int over = stabOverBass (key);
    Rng r (seed ^ 0x414E5357u);
    lane.count = 0;
    if (b.count == 0)
        return;
    const int perBar = 3 + (int) (r.next() * 3.0); // 3-5 notes a bar
    std::uint64_t sigs[kMaxBars];
    int firstBarOf[kMaxBars];
    for (int bar = 0; bar < nb; ++bar)
    {
        sigs[bar] = barSig (b, bar);
        firstBarOf[bar] = bar;
        for (int e = 0; e < bar; ++e)
            if (sigs[e] == sigs[bar])
            {
                firstBarOf[bar] = e;
                break;
            }
    }
    // written per distinct bar, then copied: each distinct bar draws from its own seed
    struct Out
    {
        float t = 0.f;
        float len = 1.f;
        int p = 0;
        int vel = 100;
    };
    Out barOut[kMaxBars][8];
    int barN[kMaxBars] {};
    int prevPitch = -100;
    for (int bar = 0; bar < nb; ++bar)
    {
        if (firstBarOf[bar] != bar)
            continue;
        Rng br ((std::uint32_t) (seed ^ (std::uint32_t) (sigs[bar] & 0xffffffffu) ^ (std::uint32_t) (sigs[bar] >> 32)));
        // candidate 16ths: no bass sounding there; scored: a 16th or two after a bass note,
        // an empty downbeat, the pickup into the next bar
        struct Cand { int step; float score; };
        Cand cand[16];
        int nc = 0;
        for (int s = 0; s < 16; ++s)
        {
            const int step = bar * 16 + s;
            if (sounding (b, step) || attackAt (b, step))
                continue;
            float sc = 0.5f;
            if (step >= 1 && (sounding (b, step - 1) || attackAt (b, step - 1)))
                sc += 2.f;
            else if (step >= 2 && (sounding (b, step - 2) || attackAt (b, step - 2)))
                sc += 1.f;
            if ((s % 4) == 0)
                sc += 1.f;
            if (s == 15)
                sc += 1.5f;
            sc += 0.3f * (float) br.next();
            cand[nc++] = { step, sc };
        }
        std::sort (cand, cand + nc, [] (const Cand& p, const Cand& q) { return p.score > q.score; });
        int chosen[8];
        int n = 0;
        for (int i = 0; i < nc && n < perBar; ++i)
        {
            bool ok = true;
            for (int j = 0; j < n && ok; ++j)
                ok = std::abs (chosen[j] - cand[i].step) >= 2;
            if (ok)
                chosen[n++] = cand[i].step;
        }
        std::sort (chosen, chosen + n);
        // the bass and the answers together cover at most 70% of the bar (the pack's loops sit at
        // 49-57%; denser reads as crowded): shorter answers first, then fewer (never under 3)
        int covB = 0;
        for (int st = 0; st < 16; ++st)
            covB += (sounding (b, bar * 16 + st) || attackAt (b, bar * 16 + st)) ? 1 : 0;
        float lens[8];
        for (int i = 0; i < n; ++i)
            lens[i] = std::max (0.5f, std::min (2.f, nextAttack (b, (float) chosen[i], (float) (bar * 16 + 16)) - (float) chosen[i] - 0.25f));
        auto covered = [&] () {
            bool on[16] {};
            for (int i = 0; i < n; ++i)
                for (int k = 0; k < (int) std::ceil (lens[i] - 0.01f); ++k)
                    if (chosen[i] - bar * 16 + k < 16)
                        on[chosen[i] - bar * 16 + k] = true;
            int c = covB;
            for (int st = 0; st < 16; ++st)
                c += on[st] && ! (sounding (b, bar * 16 + st) || attackAt (b, bar * 16 + st)) ? 1 : 0;
            return c;
        };
        if (covered() > 11)
            for (int i = 0; i < n; ++i)
                lens[i] = std::min (lens[i], 1.f);
        while (covered() > 11 && n > 3)
        {
            // drop the answer that is neither the bar's pickup nor right after a bass note
            int drop = n - 1;
            for (int i = n - 1; i >= 0; --i)
                if ((chosen[i] - bar * 16) != 15 && ! sounding (b, chosen[i] - 1) && ! attackAt (b, chosen[i] - 1))
                {
                    drop = i;
                    break;
                }
            for (int i = drop; i + 1 < n; ++i)
            {
                chosen[i] = chosen[i + 1];
                lens[i] = lens[i + 1];
            }
            --n;
        }
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) chosen[i];
            const int ci = ctxAt (b, t);
            const int bv = ci >= 0 ? b.n[ci].v : 0;
            // stab value that sounds `iv` over this bass note
            auto at = [&] (int iv) { return bv + iv - over; };
            // the bass changed pitch since the last answer (or since the bar began): sit on the new note
            const int before = ctxAt (b, i > 0 ? (float) chosen[i - 1] : (float) (bar * 16) - 0.3f);
            const bool fresh = before < 0 || pcOf (b.n[before].v) != pcOf (bv);
            // the interval family first (octave / 5th-4th / 3rd-6th / step), then its note nearest the
            // last answer, diatonic, 10-24 semitones over the bass
            static constexpr float kSettled[4] = { 0.36f, 0.20f, 0.32f, 0.12f };
            static constexpr float kFresh[4] = { 0.6f, 0.15f, 0.25f, 0.f };
            auto famOf = [] (int iv) { const int c = iv % 12; return c == 0 ? 0 : (c == 5 || c == 7 ? 1 : (c == 3 || c == 4 || c == 8 || c == 9 ? 2 : (c == 2 || c == 10 ? 3 : -1))); };
            float fw[4];
            bool has[4] {};
            for (int iv = 10; iv <= 24; ++iv)
                if (phrase::inMode (at (iv), mode) && famOf (iv) >= 0)
                    has[famOf (iv)] = true;
            float tot = 0.f;
            for (int f = 0; f < 4; ++f)
                tot += (fw[f] = has[f] ? (fresh ? kFresh[f] : kSettled[f]) : 0.f);
            int fam = 0;
            if (tot > 0.f)
            {
                float x = (float) br.next() * tot;
                for (; fam < 3; ++fam)
                    if ((x -= fw[fam]) <= 0.f && fw[fam] > 0.f)
                        break;
                while (fw[fam] <= 0.f)
                    fam = (fam + 1) % 4;
            }
            int p = at (12);
            int bestD = 1000;
            for (int iv = 10; iv <= 24; ++iv)
            {
                if (famOf (iv) != fam || ! phrase::inMode (at (iv), mode))
                    continue;
                const int d = prevPitch > -100 ? std::abs (at (iv) - prevPitch) : std::abs (iv - 14);
                if (d < bestD)
                {
                    bestD = d;
                    p = at (iv);
                }
            }
            prevPitch = p;
            Out& o = barOut[bar][barN[bar]++];
            o.t = t - (float) (bar * 16);
            o.len = lens[i];
            o.p = p;
            const int s = chosen[i] - bar * 16;
            o.vel = s == 15 ? 112 : ((s % 4) == 0 ? 96 : 102);
        }
    }
    for (int bar = 0; bar < nb; ++bar)
    {
        const int src = firstBarOf[bar];
        for (int i = 0; i < barN[src] && lane.count < kMaxNotes; ++i)
        {
            const Out& o = barOut[src][i];
            Note n;
            n.start = (float) (bar * 16) + o.t;
            n.length = o.len;
            n.vel = (std::uint8_t) o.vel;
            n.flags = NoteLine;
            n.nChord = 1;
            n.chord[0] = (std::int8_t) std::clamp (o.p, -60, 60);
            lane.notes[lane.count++] = n;
        }
    }
}
} // namespace upper
} // namespace rolla
