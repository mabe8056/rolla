#pragma once

#include "Pattern.h"

#include <cmath>

// Hook structure for every roll (Melak, Oct 19: the songs were not catchy). A producer's
// 8-bar loop is one strong 2-bar idea, call and answer, said again and varied only at the
// phrase turns: A B A B' A B A B''. The generator drew most bars fresh (per-bar RNG on the
// stab, random back halves on the bass, per-note post-pass edits, hits scattered over 128
// sixteenths), so no figure ever came back and nothing stuck.
//
// restateMotif keeps bars 1-2 as the motif, restates them in bars 3, 5, 6, 7 and keeps the
// generator's own bar 4 (the answer's variation) and last bar (the turn). Everything is the
// roll's own material: nothing is stored or taken from a reference, and the originality
// check runs on the locked result.

namespace rolla
{
namespace phrase_lock
{
// Source bar for bar b: bars 4 and 8 (and the last bar) keep their own material.
inline int sourceBar (int b, int bars) noexcept
{
    if (b < 2 || b % 4 == 3 || b == bars - 1)
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

inline void restateMotif (DrumLane& drums, int bars) noexcept
{
    if (bars < 4)
        return;
    const DrumLane src = drums;
    for (int b = 0; b < bars; ++b)
    {
        const int from = phrase_lock::sourceBar (b, bars);
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
