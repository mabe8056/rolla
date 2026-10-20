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
// Extra notes by scale step (2nd, 3rd, 4th, 5th, 6th, 7th): the minor-pentatonic-like sets of his
// favourites ({1, b3, 4, b7}, {1, b3, b7}, {1, 4, 5, b7}) over the loops' degree shares.
inline constexpr float kStepDegreeW[7] = { 0.f, 0.06f, 0.20f, 0.15f, 0.18f, 0.08f, 0.22f };
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

// The riff's pitch set: the root plus `extra` scale steps drawn by kStepDegreeW (in the mode).
inline int drawSet (int mode, int extra, Rng& r, int* semis) noexcept
{
    int n = 0;
    semis[n++] = 0;
    float w[7];
    for (int s = 0; s < 7; ++s)
        w[s] = kStepDegreeW[s];
    // Phrygian's b2 is its colour; elsewhere the 2nd stays rare.
    if (mode == phrase::Phrygian)
        w[1] *= 2.f;
    for (int e = 0; e < extra; ++e)
    {
        const int s = pickW (w, 7, r.next());
        if (s <= 0)
            break;
        semis[n++] = phrase::degreeToSemis (s, mode);
        w[s] = 0.f;
    }
    return n;
}

// Pitches for a cell's attacks in time order: beat 1 the root; then repeat (p .30), an octave move
// when the riff is organ-style, or a set note near the last one along an arch (up, then home).
inline void drawPitches (Cell& c, const int* set, int nSet, bool organ, Rng& r) noexcept
{
    int prev = 0;
    int idx = 0;
    int count = 0;
    for (bool b : c.on)
        count += b ? 1 : 0;
    for (int k = 0; k < 16; ++k)
    {
        if (! c.on[k])
            continue;
        int p = 0;
        if (idx == 0)
            p = 0;
        else if (r.next() < 0.30)
            p = prev;
        else if (organ && r.next() < 0.40 && (prev == 0 || prev == 12))
            p = prev == 0 ? 12 : 0;
        else
        {
            const bool rising = idx < (count + 1) / 2;
            float w[12];
            int cand[12];
            int nc = 0;
            for (int i = 0; i < nSet; ++i)
                for (int o = -12; o <= 12; o += 12)
                {
                    const int v = set[i] + o;
                    if (v < -5 || v > 15 || v == prev)
                        continue;
                    const int d = v - prev;
                    float x = 1.f;
                    const int ad = std::abs (d);
                    x *= ad <= 2 ? 2.f : (ad <= 5 ? 1.3f : (ad == 12 ? (organ ? 1.2f : 0.3f) : 0.6f));
                    x *= (rising ? d > 0 : d < 0) ? 1.4f : 1.f;
                    x *= v == 0 ? 1.5f : 1.f; // the root anchors (~40% of notes)
                    if (nc < 12)
                    {
                        cand[nc] = v;
                        w[nc++] = x;
                    }
                }
            const int pick = pickW (w, nc, r.next());
            p = pick < 0 ? 0 : cand[pick];
        }
        // The last attack leads home: a root, 5th, b7 or the octave.
        if (idx == count - 1 && count > 2)
        {
            const int pc = ((p % 12) + 12) % 12;
            bool leads = pc == 0;
            for (int i = 0; i < nSet && ! leads; ++i)
                if (pc == ((set[i] % 12) + 12) % 12 && (set[i] == 7 || set[i] == 10 || set[i] == 9))
                    leads = true;
            if (! leads)
                p = prev;
        }
        c.semi[k] = p;
        prev = p;
        ++idx;
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
    int set[8];
    const int extra = shape == Rolling ? (r.next() < 0.5 ? 3 : 4) : (shape == Held ? 2 : (r.next() < 0.55 ? 2 : 3));
    const int nSet = drawSet (mode, extra, r, set);

    Cell x;
    drawRhythm (x, r, attacks(), maxRun, maxKick);
    drawPitches (x, set, nSet, organ, r);
    drawVelocities (x, r);
    Cell y = x;
    if (shape == CallResponse)
    {
        // The response keeps the call's front half and answers in the back half.
        bool keep[16] {};
        for (int k = 0; k < 8; ++k)
            keep[k] = x.on[k];
        drawRhythm (y, r, attacks(), maxRun, maxKick, keep);
        drawPitches (y, set, nSet, organ, r);
        for (int k = 0; k < 8; ++k)
            if (y.on[k])
                y.semi[k] = x.semi[k];
        drawVelocities (y, r);
    }
    else if (shape == Rolling)
    {
        // Rolling lines move a little: bar 2 changes one or two notes to a neighbour in the set.
        const int changes = 1 + (r.next() < 0.5 ? 1 : 0);
        for (int c = 0; c < changes; ++c)
        {
            int idx[16];
            int n = 0;
            for (int k = 1; k < 16; ++k)
                if (y.on[k])
                    idx[n++] = k;
            if (n == 0)
                break;
            const int k = idx[(int) (r.next() * n)];
            const int s = set[(int) (r.next() * nSet)];
            y.semi[k] = s;
        }
    }
    else if (shape == Held)
    {
        drawRhythm (y, r, attacks(), maxRun, maxKick);
        drawPitches (y, set, nSet, organ, r);
        drawVelocities (y, r);
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
