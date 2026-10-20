#pragma once

#include "Pattern.h"

#include <algorithm>
#include <cmath>

// Hook structure for every roll (Melak, Oct 19: the songs were not catchy). A producer's
// 8-bar loop is one strong 2-bar idea, call and answer, said again and varied only at the
// phrase turn: A B A B A B A B''. The generator drew most bars fresh (per-bar RNG on the
// stab, random back halves on the bass, per-note post-pass edits, hits scattered over 128
// sixteenths), so no figure ever came back and nothing stuck.
//
// restateMotif keeps bars 1-2 as the motif, restates them in bars 3-7 and keeps the
// generator's own last bar (the turn); drums also keep their bar 4, the bass the back half
// of its bar 4 (restateBass: the answer), and JACKIN's bass keeps its own loop. Everything is the
// roll's own material: nothing is stored or taken from a reference, and the originality
// check runs on the locked result.

namespace rolla
{
namespace phrase_lock
{
// Source bar for bar b. Melodic lanes vary only at the last bar (the turn): references change
// bar 8 in ~62% of phrases and bar 4 of the bass almost never. Drums also keep bar 4 (a light lift).
// On a full roll only the hook and the bass keep that turn and every other lane replays bar 2 there
// afterwards; rolled drums replay bar 2 there too, then take the roll's one drum move, if any
// (replayBar2AtTurn).
inline int sourceBar (int b, int bars, bool keepBar4 = false) noexcept
{
    if (b < 2 || b == bars - 1 || (keepBar4 && b % 4 == 3))
        return b;
    return b % 2;
}

inline int barOf (float start) noexcept
{
    return (int) std::floor ((start + 1.0e-3f) / (float) kStepsPerBar);
}

// 16th of `start` inside bar `bar` (barOf's rounding).
inline int slotIn (float start, int bar) noexcept
{
    return (int) std::floor (start + 1.0e-3f) - bar * kStepsPerBar;
}

// The bass's light answer (catchiness report #3). Bar 4 restates bar 2 up to its 8th 16th and
// keeps the generator's own back half (16ths 9-16): the reference bass answers bar 2 there in ~87%
// of phrases (the model's bar4_answer: the last 4-8 sixteenths change, the front stays
// recognisable), and restating bar 2 whole put the riff above the model's repeat band. The answer
// half is source material, like the motif and the turn: every restatement keeps it as it is, so
// bar 4 is the same answer however often the riff is restated (knob passes, the hook pass).
inline constexpr int kAnswerFrom = 8;

inline bool answerHalf (int b, int slot, int bars) noexcept
{
    return bars >= 8 && b % 4 == 3 && b != bars - 1 && slot >= kAnswerFrom;
}

// Source bar of the bass at 16th `slot` of bar b: sourceBar, except the answer half (its own).
// ownLoop: JACKIN, whose bass loops on its own (loopJackinBass) and is never restated.
inline int bassSourceBar (int b, int slot, int bars, bool ownLoop = false) noexcept
{
    if (ownLoop || bars < 4 || answerHalf (b, slot, bars))
        return b;
    return sourceBar (b, bars);
}
} // namespace phrase_lock

inline void restateMotif (Lane& lane, int bars) noexcept
{
    if (bars < 4 || lane.count <= 0)
        return;
    const Lane src = lane;
    lane.count = 0;
    for (int b = 0; b < bars; ++b)
    {
        const int from = phrase_lock::sourceBar (b, bars);
        const float shift = (float) ((b - from) * kStepsPerBar);
        for (int i = 0; i < src.count && lane.count < kMaxNotes; ++i)
        {
            if (phrase_lock::barOf (src.notes[i].start) != from)
                continue;
            Note n = src.notes[i];
            n.start += shift;
            // A note copied into the bar before a kept turn must not ring into it.
            const float barEnd = (float) ((b + 1) * kStepsPerBar);
            if (phrase_lock::sourceBar (b + 1, bars) == b + 1 && n.start + n.length > barEnd)
                n.length = std::max (0.25f, barEnd - n.start);
            lane.notes[lane.count++] = n;
        }
    }
}

// The bass restatement, A B A B' A B A B'': restateMotif with bar 4's answer half kept
// (phrase_lock::bassSourceBar). cutAtKept: a note copied into the bar before a kept bar ends at
// the barline (restateMotif's rule); false copies held notes as they ring (the hook pass, whose
// motif bar 1 is not cut at bar 2). The caller runs keepMonophonic.
inline void restateBass (Lane& lane, int bars, bool cutAtKept = true) noexcept
{
    if (bars < 4 || lane.count <= 0)
        return;
    const Lane src = lane;
    lane.count = 0;
    for (int b = 0; b < bars; ++b)
    {
        const bool nextKept = b + 1 < bars && phrase_lock::bassSourceBar (b + 1, 0, bars) == b + 1;
        const float barEnd = (float) ((b + 1) * kStepsPerBar);
        for (int i = 0; i < src.count && lane.count < kMaxNotes; ++i)
        {
            const int from = phrase_lock::barOf (src.notes[i].start);
            if (phrase_lock::bassSourceBar (b, phrase_lock::slotIn (src.notes[i].start, from), bars) != from)
                continue;
            Note n = src.notes[i];
            n.start += (float) ((b - from) * kStepsPerBar);
            if (cutAtKept && nextKept && n.start + n.length > barEnd)
                n.length = std::max (0.25f, barEnd - n.start);
            lane.notes[lane.count++] = n;
        }
    }
    std::stable_sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
}

// JACKIN's bass loops on its own (loopJackinBass: A B A B4 A B A B8 from one bar pair, then its
// finishing passes vary single bars) and is never restated. An edit made to its motif (bars 1-2)
// is replayed instead: every later bar of the same parity before the turn takes the same edit
// wherever it plays the same note there (same 16th, same pitch), so the loop carries the edit and
// keeps its own per-bar differences. `before` is the lane before the edit; edits are pitch changes,
// removals, additions (the added note as written) and moves of up to two 16ths (a removed note
// paired with an added one of its pitch). Lengths follow keepMonophonic and the held-bass fit.
inline void replayMotifEdit (Lane& lane, const Lane& before, int bars) noexcept
{
    const int nb = std::min (bars, kMaxBars);
    if (nb < 4)
        return;
    int bAt[2][kStepsPerBar], aAt[2][kStepsPerBar];
    for (int m = 0; m < 2; ++m)
        for (int k = 0; k < kStepsPerBar; ++k)
            bAt[m][k] = aAt[m][k] = -1;
    for (int i = 0; i < before.count; ++i)
    {
        const int b = phrase_lock::barOf (before.notes[i].start);
        const int k = b >= 0 && b < 2 ? phrase_lock::slotIn (before.notes[i].start, b) : -1;
        if (k >= 0 && k < kStepsPerBar && bAt[b][k] < 0)
            bAt[b][k] = i;
    }
    for (int i = 0; i < lane.count; ++i)
    {
        const int b = phrase_lock::barOf (lane.notes[i].start);
        const int k = b >= 0 && b < 2 ? phrase_lock::slotIn (lane.notes[i].start, b) : -1;
        if (k >= 0 && k < kStepsPerBar && aAt[b][k] < 0)
            aAt[b][k] = i;
    }
    const Lane src = lane;
    // Moves: a removed note and an added one of the same pitch up to two 16ths away.
    int moveTo[2][kStepsPerBar];
    bool addTaken[2][kStepsPerBar] {};
    for (int m = 0; m < 2; ++m)
        for (int k = 0; k < kStepsPerBar; ++k)
        {
            moveTo[m][k] = -1;
            if (bAt[m][k] < 0 || aAt[m][k] >= 0)
                continue;
            for (int d : { 1, -1, 2, -2 })
            {
                const int t = k + d;
                if (t < 0 || t >= kStepsPerBar || addTaken[m][t] || bAt[m][t] >= 0 || aAt[m][t] < 0
                    || src.notes[aAt[m][t]].chord[0] != before.notes[bAt[m][k]].chord[0])
                    continue;
                moveTo[m][k] = t;
                addTaken[m][t] = true;
                break;
            }
        }
    auto targetAt = [&] (int bar, int k) {
        for (int i = 0; i < lane.count; ++i)
            if (phrase_lock::barOf (lane.notes[i].start) == bar && phrase_lock::slotIn (lane.notes[i].start, bar) == k)
                return i;
        return -1;
    };
    auto removeAt = [&] (int i) {
        for (int j = i; j + 1 < lane.count; ++j)
            lane.notes[j] = lane.notes[j + 1];
        --lane.count;
    };
    for (int t = 2; t < nb - 1; ++t)
    {
        const int m = t % 2;
        const float shift = (float) ((t - m) * kStepsPerBar);
        for (int k = 0; k < kStepsPerBar; ++k)
        {
            const int bi = bAt[m][k], ai = aAt[m][k];
            if (bi < 0)
                continue;
            const int ti = targetAt (t, k);
            if (ti < 0 || lane.notes[ti].chord[0] != before.notes[bi].chord[0])
                continue;
            if (ai >= 0)
            {
                if (src.notes[ai].chord[0] != before.notes[bi].chord[0])
                    lane.notes[ti].chord[0] = src.notes[ai].chord[0];
            }
            else if (moveTo[m][k] >= 0)
            {
                if (targetAt (t, moveTo[m][k]) < 0)
                    lane.notes[ti].start += (float) (moveTo[m][k] - k);
            }
            else
                removeAt (ti);
        }
        for (int k = 0; k < kStepsPerBar && lane.count < kMaxNotes; ++k)
        {
            const int ai = aAt[m][k];
            if (ai < 0 || bAt[m][k] >= 0 || addTaken[m][k] || targetAt (t, k) >= 0)
                continue;
            Note n = src.notes[ai];
            n.start += shift;
            lane.notes[lane.count++] = n;
        }
    }
    std::stable_sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
}

// Bar 8 is one clear move (catchiness research #2): references change bar 8 in ~62% of phrases,
// and with one move, not five. A lane that does not carry the turn replays its bar 2 in the last
// bar (A B A B A B A B), so bar 1 lands clean. The bar before it restates bar 1: a note there that
// restateMotif cut at the kept turn rings into the last bar again, as it rings from bar 1 into
// bar 2. Nothing new is written: the lane's own bar 2 is said once more.
inline void replayBar2AtTurn (Lane& lane, int bars) noexcept
{
    if (bars < 4 || lane.count <= 0)
        return;
    const int last = bars - 1;
    const float turnStart = (float) (last * kStepsPerBar);
    const float loopEnd = (float) (bars * kStepsPerBar);
    const float before = (float) ((last - 1) * kStepsPerBar);
    const Lane src = lane;
    lane.count = 0;
    for (int i = 0; i < src.count && lane.count < kMaxNotes; ++i)
    {
        Note n = src.notes[i];
        const int b = phrase_lock::barOf (n.start);
        if (b == last)
            continue;
        if (b == last - 1 && std::abs (n.start + n.length - turnStart) < 1.0e-3f)
            for (int k = 0; k < src.count; ++k)
            {
                const Note& o = src.notes[k];
                if (phrase_lock::barOf (o.start) == 0 && std::abs (o.start - (n.start - before)) < 1.0e-3f
                    && o.nChord == n.nChord && o.chord[0] == n.chord[0] && o.chord[1] == n.chord[1]
                    && o.chord[2] == n.chord[2] && o.chord[3] == n.chord[3]
                    && o.start + o.length > (float) kStepsPerBar + 1.0e-3f)
                {
                    n.length = o.length;
                    break;
                }
            }
        lane.notes[lane.count++] = n;
    }
    for (int i = 0; i < src.count && lane.count < kMaxNotes; ++i)
    {
        if (phrase_lock::barOf (src.notes[i].start) != 1)
            continue;
        Note n = src.notes[i];
        n.start += before; // bar 2 starts one bar in: (last - 1) bars on is the last bar
        n.length = std::min (n.length, loopEnd - n.start);
        lane.notes[lane.count++] = n;
    }
    std::stable_sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
}

// The bass is one voice: after a restatement a held note may run into the next bar's first
// note. Trim it to end where the next note starts (DEEP keeps its held overlaps: legato ties).
inline void keepMonophonic (Lane& lane, bool keepTies) noexcept
{
    for (int i = 0; i + 1 < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        const float next = lane.notes[i + 1].start;
        if (n.start + n.length > next && ! keepTies)
            n.length = std::max (0.25f, next - n.start);
    }
}

// Sounding share (held length / loop) into [lo, hi] with one rule for every note: each note
// grows toward its gap (to the next note, or the loop end) or shrinks by the same factor. The
// same move in every bar, so restated bars stay identical.
inline void shapeSounding (Lane& lane, int bars, float lo, float hi) noexcept
{
    if (lane.count <= 0)
        return;
    const float loop = (float) (bars * kStepsPerBar);
    float gap[kMaxNotes] {}, len[kMaxNotes] {};
    float held = 0.f;
    for (int i = 0; i < lane.count; ++i)
    {
        const float next = i + 1 < lane.count ? lane.notes[i + 1].start : loop;
        gap[i] = std::max (0.25f, next - lane.notes[i].start);
        len[i] = std::min (lane.notes[i].length, gap[i]);
        held += len[i];
    }
    const float snd = held / loop;
    if (snd < lo)
    {
        float room = 0.f;
        for (int i = 0; i < lane.count; ++i)
            room += gap[i] - len[i];
        const float a = room > 0.f ? std::min (1.f, ((lo + 0.01f) * loop - held) / room) : 0.f;
        for (int i = 0; i < lane.count; ++i)
            lane.notes[i].length = len[i] + a * (gap[i] - len[i]);
    }
    else if (snd > hi)
    {
        const float c = (hi - 0.01f) * loop / held;
        for (int i = 0; i < lane.count; ++i)
            lane.notes[i].length = std::max (0.25f, len[i] * c);
    }
    else
        for (int i = 0; i < lane.count; ++i)
            lane.notes[i].length = len[i];
}

// Bass voice leading (catchiness research #7): references move the bass by 5+ semitones on
// 23-34% of moves and repeat the note on ~52%; ROLLA leapt on 77-84%, mostly octave
// displacements (8, 9, 14, 15 semitones). Each note takes the octave of its own pitch class
// closest to the previous note, inside the lane's own range: same note names, same rhythm,
// steps instead of displaced leaps. A move of exactly an octave (same pitch class) is a chosen
// signature and stays. Runs on the bass's source notes (the motif bars, bar 4's answer half and
// the turn; every bar for a bass that loops on its own, ownLoop): before restateBass, and again
// after it, so the answer half and the turn lead from the restated notes before them.
inline void voiceLead (Lane& lane, int bars, bool ownLoop = false) noexcept
{
    if (lane.count < 2)
        return;
    int lo = 127, hi = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        lo = std::min (lo, (int) lane.notes[i].chord[0]);
        hi = std::max (hi, (int) lane.notes[i].chord[0]);
    }
    for (int i = 1; i < lane.count; ++i)
    {
        const int b = phrase_lock::barOf (lane.notes[i].start);
        if (phrase_lock::bassSourceBar (b, phrase_lock::slotIn (lane.notes[i].start, b), bars, ownLoop) != b)
            continue;
        const int prev = lane.notes[i - 1].chord[0];
        const int cur = lane.notes[i].chord[0];
        const int d = cur - prev;
        if (d == 0 || d == 12 || d == -12 || std::abs (d) <= 6)
            continue;
        int best = cur;
        for (int cand = cur - 24; cand <= cur + 24; cand += 12)
            if (cand >= lo && cand <= hi && std::abs (cand - prev) < std::abs (best - prev))
                best = cand;
        lane.notes[i].chord[0] = (std::int8_t) best;
    }
}

// The drums at the turn: the last bar replays bar 2 below 16th `upTo` (FILL keeps its own last
// beat, so the same roll with FILL on and off still differs only in that beat). The bar-8 drum
// move (Generator.cpp, bar8DrumMove) is then the only change there. Bar 4 keeps its light lift.
inline void replayBar2AtTurn (DrumLane& drums, int bars, int upTo) noexcept
{
    if (bars < 4)
        return;
    const int to = (bars - 1) * kStepsPerBar;
    for (int s = 0; s < upTo && s < kStepsPerBar; ++s)
    {
        for (int v = 0; v < NumDrumVoices; ++v)
            drums.vel[v][to + s] = drums.vel[v][kStepsPerBar + s];
        drums.accent[to + s] = drums.accent[kStepsPerBar + s];
    }
}

inline void restateMotif (DrumLane& drums, int bars) noexcept
{
    if (bars < 4)
        return;
    const DrumLane src = drums;
    for (int b = 0; b < bars; ++b)
    {
        const int from = phrase_lock::sourceBar (b, bars, true);
        if (from == b)
            continue;
        for (int s = 0; s < kStepsPerBar; ++s)
        {
            for (int v = 0; v < NumDrumVoices; ++v)
                drums.vel[v][b * kStepsPerBar + s] = src.vel[v][from * kStepsPerBar + s];
            drums.accent[b * kStepsPerBar + s] = src.accent[from * kStepsPerBar + s];
        }
    }
}
} // namespace rolla
