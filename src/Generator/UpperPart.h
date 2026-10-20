#pragma once

#include "Generator.h"
#include "Pattern.h"
#include "RuleComposer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

// UPPER PART over the bass riff (CHORDS knob, Oct 20). Round 8 put rootless chords on every bass note
// and Melak rejected it: "these are bad. dont sound like music" (the sound and the notes, "both...").
// Right after, he sent the synth / piano stem splits of five drops he loves (Dreams, House Party,
// Mambos, Get Busy, Tonight). Transcribed and independently verified, learn only; the detail lives
// in private anti-copy refs outside the repo. Pooled over the five:
//   - no chord stabs in any drop; 4/5 put a one-note-at-a-time hook or line over the riff, 2/5 a
//     quiet held tonic;
//   - the upper part never re-roots on the bass: it holds the tonic set (tonic 5/5, b7 5/5, 5th
//     4-5/5, b3 1/5, so the bass keeps the minor third) and the bass moving under it makes the changes;
//   - a 2- or 4-bar cell repeated exactly; the answer bar changes only its end (closed on the
//     tonic, open on the b7 or 5); one tension note (2, or b2 in Phrygian) resolves by step to the tonic;
//   - short hooks stay off the kick (on-kick share .06-.31), start with a bass note 33-47% of the
//     time, and put at least one note per phrase into a bass rest; core register E4-E5, octave
//     leaps on short notes, an octave doubling only on a few late offbeats.
// HOOK (CHORDS 1) writes that hook. STAB (CHORDS 2) gives the stab the rhythm of the DEEP chords he
// called music (a fixed 2-bar call / answer cell, ~2.5 hits a bar, every hit 2 16ths, 80%+ off the
// beat, the loudest hit the push into the next bar); DEEP's own chord writer voices it
// (rules::jazzRevoice). Every roll draws its own cell: nothing is taken from any one track.

namespace rolla
{
namespace upper
{
inline constexpr int kCell = 32; // a 2-bar cell in 16ths

inline int pcOf (int v) noexcept
{
    const int p = v % 12;
    return p < 0 ? p + 12 : p;
}

// The bass on the 16th grid: attacks, what sounds, and its pitch class (-1 for none).
struct BassGrid
{
    bool attack[kMaxSteps] {};
    int pc[kMaxSteps];
    int steps = 0;
};

inline BassGrid gridOf (const Lane& bass, int bars) noexcept
{
    BassGrid b;
    b.steps = std::clamp (bars, 1, kMaxBars) * 16;
    std::fill (b.pc, b.pc + kMaxSteps, -1);
    for (int i = 0; i < bass.count; ++i)
    {
        const Note& n = bass.notes[i];
        if (n.vel <= 2)
            continue;
        const int s = (int) std::floor (n.start + 0.01f);
        const int e = (int) std::ceil (n.start + n.length - 0.01f);
        if (s >= 0 && s < b.steps)
            b.attack[s] = true;
        for (int t = std::max (0, s); t < std::min (e, b.steps); ++t)
            b.pc[t] = pcOf (n.chord[0]);
    }
    return b;
}

// The bass pitch classes sounding over [s, e) 16ths.
inline unsigned bassOver (const BassGrid& b, float s, float e) noexcept
{
    unsigned m = 0u;
    for (int t = std::max (0, (int) std::floor (s)); t < std::min (b.steps, (int) std::ceil (e)); ++t)
        if (b.pc[t] >= 0)
            m |= 1u << (unsigned) b.pc[t];
    return m;
}

inline bool clashes (int pitch, unsigned bassPcs) noexcept
{
    const int pc = pcOf (pitch);
    return (bassPcs & ((1u << (unsigned) pcOf (pc + 1)) | (1u << (unsigned) pcOf (pc - 1)))) != 0;
}

struct Hit
{
    int slot = 0;     // 0..31 in the cell
    float len = 2.f;  // 16ths
    int pitch = 0;    // semitones over the key root at the stab base
    int dbl = 0;      // 0, or the doubling's offset (-12: an octave under)
    int vel = 100;
};

struct Cell
{
    Hit h[16];
    int n = 0;
};

// One bar's onsets. Family A: 8ths in the first half, then a chain of dotted 8ths (3 16ths apart)
// off the beat; every note gated to 2 16ths. Family B: gaps of 2 and 3 16ths around the bar from an
// offbeat start (at most one onset on a kick), short plucked notes.
inline int barRhythm (Rng& r, int family, int* slot, float* len) noexcept
{
    int n = 0;
    if (family == 0)
    {
        const int s0 = r.next() < 0.5 ? 0 : 2;
        const int chain = r.next() < 0.55 ? 7 : 6;
        for (int s = s0; s < chain - 1; s += 2)
            slot[n++] = s;
        for (int s = chain; s < 16; s += 3)
            slot[n++] = s;
        if (n > 5 && r.next() < 0.3)
            --n; // the chain's last step rests
    }
    else
    {
        for (int t = 0; t < 64; ++t)
        {
            int gaps[8];
            int ng = 0;
            int threes = r.next() < 0.7 ? 4 : 2;
            int twos = threes == 4 ? 2 : 5;
            while (threes + twos > 0)
            {
                const bool three = threes > 0 && (twos == 0 || r.next() < (double) threes / (double) (threes + twos));
                gaps[ng++] = three ? 3 : 2;
                (three ? threes : twos)--;
            }
            const int off = 1 + (int) (r.next() * 3.0);
            int pos = off;
            int onKick = 0;
            n = 0;
            for (int k = 0; k < ng; ++k)
            {
                slot[n++] = pos % 16;
                onKick += (pos % 4) == 0 ? 1 : 0;
                pos += gaps[k];
            }
            std::sort (slot, slot + n);
            if (onKick <= 1)
                break;
        }
    }
    for (int i = 0; i < n; ++i)
    {
        const int gap = (i + 1 < n ? slot[i + 1] : 16 + slot[0]) - slot[i];
        const float want = family == 0 ? 2.f : (r.next() < 0.5 ? 1.f : 1.5f);
        len[i] = std::max (0.5f, std::min (want, (float) gap - 0.25f));
    }
    return n;
}

// Pitches over the home tonic (0): the tonic, the 5th over it (and under it, rarely), the b7 under
// it, the octave, and one tension note (the 2nd, or the b2 in Phrygian) that always resolves to the
// tonic next.
inline void barPitches (Rng& r, int contour, int mode, const int* slot, int n, int* p) noexcept
{
    const int ten = mode == phrase::Phrygian ? 1 : 2;
    if (contour == 0)
    {
        // leaps (tonic and 5th across octaves), with a quick root-5th-octave flick on adjacent 16ths
        static constexpr int kLeap[4] = { 0, 7, 12, -5 };
        static constexpr float kW[4] = { 0.5f, 0.25f, 0.15f, 0.1f };
        for (int i = 0; i < n; ++i)
        {
            float x = (float) r.next();
            int k = 0;
            while (k < 3 && x > kW[k])
                x -= kW[k++];
            p[i] = kLeap[k];
        }
        for (int i = 0; i + 2 < n; ++i)
            if (slot[i + 1] == slot[i] + 1 && slot[i + 2] == slot[i] + 2)
            {
                p[i] = 0;
                p[i + 1] = 7;
                p[i + 2] = 12;
                break;
            }
    }
    else if (contour == 1)
    {
        // opens on an upper tone, falls to the tension note, resolves to the tonic and repeats it
        const int open = r.next() < 0.5 ? 7 : 10;
        for (int i = 0; i < n; ++i)
            p[i] = 0;
        p[0] = open;
        if (n > 2)
            p[1] = ten;
        else if (n > 1)
            p[1] = 0;
        if (n > 4 && r.next() < 0.5)
            p[n - 2] = open == 7 ? 7 : -2; // a second touch away before the end
    }
    else
    {
        // anchored on the tonic, stepping to the b7 under it or the 5th over it and back
        for (int i = 0; i < n; ++i)
            p[i] = 0;
        for (int i = 1; i < n; i += 2)
            p[i] = r.next() < 0.6 ? -2 : 7;
    }
    // a tension note always resolves to the tonic next
    for (int i = 0; i + 1 < n; ++i)
        if (p[i] == ten || p[i] == ten + 12)
            p[i + 1] = 0;
}

struct HookScore
{
    float onKick = 0.f;
    float withBass = 0.f;
    float sounding = 0.f;
    int restHits = 0;
    float tonic = 0.f;
};

inline HookScore scoreCell (const Cell& c, const BassGrid& b) noexcept
{
    HookScore s;
    int tonic = 0;
    for (int i = 0; i < c.n; ++i)
    {
        const Hit& h = c.h[i];
        s.onKick += (h.slot % 4) == 0 ? 1.f : 0.f;
        s.withBass += b.attack[h.slot % b.steps] ? 1.f : 0.f;
        s.restHits += b.pc[h.slot % b.steps] < 0 ? 1 : 0;
        s.sounding += h.len;
        tonic += pcOf (h.pitch) == 0 ? 1 : 0;
    }
    const float n = (float) std::max (1, c.n);
    s.onKick /= n;
    s.withBass /= n;
    s.sounding /= (float) kCell;
    s.tonic = (float) tonic / n;
    return s;
}

// HOOK: a single-voice 2-bar cell over the tonic, repeated; bar 8 closes the phrase.
inline void writeHook (Lane& lane, const Lane& bass, int mode, int bars, int key, std::uint32_t seed) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    const BassGrid bg = gridOf (bass, nb);
    // the home tonic sounds at MIDI 61-72 (the stab base is MIDI 60 + key), so the 5th over it and
    // the b7 under it keep the line in the references' core, about E4-E5
    const int k = pcOf (key);
    const int home = 60 + k < 61 ? 12 : 0;
    Rng r (seed ^ 0x484F4F4Bu);
    Cell best;
    float bestPen = 1.0e9f;
    for (int attempt = 0; attempt < 48; ++attempt)
    {
        const int family = r.next() < 0.5 ? 0 : 1;
        const int contour = (int) (r.next() * 3.0);
        int slot[16];
        float len[16];
        const int n1 = barRhythm (r, family, slot, len);
        int p1[16];
        barPitches (r, contour, mode, slot, n1, p1);
        Cell c;
        // bar 1 (call) closes on the tonic; bar 2 (answer) repeats it and ends open (b7 or 5 under),
        // or keeps only the off-kick skeleton of the call
        const bool skeleton = family == 1 && r.next() < 0.3;
        const int openEnd = r.next() < 0.6 ? -2 : 7;
        for (int bar = 0; bar < 2; ++bar)
            for (int i = 0; i < n1; ++i)
            {
                if (bar == 1 && skeleton && slot[i] < 8 && (slot[i] % 4) == 0)
                    continue;
                Hit h;
                h.slot = bar * 16 + slot[i];
                h.len = len[i];
                h.pitch = home + p1[i];
                if (i == n1 - 1)
                    h.pitch = home + (bar == 0 ? 0 : openEnd);
                c.h[c.n++] = h;
            }
        // nothing a semitone from the bass under it: such a note becomes the tonic (or the 5th)
        for (int i = 0; i < c.n; ++i)
        {
            Hit& h = c.h[i];
            for (int bar = 0; bar < nb; bar += 2)
            {
                const unsigned under = bassOver (bg, (float) (bar * 16 + h.slot), (float) (bar * 16 + h.slot) + h.len);
                if (clashes (h.pitch, under))
                    h.pitch = clashes (home, under) ? home + 7 : home;
            }
        }
        const HookScore s = scoreCell (c, bg);
        float pen = 0.f;
        pen += std::max (0.f, s.onKick - 0.30f) * 10.f;
        pen += std::max (0.f, 0.25f - s.withBass) * 6.f + std::max (0.f, s.withBass - 0.55f) * 6.f;
        pen += std::max (0.f, 0.35f - s.sounding) * 8.f;
        pen += s.restHits < 1 ? 3.f : 0.f;
        pen += std::max (0.f, 0.40f - s.tonic) * 6.f;
        pen += 0.01f * (float) attempt; // the first passing draw wins
        if (pen < bestPen)
        {
            bestPen = pen;
            best = c;
        }
        if (pen < 0.5f)
            break;
    }
    // velocities: on a kick softer; the last hit of the answer (the push into bar 1) the loudest;
    // an octave doubling under a few late offbeats (at most 30% of hits)
    int doubled = 0;
    int last = -1;
    for (int i = 0; i < best.n; ++i)
        if (best.h[i].slot >= 16)
            last = i;
    for (int i = 0; i < best.n; ++i)
    {
        Hit& h = best.h[i];
        const int s = h.slot % 16;
        h.vel = (s % 4) == 0 ? 84 : ((s & 1) ? 96 : 104);
        if (i == last)
            h.vel = 118;
        if (s >= 8 && (s & 1) && h.pitch >= home + 5 && doubled * 10 < best.n * 3 && r.next() < 0.35)
        {
            h.dbl = -12;
            ++doubled;
        }
    }
    lane.count = 0;
    for (int bar = 0; bar < nb; bar += 2)
        for (int i = 0; i < best.n && lane.count < kMaxNotes; ++i)
        {
            const Hit& h = best.h[i];
            const int at = bar * 16 + h.slot;
            if (at >= nb * 16)
                continue;
            Note n;
            n.start = (float) at;
            n.length = h.len;
            n.vel = (std::uint8_t) std::clamp (h.vel, 1, 127);
            n.flags = NoteLine;
            n.nChord = (std::uint8_t) (h.dbl != 0 ? 2 : 1);
            n.chord[0] = (std::int8_t) (h.dbl != 0 ? h.pitch + h.dbl : h.pitch);
            n.chord[1] = (std::int8_t) (h.dbl != 0 ? h.pitch : 0);
            lane.notes[lane.count++] = n;
        }
    // bar 8 closes the phrase: its last note goes home to the tonic (or rests)
    if (nb >= 8 && lane.count > 0)
    {
        Note& z = lane.notes[lane.count - 1];
        if (z.start >= (float) ((nb - 1) * 16))
        {
            if (r.next() < 0.5)
                --lane.count;
            else
            {
                z.nChord = 1;
                z.chord[0] = (std::int8_t) home;
            }
        }
    }
}

// STAB: the stab's rhythm as one fixed 2-bar call / answer cell: 2-3 hits in the call, 2-3 in the
// answer ending on the push into the next bar (the 16th or 8th before it, the loudest hit), every hit
// 2 16ths, off the beat, at most half of them with a bass attack. Pitches are a placeholder for the
// chord writer (rules::jazzRevoice) to voice.
inline void stabCell (Lane& lane, const Lane& bass, int bars, std::uint32_t seed) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    const BassGrid bg = gridOf (bass, nb);
    Rng r (seed ^ 0x53544142u);
    static constexpr int kSlots[11] = { 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 };
    static constexpr float kW[11] = { 0.5f, 0.35f, 0.15f, 0.5f, 0.35f, 0.15f, 0.5f, 0.35f, 0.15f, 0.5f, 0.35f };
    int bestSlots[8];
    int bestN = 0;
    float bestPen = 1.0e9f;
    for (int attempt = 0; attempt < 48; ++attempt)
    {
        int s[8];
        int n = 0;
        const int total = r.next() < 0.6 ? 5 : 6;
        const int push = r.next() < 0.65 ? 30 : 31; // bar 2, the & of 4 or its a
        const int inCall = total == 5 ? (r.next() < 0.5 ? 2 : 3) : 3;
        for (int bar = 0; bar < 2; ++bar)
        {
            const int want = bar == 0 ? inCall : total - inCall - 1;
            int placed = 0;
            for (int t = 0; t < 64 && placed < want; ++t)
            {
                float x = (float) r.next() * 3.85f;
                int j = 0;
                while (j < 10 && x > kW[j])
                    x -= kW[j++];
                const int at = bar * 16 + kSlots[j];
                bool ok = at < push - 2;
                for (int i = 0; i < n && ok; ++i)
                    ok = std::abs (s[i] - at) >= 3;
                if (ok)
                {
                    s[n++] = at;
                    ++placed;
                }
            }
        }
        s[n++] = push;
        std::sort (s, s + n);
        int withBass = 0;
        for (int i = 0; i < n; ++i)
            withBass += bg.attack[s[i] % bg.steps] ? 1 : 0;
        const float pen = std::max (0.f, (float) withBass / (float) n - 0.5f) * 10.f + (float) std::abs (n - total) + 0.01f * (float) attempt;
        if (pen < bestPen)
        {
            bestPen = pen;
            bestN = n;
            std::copy (s, s + n, bestSlots);
        }
        if (pen < 0.5f)
            break;
    }
    lane.count = 0;
    for (int bar = 0; bar < nb; bar += 2)
        for (int i = 0; i < bestN && lane.count < kMaxNotes; ++i)
        {
            const int at = bar * 16 + bestSlots[i];
            if (at >= nb * 16)
                continue;
            Note n;
            n.start = (float) at;
            n.length = std::min (2.f, (float) (nb * 16 - at));
            n.vel = (std::uint8_t) (i == bestN - 1 ? 120 : ((bestSlots[i] & 1) ? 96 : 104));
            n.nChord = 3;
            n.chord[0] = 3;
            n.chord[1] = 7;
            n.chord[2] = 10;
            lane.notes[lane.count++] = n;
        }
}
} // namespace upper
} // namespace rolla
