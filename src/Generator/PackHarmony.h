#pragma once

#include "Generator.h"
#include "Pattern.h"
#include "PhraseLock.h"
#include "RuleComposer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// Pack-learned harmony (Melak, Oct 20: "Use the midis to learn but dont import them").
//
// The numbers below are pooled counts over the MIDI loops in his licensed sample packs (497 loops:
// 213 bass, 136 chord, 148 lead; 76 of them polyphonic chord loops, 28 of those with a key in the
// filename; 125 keyed basslines). No loop, progression, voicing or rhythm of any file is stored or
// replayed: only how often things happen, summed over every file. Every roll still draws its own
// chords from its own seed.
//
// What the pack chord loops do that ROLLA's stab did not (ROLLA: 160 rolls, Oct 20 HEAD):
//   - chords are triads and 7ths: 3-4 notes in 86% of chord events. ROLLA's palette (read from
//     reference audio) was 63% two-note dyads, mostly bare 4ths, 5ths and 2nds;
//   - tonic-centred: i on 54% of bars, iv 20%, bVII 16%, v 4%, bVI 4%. 57% of loops stay on one
//     chord, 32% move between two, 11% use three; the first chord is i in 75%;
//   - moves: iv <-> bVII and i <-> iv lead, then i <-> v and i <-> bVII;
//   - the top voice moves by a step (1-2 semitones) in 54% of chord changes;
//   - triads 85%, 7ths 13%, add9 2%; minor 84%, major 15%;
//   - basslines move their root bar to bar 46% of the time: the bass follows the chords.
//
// harmony 1: the stab's hits keep their rhythm; their pitches become the roll's chord of the bar,
// voiced as a triad / 7th in the stab band with the top voice led by step. The pad and the chord
// hits follow the stab already (ChordLoop.h).
// harmony 2: as 1, and the bass and pluck riffs move with the chords (diatonic, per bar), the way
// a pack bassline changes root under a chord change.

namespace rolla
{
namespace pack_harmony
{
inline constexpr int kSteps = 7; // scale steps: i ii III iv v VI VII

// P(the loop uses 1 / 2 / 3 chords).
inline constexpr float kChordCount[3] = { 0.57f, 0.32f, 0.11f };
// First chord of the loop, counts by scale step.
inline constexpr float kFirst[kSteps] = { 21.f, 0.f, 0.f, 4.f, 1.f, 1.f, 1.f };
// Chord moves, counts by scale step (from -> to), every keyed polyphonic loop pooled.
inline constexpr float kMove[kSteps][kSteps] = {
    //  i    ii   III   iv    v    VI   VII
    { 0.f, 1.f, 1.f, 11.f, 5.f, 0.f, 3.f },  // i
    { 1.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f },   // ii
    { 0.f, 0.f, 0.f, 0.f, 0.f, 1.f, 0.f },   // III
    { 11.f, 0.f, 0.f, 0.f, 0.f, 0.f, 20.f }, // iv
    { 5.f, 0.f, 0.f, 0.f, 0.f, 2.f, 0.f },   // v
    { 1.f, 1.f, 0.f, 0.f, 2.f, 0.f, 0.f },   // VI
    { 3.f, 0.f, 0.f, 20.f, 0.f, 0.f, 0.f },  // VII
};
// Share of bars on each step (the smoothing prior under the counts above).
inline constexpr float kUnigram[kSteps] = { 0.538f, 0.012f, 0.006f, 0.202f, 0.04f, 0.04f, 0.162f };
// Chord colour: a 7th on 13% of chords, an added 9th on 2% (DEEP, the jazz-house style, x3).
inline constexpr float kSeventh = 0.13f;
inline constexpr float kNinth = 0.02f;

struct Plan
{
    int chords = 1;
    int step[kMaxBars] {};
    bool seventh[kMaxBars] {};
    bool ninth[kMaxBars] {};
};

inline int pcOf (int v) noexcept
{
    const int p = v % 12;
    return p < 0 ? p + 12 : p;
}

// A triad on this step that is diminished in the mode (aeolian ii, dorian vi, phrygian v) is out.
inline bool stepLegal (int step, int mode) noexcept
{
    const int r = phrase::degreeToSemis (step, mode);
    const int t = phrase::degreeToSemis (step + 2, mode) - r;
    const int f = phrase::degreeToSemis (step + 4, mode) - r;
    return ! (t == 3 && f == 6);
}

inline int pickWeighted (const float* w, int n, double u) noexcept
{
    float sum = 0.f;
    for (int i = 0; i < n; ++i)
        sum += std::max (0.f, w[i]);
    if (sum <= 0.f)
        return -1;
    float acc = 0.f;
    const float x = (float) u * sum;
    for (int i = 0; i < n; ++i)
    {
        acc += std::max (0.f, w[i]);
        if (x < acc)
            return i;
    }
    return n - 1;
}

// One plan per roll: how many chords, which, and on which bars (one chord a bar, the pack loops'
// median rate; two chords alternate A B, three go A B C B or A B A C over 4 bars).
inline Plan draw (std::uint32_t seed, int mode, int bars, bool colourful) noexcept
{
    Plan p;
    Rng r (seed ^ 0x9A3C5u);
    const int nb = std::clamp (bars, 1, kMaxBars);
    float cw[3] = { kChordCount[0], kChordCount[1], kChordCount[2] };
    const int count = 1 + std::max (0, pickWeighted (cw, 3, r.next()));

    float w[kSteps];
    for (int s = 0; s < kSteps; ++s)
        w[s] = stepLegal (s, mode) ? kFirst[s] + 0.5f * kUnigram[s] : 0.f;
    // A one-chord loop is the key's own chord (a pack file's key is its vamp's chord).
    const int drawn = std::max (0, pickWeighted (w, kSteps, r.next()));
    const int a = count == 1 ? 0 : drawn;
    auto next = [&] (int from, int avoid) noexcept
    {
        float mw[kSteps];
        for (int s = 0; s < kSteps; ++s)
            mw[s] = (s != from && s != avoid && stepLegal (s, mode)) ? kMove[from][s] + 0.3f * kUnigram[s] : 0.f;
        return pickWeighted (mw, kSteps, r.next());
    };
    int cycle[4] = { a, a, a, a };
    int len = 1;
    if (count >= 2)
    {
        const int b = next (a, -1);
        if (b >= 0)
        {
            cycle[0] = a;
            cycle[1] = b;
            len = 2;
            p.chords = 2;
            if (count >= 3)
            {
                const int c = next (b, a);
                if (c >= 0)
                {
                    // Back to b when c leads there, else the tonic chord again before c.
                    const bool cToB = kMove[c][b] > 0.f;
                    cycle[2] = cToB ? c : a;
                    cycle[3] = cToB ? b : c;
                    len = 4;
                    p.chords = 3;
                }
            }
        }
    }
    const float sev = colourful ? std::min (0.6f, 3.f * kSeventh) : kSeventh;
    const float nin = colourful ? 3.f * kNinth : kNinth;
    bool chordSev[kSteps] {};
    bool chordNin[kSteps] {};
    for (int s = 0; s < kSteps; ++s)
    {
        chordSev[s] = r.next() < (double) sev;
        chordNin[s] = ! chordSev[s] && r.next() < (double) nin;
    }
    for (int bar = 0; bar < nb; ++bar)
    {
        p.step[bar] = cycle[bar % len];
        p.seventh[bar] = chordSev[p.step[bar]];
        p.ninth[bar] = chordNin[p.step[bar]];
    }
    return p;
}

// The chord's pitch classes above the key root: triad, + 7th, or + added 9th.
inline int chordTones (const Plan& p, int bar, int mode, int* pcs) noexcept
{
    const int s = p.step[bar];
    int n = 0;
    pcs[n++] = pcOf (phrase::degreeToSemis (s, mode));
    pcs[n++] = pcOf (phrase::degreeToSemis (s + 2, mode));
    pcs[n++] = pcOf (phrase::degreeToSemis (s + 4, mode));
    if (p.seventh[bar])
        pcs[n++] = pcOf (phrase::degreeToSemis (s + 6, mode));
    else if (p.ninth[bar])
        pcs[n++] = pcOf (phrase::degreeToSemis (s + 1, mode));
    return n;
}

struct Voiced
{
    int n = 0;
    int t[4] {};
};

// Close voicings of the chord inside [lo, hi] (every inversion, every octave), the top voice led
// from `prevTop` by a step where it can (none: the top sits near the band's upper third).
inline Voiced voice (const int* pcs, int n, int lo, int hi, int prevTop) noexcept
{
    Voiced best;
    float bestCost = 1.0e9f;
    for (int inv = 0; inv < n; ++inv)
        for (int oct = -2; oct <= 4; ++oct)
        {
            Voiced v;
            v.n = n;
            int last = pcs[inv] + 12 * oct;
            v.t[0] = last;
            for (int k = 1; k < n; ++k)
            {
                int t = pcs[(inv + k) % n] + 12 * oct;
                while (t <= last)
                    t += 12;
                v.t[k] = t;
                last = t;
            }
            if (v.t[0] < lo || v.t[n - 1] > hi)
                continue;
            const int top = v.t[n - 1];
            float cost = 0.f;
            if (prevTop >= 0)
            {
                const int d = std::abs (top - prevTop);
                cost = d == 0 ? 0.5f : d <= 2 ? 0.f : d <= 4 ? 1.f : 2.f + 0.2f * (float) d;
            }
            else
            {
                cost = 0.2f * std::fabs ((float) top - (float) (lo + (2 * (hi - lo)) / 3));
            }
            float mean = 0.f;
            for (int k = 0; k < n; ++k)
                mean += (float) v.t[k] / (float) n;
            cost += 0.08f * std::fabs (mean - 0.5f * (float) (lo + hi));
            if (cost < bestCost)
            {
                bestCost = cost;
                best = v;
            }
        }
    return best;
}

// Bass pitch classes sounding over [a, b).
inline std::uint32_t bassUnder (const Lane& bass, float a, float b) noexcept
{
    std::uint32_t m = 0;
    for (int i = 0; i < bass.count; ++i)
    {
        const Note& bn = bass.notes[i];
        if (bn.vel > 2 && bn.start < b - 1.0e-3f && a < bn.start + bn.length - 1.0e-3f)
            m |= 1u << (unsigned) pcOf (bn.chord[0]);
    }
    return m;
}

// harmony 1: every stab hit takes its bar's chord, voiced once per bar with the top voice led from
// the bar before. A tone a semitone from the bass under the hit drops out while three remain.
inline void voiceStab (Lane& stab, const Plan& p, int mode, const Lane& bass, int bars, int lo, int hi) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    Voiced perBar[kMaxBars];
    int prevTop = -1;
    for (int bar = 0; bar < nb; ++bar)
    {
        int pcs[4];
        const int n = chordTones (p, bar, mode, pcs);
        perBar[bar] = voice (pcs, n, lo, hi, prevTop);
        if (perBar[bar].n > 0)
            prevTop = perBar[bar].t[perBar[bar].n - 1];
    }
    for (int i = 0; i < stab.count; ++i)
    {
        Note& n = stab.notes[i];
        if (n.vel <= 2)
            continue;
        const int bar = std::clamp (phrase_lock::barOf (n.start), 0, nb - 1);
        const Voiced& v = perBar[bar];
        if (v.n <= 0)
            continue;
        const std::uint32_t under = bassUnder (bass, n.start, n.start + std::max (0.25f, n.length));
        int out[4];
        int m = 0;
        for (int k = 0; k < v.n; ++k)
        {
            const int pc = pcOf (v.t[k]);
            const bool rub = (under & (1u << (unsigned) ((pc + 1) % 12))) != 0 || (under & (1u << (unsigned) ((pc + 11) % 12))) != 0;
            if (rub && v.n - (k - m) > 3)
                continue;
            out[m++] = v.t[k];
        }
        n.nChord = (std::uint8_t) std::clamp (m, 1, 4);
        for (int k = 0; k < 4; ++k)
            n.chord[k] = (std::int8_t) (k < m ? out[k] : 0);
    }
}

// harmony 2: a riff moves with the chords, bar by bar, along the mode (a note on the k-th degree
// goes to the (k + step)-th), by the nearer direction (bVII is a step down, not a 7th up). Tones
// outside the mode move by the chord root's interval.
inline void follow (Lane& lane, const Plan& p, int mode, int bars) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        const int bar = std::clamp (phrase_lock::barOf (n.start), 0, nb - 1);
        int k = p.step[bar];
        if (k == 0)
            continue;
        int shift = phrase::degreeToSemis (k, mode);
        if (shift > 6)
        {
            k -= 7;
            shift -= 12;
        }
        const int nc = std::clamp ((int) n.nChord, 1, 4);
        for (int c = 0; c < nc; ++c)
        {
            const int v = n.chord[c];
            const int oct = (v >= 0) ? v / 12 : -((-v + 11) / 12);
            const int pc = v - 12 * oct;
            int deg = -1;
            for (int d = 0; d < 7; ++d)
                if (phrase::degreeToSemis (d, mode) == pc)
                    deg = d;
            const int to = deg >= 0 ? 12 * oct + phrase::degreeToSemis (deg + k, mode) : v + shift;
            n.chord[c] = (std::int8_t) std::clamp (to, -60, 60);
        }
    }
}
} // namespace pack_harmony
} // namespace rolla
