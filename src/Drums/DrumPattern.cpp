#include "DrumPattern.h"

#include <algorithm>
#include <cmath>

namespace rolla
{
namespace
{

int velFromDb (float db, int full = 100) noexcept
{
    const float g = std::pow (10.f, db / 20.f);
    return std::clamp ((int) std::lround ((float) full * g), 1, 127);
}

bool stabCovers (const Lane& stab, int step) noexcept
{
    const float t = (float) step;
    for (int i = 0; i < stab.count; ++i)
    {
        const float a = stab.notes[i].start;
        const float b = a + stab.notes[i].length;
        if (t + 0.45f >= a && t <= b + 0.05f)
            return true;
    }
    return false;
}

} // namespace

void writeDrumPattern (Groove& groove, const Settings& settings, Rng& rng)
{
    auto& d = groove.drums;
    clearDrums (d);

    Settings s = settings;
    s.bars = std::clamp (s.bars, 2, kMaxBars);
    if (s.bars != 2 && s.bars != 4 && s.bars != 8)
        s.bars = 8;
    s.busy = std::clamp (s.busy, 0.f, 1.f);
    const int steps = s.bars * 16;
    const bool gaskin = s.dialect == Gaskin;
    const Lane& stab = groove.lane[Stab];

    auto jitter = [&] (int vel) noexcept
    {
        const float db = (float) rng.next() * 2.f - 1.f;
        const float g = std::pow (10.f, db / 20.f);
        return (std::uint8_t) std::clamp ((int) std::lround ((float) vel * g), 1, 127);
    };

    for (int t = 0; t < steps; ++t)
    {
        const int s16 = t % 16;
        const int bar = t / 16;
        const bool beat = s16 == 0 || s16 == 4 || s16 == 8 || s16 == 12;
        if (beat)
        {
            d.vel[Kick][t] = 100;
            d.accent[t] = 1;
        }
        if (s16 == 4 || s16 == 12)
            d.vel[Clap][t] = 100;

        if (gaskin)
        {
            if (beat)
                d.vel[HatC][t] = jitter (velFromDb (-12.f));
            if (s16 == 2 || s16 == 6 || s16 == 10 || s16 == 14)
            {
                int open = 100;
                if (s.bars >= 8 && bar == 7)
                    open = velFromDb (2.f);
                d.vel[HatO][t] = jitter (open);
                d.vel[Ride][t] = jitter (100);
            }
            if (s16 == 15 && ! stabCovers (stab, t))
                d.vel[Rim][t] = jitter (velFromDb (-8.f));
        }
        else
        {
            // "e" −22 dB, "&" open hat, "a" −12 dB. Slots are 1-based 16ths.
            if (s16 == 1 || s16 == 5 || s16 == 9 || s16 == 13)
                d.vel[HatC][t] = jitter (velFromDb (-22.f));
            if (s16 == 3 || s16 == 7 || s16 == 11 || s16 == 15)
                d.vel[HatC][t] = jitter (velFromDb (-12.f));
            if (s16 == 2 || s16 == 6 || s16 == 10 || s16 == 14)
            {
                int open = 100;
                if (s.bars >= 8 && bar == 7)
                    open = velFromDb (2.f);
                d.vel[HatO][t] = jitter (open);
            }
            if ((s16 == 7 || s16 == 15) && ! stabCovers (stab, t))
                d.vel[Rim][t] = jitter (velFromDb (-5.f));
            // Shaker 16ths. Levels cycle −5 / −17 / 0 / −7 inside the beat.
            // "e" and "a" are odd 16ths, so the engine swing delays them.
            {
                static const float shakerDb[4] = { -5.f, -17.f, 0.f, -7.f };
                d.vel[Perc][t] = (std::uint8_t) jitter (velFromDb (shakerDb[s16 & 3]));
            }
            if (s.busy > 0.55f && s16 == 5 && (bar % 2) == 1 && rng.next() < (double) s.busy)
                if (d.vel[Rim][t] == 0)
                    d.vel[Rim][t] = jitter (velFromDb (-10.f));
        }

        // Tom on slot 15 of bars 4 and 8 (1-based).
        if (s16 == 14 && (bar == 3 || bar == 7))
            d.vel[TomLow][t] = jitter (96);
    }

    if (s.fill && steps >= 16)
    {
        const int b0 = (s.bars - 1) * 16;
        for (int s16 = 8; s16 < 16; ++s16)
            d.vel[Snare][b0 + s16] = (std::uint8_t) std::min (127, 72 + (s16 - 8) * 6);
        d.vel[TomMid][b0 + 10] = 100;
        d.vel[TomLow][b0 + 13] = std::max<std::uint8_t> (d.vel[TomLow][b0 + 13], 104);
        if (d.vel[Ride][b0 + 14] == 0)
            d.vel[Ride][b0 + 14] = 72;
    }
}

} // namespace rolla
