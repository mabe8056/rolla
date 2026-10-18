#pragma once

// ROLLER polish (review of 18 Oct, Maestro #13 job 1). generate() calls this
// for UkMin only, after ROLLER's own bass pipeline, so DEEP / BOUNCY / JACKIN
// output is unchanged.
//
//  - anchorBeats: ROLLER's attacks all sat between the beats. Some bars get a
//    root on beat 1 (or beat 3), keeping bass-on-kick well under 35%.
//  - spreadPositions: the bass used fewer than three in-beat positions often
//    enough (GRID_LOCKED). Quiet ghost notes on e / a 16ths spread it out.
//  - turnBar8: bar 8 was the phrase cut short. It now keeps its first half,
//    then turns: a held approach note that bar 4 does not play, a 4/16 rest,
//    and sometimes a pickup into bar 1.
//  - matchStabColour: a b2 in the bass under a stab that holds the natural 2
//    rubs as a wrong note; that bass note moves to the root.
//
// Weighted draws from the lane seed; no tables of phrases, no reference data.
// Header-only, so every target that builds Generator.cpp picks it up.

#include "DeepBouncy.h"

namespace rolla::roller
{
using namespace deepbouncy::detail;

inline int attacksOnBeat (const Lane& lane) noexcept
{
    int n = 0;
    for (int i = 0; i < lane.count; ++i)
        n += ((int) std::lround (lane.notes[i].start) & 3) == 0 ? 1 : 0;
    return n;
}

// Inserts a note and keeps the lane monophonic around it.
inline bool insertNote (Lane& lane, float at, float len, int semi, int vel, std::uint8_t flags, float loop)
{
    if (lane.count >= kMaxNotes || at < 0.f || at >= loop || onsetAt (lane, at))
        return false;
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        if (n.start < at && n.start + n.length > at)
            n.length = std::max (0.5f, at - n.start);
    }
    float next = loop;
    for (int i = 0; i < lane.count; ++i)
        if (lane.notes[i].start > at)
            next = std::min (next, lane.notes[i].start);
    Note n;
    n.start = at;
    n.length = std::max (0.5f, std::min (len, next - at));
    n.chord[0] = (std::int8_t) semi;
    n.nChord = 1;
    n.vel = (std::uint8_t) vel;
    n.flags = flags;
    lane.notes[lane.count++] = n;
    sortLane (lane);
    return true;
}

inline void anchorBeats (Lane& lane, int bars, std::uint32_t seed)
{
    if (lane.count <= 0)
        return;
    Rng r (mix (seed, 0xA0C4u));
    const float loop = (float) (bars * 16);
    // Aim for 15-25% of attacks on the beat (bass-on-kick stays far under 35%).
    const float target = 0.15f + 0.10f * (float) r.next();
    for (int pass = 0; pass < bars * 2; ++pass)
    {
        const int total = lane.count;
        if (total <= 0 || (float) attacksOnBeat (lane) >= target * (float) (total + 1))
            break;
        const int bar = (int) (r.next() * bars);
        if (bar == bars - 1)
            continue; // bar 8 is the turn
        const float at = (float) (bar * 16 + (r.next() < 0.7 ? 0 : 8));
        if (onsetAt (lane, at))
            continue;
        insertNote (lane, at, 2.f, r.next() < 0.8 ? 0 : 12, 112, 0, loop);
    }
}

inline void spreadPositions (Lane& lane, int bars, std::uint32_t seed)
{
    if (lane.count <= 0)
        return;
    Rng r (mix (seed, 0x5E1Du));
    const float loop = (float) (bars * 16);
    for (int pass = 0; pass < 24; ++pass)
    {
        int pos[4] {};
        for (int i = 0; i < lane.count; ++i)
            ++pos[(int) std::lround (lane.notes[i].start) & 3];
        int used = 0;
        for (int k = 0; k < 4; ++k)
            used += (float) pos[k] >= 0.105f * (float) lane.count ? 1 : 0;
        if (used >= 3)
            return;
        // The thinnest of e / a gets a ghost where the bass is silent.
        const int want = pos[1] <= pos[3] ? 1 : 3;
        bool placed = false;
        const int startBar = (int) (r.next() * bars);
        for (int k = 0; k < bars * 4 && ! placed; ++k)
        {
            const int bar = (startBar + k / 4) % bars;
            if (bar == bars - 1)
                continue;
            const float at = (float) (bar * 16 + (k % 4) * 4 + want);
            if (sounding (lane, at) || onsetAt (lane, at) || onsetAt (lane, at + 1.f) || onsetAt (lane, at - 1.f))
                continue;
            int semi = 0;
            for (int i = 0; i < lane.count; ++i)
                if (lane.notes[i].start < at)
                    semi = lane.notes[i].chord[0];
            placed = insertNote (lane, at, 0.5f, semi, 70, NoteGhost, loop);
        }
        if (! placed)
            return;
    }
}

inline void turnBar8 (Lane& lane, int bars, std::uint32_t seed)
{
    if (bars < 8 || lane.count <= 0)
        return;
    Rng r (mix (seed, 0x7B48u));
    const float loop = (float) (bars * 16);
    const float origin = loop - 16.f;
    // Keep the first half of bar 8; the back half turns.
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        Note n = lane.notes[i];
        if (n.start >= origin + 8.f)
            continue;
        if (n.start >= origin && n.start + n.length > origin + 8.f)
            n.length = std::max (0.5f, origin + 8.f - n.start);
        lane.notes[w++] = n;
    }
    lane.count = w;

    // A held approach note bar 4 does not play at that slot.
    const int slot = r.next() < 0.5 ? 9 : 10;
    int bar4 = 999;
    for (int i = 0; i < lane.count; ++i)
        if ((int) std::lround (lane.notes[i].start) == 48 + slot)
            bar4 = lane.notes[i].chord[0];
    static const int kTurn[] = { -2, 7, 3, 5, 12 };
    static const float kW[] = { 1.3f, 1.1f, 0.8f, 0.6f, 0.5f };
    int semi = kTurn[pickWeighted (kW, 5, r)];
    for (int t = 0; t < 5 && semi == bar4; ++t)
        semi = kTurn[t];
    insertNote (lane, origin + (float) slot, 11.f - (float) slot, semi, 108, 0, loop);
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        if (n.start >= origin && n.start + n.length > origin + 11.f)
            n.length = std::max (0.5f, origin + 11.f - n.start);
    }
    // Sometimes a pickup on step 15 after the 4/16 rest.
    if (r.next() < 0.45)
    {
        static const int kTo[] = { -2, -5, 12 };
        insertNote (lane, loop - 1.f, 1.f, kTo[(int) (r.next() * 3)], 96, 0, loop);
    }
}

inline void matchStabColour (Lane& bass, const Lane& stab)
{
    for (int i = 0; i < bass.count; ++i)
    {
        Note& b = bass.notes[i];
        if (pcOf (b.chord[0]) != 1)
            continue;
        const float a = b.start, e = b.start + b.length;
        bool natural2 = false;
        for (int k = 0; k < stab.count && ! natural2; ++k)
        {
            const Note& s = stab.notes[k];
            if (s.start >= e || s.start + s.length <= a)
                continue;
            for (int c = 0; c < s.nChord && c < 4; ++c)
                natural2 = natural2 || pcOf (s.chord[c]) == 2;
        }
        if (natural2)
            b.chord[0] = (std::int8_t) (b.chord[0] - 1);
    }
}

} // namespace rolla::roller

// The style pass builds on the helpers above; including it here keeps the
// shared Generator.cpp include list untouched.
#include "StyleRoller.h"
