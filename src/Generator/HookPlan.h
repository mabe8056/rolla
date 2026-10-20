#pragma once

#include "Generator.h"
#include "Pattern.h"
#include "PhraseLock.h"

#include <algorithm>
#include <cmath>

// Intentional arrangement (Melak, Oct 19: "you don't have to include every sound in every roll",
// "add things intentionally"). Every roll is: drums + bass (the floor), ONE hook lane, and at most
// one support part chosen to serve that hook. Every other melodic lane is silent for the roll.
//
// Hook lane weights per style come from the catchiness research (section 4 and rule HOOK_LANE):
//   ROLLER stab 0.40 / vox 0.40 / bass 0.20     DEEP chord stab 0.55 / vox 0.25 / pluck 0.20
//   BOUNCY bass 0.50 / vox 0.35 / stab 0.15     JACKIN vox 0.45 / stab 0.35 / bass 0.20
// The support part has a job relative to the hook: a vox hook gets a chord bed (stab or pad), a
// stab hook gets a vox answer in its gaps or a pad, a bass hook gets a stab on its rhythm or a vox,
// a pluck hook gets a pad. Support plays only in the hook's gaps, a little quieter, and in half the
// rolls enters at bar 5 as the lift (references change bar 5 in ~40% of phrases). Nothing here is
// stored or reference-derived: it decides which of the roll's own lanes play and where.

namespace rolla
{
struct HookPlan
{
    int hook = -1;
    int support = -1;
    int supportFromBar = 0;
};

namespace hook_detail
{
inline bool hasNotes (const Lane& lane) noexcept
{
    for (int i = 0; i < lane.count; ++i)
        if (lane.notes[i].vel > 2)
            return true;
    return false;
}

inline int pick (Rng& r, const int* lanes, const float* w, int n, const Groove& g, std::uint32_t avail) noexcept
{
    float total = 0.f;
    for (int i = 0; i < n; ++i)
        if (lanes[i] >= 0 && ((avail >> lanes[i]) & 1u) != 0)
            total += w[i];
    if (total <= 0.f)
        return -1;
    float x = (float) r.next() * total;
    for (int i = 0; i < n; ++i)
    {
        if (lanes[i] < 0 || ((avail >> lanes[i]) & 1u) == 0)
            continue;
        x -= w[i];
        if (x <= 0.f)
            return lanes[i];
    }
    (void) g;
    return -1;
}

inline int slotOf (float start) noexcept
{
    return (int) std::lround (start);
}
} // namespace hook_detail

inline HookPlan planHook (const Groove& g, int dialect, std::uint32_t seed, std::uint32_t partMask, bool keepStab) noexcept
{
    using namespace hook_detail;
    Rng r (seed ? seed : 1u);
    std::uint32_t avail = 0;
    for (int p : { (int) Bass, (int) Stab, (int) Pluck, (int) Vox })
        if (hasNotes (g.lane[p]))
            avail |= 1u << p;
    HookPlan plan;
    static constexpr int kLanes[4][3] = {
        { Stab, Vox, Bass },  // ROLLER
        { Stab, Vox, Pluck }, // DEEP: the chord stab
        { Bass, Vox, Stab },  // BOUNCY
        { Vox, Stab, Bass },  // JACKIN
    };
    static constexpr float kW[4][3] = {
        { 0.40f, 0.40f, 0.20f },
        { 0.55f, 0.25f, 0.20f },
        { 0.50f, 0.35f, 0.15f },
        { 0.45f, 0.35f, 0.20f },
    };
    const int d = std::clamp (dialect, 0, 3);
    plan.hook = pick (r, kLanes[d], kW[d], 3, g, avail);
    if (plan.hook < 0)
        plan.hook = Bass;

    // The support part's job depends on the hook. -1 = no support (a third of rolls).
    int sup[3] = { -1, -1, -1 };
    float sw[3] = { 0.f, 0.f, 0.f };
    if (plan.hook == Vox)
    {
        sup[0] = Stab; sup[1] = Pad; sw[0] = 0.35f; sw[1] = 0.35f;
    }
    else if (plan.hook == Stab)
    {
        sup[0] = Vox; sup[1] = Pad; sw[0] = 0.40f; sw[1] = 0.30f;
    }
    else if (plan.hook == Bass)
    {
        sup[0] = Stab; sup[1] = Vox; sw[0] = 0.40f; sw[1] = 0.30f;
    }
    else
    {
        sup[0] = Pad; sup[1] = Vox; sw[0] = 0.40f; sw[1] = 0.20f;
    }
    std::uint32_t supAvail = 0;
    for (int p : { (int) Stab, (int) Pad, (int) Vox })
        if (p != plan.hook && hasNotes (g.lane[p]) && ((partMask >> p) & 1u) != 0)
            supAvail |= 1u << p;
    const float none = 1.f - sw[0] - sw[1];
    if (r.next() >= (double) none)
        plan.support = pick (r, sup, sw, 2, g, supAvail);
    plan.supportFromBar = r.next() < 0.5 ? 4 : 0;
    // The STAB knob is the user's own call: at 0.5 and up the stab plays, as the hook or as the support.
    if (keepStab && plan.hook != Stab && hasNotes (g.lane[Stab]))
    {
        plan.support = Stab;
        plan.supportFromBar = 0;
    }
    return plan;
}

// Applies the plan to the regenerated lanes (kept lanes are never touched).
inline void applyHookPlan (Groove& g, const HookPlan& plan, int bars, std::uint32_t partMask) noexcept
{
    using namespace hook_detail;
    auto regen = [&] (int p) { return ((partMask >> p) & 1u) != 0; };
    // Silence every melodic lane that is not the floor, the hook or the support.
    for (int p : { (int) Stab, (int) Pluck, (int) Pad, (int) Hits, (int) Vox })
        if (regen (p) && p != plan.hook && p != plan.support)
            g.lane[p].count = 0;

    // A vox hook speaks in every bar: the call bar's chops every bar through bar 7, at least two.
    if (plan.hook == Vox && regen (Vox) && g.lane[Vox].count > 0)
    {
        Lane& v = g.lane[Vox];
        int src = phrase_lock::barOf (v.notes[0].start);
        Lane cell;
        cell.seed = v.seed;
        for (int i = 0; i < v.count; ++i)
            if (phrase_lock::barOf (v.notes[i].start) == src && cell.count < kMaxNotes)
            {
                Note n = v.notes[i];
                n.start -= (float) (src * kStepsPerBar);
                cell.notes[cell.count++] = n;
            }
        if (cell.count == 1 && cell.count < kMaxNotes)
        {
            // An answer chop a dotted quarter later, or earlier when it would leave the bar.
            Note n = cell.notes[0];
            n.start = n.start + 6.f < 15.f ? n.start + 6.f : std::max (0.f, n.start - 6.f);
            n.length = std::min (n.length, 3.f);
            cell.notes[cell.count++] = n;
            std::sort (cell.notes, cell.notes + cell.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
        }
        Lane last;
        for (int i = 0; i < v.count; ++i)
            if (phrase_lock::barOf (v.notes[i].start) == bars - 1 && last.count < kMaxNotes)
                last.notes[last.count++] = v.notes[i];
        v.count = 0;
        for (int b = 0; b < bars; ++b)
        {
            if (b == bars - 1 && last.count > 0)
            {
                for (int i = 0; i < last.count && v.count < kMaxNotes; ++i)
                    v.notes[v.count++] = last.notes[i];
                continue;
            }
            for (int i = 0; i < cell.count && v.count < kMaxNotes; ++i)
            {
                Note n = cell.notes[i];
                n.start += (float) (b * kStepsPerBar);
                n.length = std::min (n.length, (float) ((b + 1) * kStepsPerBar) - n.start);
                v.notes[v.count++] = n;
            }
        }
    }

    // The hook is the loudest idea; support sits under it, only in its gaps, from its entry bar.
    bool hookAt[kMaxSteps] {};
    if (plan.hook >= 0)
    {
        Lane& h = g.lane[plan.hook];
        for (int i = 0; i < h.count; ++i)
        {
            const int s = slotOf (h.notes[i].start);
            if (s >= 0 && s < kMaxSteps)
                hookAt[s] = true;
            if (regen (plan.hook) && plan.hook != Bass)
                h.notes[i].vel = (std::uint8_t) std::min (127, h.notes[i].vel + 10);
        }
    }
    if (plan.support >= 0 && regen (plan.support))
    {
        Lane& sp = g.lane[plan.support];
        const Lane src = sp;
        sp.count = 0;
        for (int i = 0; i < src.count; ++i)
        {
            Note n = src.notes[i];
            const int s = slotOf (n.start);
            if (phrase_lock::barOf (n.start) < plan.supportFromBar)
                continue;
            // A pad is a bed under everything; other support parts answer in the hook's gaps.
            if (plan.support != Pad && plan.hook != Bass && s >= 0 && s < kMaxSteps && hookAt[s])
                continue;
            n.vel = (std::uint8_t) std::max (40, (int) n.vel - 8);
            sp.notes[sp.count++] = n;
        }
    }
}
} // namespace rolla
