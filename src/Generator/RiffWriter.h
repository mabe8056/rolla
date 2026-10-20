#pragma once

#include "Generator.h"
#include "Pattern.h"
#include "RuleComposer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// RIFF (Melak, Oct 20: "still just doesnt sound like a catchy song lol. its not good bass riffs").
// The rule composer drew the bass one note at a time from pooled tables: shapeless 2-bar lines that
// matched none of the riff types real basslines use (7 of 160 rolls). This writer designs the riff
// first, then builds and repeats it.
//
// Every number below is a per-riff feature measured (learn only, no riff stored or copied) on:
//   - his favourite basslines: Ellia Jaya, LIGHTLEAK, Luke Dean "Get Busy", Ozzie Guven "House
//     Party", Gaskin (peak drops of the tracks on his Mac);
//   - the 229 Sine Artist Series bass loops (Veritas, Di Chiara Brothers, Candidate, Dan Fresco,
//     Abdul Raeva, Stef Davidse), his chosen sound source;
//   - the 213 pack MIDI bass loops and his own FL bass parts.
// What they share: the riff starts on beat 1 (88-100%); 5-7 attacks a bar spread over all four 16th
// positions (never only offbeats); 3-5 notes from a minor-pentatonic-like set, the root on ~40%;
// mostly 1/16 and 1/8 notes, connected; one of a few shapes, repeated exactly: a 1-bar riff (Mambos,
// House Party, most Stef Davidse loops), a 2-bar call and response (Get Busy), rolling 16ths
// (Ellia Jaya, West Side), or a held line (organ / legato); the answer bar (bar 8) changes rhythm
// only, 2-4 slots.

namespace rolla
{
namespace riff
{
enum Shape : int
{
    OneBar = 0,
    CallResponse,
    Rolling,
    Held,
    kShapes
};

inline constexpr const char* kShapeNames[kShapes] = { "riff_1bar", "riff_call_response", "riff_rolling", "riff_held" };

// Shape weights per style (ROLLER, DEEP, BOUNCY, JACKIN): his favourites are 1-bar riffs and call /
// response first, rolling 16ths for the roller sound, held lines for organ / deep.
inline constexpr float kShapeW[4][kShapes] = {
    { 0.25f, 0.20f, 0.45f, 0.10f },
    { 0.35f, 0.30f, 0.15f, 0.20f },
    { 0.40f, 0.30f, 0.20f, 0.10f },
    { 0.35f, 0.35f, 0.10f, 0.20f },
};
// Attacks per bar by shape (favourites ~5 re-attacks; Artist loops 6 [4.5-7]; rolling 8.5; held 3.5).
inline constexpr int kAttacksLo[kShapes] = { 5, 5, 7, 3 };
inline constexpr int kAttacksHi[kShapes] = { 7, 6, 9, 4 };
// Onset weight per 16th: the 229 Artist Series loops pooled, the e's (1, 5, 9, 13) x1.5 toward his
// favourite tracks (28% of their onsets on the e against 13% in the loops).
inline constexpr float kStepW[16] = { 0.73f, 0.26f, 0.32f, 0.34f, 0.36f, 0.30f, 0.58f, 0.33f,
                                      0.33f, 0.38f, 0.48f, 0.31f, 0.42f, 0.32f, 0.63f, 0.15f };
// Gate (length against the gap to the next onset) by shape; held lines tie.
inline constexpr float kGate[kShapes] = { 0.82f, 0.85f, 0.85f, 1.0f };
// Organ-style octave bounce (Get Busy, House Party, Josh Baker's organ; 33 of 229 loops).
inline constexpr float kOrganP[kShapes] = { 0.30f, 0.30f, 0.15f, 0.50f };

struct Cell
{
    bool on[16] {};
    int semi[16] {};
    int vel[16] {};
    int n = 0;
};

inline int pickW (const float* w, int n, double u) noexcept
{
    float sum = 0.f;
    for (int i = 0; i < n; ++i)
        sum += std::max (0.f, w[i]);
    if (sum <= 0.f)
        return -1;
    float x = (float) u * sum;
    for (int i = 0; i < n; ++i)
    {
        if (w[i] <= 0.f)
            continue;
        x -= w[i];
        if (x <= 0.f)
            return i;
    }
    return n - 1;
}

inline int runAt (const bool* on, int k) noexcept
{
    int run = 1;
    for (int j = k - 1; j >= 0 && on[j]; --j)
        ++run;
    for (int j = k + 1; j < 16 && on[j]; ++j)
        ++run;
    return run;
}

// A rhythm cell: beat 1 always; the rest by the step weights, at most `maxRun` 16ths in a row and
// at most `maxKick` more attacks on the kick steps (the bass sits mostly off the kick).
inline void drawRhythm (Cell& c, Rng& r, int want, int maxRun, int maxKick, const bool* keep = nullptr) noexcept
{
    for (int k = 0; k < 16; ++k)
        c.on[k] = keep != nullptr && keep[k];
    c.on[0] = true;
    // A pickup in the last beat (16th 13-15) leads into the next bar (pack loops: beat-4 pickup).
    if (r.next() < 0.8 && ! c.on[13] && ! c.on[14] && ! c.on[15])
    {
        const float pw[3] = { kStepW[13], kStepW[14], kStepW[15] };
        const int pk = pickW (pw, 3, r.next());
        c.on[13 + std::max (0, pk)] = true;
    }
    int kicks = 0;
    for (int k = 4; k < 16; k += 4)
        kicks += c.on[k] ? 1 : 0;
    int n = 0;
    for (bool b : c.on)
        n += b ? 1 : 0;
    while (n < want)
    {
        float w[16];
        for (int k = 0; k < 16; ++k)
        {
            w[k] = 0.f;
            if (c.on[k])
                continue;
            if ((k & 3) == 0 && kicks >= maxKick)
                continue;
            c.on[k] = true;
            const bool runOk = runAt (c.on, k) <= maxRun;
            c.on[k] = false;
            if (runOk)
                w[k] = kStepW[k];
        }
        const int k = pickW (w, 16, r.next());
        if (k < 0)
            break;
        c.on[k] = true;
        kicks += (k & 3) == 0 ? 1 : 0;
        ++n;
    }
    c.n = n;
}

// Departure intervals above (or just below) the bar's anchor. Melak's verdicts on round 3 (Oct 20):
// the good riff used the root, b3 and 5 ("1 b3 b3 5 5 5 1"), the near one root, 5 and b6 as the
// 5th's neighbour; the rejects ping-ponged root / 2 / b7 / 4. Pack loops: departures go above the
// anchor in 75%, b3 / 5 / 4 lead, the 2nd and b7 sit next to the anchor. Weights by interval:
inline constexpr int kDepIv[8] = { 3, 7, 5, 10, 8, 12, 2, -2 };
inline constexpr float kDepW[8] = { 0.25f, 0.25f, 0.12f, 0.10f, 0.08f, 0.08f, 0.06f, 0.06f };
// P(a note is off the anchor) by beat (pack loops: the riff states the anchor on beat 1 and moves in
// the second half), scaled toward his picks (anchor share ~.3).
inline constexpr float kDepartByBeat[4] = { 0.30f, 0.52f, 0.64f, 0.86f };

inline bool ivInMode (int anchor, int iv, int mode) noexcept
{
    return phrase::inMode (anchor + iv, mode);
}

// The departure set for one anchor: 2-3 intervals, the first a chord colour (b3 or 5).
inline int drawDepartures (int anchor, int mode, bool organ, int count, Rng& r, int* out) noexcept
{
    float w[8];
    for (int i = 0; i < 8; ++i)
    {
        w[i] = ivInMode (anchor, kDepIv[i], mode) ? kDepW[i] : 0.f;
        if (kDepIv[i] == 12)
            w[i] = organ ? 0.25f : 0.f;
    }
    int n = 0;
    // The first departure is the b3 or the 5th above the anchor when the mode has it.
    {
        float cw[8] {};
        for (int i = 0; i < 8; ++i)
            cw[i] = (kDepIv[i] == 3 || kDepIv[i] == 7) ? w[i] : 0.f;
        const int f = pickW (cw, 8, r.next());
        const int i0 = f >= 0 ? f : pickW (w, 8, r.next());
        if (i0 >= 0)
        {
            out[n++] = kDepIv[i0];
            w[i0] = 0.f;
        }
    }
    while (n < count)
    {
        const int i = pickW (w, 8, r.next());
        if (i < 0)
            break;
        out[n++] = kDepIv[i];
        w[i] = 0.f;
    }
    return n;
}

// Pitches for a cell's attacks in time order around `anchor`: beat 1 on the anchor; then the riff
// departs and stays away for runs (a departure note often repeats: "b3 b3", "5 5 5"), returning to
// the anchor at most every other note; b6 only next to the 5th; the 2nd and b7 only next to the
// anchor; the last attack is a pickup off the anchor that leads back.
inline void drawPitches (Cell& c, int anchor, const int* dep, int nDep, Rng& r) noexcept
{
    int idx[16];
    int n = 0;
    for (int k = 0; k < 16; ++k)
        if (c.on[k])
            idx[n++] = k;
    int prev = anchor;
    bool prevAnchor = true;
    int run = 0; // consecutive notes off the anchor
    for (int i = 0; i < n; ++i)
    {
        const int k = idx[i];
        int p = anchor;
        if (i > 0)
        {
            double pOff = kDepartByBeat[k / 4];
            if (! prevAnchor && run < 2)
                pOff = 0.85; // stay away: no ping-pong
            if (i == n - 1)
                pOff = std::max (pOff, 0.75);
            if (r.next() < pOff)
            {
                if (! prevAnchor && r.next() < 0.45)
                    p = prev; // the departure note repeats
                else
                {
                    float w[8];
                    for (int d = 0; d < nDep; ++d)
                    {
                        const int iv = dep[d];
                        float x = 1.f;
                        // rising through the departures in the first half, then settling
                        x *= (k < 8 ? (anchor + iv > prev) : true) ? 1.4f : 1.f;
                        // b6 only as the 5th's neighbour; 2nd / b7 only next to the anchor
                        if (iv == 8 && prev != anchor + 7)
                            x *= 0.15f;
                        if ((iv == 2 || iv == -2) && ! prevAnchor && i != n - 1)
                            x *= 0.2f;
                        if (anchor + iv == prev)
                            x *= 0.6f;
                        w[d] = x;
                    }
                    const int d = pickW (w, nDep, r.next());
                    p = anchor + (d >= 0 ? dep[d] : 7);
                }
            }
        }
        c.semi[k] = p;
        prevAnchor = p == anchor;
        run = prevAnchor ? 0 : run + 1;
        prev = p;
    }
}

inline void drawVelocities (Cell& c, Rng& r) noexcept
{
    for (int k = 0; k < 16; ++k)
        if (c.on[k])
            c.vel[k] = std::clamp (((k & 1) == 0 ? 100 : 90) + (int) (r.next() * 9.0) - 4, 60, 120);
}

// The answer bar: rhythm only, 2-4 slots (one attack drops, one or two move by a 16th).
inline Cell answerOf (const Cell& src, Rng& r) noexcept
{
    Cell a = src;
    int idx[16];
    int n = 0;
    for (int k = 1; k < 16; ++k)
        if (a.on[k])
            idx[n++] = k;
    if (n >= 3)
    {
        const int drop = idx[(int) (r.next() * n)];
        a.on[drop] = false;
    }
    const int moves = 1 + (r.next() < 0.5 ? 1 : 0);
    for (int m = 0; m < moves; ++m)
    {
        int cand[16];
        int nc = 0;
        for (int k = 1; k < 16; ++k)
            if (a.on[k] && ((k + 1 < 16 && ! a.on[k + 1]) || (k - 1 >= 1 && ! a.on[k - 1])))
                cand[nc++] = k;
        if (nc == 0)
            break;
        const int k = cand[(int) (r.next() * nc)];
        const int to = (k + 1 < 16 && ! a.on[k + 1] && (r.next() < 0.5 || k - 1 < 1 || a.on[k - 1])) ? k + 1 : k - 1;
        if (to < 1 || to > 15 || a.on[to])
            continue;
        a.on[to] = true;
        a.semi[to] = a.semi[k];
        a.vel[to] = a.vel[k];
        a.on[k] = false;
    }
    return a;
}

// Lays the bars' cells into the lane with lengths to the next attack x the shape's gate.
inline void layOut (Lane& lane, const Cell* bars, int nb, float gate, Rng& r) noexcept
{
    lane.count = 0;
    float starts[kMaxNotes];
    for (int b = 0; b < nb; ++b)
        for (int k = 0; k < 16 && lane.count < kMaxNotes; ++k)
        {
            if (! bars[b].on[k])
                continue;
            Note n;
            n.start = (float) (b * 16 + k);
            n.chord[0] = (std::int8_t) bars[b].semi[k];
            n.nChord = 1;
            n.vel = (std::uint8_t) bars[b].vel[k];
            starts[lane.count] = n.start;
            lane.notes[lane.count++] = n;
        }
    const float loop = (float) (nb * 16);
    for (int i = 0; i < lane.count; ++i)
    {
        const float next = i + 1 < lane.count ? starts[i + 1] : loop + starts[0];
        const float gap = next - starts[i];
        float g = gate;
        if (gate < 0.99f && r.next() < 0.20)
            g = 0.5f; // a few short notes give the line its bounce
        float len = std::max (0.5f, std::floor (gap * g * 4.f + 0.5f) / 4.f);
        len = std::min (len, gap);
        lane.notes[i].length = std::min (len, loop - starts[i]);
    }
}

// Designs and writes the riff for `bars` bars (8 or fewer). Returns the shape drawn.
inline int compose (Lane& lane, int dialect, int mode, int bars, std::uint32_t seed) noexcept
{
    Rng r (seed ^ 0x52494646u);
    const int st = std::clamp (dialect, 0, 3);
    const int nb = std::clamp (bars, 1, kMaxBars);
    const int shape = std::max (0, pickW (kShapeW[st], kShapes, r.next()));
    const int lo = kAttacksLo[shape], hi = kAttacksHi[shape];
    auto attacks = [&] () { return lo + (int) (r.next() * (double) (hi - lo + 1)); };
    const int maxRun = shape == Rolling ? 4 : 3;
    const int maxKick = shape == Rolling ? 2 : 1;
    const bool organ = r.next() < (double) kOrganP[shape];
    int dep[8];
    const int nDep = drawDepartures (0, mode, organ, shape == Rolling ? 3 : (r.next() < 0.5 ? 2 : 3), r, dep);

    Cell x;
    drawRhythm (x, r, attacks(), maxRun, maxKick);
    drawPitches (x, 0, dep, nDep, r);
    drawVelocities (x, r);
    Cell y = x;
    if (shape == CallResponse || shape == Held)
    {
        // Bar B: the same head (front half) with a new tail, or the riff moves to another anchor
        // (pack loops 48%, refs 59%, his own 73%): b7 below, 4, b6 below, b3, 5 below.
        bool keep[16] {};
        for (int k = 0; k < 8; ++k)
            keep[k] = x.on[k];
        drawRhythm (y, r, attacks(), maxRun, maxKick, keep);
        int anchorB = 0;
        if (r.next() < 0.5)
        {
            static constexpr int kMoveIv[5] = { -2, 5, -4, 3, -5 };
            float mw[5] = { 0.24f, 0.18f, 0.16f, 0.14f, 0.12f };
            for (int m = 0; m < 5; ++m)
                if (! phrase::inMode (kMoveIv[m], mode))
                    mw[m] = 0.f;
            const int m = pickW (mw, 5, r.next());
            anchorB = m >= 0 ? kMoveIv[m] : 0;
        }
        int depB[8];
        const int nDepB = anchorB == 0 ? nDep : drawDepartures (anchorB, mode, organ, nDep, r, depB);
        drawPitches (y, anchorB, anchorB == 0 ? dep : depB, nDepB, r);
        if (anchorB == 0)
            for (int k = 0; k < 8; ++k)
                if (y.on[k])
                    y.semi[k] = x.semi[k];
        else
        {
            // A moved bar leads back: its last attack in the last beat returns to the tonic.
            for (int k = 15; k >= 12; --k)
                if (y.on[k])
                {
                    if (r.next() < 0.6)
                        y.semi[k] = 0;
                    break;
                }
        }
        drawVelocities (y, r);
    }
    else if (shape == Rolling)
    {
        // Rolling lines move a little: bar 2 changes one or two departure notes.
        const int changes = 1 + (r.next() < 0.5 ? 1 : 0);
        for (int c = 0; c < changes; ++c)
        {
            int idx[16];
            int n = 0;
            for (int k = 1; k < 16; ++k)
                if (y.on[k] && y.semi[k] != 0)
                    idx[n++] = k;
            if (n == 0)
                break;
            const int k = idx[(int) (r.next() * n)];
            y.semi[k] = dep[(int) (r.next() * nDep)];
        }
    }

    Cell barsOut[kMaxBars];
    const bool twoBar = shape != OneBar;
    for (int b = 0; b < nb; ++b)
        barsOut[b] = (twoBar && (b & 1) == 1) ? y : x;
    if (nb >= 4)
        barsOut[nb - 1] = answerOf (barsOut[nb - 1], r);
    layOut (lane, barsOut, nb, kGate[shape], r);
    return shape;
}
} // namespace riff
} // namespace rolla
