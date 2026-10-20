#pragma once

#include "PitchProbe.h"
#include "../Generator/Pattern.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// BASSMATCH (Oct 20 bass redesign; Melak: "I dont like anything youve done with the bass. maybe its
// your sound selection w the notes"). Measured on the taste renders and his library:
//   - the line always played at 24 + key (D1 in D minor) while 63% of the bass library is rooted on
//     C (mostly C2), and each note folded on its own inside +-12: 75% of notes were pitched down,
//     half of them by more than 5 semitones, so the sample played up to ~1.8x slower, darker and
//     smeared, and written octave jumps collapsed;
//   - KNB "_A_min" style names are song keys, not the shot's note: the label-over-probe rule played
//     those lines a minor third or a fourth off.
// Level 1 plays the whole line in the one octave that keeps the sample nearest its own pitch, takes
// a confident probe over a key-like name, and lets the line's written lengths through by sample
// class. One definition here for playback, the bass pick and tests. Pure functions on POD data.

namespace rolla
{
namespace bassmatch
{
inline constexpr int kLo = -24; // chord[0] range kept in a Line
inline constexpr int kHi = 36;
inline constexpr int kBins = kHi - kLo + 1;

struct Line
{
    int count[kBins] {};
    int notes = 0;
    float medLen16 = 0.f;
};

inline Line lineOf (const Lane& lane) noexcept
{
    Line l;
    float lens[kMaxNotes];
    int nl = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const Note& n = lane.notes[i];
        if (n.vel <= 2)
            continue;
        const int v = std::clamp ((int) n.chord[0], kLo, kHi);
        ++l.count[v - kLo];
        ++l.notes;
        if (nl < kMaxNotes)
            lens[nl++] = n.length;
    }
    if (nl > 0)
    {
        std::nth_element (lens, lens + nl / 2, lens + nl);
        l.medLen16 = lens[nl / 2];
    }
    return l;
}

// The root to retune from, or -1 to play as recorded. A confident single-note probe wins; a
// key-like name (Am, A_min) never overrides a probe at 0.5 or over; otherwise today's rule.
inline double resolveRoot (const PitchInfo& m, int namedRoot, bool namedIsKey) noexcept
{
    if (! m.pitched())
        return -1.0;
    // A gliding attack can pull a confident probe up to half a semitone off the note it settles on
    // (KNB_Bass_Shot_031_F#_min reads 41.46 and sounds F#2): a filename note that agrees within
    // 0.75 st gives the exact pitch, at the measured octave.
    const auto snapped = [&] (double r) noexcept
    {
        if (namedRoot < 0)
            return r;
        const double named = (double) namedRoot + 12.0 * std::round ((r - (double) namedRoot) / 12.0);
        return std::fabs (named - r) <= 0.75 ? named : r;
    };
    if (m.conf >= 0.80f && ! m.isChord)
        return snapped ((double) m.rootPitch);
    if (namedIsKey && m.conf >= 0.50f)
        return snapped ((double) m.rootPitch);
    const bool hasName = namedRoot >= 0;
    const bool named = hasName && pitchprobe::pcDistance ((double) namedRoot, (double) m.rootPitch) <= 1.0;
    if (m.conf >= kPitchConfMin && (! hasName || named))
        return (double) m.rootPitch;
    if (hasName)
        return (double) namedRoot + 12.0 * std::round (((double) m.rootPitch - (double) namedRoot) / 12.0);
    return -1.0;
}

// Cost of a shift: down hurts more (slower, darker), up to a 5th is cheap, past it rises.
inline double cost (double s) noexcept
{
    if (s < 0.0)
        return 1.35 * -s;
    if (s <= 7.0)
        return s;
    return s + 0.5 * (s - 7.0);
}

inline constexpr double kWinLo = -12.0; // per-note window (his FL sampled bass runs -9..+20)
inline constexpr double kWinHi = 19.0;

// Octave for the whole line (k octaves on top of the written register), ties to the higher k.
inline int octaveFor (const Line& l, int base, double root) noexcept
{
    int best = 0;
    double bestCost = 1.0e18;
    for (int k = 3; k >= -3; --k)
    {
        double c = 0.0;
        for (int b = 0; b < kBins; ++b)
        {
            if (l.count[b] == 0)
                continue;
            const double s = (double) (base + b + kLo + 12 * k) - root;
            c += (double) l.count[b] * (cost (s) + (s < kWinLo || s > kWinHi ? 12.0 : 0.0));
        }
        if (c < bestCost - 1.0e-9)
        {
            bestCost = c;
            best = k;
        }
    }
    return best;
}

inline double noteShift (int midi, int k, double root) noexcept
{
    double s = (double) (midi + 12 * k) - root;
    while (s > kWinHi)
        s -= 12.0;
    while (s < kWinLo)
        s += 12.0;
    return s;
}

// Shift of the line's most played note (its anchor).
inline double anchorShift (const Line& l, int base, int k, double root) noexcept
{
    int top = -1;
    for (int b = 0; b < kBins; ++b)
        if (l.count[b] > 0 && (top < 0 || l.count[b] > l.count[top]))
            top = b;
    return top < 0 ? 0.0 : noteShift (base + top + kLo, k, root);
}

// How long the sample rings at the speed it is played (ringMs at the recorded speed).
inline float playedRingMs (float ringMs, double shift) noexcept
{
    return ringMs * (float) std::pow (2.0, -shift / 12.0);
}

enum class Decay
{
    Pluck, // under 250 ms: rings to its own end or the next note
    Short, // 250-600 ms: the written length + half a 16th
    Held   // over 600 ms: the written length
};

inline Decay decayOf (float playedMs) noexcept
{
    return playedMs < 250.f ? Decay::Pluck : (playedMs < 600.f ? Decay::Short : Decay::Held);
}
} // namespace bassmatch
} // namespace rolla
