#pragma once

// ROLLER style pass. Generator tuning toward the ROLLER bands in
// data/genre/style_rules.json (styles.ROLLER, abstract per-style statistics,
// no note data). generate() and writeDrums() call these for UkMin only, after
// RollerPass.h, so DEEP / BOUNCY / JACKIN output is unchanged.
//
//  - fitDensity: bass onsets/bar drift toward the 5.0 target (band 3.6-7.4),
//    scaled by BUSY. Whole slots leave the even (call) bars together, so the
//    2- and 4-bar repetition holds while bar-to-bar density varies. Which slot
//    goes is weighted by how far its in-beat class sits over slot_class_weights.
//  - answerBar4: bar 4 answers bar 2 on 85% of rolls: three or more back-half
//    slots change and the note count moves by one or two (not pitch-only).
//    The other rolls keep bar 2's back half.
//  - styleDrums: hat / perc onsets per bar drift toward 11.7 (band 8.9-15.0)
//    with ghost 16ths (velocity 25-55) that take the engine swing, offbeat-8th
//    hats on about 85% of slots, and a perc / rim turn in the last beat of
//    bar 4 (half the rolls) and the last bar (always, drawn from drop-out,
//    perc pickups and a rim).
//
// Every choice is a weighted draw from the lane seed. Targets live in one
// struct so they can later be read from GenreTargets groove data.

#include "RollerPass.h"

namespace rolla::roller
{

struct Targets
{
    // bass
    float bassNpb = 5.0f;
    float bassNpbMin = 3.62f;
    float bassNpbMax = 7.44f;
    float onKickMax = 0.33f;   // soft band top; the hard rule is 35%
    float eaMax = 0.80f;       // e + a share with no beat / "and" anchor
    // In-beat classes, 0-based 16th within the beat: beat, e, and, a.
    float slotClass[4] { 0.200f, 0.252f, 0.304f, 0.244f };
    float bar4AnswerP = 0.85f;
    float pickupMinShare = 0.15f;
    float m2Min = 0.672f;
    float m4Min = 0.766f;
    // groove
    float swing = 0.537f;      // hat 16th swing p50
    float swingMin = 0.512f;
    float swingMax = 0.575f;
    float hiBandNpb = 11.66f;
    float hiBandMin = 8.87f;
    float hiBandMax = 15.01f;
    float offbeatHatP = 0.85f;
    int ghostVelMin = 25;
    int ghostVelMax = 55;
};

inline constexpr Targets kTargets {};

// Mirrors Generator.cpp's busyFactor, normalised so the default BUSY (0.40) is 1.
inline float busyScale (float busy) noexcept
{
    return (0.75f + 0.60f * std::clamp (busy, 0.f, 1.f)) / 0.99f;
}

inline int slotClassCounts (const Lane& lane, int* cls) noexcept
{
    for (int c = 0; c < 4; ++c)
        cls[c] = 0;
    for (int i = 0; i < lane.count; ++i)
        ++cls[(int) std::lround (lane.notes[i].start) & 3];
    return lane.count;
}

inline void fitDensity (Lane& lane, int bars, std::uint32_t seed, float busy)
{
    if (bars < 4 || lane.count <= 0)
        return;
    Rng r (mix (seed, 0xD3A5u));
    // A per-roll target around 5 (4.4-5.6) so rolls do not all sit on one number.
    const float npb = kTargets.bassNpb + ((float) r.next() - 0.5f) * 1.2f;
    const int want = (int) std::lround (npb * busyScale (busy) * (float) bars);
    for (int pass = 0; pass < 16 && lane.count > want; ++pass)
    {
        int cls[4];
        const int total = slotClassCounts (lane, cls);
        float w[16] {};
        // Slot 0 stays: beat 1 is ROLLER's anchor. The last bar is the turn.
        for (int slot = 1; slot < 16; ++slot)
        {
            int hits = 0;
            bool ok = true;
            for (int bar = 0; bar < bars - 1; bar += 2)
            {
                if (! onsetAt (lane, (float) (bar * 16 + slot)))
                    continue;
                ++hits;
                ok = ok && countInBar (lane, bar) > 3;
            }
            if (! ok || hits == 0 || total - hits <= 0)
                continue;
            const int c = slot & 3;
            // Keep every in-beat class at a real share (GRID_LOCKED guard).
            if ((float) (cls[c] - hits) < 0.12f * (float) (total - hits))
                continue;
            const float over = (float) cls[c] / (float) total - kTargets.slotClass[c];
            w[slot] = std::clamp (over + 0.08f, 0.01f, 1.f);
        }
        const int slot = pickWeighted (w, 16, r);
        if (w[slot] <= 0.f)
            break;
        sortLane (lane);
        int k = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            const Note n = lane.notes[i];
            const int st = (int) std::lround (n.start);
            const int bar = st / 16;
            if ((bar & 1) == 0 && bar < bars - 1 && st % 16 == slot)
            {
                // A note that ran into the dropped one holds through its span
                // (capped at 3/16), so the bass keeps sounding most of the bar.
                if (k > 0)
                {
                    Note& p = lane.notes[k - 1];
                    if ((int) std::lround (p.start) / 16 == bar && p.start + p.length >= n.start - 0.05f)
                        p.length = std::min (n.start + n.length - p.start, std::max (p.length, 3.f));
                }
                continue;
            }
            lane.notes[k++] = n;
        }
        lane.count = k;
    }
}

inline void answerBar4 (Lane& lane, int bars, std::uint32_t seed)
{
    if (bars < 8 || lane.count <= 0)
        return;
    Rng r (mix (seed, 0xB4A7u));
    const bool answer = r.next() < (double) kTargets.bar4AnswerP;
    const float loop = (float) (bars * 16);
    const float src = 16.f + 8.f, dst = 48.f + 8.f;

    // Bar 4's back half starts as a copy of bar 2's.
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        Note n = lane.notes[i];
        if (n.start >= dst - 1e-3f && n.start < dst + 8.f - 1e-3f)
            continue;
        if (n.start < dst && n.start + n.length > dst)
            n.length = std::max (0.5f, dst - n.start);
        lane.notes[w++] = n;
    }
    lane.count = w;
    Note back[8];
    int nBack = 0;
    for (int i = 0; i < lane.count && nBack < 8; ++i)
        if (lane.notes[i].start >= src - 1e-3f && lane.notes[i].start < src + 8.f - 1e-3f)
            back[nBack++] = lane.notes[i];

    bool drop[8] {};
    int nAdd = 0;
    if (answer)
    {
        // Drop 2 add 1, or drop 1 add 2: three slots change, the count moves by one.
        const bool thinner = nBack >= 2 && r.next() < 0.5;
        const int nDrop = std::min (nBack, thinner ? 2 : 1);
        for (int d = 0; d < nDrop; ++d)
        {
            int k = (int) (r.next() * nBack) % std::max (1, nBack);
            for (int t = 0; t < nBack && drop[k]; ++t)
                k = (k + 1) % nBack;
            drop[k] = true;
        }
        nAdd = thinner ? 1 : 2;
        if (nBack == 0)
            nAdd = 2;
    }
    for (int i = 0; i < nBack; ++i)
    {
        if (drop[i])
            continue;
        Note n = back[i];
        n.start += 32.f;
        insertNote (lane, n.start, n.length, n.chord[0], n.vel, n.flags, loop);
    }
    for (int a = 0; a < nAdd; ++a)
    {
        float wt[8] {};
        for (int s = 0; s < 8; ++s)
        {
            const int slot = 8 + s;
            const float at = dst + (float) s;
            bool wasDropped = false;
            for (int i = 0; i < nBack; ++i)
                wasDropped = wasDropped || (drop[i] && std::lround (back[i].start) - 16 == slot);
            if (kickSlot (slot) || wasDropped || onsetAt (lane, at) || onsetAt (lane, at - 1.f) || onsetAt (lane, at + 1.f))
                continue;
            // slot_weight_rule: class weight, x1.2 on the last two 16ths of the answer bar.
            wt[s] = kTargets.slotClass[slot & 3] * (slot >= 14 ? 1.2f : 1.f);
        }
        const int s = pickWeighted (wt, 8, r);
        if (wt[s] <= 0.f)
            break;
        // Optional pitch answer: root, 4th, 5th or b7.
        static const int kAns[] = { 0, 5, 7, 10 };
        static const float kAw[] = { 1.0f, 0.6f, 0.9f, 0.7f };
        insertNote (lane, dst + (float) s, 2.f, kAns[pickWeighted (kAw, 4, r)], 104, 0, loop);
    }
}

inline void styleDrums (DrumLane& d, int bars, std::uint32_t seed)
{
    if (bars <= 0)
        return;
    Rng r (mix (seed, 0xD7A1u));
    auto hi = [&d] (int t)
    {
        return d.vel[HatC][t] != 0 || d.vel[HatO][t] != 0 || d.vel[Ride][t] != 0 || d.vel[Perc][t] != 0;
    };
    auto vel = [&r] (int lo, int top) { return (std::uint8_t) (lo + (int) (r.next() * (double) (top - lo + 1))); };
    // A per-roll density around the target (10.5-12.9 a bar).
    const float want = kTargets.hiBandNpb + ((float) r.next() - 0.5f) * 2.4f;
    for (int bar = 0; bar < bars; ++bar)
    {
        const int o = bar * 16;
        for (int slot : { 2, 6, 10, 14 })
            if (! hi (o + slot) && r.next() < (double) kTargets.offbeatHatP)
                d.vel[HatC][o + slot] = vel (64, 90);
        int have = 0;
        for (int s = 0; s < 16; ++s)
            have += hi (o + s) ? 1 : 0;
        int add = std::max (0, (int) std::lround (want - (float) have + ((float) r.next() - 0.5f)));
        while (add-- > 0)
        {
            // Ghost 16ths: e / a first (the swing lives there), soft taps on the beat.
            float wt[16] {};
            for (int s = 0; s < 16; ++s)
                if (! hi (o + s))
                    wt[s] = (s & 1) ? ((s & 3) == 3 ? 1.1f : 0.9f) : ((s & 3) == 0 ? 0.35f : 0.8f);
            const int s = pickWeighted (wt, 16, r);
            if (wt[s] <= 0.f)
                break;
            d.vel[HatC][o + s] = (s & 3) == 0 ? vel (kTargets.ghostVelMin, 40) : vel (kTargets.ghostVelMin + 5, kTargets.ghostVelMax);
        }
    }

    // Turns: the last beat of bar 4 sometimes, bar 8 (the last bar) nearly always.
    auto turn = [&] (int bar)
    {
        const int o = bar * 16;
        auto fillCount = [&] ()
        {
            int n = 0;
            for (int s = 12; s < 16; ++s)
                n += (d.vel[Perc][o + s] != 0) + (d.vel[Rim][o + s] != 0) + (d.vel[TomLow][o + s] != 0);
            return n;
        };
        const int before = fillCount();
        // Drop out: the ghosts in the last beat leave space for the fill.
        if (r.next() < 0.2)
            for (int s = 13; s < 16; s += 2)
                d.vel[HatC][o + s] = 0;
        if (r.next() < 0.45 && d.vel[Perc][o + 13] == 0)
            d.vel[Perc][o + 13] = vel (58, 78);
        if (r.next() < 0.55 && d.vel[Perc][o + 15] == 0)
            d.vel[Perc][o + 15] = vel (70, 96);
        if (r.next() < 0.3)
        {
            const int s = r.next() < 0.5 ? 14 : 11;
            if (d.vel[Rim][o + s] == 0)
                d.vel[Rim][o + s] = vel (60, 88);
        }
        if (fillCount() <= before)
        {
            for (int s : { 15, 13, 14, 12 })
                if (d.vel[Perc][o + s] == 0)
                {
                    d.vel[Perc][o + s] = vel (70, 96);
                    break;
                }
        }
    };
    if (bars >= 8 && r.next() < 0.5)
        turn (3);
    if (bars >= 4)
        turn (bars - 1);
}

} // namespace rolla::roller
