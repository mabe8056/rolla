#pragma once

#include "Generator.h"
#include "Pattern.h"
#include "PhraseLock.h"
#include "../Sampler/PitchProbe.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// One chord loop and accents that repeat (catchiness research #5 and #6; Melak, Oct 19: the songs
// are "better, keep going").
//
// Chords: the stab, the pad and the hits each drew their own chords, so the pad matched a stab
// chord only 15-28% of the time and a roll carried 3-5 different chords. The stab's two chords
// are the roll's chord loop: drawn fresh per roll, checked against the bass and by the
// originality floor (DEEP voices them as its jazz colours). The pad and the hits now take the
// stab's chord of their bar: the same pitch-class set, and the same chord the sampler is asked
// for (root and quality, Sampler chordAskOf), voiced in their own register. The caller runs the
// clash rule again afterwards, so a tone a semitone from the bass under a long pad still goes.
//
// Accents: the stab, pluck and hits played every note at one velocity. Each lane now draws an
// accent pattern per roll over its 2-bar cell (the motif): some anchor hits (on a beat or an
// 'and') +12..+18, the other anchors at the base, e / a and ghost hits -10. It is keyed on the
// cell bar and the 16th, so the restated bars 3-7 carry bars 1-2's accents exactly.
//
// Nothing here is stored or taken from a reference: every pitch and slot is the roll's own, and
// every draw comes from the lane's own seed.

namespace rolla
{
namespace chord_loop
{
inline int pcOf (int v) noexcept
{
    const int p = v % 12;
    return p < 0 ? p + 12 : p;
}

inline std::uint32_t pcsOf (const Note& n) noexcept
{
    std::uint32_t m = 0;
    const int nc = std::clamp ((int) n.nChord, 1, 4);
    for (int c = 0; c < nc; ++c)
        m |= 1u << pcOf (n.chord[c]);
    return m;
}

inline int popc (std::uint32_t m) noexcept
{
    return (int) std::bitset<12> (m).count();
}

// Pitch classes a semitone from any in `pcs` (m2 / m9 / M7 against them).
inline std::uint32_t semitonesOf (std::uint32_t pcs) noexcept
{
    std::uint32_t m = 0;
    for (int p = 0; p < 12; ++p)
        if (((pcs >> p) & 1u) != 0)
            m |= (1u << ((p + 1) % 12)) | (1u << ((p + 11) % 12));
    return m;
}

// What the sampler is asked for when these tones play (in the order written): the chord's root
// pitch class and quality. A recorded chord file plays that chord, so two lanes with one pitch-class
// set but a different ask would still sound two chords.
struct Ask
{
    int root = -1;
    ChordQuality quality = ChordQuality::None;

    bool operator== (const Ask& o) const noexcept { return root == o.root && quality == o.quality; }
};

inline Ask askOf (const int* tones, int n) noexcept
{
    Ask a;
    if (n <= 0)
        return a;
    if (n == 1)
    {
        a.root = pcOf (tones[0]);
        return a;
    }
    const ChordAsk c = chordAskOf (tones, n);
    a.root = pcOf (tones[0] + c.rootDelta);
    a.quality = c.quality;
    return a;
}

struct Chord
{
    int tones[4] {};
    int n = 0;
    std::uint32_t pcs = 0;
    Ask ask;
};

inline Chord chordOf (const Note& note) noexcept
{
    Chord c;
    c.n = std::clamp ((int) note.nChord, 1, 4);
    for (int k = 0; k < c.n; ++k)
        c.tones[k] = note.chord[k];
    c.pcs = pcsOf (note);
    c.ask = askOf (c.tones, c.n);
    return c;
}

inline bool anyHit (const Lane& lane) noexcept
{
    for (int i = 0; i < lane.count; ++i)
        if (lane.notes[i].vel > 2)
            return true;
    return false;
}

// Bass pitch classes sounding in [t0, t1).
inline std::uint32_t bassPcsOver (const Lane& bass, float t0, float t1) noexcept
{
    std::uint32_t m = 0;
    for (int i = 0; i < bass.count; ++i)
    {
        const Note& b = bass.notes[i];
        if (b.vel > 2 && b.start < t1 - 1.0e-3f && t0 < b.start + b.length - 1.0e-3f)
            m |= 1u << pcOf (b.chord[0]);
    }
    return m;
}

// The stab's chord of bar `bar` for an upper note sounding over [t0, t1). Of the chords the stab
// strikes in the bar: the one the bass under the note leaves whole (fewest tones a semitone from
// it), then the one sounding longest in the bar, then the first. A bar with no stab hit keeps the
// chord in effect: the last one struck before it, round the loop. False when the stab is silent.
inline bool chordOfBar (const Lane& stab, const Lane& bass, int bar, int bars, float t0, float t1, Chord& out) noexcept
{
    const std::uint32_t bad = semitonesOf (bassPcsOver (bass, t0, t1));
    int best = -1;
    int bestClash = 99;
    float bestLen = -1.f;
    for (int i = 0; i < stab.count; ++i)
    {
        const Note& n = stab.notes[i];
        if (n.vel <= 2 || phrase_lock::barOf (n.start) != bar)
            continue;
        const std::uint32_t p = pcsOf (n);
        float len = 0.f;
        for (int k = 0; k < stab.count; ++k)
        {
            const Note& o = stab.notes[k];
            if (o.vel > 2 && phrase_lock::barOf (o.start) == bar && pcsOf (o) == p)
                len += o.length;
        }
        const int clash = popc (p & bad);
        if (best < 0 || clash < bestClash || (clash == bestClash && len > bestLen + 1.0e-3f))
        {
            best = i;
            bestClash = clash;
            bestLen = len;
        }
    }
    if (best < 0)
    {
        const float at = (float) (bar * kStepsPerBar);
        const float loop = (float) (std::max (1, bars) * kStepsPerBar);
        float bestD = 1.0e9f;
        for (int i = 0; i < stab.count; ++i)
        {
            const Note& n = stab.notes[i];
            if (n.vel <= 2)
                continue;
            float d = at - n.start;
            if (d <= 0.f)
                d += loop;
            if (d < bestD)
            {
                bestD = d;
                best = i;
            }
        }
    }
    if (best < 0)
        return false;
    out = chordOf (stab.notes[best]);
    return true;
}

// The stab's chord for a hit at `t`: the stab hit on the same 16th, else the last one struck
// earlier in the bar, else the bar's chord.
inline bool chordAt (const Lane& stab, const Lane& bass, float t, float len, int bars, Chord& out) noexcept
{
    const int bar = phrase_lock::barOf (t);
    int best = -1;
    for (int i = 0; i < stab.count; ++i)
    {
        const Note& n = stab.notes[i];
        if (n.vel <= 2 || phrase_lock::barOf (n.start) != bar || n.start > t + 0.01f)
            continue;
        if (best < 0 || n.start > stab.notes[best].start + 1.0e-3f
            || (std::fabs (n.start - stab.notes[best].start) <= 1.0e-3f && n.nChord > stab.notes[best].nChord))
            best = i;
    }
    if (best >= 0)
    {
        out = chordOf (stab.notes[best]);
        return true;
    }
    return chordOfBar (stab, bass, bar, bars, t, t + std::max (0.25f, len), out);
}

// Widest voicing (bottom to top, semitones) and the register band an upper lane voices in
// (semitones over the key root at middle C, the stab / pad / hits base).
inline constexpr int kVoiceSpan = 19;
inline constexpr int kVoiceLo = -5;
inline constexpr int kVoiceHi = 24;

// Voices `c`'s pitch-class set in [lo, hi], at least `minTones` tones (the asked root doubled an
// octave away when the set is smaller): no two tones a minor 2nd or minor 9th apart, at most
// kVoiceSpan wide, and asking the sampler for the stab's chord. Of those, the one nearest
// `centre` (the lane's own register), moving least from `prev`, and not the stab's own voicing.
// Writes the tones ascending; returns how many (0 when nothing fits).
inline int voiceChord (const Chord& c, int minTones, float centre, const int* prev, int nPrev, int lo, int hi, int* out) noexcept
{
    int list[4] {};
    int nl = 0;
    for (int p = 0; p < 12 && nl < 4; ++p)
        if (((c.pcs >> p) & 1u) != 0)
            list[nl++] = p;
    if (nl == 0)
        return 0;
    const int distinct = nl;
    const int want = std::min (4, std::max (minTones, distinct));
    while (nl < want && nl - distinct < 1)
        list[nl++] = c.ask.root >= 0 ? c.ask.root : list[0];

    int stabSorted[4] {};
    for (int k = 0; k < c.n; ++k)
        stabSorted[k] = c.tones[k];
    std::sort (stabSorted, stabSorted + c.n);

    float bestCost = 1.0e9f;
    int best[4] {};
    int bestN = 0;
    for (int pass = 0; pass < 2 && bestN == 0; ++pass)
    {
        // Octave choice per entry: each in [lo, hi].
        int opt[4][4] {};
        int nopt[4] {};
        for (int k = 0; k < nl; ++k)
            for (int v = list[k] - 24; v <= list[k] + 36; v += 12)
                if (v >= lo && v <= hi && nopt[k] < 4)
                    opt[k][nopt[k]++] = v;
        int idx[4] {};
        bool done = false;
        for (int k = 0; k < nl; ++k)
            if (nopt[k] == 0)
                done = true;
        while (! done)
        {
            int v[4];
            for (int k = 0; k < nl; ++k)
                v[k] = opt[k][idx[k]];
            std::sort (v, v + nl);
            bool ok = v[nl - 1] - v[0] <= kVoiceSpan;
            for (int a = 0; a < nl && ok; ++a)
                for (int b = a + 1; b < nl && ok; ++b)
                {
                    const int d = v[b] - v[a];
                    ok = d != 0 && d != 1 && d != 13;
                }
            if (ok && pass == 0 && c.n >= 2)
                ok = askOf (v, nl) == c.ask;
            if (ok)
            {
                float mean = 0.f;
                for (int k = 0; k < nl; ++k)
                    mean += (float) v[k] / (float) nl;
                float cost = std::fabs (mean - centre);
                if (nPrev > 0)
                {
                    float motion = 0.f;
                    for (int k = 0; k < nl; ++k)
                    {
                        int near = 99;
                        for (int j = 0; j < nPrev; ++j)
                            near = std::min (near, std::abs (v[k] - prev[j]));
                        motion += (float) near;
                    }
                    cost += 0.35f * motion;
                }
                cost += 0.25f * (float) std::max (0, v[nl - 1] - v[0] - 12);
                bool same = nl == c.n;
                for (int k = 0; k < nl && same; ++k)
                    same = v[k] == stabSorted[k];
                if (same)
                    cost += 2.5f;
                if (cost < bestCost - 1.0e-4f)
                {
                    bestCost = cost;
                    bestN = nl;
                    for (int k = 0; k < nl; ++k)
                        best[k] = v[k];
                }
            }
            int k = 0;
            while (k < nl && ++idx[k] >= nopt[k])
                idx[k++] = 0;
            done = k == nl;
        }
    }
    for (int k = 0; k < bestN; ++k)
        out[k] = best[k];
    return bestN;
}

inline float meanTone (const Note& n) noexcept
{
    const int nc = std::clamp ((int) n.nChord, 1, 4);
    float m = 0.f;
    for (int c = 0; c < nc; ++c)
        m += (float) n.chord[c] / (float) nc;
    return m;
}

inline void setTones (Note& n, const int* v, int nv) noexcept
{
    n.nChord = (std::uint8_t) std::clamp (nv, 1, 4);
    for (int c = 0; c < 4; ++c)
        n.chord[c] = (std::int8_t) (c < nv ? v[c] : 0);
}

// The pad plays the stab's chord of every bar it sounds in. A swell crossing into a bar with
// another chord changes chord on the bar line (a tail under four 16ths ends on the bar line
// instead); its voicing sits near the pad's own register and moves smoothly from its last chord.
// A silent stab leaves the pad as drawn.
inline void padFollowsStab (Lane& pad, const Lane& stab, const Lane& bass, int bars) noexcept
{
    if (pad.count <= 0 || ! anyHit (stab))
        return;
    const Lane src = pad;
    pad.count = 0;
    int prev[4] {};
    int nPrev = 0;
    for (int i = 0; i < src.count && pad.count < kMaxNotes; ++i)
    {
        const Note& n = src.notes[i];
        if (n.vel <= 2)
        {
            pad.notes[pad.count++] = n;
            continue;
        }
        struct Seg
        {
            float a, b;
            Chord c;
        };
        Seg seg[kMaxBars + 2];
        int ns = 0;
        const float end = n.start + n.length;
        float t = n.start;
        while (t < end - 1.0e-3f && ns < kMaxBars + 2)
        {
            const int bar = phrase_lock::barOf (t);
            const float be = std::min (end, (float) ((bar + 1) * kStepsPerBar));
            Chord c;
            if (! chordOfBar (stab, bass, bar, bars, t, be, c))
                break;
            if (ns > 0 && seg[ns - 1].c.pcs == c.pcs && seg[ns - 1].c.ask == c.ask)
                seg[ns - 1].b = be;
            else if (ns > 0 && be - t < 4.f)
                break;
            else
                seg[ns++] = { t, be, c };
            t = be;
        }
        const float own = meanTone (n);
        for (int k = 0; k < ns && pad.count < kMaxNotes; ++k)
        {
            Note m = n;
            m.start = seg[k].a;
            m.length = seg[k].b - seg[k].a;
            // The clash rule drops a tone for the whole note: a bass note a semitone from the chord
            // late in the swell (after the first beat and past 60% of it) ends the swell there
            // instead, so the pad holds the whole chord while it sounds.
            const std::uint32_t bad = semitonesOf (seg[k].c.pcs);
            float firstClash = m.start + m.length;
            for (int i2 = 0; i2 < bass.count; ++i2)
            {
                const Note& b = bass.notes[i2];
                if (b.vel <= 2 || ((bad >> pcOf (b.chord[0])) & 1u) == 0)
                    continue;
                if (b.start < m.start + m.length - 1.0e-3f && m.start < b.start + b.length - 1.0e-3f)
                    firstClash = std::min (firstClash, std::max (b.start, m.start));
            }
            if (firstClash < m.start + m.length - 1.0e-3f && firstClash - m.start >= std::max (4.f, 0.6f * m.length))
                m.length = firstClash - m.start;
            int v[4];
            int nv = voiceChord (seg[k].c, 3, own, prev, nPrev, kVoiceLo, kVoiceHi, v);
            if (nv == 0)
            {
                nv = seg[k].c.n;
                for (int c = 0; c < nv; ++c)
                    v[c] = seg[k].c.tones[c];
            }
            setTones (m, v, nv);
            nPrev = nv;
            for (int c = 0; c < nv; ++c)
                prev[c] = v[c];
            pad.notes[pad.count++] = m;
        }
    }
    std::stable_sort (pad.notes, pad.notes + pad.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
}

// Every hit is a chord hit (writeHits draws chords; one the clash rule thinned is still one): it
// plays the stab's chord at its spot (the stab hit it lands on with IN STAB), voiced near the
// hit's own register. A silent stab leaves the hits as drawn. No voice-leading term, so a
// restated bar gets exactly its source bar's voicing.
inline void hitsFollowStab (Lane& hits, const Lane& stab, const Lane& bass, int bars) noexcept
{
    if (hits.count <= 0 || ! anyHit (stab))
        return;
    for (int i = 0; i < hits.count; ++i)
    {
        Note& n = hits.notes[i];
        if (n.vel <= 2)
            continue;
        Chord c;
        if (! chordAt (stab, bass, n.start, n.length, bars, c))
            continue;
        int v[4];
        int nv = voiceChord (c, 2, meanTone (n), nullptr, 0, kVoiceLo, kVoiceHi, v);
        if (nv == 0)
        {
            nv = c.n;
            for (int k = 0; k < nv; ++k)
                v[k] = c.tones[k];
        }
        setTones (n, v, nv);
    }
}
} // namespace chord_loop

namespace accent
{
// Accent on an anchor (+kLo..+kHi, drawn per roll) and on an e / a or ghost hit.
inline constexpr int kAnchorLo = 12;
inline constexpr int kAnchorHi = 18;
inline constexpr int kOffBeat = -10;

struct Table
{
    bool any = false;
    bool cell[2][kStepsPerBar] {};
    std::int8_t off[2][kStepsPerBar] {};
};

inline int slotIn (float start, int bar) noexcept
{
    return std::clamp ((int) std::lround (start) - bar * kStepsPerBar, 0, kStepsPerBar - 1);
}

inline std::uint32_t mixSeed (std::uint32_t seed, std::uint32_t salt) noexcept
{
    std::uint32_t h = (seed ? seed : 1u) ^ (salt * 0x9E3779B9u);
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h ? h : 1u;
}

// The lane's accent pattern over its cell (bars 1-2), drawn from its seed: the accent size, how
// many anchors carry it (each with a per-roll chance, an 'and' or the bar's first hit likelier),
// at least one accented anchor and, when the cell has two or more anchors, at least one that is not.
inline Table draw (const Lane& lane, std::uint32_t salt) noexcept
{
    Table t;
    bool ghost[2][kStepsPerBar] {};
    int nEv = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const Note& n = lane.notes[i];
        const int b = phrase_lock::barOf (n.start);
        if (n.vel <= 2 || b < 0 || b > 1)
            continue;
        const int q = slotIn (n.start, b);
        if (! t.cell[b][q])
            ++nEv;
        t.cell[b][q] = true;
        if ((n.flags & NoteGhost) != 0)
            ghost[b][q] = true;
    }
    if (nEv == 0)
        return t;
    t.any = true;
    Rng r (mixSeed (lane.seed, salt));
    const int amount = kAnchorLo + (int) (r.nextU() % (std::uint32_t) (kAnchorHi - kAnchorLo + 1));
    const double chance = 0.35 + 0.30 * r.next();
    bool anchor[2][kStepsPerBar] {};
    int nAnchor = 0;
    for (int b = 0; b < 2; ++b)
        for (int q = 0; q < kStepsPerBar; ++q)
            if (t.cell[b][q] && ! ghost[b][q] && q % 2 == 0)
            {
                anchor[b][q] = true;
                ++nAnchor;
            }
    // A cell with no hit on a beat or an 'and': each bar's first hit anchors it.
    if (nAnchor == 0)
        for (int b = 0; b < 2; ++b)
            for (int q = 0; q < kStepsPerBar; ++q)
                if (t.cell[b][q])
                {
                    anchor[b][q] = true;
                    ++nAnchor;
                    break;
                }
    bool acc[2][kStepsPerBar] {};
    float score[2][kStepsPerBar] {};
    int nAcc = 0;
    for (int b = 0; b < 2; ++b)
    {
        bool first = true;
        for (int q = 0; q < kStepsPerBar; ++q)
        {
            if (! t.cell[b][q])
                continue;
            if (anchor[b][q])
            {
                const double w = (q % 4 == 2 ? 1.3 : 0.8) * (first ? 1.25 : 1.0);
                acc[b][q] = r.next() < chance * w;
                score[b][q] = (float) (w * (0.5 + r.next()));
                nAcc += acc[b][q] ? 1 : 0;
            }
            first = false;
        }
    }
    auto extreme = [&] (bool wantAcc, bool highest) {
        int bb = -1, bq = -1;
        for (int b = 0; b < 2; ++b)
            for (int q = 0; q < kStepsPerBar; ++q)
                if (anchor[b][q] && acc[b][q] == wantAcc
                    && (bb < 0 || (highest ? score[b][q] > score[bb][bq] : score[b][q] < score[bb][bq])))
                {
                    bb = b;
                    bq = q;
                }
        if (bb >= 0)
            acc[bb][bq] = ! wantAcc;
    };
    // The accent is a contrast between the anchors themselves (an e / a hit the arrangement or the
    // clash rule drops later must not leave every hit accented): with two or more anchors, at
    // least one is not accented.
    if (nAcc == 0)
        extreme (false, true);
    else if (nAnchor >= 2 && nAcc == nAnchor)
        extreme (true, false);
    for (int b = 0; b < 2; ++b)
        for (int q = 0; q < kStepsPerBar; ++q)
            if (t.cell[b][q])
                t.off[b][q] = (std::int8_t) (acc[b][q] ? amount : (anchor[b][q] ? 0 : kOffBeat));
    return t;
}

// Every bar reads the cell bar of its parity, so bars 3-7 carry bars 1-2's accents; a 16th the
// cell does not play (the turn's own hits) is at the base on a beat or an 'and', -10 on an e / a.
inline void apply (Lane& lane, const Table& t) noexcept
{
    if (! t.any)
        return;
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        if (n.vel <= 2)
            continue;
        const int bar = std::max (0, phrase_lock::barOf (n.start));
        const int cb = bar % 2;
        const int q = slotIn (n.start, bar);
        int off = 0;
        if (t.cell[cb][q])
            off = t.off[cb][q];
        else if (q % 2 == 1 || (n.flags & NoteGhost) != 0)
            off = kOffBeat;
        n.vel = (std::uint8_t) std::clamp ((int) n.vel + off, 8, 127);
    }
}
} // namespace accent
} // namespace rolla
