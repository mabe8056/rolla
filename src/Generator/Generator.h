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
void generate (Groove& g, const Settings& settings, const std::uint32_t seeds[NumParts], std::uint32_t partMask);

// OC-style bass skeleton similarity in [0, 1]. A pair at or above 0.55 is the
// same skeleton (GENERATOR_RULES v2 §4.2). Deterministic and history-free.
float bassSkeletonSimilarity (const Groove& a, const Groove& b) noexcept;

} // namespace rolla
