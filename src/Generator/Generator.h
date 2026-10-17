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
// to the new bar length. Stab / hits / vox are written against the bass (and
// stab) that are in `g` after this call, so a shuffle fits the kept lanes.
void generate (Groove& g, const Settings& settings, const std::uint32_t seeds[NumParts], std::uint32_t partMask);

} // namespace rolla
