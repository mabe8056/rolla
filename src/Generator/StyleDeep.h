#pragma once

// DEEP style pass (dialect Long only). generate() and writeDrums() call these
// behind `dialect == Long` hooks, so ROLLER / BOUNCY / JACKIN stay untouched.
//
// Targets are data/genre/style_rules.json styles.DEEP (4 references, low
// confidence). DeepTargets mirrors those numbers so a GenreTargets loader can
// fill it later; kTargets is the shipped copy.
//
//  - shapeBass: sparse rolls fill toward ~4.7 notes a bar (dark rolls already
//    sit at 4-8), fills on e / & / a by the reference slot-class weights and
//    never on the kick; a seeded downbeat (or beat-3) anchor puts ~6.5% of the
//    onsets on the kick; a bar-8 turnaround drawn from the reference palette;
//    bars 4 and 8 change the note count; a busy line keeps a >= 4/16 hole.
//  - stateMinorThird: every 4 bars state a b3 in the bass or the stab.
//  - fixOpenFifths: an open-5th-only stab gets its minor 3rd.
//  - hiBand: hat ghosts on the 16ths toward ~10.4 high-band onsets a bar, at
//    the lane's old energy, so the swing has 16ths to act on and the mix holds.
//
// Everything is a weighted draw from the lane seed. No stored phrases.

#include "DeepBouncy.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rolla::deep
{

struct DeepTargets
{
    float bassPerBar = 5.21f;    // bass.bass_onsets_per_bar (band 3.53-6.69)
    float sparsePerBar = 4.25f;  // submodes.sparse.notes_per_bar (3.5-5.5)
    float kickShare = 0.065f;    // bass.bass_on_kick_pct (hard max 35%)
    float busyHoleNpb = 8.5f;    // avoid: busy bass with no >= 4/16 hole
    float hiBandPerBar = 10.41f; // groove.hiband_onsets_per_bar (8.97-12.68)
    float swing = 0.53f;         // groove.hat_16th_swing_pct (49.8-66); preset default
    float duckDb = -16.f;        // groove.duck_depth_db_median; mix calibration, not used here
    float ghostVel = 35.f;       // groove.ghost_notes.velocity (20-50)
    // arrangement.bar8_turnaround.palette (the pickup share is pickup()'s own draw)
    float turnDrop = 0.3f, turnPitch = 0.2f, turnShift = 0.1f, turnFill = 0.05f, turnOctave = 0.05f, turnTrunc = 0.1f;
    // bass.slot_class_weights: beat, e, and, a (in-beat 16th position)
    float slotClass[4] { 0.172f, 0.306f, 0.395f, 0.128f };
};

inline constexpr DeepTargets kTargets {};

namespace detail
{
using rolla::deepbouncy::detail::countInBar;
using rolla::deepbouncy::detail::dropInBar;
using rolla::deepbouncy::detail::kickSlot;
using rolla::deepbouncy::detail::mix;
using rolla::deepbouncy::detail::onsetAt;
using rolla::deepbouncy::detail::pcOf;
using rolla::deepbouncy::detail::pickWeighted;
using rolla::deepbouncy::detail::sortLane;

// applyBusyDensity's curve, normalised so the shipped BUSY 0.40 is 1.
inline float busyScale (float busy) noexcept
{
    return (0.75f + 0.60f * std::clamp (busy, 0.f, 1.f)) / 0.99f;
}

inline int barOf (float start) noexcept
{
    return (int) std::floor (start / 16.f + 1e-4f);
}

// No note rings past the next onset or the loop end.
inline void mono (Lane& lane, float loop)
{
    sortLane (lane);
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        if (i + 1 < lane.count && n.start + n.length > lane.notes[i + 1].start)
            n.length = std::max (0.5f, lane.notes[i + 1].start - n.start);
        n.length = std::max (0.5f, std::min (n.length, loop - n.start));
    }
}

// Root on both the e and the a of beat 1 is the banned reaction figure.
inline bool makesReaction (const Lane& lane, int bar, int slot, int semi) noexcept
{
    if (pcOf (semi) != 0 || (slot != 1 && slot != 3))
        return false;
    const float other = (float) (bar * 16 + (slot == 1 ? 3 : 1));
    for (int i = 0; i < lane.count; ++i)
        if (std::fabs (lane.notes[i].start - other) < 1e-3f && pcOf (lane.notes[i].chord[0]) == 0)
            return true;
    return false;
}

inline bool soundingPitch (const Lane& lane, float t, int& semi) noexcept
{
    for (int i = 0; i < lane.count; ++i)
        if (t > lane.notes[i].start + 1e-3f && t < lane.notes[i].start + lane.notes[i].length - 1e-3f)
        {
            semi = lane.notes[i].chord[0];
            return true;
        }
    return false;
}

inline bool addAt (Lane& lane, float at, int semi, float len, float loop)
{
    if (lane.count >= kMaxNotes || at < 0.f || at >= loop || onsetAt (lane, at))
        return false;
    float next = loop;
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        if (n.start < at && n.start + n.length > at)
            n.length = std::max (0.5f, at - n.start);
        if (n.start > at && n.start < next)
            next = n.start;
    }
    Note n;
    n.start = at;
    n.length = std::clamp (std::min (len, next - at), 0.5f, 4.f);
    n.chord[0] = (std::int8_t) std::clamp (semi, -5, 19);
    n.nChord = 1;
    n.vel = (std::uint8_t) (((int) at & 3) == 2 ? 112 : 100);
    lane.notes[lane.count++] = n;
    sortLane (lane);
    return true;
}

// Drops onsets in [from, bar end) and cuts notes at `from`; the bar keeps its first note.
inline void cutFrom (Lane& lane, int bar, float from)
{
    const float lo = (float) (bar * 16), hi = lo + 16.f;
    sortLane (lane);
    int first = -1;
    for (int i = 0; i < lane.count && first < 0; ++i)
        if (lane.notes[i].start >= lo && lane.notes[i].start < hi)
            first = i;
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        Note n = lane.notes[i];
        if (i != first && n.start >= from - 1e-3f && n.start < hi)
            continue;
        if (n.start < from && n.start + n.length > from)
            n.length = std::max (0.5f, from - n.start);
        lane.notes[w++] = n;
    }
    lane.count = w;
}

inline int lastInBar (const Lane& lane, int bar) noexcept
{
    int k = -1;
    for (int i = 0; i < lane.count; ++i)
        if (barOf (lane.notes[i].start) == bar)
            k = i;
    return k;
}

inline int longestRest (const Lane& lane, int steps) noexcept
{
    int run = 0, best = 0;
    for (int t = 0; t < steps; ++t)
    {
        bool on = false;
        for (int i = 0; i < lane.count && ! on; ++i)
            on = (float) t >= lane.notes[i].start - 1e-3f && (float) t < lane.notes[i].start + lane.notes[i].length - 1e-3f;
        run = on ? 0 : run + 1;
        best = std::max (best, run);
    }
    return best;
}

// The 12 off-kick 16ths in a seeded order, weighted by in-beat slot class.
inline void rankSlots (Rng& r, const float* classW, int* out)
{
    float w[16];
    for (int s = 0; s < 16; ++s)
        w[s] = kickSlot (s) ? 0.f : classW[s & 3] * (0.6f + 0.8f * (float) r.next());
    for (int k = 0; k < 12; ++k)
    {
        const int s = pickWeighted (w, 16, r);
        out[k] = s;
        w[s] = 0.f;
    }
}

// scales.degree_emphasis, minus the 2 and b6 (not in every DEEP mode).
inline int fillPitch (Rng& r)
{
    static const int kSemi[] = { 0, 3, 5, 7, -2 };
    static const float kW[] = { 0.45f, 0.12f, 0.10f, 0.13f, 0.06f };
    return kSemi[pickWeighted (kW, 5, r)];
}

// Moves the first note just after the kick onto it (holding its end).
inline void anchorKick (Lane& lane, int bar, int kick, float loop)
{
    const float at = (float) (bar * 16 + kick);
    if (onsetAt (lane, at))
        return;
    sortLane (lane);
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        if (n.start > at + 1e-3f && n.start <= at + 2.f + 1e-3f)
        {
            const float shift = n.start - at;
            n.start = at;
            n.length = std::min (n.length + shift, 4.f);
            break;
        }
    }
    mono (lane, loop);
}

// Bar `ans` changes its note count against the bar before it.
inline void answerCount (Lane& lane, int ans, bool preferAdd, bool isLast, float loop, Rng& r)
{
    if (countInBar (lane, ans) != countInBar (lane, ans - 1))
        return;
    static const int kAnswer[] = { 5, 7, -2 }; // bar4_answer: 4th / 5th / b7
    auto tryAdd = [&]
    {
        // Back half; the last bar keeps its turnaround rest from step 12.
        const int hiSlot = isLast ? 12 : 16;
        const int spin = (int) (r.next() * 8.0);
        for (int k = 0; k < 8; ++k)
        {
            const int s = 8 + (spin + k) % 8;
            if (s >= hiSlot || kickSlot (s))
                continue;
            if (addAt (lane, (float) (ans * 16 + s), kAnswer[(int) (r.next() * 3.0) % 3], 2.f, loop))
                return true;
        }
        return false;
    };
    if (preferAdd)
    {
        if (! tryAdd())
            dropInBar (lane, ans, loop);
    }
    else if (! dropInBar (lane, ans, loop))
        tryAdd();
}
} // namespace detail

inline void shapeBass (Lane& bass, int bars, std::uint32_t seed, float busy, bool dark, const DeepTargets& t = kTargets)
{
    using namespace detail;
    bars = std::clamp (bars, 2, kMaxBars);
    if (bass.count <= 0)
        return;
    const float loop = (float) (bars * 16);
    const int last = bars - 1;
    Rng r (mix (seed, 0xDE5Bu));
    sortLane (bass);

    // 1. Density. Only ~40% of our rolls take the dark submode against ~65% of
    // the references, so the sparse rolls aim between the sparse target and the
    // style target. BUSY scales the aim like applyBusyDensity, add-only, so the
    // knob stays monotonic. A B A B keeps the same fill slots and pitches.
    if (! dark)
    {
        const float aim = 0.5f * (t.sparsePerBar + t.bassPerBar) + ((float) r.next() - 0.5f) * 0.8f;
        const float scale = busyScale (busy);
        int rank[2][12];
        int pitch[2][16];
        bool reattack[2][16];
        int off[2];
        static const float kOff[] = { 1.f, 2.f, 1.f }; // -1, 0, +1
        for (int p = 0; p < 2; ++p)
        {
            rankSlots (r, t.slotClass, rank[p]);
            for (int s = 0; s < 16; ++s)
            {
                pitch[p][s] = fillPitch (r);
                reattack[p][s] = r.next() < 0.35;
            }
            off[p] = pickWeighted (kOff, 3, r) - 1;
        }
        for (int bar = 0; bar < last; ++bar)
        {
            const int p = bar & 1;
            const int want = std::clamp ((int) std::lround ((aim + (float) off[p]) * scale), 2, 8);
            int have = countInBar (bass, bar);
            for (int k = 0; k < 12 && have < want; ++k)
            {
                const int s = rank[p][k];
                const float at = (float) (bar * 16 + s);
                if (onsetAt (bass, at))
                    continue;
                int semi = pitch[p][s];
                int held = 0;
                if (reattack[p][s] && soundingPitch (bass, at, held))
                    semi = held;
                if (makesReaction (bass, bar, s, semi))
                    semi = 7;
                if (addAt (bass, at, semi, 2.f, loop))
                    ++have;
            }
        }
    }

    // 2. The bass mostly avoids the kick. About two rolls in three anchor A bars
    // on beat 1 (or B bars on beat 3): ~4 of ~40 onsets, so ~6.5% on the kick.
    {
        const float pAny = std::clamp (t.kickShare * 2.f * t.bassPerBar, 0.f, 1.f);
        const float kw[3] = { 1.f - pAny, 0.6f * pAny, 0.4f * pAny };
        const int pick = pickWeighted (kw, 3, r);
        if (pick > 0)
            for (int bar = pick - 1; bar < last; bar += 2)
                anchorKick (bass, bar, pick == 1 ? 0 : 8, loop);
    }

    // 3. Bar-8 turnaround from the reference palette (pickup() draws its own share).
    {
        const float o = (float) (last * 16);
        const float pw[6] = { t.turnDrop, t.turnPitch, t.turnShift, t.turnFill, t.turnOctave, t.turnTrunc };
        switch (pickWeighted (pw, 6, r))
        {
            case 0:
                cutFrom (bass, last, o + 12.f); // drop out on the last beat
                break;
            case 1:
            {
                const int k = lastInBar (bass, last);
                static const int kTo[] = { 5, 7, -2 };
                for (int tries = 0; k >= 0 && tries < 4; ++tries)
                {
                    const int to = kTo[(int) (r.next() * 3.0) % 3];
                    if (pcOf (to) != pcOf (bass.notes[k].chord[0]))
                    {
                        bass.notes[k].chord[0] = (std::int8_t) to;
                        break;
                    }
                }
                break;
            }
            case 2:
                for (int i = bass.count - 1; i >= 0; --i)
                {
                    const float s = bass.notes[i].start;
                    if (s < o + 8.f || s >= o + 12.f)
                        continue;
                    const float to = s + 1.f;
                    if (! kickSlot ((int) std::lround (to) & 15) && to < o + 12.f && ! onsetAt (bass, to))
                        bass.notes[i].start = to;
                    break;
                }
                break;
            case 3:
                cutFrom (bass, last, o + 12.f);
                addAt (bass, o + 13.f, 7, 1.f, loop);
                addAt (bass, o + 14.f, 10, 1.f, loop);
                break;
            case 4:
            {
                const int k = lastInBar (bass, last);
                if (k >= 0 && bass.notes[k].chord[0] <= 7)
                    bass.notes[k].chord[0] = (std::int8_t) (bass.notes[k].chord[0] + 12);
                break;
            }
            default:
                cutFrom (bass, last, o + 10.f); // truncation
                break;
        }
        mono (bass, loop);
    }

    // 4. Bars 4 and 8 answer by density. The bar-8 change drops first, so the
    // turnaround rest stays a rest.
    if (3 < last)
        answerCount (bass, 3, r.next() < 0.5, false, loop, r);
    if (last >= 1)
        answerCount (bass, last, false, true, loop, r);

    // 5. A busy line (> 8.5 a bar) keeps a hole of at least four 16ths.
    if ((float) bass.count / (float) bars > t.busyHoleNpb && longestRest (bass, bars * 16) < 4)
        cutFrom (bass, last, (float) (last * 16) + 12.f);
    mono (bass, loop);
}

// style_rules: a minor 3rd at least once per 4 bars, in the stab or the bass.
// When neither has one, the longest non-root bass note in the window (never
// the window's first) moves to the b3 in its own octave.
inline void stateMinorThird (Lane& bass, const Lane& stab, int bars)
{
    using namespace detail;
    sortLane (bass);
    for (int w0 = 0; w0 < bars; w0 += 4)
    {
        const float lo = (float) (w0 * 16), hi = (float) (std::min (bars, w0 + 4) * 16);
        bool has = false;
        for (int i = 0; i < bass.count && ! has; ++i)
            has = bass.notes[i].start >= lo && bass.notes[i].start < hi && pcOf (bass.notes[i].chord[0]) == 3;
        for (int i = 0; i < stab.count && ! has; ++i)
        {
            const Note& n = stab.notes[i];
            if (n.start >= hi || n.start + n.length <= lo)
                continue;
            for (int c = 0; c < n.nChord && c < 4; ++c)
                has = has || pcOf (n.chord[c]) == 3;
        }
        if (has)
            continue;
        int best = -1;
        bool bestRoot = true, first = true;
        float bestLen = -1.f;
        for (int i = 0; i < bass.count; ++i)
        {
            const Note& n = bass.notes[i];
            if (n.start < lo || n.start >= hi)
                continue;
            if (first)
            {
                first = false;
                continue;
            }
            const bool root = pcOf (n.chord[0]) == 0;
            if (best < 0 || (bestRoot && ! root) || (root == bestRoot && n.length > bestLen))
            {
                best = i;
                bestRoot = root;
                bestLen = n.length;
            }
        }
        if (best < 0)
            continue;
        const int semi = bass.notes[best].chord[0];
        int to = semi - pcOf (semi) + 3;
        if (to > 19)
            to -= 12;
        bass.notes[best].chord[0] = (std::int8_t) to;
    }
}

// style_rules: open-5th-only stab voicings <= 25% of hits. A bare root + 5th
// takes the minor 3rd over its root when the roll's mode has it.
inline void fixOpenFifths (Lane& stab, const Lane& bass)
{
    using namespace detail;
    const int mode = rolla::deepbouncy::detail::modeOf (bass);
    for (int i = 0; i < stab.count; ++i)
    {
        Note& n = stab.notes[i];
        if (n.nChord < 2 || n.nChord >= 4)
            continue;
        int lowest = n.chord[0];
        for (int c = 1; c < n.nChord; ++c)
            lowest = std::min (lowest, (int) n.chord[c]);
        bool bare = true;
        for (int c = 0; c < n.nChord; ++c)
        {
            const int d = pcOf (n.chord[c] - lowest);
            bare = bare && (d == 0 || d == 7);
        }
        if (! bare)
            continue;
        const int third = pcOf (lowest + 3);
        bool inMode = false;
        for (int pc : rolla::deepbouncy::detail::kModes[mode])
            inMode = inMode || pc == third;
        if (inMode)
            n.chord[n.nChord++] = (std::int8_t) (lowest + 3);
    }
}

// High band (closed / open hat, ride, perc): ghost closed hats on free 16ths
// until a bar has ~10.4 onsets (BUSY scales the aim). Each bar's closed-hat
// energy (sum of vel^2) is put back to what it was, so the ghosts sit about
// 0.4 of the main hats (groove.hat_16th_level) and the lane level is unchanged.
inline void hiBand (DrumLane& d, int bars, std::uint32_t seed, float busy, const DeepTargets& t = kTargets)
{
    using namespace detail;
    bars = std::clamp (bars, 1, kMaxBars);
    Rng r (mix (seed, 0x41B4u));
    const float scale = busyScale (busy);
    const float rollOff = ((float) r.next() - 0.5f) * 1.6f;
    float w[16];
    for (int s = 0; s < 16; ++s)
    {
        const float cls = kickSlot (s) ? 0.25f : ((s & 3) == 2 ? 1.2f : ((s & 3) == 3 ? 1.0f : 0.8f));
        w[s] = cls * (0.6f + 0.8f * (float) r.next());
    }
    int rank[16];
    for (int k = 0; k < 16; ++k)
    {
        const int s = pickWeighted (w, 16, r);
        rank[k] = s;
        w[s] = 0.f;
    }
    auto hi = [&] (int st)
    {
        return d.vel[HatC][st] != 0 || d.vel[HatO][st] != 0 || d.vel[Ride][st] != 0 || d.vel[Perc][st] != 0;
    };
    static const float kJit[] = { 1.f, 2.f, 1.f };
    for (int bar = 0; bar < bars; ++bar)
    {
        const int base = bar * 16;
        int have = 0;
        double before = 0.0;
        for (int s = 0; s < 16; ++s)
        {
            have += hi (base + s) ? 1 : 0;
            before += (double) d.vel[HatC][base + s] * (double) d.vel[HatC][base + s];
        }
        const float jit = (float) (pickWeighted (kJit, 3, r) - 1);
        const int want = std::clamp ((int) std::lround ((t.hiBandPerBar + rollOff + jit) * scale), 6, 16);
        bool added = false;
        for (int k = 0; k < 16 && have < want; ++k)
        {
            const int st = base + rank[k];
            if (hi (st))
                continue;
            // Quieter on the e than on the a / &.
            const float v = t.ghostVel * ((rank[k] & 3) == 1 ? 0.75f : 1.f) + ((float) r.next() - 0.5f) * 12.f;
            d.vel[HatC][st] = (std::uint8_t) std::clamp ((int) std::lround (v), 16, 60);
            ++have;
            added = true;
        }
        if (! added || before <= 0.0)
            continue;
        double after = 0.0;
        for (int s = 0; s < 16; ++s)
            after += (double) d.vel[HatC][base + s] * (double) d.vel[HatC][base + s];
        const double g = std::sqrt (before / after);
        for (int s = 0; s < 16; ++s)
            if (d.vel[HatC][base + s] != 0)
                d.vel[HatC][base + s] = (std::uint8_t) std::clamp ((int) std::lround (d.vel[HatC][base + s] * g), 8, 127);
    }
}

} // namespace rolla::deep
