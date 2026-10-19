#pragma once

#include "Pattern.h"

#include <cstdio>

namespace rolla
{

// INTENSITY (UI_SPEC v0.7 §3.18). The generator keeps writing the full phrase
// from the lane seeds. This plan only changes presence and arrangement, so
// moving the slider back rebuilds the same notes. It does not touch BUSY,
// STAB's answer rate, DARK, BOUNCE, or which sound is loaded.
//
// The generator agent should keep this surface: planIntensity + applyIntensity.
// Do not fold the slider into the seed stream.

struct IntensityPlan
{
    int value = 75;
    int dialect = UkMin;
    bool inLane[NumParts] {};
    int comesInAt[NumParts] {}; // percent, or -1 when the lane is not on the ladder
    bool bar8Turn = true;
    bool variation = true; // A B A B'
    int oddLoops = 0;
    const char* word = "";
    char lanesIn[96] {};
    char nextIn[96] {};
    char arrange[64] {};
    char poly[64] {};
};

inline bool intensityLaneIn (const IntensityPlan& plan, int part, bool kept, bool userSample) noexcept
{
    if (part < 0 || part >= NumParts)
        return false;
    if (part == Drums || kept || userSample)
        return true;
    return plan.inLane[part];
}

inline const char* intensityPart (int part) noexcept
{
    static const char* names[] = { "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX", "DRUMS" };
    if (part < 0 || part >= NumParts)
        return "";
    return names[part];
}

inline const char* intensityWord (int dialect, int value) noexcept
{
    static const char* words[NumDialects][3] = {
        { "BARE ROLLER", "ROLLING", "FULL ROLL" },
        { "BARE DUB", "DUBBY", "LUSH" },
        { "BASS + HOOK", "BOUNCING", "FULL HOOK" },
        { "STRIPPED", "JACKIN", "FULL JACK" },
    };
    const int d = dialect < 0 ? 0 : (dialect >= NumDialects ? NumDialects - 1 : dialect);
    const int band = value <= 33 ? 0 : (value <= 66 ? 1 : 2);
    return words[d][band];
}

// stabKnob is 0..1. Above bass-only, the STAB knob forces that lane in.
inline IntensityPlan planIntensity (int dialect, int intensity, float stabKnob) noexcept
{
    IntensityPlan plan;
    plan.dialect = dialect < 0 ? 0 : (dialect >= NumDialects ? NumDialects - 1 : dialect);
    plan.value = intensity < 0 ? 0 : (intensity > 100 ? 100 : intensity);
    plan.word = intensityWord (plan.dialect, plan.value);

    static constexpr int kEnter[6] = { 0, 15, 35, 55, 75, 90 };
    static constexpr int kOrder[NumDialects][6] = {
        { Bass, Stab, Vox, Hits, Pad, Pluck },
        { Bass, Pad, Stab, Pluck, Vox, Hits },
        { Bass, Vox, Stab, Pad, Pluck, Hits },
        { Bass, Hits, Stab, Vox, Pad, Pluck },
    };

    for (int p = 0; p < NumParts; ++p)
        plan.comesInAt[p] = -1;
    plan.inLane[Drums] = true;

    for (int i = 0; i < 6; ++i)
    {
        const int part = kOrder[plan.dialect][i];
        // Pluck is a DEEP / BOUNCY lane. ROLLER and JACKIN do not bring it in.
        if (part == Pluck && (plan.dialect == UkMin || plan.dialect == Jackin))
            continue;
        plan.comesInAt[part] = kEnter[i];
        plan.inLane[part] = plan.value >= kEnter[i];
    }
    if (stabKnob > 0.001f)
        plan.inLane[Stab] = true;

    plan.bar8Turn = plan.value >= 34;
    plan.variation = plan.value >= 67;
    // Mid band (the A2 frame at 60) already loops one lane. LONG's high band loops two.
    if (plan.value >= 34)
        plan.oddLoops = (plan.dialect == Long && plan.value >= 67) ? 2 : 1;

    int nIn = 0;
    plan.lanesIn[0] = 0;
    for (int i = 0; i < 6; ++i)
    {
        const int part = kOrder[plan.dialect][i];
        if (! plan.inLane[part])
            continue;
        std::snprintf (plan.lanesIn + nIn, sizeof (plan.lanesIn) - (size_t) nIn,
                       nIn == 0 ? "%s" : " %s", intensityPart (part));
        nIn = (int) std::strlen (plan.lanesIn);
    }
    std::snprintf (plan.lanesIn + nIn, sizeof (plan.lanesIn) - (size_t) nIn,
                   nIn == 0 ? "DRUMS" : " + DRUMS");

    int nNext = 0;
    plan.nextIn[0] = 0;
    for (int i = 0; i < 6; ++i)
    {
        const int part = kOrder[plan.dialect][i];
        if (plan.inLane[part] || plan.comesInAt[part] < 0)
            continue;
        std::snprintf (plan.nextIn + nNext, sizeof (plan.nextIn) - (size_t) nNext,
                       nNext == 0 ? "%s at %d%%" : " · %s at %d%%",
                       intensityPart (part), kEnter[i]);
        nNext = (int) std::strlen (plan.nextIn);
    }
    if (nNext == 0)
        std::snprintf (plan.nextIn, sizeof (plan.nextIn), "all in");

    std::snprintf (plan.arrange, sizeof (plan.arrange), "%s · %s",
                   plan.bar8Turn ? "bar-8 turn on" : "no bar-8 turn",
                   plan.variation ? "A B A B'" : "A B A B");
    if (plan.oddLoops <= 0)
        std::snprintf (plan.poly, sizeof (plan.poly), "no odd-length loop");
    else if (plan.oddLoops == 1)
        std::snprintf (plan.poly, sizeof (plan.poly), "odd-length loop on 1 lane");
    else
        std::snprintf (plan.poly, sizeof (plan.poly), "odd-length loops on %d lanes", plan.oddLoops);
    return plan;
}

namespace intensity_detail
{
inline int noteBar (float start) noexcept
{
    return (int) std::floor (start / (float) kStepsPerBar);
}

inline void pushNote (Lane& dst, Note n, int bars) noexcept
{
    const float end = (float) (bars * kStepsPerBar);
    if (dst.count >= kMaxNotes || n.start >= end || n.length <= 0.05f)
        return;
    if (n.start + n.length > end)
        n.length = end - n.start;
    if (n.length <= 0.05f)
        return;
    dst.notes[dst.count++] = n;
}

inline void arrangeLane (const Lane& src, Lane& dst, int bars, bool variation, bool bar8) noexcept
{
    dst.seed = src.seed;
    dst.count = 0;
    if (variation && bar8)
    {
        dst.count = src.count < kMaxNotes ? src.count : kMaxNotes;
        for (int i = 0; i < dst.count; ++i)
            dst.notes[i] = src.notes[i];
        return;
    }
    for (int b = 0; b < bars; ++b)
    {
        int from = b;
        if (! bar8 && from >= 4)
            from -= 4;
        // Keep the last bar when the turn is on, so flattening A B A B' does not eat it.
        if (! variation && ! (bar8 && b == bars - 1))
        {
            const int pos = from % 4;
            if (pos >= 2)
                from -= 2;
        }
        const float shift = (float) ((b - from) * kStepsPerBar);
        for (int i = 0; i < src.count; ++i)
        {
            if (noteBar (src.notes[i].start) != from)
                continue;
            Note n = src.notes[i];
            n.start += shift;
            pushNote (dst, n, bars);
        }
    }
}

inline void oddLoop (Lane& lane, int loopBars, int bars) noexcept
{
    if (loopBars < 2 || loopBars >= bars)
        return;
    Note base[kMaxNotes];
    int nBase = 0;
    const float loopEnd = (float) (loopBars * kStepsPerBar);
    for (int i = 0; i < lane.count && nBase < kMaxNotes; ++i)
        if (lane.notes[i].start < loopEnd)
            base[nBase++] = lane.notes[i];
    lane.count = 0;
    for (int k = 0; k * loopBars < bars; ++k)
    {
        const float shift = (float) (k * loopBars * kStepsPerBar);
        for (int i = 0; i < nBase; ++i)
        {
            Note n = base[i];
            n.start += shift;
            pushNote (lane, n, bars);
        }
    }
}
} // namespace intensity_detail

// keepMask bits are copied verbatim and stay in. sampleMask bits stay in,
// and still take the arrangement. Other out lanes are emptied.
inline void applyIntensity (const Groove& src, Groove& dst, const IntensityPlan& plan,
                            std::uint32_t keepMask, std::uint32_t sampleMask) noexcept
{
    dst = src;
    const int bars = src.bars < 2 ? 2 : (src.bars > kMaxBars ? kMaxBars : src.bars);

    int oddPart[2] = { -1, -1 };
    int nOdd = 0;
    static constexpr int kOrder[NumDialects][6] = {
        { Bass, Stab, Vox, Hits, Pad, Pluck },
        { Bass, Pad, Stab, Pluck, Vox, Hits },
        { Bass, Vox, Stab, Pad, Pluck, Hits },
        { Bass, Hits, Stab, Vox, Pad, Pluck },
    };
    for (int i = 5; i >= 1 && nOdd < plan.oddLoops; --i)
    {
        const int part = kOrder[plan.dialect][i];
        if ((keepMask & (1u << part)) != 0)
            continue;
        if (! plan.inLane[part] && (sampleMask & (1u << part)) == 0)
            continue;
        oddPart[nOdd++] = part;
    }

    for (int part = 0; part < Drums; ++part)
    {
        const bool kept = (keepMask & (1u << part)) != 0;
        const bool sampled = (sampleMask & (1u << part)) != 0;
        if (kept)
            continue;
        if (! plan.inLane[part] && ! sampled)
        {
            dst.lane[part].seed = src.lane[part].seed;
            dst.lane[part].count = 0;
            continue;
        }
        Lane arranged;
        intensity_detail::arrangeLane (src.lane[part], arranged, bars, plan.variation, plan.bar8Turn);
        int loop = 0;
        if (part == oddPart[0])
            loop = 5;
        else if (part == oddPart[1])
            loop = 3;
        if (loop > 0)
            intensity_detail::oddLoop (arranged, loop, bars);
        dst.lane[part] = arranged;
    }
}

} // namespace rolla
