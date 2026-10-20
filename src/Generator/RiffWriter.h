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
// Attacks per bar by shape. Round 4 (Oct 20, Melak): "alot of your basslines are crowded ... empty
// spaces work well if the drums work rhythmically": fewer attacks than the loops' median (6), with
// a real rest in every bar (rolling stays busier for ROLLER).
inline constexpr int kAttacksLo[kShapes] = { 3, 3, 5, 2 };
inline constexpr int kAttacksHi[kShapes] = { 4, 4, 6, 3 };
// The longest silence inside a bar (16ths, to the next attack or the next bar's beat 1).
inline constexpr int kMinRest[kShapes] = { 3, 3, 2, 3 };
// Onset weight per 16th: the 229 Artist Series loops pooled, the e's (1, 5, 9, 13) x1.5 toward his
// favourite tracks (28% of their onsets on the e against 13% in the loops).
inline constexpr float kStepW[16] = { 0.73f, 0.26f, 0.32f, 0.34f, 0.36f, 0.30f, 0.58f, 0.33f,
                                      0.33f, 0.38f, 0.48f, 0.31f, 0.42f, 0.32f, 0.63f, 0.15f };
// Gate (length against the gap to the next onset) by shape; held lines tie.
inline constexpr float kGate[kShapes] = { 0.60f, 0.62f, 0.70f, 0.90f };
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

inline int longestRest (const bool* on) noexcept
{
    int best = 0;
    int last = -1;
    for (int k = 0; k < 16; ++k)
        if (on[k])
        {
            if (last >= 0)
                best = std::max (best, k - last - 1);
            last = k;
        }
    return last < 0 ? 16 : std::max (best, 16 - last - 1);
}

inline void drawRhythmOnce (Cell& c, Rng& r, int want, int maxRun, int maxKick, const bool* keep) noexcept;

// A rhythm cell: beat 1 always; the rest by the step weights, at most `maxRun` 16ths in a row, at
// most `maxKick` more attacks on the kick steps (the bass sits mostly off the kick), and a silence of
// at least `minRest` 16ths somewhere in the bar (the drums carry the groove there).
inline void drawRhythm (Cell& c, Rng& r, int want, int maxRun, int maxKick, int minRest, const bool* keep = nullptr) noexcept
{
    for (int t = 0; t < 32; ++t)
    {
        drawRhythmOnce (c, r, want, maxRun, maxKick, keep);
        if (longestRest (c.on) >= minRest)
            return;
    }
}

inline void drawRhythmOnce (Cell& c, Rng& r, int want, int maxRun, int maxKick, const bool* keep) noexcept
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

// P(a note is off the anchor) by beat (pack loops: the riff states the anchor on beat 1 and moves in
// the second half), scaled toward his picks (anchor share ~.3).
inline constexpr float kDepartByBeat[4] = { 0.30f, 0.52f, 0.64f, 0.86f };

// Notes. Round 3 (Oct 20): the good riff was the root, b3 and 5 ("1 b3 b3 5 5 5 1"), the near one
// the root, 5 and b6 beside the 5th; the rejects ping-ponged root / 2 / b7 / 4. Round 4: "starting
// on the chord note is important ... if we build on top of it, we have a chord structure to work
// with". So a bar states its chord root (the anchor) on beat 1 and departs to that chord's own tones
// (its 3rd and 5th in the mode, the octave for organ riffs); the one passing note is the pickup that
// steps into the next bar's root.
struct Chord
{
    int root = 0;  // semitones from the key root
    int third = 3; // above the root
    int fifth = 7;
};

inline Chord chordOn (int step, int mode, int near) noexcept
{
    Chord c;
    int root = phrase::degreeToSemis (step, mode);
    while (root - near > 6)
        root -= 12;
    while (near - root > 6)
        root += 12;
    c.root = root;
    const int base = phrase::degreeToSemis (step, mode);
    c.third = phrase::degreeToSemis (step + 2, mode) - base;
    c.fifth = phrase::degreeToSemis (step + 4, mode) - base;
    return c;
}

// Pitches for a cell's attacks in time order: beat 1 the chord root; then runs on the chord's 3rd /
// 5th (/ octave), the departure note often repeating ("b3 b3", "5 5 5"), never root-x-root ping-pong;
// the last attack, when it sits in the last beat, leads into `nextRoot` by a step or from its 5th.
inline void drawPitches (Cell& c, const Chord& ch, bool organ, int nextRoot, int mode, Rng& r) noexcept
{
    int idx[16];
    int n = 0;
    for (int k = 0; k < 16; ++k)
        if (c.on[k])
            idx[n++] = k;
    const int anchor = ch.root;
    int prev = anchor;
    bool prevAnchor = true;
    int run = 0;
    for (int i = 0; i < n; ++i)
    {
        const int k = idx[i];
        int p = anchor;
        const bool pickup = i == n - 1 && i > 0 && k >= 13;
        if (pickup)
        {
            // Into the next bar's root: a step above or below it (in the mode), its 5th, or a chord tone.
            int cand[4];
            float w[4];
            int nc = 0;
            for (int d : { 2, -1, -2 })
                if (phrase::inMode (nextRoot + d, mode) && nc < 4)
                {
                    cand[nc] = nextRoot + d;
                    w[nc++] = d == 2 ? 0.25f : 0.15f;
                }
            cand[nc] = nextRoot + 7 > anchor + 12 ? nextRoot - 5 : nextRoot + 7;
            w[nc++] = 0.20f;
            const int pk = pickW (w, nc, r.next());
            p = (r.next() < 0.3 || pk < 0) ? anchor + ch.fifth : cand[pk];
        }
        else if (i > 0)
        {
            double pOff = kDepartByBeat[k / 4];
            if (! prevAnchor && run < 2)
                pOff = 0.85; // stay away: no ping-pong
            if (r.next() < pOff)
            {
                if (! prevAnchor && r.next() < 0.45)
                    p = prev; // the departure note repeats
                else
                {
                    int cand[3] = { anchor + ch.third, anchor + ch.fifth, anchor + 12 };
                    float w[3] = { 1.f, 1.f, organ ? 0.8f : 0.f };
                    // rising through the chord in the first half
                    for (int j = 0; j < 3; ++j)
                    {
                        if (k < 8 && cand[j] > prev)
                            w[j] *= 1.4f;
                        if (cand[j] == prev)
                            w[j] *= 0.5f;
                    }
                    const int pk = pickW (w, 3, r.next());
                    p = cand[pk < 0 ? 1 : pk];
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
    const int minRest = kMinRest[shape];
    const bool organ = r.next() < (double) kOrganP[shape];
    const Chord home = chordOn (0, mode, 0);

    // Bar B's chord: the same (same head, new tail) or a move to another chord root that chords can
    // be built on (pack loops 48%, refs 59%, his own 73%): bVII, iv, bVI, bIII, v; never a
    // diminished triad.
    const bool twoBar = shape != OneBar;
    Chord chB = home;
    bool moved = false;
    if ((shape == CallResponse || shape == Held) && r.next() < 0.5)
    {
        static constexpr int kMoveStep[5] = { 6, 3, 5, 2, 4 };
        float mw[5] = { 0.24f, 0.18f, 0.16f, 0.14f, 0.12f };
        for (int m = 0; m < 5; ++m)
        {
            const Chord t = chordOn (kMoveStep[m], mode, 0);
            if (t.fifth != 7)
                mw[m] = 0.f; // diminished or augmented: no
        }
        const int m = pickW (mw, 5, r.next());
        if (m >= 0)
        {
            chB = chordOn (kMoveStep[m], mode, 0);
            moved = true;
        }
    }

    Cell x;
    drawRhythm (x, r, attacks(), maxRun, maxKick, minRest);
    drawPitches (x, home, organ, twoBar ? chB.root : home.root, mode, r);
    drawVelocities (x, r);
    Cell y = x;
    if (shape == CallResponse || shape == Held)
    {
        bool keep[16] {};
        for (int k = 0; k < 8; ++k)
            keep[k] = x.on[k];
        drawRhythm (y, r, attacks(), maxRun, maxKick, minRest, keep);
        drawPitches (y, chB, organ, home.root, mode, r);
        if (! moved)
        {
            for (int k = 0; k < 8; ++k)
                if (y.on[k])
                    y.semi[k] = x.semi[k];
        }
        drawVelocities (y, r);
    }
    else if (shape == Rolling)
    {
        // Rolling lines move a little: bar 2 swaps one or two chord tones.
        const int changes = 1 + (r.next() < 0.5 ? 1 : 0);
        for (int c = 0; c < changes; ++c)
        {
            int idx[16];
            int n = 0;
            for (int k = 1; k < 13; ++k)
                if (y.on[k] && y.semi[k] != home.root)
                    idx[n++] = k;
            if (n == 0)
                break;
            const int k = idx[(int) (r.next() * n)];
            y.semi[k] = y.semi[k] == home.root + home.third ? home.root + home.fifth : home.root + home.third;
        }
    }

    Cell barsOut[kMaxBars];
    for (int b = 0; b < nb; ++b)
        barsOut[b] = (twoBar && (b & 1) == 1) ? y : x;
    if (nb >= 4)
        barsOut[nb - 1] = answerOf (barsOut[nb - 1], r);
    layOut (lane, barsOut, nb, kGate[shape], r);
    return shape;
}
} // namespace riff
} // namespace rolla
