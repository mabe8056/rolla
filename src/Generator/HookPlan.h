#pragma once

#include "Generator.h"
#include "Pattern.h"
#include "PhraseLock.h"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstring>
#include <vector>

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

// The plan the last full roll on this thread shipped with. Read by the roll tests and the
// measuring tools only; generation never reads it.
inline HookPlan& lastHookPlan() noexcept
{
    static thread_local HookPlan plan;
    return plan;
}

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
// `support` (the stab is not the hook): at the turn the stab keeps bar 2's hits where it can.
// Every pitch and slot is the roll's own: nothing stored, nothing from a reference.
inline void stabOnBass (Lane& stab, const Lane& bass, int bars, const bool* avoid, bool support) noexcept
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
        // The turn (bar 8 is one clear move): a support stab keeps bar 2's hits wherever the bass
        // still attacks there, and follows the bass's turn only where the bass moved. A stab hook
        // keeps its own turn.
        if (support && b == nb - 1 && nb >= 4 && rewritten[1])
            for (int i = 0; i < barCount[1] && nChosen < want; ++i)
            {
                const int q = slotOf (stab.notes[barFirst[1] + i].start) - kStepsPerBar;
                if (q >= 0 && q < kStepsPerBar && cand[q])
                {
                    chosen[nChosen++] = q;
                    ea += q & 1;
                }
            }
        for (int pick = nChosen; pick < want; ++pick)
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
                if (nChosen == 0 && anyAnd && ! isAnd)
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

// Hook cell rules (catchiness research, Oct 19). The hook lane's cell (bars 1-2, the motif every
// repeat restates; a vox hook's cell is its one bar) is held to the pooled hook-cell statistics:
//  rhythm: every cell bar has an anchor (an event on a beat or an 'and': slot % 2 == 0) and an
//    event off the kick slots (0, 4, 8, 12); the share of events on e / a slots (odd 16ths) sits in
//    0.20-0.50 (vox 0.20-0.60). A repair moves one onset by one 16th (a lone bar hit on a kick
//    slot moves to its 'and'), never onto a kick slot and never out of its bar.
//  pitch (stab top line, pluck, bass): 2-4 pitch classes and a signature interval: at least one
//    move of 4+ semitones, and at most a quarter of the moves that big (at least one is allowed,
//    so a short cell has exactly one). The restatement puts it at the same spot in every repeat.
//    A repair only re-places notes by octaves (a stab is re-voiced on its own chord tones) or swaps
//    one note (two when the line has six pitch classes) to a tone already in the roll.
// Nothing is stored or reference-derived: the search runs on the roll's own notes. No repair adds
// a semitone against the bass (or, for the bass, against the stab / pad over it), a b2, the banned
// 2&4 root figure or a bass note on the kick; a cell no such repair fixes is left as it was.
namespace hook_cell
{
inline int pcOf (int v) noexcept
{
    const int p = v % 12;
    return p < 0 ? p + 12 : p;
}

inline int slotOf (float t) noexcept
{
    return (int) std::lround (t);
}

inline std::uint32_t semitoneMask (std::uint32_t pcs) noexcept
{
    std::uint32_t m = 0;
    for (int p = 0; p < 12; ++p)
        if (((pcs >> p) & 1u) != 0)
            m |= (1u << ((p + 1) % 12)) | (1u << ((p + 11) % 12));
    return m;
}

// Sounding pitch classes of a note (vox is written from MIDI 64, four semitones over the key).
inline std::uint32_t tonesOf (const Note& n, int part) noexcept
{
    std::uint32_t m = 0;
    const int bias = part == Vox ? 4 : 0;
    const int nc = std::clamp ((int) n.nChord, 1, 4);
    for (int c = 0; c < nc; ++c)
        m |= 1u << pcOf (n.chord[c] + bias);
    return m;
}

inline std::uint32_t pcsOver (const Lane& l, int part, float t0, float t1) noexcept
{
    std::uint32_t m = 0;
    for (int i = 0; i < l.count; ++i)
    {
        const Note& n = l.notes[i];
        if (n.vel > 2 && n.start < t1 - 1.0e-3f && t0 < n.start + n.length - 1.0e-3f)
            m |= tonesOf (n, part);
    }
    return m;
}

inline void sortLane (Lane& l) noexcept
{
    std::stable_sort (l.notes, l.notes + l.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
}

struct Ctx
{
    Groove* g = nullptr;
    Lane* lane = nullptr; // the cell's notes (the hook lane, or a vox hook's one-bar cell)
    int part = -1;
    int cellBars = 2;
    int copies[2][kMaxBars] {}; // the bars each cell bar is heard in (itself and its restatements)
    int nCopies[2] {};
    bool nudge = true;          // rhythm repairs allowed (not on a BOUNCY pulse grid)
    bool keepBassClass = false; // DEEP / BOUNCY stab: a hit keeps its place against the bass
    bool octaveSignature = false; // BOUNCY bass: the octave pump is the preferred signature
    std::uint32_t seed = 1;
};

// The bars (bit k = copy k) in which [t0, t1) of cell bar cb meets the other side of the clash
// rule a semitone from `sounding`: the bass for an upper lane, the stab / pad for the bass.
inline std::uint32_t clashCopies (const Ctx& c, std::uint32_t sounding, float t0, float t1, int cb) noexcept
{
    const Groove& g = *c.g;
    std::uint32_t hit = 0;
    for (int k = 0; k < c.nCopies[cb]; ++k)
    {
        const float sh = (float) ((c.copies[cb][k] - cb) * kStepsPerBar);
        std::uint32_t other = 0;
        if (c.part == Bass)
            other = pcsOver (g.lane[Stab], Stab, t0 + sh, t1 + sh) | pcsOver (g.lane[Pad], Pad, t0 + sh, t1 + sh);
        else
            other = pcsOver (g.lane[Bass], Bass, t0 + sh, t1 + sh);
        if ((semitoneMask (other) & sounding) != 0)
            hit |= 1u << k;
    }
    return hit;
}

// A repair may not add a clash: every bar that clashes after it clashed before it.
inline bool addsNoClash (std::uint32_t was, std::uint32_t now) noexcept
{
    return (now & ~was) == 0;
}

inline bool occupied (const Lane& l, int abs) noexcept
{
    for (int i = 0; i < l.count; ++i)
        if (slotOf (l.notes[i].start) == abs)
            return true;
    return false;
}

// 0 bass gap, 1 a held bass note, 2 a bass attack at 16th `abs`.
inline int bassClass (const Lane& bass, int abs) noexcept
{
    int cls = 0;
    for (int i = 0; i < bass.count; ++i)
    {
        const int a = slotOf (bass.notes[i].start);
        const int b = slotOf (bass.notes[i].start + bass.notes[i].length);
        if (a == abs)
            return 2;
        if (abs > a && abs < b)
            cls = 1;
    }
    return cls;
}

inline bool rootAt (const Lane& l, int abs) noexcept
{
    for (int i = 0; i < l.count; ++i)
        if (slotOf (l.notes[i].start) == abs && pcOf (l.notes[i].chord[0]) == 0)
            return true;
    return false;
}

// Sorted in-bar onset slots (vel > 2) of cell bar `bar`.
inline int eventsInBar (const Lane& l, int bar, int* out) noexcept
{
    bool at[kStepsPerBar] {};
    for (int i = 0; i < l.count; ++i)
    {
        const int s = slotOf (l.notes[i].start) - bar * kStepsPerBar;
        if (l.notes[i].vel > 2 && s >= 0 && s < kStepsPerBar)
            at[s] = true;
    }
    int n = 0;
    for (int s = 0; s < kStepsPerBar; ++s)
        if (at[s])
            out[n++] = s;
    return n;
}

// Can the onset at absolute slot s move to t (same bar, never a kick slot)?
inline bool canMove (const Ctx& c, int s, int t) noexcept
{
    const Lane& l = *c.lane;
    const int bar = s / kStepsPerBar;
    if (t < bar * kStepsPerBar || t >= (bar + 1) * kStepsPerBar || t % 4 == 0 || occupied (l, t))
        return false;
    const int dt = t - s;
    const int in = t % kStepsPerBar;
    if (c.part == Bass)
    {
        // No e-and-a run on one beat (the A&R gate's eaa share).
        const int beat = in / 4 * 4;
        int run = 0;
        for (int k = 1; k < 4; ++k)
        {
            const int abs = bar * kStepsPerBar + beat + k;
            run += (abs == t || (abs != s && occupied (l, abs))) ? 1 : 0;
        }
        if (run == 3)
            return false;
    }
    // DEEP and BOUNCY stab hits stay in the bass gap, over a held bass note or on a bass attack as
    // written: DEEP's answers in the gaps and BOUNCY's stabs on the bass attacks hold.
    if (c.keepBassClass && bassClass (c.g->lane[Bass], s) != bassClass (c.g->lane[Bass], t))
        return false;
    // The other way round: a bass hook's stab rides its rhythm (BOUNCY's stabs land on the bass
    // attacks), so a bass onset a stab hits with, in any bar the cell plays in, stays put.
    if (c.part == Bass)
        for (int k = 0; k < c.nCopies[bar]; ++k)
            if (occupied (c.g->lane[Stab], s + (c.copies[bar][k] - bar) * kStepsPerBar))
                return false;
    for (int i = 0; i < l.count; ++i)
    {
        const Note& n = l.notes[i];
        if (slotOf (n.start) != s)
            continue;
        // The banned 2&4 root figure: a root on the bar's 16ths 2 and 4.
        if (c.part == Bass && pcOf (n.chord[0]) == 0 && (in == 1 || in == 3)
            && rootAt (l, bar * kStepsPerBar + (in == 1 ? 3 : 1)))
            return false;
        const std::uint32_t tones = tonesOf (n, c.part);
        if (! addsNoClash (clashCopies (c, tones, n.start, n.start + n.length, bar),
                           clashCopies (c, tones, n.start + (float) dt, n.start + (float) dt + n.length, bar)))
            return false;
    }
    // A bass note that moves later leaves its held predecessor ringing one 16th more.
    if (c.part == Bass && dt > 0)
        for (int i = 0; i < l.count; ++i)
        {
            const Note& p = l.notes[i];
            const float end = p.start + p.length;
            if (p.vel > 2 && p.start < (float) s - 0.5f && end > (float) s - 0.26f && end <= (float) s + 1.0e-3f
                && clashCopies (c, tonesOf (p, Bass), end, (float) t, bar) != 0)
                return false;
        }
    return true;
}

// Moves the onset at s to t. The bass keeps its sounding time: a note moved earlier keeps its
// end (its predecessor gives way), a note moved later hands that 16th to a held predecessor.
inline void moveOnset (Ctx& c, int s, int t) noexcept
{
    Lane& l = *c.lane;
    const float dt = (float) (t - s);
    const bool mono = c.part == Bass;
    if (mono && dt > 0.f)
        for (int i = 0; i < l.count; ++i)
        {
            Note& p = l.notes[i];
            const float end = p.start + p.length;
            if (p.vel > 2 && p.start < (float) s - 0.5f && end > (float) s - 0.26f && end <= (float) s + 1.0e-3f)
                p.length += dt;
        }
    float moved = -1.f;
    for (int i = 0; i < l.count; ++i)
    {
        Note& n = l.notes[i];
        if (slotOf (n.start) != s)
            continue;
        n.start += dt;
        moved = n.start;
        if (mono)
            // Moved later, it keeps its end only while a 16th of it is left (a lone hit moves by
            // two 16ths to its 'and': a shorter note would end before it starts).
            n.length = dt < 0.f ? n.length - dt : (n.length > dt + 0.25f ? n.length - dt : n.length);
    }
    if (moved < 0.f)
        return;
    sortLane (l);
    // Nothing rings into the moved onset, and the moved onset ends where the next one starts.
    float next = 1.0e9f;
    for (int i = 0; i < l.count; ++i)
        if (l.notes[i].start > moved + 1.0e-3f)
            next = std::min (next, l.notes[i].start);
    for (int i = 0; i < l.count; ++i)
    {
        Note& n = l.notes[i];
        if (n.start < moved - 1.0e-3f && n.start + n.length > moved + 1.0e-3f)
            n.length = std::max (0.25f, moved - n.start);
        else if (std::abs (n.start - moved) <= 1.0e-3f && n.start + n.length > next)
            n.length = std::max (0.25f, next - n.start);
    }
}

// Rhythm: an anchor and an off-kick event in every cell bar, then the cell's e / a share.
inline void repairRhythm (Ctx& c, float eaHi) noexcept
{
    Lane& l = *c.lane;
    auto tryMove = [&] (int s, int t) {
        if (! canMove (c, s, t))
            return false;
        moveOnset (c, s, t);
        return true;
    };
    for (int b = 0; b < c.cellBars; ++b)
    {
        int ev[kStepsPerBar];
        const int n = eventsInBar (l, b, ev);
        if (n == 0)
            continue;
        const int rot = (int) (c.seed % (std::uint32_t) n);
        bool anchor = false, offKick = false;
        for (int k = 0; k < n; ++k)
        {
            anchor = anchor || ev[k] % 2 == 0;
            offKick = offKick || ev[k] % 4 != 0;
        }
        // Every event on an e / a: one moves to its 'and' (an anchor, still off the kick).
        for (int k = 0; k < n && ! anchor; ++k)
        {
            const int s = b * kStepsPerBar + ev[(k + rot) % n];
            anchor = tryMove (s, s + (s % 4 == 1 ? 1 : -1));
        }
        // Every event on a kick slot: one moves to the 'a' before it (or the 'e' after); a lone
        // hit, which is also the bar's anchor, moves to its 'and' instead.
        for (int k = 0; k < n && ! offKick; ++k)
        {
            const int s = b * kStepsPerBar + ev[(k + rot) % n];
            offKick = n >= 2 ? (s % kStepsPerBar > 0 && tryMove (s, s - 1)) || tryMove (s, s + 1)
                             : tryMove (s, s + 2) || (s % kStepsPerBar > 0 && tryMove (s, s - 2));
        }
    }
    for (int guard = 0; guard < 4 * kStepsPerBar; ++guard)
    {
        int all[2 * kStepsPerBar];
        int n = 0, odd = 0, evenIn[2] {};
        for (int b = 0; b < c.cellBars; ++b)
        {
            int ev[kStepsPerBar];
            const int m = eventsInBar (l, b, ev);
            for (int k = 0; k < m; ++k)
            {
                all[n++] = b * kStepsPerBar + ev[k];
                odd += ev[k] % 2;
                evenIn[b] += 1 - ev[k] % 2;
            }
        }
        if (n < 2)
            return;
        const int rot = (int) (c.seed % (std::uint32_t) n);
        bool movedOne = false;
        if ((float) odd > eaHi * (float) n + 1.0e-4f)
        {
            // Too many e / a events: one moves to its 'and'.
            for (int k = 0; k < n && ! movedOne; ++k)
            {
                const int s = all[(k + rot) % n];
                if (s % 2 == 1)
                    movedOne = tryMove (s, s + (s % 4 == 1 ? 1 : -1));
            }
        }
        else if ((float) odd < 0.20f * (float) n - 1.0e-4f)
        {
            // Too square: an event whose bar keeps another anchor moves to the 'a' (a push into
            // the next beat), else the 'e'. The bass keeps its notes on the kick.
            for (int k = 0; k < n && ! movedOne; ++k)
            {
                const int s = all[(k + rot) % n];
                if (s % 2 != 0 || evenIn[s / kStepsPerBar] < 2 || (c.part == Bass && s % 4 == 0))
                    continue;
                if (s % 4 == 0)
                    movedOne = (s % kStepsPerBar > 0 && tryMove (s, s - 1)) || tryMove (s, s + 1);
                else
                    movedOne = tryMove (s, s + 1) || tryMove (s, s - 1);
            }
        }
        else
            return;
        if (! movedOne)
            return;
    }
}

// The pitch line of a cell: one value per onset (a chord's top tone), with what may replace it.
struct Cand
{
    std::int8_t tones[4] {};
    int nc = 1;
    int top = 0;
    float cost = 0.f;
    bool swap = false;
};

struct LineEvent
{
    int note = -1; // lane index of the note that carries the line at this onset
    Cand cand[24];
    int nCand = 0;
};

inline int topOf (const Note& n) noexcept
{
    int t = n.chord[0];
    for (int c = 1; c < std::clamp ((int) n.nChord, 1, 4); ++c)
        t = std::max (t, (int) n.chord[c]);
    return t;
}

inline int maxBigMoves (int moves) noexcept
{
    return std::max (1, moves / 4);
}

// The pitch rules on a line of values: 2-4 pitch classes, 1..maxBig moves of 4+ semitones.
inline bool lineOk (const int* v, int n) noexcept
{
    if (n < 2)
        return true;
    std::uint32_t pcs = 0;
    int big = 0;
    for (int i = 0; i < n; ++i)
    {
        pcs |= 1u << pcOf (v[i]);
        if (i > 0 && std::abs (v[i] - v[i - 1]) >= 4)
            ++big;
    }
    int npc = 0;
    for (; pcs != 0; pcs &= pcs - 1)
        ++npc;
    return npc >= 2 && npc <= 4 && big >= 1 && big <= maxBigMoves (n - 1);
}

// What a move of d semitones costs in the search: steps are free, a fourth, fifth or octave is
// the preferred signature (BOUNCY's bass pumps the octave first), a tritone or a displaced ninth
// is the last resort.
inline float moveCost (int d, bool octaveFirst) noexcept
{
    d = std::abs (d);
    if (d == 12 && octaveFirst)
        return 0.05f;
    if (d == 0)
        return 0.05f;
    if (d <= 2)
        return 0.f;
    if (d == 3)
        return 0.15f;
    if (d == 5 || d == 7)
        return 0.15f;
    if (d == 4)
        return 0.3f;
    if (d == 12)
        return 0.25f;
    if (d == 8 || d == 9)
        return 0.6f;
    if (d == 10 || d == 11)
        return 0.9f;
    return 1.2f;
}

// A stab voicing with `top` as its top tone: the tone of that pitch class goes to `top`, every
// tone over it folds down by octaves, and no two tones share a pitch.
inline bool revoice (const Note& n, int top, Cand& out) noexcept
{
    const int nc = std::clamp ((int) n.nChord, 1, 4);
    int v[4] {};
    int lo = 127;
    int j = -1;
    for (int c = 0; c < nc; ++c)
    {
        v[c] = n.chord[c];
        lo = std::min (lo, v[c]);
        if (pcOf (v[c]) == pcOf (top) && (j < 0 || std::abs (v[c] - top) < std::abs (v[j] - top)))
            j = c;
    }
    if (j < 0)
        return false;
    int moved = v[j] != top ? 1 : 0;
    v[j] = top;
    for (int c = 0; c < nc; ++c)
    {
        if (c == j)
            continue;
        const int was = v[c];
        while (v[c] >= top)
            v[c] -= 12;
        for (int k = 0; k < nc; ++k)
            if (k != c && k < c && v[k] == v[c])
                v[c] -= 12;
        moved += v[c] != was ? 1 : 0;
    }
    for (int c = 0; c < nc; ++c)
        if (v[c] < lo - 12 || v[c] < -36 || v[c] > 48)
            return false;
    out.nc = nc;
    for (int c = 0; c < 4; ++c)
        out.tones[c] = (std::int8_t) (c < nc ? v[c] : 0);
    out.top = top;
    // A recorded chord sample sounds from chord[0]: a voicing that keeps it keeps that register.
    out.cost = moved == 0 ? 0.f
                          : 0.6f + 0.04f * (float) std::abs (top - topOf (n)) + 0.1f * (float) moved
                                + (v[0] != n.chord[0] ? 0.5f : 0.f);
    return true;
}

// Pitch: re-place or swap as little as the rules need (a small Viterbi search over the cell).
inline void repairPitch (Ctx& c, std::uint32_t rollPcs) noexcept
{
    Lane& l = *c.lane;
    const Groove& g = *c.g;
    LineEvent ev[2 * kStepsPerBar];
    int n = 0;
    for (int b = 0; b < c.cellBars; ++b)
    {
        int at[kStepsPerBar];
        const int m = eventsInBar (l, b, at);
        for (int k = 0; k < m; ++k)
        {
            const int abs = b * kStepsPerBar + at[k];
            int best = -1;
            for (int i = 0; i < l.count; ++i)
                if (l.notes[i].vel > 2 && slotOf (l.notes[i].start) == abs
                    && (best < 0 || topOf (l.notes[i]) > topOf (l.notes[best])))
                    best = i;
            ev[n++].note = best;
        }
    }
    if (n < 2)
        return;
    int line[2 * kStepsPerBar];
    std::uint32_t pcs = 0;
    int lo = 127, hi = -128;
    for (int i = 0; i < n; ++i)
        line[i] = topOf (l.notes[ev[i].note]);
    if (lineOk (line, n))
        return;
    for (int i = 0; i < l.count; ++i)
        if (l.notes[i].vel > 2 && l.notes[i].nChord <= 1)
        {
            lo = std::min (lo, (int) l.notes[i].chord[0]);
            hi = std::max (hi, (int) l.notes[i].chord[0]);
        }
    hi = std::max (hi, lo + 12);
    for (int i = 0; i < n; ++i)
        pcs |= 1u << pcOf (line[i]);
    int npc = 0;
    for (std::uint32_t m = pcs; m != 0; m &= m - 1)
        ++npc;
    const int swapBudget = npc > 4 ? std::min (2, npc - 4) : 1;

    // Candidates per onset.
    for (int i = 0; i < n; ++i)
    {
        LineEvent& e = ev[i];
        const Note& note = l.notes[e.note];
        const int cb = slotOf (note.start) / kStepsPerBar;
        const int orig = line[i];
        if (c.part == Stab || note.nChord > 1)
        {
            const int nc = std::clamp ((int) note.nChord, 1, 4);
            for (int t = orig - 7; t <= orig + 7 && e.nCand < 24; ++t)
            {
                bool tone = false;
                for (int k = 0; k < nc; ++k)
                    tone = tone || pcOf (note.chord[k]) == pcOf (t);
                Cand cd;
                if (tone && revoice (note, t, cd))
                    e.cand[e.nCand++] = cd;
            }
            continue;
        }
        auto add = [&] (int v, float cost, bool swap) {
            if (e.nCand >= 24 || v < lo || v > hi)
                return;
            for (int k = 0; k < e.nCand; ++k)
                if (e.cand[k].top == v)
                    return;
            Cand cd;
            cd.tones[0] = (std::int8_t) v;
            cd.top = v;
            cd.cost = cost;
            cd.swap = swap;
            e.cand[e.nCand++] = cd;
        };
        for (int k = -2; k <= 2; ++k)
            add (orig + 12 * k, 0.5f * (float) std::abs (k), false);
        const int slot = slotOf (note.start);
        const int in = slot % kStepsPerBar;
        // Chord tones sounding with the note (stab / pad over the bass; bass and chords under a pluck).
        std::uint32_t over = pcsOver (g.lane[Stab], Stab, note.start, note.start + note.length)
                           | pcsOver (g.lane[Pad], Pad, note.start, note.start + note.length);
        if (c.part != Bass)
            over |= pcsOver (g.lane[Bass], Bass, note.start, note.start + note.length);
        for (int p = 0; p < 12; ++p)
        {
            if (p == pcOf (orig) || p == 1 || ((rollPcs >> p) & 1u) == 0)
                continue;
            if (c.part == Bass && p == 0 && (in == 1 || in == 3) && rootAt (l, slot - in + (in == 1 ? 3 : 1)))
                continue;
            if (! addsNoClash (clashCopies (c, tonesOf (note, c.part), note.start, note.start + note.length, cb),
                               clashCopies (c, 1u << p, note.start, note.start + note.length, cb)))
                continue;
            int d = p - pcOf (orig);
            if (d > 6)
                d -= 12;
            if (d < -6)
                d += 12;
            const float base = ((pcs >> p) & 1u) != 0 ? 1.6f : (((over >> p) & 1u) != 0 ? 1.8f : 2.0f);
            for (int k = -1; k <= 1; ++k)
                add (orig + d + 12 * k, base + 0.04f * (float) std::abs (d + 12 * k) + 0.2f * (float) std::abs (k), true);
        }
    }

    // Viterbi over (candidate, big moves, swaps, pitch classes).
    struct St
    {
        std::uint32_t key;
        float cost;
        int prev;
    };
    const int maxBig = maxBigMoves (n - 1);
    auto keyOf = [] (int cand, int big, int swaps, std::uint32_t p) {
        return (std::uint32_t) cand | ((std::uint32_t) big << 5) | ((std::uint32_t) swaps << 9) | (p << 11);
    };
    std::vector<std::vector<St>> layer ((size_t) n);
    for (int k = 0; k < ev[0].nCand; ++k)
    {
        const Cand& cd = ev[0].cand[k];
        const int sw = cd.swap ? 1 : 0;
        if (sw <= swapBudget)
            layer[0].push_back ({ keyOf (k, 0, sw, 1u << pcOf (cd.top)), cd.cost, -1 });
    }
    for (int i = 1; i < n; ++i)
    {
        auto& out = layer[(size_t) i];
        const auto& in = layer[(size_t) i - 1];
        for (int si = 0; si < (int) in.size(); ++si)
        {
            const St& s = in[(size_t) si];
            const int ci = (int) (s.key & 31u);
            const int big = (int) ((s.key >> 5) & 15u);
            const int sw = (int) ((s.key >> 9) & 3u);
            const std::uint32_t p = s.key >> 11;
            const int prevTop = ev[i - 1].cand[ci].top;
            for (int k = 0; k < ev[i].nCand; ++k)
            {
                const Cand& cd = ev[i].cand[k];
                const int d = cd.top - prevTop;
                const int big2 = big + (std::abs (d) >= 4 ? 1 : 0);
                const int sw2 = sw + (cd.swap ? 1 : 0);
                if (big2 > maxBig || sw2 > swapBudget)
                    continue;
                // BOUNCY's bass keeps a written octave pump where it can.
                const float lost = c.octaveSignature && std::abs (line[i] - line[i - 1]) == 12 && std::abs (d) != 12 ? 0.4f : 0.f;
                out.push_back ({ keyOf (k, big2, sw2, p | (1u << pcOf (cd.top))),
                                 s.cost + cd.cost + moveCost (d, c.octaveSignature) + lost, si });
            }
        }
        // One state per key, the cheapest; a beam keeps the search small.
        std::sort (out.begin(), out.end(), [] (const St& a, const St& b) {
            return a.key != b.key ? a.key < b.key : a.cost < b.cost;
        });
        out.erase (std::unique (out.begin(), out.end(), [] (const St& a, const St& b) { return a.key == b.key; }), out.end());
        if (out.size() > 3000)
        {
            std::nth_element (out.begin(), out.begin() + 3000, out.end(), [] (const St& a, const St& b) { return a.cost < b.cost; });
            out.resize (3000);
        }
    }
    int best = -1;
    const auto& last = layer[(size_t) n - 1];
    for (int si = 0; si < (int) last.size(); ++si)
    {
        const St& s = last[(size_t) si];
        const int big = (int) ((s.key >> 5) & 15u);
        int np = 0;
        for (std::uint32_t m = s.key >> 11; m != 0; m &= m - 1)
            ++np;
        if (big >= 1 && np >= 2 && np <= 4 && (best < 0 || s.cost < last[(size_t) best].cost))
            best = si;
    }
    if (best < 0)
        return;
    const Lane was = l;
    for (int i = n - 1, si = best; i >= 0; --i)
    {
        const St& s = layer[(size_t) i][(size_t) si];
        const Cand& cd = ev[i].cand[s.key & 31u];
        si = s.prev;
        if (cd.cost <= 0.f)
            continue; // as written
        Note& note = l.notes[ev[i].note];
        note.nChord = (std::uint8_t) cd.nc;
        for (int k = 0; k < 4; ++k)
            note.chord[k] = cd.tones[k];
    }
    // Two swaps in one bar can meet as the banned 2&4 root figure: then the cell stays as written.
    if (c.part == Bass)
        for (int b = 0; b < c.cellBars; ++b)
            if (rootAt (l, b * kStepsPerBar + 1) && rootAt (l, b * kStepsPerBar + 3))
                l = was;
}

// The cell rules on one hook lane. For a vox hook `cell` is its one-bar cell (laid out after);
// otherwise the hook lane's bars 1-2, restated through the phrase afterwards. True when a note moved.
inline bool shapeHookCell (Groove& g, int part, Lane& cell, int cellBars, int bars, const bool* cellPlays) noexcept
{
    if (cell.count <= 0 || bars < 2)
        return false;
    Ctx c;
    c.g = &g;
    c.lane = &cell;
    c.part = part;
    c.cellBars = cellBars;
    for (int b = 0; b < bars && b < kMaxBars; ++b)
    {
        const int cb = cellBars == 1 ? (cellPlays != nullptr && ! cellPlays[b] ? -1 : 0)
                                     : (bars >= 4 ? phrase_lock::sourceBar (b, bars) : b);
        if (cb == 0 || cb == 1)
            c.copies[cb][c.nCopies[cb]++] = b;
    }
    c.nudge = ! (part == Bass && g.bassMode == BassModePulse);
    c.keepBassClass = part == Stab && (g.dialect == Long || g.dialect == Pop);
    c.octaveSignature = part == Bass && g.dialect == Pop;
    c.seed = (cell.seed ? cell.seed : 1u) * 0x9E3779B9u;
    c.seed ^= c.seed >> 16;
    const Lane before = cell;
    if (c.nudge)
        repairRhythm (c, part == Vox ? 0.60f : 0.50f);
    if (part == Bass || part == Stab || part == Pluck)
    {
        std::uint32_t roll = 0;
        for (int p : { (int) Bass, (int) Stab, (int) Pad, (int) Pluck })
            for (int i = 0; i < g.lane[p].count; ++i)
                if (g.lane[p].notes[i].vel > 2)
                    roll |= tonesOf (g.lane[p].notes[i], p);
        repairPitch (c, roll);
    }
    if (before.count != cell.count)
        return true;
    for (int i = 0; i < cell.count; ++i)
    {
        const Note& a = before.notes[i];
        const Note& b = cell.notes[i];
        if (std::abs (a.start - b.start) > 1.0e-6f || std::abs (a.length - b.length) > 1.0e-6f || a.nChord != b.nChord
            || std::memcmp (a.chord, b.chord, sizeof a.chord) != 0)
            return true;
    }
    return false;
}
} // namespace hook_cell

// Applies the plan to the regenerated lanes (kept lanes are never touched).
// hitsOnStab is the IN STAB switch (the user's own call, like the STAB knob): the hits are then
// not a part of their own but a layer on the stab's hits, so they play wherever the stab still
// plays and nowhere else. Without it the hits lane is never the hook or the support and stays out.
inline void applyHookPlan (Groove& g, const HookPlan& plan, int bars, std::uint32_t partMask,
                           bool hitsOnStab = false) noexcept
{
    using namespace hook_detail;
    lastHookPlan() = plan;
    auto regen = [&] (int p) { return ((partMask >> p) & 1u) != 0; };
    const bool stabPlays = plan.hook == Stab || plan.support == Stab || (! regen (Stab) && hasNotes (g.lane[Stab]));
    const bool hitsLayer = hitsOnStab && stabPlays;
    // Silence every melodic lane that is not the floor, the hook or the support.
    for (int p : { (int) Stab, (int) Pluck, (int) Pad, (int) Hits, (int) Vox })
        if (regen (p) && p != plan.hook && p != plan.support && ! (p == Hits && hitsLayer))
            g.lane[p].count = 0;

    // Bar 8 is one clear move (catchiness research #2): only the hook and the bass keep their own
    // turn; every other riff lane replays its bar 2 there, so bar 1 lands clean. IN STAB hits are a
    // layer on the stab's hits and turn with it. The pad keeps its swells (it is never restated).
    // Runs before the support is cut to its gaps and entry bar, so the last bar gets the same cut
    // as every other copy of bar 2.
    for (int p : { (int) Stab, (int) Pluck, (int) Hits, (int) Vox })
        if (regen (p) && p != plan.hook && ! (p == Hits && plan.hook == Stab))
            replayBar2AtTurn (g.lane[p], bars);

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
        // The hook cell rules on the one-bar cell, checked against every bar it plays in.
        bool plays[kMaxBars] {};
        for (int b = 0; b < bars && b < kMaxBars; ++b)
            plays[b] = ! (b == bars - 1 && last.count > 0);
        hook_cell::shapeHookCell (g, Vox, cell, 1, bars, plays);
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

    // The other hook lanes: the cell rules on bars 1-2, then the A B A B A B A B'' restatement.
    if ((plan.hook == Bass || plan.hook == Stab || plan.hook == Pluck) && regen (plan.hook))
    {
        Lane& h = g.lane[plan.hook];
        const Lane before = h;
        if (hook_cell::shapeHookCell (g, plan.hook, h, 2, bars, nullptr))
        {
            if (plan.hook == Bass)
            {
                // Held notes are copied as they ring (the motif's own bar 1 is not cut at bar 2),
                // bar 4 keeps its answer half; JACKIN's own loop takes the cell's edits by replay.
                if (g.dialect == Jackin)
                    replayMotifEdit (h, before, bars);
                else
                    restateBass (h, bars, false);
                keepMonophonic (h, false);
            }
            else
                restateMotif (h, bars);
        }
    }

    // The stab rides the bass's rhythm when the bass is the hook (the stab is its support), and in
    // BOUNCY whenever both play (hook_detail::stabOnBass). A BOUNCY stab hook takes it here, before
    // its slots are read as the hook's.
    const bool bouncy = g.dialect == Pop;
    if (bouncy && plan.hook == Stab && regen (Stab) && regen (Bass))
        stabOnBass (g.lane[Stab], g.lane[Bass], bars, nullptr, false);

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
        const int lastBar = bars - 1;
        for (int i = 0; i < src.count; ++i)
        {
            Note n = src.notes[i];
            const int s = slotOf (n.start);
            if (phrase_lock::barOf (n.start) < plan.supportFromBar)
                continue;
            // A pad is a bed under everything; other support parts answer in the hook's gaps. At
            // the turn the support (bar 2 replayed) keeps out of the hook's bar-2 slots as well as
            // its turn: it plays no more of bar 2 there than it does in every other bar.
            const bool turn = bars >= 4 && s / kStepsPerBar == lastBar;
            if (plan.support != Pad && plan.hook != Bass && s >= 0 && s < kMaxSteps
                && (hookAt[s] || (turn && hookAt[s - (lastBar - 1) * kStepsPerBar])))
                continue;
            n.vel = (std::uint8_t) std::max (40, (int) n.vel - 8);
            sp.notes[sp.count++] = n;
        }
        // A stab supporting a bass hook (any style), or any BOUNCY stab: onto the bass's attacks,
        // still clear of a vox hook's own slots. The caller runs the clash rule again after this.
        if (plan.support == Stab && regen (Bass) && (plan.hook == Bass || bouncy))
            stabOnBass (sp, g.lane[Bass], bars, plan.hook == Bass ? nullptr : hookAt, true);
    }
    // IN STAB: the hits keep only the stab onsets that are still there.
    if (hitsLayer && regen (Hits))
    {
        Lane& h = g.lane[Hits];
        const Lane& st = g.lane[Stab];
        int w = 0;
        for (int i = 0; i < h.count; ++i)
        {
            bool onStab = false;
            for (int k = 0; k < st.count && ! onStab; ++k)
                onStab = std::fabs (h.notes[i].start - st.notes[k].start) < 0.01f;
            if (onStab)
                h.notes[w++] = h.notes[i];
        }
        h.count = w;
    }
}
} // namespace rolla
