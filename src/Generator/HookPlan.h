#pragma once

#include "Generator.h"
#include "Pattern.h"
#include "PhraseLock.h"

#include <algorithm>
#include <bitset>
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

// A fixed per-roll hash of an in-bar slot (tie-breaks only). Keyed on the slot inside the bar, so
// two bars with the same bass make the same choice and restated bars stay restated.
inline std::uint32_t slotHash (std::uint32_t salt, int slot) noexcept
{
    std::uint32_t h = salt ^ (0x9E3779B9u * (std::uint32_t) (slot + 1));
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

inline int pcOfSemi (int semi) noexcept
{
    return ((semi % 12) + 12) % 12;
}

inline std::uint32_t chordPcs (const Note& n) noexcept
{
    std::uint32_t m = 0;
    const int nc = std::clamp ((int) n.nChord, 1, 4);
    for (int c = 0; c < nc; ++c)
        m |= 1u << pcOfSemi (n.chord[c]);
    return m;
}

// Stab on the bass's rhythm (catchiness research, BOUNCY: the bass is usually the hook, and the chord
// stabs reuse the bassline's rhythm with shorter notes; pooled BOUNCY stabs land on a bass attack 100%
// of the time, ROLLA's landed on 39%). Each bar's stab onsets become a subset of that bar's bass
// onsets: as many hits as the stab had there, at least 2 and at most 3 (never more than the bass has),
// picked by one rule from the slots alone so a restated bass bar gives a restated stab bar:
//   - syncopated attacks first: an "and" (16th 3 of the beat) is the bar's anchor whenever the bass
//     plays one, then an e / a, then another "and"; e / a stays at or under half the bar's hits (hook
//     cell band 0.20-0.50), so a bar with no free "and" takes a beat under the bass as its anchor;
//   - no two hits a 16th apart where the bass leaves room, spread across the bar;
//   - an attack whose bass note is a semitone from the stab's chord ranks lower (the clash rule
//     would thin the chord there).
// Hits are short (1 16th, 1.5 over a bass note of 2, 2 over a held note) and keep the stab's own
// chords, in their order; a tone the clash rule dropped comes back when the bar holds the full
// voicing (the caller runs the clash rule again). `avoid` (optional) marks loop steps the hook owns.
// Bars without a stab stay empty; a bar whose bass has no free attack keeps its stab as it was.
// Every pitch and slot is the roll's own: nothing stored, nothing from a reference.
inline void stabOnBass (Lane& stab, const Lane& bass, int bars, const bool* avoid) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    if (stab.count <= 0 || bass.count <= 0)
        return;
    const Lane src = stab;
    const std::uint32_t salt = stab.seed ? stab.seed : 1u;
    std::uint32_t bassMask[kMaxBars] {};
    bool rewritten[kMaxBars] {};
    int barFirst[kMaxBars] {};
    int barCount[kMaxBars] {};
    stab.count = 0;
    for (int b = 0; b < nb; ++b)
    {
        int srcIdx[kMaxNotes];
        int nSrc = 0;
        for (int i = 0; i < src.count; ++i)
            if (src.notes[i].vel > 2 && phrase_lock::barOf (src.notes[i].start) == b)
                srcIdx[nSrc++] = i;
        if (nSrc == 0)
            continue;
        int bassAt[kStepsPerBar];
        for (int& x : bassAt)
            x = -1;
        for (int i = 0; i < bass.count; ++i)
        {
            const int s = slotOf (bass.notes[i].start);
            if (bass.notes[i].vel <= 2 || s < b * kStepsPerBar || s >= (b + 1) * kStepsPerBar)
                continue;
            const int q = s - b * kStepsPerBar;
            bassMask[b] |= 1u << q;
            if (bassAt[q] < 0)
                bassAt[q] = i;
        }
        const int first = stab.count;
        // A restated bar (same bass as an earlier bar of its parity) repeats that bar's stab exactly;
        // the turn (last bar) is always its own.
        if (b >= 2 && b < nb - 1)
        {
            int c = -1;
            for (int k = b % 2; k < b && c < 0; k += 2)
                if (rewritten[k] && bassMask[k] == bassMask[b])
                    c = k;
            if (c >= 0)
            {
                for (int i = 0; i < barCount[c] && stab.count < kMaxNotes; ++i)
                {
                    Note n = stab.notes[barFirst[c] + i];
                    n.start += (float) ((b - c) * kStepsPerBar);
                    stab.notes[stab.count++] = n;
                }
                rewritten[b] = true;
                barFirst[b] = first;
                barCount[b] = stab.count - first;
                continue;
            }
        }
        bool cand[kStepsPerBar] {};
        int nCand = 0;
        for (int q = 0; q < kStepsPerBar; ++q)
            if (bassAt[q] >= 0 && ! (avoid != nullptr && avoid[b * kStepsPerBar + q]))
            {
                cand[q] = true;
                ++nCand;
            }
        if (nCand == 0)
        {
            for (int k = 0; k < nSrc && stab.count < kMaxNotes; ++k)
                stab.notes[stab.count++] = src.notes[srcIdx[k]];
            continue;
        }
        // The bar's fullest voicing scores the clash with the bass under each attack.
        int fullest = srcIdx[0];
        for (int k = 1; k < nSrc; ++k)
            if (src.notes[srcIdx[k]].nChord > src.notes[fullest].nChord)
                fullest = srcIdx[k];
        const std::uint32_t fullPcs = chordPcs (src.notes[fullest]);
        const int want = std::min (std::clamp (nSrc, 2, 3), nCand);
        bool anyAnd = false;
        for (int q = 2; q < kStepsPerBar; q += 4)
            anyAnd = anyAnd || cand[q];
        int chosen[3];
        int nChosen = 0;
        int ea = 0;
        for (int pick = 0; pick < want; ++pick)
        {
            int best = -1;
            float bestScore = -1.0e9f;
            for (int q = 0; q < kStepsPerBar; ++q)
            {
                if (! cand[q])
                    continue;
                bool used = false;
                int minDist = kStepsPerBar;
                for (int k = 0; k < nChosen; ++k)
                {
                    used = used || chosen[k] == q;
                    minDist = std::min (minDist, std::abs (chosen[k] - q));
                }
                if (used)
                    continue;
                const bool isEa = (q & 1) != 0;
                const bool isAnd = q % 4 == 2;
                // The first hit is the bar's anchor: an "and" whenever the bass plays one.
                if (pick == 0 && anyAnd && ! isAnd)
                    continue;
                float sc = isAnd ? 3.f : (isEa ? 2.f : 1.f);
                if (isEa && nChosen > 0)
                {
                    if (ea == 0)
                        sc += 0.9f;
                    // Over half the hits on e / a: a beat under the bass anchors the bar instead.
                    if (2 * (ea + 1) > nChosen + 1)
                        sc -= 2.f;
                }
                if (nChosen > 0)
                {
                    if (minDist == 1)
                        sc -= 2.5f;
                    sc += 0.15f * (float) std::min (4, minDist);
                }
                const int bpc = pcOfSemi (bass.notes[bassAt[q]].chord[0]);
                const std::uint32_t near = (1u << ((bpc + 1) % 12)) | (1u << ((bpc + 11) % 12));
                sc -= 0.35f * (float) std::bitset<12> (fullPcs & near).count();
                sc += 0.3f * (float) (slotHash (salt, q) >> 8) / 16777216.f;
                if (sc > bestScore)
                {
                    bestScore = sc;
                    best = q;
                }
            }
            if (best < 0)
                break;
            chosen[nChosen++] = best;
            if ((best & 1) != 0)
                ++ea;
        }
        std::sort (chosen, chosen + nChosen);
        for (int j = 0; j < nChosen && stab.count < kMaxNotes; ++j)
        {
            const int q = chosen[j];
            // The stab's own chords in their order across the bar.
            const int m = nSrc == 1 || nChosen == 1 ? srcIdx[0]
                                                    : srcIdx[(int) std::lround ((float) j * (float) (nSrc - 1) / (float) (nChosen - 1))];
            const std::uint32_t mPcs = chordPcs (src.notes[m]);
            int voice = m;
            for (int k = 0; k < nSrc; ++k)
            {
                const Note& o = src.notes[srcIdx[k]];
                if ((chordPcs (o) & mPcs) == mPcs && o.nChord > src.notes[voice].nChord)
                    voice = srcIdx[k];
            }
            Note n = src.notes[m];
            n.nChord = src.notes[voice].nChord;
            for (int c = 0; c < 4; ++c)
                n.chord[c] = src.notes[voice].chord[c];
            const float barEnd = (float) ((b + 1) * kStepsPerBar);
            n.start = (float) (b * kStepsPerBar + q);
            const Note& under = bass.notes[bassAt[q]];
            const float bassLen = std::min (under.length, barEnd - n.start);
            float len = bassLen >= 2.5f ? 2.f : (bassLen >= 1.75f ? 1.5f : 1.f);
            const float next = j + 1 < nChosen ? (float) (b * kStepsPerBar + chosen[j + 1]) : barEnd;
            n.length = std::max (0.5f, std::min (len, next - n.start));
            stab.notes[stab.count++] = n;
        }
        rewritten[b] = true;
        barFirst[b] = first;
        barCount[b] = stab.count - first;
    }
    std::sort (stab.notes, stab.notes + stab.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
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

    // The stab rides the bass's rhythm when the bass is the hook (the stab is its support), and in
    // BOUNCY whenever both play (hook_detail::stabOnBass). A BOUNCY stab hook takes it here, before
    // its slots are read as the hook's.
    const bool bouncy = g.dialect == Pop;
    if (bouncy && plan.hook == Stab && regen (Stab) && regen (Bass))
        stabOnBass (g.lane[Stab], g.lane[Bass], bars, nullptr);

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
        // A stab supporting a bass hook (any style), or any BOUNCY stab: onto the bass's attacks,
        // still clear of a vox hook's own slots. The caller runs the clash rule again after this.
        if (plan.support == Stab && regen (Bass) && (plan.hook == Bass || bouncy))
            stabOnBass (sp, g.lane[Bass], bars, plan.hook == Bass ? nullptr : hookAt);
    }
}
} // namespace rolla
