#pragma once

#include "Pattern.h"

#include <cmath>

// Hook structure for every roll (Melak, Oct 19: the songs were not catchy). A producer's
// 8-bar loop is one strong 2-bar idea, call and answer, said again and varied only at the
// phrase turn: A B A B A B A B''. The generator drew most bars fresh (per-bar RNG on the
// stab, random back halves on the bass, per-note post-pass edits, hits scattered over 128
// sixteenths), so no figure ever came back and nothing stuck.
//
// restateMotif keeps bars 1-2 as the motif, restates them in bars 3-7 and keeps the
// generator's own last bar (the turn); drums also keep their bar 4. Everything is the
// roll's own material: nothing is stored or taken from a reference, and the originality
// check runs on the locked result.

namespace rolla
{
namespace phrase_lock
{
// Source bar for bar b. Melodic lanes vary only at the last bar (the turn): references change
// bar 8 in ~62% of phrases and bar 4 of the bass almost never. Drums also keep bar 4 (a light lift).
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
