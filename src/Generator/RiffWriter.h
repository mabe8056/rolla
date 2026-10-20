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
    GhostClimb,
    Groove,
    kShapes
};

inline constexpr const char* kShapeNames[kShapes] = { "riff_1bar", "riff_call_response", "riff_rolling", "riff_held", "riff_ghost_climb", "riff_groove" };

// Shape weights per style (ROLLER, DEEP, BOUNCY, JACKIN): his favourites are 1-bar riffs and call /
// response first, rolling 16ths for the roller sound, held lines for organ / deep.
// GhostClimb (Oct 20, "catchy and hypnotic grooves ... disco inspired"; lessons from the LIGHTLEAK
// "Mambos" bass split he sent, learn only) takes half of every style's draws.
// Groove (the verified stem splits: Mambos, Tonight, West Side Factory, House Party) leads.
inline constexpr float kShapeW[4][kShapes] = {
    { 0.06f, 0.05f, 0.12f, 0.02f, 0.15f, 0.60f },
    { 0.08f, 0.07f, 0.03f, 0.05f, 0.17f, 0.60f },
    { 0.10f, 0.07f, 0.05f, 0.03f, 0.15f, 0.60f },
    { 0.08f, 0.08f, 0.02f, 0.05f, 0.17f, 0.60f },
};
// Attacks per bar by shape. Round 4 (Oct 20, Melak): "alot of your basslines are crowded ... empty
// spaces work well if the drums work rhythmically": fewer attacks than the loops' median (6), with
// a real rest in every bar (rolling stays busier for ROLLER).
inline constexpr int kAttacksLo[kShapes] = { 3, 3, 5, 2, 4, 5 };
inline constexpr int kAttacksHi[kShapes] = { 4, 4, 6, 3, 5, 6 };
// The longest silence inside a bar (16ths, to the next attack or the next bar's beat 1).
inline constexpr int kMinRest[kShapes] = { 3, 3, 2, 3, 1, 1 };
// Onset weight per 16th: the 229 Artist Series loops pooled, the e's (1, 5, 9, 13) x1.5 toward his
// favourite tracks (28% of their onsets on the e against 13% in the loops).
inline constexpr float kStepW[16] = { 0.73f, 0.26f, 0.32f, 0.34f, 0.36f, 0.30f, 0.58f, 0.33f,
                                      0.33f, 0.38f, 0.48f, 0.31f, 0.42f, 0.32f, 0.63f, 0.15f };
// Gate (length against the gap to the next onset) by shape; held lines tie.
inline constexpr float kGate[kShapes] = { 0.60f, 0.62f, 0.70f, 0.90f, 0.85f, 0.85f };
// Organ-style octave bounce (Get Busy, House Party, Josh Baker's organ; 33 of 229 loops).
inline constexpr float kOrganP[kShapes] = { 0.30f, 0.30f, 0.15f, 0.50f, 0.0f, 0.0f };

struct Cell
{
    bool on[16] {};
    int semi[16] {};
    int vel[16] {};
    bool ghost[16] {}; // GhostClimb's quiet upper-octave notes (staccato)
    float len[16] {};  // Groove: explicit length in 16ths (0: from the gap)
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

// ------------------------------------------------------------------ GhostClimb
// What the Mambos split taught (structure only; the riff itself is in the private anti-copy index,
// never here): a one-bar figure repeated through the drop; ~5 short deep MAIN notes that climb
// through the minor pentatonic / blues scale from the root on beat 1 (one chromatic blue note at
// most) and never return to the root inside the bar; quiet GHOST notes an octave above the previous
// main note in the gaps (the disco 16th motion without crowding the low end); no note on the 16th
// right before beat 2 or beat 1 (the kick lands in space); the strongest notes between the kicks;
// ghosts accented every 4th bar; a fill note at the phrase end.
inline constexpr float kGcMainW[16] = { 0.f, 0.f, 0.25f, 0.f, 0.55f, 0.20f, 0.25f, 0.65f, 0.15f, 0.20f, 0.70f, 0.25f, 0.50f, 0.20f, 0.25f, 0.f };

// The main notes' path, in semitones from the root, for `n` notes (the first is the root).
inline int gcPath (int n, int mode, Rng& r, int* out) noexcept
{
    // Path families (weights): climb and settle .40, octave climb .25, dip below then climb .20,
    // fifth bounce .15. All on the minor pentatonic (in every minor mode); the b5 blue note only as a
    // chromatic step between the 4th and 5th.
    const float fw[4] = { 0.40f, 0.25f, 0.20f, 0.15f };
    const int fam = std::max (0, pickW (fw, 4, r.next()));
    static constexpr int kPenta[6] = { 0, 3, 5, 7, 10, 12 };
    int m = 0;
    out[m++] = 0;
    if (fam == 0)
    {
        // up the pentatonic one or two steps at a time, then settle one step down
        int i = 0;
        while (m < n - 1)
        {
            i = std::min (5, i + (r.next() < 0.75 ? 1 : 2));
            out[m++] = kPenta[i];
        }
        if (m < n)
            out[m++] = kPenta[std::max (1, i - 1)];
    }
    else if (fam == 1)
    {
        static constexpr int kUp[5] = { 7, 10, 12, 10, 7 };
        for (int k = 0; m < n; ++k)
            out[m++] = kUp[std::min (4, k)];
    }
    else if (fam == 2)
    {
        out[m++] = -2;
        static constexpr int kUp[4] = { 0, 3, 5, 3 };
        for (int k = 0; m < n; ++k)
            out[m++] = kUp[std::min (3, k)];
    }
    else
    {
        static constexpr int kUp[4] = { 3, 7, 5, 3 };
        for (int k = 0; m < n; ++k)
            out[m++] = kUp[std::min (3, k)];
    }
    // The blue note: a 5 -> 7 step may pass through the b5 (p .5).
    for (int k = 1; k + 1 < m && r.next() < 0.5; ++k)
        if (out[k - 1] == 5 && out[k] == 7)
        {
            out[k] = 6;
            break;
        }
    // Every note but the blue one stays in the mode (the b7 below, the b3 ... are in every minor mode).
    for (int k = 0; k < m; ++k)
        if (out[k] != 6 && ! phrase::inMode (out[k], mode))
            out[k] = out[k] > 0 ? out[k] - 1 : out[k] + 1;
    return m;
}

inline Cell ghostClimbCell (int mode, Rng& r) noexcept
{
    Cell c;
    for (int tries = 0; tries < 16; ++tries)
    {
        c = Cell {};
        c.on[0] = true;
        const int want = 4 + (r.next() < 0.5 ? 1 : 0);
        int n = 1;
        float w[16];
        for (int k = 0; k < 16; ++k)
            w[k] = kGcMainW[k];
        while (n < want)
        {
            float ww[16];
            for (int k = 0; k < 16; ++k)
            {
                // mains at least two 16ths apart, so a ghost fits between them
                bool near = false;
                for (int j = std::max (0, k - 1); j <= std::min (15, k + 1); ++j)
                    near = near || c.on[j];
                // a gap of three 16ths or more leaves room for a ghost (Mambos: 4, 3, 3, 2, 4)
                bool close = false;
                for (int j = std::max (0, k - 2); j <= std::min (15, k + 2); ++j)
                    close = close || c.on[j];
                ww[k] = near ? 0.f : (close ? 0.35f * w[k] : w[k]);
            }
            const int k = pickW (ww, 16, r.next());
            if (k < 0)
                break;
            c.on[k] = true;
            ++n;
        }
        int slots[16];
        int ns = 0;
        for (int k = 0; k < 16; ++k)
            if (c.on[k])
                slots[ns++] = k;
        int path[16];
        gcPath (ns, mode, r, path);
        // Not the Mambos cell (root, b3, 4, b5, 4 on 0, 4, 7, 10, 12): the anti-copy index is the
        // backstop, this keeps the writer from even proposing it.
        static constexpr int kRefSlots[5] = { 0, 4, 7, 10, 12 };
        static constexpr int kRefPath[5] = { 0, 3, 5, 6, 5 };
        bool same = ns == 5;
        for (int i = 0; i < ns && same; ++i)
            same = slots[i] == kRefSlots[i] && path[i] == kRefPath[i];
        if (same)
            continue;
        for (int i = 0; i < ns; ++i)
        {
            const int k = slots[i];
            c.semi[k] = path[i];
            c.vel[k] = k == 0 ? 110 : ((k & 1) == 1 || (k & 3) == 2 ? 104 : 98);
        }
        // Ghosts: the octave of the previous main, two 16ths after it, if that slot is free, not a
        // main's neighbour and not the 16th before beat 2 or beat 1 (p .75 each).
        for (int i = 0; i < ns; ++i)
        {
            const int g = slots[i] + 2;
            if (g > 15 || g == 3 || g == 15 || c.on[g])
                continue;
            if (r.next() < 0.85)
            {
                c.on[g] = true;
                c.ghost[g] = true;
                c.semi[g] = path[i] + 12;
                c.vel[g] = 52 + (int) (r.next() * 8.0);
            }
        }
        c.n = 0;
        for (bool b : c.on)
            c.n += b ? 1 : 0;
        return c;
    }
    return c;
}

// Lengths: a main note holds up to two 16ths (to the next note x gate), a ghost is a staccato 16th.
inline void layOutGhost (Lane& lane, const Cell* bars, int nb) noexcept
{
    lane.count = 0;
    for (int b = 0; b < nb; ++b)
        for (int k = 0; k < 16 && lane.count < kMaxNotes; ++k)
        {
            if (! bars[b].on[k])
                continue;
            int next = 16;
            for (int j = k + 1; j < 16; ++j)
                if (bars[b].on[j])
                {
                    next = j;
                    break;
                }
            const float gap = (float) (next - k);
            Note n;
            n.start = (float) (b * 16 + k);
            n.chord[0] = (std::int8_t) bars[b].semi[k];
            n.nChord = 1;
            n.vel = (std::uint8_t) std::clamp (bars[b].vel[k], 1, 127);
            n.length = bars[b].ghost[k] ? std::min (0.6f, gap) : std::min ({ 2.f, gap * 0.85f, gap });
            n.length = std::max (0.25f, std::floor (n.length * 4.f + 0.5f) / 4.f);
            lane.notes[lane.count++] = n;
        }
}

// ------------------------------------------------------------------ Groove
// Pooled from the four verified stem splits Melak sent (Oct 20; learn only, each riff sits in a
// private anti-copy index): a short cell looped exactly (1 bar .5, 2 bars where bar 2 changes only
// its first onset .25, 4 bars call / answer / call / answer-with-turnaround .25); the root plus 2-3 of
// b3 (.75), 4 (.75), 5 (.5), b7 (.5), never 2 / 6 / b6 / 3, one b5 at most and only between two 4s;
// range 5-7 semitones; 5-6 main attacks a bar, 1-2 16ths long, one anchor of 3-6 on the beat-1 / 2
// kick at most; the first half anchors on the root, the second half carries the hook; the loudest
// note between kicks on the a of 2 or the a of 3; nothing bright on a kick or a clap (a note there
// is a ducked swell); a rest on or right before a beat; the last note leads home (+2 from the b7
// below, or -5 from the 4); optional quiet octave ghosts. Four contour families:
//   Climb          root, then one attack per pitch up the set (b5 between two 4s), ends on 4 / b3
//   RiseDropBelow  root x2, b3, 4 x2, then the b7 below the root, which steps up into bar 1
//   HookRepeat     long root anchor, down a 4th to the 5, then a hook pitch repeated three times
//   PedalCall      call bar: the root alone in rhythm; answer bar: 5 then b3 repeated, no root;
//                  every 4th bar's last cell turns to the 4 to lead back (period 4)
// Three more from the last verified splits (Keep on Dancing, Can You Feel The Bass, All Day):
//   CallRootAnswer  call bar climbs b3 (repeated) to b6 / 5 and never touches the root, a pickup
//                   leads on; the answer bar falls to the root, repeated to the bar line (period 2)
//   NeighbourLoop   a 3-5 semitone loop: starts on the b3 just after beat 1, sits on the root, a
//                   2nd (or the b7 below) at the end leads back up across the bar line
//   OctaveDisco     low notes on the kicks, the same note an octave up in between, the second half
//                   slides down (1 -> maj7 -> b7, or 1 -> b7 -> b6 / 5) and climbs home
// The 2nd, b6 and one chromatic note appear here only as passing / leading notes (3 of 7 splits).
enum GrooveContour : int { Climb = 0, RiseDropBelow, HookRepeat, PedalCall, CallRootAnswer, NeighbourLoop, OctaveDisco, kContours };
// Round 7 (Oct 20): Melak called 4, 7, 8, 9, 10 "great riff" and 3, 5 "good"; five of those seven
// were PedalCall (a rhythmic root call, a two-note answer stepping down: 4 -> b3, 5 -> 4), one
// RiseDropBelow; the "eh" / "too chaotic" ones carried the most ghosts. PedalCall leads now.
// Round 11 (Oct 21): with PedalCall at .40 and riffs ranked by the old penalty, 9 of 10 were the same
// riff ("all the basslines were the same lol"): PedalCall back to about a quarter, RiseDropBelow (the
// other round-7 favourite) up, and PedalCall's call and answer now vary (below).
inline constexpr float kContourW[4][kContours] = {
    { 0.10f, 0.20f, 0.10f, 0.24f, 0.12f, 0.12f, 0.12f }, // ROLLER
    { 0.08f, 0.22f, 0.10f, 0.26f, 0.14f, 0.10f, 0.10f }, // DEEP
    { 0.10f, 0.18f, 0.12f, 0.24f, 0.12f, 0.10f, 0.14f }, // BOUNCY
    { 0.10f, 0.18f, 0.12f, 0.22f, 0.12f, 0.10f, 0.16f }, // JACKIN
};

struct GrooveHit
{
    int slot;
    int semi;
    float len;
    int vel;
};

inline int gVel (int slot, bool accent) noexcept
{
    if (accent)
        return 116;
    if ((slot & 3) == 0)
        return 76; // on a kick: a ducked swell, not a bright pluck
    return 100;
}

inline void putHits (Cell& c, const GrooveHit* h, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const int k = std::clamp (h[i].slot, 0, 15);
        c.on[k] = true;
        c.semi[k] = h[i].semi;
        c.len[k] = h[i].len;
        c.vel[k] = std::clamp (h[i].vel, 1, 127);
    }
    c.n = 0;
    for (bool b : c.on)
        c.n += b ? 1 : 0;
}

// The bar's rhythm, drawn fresh from the splits' rules (no slot pattern of any reference is
// stored): an anchor on beat 1 (or the e of 1); 0-2 more attacks in the first half, outside the
// anchor; 2-4 hook attacks in the second half on e / & / a slots (the clap 16th only as a swell);
// 5-6 attacks in all; never two attacks on adjacent 16ths except a run into the bar line; at least
// one of the 16ths right before a beat (3, 7, 11, 15) left empty.
struct GrooveRhythm
{
    int slot[8];
    int n = 0;
    float anchorLen = 1.f;
};

inline GrooveRhythm drawGrooveRhythm (Rng& r, bool longAnchor, bool restDownbeat) noexcept
{
    GrooveRhythm g;
    for (int t = 0; t < 48; ++t)
    {
        bool on[16] {};
        const int a0 = restDownbeat ? 1 + (int) (r.next() * 3.0) : (r.next() < 0.75 ? 0 : 1);
        on[a0] = true;
        const float alen = longAnchor ? 3.f + (float) (int) (r.next() * 4.0) : (r.next() < 0.5 ? 1.f : 2.f);
        const int busyUntil = a0 + (int) alen; // the anchor still sounds
        const int want = 5 + (r.next() < 0.5 ? 1 : 0);
        static constexpr int kFront[6] = { 2, 3, 5, 6, 7, 4 };
        float fw[6] = { 0.5f, 0.35f, 0.35f, 0.5f, 0.8f, 0.1f };
        static constexpr int kBack[7] = { 9, 10, 11, 13, 14, 15, 8 };
        float bw[7] = { 0.6f, 0.5f, 0.9f, 0.6f, 0.5f, 0.35f, 0.15f };
        int n = 1;
        const int front = longAnchor ? (r.next() < 0.5 ? 0 : 1) : (r.next() < 0.5 ? 1 : 2);
        for (int i = 0; i < front; ++i)
        {
            float w[6];
            for (int j = 0; j < 6; ++j)
                w[j] = (kFront[j] < busyUntil || on[kFront[j]] || on[kFront[j] - 1] || (kFront[j] < 15 && on[kFront[j] + 1])) ? 0.f : fw[j];
            const int j = pickW (w, 6, r.next());
            if (j < 0)
                break;
            on[kFront[j]] = true;
            ++n;
        }
        while (n < want)
        {
            float w[7];
            for (int j = 0; j < 7; ++j)
            {
                const int k = kBack[j];
                const bool adj = on[k - 1] || (k < 15 && on[k + 1] && k + 1 != 15);
                w[j] = (on[k] || k < busyUntil || (adj && k != 15)) ? 0.f : bw[j];
            }
            const int j = pickW (w, 7, r.next());
            if (j < 0)
                break;
            on[kBack[j]] = true;
            ++n;
        }
        const bool rest = ! on[3] || ! on[7] || ! on[11] || ! on[15];
        if (n < 5 || ! rest)
            continue;
        g.n = 0;
        for (int k = 0; k < 16; ++k)
            if (on[k])
                g.slot[g.n++] = k;
        g.anchorLen = std::min (alen, (float) (g.n > 1 ? g.slot[1] - g.slot[0] : alen));
        return g;
    }
    g.n = 0;
    for (int k : { 0, 6, 9, 11, 14 })
        g.slot[g.n++] = k;
    g.anchorLen = 2.f;
    return g;
}

inline Cell grooveBar (int contour, const int* set, int nSet, bool b5, Rng& r, bool answer, bool turnaround) noexcept
{
    auto has = [&] (int iv) { for (int i = 0; i < nSet; ++i) if (set[i] == iv) return true; return false; };
    const bool longAnchor = contour == HookRepeat || (contour == PedalCall && ! answer);
    const bool restDownbeat = (contour == PedalCall && answer) || contour == NeighbourLoop || (contour == CallRootAnswer && answer);
    GrooveRhythm g = drawGrooveRhythm (r, longAnchor, restDownbeat);
    if (contour == OctaveDisco)
    {
        // kicks 0 / 8 / 12 (low) plus 3-4 sixteenths in between (high), a rest on one 16th before a beat
        bool on[16] {};
        on[0] = true;
        if (r.next() < 0.7) on[8] = true;
        if (r.next() < 0.6) on[12] = true;
        static constexpr int kMid[7] = { 2, 3, 6, 7, 10, 14, 15 };
        float w[7] = { 0.5f, 0.6f, 0.6f, 0.5f, 0.5f, 0.4f, 0.4f };
        int cnt = 0;
        for (bool b : on) cnt += b ? 1 : 0;
        while (cnt < 6)
        {
            const int j = pickW (w, 7, r.next());
            if (j < 0) break;
            on[kMid[j]] = true;
            w[j] = 0.f;
            ++cnt;
        }
        if (on[3] && on[7] && on[11] && on[15])
            on[15] = false;
        g.n = 0;
        for (int k = 0; k < 16; ++k)
            if (on[k])
                g.slot[g.n++] = k;
        g.anchorLen = 1.f;
    }
    int ups[5];
    int nu = 0;
    for (int iv : { 3, 5, 7, 10 })
        if (has (iv))
            ups[nu++] = iv;
    int pitch[8] {};
    const int n = g.n;
    if (contour == Climb)
    {
        // one attack per pitch up the set; the top note settles a step back if attacks remain
        int lvl = 0;
        for (int i = 0; i < n; ++i)
        {
            if (i == 0)
                pitch[i] = 0;
            else if (lvl < nu)
                pitch[i] = ups[lvl++];
            else
                pitch[i] = nu >= 2 ? ups[nu - 2] : 0;
        }
        if (b5 && has (5))
            for (int i = 1; i + 1 < n; ++i)
                if (pitch[i] == 5 && (i + 2 >= n || pitch[i + 2] != 6))
                {
                    pitch[i + 1] = 6; // 4, b5, 4
                    if (i + 2 < n)
                        pitch[i + 2] = 5;
                    break;
                }
    }
    else if (contour == RiseDropBelow)
    {
        // root (often doubled), up through the set (each may double), the last attack to the b7 below
        int lvl = 0;
        pitch[0] = 0;
        for (int i = 1; i < n - 1; ++i)
        {
            const bool dbl = r.next() < 0.45;
            if (! dbl && lvl < std::min (nu, 2))
                ++lvl;
            pitch[i] = lvl == 0 ? 0 : ups[lvl - 1];
        }
        pitch[n - 1] = has (10) || phrase::inMode (10, 1) ? -2 : -5;
    }
    else if (contour == HookRepeat)
    {
        // long root, then down a 4th to the 5 (or the b7 below), then one hook pitch repeated
        const int drop = r.next() < 0.6 ? -5 : -2;
        const int hook = ups[(int) (r.next() * nu)] == 7 ? 10 : ups[(int) (r.next() * nu)];
        pitch[0] = 0;
        if (n > 1)
            pitch[1] = drop;
        for (int i = 2; i < n; ++i)
            pitch[i] = hook > 7 ? hook - 12 : hook;
    }
    else if (contour == CallRootAnswer)
    {
        const int x = has (3) ? 3 : 5;
        const int y = phrase::inMode (8, 1) && r.next() < 0.6 ? 8 : 7; // b6 or 5
        const int z = r.next() < 0.5 ? 7 : 10;                          // the pickup: 5 or b7
        if (! answer)
        {
            const int splitAt = std::max (1, (n * 3) / 5);
            for (int i = 0; i < n; ++i)
                pitch[i] = i < splitAt ? x : y;
            pitch[n - 1] = z;
        }
        else
        {
            const int lead = std::max (1, n / 3);
            for (int i = 0; i < n; ++i)
                pitch[i] = i < lead ? (i == 0 ? z : x) : 0;
        }
    }
    else if (contour == NeighbourLoop)
    {
        const int up = has (3) ? 3 : 2;
        const int back = r.next() < 0.6 ? 2 : -2; // the 2nd leads up to the b3, the b7 below to the root
        for (int i = 0; i < n; ++i)
            pitch[i] = 0;
        pitch[0] = up;
        pitch[n - 1] = back;
        if (n > 3 && r.next() < 0.35)
            pitch[n - 2] = up; // a second touch of the neighbour before the turn
    }
    else if (contour == OctaveDisco)
    {
        // second half slides down to a target and the last note leads home from the b7 below
        const int target = r.next() < 0.5 ? 10 : (r.next() < 0.5 ? 8 : 7);
        int path[3] = { 0, 0, 0 };
        if (target == 10)
        {
            path[0] = r.next() < 0.6 ? -1 : 0; // the chromatic maj7 below the root, or straight on
            path[1] = -2;
            path[2] = -2;
        }
        else
        {
            path[0] = -2;
            path[1] = target - 12;
            path[2] = -2;
        }
        const int half = std::max (1, n / 2);
        for (int i = 0; i < n; ++i)
        {
            const int base = i < half ? 0 : path[std::min (2, i - half)];
            // low on a kick 16th, the octave up in between
            pitch[i] = (g.slot[i] & 3) == 0 ? base : base + 12;
        }
    }
    else // PedalCall
    {
        if (! answer)
        {
            // the call: the root in rhythm; or the root jumping to its octave on an offbeat or two (the
            // Dreams / Mambos lift); or the root with the b7 under it leading back at the end
            for (int i = 0; i < n; ++i)
                pitch[i] = 0;
            const double cv = r.next();
            if (cv < 0.30)
            {
                int jumps = 0;
                for (int i = 1; i < n && jumps < 2; ++i)
                    if ((g.slot[i] & 3) != 0 && r.next() < 0.6)
                    {
                        pitch[i] = 12;
                        ++jumps;
                    }
            }
            else if (cv < 0.50 && n > 2)
                pitch[n - 1] = -2;
        }
        else
        {
            // the answer: a two-note move away from the root. His round-7 favourites step down
            // (4 -> b3, 5 -> 4); more of the same family: b7 -> 5, 4 -> 5 rising, b3 -> 4 rising
            int a = has (7) ? 7 : (has (5) ? 5 : ups[0]);
            int b = has (3) ? 3 : (has (10) ? -2 : ups[0]);
            static constexpr int kPairs[5][2] = { { 5, 3 }, { 7, 5 }, { 10, 7 }, { 5, 7 }, { 3, 5 } };
            static constexpr float kPairW[5] = { 0.30f, 0.25f, 0.17f, 0.14f, 0.14f };
            if (r.next() < 0.90)
            {
                const int pk = std::max (0, pickW (kPairW, 5, r.next()));
                a = kPairs[pk][0];
                b = kPairs[pk][1];
                if (a == 10 && ! phrase::inMode (10, 1))
                    a = 7;
            }
            const int split = std::max (1, n / 2);
            for (int i = 0; i < n; ++i)
                pitch[i] = i < split ? a : b;
            if (turnaround)
                pitch[n - 1] = has (5) ? 5 : 7;
        }
    }
    Cell c;
    GrooveHit h[8];
    int accent = -1;
    for (int i = 0; i < n; ++i)
        if (g.slot[i] == 11 || (accent < 0 && g.slot[i] == 7))
            accent = i;
    for (int i = 0; i < n; ++i)
    {
        const int k = g.slot[i];
        const float len = i == 0 ? g.anchorLen : (r.next() < 0.6 ? 1.f : 2.f);
        int vel = gVel (k, i == accent);
        if (contour == OctaveDisco && (k & 3) == 0)
            vel = 94; // the disco low note on the kick is part of the figure
        h[i] = { k, pitch[i], len, vel };
    }
    putHits (c, h, n);
    return c;
}

// Quiet octave ghosts (p .5 per riff): 1-3 a bar, the previous main's pitch an octave up, in free
// 16ths right after a main, never on a kick or a clap.
inline void addGhosts (Cell& c, Rng& r) noexcept
{
    int prev = -100;
    int placed = 0;
    for (int k = 0; k < 16 && placed < 3; ++k)
    {
        if (c.on[k] && ! c.ghost[k])
        {
            prev = c.semi[k];
            continue;
        }
        if (prev == -100 || c.on[k] || (k & 3) == 0 || k == 15)
            continue;
        if (k > 0 && c.on[k - 1] && ! c.ghost[k - 1] && c.len[k - 1] > 1.01f)
            continue; // a held note still sounds
        if (r.next() < 0.45)
        {
            c.on[k] = true;
            c.ghost[k] = true;
            c.semi[k] = prev + 12;
            c.len[k] = 0.5f;
            c.vel[k] = 48 + (int) (r.next() * 8.0);
            ++placed;
        }
    }
}

inline void layOutGroove (Lane& lane, const Cell* bars, int nb) noexcept
{
    lane.count = 0;
    for (int b = 0; b < nb; ++b)
        for (int k = 0; k < 16 && lane.count < kMaxNotes; ++k)
        {
            if (! bars[b].on[k])
                continue;
            int next = 16 + 0;
            for (int j = k + 1; j < 16; ++j)
                if (bars[b].on[j])
                {
                    next = j;
                    break;
                }
            const float gap = (float) (next - k);
            Note n;
            n.start = (float) (b * 16 + k);
            n.chord[0] = (std::int8_t) bars[b].semi[k];
            n.nChord = 1;
            n.vel = (std::uint8_t) std::clamp (bars[b].vel[k], 1, 127);
            const float want = bars[b].len[k] > 0.f ? bars[b].len[k] : 1.f;
            n.length = std::max (0.5f, std::min (want, gap - (next < 16 ? 0.25f : 0.f)));
            lane.notes[lane.count++] = n;
        }
}

inline int grooveSet (Rng& r, int mode, int* set) noexcept
{
    for (int t = 0; t < 32; ++t)
    {
        int n = 0;
        set[n++] = 0;
        if (r.next() < 0.75 && phrase::inMode (3, mode)) set[n++] = 3;
        if (r.next() < 0.75 && phrase::inMode (5, mode)) set[n++] = 5;
        if (r.next() < 0.50 && phrase::inMode (7, mode)) set[n++] = 7;
        if (r.next() < 0.50 && phrase::inMode (10, mode)) set[n++] = 10;
        if (n >= 3 && n <= 4)
            return n;
    }
    set[0] = 0; set[1] = 3; set[2] = 5;
    return 3;
}

// rolla-rolls only (--bass-midi): a written bass lane played in place of the drawn riff, so a
// listening round can build on riffs Melak already rated. The plugin never sets it.
inline const Lane*& testRiffLane() noexcept
{
    static const Lane* lane = nullptr;
    return lane;
}

// Designs and writes the riff for `bars` bars (8 or fewer). Returns the shape drawn.
inline int compose (Lane& lane, int dialect, int mode, int bars, std::uint32_t seed, int forceShape = -1) noexcept
{
    Rng r (seed ^ 0x52494646u);
    const int st = std::clamp (dialect, 0, 3);
    const int nb = std::clamp (bars, 1, kMaxBars);
    const int drawn = std::max (0, pickW (kShapeW[st], kShapes, r.next()));
    const int shape = forceShape >= 0 && forceShape < kShapes ? forceShape : drawn;
    if (shape == Groove)
    {
        int set[5];
        const int nSet = grooveSet (r, mode, set);
        const int contour = std::max (0, pickW (kContourW[st], kContours, r.next()));
        const bool b5 = r.next() < 0.25;
        const bool ghosts = r.next() < 0.15; // ghosts made riffs busy ("eh", "too chaotic")
        // period: PedalCall is 4 bars; the others 1 bar (.67) or 2 (bar 2 changes its first onset)
        const int period = contour == PedalCall ? 4 : (contour == CallRootAnswer ? 2 : (r.next() < 0.67 ? 1 : 2));
        Cell cells[4];
        for (int i = 0; i < period; ++i)
        {
            const bool answer = (i & 1) == 1;
            const bool turn = period == 4 && i == 3;
            cells[i] = grooveBar (contour, set, nSet, b5, r, answer, turn);
            if (period == 2 && i == 1 && contour != RiseDropBelow)
            {
                // bar 2 differs only in its first onset: the on-beat root becomes a pluck on the e of 1
                cells[1] = cells[0];
                if (cells[1].on[0] && ! cells[1].on[1])
                {
                    cells[1].on[1] = true;
                    cells[1].semi[1] = cells[1].semi[0];
                    cells[1].len[1] = std::max (1.f, cells[1].len[0] - 1.f);
                    cells[1].vel[1] = 100;
                    cells[1].on[0] = false;
                }
            }
        }
        if (ghosts)
        {
            Rng gr (seed ^ 0x47484F53u);
            for (int i = 0; i < period; ++i)
            {
                Rng g2 = gr; // the same ghosts in every cell bar
                addGhosts (cells[i], g2);
            }
        }
        Cell out[kMaxBars];
        for (int b = 0; b < nb; ++b)
            out[b] = cells[b % period];
        // bar 8: at most one subtractive change (the last note drops), p .5
        if (nb >= 8 && r.next() < 0.5)
        {
            Cell& f = out[nb - 1];
            for (int k = 15; k > 0; --k)
                if (f.on[k] && ! f.ghost[k])
                {
                    f.on[k] = false;
                    break;
                }
        }
        layOutGroove (lane, out, nb);
        return shape;
    }
    if (shape == GhostClimb)
    {
        const Cell x = ghostClimbCell (mode, r);
        Cell out[kMaxBars];
        for (int b = 0; b < nb; ++b)
        {
            out[b] = x;
            // every 4th bar the ghosts speak up
            if ((b & 3) == 3)
                for (int k = 0; k < 16; ++k)
                    if (out[b].ghost[k])
                        out[b].vel[k] = 84 + (int) (r.next() * 8.0);
        }
        // phrase-end fill: an upper-octave note in the first free mid-bar 16th of the last bar
        if (nb >= 4)
        {
            Cell& f = out[nb - 1];
            int lastMain = 0;
            for (int k = 0; k < 16; ++k)
            {
                if (f.on[k] && ! f.ghost[k] && k <= 8)
                    lastMain = f.semi[k];
                if (k >= 8 && k <= 11 && ! f.on[k] && (k + 1 > 15 || ! f.on[k + 1]) && (k == 0 || ! f.on[k - 1]))
                {
                    f.on[k] = true;
                    f.ghost[k] = true;
                    f.semi[k] = lastMain + 12;
                    f.vel[k] = 96;
                    break;
                }
            }
        }
        layOutGhost (lane, out, nb);
        return shape;
    }
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
