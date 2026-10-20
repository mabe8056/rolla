#pragma once

#include "Generator.h"
#include "Pattern.h"
#include "RuleComposer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// CHORDS (Melak, Oct 20, round 4: "starting on the chord note is important too because if we build
// on top if it, we have a chord structure to work with"; round 7 named the riffs to build on). The
// bass riff decides the chords. Each half bar (CHORDS 1) or bar (CHORDS 2) takes the chord rooted on
// its structural bass note: the one that sounds most there, with a bonus for the first attack and
// the accent; ghosts and notes that carry no chord (a diminished step, notes outside the mode) are
// passing notes. A bar's chord is the one its bass starts on.
//
// The chord is the mode's seventh / ninth on that root in DEEP's colours ("deeps chords sound the
// most like music"): minor 9, minor 7, minor 11 on a minor root; on a major root add9 or 6/9, since
// a major 7th is a semitone under the root the bass plays and the upper lanes give way to the bass
// there (fixUpperClashes); DEEP keeps no dominant 7th. Rootless: the bass has the root. One voicing per
// chord for the whole roll, chosen so the voices move little and the top keeps a common tone or
// steps by 1-2 semitones (54% of the pack chord loops' changes step on top, PackHarmony.h). Pooled
// from the pack chord loops: one to three chords a loop, i first, the moves to iv / bVII / bIII / v.
// Nothing is taken from any one file.
//
// The stab keeps its rhythm, lengths and velocities; only pitches change, and a held chord the bass
// moves away from ends at the change and the new chord speaks there.

namespace rolla
{
namespace bass_chords
{
inline int pcOf (int v) noexcept
{
    const int p = v % 12;
    return p < 0 ? p + 12 : p;
}

inline constexpr int kGhostVel = 60;    // riff ghosts are written at 48-56
inline constexpr int kAccentVel = 110;  // the riff accent is 116
inline constexpr int kRegions = kMaxBars * 2;
inline constexpr int kMaxVoicings = 24;

enum Quality : int { Min9 = 0, Min7, Min11, Add9, Six9, kQualities };
// Preference among the colours a root allows (minor 9 is DEEP's signature minor colour).
inline constexpr float kQualityW[kQualities] = { 0.50f, 0.25f, 0.25f, 0.55f, 0.45f };

struct Chord
{
    int root = 0; // pitch class over the key root
    int quality = Min7;
    int n = 0;
    int iv[4] {}; // rootless tones over the root
};

// The chord of `quality` on pitch class `root`, stacked in thirds in the mode; false when the mode
// does not give that colour there.
inline bool chordOn (int root, int mode, int quality, Chord& c) noexcept
{
    int deg = -1;
    for (int d = 0; d < 7 && deg < 0; ++d)
        if (phrase::degreeToSemis (d, mode) == root)
            deg = d;
    if (deg < 0)
        return false;
    auto above = [&] (int k) { return phrase::degreeToSemis (deg + k, mode) - root; };
    const int third = above (2), fifth = above (4), sixth = above (5), seventh = above (6), ninth = above (8), eleventh = above (10);
    const bool minor = third == 3 && fifth == 7 && seventh == 10;
    const bool major = third == 4 && fifth == 7;
    auto set = [&] (std::initializer_list<int> tones)
    {
        c.root = root;
        c.quality = quality;
        c.n = 0;
        for (int t : tones)
            c.iv[c.n++] = t;
        return true;
    };
    switch (quality)
    {
        case Min9:  return minor && ninth == 14 ? set ({ 3, 7, 10, 14 }) : false;
        case Min7:  return minor ? set ({ 3, 7, 10 }) : false;
        case Min11: return minor && ninth == 14 && eleventh == 17 ? set ({ 3, 10, 14, 17 }) : false;
        case Add9:  return major && ninth == 14 ? set ({ 4, 7, 14 }) : false;
        case Six9:  return major && ninth == 14 && sixth == 9 ? set ({ 4, 9, 14 }) : false;
        default:    return false;
    }
}

inline bool chordable (int pc, int mode) noexcept
{
    Chord c;
    for (int q = 0; q < kQualities; ++q)
        if (chordOn (pc, mode, q, c))
            return true;
    return false;
}

// The structural bass pitch class over [a, b), or -1 when the bass rests or only passes there.
inline int rootIn (const Lane& bass, float a, float b, int mode) noexcept
{
    float score[12] {};
    float first = 1.0e9f;
    int firstPc = -1;
    for (int i = 0; i < bass.count; ++i)
    {
        const Note& n = bass.notes[i];
        if (n.vel < kGhostVel)
            continue;
        const float s = n.start;
        const float e = n.start + n.length;
        const float ov = std::min (e, b) - std::max (s, a);
        if (ov <= 1.0e-3f)
            continue;
        const int pc = pcOf (n.chord[0]);
        score[pc] += std::min (ov, 4.f);
        if (s < a - 1.0e-3f)
            score[pc] += 0.5f; // held over the line: it was the root already
        else
        {
            if (s < first)
            {
                first = s;
                firstPc = pc;
            }
            if (n.vel >= kAccentVel)
                score[pc] += 1.f;
        }
    }
    if (firstPc >= 0)
        score[firstPc] += 1.5f;
    int best = -1;
    for (int pc = 0; pc < 12; ++pc)
        if (score[pc] > 0.f && chordable (pc, mode) && (best < 0 || score[pc] > score[best]))
            best = pc;
    return best;
}

struct Region
{
    float start = 0.f;
    float end = 16.f;
    int root = 0;
};

// Regions of `grain` 16ths (8 or 16) over `bars` bars, each with its chord root. A bar region's
// root is the one its first half starts on. A region with no root keeps the one before (the first
// falls back to the key root).
inline int regionsOf (const Lane& bass, int bars, int grain, int mode, Region* out) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    const int g = grain >= 16 ? 16 : 8;
    int n = 0;
    int prev = -1;
    for (int b = 0; b < nb; ++b)
        for (int h = 0; h < 16; h += g)
        {
            const float a = (float) (b * 16 + h);
            int root = rootIn (bass, a, a + 8.f, mode);
            if (root < 0 && g == 16)
                root = rootIn (bass, a, a + 16.f, mode);
            if (root < 0)
                root = prev >= 0 ? prev : 0;
            out[n].start = a;
            out[n].end = a + (float) g;
            out[n].root = root;
            prev = root;
            ++n;
        }
    return n;
}

struct Voicing
{
    int n = 0;
    int p[4] {}; // ascending
    int top = 0;
};

// Close and drop-2 voicings of `c` with every tone in [lo, hi], no two tones a minor 2nd or minor
// 9th apart, spanning at most 15 semitones.
inline int voicingsOf (const Chord& c, int lo, int hi, Voicing* out) noexcept
{
    int nv = 0;
    for (int topIdx = 0; topIdx < c.n; ++topIdx)
        for (int top = hi; top >= lo + 5; --top)
        {
            if (pcOf (top) != pcOf (c.root + c.iv[topIdx]))
                continue;
            for (int drop = 0; drop < 2; ++drop)
            {
                int p[4];
                int m = 0;
                p[m++] = top;
                for (int k = 0; k < c.n; ++k)
                {
                    if (k == topIdx)
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
                    p[m - 2] -= 12; // drop the second voice from the top
                    std::sort (p, p + m);
                }
                bool ok = p[0] >= lo && p[m - 1] - p[0] <= 15;
                for (int i = 0; i < m && ok; ++i)
                    for (int j = i + 1; j < m && ok; ++j)
                        ok = (p[j] - p[i]) != 1 && (p[j] - p[i]) != 13;
                if (! ok || nv >= kMaxVoicings)
                    continue;
                Voicing& v = out[nv++];
                v.n = m;
                v.top = top;
                for (int i = 0; i < m; ++i)
                    v.p[i] = p[i];
            }
        }
    return nv;
}

inline float motion (const Voicing& a, const Voicing& b) noexcept
{
    float sum = 0.f;
    for (int k = 0; k < b.n; ++k)
    {
        int best = 99;
        for (int j = 0; j < a.n; ++j)
            best = std::min (best, std::abs (b.p[k] - a.p[j]));
        sum += (float) best;
    }
    const int d = std::abs (a.top - b.top);
    const float topCost = (d == 1 || d == 2) ? 0.f : (d == 0 ? 1.f : (d <= 4 ? 3.f : 10.f));
    return sum + 2.f * topCost;
}

// Bass pitch classes (not ghosts) sounding over [a, b).
inline unsigned bassPcs (const Lane& bass, float a, float b) noexcept
{
    unsigned m = 0u;
    for (int i = 0; i < bass.count; ++i)
    {
        const Note& n = bass.notes[i];
        if (n.vel >= kGhostVel && n.start < b - 1.0e-3f && a < n.start + n.length - 1.0e-3f)
            m |= 1u << (unsigned) pcOf (n.chord[0]);
    }
    return m;
}

// Writes the chords over the bass into the stab's own hits. False (the lane untouched) when no
// voicing fits. `lo` / `hi` are the stab register (semitones over the key root at the stab base).
inline bool write (Lane& stab, const Lane& bass, int mode, int bars, int grain, std::uint32_t seed, int lo, int hi) noexcept
{
    Region reg[kRegions];
    const int nr = regionsOf (bass, bars, grain, mode, reg);
    if (nr == 0)
        return false;
    // the distinct roots in order of first appearance (at most 4 chords a roll)
    int roots[4];
    int nRoots = 0;
    for (int i = 0; i < nr; ++i)
    {
        bool seen = false;
        for (int k = 0; k < nRoots; ++k)
            seen = seen || roots[k] == reg[i].root;
        if (! seen)
        {
            if (nRoots == 4)
                reg[i].root = reg[i > 0 ? i - 1 : 0].root;
            else
                roots[nRoots++] = reg[i].root;
        }
    }
    auto rootIdx = [&] (int pc) { for (int k = 0; k < nRoots; ++k) if (roots[k] == pc) return k; return 0; };
    // bass under each chord: a tone a semitone over a bass note it sounds with is not voiced
    unsigned under[4] {};
    for (int i = 0; i < nr; ++i)
        under[rootIdx (reg[i].root)] |= bassPcs (bass, reg[i].start, reg[i].end);

    Rng r (seed ^ 0x43484F52u);
    Chord chord[4];
    Voicing voic[4][kMaxVoicings];
    int nv[4] {};
    for (int k = 0; k < nRoots; ++k)
    {
        float w[kQualities] {};
        Chord cand[kQualities];
        float sum = 0.f;
        for (int q = 0; q < kQualities; ++q)
            if (chordOn (roots[k], mode, q, cand[q]))
            {
                bool clash = false;
                for (int t = 0; t < cand[q].n; ++t)
                {
                    const int pc = pcOf (roots[k] + cand[q].iv[t]);
                    clash = clash || (under[k] & ((1u << (unsigned) pcOf (pc - 1)) | (1u << (unsigned) pcOf (pc + 1)))) != 0;
                }
                w[q] = kQualityW[q] * (clash ? 0.05f : 1.f);
                sum += w[q];
            }
        if (sum <= 0.f)
            return false;
        float x = (float) r.next() * sum;
        int q = 0;
        for (; q < kQualities - 1; ++q)
        {
            if (w[q] <= 0.f)
                continue;
            x -= w[q];
            if (x <= 0.f)
                break;
        }
        while (w[q] <= 0.f)
            q = (q + 1) % kQualities;
        chord[k] = cand[q];
        nv[k] = voicingsOf (chord[k], lo, hi, voic[k]);
        if (nv[k] == 0)
            return false;
    }
    // the chord changes in time order (cyclic), as pairs of chord indices
    int chA[kRegions], chB[kRegions];
    int nch = 0;
    for (int i = 0; i < nr; ++i)
    {
        const int a = rootIdx (reg[i].root);
        const int b = rootIdx (reg[(i + 1) % nr].root);
        if (a != b)
        {
            chA[nch] = a;
            chB[nch] = b;
            ++nch;
        }
    }
    // every voicing combination; keep the cheapest few and draw one
    constexpr int kKeep = 6;
    float bestCost[kKeep];
    int bestPick[kKeep][4];
    int kept = 0;
    int pick[4] {};
    const int centre = 14;
    for (;;)
    {
        float cost = 0.f;
        for (int k = 0; k < nRoots; ++k)
        {
            const Voicing& v = voic[k][pick[k]];
            cost += 0.3f * (float) std::abs (v.top - centre) + 0.2f * (float) std::max (0, v.p[v.n - 1] - v.p[0] - 11);
        }
        for (int c = 0; c < nch; ++c)
            cost += motion (voic[chA[c]][pick[chA[c]]], voic[chB[c]][pick[chB[c]]]) / (float) std::max (1, nch / std::max (1, nRoots));
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
    float pw[kKeep];
    float total = 0.f;
    for (int i = 0; i < kept; ++i)
    {
        pw[i] = std::exp (-(bestCost[i] - bestCost[0]));
        total += pw[i];
    }
    float y = (float) r.next() * total;
    int chosen = 0;
    for (; chosen < kept - 1; ++chosen)
    {
        y -= pw[chosen];
        if (y <= 0.f)
            break;
    }
    const int* use = bestPick[chosen];

    auto regionAt = [&] (float t) { for (int i = 0; i < nr; ++i) if (t < reg[i].end - 1.0e-3f) return i; return nr - 1; };
    auto setTones = [&] (Note& n, int ri)
    {
        const int k = rootIdx (reg[ri].root);
        const Voicing& v = voic[k][use[k]];
        // a tone a semitone from a bass note sounding with the hit gives way, as the upper lanes do
        const unsigned bp = bassPcs (bass, n.start, n.start + std::max (0.25f, n.length));
        int w = 0;
        std::int8_t t[4] {};
        for (int i = 0; i < v.n; ++i)
            if ((bp & ((1u << (unsigned) pcOf (v.p[i] - 1)) | (1u << (unsigned) pcOf (v.p[i] + 1)))) == 0)
                t[w++] = (std::int8_t) v.p[i];
        if (w == 0)
            t[w++] = (std::int8_t) v.p[v.n - 1];
        n.nChord = (std::uint8_t) std::max (1, w);
        for (int c = 0; c < 4; ++c)
            n.chord[c] = c < w ? t[c] : 0;
    };
    const int count0 = stab.count;
    for (int i = 0; i < count0; ++i)
    {
        Note& n = stab.notes[i];
        if (n.vel <= 2)
            continue;
        const int ri = regionAt (n.start);
        const float end = n.start + n.length;
        if (end > reg[ri].end + 0.5f && ri + 1 < nr && reg[ri + 1].root != reg[ri].root)
        {
            // the bass moves under the held chord: it ends at the change, the next chord speaks
            const float at = reg[ri].end;
            bool taken = false;
            for (int j = 0; j < stab.count; ++j)
                taken = taken || (j != i && stab.notes[j].vel > 2 && std::fabs (stab.notes[j].start - at) < 0.75f);
            Note tail = n;
            n.length = std::max (0.5f, at - n.start - 0.25f);
            if (! taken && end - at >= 1.f && stab.count < kMaxNotes)
            {
                tail.start = at;
                tail.length = end - at;
                tail.vel = (std::uint8_t) std::max (1, (int) tail.vel * 9 / 10);
                setTones (tail, ri + 1);
                stab.notes[stab.count++] = tail;
            }
        }
        setTones (n, ri);
    }
    std::sort (stab.notes, stab.notes + stab.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
    return true;
}
} // namespace bass_chords
} // namespace rolla
