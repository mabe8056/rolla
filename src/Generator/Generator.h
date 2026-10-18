#pragma once

#include "Pattern.h"
#include <cstdint>

namespace rolla
{

// Deterministic mulberry32, bit-compatible with the mockup's JS generator.
struct Rng
{
    std::uint32_t s = 1;

    explicit Rng (std::uint32_t seed) noexcept : s (seed == 0 ? 1u : seed) {}

    std::uint32_t nextU() noexcept
    {
        s += 0x6D2B79F5u;
        std::uint32_t t = s ^ (s >> 15);
        t *= (1u | s);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return t ^ (t >> 14);
    }

    double next() noexcept
    {
        return (double) nextU() / 4294967296.0;
    }
};

// partMask bit N regenerates that part. Other parts are kept and only clamped
// to the new bar length. A regenerated stab is placed in the holes of the bass
// left in `g`, and drums sit under that pair, so a shuffle fits the kept lanes.
// Each draw builds an 8-bar phrase from typed rhythm cells, phrase plans, and
// a contour. Only four things reject a candidate: out-of-key notes, a bass
// attack on a kick with the low end stacked past 35% of the onsets, an empty
// bar in a lane that is supposed to play, and a banned figure. Slot weights,
// density, stab-vs-bass placement, plans, contours, and rests are a penalty.
// Six candidates are ranked by that penalty and one is sampled with a
// temperature that rises with freedom. The caller seed is what lands on the lane.
// The finished roll then passes the originality floor (Originality.h): a lane
// that matches a banned figure or the loaded reference index is redrawn from
// its seed + k, up to 12 rolls; a composition that never clears is dropped and
// composed again from a derived seed (3 compositions in all). A copy is never
// returned: if nothing clears, generate() logs it to stderr, leaves g exactly
// as passed in (the caller keeps its previous roll) and returns false.
bool generate (Groove& g, const Settings& settings, const std::uint32_t seeds[NumParts], std::uint32_t partMask);

// Originality floor budget: rolls per composition, compositions per generate().
inline constexpr int kOrigAttempts = 12;
inline constexpr int kOrigRounds = 3;

// Seed a refused composition restarts from in round 1..kOrigRounds-1 (round 0
// uses the caller seed). A fixed hash, so a refusal retry is deterministic.
std::uint32_t originalityRecomposeSeed (std::uint32_t seed, int part, int round) noexcept;

// The one mode a roll uses for bass, stab, pad, pluck and hits
// (phrase::Dorian / Aeolian / Phrygian). JACKIN is always phrygian; DEEP
// follows its phrase bank; ROLLER/BOUNCY draw from DARK and the bass seed.
// After generation, generate() snaps the regenerated lanes to this mode, caps
// the bass b2 share of sounding time (phrygian 30%; otherwise one-16th passing
// notes only, ROLLER 12%, DEEP 6%, JACKIN 6%, BOUNCY 3%), and removes every
// bass vs stab/pad semitone clash: up to 8 deterministic resamples per note,
// then the nearest sounding chord tone. Every rolled lane then has one
// note-on per (onset, pitch) and no overlapping same-pitch notes.
int rollMode (const Settings& settings, std::uint32_t bassSeed) noexcept;

// True for the ~30% of bass seeds whose roll ends with a bar-8 pickup:
// 1-3 bass notes on 16ths 13-16 of the last bar leading back to the bar-1
// root. generate() skips it only when no shape fits (kick step or a clash).
bool rollWantsPickup (std::uint32_t bassSeed) noexcept;

// OC-style bass skeleton similarity in [0, 1]. A pair at or above 0.55 is the
// same skeleton (GENERATOR_RULES v2 §4.2). Deterministic and history-free.
float bassSkeletonSimilarity (const Groove& a, const Groove& b) noexcept;

} // namespace rolla
