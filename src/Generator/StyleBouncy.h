#pragma once

// BOUNCY style pass. Targets mirror data/genre/style_rules.json styles.BOUNCY
// (abstract statistics from five references, low confidence). They live in one
// struct so a later GenreTargets loader can fill them from that file instead of
// these defaults. Everything here is a seeded tendency, never a stored pattern.
//
//  - pulse share 0.25 (the rest bounce), from flavours
//  - bass on the kick: target 32%, HARD <= 35% per roll (H2_low_end)
//  - hat 16th swing target 56.7% (band 47.7-65.7); the swing parameter tops
//    out at 57%, so the default is the target clamped into 50-57
//  - high-band onsets ~10.3 a bar: ghost closed hats on the e / a, which is
//    where the swing shows
//  - duck depth -7.5 dB (lightest of the four styles)
//  - bar 8: pickup into bar 1 0.25, octave lift 0.2

#include "Generator.h"
#include "Pattern.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rolla::bouncy
{

struct Targets
{
    // flavours
    float pulseShare = 0.25f;
    // groove
    float hatSwingMin = 0.477f, hatSwing = 0.567f, hatSwingMax = 0.657f;
    float hiOnsetsMin = 7.24f, hiOnsets = 10.28f, hiOnsetsMax = 12.76f;
    float ghostVelMin = 25.f, ghostVel = 40.f, ghostVelMax = 55.f;
    float duckDb = 7.5f; // duck_depth_db_median -7.5, as a positive depth
    // bass
    float bassOnKick = 0.322f, bassOnKickHardMax = 0.35f;
    float bassOnsetsPerBar = 5.0f;
    // bar 8 palette
    float pickup = 0.25f, octaveLift = 0.2f;
    // bar 4 answer probability (the shipped answer pass changes density more often; see fixAnswers)
    float bar4Answer = 0.7f;
};

inline constexpr Targets kTargets {};

// The swing parameter range is 0.50-0.57 (PluginProcessor, presets loader).
inline constexpr float defaultSwing() noexcept
{
    return kTargets.hatSwing < 0.50f ? 0.50f : (kTargets.hatSwing > 0.57f ? 0.57f : kTargets.hatSwing);
}

// Per-roll on-kick share: centred just under the 32% target and kept clear of
// the 35% hard cap, because later passes (pickup, answers) only add off-kick
// notes and the twin nudge only moves notes off the kick.
inline float kickShareFor (Rng& r) noexcept
{
    const float hi = std::min (kTargets.bassOnKick + 0.02f, kTargets.bassOnKickHardMax - 0.01f);
    return hi - 0.08f * (float) r.next();
}

// Kick-slot hits for a bar of n notes at share q (stochastic rounding), at most two.
inline int kicksForBar (Rng& r, int n, float q) noexcept
{
    const float want = q * (float) n;
    int k = (int) std::floor (want);
    if ((float) r.next() < want - (float) k)
        ++k;
    return std::clamp (k, 0, 2);
}

inline bool hiBandAt (const DrumLane& d, int t) noexcept
{
    return d.vel[HatC][t] > 0 || d.vel[HatO][t] > 0 || d.vel[Ride][t] > 0 || d.vel[Perc][t] > 0;
}

inline float hiOnsetsPerBar (const DrumLane& d, int bars) noexcept
{
    int n = 0;
    for (int t = 0; t < bars * 16; ++t)
        n += hiBandAt (d, t) ? 1 : 0;
    return bars > 0 ? (float) n / (float) bars : 0.f;
}

// Ghost closed hats on the e / a until each bar has about the target number of
// high-band onsets. BUSY moves the target (+-3 across the knob), each roll and
// bar wobble a little, and the "a" is likelier than the "e" (refs: a 0.21 vs e
// 0.12 of in-beat onsets). Odd 16ths take the engine swing.
inline void ghostHats (DrumLane& d, int bars, std::uint32_t seed, float busy)
{
    Rng r ((seed ? seed : 1u) * 0x9E3779B1u ^ 0xB0C47u);
    const float rollBias = ((float) r.next() - 0.5f) * 1.2f;
    const float target = std::clamp (kTargets.hiOnsets + (busy - 0.4f) * 5.f + rollBias, kTargets.hiOnsetsMin,
                                     kTargets.hiOnsetsMax);
    for (int bar = 0; bar < bars; ++bar)
    {
        int have = 0;
        for (int s = 0; s < 16; ++s)
            have += hiBandAt (d, bar * 16 + s) ? 1 : 0;
        int need = (int) std::lround (target + ((float) r.next() - 0.5f)) - have;
        for (int guard = 0; guard < 16 && need > 0; ++guard)
        {
            float w[16] {};
            float sum = 0.f;
            for (int s = 1; s < 16; s += 2)
                if (! hiBandAt (d, bar * 16 + s))
                {
                    w[s] = (s & 3) == 3 ? 1.0f : 0.6f;
                    sum += w[s];
                }
            if (sum <= 0.f)
                break;
            float x = (float) r.next() * sum;
            int pick = -1;
            for (int s = 1; s < 16 && pick < 0; s += 2)
            {
                x -= w[s];
                if (w[s] > 0.f && x <= 0.f)
                    pick = s;
            }
            if (pick < 0)
                pick = 15;
            // e quieter than a, inside the 25-55 ghost band.
            const float base = (pick & 3) == 3 ? kTargets.ghostVel + 6.f : kTargets.ghostVel - 8.f;
            const float v = std::clamp (base + ((float) r.next() - 0.5f) * 12.f, kTargets.ghostVelMin, kTargets.ghostVelMax);
            d.vel[HatC][bar * 16 + pick] = (std::uint8_t) std::lround (v);
            --need;
        }
    }
}

} // namespace rolla::bouncy
