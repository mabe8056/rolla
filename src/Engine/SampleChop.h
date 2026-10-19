#pragma once

#include "../Generator/Pattern.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rolla
{

// Spectral flatness is 0 for a pure tone and 1 for white noise.
// A measurement at or above this is broadband: not a chop, and not a pad bed.
// Below zero means "not measured"; the name table is the fallback.
inline constexpr float kTonalFlatness = 0.40f;

// Files shorter than this play whole. A sliced hit in the tests is longer.
inline constexpr double kChopMinSeconds = 0.90;

inline constexpr const char* kHardBroadband[] = {
    "noise", "crackle", "vinyl", "riser", "sweep", "whoosh"
};

inline constexpr const char* kSoftPadBed[] = {
    "texture", "background", "atmos", "drone"
};

struct ChopPlan
{
    int start = 0;
    int end = 0;
    int fade = 0;
    bool hold = false;
    bool blocked = false;
};

inline bool chopPlanOn (const ChopPlan& plan) noexcept
{
    return ! plan.blocked && plan.fade > 0 && plan.end > plan.start + plan.fade * 2;
}

// Raised cosine. The sample at `start` and the sample at `end - 1` are silent,
// so the cut meets the surrounding silence with no step.
inline float chopEdge (int index, int start, int end, int fade) noexcept
{
    if (fade <= 0 || index < start || index >= end)
        return 0.f;
    const int into = index - start;
    const int left = end - 1 - index;
    float gain = 1.f;
    if (into < fade)
    {
        const float t = (float) into / (float) fade;
        gain *= 0.5f * (1.f - std::cos (3.14159265f * t));
    }
    if (left < fade)
    {
        const float t = (float) left / (float) fade;
        gain *= 0.5f * (1.f - std::cos (3.14159265f * t));
    }
    return gain;
}

namespace chop_detail
{

inline void fftRadix2 (float* re, float* im, int n) noexcept
{
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
        {
            std::swap (re[i], re[j]);
            std::swap (im[i], im[j]);
        }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const float ang = -2.f * 3.14159265f / (float) len;
        const float wRe = std::cos (ang);
        const float wIm = std::sin (ang);
        for (int i = 0; i < n; i += len)
        {
            float wr = 1.f;
            float wi = 0.f;
            for (int k = 0; k < len / 2; ++k)
            {
                const int a = i + k;
                const int b = a + len / 2;
                const float tr = wr * re[b] - wi * im[b];
                const float ti = wr * im[b] + wi * re[b];
                re[b] = re[a] - tr;
                im[b] = im[a] - ti;
                re[a] += tr;
                im[a] += ti;
                const float next = wr * wRe - wi * wIm;
                wi = wr * wIm + wi * wRe;
                wr = next;
            }
        }
    }
}

} // namespace chop_detail

// Geometric mean of the spectrum over its arithmetic mean. One 512-point FFT.
inline float spectralFlatness (const float* x, int n) noexcept
{
    if (x == nullptr || n < 64)
        return -1.f;
    constexpr int kN = 512;
    const int use = n >= kN ? kN : 256;
    if (n < use)
        return -1.f;
    int origin = (n - use) / 2;
    float peak = 0.f;
    int peakAt = origin;
    const int hop = std::max (1, use / 4);
    for (int i = 0; i + use <= n; i += hop)
    {
        float e = 0.f;
        for (int k = 0; k < use; k += 4)
            e += std::fabs (x[i + k]);
        if (e > peak)
        {
            peak = e;
            peakAt = i;
        }
    }
    float re[kN] {};
    float im[kN] {};
    for (int i = 0; i < use; ++i)
    {
        const float w = 0.5f * (1.f - std::cos (2.f * 3.14159265f * (float) i / (float) (use - 1)));
        re[i] = x[peakAt + i] * w;
    }
    chop_detail::fftRadix2 (re, im, use);
    double logSum = 0.0;
    double magSum = 0.0;
    int bins = 0;
    for (int i = 2; i < use / 2 - 1; ++i)
    {
        const float mag = std::sqrt (re[i] * re[i] + im[i] * im[i]) + 1.0e-12f;
        logSum += std::log ((double) mag);
        magSum += (double) mag;
        ++bins;
    }
    if (bins < 8 || magSum < 1.0e-8)
        return 1.f;
    const double geo = std::exp (logSum / (double) bins);
    const double ari = magSum / (double) bins;
    if (ari <= 0.0)
        return 1.f;
    return (float) std::clamp (geo / ari, 0.0, 1.0);
}

// Loudest short window. The source buffer is not written.
inline int loudestRegionStart (const float* x, int frames, double rate) noexcept
{
    if (x == nullptr || frames <= 0)
        return 0;
    const int win = std::clamp ((int) (rate * 0.25), 64, std::max (64, frames / 4));
    if (frames <= win)
        return 0;
    const int hop = std::max (1, win / 4);
    float best = -1.f;
    int at = 0;
    for (int i = 0; i + win <= frames; i += hop)
    {
        float e = 0.f;
        for (int k = 0; k < win; k += 4)
            e += std::fabs (x[i + k]);
        if (e > best)
        {
            best = e;
            at = i;
        }
    }
    return at;
}

// ---------------------------------------------------------------- VOX starts
// Vox chops started on near-silence (a file's lead-in, a breath, a fading
// tail). A "voiced" start is an onset whose next 150 ms is within 15 dB of the
// file's loudest 150 ms window and above -45 dBFS. Measured on the in-memory
// buffer at load; the source file is never written.
inline constexpr double kVoicedWindowSeconds = 0.15;
inline constexpr float kVoicedBelowLoudestDb = 15.f;
inline constexpr float kVoicedFloorDbfs = -45.f;

inline float windowRms (const float* l, const float* r, int frames, int start, int win) noexcept
{
    if (l == nullptr || frames <= 0 || win <= 0)
        return 0.f;
    start = std::clamp (start, 0, std::max (0, frames - 1));
    const int end = std::min (frames, start + win);
    double sum = 0.0;
    for (int i = start; i < end; ++i)
    {
        const double a = l[i];
        const double b = r != nullptr ? r[i] : a;
        sum += 0.5 * (a * a + b * b);
    }
    return end > start ? (float) std::sqrt (sum / (double) (end - start)) : 0.f;
}

// Fills `out` with voiced starts (sorted, >= 40 ms apart), drawn from the
// onset candidates and the loudest-region start. A candidate that opens on
// near-silence slides forward (in quarter-window steps, up to 300 ms) to where
// the voice comes in. Always returns at least one start. `floorOut` is the
// RMS floor used, for voxStart.
inline int voicedOnsets (const float* l, const float* r, int frames, double rate, const int* cand, int nCand,
                         int loudStart, int* out, int cap, float& floorOut) noexcept
{
    floorOut = 0.f;
    if (l == nullptr || frames <= 0 || cap <= 0)
        return 0;
    const int win = std::max (16, (int) (rate * kVoicedWindowSeconds));
    const int hop = std::max (4, win / 4);
    float loudest = 0.f;
    for (int i = 0; i < frames; i += hop)
        loudest = std::max (loudest, windowRms (l, r, frames, i, win));
    const float floorAbs = std::pow (10.f, kVoicedFloorDbfs / 20.f);
    const float floorRel = loudest * std::pow (10.f, -kVoicedBelowLoudestDb / 20.f);
    floorOut = std::max (floorAbs, floorRel);
    const int slideMax = (int) (rate * 0.30);
    const int minGap = std::max (1, (int) (rate * 0.04));
    int n = 0;
    auto add = [&] (int at)
    {
        for (int k = 0; k < n; ++k)
            if (std::abs (out[k] - at) < minGap)
                return;
        if (n < cap)
            out[n++] = at;
    };
    for (int c = 0; c <= nCand; ++c)
    {
        const int at0 = c < nCand ? (cand != nullptr ? cand[c] : 0) : loudStart;
        if (at0 < 0 || at0 >= frames)
            continue;
        for (int at = at0; at <= at0 + slideMax && at < frames; at += hop)
            if (windowRms (l, r, frames, at, win) >= floorOut)
            {
                // The window holds the voice; start where it actually comes in
                // (a 5 ms window within 6 dB of the floor), not on the silence before it.
                const int fine = std::max (4, (int) (rate * 0.0025));
                const int shortWin = std::max (8, (int) (rate * 0.005));
                int onset = at;
                while (onset + fine < std::min (frames, at + win)
                       && windowRms (l, r, frames, onset, shortWin) < 0.5f * floorOut)
                    onset += fine;
                add (onset);
                break;
            }
    }
    if (n == 0)
        out[n++] = std::clamp (loudStart, 0, frames - 1);
    std::sort (out, out + n);
    return n;
}

// Where a vox note starts. Sliced vox rotates through the voiced starts; an
// unsliced note keeps its own start unless that opens on near-silence, then it
// moves to the next voiced start (or the first one).
inline int voxStart (int startFrame, bool slice, int sliceIndex, const int* voiced, int nVoiced, float floorRms,
                     const float* l, const float* r, int frames, double rate) noexcept
{
    if (voiced == nullptr || nVoiced <= 0)
        return startFrame;
    if (slice)
        return voiced[((sliceIndex % nVoiced) + nVoiced) % nVoiced];
    const int win = std::max (16, (int) (rate * kVoicedWindowSeconds));
    if (windowRms (l, r, frames, startFrame, win) >= floorRms)
        return startFrame;
    for (int i = 0; i < nVoiced; ++i)
        if (voiced[i] >= startFrame)
            return voiced[i];
    return voiced[0];
}

// HITS and STAB: a short faded slice of a longer tonal file, started on an onset.
// PAD: a held loop of the loudest region when `padBed` is set.
// `flatness` < 0 means unmeasured (the name table already decided).
// Broadband audio is blocked for a chop. Hard noise names are blocked outright.
inline ChopPlan planSampleChop (int lane, int frames, double rate, float bpm, std::uint32_t seed,
                                const int* onsets, int onsetCount, int loudStart,
                                float flatness, bool hardName, bool padBed) noexcept
{
    ChopPlan plan;
    if (frames < 16 || rate < 1000.0)
        return plan;
    if (hardName || (flatness >= kTonalFlatness && flatness >= 0.f))
    {
        plan.blocked = hardName || lane == Hits || lane == Stab || padBed;
        return plan;
    }
    const int fade = std::max (8, (int) (0.004 * rate));
    if (lane == Pad && padBed)
    {
        const int win = std::clamp ((int) (rate * 1.0), fade * 8, frames);
        // At least one fade into the file, so the loop seam has frames before its start to crossfade from.
        int start = std::clamp (loudStart, std::min (fade, frames - 1), frames - 1);
        if (start + win > frames)
            start = std::max (std::min (fade, std::max (0, frames - win)), frames - win);
        plan.start = start;
        plan.end = std::min (frames, start + win);
        plan.fade = fade;
        plan.hold = true;
        return plan;
    }
    if (lane != Hits && lane != Stab)
        return plan;
    const float tempo = bpm >= 20.f ? bpm : 130.f;
    const int sixteenth = std::max (fade * 4, (int) (rate * 60.0 / (double) tempo / 4.0));
    const int steps = 1 + (int) (seed % 3u); // 1, 2, or 4 sixteenths: 1/16, 1/8, 1/4
    const int length = std::min (frames / 2, sixteenth * (steps == 3 ? 4 : steps));
    if (length < fade * 4 || frames < length + fade)
        return plan;
    // Short one-shots stay whole. Loops and long shots chop.
    if (frames < (int) (rate * kChopMinSeconds))
        return plan;
    int start = 0;
    if (onsets != nullptr && onsetCount > 0)
    {
        const int pick = (int) (seed % (std::uint32_t) onsetCount);
        for (int i = 0; i < onsetCount; ++i)
        {
            const int at = onsets[(pick + i) % onsetCount];
            if (at >= 0 && at + length <= frames)
            {
                start = at;
                break;
            }
        }
    }
    if (start + length > frames)
        start = frames - length;
    plan.start = start;
    plan.end = start + length;
    plan.fade = fade;
    return plan;
}

} // namespace rolla
