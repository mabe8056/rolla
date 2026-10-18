#pragma once

// JACKIN style pass. generate() reaches this for Gaskin only (loopify and
// writeDrums gate it), so ROLLER / DEEP / BOUNCY output is unchanged.
//
// Targets are the abstract per-style bands in data/genre/style_rules.json
// (styles.JACKIN): no note data, no reference names. They sit in one struct so
// a later pass can fill it from that file instead of these constants.
//
//  - shapePair: the 2-bar hook loopify repeats is thinned toward ~4.4 bass
//    onsets a bar (BUSY scales it) and about a fifth of the onsets move onto a
//    kick. A note is removed by letting the one before it ring on, and a kick
//    move keeps the note's end, so the bass still sounds as much of the bar.
//    A change is taken only if it adds no 16th where A and B sound the same
//    pitch, so call-and-answer and the 1-bar repeat gate cannot get worse.
//  - styleDrums: the raw jacking top end. Open hats on most offbeat 8ths,
//    ghost closed hats on e / a at a per-roll rate, a few quiet beat hats and
//    the odd extra open hat. This is the JACKIN branch of writeDrumPattern
//    (DrumPattern.cpp) as seeded tendencies; that file is not linked into the
//    no-JUCE targets (style test, variety, dump), so it is not called directly.
//
// All choices are weighted draws from the lane seed. Header-only.

#include "RollerPass.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rolla::jackin
{

struct Targets
{
    // bass
    float bassOnsetsMin = 2.66f, bassOnsets = 4.44f, bassOnsetsMax = 5.79f; // per bar
    float onKick = 0.189f;        // share of bass onsets on a kick
    float onKickHardMax = 0.35f;  // hard rule
    float b2MaxShare = 0.30f;     // b2 is genre colour here, capped at 30% of bass time
    // groove
    float swingMin = 0.498f, swing = 0.565f, swingMax = 0.659f;
    float swingFloor = 0.52f;     // no straight hats in a jack groove
    float hiBand = 10.f;          // hat / perc onsets per bar
    float duckDb = 9.f;           // sidechain duck depth on bass (p50 -9 dB)
    float ghostMin = 0.25f, ghost = 0.40f, ghostMax = 0.55f; // ghost hat per e / a 16th
    int ghostVelMin = 30, ghostVelMax = 60;
    float openHatOffbeat = 0.9f;  // open hat on each offbeat 8th
    float openHatExtra = 0.2f;    // per bar: an extra open hat on slot 8 or 16 (1-based)
    float bar4Answer = 0.9f;      // reference; differBar4 / answerBar own the draw
};

inline constexpr Targets kTargets {};

// The SWING parameter tops out at 0.57, so JACKIN's default is the band's p50
// clamped into the knob. Presets.cpp and resources/presets.json use this value.
inline constexpr float kDefaultSwing = 0.565f;
static_assert (kDefaultSwing >= kTargets.swingFloor && kDefaultSwing <= 0.57f, "JACKIN swing outside the knob");

namespace style_detail
{
inline int pcJ (int semi) noexcept
{
    return ((semi % 12) + 12) % 12;
}

inline bool kickJ (int slot) noexcept
{
    return (slot & 3) == 0;
}

// BUSY's bass scale, the same curve applyBusyDensity uses: 0.75 / 0.99 at the
// shipped 0.40 / 1.35.
inline float busyScale (float busy) noexcept
{
    return 0.75f + 0.60f * std::clamp (busy, 0.f, 1.f);
}

inline void sortPair (Lane& lane) noexcept
{
    std::sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
}

// Sounding pitch per 16th of the looped pair (-99 is a rest). B's tail wraps
// onto A's front, under A's own notes, the way the loop plays it.
inline void pairTokens (const Lane& pair, int tok[32]) noexcept
{
    for (int s = 0; s < 32; ++s)
        tok[s] = -99;
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < pair.count; ++i)
        {
            const Note& n = pair.notes[i];
            const int a = (int) std::lround (n.start);
            const int b = (int) std::lround (n.start + n.length);
            for (int t = a; t < b && t < a + 32; ++t)
            {
                if ((pass == 0) != (t >= 32))
                    continue;
                tok[((t % 32) + 32) % 32] = n.chord[0];
            }
        }
}

struct Matches
{
    int pc = 0;
    int exact = 0;
};

// 16ths where A and B sound the same thing. Bar n vs n+1 is always A vs B in
// the A B A B loop, so this is what call-and-answer and the 1-bar repeat see.
inline Matches pairMatches (const Lane& pair) noexcept
{
    int tok[32];
    pairTokens (pair, tok);
    Matches m;
    for (int s = 0; s < 16; ++s)
    {
        const int a = tok[s], b = tok[s + 16];
        m.exact += a == b ? 1 : 0;
        const int ka = a == -99 ? -99 : pcJ (a);
        const int kb = b == -99 ? -99 : pcJ (b);
        m.pc += ka == kb ? 1 : 0;
    }
    return m;
}

inline float b2Share (const Lane& pair) noexcept
{
    float all = 0.f, b2 = 0.f;
    for (int i = 0; i < pair.count; ++i)
    {
        const float len = std::max (0.f, pair.notes[i].length);
        all += len;
        b2 += pcJ (pair.notes[i].chord[0]) == 1 ? len : 0.f;
    }
    return all > 0.f ? b2 / all : 0.f;
}

inline int onsetsInBar (const Lane& pair, int bar) noexcept
{
    int n = 0;
    for (int i = 0; i < pair.count; ++i)
        n += (int) std::floor (pair.notes[i].start / 16.f + 1e-4f) == bar ? 1 : 0;
    return n;
}

inline bool hasOnset (const Lane& pair, int at) noexcept
{
    for (int i = 0; i < pair.count; ++i)
        if ((int) std::lround (pair.notes[i].start) == at)
            return true;
    return false;
}

// Monophonic inside the pair; the wrap into A is cut later by loopify.
inline void monoPair (Lane& pair) noexcept
{
    sortPair (pair);
    for (int i = 0; i + 1 < pair.count; ++i)
        if (pair.notes[i].start + pair.notes[i].length > pair.notes[i + 1].start)
            pair.notes[i].length = pair.notes[i + 1].start - pair.notes[i].start;
}

inline bool noWorse (const Matches& now, const Matches& was) noexcept
{
    return now.pc <= was.pc && now.exact <= was.exact;
}
} // namespace style_detail

// Thin the hook pair (bars 0-1) toward the onset target and move about a
// fifth of its onsets onto a kick. Seeded; never adds an A/B match.
inline void shapePair (Lane& pair, std::uint32_t seed, float busy)
{
    using namespace style_detail;
    if (pair.count <= 2)
        return;
    const Targets& T = kTargets;
    Rng r (deepbouncy::detail::mix (seed, 0x5A1Cu));
    sortPair (pair);

    // Onset budget for the two bars: expectation 2 x target x BUSY, split by seed.
    const float want = 2.f * T.bassOnsets * busyScale (busy);
    int total = (int) std::floor (want + (float) r.next());
    total = std::clamp (total, 4, 2 * (int) std::ceil (T.bassOnsetsMax * busyScale (busy)));
    const int first = r.next() < 0.5 ? total / 2 : total - total / 2;
    const int target[2] = { std::max (2, first), std::max (2, total - first) };

    // Removal: the note before rings on over the removed note's span (capped
    // at 4/16 unless it was already longer). Off-dotted notes go first, so
    // the dotted JACKIN feel survives.
    for (int guard = 0; guard < 16; ++guard)
    {
        const Matches was = pairMatches (pair);
        int best = -1;
        float bestScore = 1.0e9f;
        Lane bestLane;
        for (int i = 1; i < pair.count; ++i)
        {
            const int bar = (int) std::floor (pair.notes[i].start / 16.f + 1e-4f);
            if (bar < 0 || bar > 1 || onsetsInBar (pair, bar) <= target[bar])
                continue;
            Lane trial = pair;
            Note& prev = trial.notes[i - 1];
            const Note& gone = trial.notes[i];
            const float end = gone.start + gone.length;
            prev.length = std::min (end - prev.start, std::max (prev.length, 4.f));
            for (int k = i; k + 1 < trial.count; ++k)
                trial.notes[k] = trial.notes[k + 1];
            --trial.count;
            const Matches now = pairMatches (trial);
            if (! noWorse (now, was) || b2Share (trial) > T.b2MaxShare)
                continue;
            const int slot = (int) std::lround (pair.notes[i].start) & 15;
            const float score = (float) now.pc + (slot % 3 == 0 ? 0.6f : 0.f) + 0.5f * (float) r.next();
            if (score < bestScore)
            {
                bestScore = score;
                best = i;
                bestLane = trial;
            }
        }
        if (best < 0)
            break;
        pair = bestLane;
    }

    // Kick onsets: expectation onKick x onsets, kept under 30% so the B4 / B8
    // variations stay well inside the 35% hard rule.
    int kicks = 0;
    for (int i = 0; i < pair.count; ++i)
        kicks += kickJ ((int) std::lround (pair.notes[i].start) & 15) ? 1 : 0;
    int wantKicks = (int) std::floor (T.onKick * (float) pair.count + (float) r.next());
    wantKicks = std::min (wantKicks, (int) std::floor (0.30f * (float) pair.count));
    for (int guard = 0; guard < 4 && kicks < wantKicks; ++guard)
    {
        const Matches was = pairMatches (pair);
        float bestScore = 1.0e9f;
        bool found = false;
        Lane bestLane;
        for (int i = 0; i < pair.count; ++i)
        {
            const int st = (int) std::lround (pair.notes[i].start);
            const int slot = st & 15;
            int to = -1;
            if ((slot & 3) == 1)
                to = st - 1;      // pull onto the kick, keep the end
            else if ((slot & 3) == 3 && slot < 15)
                to = st + 1;      // push onto the next kick
            if (to < 0 || to >= 32 || hasOnset (pair, to))
                continue;
            Lane trial = pair;
            Note& n = trial.notes[i];
            const float end = n.start + n.length;
            n.start = (float) to;
            n.length = std::max (1.f, end - n.start);
            for (int k = 0; k < trial.count; ++k)
            {
                Note& p = trial.notes[k];
                if (k != i && p.start < n.start && p.start + p.length > n.start)
                    p.length = n.start - p.start;
            }
            bool tooShort = false;
            for (int k = 0; k < trial.count; ++k)
                tooShort = tooShort || trial.notes[k].length < 0.5f;
            if (tooShort)
                continue;
            monoPair (trial);
            const Matches now = pairMatches (trial);
            if (! noWorse (now, was) || b2Share (trial) > T.b2MaxShare)
                continue;
            // Spread across the four kicks so rolls do not all land on beat 1.
            const float score = (float) now.pc + (float) r.next();
            if (score < bestScore)
            {
                bestScore = score;
                bestLane = trial;
                found = true;
            }
        }
        if (! found)
            break;
        pair = bestLane;
        ++kicks;
    }
    sortPair (pair);
}

// JACKIN top end over writeDrums' base layer. Own RNG stream, so the base
// layer's draws are untouched.
inline void styleDrums (DrumLane& d, std::uint32_t seed, int bars, float busy)
{
    const Targets& T = kTargets;
    Rng r (deepbouncy::detail::mix (seed, 0xD7A3u));
    const int nBars = std::clamp (bars, 1, kMaxBars);
    const int steps = nBars * 16;
    // Per-roll ghost rate inside the band; BUSY leans it.
    float ghostP = T.ghostMin + (T.ghostMax - T.ghostMin) * (float) r.next();
    ghostP = std::clamp (ghostP + 0.25f * (std::clamp (busy, 0.f, 1.f) - 0.4f), T.ghostMin, T.ghostMax);
    auto vel = [&r] (int lo, int hi) { return (std::uint8_t) (lo + (int) (r.next() * (double) (hi - lo + 1))); };
    for (int t = 0; t < steps; ++t)
    {
        const int s = t % 16;
        if ((s & 3) == 2)
        {
            // Offbeat 8th: open hat most of the time, else a loud closed hat.
            if (r.next() < (double) T.openHatOffbeat)
            {
                d.vel[HatO][t] = vel (70, 100);
                d.vel[HatC][t] = 0;
            }
            else
            {
                d.vel[HatO][t] = 0;
                d.vel[HatC][t] = vel (64, 96);
            }
        }
        else if ((s & 1) == 1)
        {
            // e / a: ghosts at about a third of the main hat level; swing moves them.
            if (d.vel[HatC][t] == 0 && d.vel[HatO][t] == 0 && r.next() < (double) ghostP)
                d.vel[HatC][t] = vel (T.ghostVelMin, T.ghostVelMax);
        }
        else if (d.vel[HatC][t] == 0 && r.next() < 0.3)
        {
            // A quiet closed hat under some kicks, as the 90s jack kits play it.
            d.vel[HatC][t] = vel (52, 66);
        }
    }
    for (int bar = 0; bar < nBars; ++bar)
    {
        if (r.next() >= (double) T.openHatExtra)
            continue;
        const int t = bar * 16 + (r.next() < 0.5 ? 7 : 15);
        d.vel[HatO][t] = vel (64, 90);
        d.vel[HatC][t] = 0;
    }
}

} // namespace rolla::jackin
