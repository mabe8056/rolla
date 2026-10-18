#pragma once

// JACKIN loop identity (Maestro #13 job 2). generate() calls this for Gaskin
// only, after JACKIN's own bass passes. Half the rolls played eight different
// bars, so the groove never settled. The roll's own first two bars are now the
// hook and the loop plays A B A B4 A B A B8:
//   B4  B with a small answer in its back quarter (a pitch the hook already
//       uses, or one note fewer / more)
//   B8  B's first half, then the turn (held approach note, 4/16 rest, maybe a
//       pickup) from RollerPass.h
// The hook itself is whatever the grammar drew for this roll, so the variety
// across rolls is untouched. Header-only.

#include "RollerPass.h"
#include "StyleJackin.h"

namespace rolla::jackin
{
using namespace deepbouncy::detail;

inline void copyBar (const Lane& from, int srcBar, Lane& to, int dstBar, int bars)
{
    for (int i = 0; i < from.count && to.count < kMaxNotes; ++i)
    {
        const Note& n = from.notes[i];
        if ((int) std::floor (n.start / 16.f + 1e-4f) != srcBar)
            continue;
        Note c = n;
        c.start += (float) ((dstBar - srcBar) * 16);
        if (dstBar >= bars)
            continue;
        to.notes[to.count++] = c;
    }
}

inline void answerBar (Lane& lane, int bar, int bars, Rng& r)
{
    const float origin = (float) (bar * 16);
    int idx[16];
    int m = 0;
    for (int i = 0; i < lane.count && m < 16; ++i)
        if (lane.notes[i].start >= origin && lane.notes[i].start < origin + 16.f)
            idx[m++] = i;
    if (m == 0)
        return;
    // Pitches the hook already plays: the answer stays in JACKIN's mode.
    int pool[16];
    int np = 0;
    for (int i = 0; i < lane.count && np < 16; ++i)
        if (lane.notes[i].start < 32.f)
        {
            bool have = false;
            for (int k = 0; k < np; ++k)
                have = have || pool[k] == lane.notes[i].chord[0];
            if (! have)
                pool[np++] = lane.notes[i].chord[0];
        }
    const float x = (float) r.next();
    Note& last = lane.notes[idx[m - 1]];
    if (x < 0.55f && np > 1)
    {
        for (int t = 0; t < 6; ++t)
        {
            const int semi = pool[(int) (r.next() * np)];
            if (semi != last.chord[0])
            {
                last.chord[0] = (std::int8_t) semi;
                break;
            }
        }
    }
    else if (x < 0.8f && m > 2)
    {
        // One note fewer: the note before rings on into the space.
        const float end = last.start + last.length;
        const int at = idx[m - 1];
        for (int i = at; i + 1 < lane.count; ++i)
            lane.notes[i] = lane.notes[i + 1];
        --lane.count;
        if (at > 0)
            lane.notes[at - 1].length = std::max (lane.notes[at - 1].length, std::min (end, origin + 16.f) - lane.notes[at - 1].start);
    }
    else
    {
        // One note more, in the back quarter where the bass is silent.
        for (int s = 13; s < 16; ++s)
        {
            const float t = origin + (float) s;
            if (! sounding (lane, t) && ! onsetAt (lane, t) && (s & 3) != 0)
            {
                roller::insertNote (lane, t, 1.f, pool[(int) (r.next() * np)], 100, 0, (float) (bars * 16));
                break;
            }
        }
    }
    sortLane (lane);
}

// The bar pair (0-1, 2-3, ...) with the most JACKIN flavour (dotted 16ths,
// b2 colour), as long as the root is clearly the most-sounding pitch class,
// the b2 stays <= 20% of the time and nothing sits outside JACKIN's scale.
// Ties, and rolls where no pair qualifies, go to the earliest pair.
inline int pickHook (const Lane& lane, int bars)
{
    int best = 0;
    float bestScore = -1.f;
    for (int p = 0; p * 2 + 1 < bars && p < 4; ++p)
    {
        int att = 0, dotted = 0, inA = 0, inB = 0;
        float t = 0.f, b2 = 0.f, out = 0.f;
        float pcTime[12] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const Note& n = lane.notes[i];
            const int bar = (int) std::floor (n.start / 16.f + 1e-4f);
            if (bar != p * 2 && bar != p * 2 + 1)
                continue;
            (bar == p * 2 ? inA : inB) += 1;
            const int slot = (int) std::lround (n.start) & 15;
            ++att;
            dotted += (slot % 3 == 0) ? 1 : 0;
            const int pc = pcOf (n.chord[0]);
            t += n.length;
            pcTime[pc] += n.length;
            b2 += pc == 1 ? n.length : 0.f;
            out += (pc == 4 || pc == 6 || pc == 9 || pc == 11) ? n.length : 0.f;
        }
        // Repeated four times, the pair sets the key a listener hears: the root
        // has to be clearly the most-sounding pitch class.
        float other = 0.f;
        for (int pc = 1; pc < 12; ++pc)
            other = std::max (other, pcTime[pc]);
        if (inA == 0 || inB == 0 || t <= 0.f || b2 > 0.20f * t || out > 0.f || pcTime[0] < 1.15f * other)
            continue;
        const float score = std::min (1.5f, (float) dotted / (0.55f * (float) att)) + std::min (1.5f, b2 / (0.10f * t));
        if (score > bestScore + 1e-4f)
        {
            bestScore = score;
            best = p;
        }
    }
    return best;
}

inline void loopify (Lane& lane, int bars, std::uint32_t seed, float busy = 0.40f)
{
    if (lane.count <= 0)
        return;
    if (bars < 4)
    {
        // A 2-bar loop is the pair itself.
        if (bars == 2)
            shapePair (lane, seed, busy);
        return;
    }
    Rng r (mix (seed, 0x1AC4u));
    const int hook = pickHook (lane, bars);
    // The hook pair gets JACKIN's density and kick placement before it repeats.
    Lane src;
    src.seed = lane.seed;
    copyBar (lane, hook * 2, src, 0, 2);
    copyBar (lane, hook * 2 + 1, src, 1, 2);
    shapePair (src, seed, busy);
    lane.count = 0;
    // A B A B4 A B A B8 (4 bars: A B A B8), from the pair that carries the most JACKIN flavour.
    for (int bar = 0; bar < bars; ++bar)
        copyBar (src, bar & 1, lane, bar, bars);
    sortLane (lane);
    if (bars >= 8)
        answerBar (lane, 3, bars, r);
    // Monophonic again before the turn.
    for (int i = 0; i + 1 < lane.count; ++i)
        if (lane.notes[i].start + lane.notes[i].length > lane.notes[i + 1].start)
            lane.notes[i].length = std::max (0.5f, lane.notes[i + 1].start - lane.notes[i].start);
    roller::turnBar8 (lane, bars, seed ^ 0x6A6Bu);
}

} // namespace rolla::jackin
