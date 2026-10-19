#include "Generator.h"
#include "Scale.h"
#include "Originality.h"
#include "RuleComposer.h"
#include "RuleTables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>

#if defined (_MSC_VER)
#include <intrin.h>
#endif

namespace rolla
{
namespace
{

// The roll being written (set at the top of generateOnce). The rewrite passes
// draw every pitch they change from the style's model under this mode, so no
// pass carries a fixed degree list of its own.
struct RollCtx
{
    int dialect = UkMin;
    int mode = 1; // phrase::Aeolian
};
thread_local RollCtx tRoll;

// A pitch class from the style's first-order degree table, one step from
// prevPc, in the roll's mode, never one in `avoid` (bit pc) and never the b2.
// -1 if 12 draws find none.
int drawPc (int prevPc, unsigned avoid, std::uint32_t hash)
{
    // The b2 is the composer's (and JACKIN's flat-2 flavour's), under its cap:
    // a rewrite pass never adds one.
    avoid |= 1u << 1;
    for (int t = 0; t < 12; ++t)
    {
        const int pc = rules::sampleDegreeSemis (tRoll.dialect, tRoll.mode, ((prevPc % 12) + 12) % 12,
                                                 (hash ? hash : 1u) * 0x9E3779B1u + 0x632BE5ABu * (std::uint32_t) (t + 1));
        if ((avoid & (1u << pc)) == 0u)
            return pc;
    }
    return -1;
}

constexpr int kLoop = 8 * 16;

int clampi (int v, int lo, int hi) noexcept
{
    return std::max (lo, std::min (hi, v));
}

bool kickSlot (int slot) noexcept
{
    return slot == 0 || slot == 4 || slot == 8 || slot == 12;
}

bool scalePc (int dialect, int semi) noexcept
{
    int pc = semi % 12;
    if (pc < 0)
        pc += 12;
    if (dialect == Jackin)
        return pc == 0 || pc == 1 || pc == 2 || pc == 3 || pc == 5 || pc == 7 || pc == 8 || pc == 10;
    return pc != 4 && pc != 6 && pc != 11;
}

int snapScale (int dialect, int semi) noexcept
{
    if (scalePc (dialect, semi))
        return semi;
    for (int d = 1; d <= 6; ++d)
    {
        if (scalePc (dialect, semi - d))
            return semi - d;
        if (scalePc (dialect, semi + d))
            return semi + d;
    }
    return 0;
}

struct Ev
{
    float start = 0;
    float len = 1;
    std::int8_t chord[4] {};
    std::uint8_t nChord = 1;
    std::uint8_t vel = 100;
    std::uint8_t flags = 0;
};

void clearLane (Lane& lane, std::uint32_t seed)
{
    lane.seed = seed ? seed : 1u;
    lane.count = 0;
}

bool pushEv (Lane& lane, const Ev& e, int bars)
{
    if (lane.count >= kMaxNotes || e.len < 0.25f)
        return false;
    const float end = (float) (bars * kStepsPerBar);
    if (e.start < 0.f || e.start >= end)
        return false;
    Ev n = e;
    if (n.start + n.len > end)
        n.len = end - n.start;
    if (n.len < 0.25f)
        return false;
    lane.notes[lane.count].start = n.start;
    lane.notes[lane.count].length = n.len;
    lane.notes[lane.count].nChord = n.nChord;
    lane.notes[lane.count].vel = n.vel;
    lane.notes[lane.count].flags = n.flags;
    for (int c = 0; c < 4; ++c)
        lane.notes[lane.count].chord[c] = n.chord[c];
    ++lane.count;
    return true;
}

void clampLane (Lane& lane, int bars)
{
    const float end = (float) (bars * kStepsPerBar);
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        Note n = lane.notes[i];
        if (n.start >= end)
            continue;
        if (n.start + n.length > end)
            n.length = end - n.start;
        if (n.length < 0.25f)
            continue;
        lane.notes[w++] = n;
    }
    lane.count = w;
}

// 0.90 at BUSY 0, 1.0 at the shipped 0.40, 1.36 at BUSY 1. The low end is
// compressed so BUSY 0 still lands inside every style's density band (Maestro
// 655: the gate runs BUSY 0.40 and BUSY 0, and both must pass).
float busyFactor (float busy) noexcept
{
    const float b = std::clamp (busy, 0.f, 1.f);
    return b < 0.40f ? 0.90f + 0.25f * b : 1.0f + 0.60f * (b - 0.40f);
}

// Scale one bar's attacks around the phrase the style already ships. Low BUSY
// drops the latest hits. High BUSY adds off-kick fills, never on a kick, and
// never into the last bar's turn rest. BUSY 0.40 is a no-op, so the scored
// phrase stays the one the lane was written for.
void applyBusyDensity (Lane& lane, int dialect, int bars, float busy, std::uint32_t seed)
{
    const float factor = busyFactor (busy);
    if (std::fabs (factor - 1.f) < 0.02f || lane.count <= 0 || bars <= 0)
        return;

    Rng rng (seed ^ 0xB145u);
    bool used[8][16] {};
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st >= 0 && st < bars * 16 && st / 16 < 8)
            used[st / 16][st % 16] = true;
    }

    Note out[kMaxNotes];
    int w = 0;
    const int lastBar = std::min (bars, 8) - 1;
    static const int fillSlots[] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 };

    // Low BUSY: the same number of notes comes out, but only from bars of the
    // sparser parity (bars 1/3/5/7 or 2/4/6/8), so the 2-bar pattern still
    // repeats and the bar densities spread instead of evening out (A&R loop
    // repeat and CONSTANT_DENSITY). Inside a bar the front half thins first,
    // never the bar's first note, so bar-to-bar changes stay in the back halves.
    int dropAt[8] {};
    bool thinBar[8] {};
    if (factor < 1.f)
    {
        int count[8] {};
        int total = 0, parityCount[2] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st >= 0 && st < bars * 16 && st / 16 < 8)
            {
                ++count[st / 16];
                ++total;
                parityCount[(st / 16) & 1]++;
            }
        }
        int drops = 0;
        for (int b = 0; b < std::min (bars, 8); ++b)
            drops += count[b] - std::clamp ((int) std::lround ((float) count[b] * factor), 1, 9);
        const int par = parityCount[0] <= parityCount[1] ? 0 : 1;
        int nPar = 0;
        for (int b = par; b < std::min (bars, 8); b += 2)
            ++nPar;
        const int each = nPar > 0 ? (drops + nPar - 1) / nPar : 0;
        for (int b = par; b < std::min (bars, 8); b += 2)
        {
            thinBar[b] = true;
            dropAt[b] = std::min (each, std::max (0, count[b] - 2));
        }
    }
    for (int bar = 0; bar < bars && bar < 8; ++bar)
    {
        int idx[16];
        int n = 0;
        const float origin = (float) (bar * 16);
        for (int i = 0; i < lane.count && n < 16; ++i)
            if (lane.notes[i].start >= origin && lane.notes[i].start < origin + 16.f)
                idx[n++] = i;
        if (n <= 0)
            continue;
        std::sort (idx, idx + n, [&] (int a, int b) { return lane.notes[a].start < lane.notes[b].start; });
        int target = (int) std::lround ((float) n * factor);
        target = std::clamp (target, 1, 9);
        if (bar == lastBar && target > n)
            target = n;
        if (factor < 1.f)
        {
            // Thinning: drop from this bar only if it is one of the thinned
            // parity, front-half notes (not the first) before back-half ones.
            target = thinBar[bar] ? std::max (2, n - dropAt[bar]) : n;
            target = std::min (target, n);
            bool drop[16] {};
            int toDrop = n - target;
            for (int pass = 0; pass < 2 && toDrop > 0; ++pass)
                for (int k = n - 1; k >= 1 && toDrop > 0; --k)
                {
                    const int sl = (int) std::lround (lane.notes[idx[k]].start) % 16;
                    if (! drop[k] && (pass == 1 || sl < 8))
                    {
                        drop[k] = true;
                        --toDrop;
                    }
                }
            for (int k = 0; k < n; ++k)
            {
                const int st = (int) std::lround (lane.notes[idx[k]].start);
                if (drop[k])
                {
                    if (st >= 0 && st / 16 == bar && st / 16 < 8)
                        used[bar][st % 16] = false;
                }
                else if (w < kMaxNotes)
                    out[w++] = lane.notes[idx[k]];
            }
            continue;
        }
        const int keep = std::min (n, target);
        for (int k = keep; k < n; ++k)
        {
            const int st = (int) std::lround (lane.notes[idx[k]].start);
            if (st >= 0 && st / 16 == bar && st / 16 < 8)
                used[bar][st % 16] = false;
        }
        for (int k = 0; k < keep && w < kMaxNotes; ++k)
            out[w++] = lane.notes[idx[k]];

        const float fillLen = dialect == Pop ? 1.1f : dialect == Long ? 2.f : dialect == Jackin ? 1.5f : 1.5f;
        const auto fillPitch = (std::int8_t) (dialect == Pop ? 7 : 0);
        int have = keep;
        int guard = 0;
        while (have < target && w < kMaxNotes && guard++ < 16)
        {
            int slot = -1;
            const int spin = (int) (rng.next() * 12.0);
            for (int t = 0; t < 12; ++t)
            {
                const int s = fillSlots[(spin + t) % 12];
                if (! used[bar][s])
                {
                    slot = s;
                    break;
                }
            }
            if (slot < 0)
                break;
            used[bar][slot] = true;
            Note nn;
            nn.start = origin + (float) slot;
            float next = origin + 16.f;
            for (int i = 0; i < w; ++i)
                if (out[i].start > nn.start && out[i].start < next)
                    next = out[i].start;
            nn.length = std::max (0.5f, std::min (fillLen, next - nn.start));
            nn.chord[0] = fillPitch;
            nn.nChord = 1;
            nn.vel = 104;
            for (int i = 0; i < w; ++i)
                if (out[i].start < nn.start && out[i].start + out[i].length > nn.start)
                    out[i].length = std::max (0.5f, nn.start - out[i].start);
            out[w++] = nn;
            ++have;
        }
    }
    std::sort (out, out + w, [] (const Note& a, const Note& b) { return a.start < b.start; });
    lane.count = w;
    for (int i = 0; i < w; ++i)
        lane.notes[i] = out[i];
}

int pcOf (int semi) noexcept
{
    int pc = semi % 12;
    if (pc < 0)
        pc += 12;
    return pc;
}

// 0 dotted grid, 1 flat 2nd, 2 sustained stabs, 3 offbeat counter.
int jackinFlavour (std::uint32_t seed) noexcept
{
    return (int) (((seed ? seed : 1u) * 0x9E3779B9u) % 4u);
}

bool deepIsDark (float dark, std::uint32_t seed) noexcept
{
    if (dark >= 0.75f)
        return true;
    if (dark <= 0.25f)
        return false;
    return (((seed ? seed : 1u) * 0x85EBCA6Bu) % 100u) < 40u;
}

bool popIsPulse (float bounce, std::uint32_t seed) noexcept
{
    if (bounce >= 0.80f || bounce <= 0.15f)
        return false;
    // Genre model v1.2: pulse is a batch quota of 30-40% (kPulseQuota 0.35),
    // not an independent 25% draw. The knob's extremes stay on the bounce
    // grid. The seed's own position in [0, 2^32) is the draw, so a batch's
    // golden-ratio seed walk (seed + roll * 0x9E3779B9) gets an even 35% share:
    // 6-8 pulse rolls in any 20. The seed is the roll's drums seed, which the
    // originality and skeleton redraws never move, so a redraw keeps its mode.
    const std::uint32_t s = seed ? seed : 1u;
    const auto quota = (std::uint32_t) std::lround (rules::tables::kPulseQuota * 100.f);
    return (std::uint32_t) (((std::uint64_t) s * 100u) >> 32) < quota;
}

float rootShareOf (const Lane& lane) noexcept
{
    float root = 0.f;
    float tot = 0.f;
    for (int i = 0; i < lane.count; ++i)
    {
        const float len = std::max (0.f, lane.notes[i].length);
        tot += len;
        if (pcOf (lane.notes[i].chord[0]) == 0)
            root += len;
    }
    return tot > 0.f ? root / tot : 0.f;
}

// A non-root pitch class drawn from the style's degree table under the mode.
int sampleColour (int dialect, int mode, std::uint32_t hash)
{
    for (int t = 0; t < 6; ++t)
    {
        const int pc = rules::sampleDegreeSemis (dialect, mode, 0, hash * 0x9E3779B1u + (std::uint32_t) t);
        if (pc != 0)
            return pc;
    }
    return 7;
}

void fitRootShare (Lane& lane, float lo, float hi, std::uint32_t seed, int dialect, int mode)
{
    int guard = 0;
    while (rootShareOf (lane) > hi && guard++ < lane.count)
    {
        int best = -1;
        float bestLen = -1.f;
        int roots = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            if (pcOf (lane.notes[i].chord[0]) != 0)
                continue;
            ++roots;
            if (lane.notes[i].length > bestLen)
            {
                bestLen = lane.notes[i].length;
                best = i;
            }
        }
        if (best < 0 || roots <= 1)
            break;
        lane.notes[best].chord[0] = (std::int8_t) sampleColour (dialect, mode, (seed ^ 0xC010Fu) + (std::uint32_t) guard);
    }
    guard = 0;
    while (rootShareOf (lane) < lo && guard++ < lane.count)
    {
        int best = -1;
        float bestLen = 1.0e9f;
        for (int i = 0; i < lane.count; ++i)
        {
            if (pcOf (lane.notes[i].chord[0]) == 0)
                continue;
            if (lane.notes[i].length < bestLen)
            {
                bestLen = lane.notes[i].length;
                best = i;
            }
        }
        if (best < 0)
            break;
        lane.notes[best].chord[0] = 0;
    }
}

void densifyDeepDark (Lane& lane, int bars, std::uint32_t seed, int mode)
{
    for (int i = 0; i < lane.count; ++i)
        if (lane.notes[i].length > 3.2f)
            lane.notes[i].length = 2.2f;

    bool used[8][16] {};
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st >= 0 && st < bars * 16 && st / 16 < 8)
            used[st / 16][st % 16] = true;
    }
    const int last = std::min (bars, 8) - 1;
    for (int bar = 0; bar < bars && bar < 8; ++bar)
    {
        int have = 0;
        for (int s = 0; s < 16; ++s)
            if (used[bar][s])
                ++have;
        // Dark stays busier than the sparse line, and stops around 6-7 attacks.
        int target = 6 + (int) (((seed >> (bar & 3)) + (std::uint32_t) bar) % 2u);
        if (bar == last)
            target = std::min (target, 6);
        int guard = 0;
        while (have < target && lane.count < kMaxNotes && guard++ < 16)
        {
            // Fill slots come from the model's DEEP onset weights (off the kick
            // steps; the turn bar keeps its last beat free).
            bool allowed[16];
            for (int s = 0; s < 16; ++s)
                allowed[s] = ! used[bar][s] && ! kickSlot (s) && ! (bar == last && s >= 12);
            const int group = bar == last ? 2 : (bar == 3 ? 1 : 0);
            const int slot = rules::sampleBassSlot (Long, group, allowed,
                                                    (seed ^ 0xD3F1u) * 0x9E3779B1u + (std::uint32_t) (bar * 32 + guard));
            if (slot < 0)
                break;
            used[bar][slot] = true;
            Note n;
            n.start = (float) (bar * 16 + slot);
            n.length = 1.6f;
            // The fill's pitch is one Markov step from the note before it.
            int prevSemi = 0;
            float prevStart = -1.f;
            for (int i = 0; i < lane.count; ++i)
                if (lane.notes[i].start < n.start && lane.notes[i].start > prevStart)
                {
                    prevStart = lane.notes[i].start;
                    prevSemi = lane.notes[i].chord[0];
                }
            n.chord[0] = (std::int8_t) rules::sampleDegreeSemis (Long, mode, prevSemi,
                                                                 (seed ^ 0xDA4Cu) * 0x9E3779B1u + (std::uint32_t) (bar * 16 + slot));
            n.nChord = 1;
            n.vel = 104;
            for (int i = 0; i < lane.count; ++i)
                if (lane.notes[i].start < n.start && lane.notes[i].start + lane.notes[i].length > n.start)
                    lane.notes[i].length = std::max (0.5f, n.start - lane.notes[i].start);
            lane.notes[lane.count++] = n;
            ++have;
        }
        // A phrase that was already denser than the cap loses its latest fills.
        while (have > target)
        {
            int drop = -1;
            int latest = -1;
            for (int i = 0; i < lane.count; ++i)
            {
                const int st = (int) std::lround (lane.notes[i].start);
                if (st / 16 != bar)
                    continue;
                if (st % 16 > latest)
                {
                    latest = st % 16;
                    drop = i;
                }
            }
            if (drop < 0)
                break;
            used[bar][latest] = false;
            for (int i = drop + 1; i < lane.count; ++i)
                lane.notes[i - 1] = lane.notes[i];
            --lane.count;
            --have;
        }
    }
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
}

void moveJackinNote (Lane& lane, int bars, int parity, std::uint32_t salt)
{
    const int turn = std::min (bars, 8) - 1;
    for (int b = parity; b < bars && b < 8; b += 2)
    {
        if (b == turn)
            continue;
        bool used[16] {};
        int movable[12];
        int n = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st / 16 != b)
                continue;
            const int slot = st % 16;
            if (slot >= 0 && slot < 16)
                used[slot] = true;
            if (slot >= 1 && slot <= 13 && ! kickSlot (slot))
                movable[n++] = i;
        }
        // The destination is drawn from the model's JACKIN onset weights.
        bool allowed[16];
        for (int s = 0; s < 16; ++s)
            allowed[s] = s >= 1 && s <= 13 && ! kickSlot (s) && ! used[s];
        const int dest = rules::sampleBassSlot (Jackin, b == 3 ? 1 : 0, allowed, salt * 0x9E3779B1u + (std::uint32_t) b);
        int src = -1;
        for (int k = n - 1; k >= 0; --k)
        {
            const int slot = (int) std::lround (lane.notes[movable[k]].start) % 16;
            if (slot != dest && slot < 14)
            {
                src = movable[k];
                break;
            }
        }
        if (src < 0 || dest < 0)
            continue;
        lane.notes[src].start = (float) (b * 16 + dest);
        lane.notes[src].length = 2.f;
    }
}

void diversifyJackin (Lane& lane, int bars, std::uint32_t seed)
{
    moveJackinNote (lane, bars, 0, seed);
    moveJackinNote (lane, bars, 0, seed + 1u);
    moveJackinNote (lane, bars, 1, seed + 2u);
    moveJackinNote (lane, bars, 1, seed + 3u);
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
}

// JACKIN's dotted flavour (A&R: >= 55% of attacks on the dotted grid 0/3/6/9/12/15)
// is a statistical lean: off-grid attacks move to dotted-grid positions drawn by
// the model's JACKIN onset weights. No fixed cell or order.
void leanJackinDotted (Lane& lane, int bars, std::uint32_t seed)
{
    const int turn = std::min (bars, 8) - 1;
    for (int b = 0; b < bars && b < 8; ++b)
    {
        if (b == turn)
            continue;
        bool used[16] {};
        int idx[16];
        int n = 0;
        int dots = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st / 16 != b)
                continue;
            const int slot = st % 16;
            if (slot < 0 || slot > 15)
                continue;
            used[slot] = true;
            idx[n++] = i;
            if (slot == 3 || slot == 6 || slot == 9 || slot == 15 || slot == 0 || slot == 12)
                ++dots;
        }
        int guard = 0;
        while (n > 0 && dots * 5 < n * 3 && guard++ < 4)
        {
            bool allowed[16] {};
            for (int s : { 3, 6, 9, 15 })
                allowed[s] = ! used[s];
            const int dest = rules::sampleBassSlot (Jackin, b == 3 ? 1 : 0, allowed,
                                                    (seed ^ 0xD077u) * 0x9E3779B1u + (std::uint32_t) (b * 8 + guard));
            if (dest < 0)
                break;
            int src = -1;
            for (int k = 0; k < n; ++k)
            {
                const int slot = (int) std::lround (lane.notes[idx[k]].start) % 16;
                if (slot == 3 || slot == 6 || slot == 9 || slot == 15 || slot >= 14 || kickSlot (slot))
                    continue;
                src = idx[k];
                break;
            }
            if (src < 0)
                break;
            const int old = (int) std::lround (lane.notes[src].start) % 16;
            used[old] = false;
            used[dest] = true;
            lane.notes[src].start = (float) (b * 16 + dest);
            lane.notes[src].length = 2.f;
            ++dots;
        }
    }
}

// The line carries at least two colours besides the root (A&R F2: 2-5 pitch
// classes). Missing colours are drawn from the model and replace the latest roots.
void ensureColour (Lane& lane, std::uint32_t seed)
{
    for (int k = 0; k < 3; ++k)
    {
        unsigned present = 0u;
        for (int i = 0; i < lane.count; ++i)
            present |= 1u << pcOf (lane.notes[i].chord[0]);
        int colours = 0;
        for (int pc = 1; pc < 12; ++pc)
            if (present & (1u << pc))
                ++colours;
        if (colours >= 2)
            return;
        const int want = drawPc (0, present | 1u, (seed ^ 0xC0105u) + (std::uint32_t) k);
        if (want < 0)
            return;
        bool done = false;
        for (int i = lane.count - 1; i >= 1 && ! done; --i)
            if (pcOf (lane.notes[i].chord[0]) == 0)
            {
                lane.notes[i].chord[0] = (std::int8_t) want;
                done = true;
            }
        if (! done)
            return;
    }
}

void paintFlat2 (Lane& lane)
{
    int guard = 0;
    while (guard++ < lane.count)
    {
        float b2 = 0.f;
        float tot = 0.f;
        for (int i = 0; i < lane.count; ++i)
        {
            tot += std::max (0.f, lane.notes[i].length);
            if (pcOf (lane.notes[i].chord[0]) == 1)
                b2 += std::max (0.f, lane.notes[i].length);
        }
        if (tot <= 0.f || b2 / tot >= 0.12f)
            break;
        int best = -1;
        float bestLen = -1.f;
        for (int i = 0; i < lane.count; ++i)
        {
            const int pc = pcOf (lane.notes[i].chord[0]);
            if (pc == 0 || pc == 1)
                continue;
            if (lane.notes[i].length > bestLen)
            {
                bestLen = lane.notes[i].length;
                best = i;
            }
        }
        if (best < 0)
        {
            for (int i = 1; i < lane.count; ++i)
                if (pcOf (lane.notes[i].chord[0]) == 0)
                {
                    best = i;
                    break;
                }
        }
        if (best < 0)
            break;
        lane.notes[best].chord[0] = 1;
    }
}

void styleJackinBass (Lane& lane, int bars, std::uint32_t seed, int mode)
{
    // The flat-2nd flavour needs phrygian (the b2 cap is 6% elsewhere): outside
    // it the roll takes the dotted grid, so every roll still carries a flavour.
    int flavour = jackinFlavour (seed);
    if (flavour == 1 && mode != phrase::Phrygian)
        flavour = 0;
    diversifyJackin (lane, bars, seed);
    if (flavour == 0)
        leanJackinDotted (lane, bars, seed);
    ensureColour (lane, seed);
    if (flavour == 1)
        paintFlat2 (lane);
    fitRootShare (lane, 0.40f, 0.72f, seed, Jackin, mode);
}

void enforceMonophonic (Lane& lane)
{
    if (lane.count <= 1)
        return;
    std::sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b)
    {
        if (a.start != b.start)
            return a.start < b.start;
        return a.vel > b.vel;
    });
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        Note n = lane.notes[i];
        const int st = (int) std::lround (n.start);
        if (w > 0 && (int) std::lround (lane.notes[w - 1].start) == st)
            continue;
        // End one tick before the next onset. A full 16th of hold into the
        // next note is what the scorer rejects; a couple of ticks would pass.
        if (w > 0)
        {
            const float tick = 1.f / 120.f;
            const float gap = n.start - lane.notes[w - 1].start;
            float len = gap - tick;
            if (len < tick)
                len = tick;
            if (lane.notes[w - 1].length > len)
                lane.notes[w - 1].length = len;
        }
        if (n.length < 0.25f)
            continue;
        lane.notes[w++] = n;
    }
    lane.count = w;
}

// No root on both the 2nd and the 4th 16th of a bar: the later one takes a
// colour drawn from the model.
void avoidRootTwoFour (Lane& lane)
{
    for (int b = 0; b < 8; ++b)
    {
        int at1 = -1;
        int at3 = -1;
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st / 16 != b)
                continue;
            if (pcOf (lane.notes[i].chord[0]) != 0)
                continue;
            if (st % 16 == 1)
                at1 = i;
            if (st % 16 == 3)
                at3 = i;
        }
        if (at1 >= 0 && at3 >= 0)
        {
            const int pc = drawPc (0, 1u, 0xA2F4u + (std::uint32_t) b * 0x9E37u);
            lane.notes[at3].chord[0] = (std::int8_t) (pc > 0 ? pc : 3);
        }
    }
}

void finishBass (Lane& lane)
{
    enforceMonophonic (lane);
    avoidRootTwoFour (lane);
}

int octaveMoves (const Lane& lane) noexcept
{
    int m = 0;
    for (int i = 1; i < lane.count; ++i)
    {
        const int d = (int) lane.notes[i].chord[0] - (int) lane.notes[i - 1].chord[0];
        const int ad = d < 0 ? -d : d;
        if (ad == 12 || ad == 24)
            ++m;
    }
    return m;
}

int adiff (int a, int b) noexcept
{
    const int d = a - b;
    return d < 0 ? -d : d;
}

// About a fifth of the melodic moves are octaves, inside the 15-26% lane.
// A lift keeps its pitch class. When the line has no same-class pair, the
// note takes the previous colour first so the jump is a real octave.
void paintOctaveShare (Lane& lane, std::uint32_t seed, float share = 0.20f)
{
    if (lane.count < 2)
        return;
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    const int moves = std::max (1, lane.count - 1);
    const int want = std::clamp ((int) std::lround ((float) moves * share), 1, moves);
    const std::uint32_t h = seed ? seed : 1u;

    auto tryLift = [&] (int i) -> bool
    {
        if (i <= 0 || i >= lane.count || lane.notes[i].chord[0] > 14)
            return false;
        const int pc = pcOf (lane.notes[i].chord[0]);
        const int lifted = pc + 12;
        const int prev = lane.notes[i - 1].chord[0];
        const int next = i + 1 < lane.count ? lane.notes[i + 1].chord[0] : lifted;
        if (adiff (lifted, prev) > 19 || adiff (lifted, next) > 19)
            return false;
        if (adiff (lifted, prev) != 12 && adiff (lifted, next) != 12)
            return false;
        const int before = octaveMoves (lane);
        const int old = lane.notes[i].chord[0];
        lane.notes[i].chord[0] = (std::int8_t) lifted;
        if (octaveMoves (lane) <= before)
        {
            lane.notes[i].chord[0] = (std::int8_t) old;
            return false;
        }
        return true;
    };

    for (int n = 0; n < lane.count && octaveMoves (lane) < want; ++n)
        tryLift (1 + (int) ((h + (std::uint32_t) n) % (std::uint32_t) (lane.count - 1)));

    for (int n = 0; n < lane.count && octaveMoves (lane) < want; ++n)
    {
        const int i = 1 + (int) ((h / 3u + (std::uint32_t) n) % (std::uint32_t) (lane.count - 1));
        if (lane.notes[i].chord[0] > 14)
            continue;
        const int prevPc = pcOf (lane.notes[i - 1].chord[0]);
        if (prevPc == 0 && rootShareOf (lane) > 0.55f)
            continue;
        const int old = lane.notes[i].chord[0];
        lane.notes[i].chord[0] = (std::int8_t) prevPc;
        if (! tryLift (i))
            lane.notes[i].chord[0] = (std::int8_t) old;
    }
}

// `want` (4-6) off-kick slots, two 16ths apart where possible, drawn one by one
// from the model's BOUNCY onset weights (bar group: body, or the turn bar, which
// stops by slot 9 so its turn rest stays silent). False if `want` can't be met.
bool pickPulseSlots (int* out, std::uint32_t h, bool turn, int want)
{
    want = clampi (want, 1, 6);
    int picked[6];
    int n = 0;
    for (int gap = 2; gap >= 1 && n < want; --gap)
        for (int guard = 0; guard < 24 && n < want; ++guard)
        {
            bool allowed[16];
            for (int sl = 0; sl < 16; ++sl)
            {
                bool ok = ! kickSlot (sl) && ! (turn && sl > 9);
                for (int k = 0; k < n && ok; ++k)
                    if (std::abs (sl - picked[k]) < gap)
                        ok = false;
                allowed[sl] = ok;
            }
            h = h * 0x9E3779B1u + 0x85EBCA6Bu;
            const int pick = rules::sampleBassSlot (Pop, turn ? 2 : 0, allowed, h);
            if (pick < 0)
                break;
            picked[n++] = pick;
        }
    if (n < want)
        return false;
    std::sort (picked, picked + want);
    for (int k = 0; k < want; ++k)
        out[k] = picked[k];
    return true;
}

// Pulse is a steady off-kick grid: six attacks a bar (five in the closing bar,
// which keeps its four-16th rest), so the held line lands at 5-7 notes a bar
// (47 attacks plus at most 3 pickup notes over 8 bars: 5.9-6.3) with lengths
// in the 1.8-3.2 band. The grid comes from the seed, not a shape book, so a
// batch does not reprint one onset set. Neighbours never share a pitch class
// (so no octave moves, and holdBassNotes can tie every note into the next).
void makePulse (Lane& lane, int bars, std::uint32_t seed)
{
    constexpr int kBody = 6;
    constexpr int kTurn = 5;
    const std::uint32_t h = (seed ? seed : 1u) * 0x9E3779B1u;
    // Every grid is drawn from the model; with 12 off-kick slots and a one-16th
    // fallback spacing a draw always completes, so there is no fixed default.
    int even[kBody] {};
    int odd[kBody] {};
    int turn[kBody] {};
    for (int t = 0; t < 8; ++t)
        if (pickPulseSlots (even, h + (std::uint32_t) t * 17u, false, kBody))
            break;
    for (int t = 0; t < 8; ++t)
        if (pickPulseSlots (odd, h ^ (0xA5A5A5A5u + (std::uint32_t) t * 13u), false, kBody))
            break;
    for (int t = 0; t < 8; ++t)
        if (pickPulseSlots (turn, h ^ (0x3C6EF35Fu + (std::uint32_t) t * 9u), true, kTurn))
            break;
    bool same = true;
    for (int k = 0; k < kBody; ++k)
        if (even[k] != odd[k])
            same = false;
    for (int t = 1; t < 8 && same; ++t)
    {
        if (! pickPulseSlots (odd, h ^ (0x5BD1E995u + (std::uint32_t) t * 31u), false, kBody))
            continue;
        same = true;
        for (int k = 0; k < kBody; ++k)
            if (even[k] != odd[k])
                same = false;
    }
    Note out[kMaxNotes];
    int w = 0;
    int prevPitch = 0;
    bool havePrev = false;
    for (int bar = 0; bar < bars && bar < 8; ++bar)
    {
        int src[8];
        int n = 0;
        for (int i = 0; i < lane.count && n < 8; ++i)
            if ((int) std::floor (lane.notes[i].start / 16.f) == bar)
                src[n++] = i;
        const bool last = bar == std::min (bars, 8) - 1;
        const int* slots = last ? turn : (((bar & 1) != 0) ? odd : even);
        const int count = last ? kTurn : kBody;
        for (int k = 0; k < count && w < kMaxNotes; ++k)
        {
            Note nte;
            if (n > 0)
            {
                nte = lane.notes[src[k % n]];
                for (int j = 1; j < n && havePrev && pcOf (nte.chord[0]) == pcOf (prevPitch); ++j)
                    nte = lane.notes[src[(k + j) % n]];
            }
            nte.start = (float) (bar * 16 + slots[k]);
            nte.length = 2.2f;
            nte.nChord = 1;
            if (nte.chord[0] > 14)
                nte.chord[0] = (std::int8_t) pcOf (nte.chord[0]);
            // A bar of one pitch class answers itself with a colour, not an
            // octave: pulse keeps octave moves at or under 8% (Composer).
            if (havePrev && pcOf (nte.chord[0]) == pcOf (prevPitch))
            {
                const int prevPc = pcOf (prevPitch);
                const int c = drawPc (prevPc, 1u << prevPc, h + (std::uint32_t) (bar * 8 + k));
                if (c >= 0)
                    nte.chord[0] = (std::int8_t) c;
            }
            prevPitch = nte.chord[0];
            havePrev = true;
            out[w++] = nte;
        }
    }
    // Two neighbouring grid slots one 16th apart must not overlap.
    for (int i = 0; i + 1 < w; ++i)
        out[i].length = std::min (out[i].length, std::max (0.5f, out[i + 1].start - out[i].start));
    lane.count = w;
    for (int i = 0; i < w; ++i)
        lane.notes[i] = out[i];
}

// Bar 8 has to rest for at least four 16ths, and its token string must not
// be bar 4's. The rest is the tail of the bar so the downbeat still speaks.
void ensureBar8Turn (Lane& lane, int bars, std::uint32_t seed)
{
    if (bars < 8 || lane.count <= 0)
        return;
    const int origin = 7 * 16;
    const float cut = (float) (origin + 12);
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        Note n = lane.notes[i];
        if (n.start >= (float) origin && n.start < (float) (origin + 16))
        {
            if (n.start >= cut)
                continue;
            if (n.start + n.length > cut)
                n.length = cut - n.start;
            if (n.length < 0.5f)
                continue;
        }
        lane.notes[w++] = n;
    }
    lane.count = w;
    int left = 0;
    for (int i = 0; i < lane.count; ++i)
        if (lane.notes[i].start >= (float) origin && lane.notes[i].start < (float) (origin + 16))
            ++left;
    if (left < 1 && lane.count < kMaxNotes)
    {
        Note n;
        n.start = (float) (origin + 2);
        n.length = 2.f;
        n.chord[0] = 0;
        n.vel = 112;
        n.nChord = 1;
        lane.notes[lane.count++] = n;
    }
    auto tokenAt = [&] (int bar, int slot) -> int
    {
        const int abs = bar * 16 + slot;
        for (int i = 0; i < lane.count; ++i)
        {
            const int a = (int) std::lround (lane.notes[i].start);
            const int b = (int) std::lround (lane.notes[i].start + lane.notes[i].length);
            if (abs == a)
                return pcOf (lane.notes[i].chord[0]) + 1;
            if (abs > a && abs < b)
                return -1;
        }
        return 0;
    };
    bool same = true;
    for (int s = 0; s < 16; ++s)
        if (tokenAt (3, s) != tokenAt (7, s))
            same = false;
    if (! same)
        return;
    for (int i = lane.count - 1; i >= 0; --i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st / 16 != 7 || st % 16 >= 12)
            continue;
        const int cur = pcOf (lane.notes[i].chord[0]);
        const int d = drawPc (cur, 1u << cur, (seed ^ 0xB8u) + (std::uint32_t) st);
        lane.notes[i].chord[0] = (std::int8_t) (d >= 0 ? d : 0);
        break;
    }
}

bool noteInOctave (const Lane& lane, int i) noexcept
{
    const int pc = lane.notes[i].chord[0];
    if (i > 0 && adiff (pc, lane.notes[i - 1].chord[0]) == 12)
        return true;
    if (i + 1 < lane.count && adiff (pc, lane.notes[i + 1].chord[0]) == 12)
        return true;
    return false;
}

// The tonic has to be the most-sounding bass pitch. The b2 and the 2 are passing
// tones: short, and never the note a key reader could mistake for the tonic.
void anchorPopRoot (Lane& lane, std::uint32_t seed)
{
    if (lane.count <= 0)
        return;
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    for (int i = 0; i < lane.count; ++i)
    {
        const int pc = pcOf (lane.notes[i].chord[0]);
        if ((pc == 1 || pc == 2) && lane.notes[i].length > 1.15f && ! noteInOctave (lane, i))
        {
            const int c = drawPc (pc, 0x7u, (seed ^ 0xA9C0u) + (std::uint32_t) i);
            if (c >= 0)
                lane.notes[i].chord[0] = (std::int8_t) c;
        }
    }
    auto timeOf = [&] (int want) -> float
    {
        float t = 0.f;
        for (int i = 0; i < lane.count; ++i)
            if (pcOf (lane.notes[i].chord[0]) == want)
                t += std::max (0.f, lane.notes[i].length);
        return t;
    };
    auto total = [&] () -> float
    {
        float t = 0.f;
        for (int i = 0; i < lane.count; ++i)
            t += std::max (0.f, lane.notes[i].length);
        return t;
    };
    int guard = 0;
    while (guard++ < lane.count)
    {
        const float tot = total();
        if (tot <= 0.f)
            break;
        const float root = timeOf (0);
        float bestOther = 0.f;
        for (int pc = 1; pc < 12; ++pc)
            bestOther = std::max (bestOther, timeOf (pc));
        const float share = root / tot;
        if (root > bestOther && share >= 0.36f && share <= 0.60f)
            break;
        if (root <= bestOther || share < 0.36f)
        {
            int best = -1;
            float bestLen = -1.f;
            for (int i = 0; i < lane.count; ++i)
            {
                if (pcOf (lane.notes[i].chord[0]) == 0 || noteInOctave (lane, i))
                    continue;
                if (lane.notes[i].length > bestLen)
                {
                    bestLen = lane.notes[i].length;
                    best = i;
                }
            }
            if (best < 0)
                break;
            const int reg = (int) lane.notes[best].chord[0] - pcOf (lane.notes[best].chord[0]);
            lane.notes[best].chord[0] = (std::int8_t) (reg >= 12 ? 12 : 0);
            continue;
        }
        int best = -1;
        float bestLen = -1.f;
        int roots = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            if (pcOf (lane.notes[i].chord[0]) != 0)
                continue;
            ++roots;
            if (noteInOctave (lane, i))
                continue;
            if (lane.notes[i].length > bestLen)
            {
                bestLen = lane.notes[i].length;
                best = i;
            }
        }
        if (best < 0 || roots <= 1)
            break;
        const int c = drawPc (0, 1u, (seed ^ 0xA9C1u) + (std::uint32_t) guard);
        lane.notes[best].chord[0] = (std::int8_t) (c > 0 ? c : 7);
    }
}

// JACKIN: adjacent bars may share a sounding pitch on at most a fifth of the
// 16ths. Change the later bar's pitch, or let a note cover a shared rest.
// Onsets stay put so the rhythm history is not undone.
int soundingPcAt (const Lane& lane, int slot) noexcept
{
    int found = -2;
    for (int i = 0; i < lane.count; ++i)
    {
        const int a = (int) std::lround (lane.notes[i].start);
        const int b = (int) std::lround (lane.notes[i].start + std::max (0.f, lane.notes[i].length));
        if (slot >= a && slot < b)
            found = pcOf (lane.notes[i].chord[0]);
    }
    return found;
}

void contrastCallAnswer (Lane& lane, int bars)
{
    if (bars < 2 || lane.count <= 0)
        return;
    const int last = std::min (bars, 8) - 1;
    for (int pass = 0; pass < 3; ++pass)
    {
        bool changed = false;
        for (int b = 0; b + 1 < bars && b < 7; ++b)
        {
            for (int s = 0; s < 16; ++s)
            {
                if (b + 1 == last && s >= 12)
                    continue;
                const int abs0 = b * 16 + s;
                const int abs1 = (b + 1) * 16 + s;
                const int p0 = soundingPcAt (lane, abs0);
                const int p1 = soundingPcAt (lane, abs1);
                if (p0 != p1)
                    continue;
                int cover = -1;
                for (int i = 0; i < lane.count; ++i)
                {
                    const int a = (int) std::lround (lane.notes[i].start);
                    const int e = (int) std::lround (lane.notes[i].start + std::max (0.f, lane.notes[i].length));
                    if (abs1 >= a && abs1 < e)
                        cover = i;
                }
                if (cover >= 0 && (int) std::lround (lane.notes[cover].start) / 16 != b + 1)
                    cover = -1;
                if (cover >= 0)
                {
                    const int cur = pcOf (lane.notes[cover].chord[0]);
                    const int d = drawPc (cur, (1u << cur) | (p0 >= 0 ? 1u << p0 : 0u),
                                          0xCA11u + (std::uint32_t) (pass * 4096 + b * 16 + s));
                    const int want = d >= 0 ? d : cur;
                    if (want != cur)
                    {
                        const int reg = (int) lane.notes[cover].chord[0] - cur;
                        lane.notes[cover].chord[0] = (std::int8_t) (want + (reg >= 12 ? 12 : 0));
                        changed = true;
                    }
                    continue;
                }
                int best = -1;
                int bestEnd = -100;
                for (int i = 0; i < lane.count; ++i)
                {
                    const int a = (int) std::lround (lane.notes[i].start);
                    if (a / 16 != b + 1 || a > abs1)
                        continue;
                    const int e = (int) std::lround (lane.notes[i].start + std::max (0.f, lane.notes[i].length));
                    if (e <= abs1 && e > bestEnd)
                    {
                        bestEnd = e;
                        best = i;
                    }
                }
                if (best >= 0)
                {
                    const float need = (float) (abs1 + 1) - lane.notes[best].start;
                    if (need > lane.notes[best].length && need <= 4.f)
                    {
                        lane.notes[best].length = need;
                        if (pcOf (lane.notes[best].chord[0]) == p0)
                        {
                            const int cur = pcOf (lane.notes[best].chord[0]);
                            const int d = drawPc (cur, (1u << cur) | (1u << p0), 0xCA12u + (std::uint32_t) (pass * 4096 + abs1));
                            if (d >= 0)
                                lane.notes[best].chord[0] = (std::int8_t) d;
                        }
                        changed = true;
                    }
                }
            }
        }
        if (! changed)
            break;
    }
}

// ---- JACKIN loop identity (A&R 490 #3, ported from #30's JackinLoop.h) ----
// The A&R gate read "loop doesn't repeat" on 14 of 20 JACKIN rolls: the
// scorer wants the 2-bar repeat (LOOP_NO_REPEAT: sounding match at 2 bars
// >= 0.50; lane F6: attack/pitch match at 2 bars >= 0.60) with the 1-bar
// repeat kept low (F6 <= 0.10). The line used to play eight different bars:
// bars 5-8 carried their own back-half variation and the call/answer
// pass edits each bar against the one before. Now one bar pair of the roll's
// own line is the hook and the loop plays A B A B4 A B A B8:
//   B4  B, and on about three fifths of rolls its last onset takes another
//       pitch the hook plays (the bar-4 answer)
//   B8  B up to step 12, then the four-16th turn rest (and maybe the pickup)
// The hook is whatever the grammar drew for the roll, so variety across rolls
// is kept. Runs after the call/answer contrast and before the kick and hold
// passes, which key their choices on the 2-bar position so repeats move
// together.

int jackinBarOf (float start) noexcept
{
    return (int) std::floor (start / 16.f + 1.0e-4f);
}

// The bar pair (0-1, 2-3, 4-5; never the turn bar) with the most JACKIN
// flavour (dotted 16ths, b2 colour), while the root clearly leads the pitch
// time (#30: repeated four times, the pair sets the key a listener hears),
// the b2 stays at or under 20% and nothing sits outside JACKIN's scale.
// Ties, and rolls where no pair qualifies, take the first pair.
int pickJackinHook (const Lane& lane, int bars) noexcept
{
    int best = 0;
    float bestScore = -1.f;
    for (int p = 0; p * 2 + 1 < bars - 1 && p < 4; ++p)
    {
        int att = 0;
        int dotted = 0;
        int inA = 0;
        int inB = 0;
        float t = 0.f;
        float b2 = 0.f;
        float out = 0.f;
        float pcTime[12] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const Note& n = lane.notes[i];
            const int bar = jackinBarOf (n.start);
            if (bar != p * 2 && bar != p * 2 + 1)
                continue;
            if (bar == p * 2)
                ++inA;
            else
                ++inB;
            const int slot = ((int) std::lround (n.start)) % 16;
            ++att;
            if (slot % 3 == 0)
                ++dotted;
            const int pc = pcOf (n.chord[0]);
            const float len = std::max (0.f, n.length);
            t += len;
            pcTime[pc] += len;
            if (pc == 1)
                b2 += len;
            if (pc == 4 || pc == 6 || pc == 9 || pc == 11)
                out += len;
        }
        float other = 0.f;
        for (int pc = 1; pc < 12; ++pc)
            other = std::max (other, pcTime[pc]);
        if (inA == 0 || inB == 0 || t <= 0.f || b2 > 0.20f * t || out > 0.f || pcTime[0] < 1.15f * other)
            continue;
        const float score = std::min (1.5f, (float) dotted / (0.55f * (float) att))
                          + std::min (1.5f, b2 / (0.10f * t));
        if (score > bestScore + 1.0e-4f)
        {
            bestScore = score;
            best = p;
        }
    }
    return best;
}

// B4: the last onset of bar 4 takes another pitch, drawn from the model in the
// roll's mode (one the hook plays where possible, never a pitch bar 2 has within
// one 16th of that slot), so bar 4 answers bar 2 and the rest of the bar repeats.
void answerJackinBar4 (Lane& lane, std::uint32_t seed)
{
    const int dst = 3;
    int last = -1;
    for (int i = 0; i < lane.count; ++i)
        if (jackinBarOf (lane.notes[i].start) == dst && (last < 0 || lane.notes[i].start > lane.notes[last].start))
            last = i;
    if (last < 0)
        return;
    const int slot = (int) std::lround (lane.notes[last].start) - dst * 16;
    const int oldPc = pcOf (lane.notes[last].chord[0]);
    bool near[12] {};
    bool inHook[12] {};
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        const int bar = jackinBarOf (lane.notes[i].start);
        const int pc = pcOf (lane.notes[i].chord[0]);
        if (bar == 1 && std::abs ((st - 16) - slot) <= 1)
            near[pc] = true;
        if (bar == 0 || bar == 1)
            inHook[pc] = true;
    }
    unsigned avoid = 1u << oldPc;
    for (int pc = 0; pc < 12; ++pc)
        if (near[pc])
            avoid |= 1u << pc;
    // A root on 16th 2 or 4 would pair with avoidRootTwoFour's rule.
    if (slot == 1 || slot == 3)
        avoid |= 1u;
    unsigned notHook = 0u;
    for (int pc = 0; pc < 12; ++pc)
        if (! inHook[pc])
            notHook |= 1u << pc;
    int want = drawPc (oldPc, avoid | notHook, (seed ? seed : 1u) ^ 0xB4A1u);
    if (want < 0)
        want = drawPc (oldPc, avoid, (seed ? seed : 1u) ^ 0xB4A2u);
    if (want < 0)
        return;
    int delta = want - oldPc;
    if (delta > 6)
        delta -= 12;
    if (delta < -6)
        delta += 12;
    lane.notes[last].chord[0] = (std::int8_t) (lane.notes[last].chord[0] + delta);
}

void loopJackinBass (Lane& lane, int bars, std::uint32_t seed)
{
    const int nb = std::min (bars, kMaxBars);
    if (nb < 4 || lane.count <= 0)
        return;
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    const Lane src = lane;
    const int hook = pickJackinHook (src, nb);
    const float loopEnd = (float) (nb * 16);
    const float turnCut = (float) ((nb - 1) * 16 + 12);

    lane.count = 0;
    for (int bar = 0; bar < nb; ++bar)
    {
        const int from = hook * 2 + (bar & 1);
        const float shift = (float) ((bar - from) * 16);
        for (int i = 0; i < src.count && lane.count < kMaxNotes; ++i)
        {
            if (jackinBarOf (src.notes[i].start) != from)
                continue;
            Note n = src.notes[i];
            n.start += shift;
            if (bar == nb - 1)
            {
                // B8: B up to step 12, then the turn rest.
                if (n.start >= turnCut - 1.0e-3f)
                    continue;
                n.length = std::min (n.length, turnCut - n.start);
            }
            n.length = std::min (n.length, loopEnd - n.start);
            if (n.length < 0.25f)
                continue;
            lane.notes[lane.count++] = n;
        }
    }
    bool lastBar = false;
    for (int i = 0; i < lane.count; ++i)
        if (jackinBarOf (lane.notes[i].start) == nb - 1)
            lastBar = true;
    if (! lastBar && lane.count < kMaxNotes)
    {
        // A wiped bar is a hard fail. A short in-scale root.
        Note n;
        n.start = (float) ((nb - 1) * 16 + 2);
        n.length = 2.f;
        n.chord[0] = 0;
        n.nChord = 1;
        n.vel = 112;
        lane.notes[lane.count++] = n;
    }
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    // One onset per 16th; a held note ends at the next onset (B's tail rings
    // into A's first slots, as it did into bar 3).
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        if (w > 0 && std::lround (lane.notes[w - 1].start) == std::lround (lane.notes[i].start))
            continue;
        lane.notes[w++] = lane.notes[i];
    }
    lane.count = w;
    for (int i = 0; i + 1 < lane.count; ++i)
    {
        const float room = lane.notes[i + 1].start - lane.notes[i].start;
        if (lane.notes[i].length > room)
            lane.notes[i].length = std::max (0.25f, room);
    }
    // Same share and hash as differBar4, so the bar-4 answer rate is unchanged.
    // Same share and hash as differBar4 (see there).
    if (nb >= 8 && (((seed ? seed : 1u) * 0xC2B2AE35u) % 100u) < 58u)
        answerJackinBar4 (lane, seed);
}

void softenFifths (Lane& lane, std::uint32_t seed)
{
    int guard = 0;
    while (guard++ < lane.count)
    {
        int fifths = 0;
        for (int i = 0; i < lane.count; ++i)
            if (pcOf (lane.notes[i].chord[0]) == 7)
                ++fifths;
        if (lane.count <= 0 || fifths * 100 <= lane.count * 40)
            break;
        bool changed = false;
        for (int i = lane.count - 1; i >= 0; --i)
        {
            if (pcOf (lane.notes[i].chord[0]) != 7)
                continue;
            const int d = drawPc (7, (1u << 7) | 1u, seed + (std::uint32_t) guard * 977u + (std::uint32_t) i);
            lane.notes[i].chord[0] = (std::int8_t) (d >= 0 ? d : 3);
            changed = true;
            break;
        }
        if (! changed)
            break;
    }
}

// ROLLER and BOUNCY share a small shape book, so thousands of seeds reprint
// one bass line. A ghost on later hits, and an octave on about a fifth of the
// moves, splits those reprints. The octave keeps the pitch class.
// octaveShare 0 skips the octaves (BOUNCY pulse: at most 8% octave moves).
// BOUNCY bounce asks for 0.24 so the style average across both modes stays
// inside the 15-30% octave lane with pulse near zero.
void paintPhraseVariety (Lane& lane, int dialect, std::uint32_t seed, float octaveShare = 0.20f)
{
    if ((dialect != UkMin && dialect != Pop) || lane.count <= 0)
        return;
    const std::uint32_t h = (seed ? seed : 1u) * 0x85EBCA6Bu;
    std::uint32_t bit = h;
    int prevBar = -1;
    int idxInBar = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const int bar = (int) std::floor (lane.notes[i].start / 16.f);
        if (bar != prevBar)
        {
            prevBar = bar;
            idxInBar = 0;
        }
        bit = bit * 0x9E3779B1u + (std::uint32_t) (i + 1);
        if (idxInBar > 0 && (bit & 1u) == 0u)
            lane.notes[i].flags = (std::uint8_t) (lane.notes[i].flags | NoteGhost);
        ++idxInBar;
    }
    if (octaveShare > 0.f)
        paintOctaveShare (lane, seed, octaveShare);
}

// Bar 4 answers bar 2 on about three fifths of rolls. The back half of bar 4
// moves, so the 8-bar loop is not a photocopy of the opening pair. The other
// two fifths keep the copy.
void differBar4 (Lane& lane, int bars, std::uint32_t seed)
{
    if (bars < 4)
        return;
    // Just over half the rolls. A pitch-class change is what the token test counts;
    // a one-slot slide with the same pitch still matches.
    // 58% here: the composer already answers bar 2 at the model's rate, and the
    // later density, kick and hold passes move bar 4 on some copied rolls too,
    // so the shipped rate lands near the model's kBar4AnswerProb (~0.8-0.9).
    const bool answer = (((seed ? seed : 1u) * 0xC2B2AE35u) % 100u) < 58u;
    const int srcBar = 1;
    const int dstBar = 3;
    if (dstBar >= bars)
        return;

    bool src[16] {};
    bool dst[16] {};
    int back[8];
    int nBack = 0;
    int inDst = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < 0)
            continue;
        const int bar = st / 16;
        const int slot = ((st % 16) + 16) % 16;
        if (bar == srcBar)
            src[slot] = true;
        if (bar == dstBar)
        {
            dst[slot] = true;
            ++inDst;
            if (slot >= 8 && nBack < 8)
                back[nBack++] = i;
        }
    }
    bool sameBack = true;
    for (int s = 8; s < 16; ++s)
        if (src[s] != dst[s])
            sameBack = false;
    if (! answer)
    {
        if (sameBack)
            return;
        // Keep the photocopy on the other two fifths, including when a darker
        // fill had already moved bar 4.
        int w = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            const int bar = st / 16;
            const int slot = ((st % 16) + 16) % 16;
            if (bar == dstBar && slot >= 8 && ! src[slot])
                continue;
            lane.notes[w++] = lane.notes[i];
        }
        lane.count = w;
        bool have[16] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st / 16 == dstBar)
                have[st % 16] = true;
        }
        for (int s = 8; s < 16 && lane.count < kMaxNotes; ++s)
        {
            if (! src[s] || have[s])
                continue;
            Note n;
            n.start = (float) (dstBar * 16 + s);
            n.length = 1.5f;
            n.chord[0] = 0;
            n.vel = 104;
            n.nChord = 1;
            for (int i = 0; i < lane.count; ++i)
            {
                const int st = (int) std::lround (lane.notes[i].start);
                if (st / 16 == srcBar && st % 16 == s)
                {
                    n = lane.notes[i];
                    n.start = (float) (dstBar * 16 + s);
                    break;
                }
            }
            lane.notes[lane.count++] = n;
        }
        return;
    }
    if (! sameBack)
        return;

    // Same slot, different pitch class: the token (slot, pc) no longer matches
    // bar 2 within one 16th, and the rest of the bar is kept.
    if (nBack > 0)
    {
        const int ni = back[(int) ((seed >> 8) % (std::uint32_t) nBack)];
        const int slot = (int) std::lround (lane.notes[ni].start) % 16;
        bool near[12] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st / 16 != srcBar)
                continue;
            if (std::abs (st % 16 - slot) <= 1)
                near[pcOf (lane.notes[i].chord[0])] = true;
        }
        const int oldPc = pcOf (lane.notes[ni].chord[0]);
        unsigned avoid = 1u << oldPc;
        for (int pc = 0; pc < 12; ++pc)
            if (near[pc])
                avoid |= 1u << pc;
        const int d = drawPc (oldPc, avoid, (seed >> 4) ^ 0xD4B4u);
        if (d >= 0)
        {
            const int reg = (int) lane.notes[ni].chord[0] - oldPc;
            lane.notes[ni].chord[0] = (std::int8_t) (d + (reg >= 12 ? 12 : 0));
            return;
        }
    }
    const int spin = (int) ((seed >> 4) & 7u);
    int dest = -1;
    int oldSlot = nBack > 0 ? (int) std::lround (lane.notes[back[0]].start) % 16 : -1;
    if (nBack > 0)
        oldSlot = (int) std::lround (lane.notes[back[(int) ((seed >> 8) % (std::uint32_t) nBack)]].start) % 16;
    for (int t = 0; t < 8 && dest < 0; ++t)
    {
        const int s = 8 + ((spin + t) % 8);
        if (dst[s] || kickSlot (s))
            continue;
        if (oldSlot >= 0 && std::abs (s - oldSlot) <= 1)
            continue;
        dest = s;
    }
    if (nBack > 0 && dest >= 0)
    {
        const int ni = back[(int) ((seed >> 8) % (std::uint32_t) nBack)];
        const int old = (int) std::lround (lane.notes[ni].start) % 16;
        dst[old] = false;
        lane.notes[ni].start = (float) (dstBar * 16 + dest);
        return;
    }
    if (nBack > 0 && inDst > 1)
    {
        // The back half is full and identical. Dropping one attack is the answer.
        const int ni = back[(int) ((seed >> 8) % (std::uint32_t) nBack)];
        for (int i = ni + 1; i < lane.count; ++i)
            lane.notes[i - 1] = lane.notes[i];
        --lane.count;
        return;
    }
    if (dest < 0 || lane.count >= kMaxNotes)
        return;
    Note n;
    n.start = (float) (dstBar * 16 + dest);
    n.length = 1.5f;
    n.chord[0] = 7;
    n.nChord = 1;
    n.vel = 104;
    lane.notes[lane.count++] = n;
}

// The bass line itself comes from rules::composeBass (RuleComposer): fresh
// seeded sampling, no stored phrase. These are the rule passes that follow it:
// the BOUNCE shove, STYLE thinning, the style colour passes, BUSY scaling, the
// BOUNCY root / fifth / pulse rules, the bar-4 answer and the bar-8 turn. The
// composer already leaves the last bar's four-16th turn rest.
void finishPhrase (Lane& lane, const Settings& s, std::uint32_t seed, bool pulse, int mode)
{
    const int bars = s.bars;
    const int dialect = s.dialect;
    if (lane.count <= 0)
        return;
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });

    // BOUNCE above 0.6 pushes even 16ths one later (the knob audit compares it).
    if (s.bounce > 0.6f)
    {
        bool used[kMaxSteps] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st >= 0 && st < kMaxSteps)
                used[st] = true;
        }
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            const int slot = st % 16;
            if (st < 0 || st + 1 >= bars * 16 || st + 1 >= kMaxSteps || (slot & 1) != 0 || slot >= 15 || used[st + 1])
                continue;
            used[st] = false;
            used[st + 1] = true;
            lane.notes[i].start = (float) (st + 1);
        }
        std::sort (lane.notes, lane.notes + lane.count,
                   [] (const Note& a, const Note& b) { return a.start < b.start; });
        for (int i = 0; i + 1 < lane.count; ++i)
        {
            const float room = lane.notes[i + 1].start - lane.notes[i].start;
            if (lane.notes[i].length > room)
                lane.notes[i].length = std::max (0.25f, room);
        }
    }

    // STYLE at the stripped end keeps at most four attacks a bar (the latest go).
    if (s.styleIntensity <= 0.25f)
    {
        int w = 0;
        int perBar[kMaxBars] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const int b = (int) std::lround (lane.notes[i].start) / 16;
            if (b >= 0 && b < kMaxBars && perBar[b] >= 4)
                continue;
            if (b >= 0 && b < kMaxBars)
                ++perBar[b];
            lane.notes[w++] = lane.notes[i];
        }
        lane.count = w;
    }

    if (dialect == Jackin)
        styleJackinBass (lane, bars, seed, mode);
    else if (dialect == Long && deepIsDark (s.dark, seed))
        densifyDeepDark (lane, bars, seed, mode);
    // BOUNCY bounce sets its own density (the 2-bar cell's contrast, its kick cap);
    // low BUSY thinning on top took it under the A&R density band (3.9 a bar).
    if (! (dialect == Pop && ! pulse && s.busy < 0.40f))
        applyBusyDensity (lane, dialect, bars, s.busy, seed);
    if (dialect == Long)
        fitRootShare (lane, 0.42f, 0.68f, seed, dialect, mode);
    if (dialect == Pop)
    {
        // Bounce root share stays inside 30-64% of bass time. Notes are about
        // equal length, so attack share is the time share. A bar over 64%
        // tonic hands the extra attacks to a colour degree of the mode.
        for (int b = 0; b < bars && b < 8; ++b)
        {
            int rootIdx[16];
            int n = 0;
            int roots = 0;
            for (int i = 0; i < lane.count && n < 16; ++i)
            {
                if ((int) std::floor (lane.notes[i].start / 16.f) != b)
                    continue;
                rootIdx[n++] = i;
                if (pcOf (lane.notes[i].chord[0]) == 0)
                    ++roots;
            }
            for (int k = n - 1; k >= 0 && n > 0 && roots * 100 > n * 64; --k)
            {
                Note& note = lane.notes[rootIdx[k]];
                if (pcOf (note.chord[0]) != 0)
                    continue;
                int semi = 0;
                for (int t = 0; t < 4 && pcOf (semi) == 0; ++t)
                    semi = rules::sampleDegreeSemis (Pop, mode, 0,
                                                     (seed ^ 0xC0105u) * 0x9E3779B1u + (std::uint32_t) (b * 64 + k * 4 + t));
                if (pcOf (semi) == 0)
                    semi = 7;
                note.chord[0] = (std::int8_t) semi;
                --roots;
            }
        }
        softenFifths (lane, seed);
        if (pulse)
        {
            makePulse (lane, bars, seed);
            // The pulse grid replaces the onsets, so BUSY has to scale it again.
            applyBusyDensity (lane, dialect, bars, s.busy, seed);
        }
    }
    // Bounce carries the style's octave moves (pulse stays near zero and is 30-40%
    // of a batch), so the BOUNCY average lands in the 15-30% lane.
    paintPhraseVariety (lane, dialect, seed, pulse ? 0.f : 0.25f);
    differBar4 (lane, bars, seed);
    finishBass (lane);
    if (dialect == Pop)
    {
        ensureBar8Turn (lane, bars, seed);
        anchorPopRoot (lane, seed);
    }
}

bool bassOnset (const Lane& lane, int slot)
{
    for (int i = 0; i < lane.count; ++i)
        if ((int) std::lround (lane.notes[i].start) == slot)
            return true;
    return false;
}

// Stab chord onsets per bar (a chord counts as one hit), every style and every
// STAB value (genre model v1.2: cap 3, no riff mode above it).
constexpr int kStabHitsMax = rules::tables::kStabHitsCap;
static_assert (kStabHitsMax <= 3, "the stab cap is 3 hits a bar at every knob value");

// Chord hits allowed in one bar. Composer's cap is 3 at the default STAB
// (0.55), but a full 3 in every bar is 3.0 hits a bar, over the stab_hpb
// ceilings in data/genre/style_rules.json (DEEP 2.76, BOUNCY / JACKIN 2.97).
// So up to 0.8, bars 2, 4 and 6 (0-based 1, 3, 5) take one fewer: a style
// that fills the cap lands at 2.625 hits a bar. Over 0.8 every bar takes the
// full kStabHitsMax (3.0 a bar), never more: no knob value makes a 4-hit riff
// (0 / 0.55 / 1 still rises strictly; roll_test testStabDensity checks it).
int stabHitCap (const Settings& s, int bar) noexcept
{
    if (s.stab > 0.8f)
        return kStabHitsMax;
    const int b = bar & 7;
    return (b == 1 || b == 3 || b == 5) ? kStabHitsMax - 1 : kStabHitsMax;
}

// The top of the STAB knob adds density through more full bars, not a 4th
// hit: up to STAB 0.8 a capped style plays bars 2, 4 and 6 one hit lighter
// (2.625 hits a bar at the cap), above it every bar is full.
std::uint8_t stabLighterMask (const Settings& s, int per) noexcept
{
    if (per < kStabHitsMax || s.stab > 0.8f)
        return 0;
    return (std::uint8_t) ((1u << 1) | (1u << 3) | (1u << 5));
}

// Stab density follows the STAB / BUSY / STYLE knobs exactly as before; the
// hits themselves come from rules::composeStab: two voicings per roll drawn
// from the style's chord-quality palette, placed in bass gaps and over bass
// sustains by the style's learned shares (never on a bass attack outside
// BOUNCY), JACKIN repeating on its 2-bar grid.
void writeStab (Lane& lane, const Lane& bass, const Settings& s, int mode)
{
    const std::uint32_t seed = lane.seed ? lane.seed : 1u;
    clearLane (lane, seed);
    int per = s.dialect == Pop ? 8 : s.dialect == Jackin ? 6 : s.dialect == Long ? 3 : 1;
    if (s.styleIntensity <= 0.25f)
        per = std::min (per, 1);
    else if (s.styleIntensity >= 0.75f)
        per = std::min (12, per + 2);
    // Fills follow BUSY. 0.40 keeps the count above. Low BUSY drops a hit and,
    // on a sparse line, skips the odd bars. High BUSY adds two hits a bar.
    // ROLLER's single stab is not dropped by BUSY: that used to cancel STAB.
    if (s.busy > 0.75f)
        per = std::min (12, per + 2);
    else if (s.busy < 0.25f && s.dialect != UkMin)
        per = std::max (1, per - 1);
    // STAB sets stab density in every style. 0.55 (the default) keeps the count
    // above; 0 is about a quarter of it, 1 about 1.6x. ROLLER steps 1 / 2 / 3.
    const float stabKnob = std::clamp (s.stab, 0.f, 1.f);
    if (s.dialect == UkMin)
        per = std::min (12, per + (stabKnob > 0.85f ? 2 : stabKnob > 0.66f ? 1 : 0));
    else
        per = clampi ((int) std::lround ((float) per * (0.25f + 1.364f * stabKnob)), 1, 12);
    // Low STAB also thins the bars: stabs only on even bars.
    const bool stabSparse = stabKnob < 0.2f;
    // Presence follows STAB too: 98 at the default.
    const int stabVel = clampi ((int) std::lround (70.f + 51.f * stabKnob), 60, 124);

    const int flavour = s.dialect == Jackin ? jackinFlavour (seed) : -1;
    float len = s.dialect == Pop ? 1.f : 2.f;
    // JACKIN's stab modes (SR stab modes): sustain counter, offbeat jab, dub.
    if (s.dialect == Jackin)
        len = flavour == 2 ? 6.5f : (flavour == 3 ? 1.f : 1.9f);

    const bool skipOdd = (s.busy < 0.25f && per <= 2) || stabSparse;
    // At most stabHitCap chord hits a bar in every style. Low BUSY keeps one
    // fewer outside ROLLER, so BUSY still moves the density under the cap.
    per = std::min (per, (s.busy < 0.25f && s.dialect != UkMin) ? kStabHitsMax - 1 : kStabHitsMax);

    rules::StabParams sp;
    sp.dialect = s.dialect;
    sp.bars = std::min (s.bars, kMaxBars);
    sp.mode = mode;
    sp.perBar = per;
    sp.lighterMask = s.dialect == UkMin ? (std::uint8_t) 0 : stabLighterMask (s, per);
    sp.skipOdd = skipOdd;
    sp.length = len;
    sp.varyLength = s.dialect == UkMin;
    sp.vel = stabVel;
    sp.repeatByParity = s.dialect == Jackin;
    rules::composeStab (lane, bass, sp, seed);
    lane.seed = seed;
}

void writeHits (Lane& lane, const Groove& g, const Settings& s, Rng& rng, int mode)
{
    clearLane (lane, lane.seed);
    // styleIntensity only adds or removes the lane. The 0-100 INTENSITY slider
    // is applyIntensity: it gates lanes and never scales these velocities.
    // Velocity stays 106, the level the UK MIN hits-vs-bass gate was set on.
    if (s.styleIntensity <= 0.25f)
        return;

    const float mix = std::clamp (s.busy * 0.45f + s.stab * 0.55f, 0.f, 1.f);
    int want = (int) std::lround (1.f + 8.f * mix);
    if (s.stab < 0.08f)
        want = std::min (want, 3);
    want = clampi (want, 1, 10);
    if (s.styleIntensity >= 0.75f)
        want = std::min (10, want + 2);

    const int loop = std::min (s.bars, 8) * 16;
    // Two or three chords from the style's chord-quality palette (RuleTables), as
    // the stab draws them, whose pitch-class sets differ by two notes or more; the
    // hits cycle through them in time order. One chord on every hit lined up with
    // a reference's offbeat stab within one note (the originality voicing match).
    // The first chord takes the lane's own draw (as before the cycle); the others
    // come from a side stream, so hit placement and count are untouched, and keep
    // the first chord's note count where the palette allows (same weight per hit).
    struct Chord { int tones[4]; int n; unsigned mask; };
    Chord chords[3] {};
    int nChords = 0;
    Rng side (rng.s ^ 0x5EC0C4D5u);
    const int wantChords = 2 + (side.next() < 0.35 ? 1 : 0);
    for (int tries = 0; tries < 24 && nChords < wantChords; ++tries)
    {
        Chord c {};
        rules::stabVoicing (s.dialect, mode, (nChords == 0 ? rng.nextU() : side.nextU()) | 1u, c.tones, c.n);
        c.n = clampi (c.n, 1, 3);
        for (int k = 0; k < c.n; ++k)
            c.mask |= 1u << (unsigned) (((c.tones[k] % 12) + 12) % 12);
        if (nChords > 0 && c.n != chords[0].n && tries < 16)
            continue;
        bool distinct = true;
        for (int j = 0; j < nChords && distinct; ++j)
        {
            const unsigned both = c.mask & chords[j].mask;
            const unsigned either = c.mask | chords[j].mask;
            // Jaccard under 0.75: not the same set give or take one note.
            distinct = (float) __builtin_popcount (both) < 0.75f * (float) __builtin_popcount (either);
        }
        if (distinct || tries == 23)
            chords[nChords++] = c;
    }
    int hitIndex = 0;
    auto chordFor = [&] () -> const Chord& { return chords[(hitIndex++) % std::max (1, nChords)]; };

    // IN STAB: the hits double the stab's answers, one hit per stab onset
    // (up to `want`), instead of filling the holes around them.
    if (s.hitsInStab && g.lane[Stab].count > 0)
    {
        const auto& stab = g.lane[Stab];
        for (int i = 0; i < stab.count && lane.count < want && lane.count < kMaxNotes; ++i)
        {
            const float start = stab.notes[i].start;
            if (start >= (float) loop)
                continue;
            bool clash = false;
            for (int k = 0; k < lane.count; ++k)
                if (std::fabs (lane.notes[k].start - start) < 1.f)
                    clash = true;
            if (clash)
                continue;
            Ev e;
            e.start = start;
            e.len = std::clamp (stab.notes[i].length, 0.5f, 1.5f);
            const Chord& ch = chordFor();
            e.nChord = (std::uint8_t) ch.n;
            for (int c = 0; c < ch.n; ++c)
                e.chord[c] = (std::int8_t) ch.tones[c];
            e.vel = 106;
            if (pushEv (lane, e, s.bars))
                lane.notes[lane.count - 1].timbre = (std::uint8_t) ((int) (rng.next() * 5.0) % 5);
        }
        if (lane.count > 0)
            return;
    }

    for (int n = 0; n < want && lane.count < kMaxNotes; ++n)
    {
        int best = -1;
        float bestS = -1.f;
        for (int t = 0; t < loop; ++t)
        {
            if (kickSlot (t % 16) || bassOnset (g.lane[Bass], t) || bassOnset (g.lane[Stab], t))
                continue;
            bool clash = false;
            for (int k = 0; k < lane.count; ++k)
                if (std::fabs (lane.notes[k].start - (float) t) < 3.f)
                    clash = true;
            if (clash)
                continue;
            float sc = (float) rng.next();
            const int slot = t % 16;
            // Offbeat lean from the stab slot-class weights (RuleTables).
            sc += 1.6f * rules::tables::kStabSlotClass[(slot & 3) - 1];
            if (((t / 16) & 1) == 1)
                sc += 0.25f;
            if (sc > bestS)
            {
                bestS = sc;
                best = t;
            }
        }
        if (best < 0)
            break;
        Ev e;
        e.start = (float) best;
        e.len = rng.next() < 0.65 ? 1.f : 1.5f;
        const Chord& ch = chords[0];
        e.nChord = (std::uint8_t) ch.n;
        for (int c = 0; c < ch.n; ++c)
            e.chord[c] = (std::int8_t) ch.tones[c];
        e.vel = 106;
        if (pushEv (lane, e, s.bars))
            lane.notes[lane.count - 1].timbre = (std::uint8_t) ((int) (rng.next() * 5.0) % 5);
    }
    // The chord cycle runs in time order.
    std::sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
    for (int i = 0; i < lane.count; ++i)
    {
        const Chord& ch = chords[i % std::max (1, nChords)];
        lane.notes[i].nChord = (std::uint8_t) ch.n;
        for (int c = 0; c < 4; ++c)
            lane.notes[i].chord[c] = (std::int8_t) (c < ch.n ? ch.tones[c] : 0);
    }
}

// How many bars a lane's figure runs before it repeats (1, 2 or 4), drawn
// from the pooled repeat-cycle shares and capped at the loop.
int drawCycleBars (Rng& rng, int bars, bool allowOne)
{
    namespace rt = rules::tables;
    float w[3];
    float sum = 0.f;
    for (int i = 0; i < 3; ++i)
    {
        w[i] = (allowOne || rt::kRepeatCycleBars[i] > 1) ? rt::kRepeatCycleShare[i] : 0.f;
        sum += w[i];
    }
    float x = (float) rng.next() * sum;
    int cyc = rt::kRepeatCycleBars[2];
    for (int i = 0; i < 3; ++i)
    {
        if (w[i] <= 0.f)
            continue;
        x -= w[i];
        if (x <= 0.f)
        {
            cyc = rt::kRepeatCycleBars[i];
            break;
        }
    }
    return clampi (cyc, 1, std::max (1, std::min (bars, 8)));
}

// A count around the pooled median: p10..median half the time, median..cap otherwise.
int drawAround (Rng& rng, const int* q, int cap)
{
    const int lo = std::min (q[0], cap);
    const int mid = std::min (q[1], cap);
    const int hi = std::min (q[2], cap);
    if (rng.next() < 0.5)
        return lo + (int) (rng.next() * (double) (mid - lo + 1));
    return mid + (int) (rng.next() * (double) (hi - mid + 1));
}

void writePluck (Lane& lane, const Settings& s, Rng& rng, int mode)
{
    clearLane (lane, lane.seed);
    // Pluck belongs to DEEP and BOUNCY. ROLLER and JACKIN leave the lane empty.
    if (s.dialect != Long && s.dialect != Pop)
        return;
    namespace rt = rules::tables;
    int per = s.dialect == Pop ? 6 : 4;
    if (s.styleIntensity <= 0.25f)
        per = 1;
    else if (s.styleIntensity >= 0.75f)
        per = std::min (8, per + 2);
    // Pitches walk the style's degree / interval Markov chain (RuleTables).
    int prevDeg = 0;
    auto nextDeg = [&] () -> int
    {
        prevDeg = rules::sampleDegreeSemis (s.dialect, mode, prevDeg, rng.nextU() | 1u);
        return prevDeg;
    };
    // The figure is written fresh as short phrases with the pooled shape:
    // about two beats long, a few notes on two or three pitches. It runs for a
    // drawn cycle (1, 2 or 4 bars) and repeats; a repeat changes a note at the
    // pooled bar 2-7 rate, and bar 8 rewrites its last phrase at the bar-8 rate.
    const int loopBars = std::min (s.bars, 8);
    if (loopBars <= 0)
        return;
    // No 1-bar cycle: a bar-long figure repeated eight times is the generic shape
    // that kept matching reference skeletons (Maestro 655 item 5).
    const int cyc = drawCycleBars (rng, loopBars, false);
    struct PNote { int slot; int deg; float len; };
    struct Notes
    {
        PNote v[64];
        int n = 0;
        void add (const PNote& x) noexcept { if (n < 64) v[n++] = x; }
    };
    Notes cell;
    auto writePhrases = [&] (int bar, Notes& out)
    {
        const int phrases = std::max (1, (per + 2) / 4);
        int left = per;
        for (int p = 0; p < phrases && left > 0; ++p)
        {
            const int n = p + 1 == phrases ? left : std::max (1, std::min (left, drawAround (rng, rt::kPhraseNotes, per)));
            left -= n;
            const float beats = rt::kPhraseBeats[0] + (float) rng.next() * (rt::kPhraseBeats[1] * 1.5f - rt::kPhraseBeats[0]);
            const int span = clampi ((int) std::lround (beats * 4.f), n, 15);
            const int zone = 16 / phrases;
            const int start = clampi (p * zone + 1 + (int) (rng.next() * (double) std::max (1, zone - span)), 1, 15);
            // The phrase's own few pitches, from the chain.
            const int distinct = clampi (std::max (3, drawAround (rng, rt::kPhraseDistinct, n)), 1, std::min (n, 6));
            int pcs[6] {};
            for (int k = 0; k < distinct; ++k)
                pcs[k] = nextDeg();
            bool used[16] {};
            int last = -1;
            for (int k = 0, guard = 0; k < n && guard < 64; ++guard)
            {
                const int slot = start + (int) (rng.next() * (double) span);
                if (slot < 1 || slot > 15 || used[slot])
                    continue;
                used[slot] = true;
                int pick = (int) (rng.next() * (double) distinct) % distinct;
                if (distinct > 1 && pick == last && rng.next() < 0.7)
                    pick = (pick + 1) % distinct;
                last = pick;
                out.add ({ bar * 16 + slot, pcs[pick], std::min (1.5f, 0.5f + (float) rng.next()) });
                ++k;
            }
        }
    };
    for (int bar = 0; bar < cyc; ++bar)
        writePhrases (bar, cell);
    for (int bar = 0; bar < loopBars; ++bar)
    {
        const int src = bar % cyc;
        const bool repeat = bar >= cyc;
        const bool bar8 = loopBars >= 8 && bar == loopBars - 1 && rng.next() < (double) rt::kBar8ChangeRate;
        Notes notes;
        for (int i = 0; i < cell.n; ++i)
            if (cell.v[i].slot / 16 == src)
            {
                // The back half of bar 8 is written again below.
                if (bar8 && cell.v[i].slot % 16 >= 8)
                    continue;
                notes.add ({ bar * 16 + cell.v[i].slot % 16, cell.v[i].deg, cell.v[i].len });
            }
        if (bar8)
        {
            Notes fresh;
            writePhrases (bar, fresh);
            for (int i = 0; i < fresh.n; ++i)
                if (fresh.v[i].slot % 16 >= 8)
                    notes.add (fresh.v[i]);
        }
        else if (repeat && notes.n > 0 && rng.next() < (double) rt::kBar2to7ChangeRate)
            notes.v[(int) (rng.next() * (double) notes.n) % notes.n].deg = nextDeg();
        for (int i = 0; i < notes.n; ++i)
        {
            Ev e;
            e.start = (float) notes.v[i].slot;
            e.len = notes.v[i].len;
            e.nChord = 1;
            e.chord[0] = (std::int8_t) notes.v[i].deg;
            e.vel = 84;
            pushEv (lane, e, s.bars);
        }
    }
    if (s.styleIntensity >= 0.75f)
    {
        // An odd-length line over the bar (period and phase drawn per roll).
        static constexpr int kPeriods[3] = { 3, 5, 7 };
        const int step = kPeriods[(int) (rng.next() * 3.0) % 3];
        const int phase = 1 + (int) (rng.next() * (double) step);
        for (int t = phase; t < s.bars * 16; t += step)
        {
            Ev e;
            e.start = (float) t;
            e.len = 1.f;
            e.nChord = 1;
            e.chord[0] = (std::int8_t) nextDeg();
            e.vel = 76;
            if (! pushEv (lane, e, s.bars))
                break;
        }
    }
}

void writePad (Lane& lane, const Settings& s, Rng& rng, int mode)
{
    clearLane (lane, lane.seed);
    if (s.styleIntensity <= 0.25f)
        return;
    const int bars[6] = { 0, 1, 2, 3, 4, 5 };
    const int b0 = bars[(int) (rng.next() * 4) % 4];
    int b1 = b0 + 3 + (int) (rng.next() * 2);
    if (b1 > 5)
        b1 = 5;
    if (b1 == b0)
        b1 = std::min (5, b0 + 2);
    for (int k = 0; k < 2; ++k)
    {
        const int bar = k == 0 ? b0 : b1;
        if (bar >= s.bars)
            continue;
        float len = 10.f + (float) ((int) (rng.next() * 16)) * 0.5f;
        const float start = (float) (bar * 16 + 1);
        const float limit = (float) (std::min (s.bars, bar + 2) * 16);
        if (start + len > limit)
            len = limit - start;
        if (len < 8.f)
            continue;
        Ev e;
        e.start = start;
        e.len = len;
        e.vel = 70;
        // Each swell draws its chord from the style's chord-quality palette.
        int tones[4] {};
        int n = 0;
        rules::stabVoicing (s.dialect, mode, rng.nextU() | 1u, tones, n);
        n = clampi (n, 1, 3);
        e.nChord = 3;
        for (int c = 0; c < 3; ++c)
            e.chord[c] = (std::int8_t) (c < n ? tones[c] : tones[0] + 12 * (c - n + 1));
        pushEv (lane, e, s.bars);
    }
}

void writeVox (Lane& lane, const Settings& s, Rng& rng)
{
    clearLane (lane, lane.seed);
    namespace rt = rules::tables;
    // One to three cuts open every figure; the figure's cycle (2 or 4 bars,
    // from the pooled repeat cycle) sets which bars the cuts come back on, and
    // the last cycle rewrites its cut slots at the pooled bar-8 rate.
    const int nCuts = 1 + (int) (rng.next() * 3);
    int slots[3];
    for (int i = 0; i < nCuts; ++i)
        slots[i] = (int) (2 + rng.next() * 12);
    const int shift = (int) (rng.next() * 11) - 5;
    const bool ghost = rng.next() < 0.3;
    const int loopBars = std::min (s.bars, 8);
    const int cyc = drawCycleBars (rng, std::max (2, loopBars), false);
    for (int bar = 0; bar < loopBars; bar += cyc)
    {
        if (bar + cyc >= loopBars && bar > 0 && loopBars >= 8 && rng.next() < (double) rt::kBar8ChangeRate)
            for (int i = 0; i < nCuts; ++i)
                slots[i] = (int) (2 + rng.next() * 12);
        for (int i = 0; i < nCuts; ++i)
        {
            Ev e;
            e.start = (float) (bar * 16 + (slots[i] % 14));
            e.len = 1.5f + (float) ((int) (rng.next() * 8)) * 0.5f;
            if (e.start + e.len > (float) ((bar + 1) * 16))
                e.len = (float) ((bar + 1) * 16) - e.start;
            e.nChord = 1;
            // chord is an offset from MIDI 64, so subtract that bias before storing a scale degree.
            e.chord[0] = (std::int8_t) (snapScale (s.dialect, shift) - 4);
            e.vel = ghost && i > 0 ? 80 : 100;
            e.flags = ghost && i > 0 ? (std::uint8_t) NoteGhost : 0;
            pushEv (lane, e, s.bars);
        }
    }
}

void writeDrums (DrumLane& drums, std::uint32_t seed, const Settings& s, Rng& rng)
{
    drums.seed = seed ? seed : 1u;
    std::memset (drums.vel, 0, sizeof drums.vel);
    std::memset (drums.accent, 0, sizeof drums.accent);
    const int steps = s.bars * 16;
    for (int t = 0; t < steps; ++t)
    {
        if (kickSlot (t % 16))
            drums.vel[Kick][t] = 112;
        if ((t % 16) == 4 || (t % 16) == 12)
            drums.vel[Clap][t] = 104;
    }
    // Kick on every beat and clap on 2 and 4 stay rules (above). Hats, perc,
    // rim, tom and the ghost snare are sampled per bar from RuleTables:
    // offbeat-8th hat presence, open-hat odds, and in-beat class weights.
    namespace rt = rules::tables;
    const int st = clampi (s.dialect, 0, rt::kStyles - 1);
    auto pickSlot = [&] (float andW, float eaW) -> int
    {
        float w[16];
        float sum = 0.f;
        for (int k = 0; k < 16; ++k)
        {
            w[k] = kickSlot (k) ? 0.f : ((k & 3) == 2 ? andW : eaW);
            sum += w[k];
        }
        float x = (float) rng.next() * sum;
        for (int k = 0; k < 16; ++k)
        {
            if (w[k] <= 0.f)
                continue;
            x -= w[k];
            if (x <= 0.f)
                return k;
        }
        return 15;
    };
    for (int bar = 0; bar < s.bars; ++bar)
    {
        int hatsPlaced = 0;
        for (int slot = 2; slot < 16; slot += 4)
        {
            const bool forced = slot == 14 && hatsPlaced < 2;
            if (! forced && rng.next() >= (double) rt::kHatOffbeatProb[st])
                continue;
            const float pOpen = (slot == 14 && (bar & 1) == 1) ? rt::kOpenHatTail : rt::kOpenHatOther;
            if (rng.next() < (double) pOpen)
                drums.vel[HatO][bar * 16 + slot] = (std::uint8_t) (70 + (int) (rng.next() * 30));
            else
                drums.vel[HatC][bar * 16 + slot] = (std::uint8_t) (64 + (int) (rng.next() * 40));
            ++hatsPlaced;
        }
        auto place = [&] (int voice, float andW, float eaW)
        {
            const int slot = pickSlot (andW, eaW);
            drums.vel[voice][bar * 16 + slot] = (std::uint8_t) (55 + (int) (rng.next() * 40));
        };
        place (Perc, rt::kPercAndWeight, rt::kPercEaWeight);
        place (Perc, rt::kPercAndWeight, rt::kPercEaWeight);
        place (Rim, rt::kRimAndWeight, rt::kRimEaWeight);
        place (TomLow, rt::kRimAndWeight, rt::kRimEaWeight);
        if (rng.next() < (double) rt::kSnareGhostBar)
            place (Snare, 1.f, 1.f);
    }
    // Ride belongs in the pattern, for four bars at a time. BUSY opens that
    // spell and adds hats; a quiet BUSY keeps a single ride and thins the hats.
    // The overlay does not draw from the drum seed, so min and max cannot tie.
    // Each ride bar draws its own offbeat 8ths from the style's offbeat odds
    // (at least two a bar), from a stream of its own so BUSY moves only this.
    const int rideBars = s.busy > 0.7f ? s.bars : (s.busy < 0.3f ? 1 : std::min (4, s.bars));
    const int rideVel = s.busy > 0.7f ? 96 : 80;
    Rng rideRng (drums.seed * 0x9E3779B1u + 0x52494445u);
    for (int bar = 0; bar < rideBars && bar < s.bars; ++bar)
    {
        int placed = 0;
        for (int slot = 2; slot < 16; slot += 4)
        {
            const bool forced = slot >= 10 && placed < (slot == 10 ? 1 : 2);
            if (! forced && rideRng.next() >= (double) rt::kHatOffbeatProb[st])
                continue;
            drums.vel[Ride][bar * 16 + slot] = (std::uint8_t) (rideVel - 8 + (int) (rideRng.next() * 16.0));
            ++placed;
        }
    }
    if (s.busy < 0.3f)
    {
        // One ride hit, on an offbeat 8th drawn for this roll.
        const int lone = 2 + 4 * (int) (rideRng.next() * 4.0);
        for (int t = 0; t < steps; ++t)
            drums.vel[Ride][t] = 0;
        drums.vel[Ride][lone] = 80;
        int seen = 0;
        for (int t = 0; t < steps; ++t)
        {
            if (drums.vel[HatC][t] == 0 && drums.vel[HatO][t] == 0)
                continue;
            if ((seen++ & 1) != 0)
            {
                drums.vel[HatC][t] = 0;
                drums.vel[HatO][t] = 0;
            }
        }
    }
    else if (s.busy > 0.7f)
    {
        for (int t = 0; t < steps; ++t)
        {
            if ((t & 1) == 0 || drums.vel[HatC][t] != 0 || drums.vel[HatO][t] != 0 || kickSlot (t % 16))
                continue;
            drums.vel[HatC][t] = 40;
        }
        for (int bar = 0; bar < s.bars; ++bar)
        {
            const int slot = pickSlot (rt::kPercAndWeight, rt::kPercEaWeight);
            if (drums.vel[Perc][bar * 16 + slot] == 0)
                drums.vel[Perc][bar * 16 + slot] = 70;
        }
    }
    // Style intensity. The middle leaves the style pattern alone. Low strips
    // percussion to one hit a bar (the bar's first). High lays an odd-length
    // perc loop over the bar line: its period (3, 5 or 7 sixteenths, the
    // style's own most likely) and phase are drawn per roll.
    if (s.styleIntensity <= 0.25f)
    {
        for (int bar = 0; bar < s.bars; ++bar)
        {
            bool kept = false;
            for (int k = 0; k < 16; ++k)
            {
                std::uint8_t& v = drums.vel[Perc][bar * 16 + k];
                if (v == 0)
                    continue;
                if (kept)
                    v = 0;
                kept = true;
            }
        }
    }
    else if (s.styleIntensity >= 0.75f)
    {
        Rng loopRng (drums.seed * 0x85EBCA6Bu + 0x50455243u);
        static constexpr int kPeriods[3] = { 3, 5, 7 };
        const int home = s.dialect == Long ? 1 : (s.dialect == Jackin ? 2 : 0);
        const double x = loopRng.next();
        const int pick = x < 0.5 ? home : (x < 0.75 ? (home + 1) % 3 : (home + 2) % 3);
        const int step = kPeriods[pick];
        const int phase = (int) (loopRng.next() * (double) step);
        for (int t = phase; t < steps; t += step)
        {
            if (kickSlot (t % 16))
                continue;
            if (drums.vel[Perc][t] == 0)
                drums.vel[Perc][t] = (std::uint8_t) (56 + (int) (loopRng.next() * 14.0));
        }
    }
    // FILL: the last beat of the phrase becomes a run into the downbeat that
    // ends on the low tom. The hits before it (one to three snare, mid tom or
    // rim) and a rising velocity are drawn per roll from a stream of their
    // own, so the same roll with FILL on and off differs only in that beat.
    // Hats there drop out so the run reads.
    if (s.fill && steps >= 16)
    {
        const int beat = steps - 4;
        Rng fillRng (drums.seed * 0xC2B2AE35u + 0x46494C4Cu);
        for (int k = 0; k < 4; ++k)
        {
            const int t = beat + k;
            drums.vel[HatC][t] = 0;
            drums.vel[HatO][t] = 0;
            drums.vel[Ride][t] = 0;
            drums.vel[Perc][t] = 0;
        }
        bool hit[3] = { false, false, false };
        const int count = 1 + (int) (fillRng.next() * 3.0);
        for (int placed = 0; placed < count;)
        {
            const int k = (int) (fillRng.next() * 3.0);
            if (! hit[k])
            {
                hit[k] = true;
                ++placed;
            }
        }
        const int base = 70 + (int) (fillRng.next() * 14.0);
        for (int k = 0; k < 3; ++k)
        {
            if (! hit[k])
                continue;
            const double v = fillRng.next();
            const int voice = v < 0.5 ? Snare : (v < 0.8 ? TomMid : Rim);
            drums.vel[voice][beat + k] = (std::uint8_t) std::min (110, base + 10 * k + (int) (fillRng.next() * 8.0));
        }
        drums.vel[TomLow][beat + 3] = (std::uint8_t) (104 + (int) (fillRng.next() * 16.0));
    }
}

bool neighbourPc (int a, int b) noexcept
{
    int d = std::abs (a - b) % 12;
    if (d > 6)
        d = 12 - d;
    return d == 1;
}

int soundingPc (int part, int semi) noexcept
{
    // Vox is written from MIDI 64, four semitones above the key, so chord 0 is a major third.
    const int bias = part == Vox ? 4 : 0;
    int pc = (bias + semi) % 12;
    if (pc < 0)
        pc += 12;
    return pc;
}

bool laneInKey (const Lane& lane, int part, int dialect) noexcept
{
    for (int i = 0; i < lane.count; ++i)
    {
        const int nc = clampi ((int) lane.notes[i].nChord, 1, 4);
        for (int c = 0; c < nc; ++c)
        {
            const int pc = soundingPc (part, lane.notes[i].chord[c]);
            if (scalePc (dialect, pc))
                continue;
            bool ok = false;
            auto near = [&] (int other)
            {
                const int opc = soundingPc (part, other);
                return neighbourPc (pc, opc) && scalePc (dialect, opc);
            };
            for (int k = 0; k < nc; ++k)
                if (k != c && near (lane.notes[i].chord[k]))
                    ok = true;
            if (i > 0)
            {
                const int pn = clampi ((int) lane.notes[i - 1].nChord, 1, 4);
                for (int k = 0; k < pn; ++k)
                    if (near (lane.notes[i - 1].chord[k]))
                        ok = true;
            }
            if (i + 1 < lane.count)
            {
                const int nn = clampi ((int) lane.notes[i + 1].nChord, 1, 4);
                for (int k = 0; k < nn; ++k)
                    if (near (lane.notes[i + 1].chord[k]))
                        ok = true;
            }
            if (! ok)
                return false;
        }
    }
    return true;
}

// Hard rejects only. Soft taste (density, rests, plans, stabs on the bass) is a penalty.
bool hardFail (const Groove& g) noexcept
{
    const int bars = g.bars;
    const int nslots = bars * 16;
    if (bars < 1 || nslots > 128)
        return true;
    for (int p = 0; p < Drums; ++p)
        if (! laneInKey (g.lane[p], p, g.dialect))
            return true;

    const Lane& bass = g.lane[Bass];
    bool sound[128] {};
    bool onsetBar[8] {};
    int onsets = 0;
    int onKick = 0;
    int rootPair = 0;
    int tie = 0;
    for (int i = 0; i < bass.count; ++i)
    {
        const auto& n = bass.notes[i];
        if (n.length < 0.25f || n.start < 0.f || n.start + n.length > (float) nslots + 0.01f)
            return true;
        const int s = (int) std::lround (n.start);
        if (s < 0 || s >= nslots)
            return true;
        const int bar = s / 16;
        const int slot = s % 16;
        if (bar >= 0 && bar < 8)
            onsetBar[bar] = true;
        const int last = (int) std::floor (n.start + n.length - 0.001f);
        for (int k = s; k <= last && k < nslots; ++k)
            sound[k] = true;
        ++onsets;
        if (kickSlot (slot))
            ++onKick;
        const int pc = ((int) n.chord[0] % 12 + 12) % 12;
        if (slot == 14 && pc == 3 && n.start + n.length > (float) ((bar + 1) * 16) + 0.01f)
            ++tie;
    }
    if (onsets > 0 && (float) onKick / (float) onsets > 0.35f)
        return true;
    for (int b = 0; b < bars && b < 8; ++b)
    {
        bool any = onsetBar[b];
        for (int k = 0; k < 16 && ! any; ++k)
            any = sound[b * 16 + k];
        if (! any)
            return true;
        bool kick = false;
        for (int k = 0; k < 16; ++k)
            if (g.drums.vel[Kick][b * 16 + k] > 0)
                kick = true;
        if (! kick)
            return true;
        bool s1 = false, s3 = false;
        for (int i = 0; i < bass.count; ++i)
        {
            const int s = (int) std::lround (bass.notes[i].start);
            if (s / 16 != b)
                continue;
            const int pc = ((int) bass.notes[i].chord[0] % 12 + 12) % 12;
            if (s % 16 == 1 && pc == 0)
                s1 = true;
            if (s % 16 == 3 && pc == 0)
                s3 = true;
        }
        if (s1 && s3)
            ++rootPair;
    }
    if (rootPair > 4 || tie > 4)
        return true;
    for (int p = 0; p < Drums; ++p)
        for (int i = 0; i < g.lane[p].count; ++i)
            if (g.lane[p].notes[i].start + g.lane[p].notes[i].length > (float) nslots + 0.01f)
                return true;
    return false;
}

float bandPenalty (float x, float lo, float hi, float tol, float w, float stick, float f) noexcept
{
    const float tolE = std::max (0.05f, tol * (1.f + 2.f * f));
    const float wE = w * (1.f - f * (1.f - stick));
    const float d = std::max (0.f, std::max (lo - x, x - hi)) / tolE;
    const float c = d <= 1.f ? d * d : std::min (4.f, 2.f * d - 1.f);
    return std::max (0.f, wE) * c;
}

// DARK at its ends also steers which candidate ships. The composer's tilt is
// per note, so a root-heavy line can come out the same at DARK 0 and DARK 1;
// this adds the dark share (b2 b3 b5 b6 b7 of the non-root pitches, the
// roll_batch measure) to the candidate's penalty. 0.2 < DARK < 0.8 adds nothing.
static float darkPull (const Lane& bass, float dark) noexcept
{
    if (dark > 0.2f && dark < 0.8f)
        return 0.f;
    int darkPcs = 0, pitches = 0;
    for (int i = 0; i < bass.count; ++i)
    {
        const int pc = ((int) bass.notes[i].chord[0] % 12 + 12) % 12;
        if (pc == 0)
            continue;
        ++pitches;
        darkPcs += pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }
    const float share = pitches > 0 ? (float) darkPcs / (float) pitches : 0.f;
    return dark >= 0.8f ? 2.f * std::max (0.f, 0.6f - share) : 2.f * share;
}

float softPenalty (const Groove& g, float freedom, const char* plan, bool offPlan, bool offContour) noexcept
{
    const int bars = std::max (1, g.bars);
    const int nslots = bars * 16;
    const Lane& bass = g.lane[Bass];
    int per[8] {};
    bool sound[128] {};
    float lens[kMaxNotes];
    int n = 0;
    int onKick = 0;
    int root = 0;
    int pcs[12] {};
    for (int i = 0; i < bass.count && n < kMaxNotes; ++i)
    {
        const int s = (int) std::lround (bass.notes[i].start);
        if (s < 0 || s >= nslots)
            continue;
        lens[n] = bass.notes[i].length;
        if (s / 16 < 8)
            ++per[s / 16];
        if (kickSlot (s % 16))
            ++onKick;
        const int pc = ((int) bass.notes[i].chord[0] % 12 + 12) % 12;
        ++pcs[pc];
        if (pc == 0)
            ++root;
        const int last = (int) std::floor (bass.notes[i].start + bass.notes[i].length - 0.001f);
        for (int k = s; k <= last && k < nslots; ++k)
            sound[k] = true;
        ++n;
    }
    float npbLo = 4.5f, npbHi = 7.5f, lenLo = 1.4f, lenHi = 2.2f;
    float rootLo = 0.35f, rootHi = 0.76f, topLo = 0.63f, topHi = 0.98f;
    float sndLo = 0.50f, sndHi = 0.85f, kickSoft = 0.10f;
    float stabLo = 0.5f, stabHi = 3.f;
    if (g.dialect == Long)
    {
        npbLo = 6.5f; npbHi = 8.f; lenLo = 2.f; lenHi = 2.4f;
        rootLo = 0.30f; rootHi = 0.45f; topLo = 0.75f; topHi = 0.85f;
        sndLo = 0.75f; sndHi = 0.90f; kickSoft = 0.30f;
        stabLo = 2.5f; stabHi = 4.5f;
    }
    else if (g.dialect == Pop)
    {
        npbLo = 4.5f; npbHi = 6.f; lenLo = 1.f; lenHi = 1.25f;
        rootLo = 0.30f; rootHi = 0.42f; topLo = 0.94f; topHi = 0.99f;
        sndLo = 0.28f; sndHi = 0.40f; kickSoft = 0.05f;
        stabLo = 9.f; stabHi = 11.f;
    }
    else if (g.dialect == Jackin)
    {
        npbLo = 6.f; npbHi = 8.f; lenLo = 1.9f; lenHi = 2.3f;
        rootLo = 0.28f; rootHi = 0.40f; topLo = 0.55f; topHi = 0.70f;
        sndLo = 0.75f; sndHi = 0.85f; kickSoft = 0.35f;
        stabLo = 5.5f; stabHi = 6.5f;
    }
    const float npb = (float) n / (float) bars;
    std::sort (lens, lens + n);
    const float lenMed = n ? lens[n / 2] : 0.f;
    int occupied = 0;
    for (int i = 0; i < nslots; ++i)
        if (sound[i])
            ++occupied;
    const float sounding = (float) occupied / (float) nslots;
    const float rootShare = n ? (float) root / (float) n : 0.f;
    int best = 0, second = 0;
    for (int c : pcs)
    {
        if (c > best)
        {
            second = best;
            best = c;
        }
        else if (c > second)
            second = c;
    }
    const float top2 = n ? (float) (best + second) / (float) n : 0.f;
    const float kick = n ? (float) onKick / (float) n : 0.f;
    int mn = 99, mx = 0;
    for (int b = 0; b < bars && b < 8; ++b)
    {
        mn = std::min (mn, per[b]);
        mx = std::max (mx, per[b]);
    }
    const float stabPb = (float) g.lane[Stab].count / (float) bars;
    int stabOnBass = 0;
    for (int i = 0; i < g.lane[Stab].count; ++i)
    {
        const int s = (int) std::lround (g.lane[Stab].notes[i].start);
        for (int b = 0; b < bass.count; ++b)
            if ((int) std::lround (bass.notes[b].start) == s)
                ++stabOnBass;
    }
    const float stabOn = g.lane[Stab].count ? (float) stabOnBass / (float) g.lane[Stab].count : 0.f;
    const float f = freedom;
    float p = 0.f;
    p += bandPenalty (npb, npbLo, npbHi, 1.f, 1.f, 0.3f, f);
    p += bandPenalty (lenMed, lenLo, lenHi, 0.4f, 1.f, 0.3f, f);
    p += bandPenalty (rootShare, rootLo, rootHi, 0.08f, 0.7f, 0.3f, f);
    p += bandPenalty (top2, topLo, topHi, 0.08f, 1.f, 0.3f, f);
    p += bandPenalty (sounding, sndLo, sndHi, 0.10f, 1.f, 0.3f, f);
    p += bandPenalty (kick, 0.f, kickSoft, 0.05f, 2.f, 0.6f, f);
    p += bandPenalty (stabPb, stabLo, stabHi, 1.f, 0.8f, 0.3f, f);
    p += bandPenalty (stabOn, 0.f, 0.80f, 0.10f, 0.4f, 0.1f, f);
    if (mx - mn < 2)
        p += bandPenalty ((float) (mx - mn), 2.f, 8.f, 1.f, 1.5f, 0.5f, f);
    bool endHole = true;
    for (int k = (bars - 1) * 16 + 12; k < nslots; ++k)
        if (sound[k])
            endHole = false;
    if (! endHole)
        p += 0.5f * (1.f - f * 0.9f);
    if (offPlan)
        p += 0.5f * (1.f - f * 0.8f);
    if (offContour)
        p += 0.4f * (1.f - f * 0.8f);
    (void) plan;
    return p;
}

float effectiveFreedom (const Settings& s, Rng& rng) noexcept
{
    float f = std::clamp (s.freedom, 0.f, 1.f);
    const float busyExt = std::fabs (s.busy - 0.5f) * 2.f;
    const float bounceExt = std::fabs (s.bounce - 0.5f) * 2.f;
    f += 0.20f * std::max (busyExt, bounceExt);
    f += std::clamp (s.chaos, 0.f, 1.f);
    if (rng.next() < 0.12)
        f += 0.25f;
    return std::clamp (f, 0.f, 1.f);
}

} // namespace

float bassSkeletonSimilarity (const Groove& a, const Groove& b) noexcept
{
    struct Tok { int bar; int slot; int pc; };
    Tok A[kMaxNotes], B[kMaxNotes];
    int na = 0, nb = 0;
    int aBars = 0, bBars = 0;
    auto load = [] (const Lane& lane, Tok* t, int& n, int& bars)
    {
        n = 0;
        bars = 0;
        for (int i = 0; i < lane.count && n < kMaxNotes; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st < 0 || st >= 128)
                continue;
            int pc = lane.notes[i].chord[0] % 12;
            if (pc < 0)
                pc += 12;
            t[n].bar = st / 16;
            t[n].slot = st % 16;
            t[n].pc = pc;
            bars = std::max (bars, t[n].bar + 1);
            ++n;
        }
    };
    load (a.lane[Bass], A, na, aBars);
    load (b.lane[Bass], B, nb, bBars);
    if (na < 6 || nb < 6 || aBars < 2 || bBars < 2)
        return 0.f;
    float best = 0.f;
    for (int w : { 2, 4 })
    {
        if (w > aBars || w > bBars)
            continue;
        for (int a0 = 0; a0 <= aBars - w; ++a0)
        {
            for (int b0 = 0; b0 <= bBars - w; ++b0)
            {
                for (int shift = -2; shift <= 2; ++shift)
                {
                    for (int transp = 0; transp < 12; ++transp)
                    {
                        bool ga[4 * 16 * 12] {};
                        bool gb[4 * 16 * 12] {};
                        int ua = 0, ub = 0;
                        auto put = [] (bool* grid, int& u, int local, int slot, int pc)
                        {
                            if (local < 0 || local >= 4 || slot < 0 || slot >= 16)
                                return;
                            const int key = (local * 16 + slot) * 12 + pc;
                            if (! grid[key])
                            {
                                grid[key] = true;
                                ++u;
                            }
                        };
                        for (int i = 0; i < na; ++i)
                        {
                            int local = A[i].bar - a0;
                            int slot = A[i].slot + shift;
                            if (slot < 0)
                            {
                                --local;
                                slot += 16;
                            }
                            else if (slot >= 16)
                            {
                                ++local;
                                slot -= 16;
                            }
                            if (local < 0 || local >= w)
                                continue;
                            put (ga, ua, local, slot, (A[i].pc + transp) % 12);
                        }
                        for (int i = 0; i < nb; ++i)
                        {
                            const int local = B[i].bar - b0;
                            if (local < 0 || local >= w)
                                continue;
                            put (gb, ub, local, B[i].slot, B[i].pc);
                        }
                        if (ua < 4 || ub < 4)
                            continue;
                        int inter = 0;
                        for (int key = 0; key < 4 * 16 * 12; ++key)
                            if (ga[key] && gb[key])
                                ++inter;
                        const int uni = ua + ub - inter;
                        if (uni > 0)
                            best = std::max (best, (float) inter / (float) uni);
                    }
                }
            }
        }
    }
    return best;
}

void uniqueChord (Note& n)
{
    const int nc = clampi ((int) n.nChord, 1, 4);
    std::int8_t kept[4];
    int w = 0;
    for (int c = 0; c < nc; ++c)
    {
        bool seen = false;
        for (int k = 0; k < w; ++k)
            if (kept[k] == n.chord[c])
                seen = true;
        if (! seen)
            kept[w++] = n.chord[c];
    }
    if (w <= 0)
        kept[w++] = 0;
    n.nChord = (std::uint8_t) w;
    for (int c = 0; c < 4; ++c)
        n.chord[c] = c < w ? kept[c] : 0;
}

// One note-on per (onset, pitch) inside a lane. Velocity 1-2 is the old
// downbeat anchor and any other placeholder; those notes do not ship.
// On a tie the louder note keeps the pitch, then the longer one.
void dedupeNoteOns (Lane& lane)
{
    int order[kMaxNotes];
    const int n = std::min (lane.count, kMaxNotes);
    for (int i = 0; i < n; ++i)
        order[i] = i;
    std::sort (order, order + n, [&] (int a, int b)
    {
        if (lane.notes[a].start != lane.notes[b].start)
            return lane.notes[a].start < lane.notes[b].start;
        if (lane.notes[a].vel != lane.notes[b].vel)
            return lane.notes[a].vel > lane.notes[b].vel;
        if (lane.notes[a].length != lane.notes[b].length)
            return lane.notes[a].length > lane.notes[b].length;
        return a < b;
    });

    Note out[kMaxNotes];
    int w = 0;
    for (int oi = 0; oi < n; ++oi)
    {
        Note note = lane.notes[order[oi]];
        if (note.vel <= 2)
            continue;
        uniqueChord (note);
        const int st = (int) std::lround (note.start);
        std::int8_t kept[4];
        int k = 0;
        const int nc = clampi ((int) note.nChord, 1, 4);
        for (int c = 0; c < nc; ++c)
        {
            bool used = false;
            for (int j = 0; j < w && ! used; ++j)
            {
                if ((int) std::lround (out[j].start) != st)
                    continue;
                const int nj = clampi ((int) out[j].nChord, 1, 4);
                for (int p = 0; p < nj; ++p)
                    if (out[j].chord[p] == note.chord[c])
                        used = true;
            }
            if (! used)
                kept[k++] = note.chord[c];
        }
        if (k <= 0)
            continue;
        note.nChord = (std::uint8_t) k;
        for (int c = 0; c < 4; ++c)
            note.chord[c] = c < k ? kept[c] : 0;
        out[w++] = note;
    }
    lane.count = w;
    for (int i = 0; i < w; ++i)
        lane.notes[i] = out[i];
}

namespace
{

// ---- Roll harmony: one mode per roll, b2 cap, bass vs stab/pad semitone guard ----

constexpr int kClashTries = 8;

// Bass b2 cap as a share of bass sounding time. Phrygian rolls may lean on the
// b2 (their stab/pad never carry the natural 2: snapToMode moves 2 -> b3).
// Outside phrygian the cap is per style: ROLLER (UkMin) 12%, DEEP (Long) 6%,
// JACKIN (Jackin) 6%, BOUNCY (Pop) 3%.
float b2Cap (int dialect, int mode) noexcept
{
    // Genre model v1.2: ROLLER 6% (was 12), DEEP 6, JACKIN 6, BOUNCY 3; phrygian 30.
    // One point of margin: the Composer gate reads b2 time on a 16th grid, clipped
    // at the next onset, which can land just over the cap our exact lengths keep.
    // Phrygian's long b2 notes round up most on that grid (5.5 -> 6 sixteenths).
    constexpr float kMargin = 0.01f;
    if (mode == phrase::Phrygian)
        return rules::tables::kPhrygianB2Cap - 0.04f;
    return std::max (0.f, rules::tables::kB2Cap[clampi (dialect, 0, rules::tables::kStyles - 1)] - kMargin);
}

// Out-of-mode pitch class -> the in-mode neighbour. Works for all three modes:
// 1->0, 2->3 (phrygian), 4->3, 6->7, 8->7 (dorian), 9->8, 11->10.
int snapToMode (int semi, int mode) noexcept
{
    if (phrase::inMode (semi, mode))
        return semi;
    const int pc = pcOf (semi);
    return semi + ((pc == 2 || pc == 6) ? 1 : -1);
}

void conformUpperLane (Lane& lane, int mode)
{
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        const int nc = clampi ((int) n.nChord, 1, 4);
        for (int c = 0; c < nc; ++c)
            n.chord[c] = (std::int8_t) snapToMode (n.chord[c], mode);
        uniqueChord (n);
    }
}

void sortByStart (Lane& lane)
{
    std::stable_sort (lane.notes, lane.notes + lane.count,
                      [] (const Note& a, const Note& b) { return a.start < b.start; });
}

// Sounding length of bass note i (the lane is monophonic and sorted).
float bassSounding (const Lane& lane, int i) noexcept
{
    float len = lane.notes[i].length;
    if (i + 1 < lane.count)
        len = std::min (len, lane.notes[i + 1].start - lane.notes[i].start);
    return std::max (0.f, len);
}

// Root on slot 1 and slot 3 of one bar is the banned 2&4 root figure.
bool makesRootTwoFour (const Lane& lane, int idx, int semi) noexcept
{
    if (pcOf (semi) != 0)
        return false;
    const int st = (int) std::lround (lane.notes[idx].start);
    const int slot = st % 16;
    if (slot != 1 && slot != 3)
        return false;
    const int want = slot == 1 ? 3 : 1;
    for (int i = 0; i < lane.count; ++i)
    {
        if (i == idx)
            continue;
        const int s2 = (int) std::lround (lane.notes[i].start);
        if (s2 / 16 == st / 16 && s2 % 16 == want && pcOf (lane.notes[i].chord[0]) == 0)
            return true;
    }
    return false;
}

// Keep the register: move to the nearest octave of target pc.
int retarget (int semi, int pc) noexcept
{
    int d = pc - pcOf (semi);
    if (d > 6)
        d -= 12;
    if (d < -6)
        d += 12;
    return semi + d;
}

float bassTotal (const Lane& lane) noexcept
{
    float total = 0.f;
    for (int i = 0; i < lane.count; ++i)
        total += bassSounding (lane, i);
    return total;
}

float bassB2Time (const Lane& lane) noexcept
{
    float t = 0.f;
    for (int i = 0; i < lane.count; ++i)
        if (pcOf (lane.notes[i].chord[0]) == 1)
            t += bassSounding (lane, i);
    return t;
}

void conformBass (Lane& lane, int mode, float cap)
{
    sortByStart (lane);
    const bool phrygian = mode == phrase::Phrygian;
    for (int i = 0; i < lane.count; ++i)
        if (pcOf (lane.notes[i].chord[0]) != 1 || phrygian)
            lane.notes[i].chord[0] = (std::int8_t) snapToMode (lane.notes[i].chord[0], mode);

    // The b2 holds at most `cap` of the bass sounding time. Outside phrygian it
    // is also only a one-sixteenth passing/neighbour note between 1 and 2.
    const float budget = cap * bassTotal (lane);
    float used = 0.f;
    auto edgePc = [] (int pc) { return pc == 0 || pc == 2; };
    for (int i = 0; i < lane.count; ++i)
    {
        const int semi = lane.notes[i].chord[0];
        if (pcOf (semi) != 1)
            continue;
        const float t = bassSounding (lane, i);
        bool passing = phrygian || (lane.notes[i].length <= 1.05f && i > 0 && i + 1 < lane.count);
        if (passing && ! phrygian)
        {
            const Note& p = lane.notes[i - 1];
            const Note& n = lane.notes[i + 1];
            passing = edgePc (pcOf (p.chord[0])) && edgePc (pcOf (n.chord[0]))
                   && lane.notes[i].start - (p.start + p.length) <= 1.05f
                   && n.start - (lane.notes[i].start + lane.notes[i].length) <= 1.05f;
        }
        if (passing && used + t <= budget + 1e-4f)
        {
            used += t;
            continue;
        }
        int to = semi - 1; // root
        if (makesRootTwoFour (lane, i, to))
            to = semi + 2; // b3
        lane.notes[i].chord[0] = (std::int8_t) to;
    }
}

bool overlaps (const Note& a, const Note& b) noexcept
{
    return a.start < b.start + b.length - 1e-3f && b.start < a.start + a.length - 1e-3f;
}

// Pitch-class bitmask of every stab/pad tone sounding with note b.
std::uint32_t upperPcsOver (const Groove& g, const Note& b) noexcept
{
    std::uint32_t m = 0;
    for (int part : { (int) Stab, (int) Pad })
    {
        const Lane& l = g.lane[part];
        for (int k = 0; k < l.count; ++k)
        {
            if (l.notes[k].vel <= 2 || ! overlaps (b, l.notes[k]))
                continue;
            const int nc = clampi ((int) l.notes[k].nChord, 1, 4);
            for (int c = 0; c < nc; ++c)
                m |= 1u << pcOf (l.notes[k].chord[c]);
        }
    }
    return m;
}

std::uint32_t upperPcsWith (const Groove& g, int i) noexcept
{
    return upperPcsOver (g, g.lane[Bass].notes[i]);
}

// Bass pcs that sit a semitone (m2 / m9 / M7) from any tone in `upper`.
std::uint32_t clashMask (std::uint32_t upper) noexcept
{
    std::uint32_t m = 0;
    for (int pc = 0; pc < 12; ++pc)
        if (upper & (1u << pc))
            m |= (1u << ((pc + 1) % 12)) | (1u << ((pc + 11) % 12));
    return m;
}

void fixBassClashes (Groove& g, int mode, float cap, std::uint32_t seed)
{
    Lane& lane = g.lane[Bass];
    const float budget = cap * bassTotal (lane);
    float b2Used = bassB2Time (lane);
    for (int i = 0; i < lane.count; ++i)
    {
        if (lane.notes[i].vel <= 2)
            continue;
        const std::uint32_t upper = upperPcsWith (g, i);
        const std::uint32_t bad = clashMask (upper);
        const int semi = lane.notes[i].chord[0];
        if ((bad & (1u << pcOf (semi))) == 0)
            continue;
        const float t = bassSounding (lane, i);
        const bool wasB2 = pcOf (semi) == 1;
        auto ok = [&] (int pc)
        {
            if (bad & (1u << pc))
                return false;
            if (! phrase::inMode (pc, mode))
                return false;
            // Outside phrygian the fixer never adds a b2; inside, only within the cap.
            if (pc == 1 && (mode != phrase::Phrygian || b2Used - (wasB2 ? t : 0.f) + t > budget + 1e-4f))
                return false;
            return ! makesRootTwoFour (lane, i, retarget (semi, pc));
        };
        // Resample: bounded, deterministic from the bass seed and the note.
        // JACKIN keys it on the note's place in its 2-bar loop, so the
        // repeats of one note take the same pitch.
        const std::uint32_t key = g.dialect == Jackin
                                      ? 0x10000u + (std::uint32_t) (((int) std::lround (lane.notes[i].start) % 32 + 32) % 32)
                                      : (std::uint32_t) i;
        int chosen = -1;
        Rng r (seed ^ (0x5EC0DEu + key * 0x9E3779B9u));
        for (int tryN = 0; tryN < kClashTries && chosen < 0; ++tryN)
        {
            const int pc = phrase::degreeToSemis ((int) (r.nextU() % 7u), mode);
            if (ok (pc))
                chosen = pc;
        }
        // Fallback: nearest chord tone sounding above, then the root, then any safe pc.
        if (chosen < 0)
        {
            const int here = pcOf (semi);
            for (int d = 0; d <= 6 && chosen < 0; ++d)
                for (int sgn : { -1, 1 })
                {
                    const int pc = (here + sgn * d + 12) % 12;
                    if (chosen < 0 && (upper & (1u << pc)) && ok (pc))
                        chosen = pc;
                }
        }
        if (chosen < 0 && ok (0))
            chosen = 0;
        for (int pc = 0; pc < 12 && chosen < 0; ++pc)
            if (ok (pc))
                chosen = pc;
        if (chosen >= 0)
        {
            lane.notes[i].chord[0] = (std::int8_t) retarget (semi, chosen);
            b2Used += (chosen == 1 ? t : 0.f) - (wasB2 ? t : 0.f);
        }
    }
}

// conformBass's rule for a b2 outside phrygian: a one-16th passing note with
// the root or the 2 on both sides.
bool b2Passing (const Lane& lane, int i) noexcept
{
    if (i <= 0 || i + 1 >= lane.count || lane.notes[i].length > 1.05f)
        return false;
    const Note& p = lane.notes[i - 1];
    const Note& n = lane.notes[i + 1];
    auto edgePc = [] (int pc) { return pc == 0 || pc == 2; };
    return edgePc (pcOf (p.chord[0])) && edgePc (pcOf (n.chord[0]))
        && lane.notes[i].start - (p.start + p.length) <= 1.05f
        && n.start - (lane.notes[i].start + lane.notes[i].length) <= 1.05f;
}

// A&R 490 #2 (ROLLER roll_04, Eb bass in D minor). fixBassClashes can move a
// passing b2's neighbour (a 2 under a b3 stab), which leaves a b2 that is no
// longer passing: out of the roll's mode. Outside phrygian such a b2 takes
// the root, else the b3, else the nearest in-mode pitch with no semitone
// clash and no 2&4 root figure. Phrygian rolls keep their b2 (in mode).
void settleStrandedB2 (Groove& g, int mode)
{
    if (mode == phrase::Phrygian)
        return;
    Lane& lane = g.lane[Bass];
    sortByStart (lane);
    static const int order[] = { 0, 3, 2, 10, 5, 7, 8, 9 };
    for (int i = 0; i < lane.count; ++i)
    {
        const int semi = lane.notes[i].chord[0];
        if (pcOf (semi) != 1 || b2Passing (lane, i))
            continue;
        const std::uint32_t bad = clashMask (upperPcsWith (g, i));
        int chosen = -1;
        for (int pc : order)
        {
            if (! phrase::inMode (pc, mode) || (bad & (1u << pc)) != 0)
                continue;
            if (makesRootTwoFour (lane, i, retarget (semi, pc)))
                continue;
            chosen = pc;
            break;
        }
        lane.notes[i].chord[0] = (std::int8_t) retarget (semi, chosen >= 0 ? chosen : 0);
    }
}

// Bass is kept: drop the stab/pad tones that clash with it instead.
void fixUpperClashes (Groove& g, int part)
{
    Lane& l = g.lane[part];
    const Lane& bass = g.lane[Bass];
    for (int k = 0; k < l.count; ++k)
    {
        std::uint32_t bassPcs = 0;
        int firstBass = -1;
        for (int i = 0; i < bass.count; ++i)
            if (bass.notes[i].vel > 2 && overlaps (bass.notes[i], l.notes[k]))
            {
                bassPcs |= 1u << pcOf (bass.notes[i].chord[0]);
                if (firstBass < 0)
                    firstBass = i;
            }
        if (bassPcs == 0)
            continue;
        const std::uint32_t bad = clashMask (bassPcs);
        Note& n = l.notes[k];
        const int nc = clampi ((int) n.nChord, 1, 4);
        // Vox is written from MIDI 64 (soundingPc adds its bias); the others sound as written.
        const int bias = soundingPc (part, 0);
        std::int8_t keep[4];
        int w = 0;
        for (int c = 0; c < nc; ++c)
            if ((bad & (1u << soundingPc (part, n.chord[c]))) == 0)
                keep[w++] = n.chord[c];
        if (w == 0)
        {
            // Take a bass pitch class that clashes with no other bass note it
            // sounds over. When the bass slides a semitone under the note (two
            // bass notes a semitone apart), no pitch fits both: the note keeps
            // the first bass pitch and ends where the bass moves (or goes).
            int target = -1;
            for (int pc = 0; pc < 12; ++pc)
                if ((bassPcs & (1u << pc)) != 0 && (bad & (1u << pc)) == 0)
                {
                    target = pc;
                    break;
                }
            if (target < 0)
            {
                target = pcOf (bass.notes[firstBass].chord[0]);
                float cut = n.start + n.length;
                for (int i = 0; i < bass.count; ++i)
                    if (bass.notes[i].vel > 2 && overlaps (bass.notes[i], n) && bass.notes[i].start > n.start
                        && pcOf (bass.notes[i].chord[0]) != target)
                        cut = std::min (cut, bass.notes[i].start);
                n.length = cut - n.start;
                if (n.length < 0.5f)
                    n.vel = 0; // dropped below
            }
            keep[w++] = (std::int8_t) retarget (n.chord[0], (target - bias + 12) % 12);
        }
        // The engine's stab chord memory turns a one-tone stab back into a full chord,
        // which would put the clashing tones back (and play notes the MIDI file lacks).
        // A lone surviving tone gets its octave, so the stab plays as written.
        if (part == Stab && w == 1)
            keep[w++] = (std::int8_t) (keep[0] + (keep[0] <= 36 ? 12 : -12));
        n.nChord = (std::uint8_t) w;
        for (int c = 0; c < 4; ++c)
            n.chord[c] = c < w ? keep[c] : 0;
    }
    int w2 = 0;
    for (int k = 0; k < l.count; ++k)
        if (l.notes[k].vel > 0)
            l.notes[w2++] = l.notes[k];
    l.count = w2;
}

// No lane plays the same pitch twice at once: a note that is still sounding
// when the same pitch starts again ends one tick before it. Run after
// dedupeNoteOns, which already removed same-onset repeats.
void trimSamePitchOverlaps (Lane& lane)
{
    sortByStart (lane);
    const float tick = 1.f / 120.f;
    for (int i = 0; i < lane.count; ++i)
    {
        Note& a = lane.notes[i];
        const int na = clampi ((int) a.nChord, 1, 4);
        for (int j = i + 1; j < lane.count; ++j)
        {
            const Note& b = lane.notes[j];
            if (b.start >= a.start + a.length - 1e-3f)
                break;
            const int nb = clampi ((int) b.nChord, 1, 4);
            bool shared = false;
            for (int c = 0; c < na && ! shared; ++c)
                for (int d = 0; d < nb; ++d)
                    if (a.chord[c] == b.chord[d])
                        shared = true;
            if (shared)
                a.length = std::max (tick, b.start - a.start - tick);
        }
    }
}

// At most stabHitCap (s, bar) chord onsets a bar. The beat-1 hit and the
// closing answer of bars 4 and 8 stay; then louder, then more accented (8th
// offbeat, then 8th), then earlier hits win.
void capStabHits (Lane& lane, const Settings& s)
{
    for (int bar = 0; bar < s.bars && bar < kMaxBars; ++bar)
    {
        const int cap = stabHitCap (s, bar);
        int slots[16];
        int vel[16];
        int n = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st < bar * 16 || st >= (bar + 1) * 16)
                continue;
            const int slot = st - bar * 16;
            int k = 0;
            while (k < n && slots[k] != slot)
                ++k;
            if (k == n)
            {
                slots[n] = slot;
                vel[n] = 0;
                ++n;
            }
            vel[k] = std::max (vel[k], (int) lane.notes[i].vel);
        }
        if (n <= cap)
            continue;
        int first = 16;
        int last = -1;
        for (int k = 0; k < n; ++k)
        {
            first = std::min (first, slots[k]);
            last = std::max (last, slots[k]);
        }
        float score[16];
        for (int k = 0; k < n; ++k)
        {
            const int o = slots[k];
            float sc = (float) vel[k] + (o % 4 == 2 ? 6.f : (o % 2 == 0 ? 3.f : 0.f)) - 0.01f * (float) o;
            if (o == first && o < 4)
                sc += 1000.f;
            if ((bar == 3 || bar == 7) && o == last && o >= 8)
                sc += 500.f;
            score[k] = sc;
        }
        bool keep[16] {};
        for (int pick = 0; pick < cap; ++pick)
        {
            int best = -1;
            for (int k = 0; k < n; ++k)
                if (! keep[k] && (best < 0 || score[k] > score[best]))
                    best = k;
            if (best >= 0)
                keep[best] = true;
        }
        int w = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            bool drop = false;
            if (st >= bar * 16 && st < (bar + 1) * 16)
                for (int k = 0; k < n; ++k)
                    if (slots[k] == st - bar * 16 && ! keep[k])
                        drop = true;
            if (! drop)
                lane.notes[w++] = lane.notes[i];
        }
        lane.count = w;
    }
}

// Bar-8 pickup: 1-3 bass notes on 16ths 13-16 of the last bar that approach
// the bar-1 root by step or by a fifth, in the roll's mode. Never on a kick
// step, never a b2, never a semitone from a sounding stab/pad tone. Replaces
// whatever the bass had from the first pickup 16th on. BOUNCY keeps a rest of
// four 16ths in front of it so its bar-8 turn survives.
bool addBar8Pickup (Groove& g, int mode, int dialect, std::uint32_t bassSeed)
{
    if (! rollWantsPickup (bassSeed))
        return false;
    Lane& lane = g.lane[Bass];
    const int bars = clampi (g.bars, 1, kMaxBars);
    const int origin = (bars - 1) * kStepsPerBar;
    const float loopEnd = (float) (bars * kStepsPerBar);
    const float tick = 1.f / 120.f;
    if (lane.count <= 0 || lane.count + 3 > kMaxNotes)
        return false;
    sortByStart (lane);
    const int target = retarget (lane.notes[0].chord[0], 0);
    const int baseVel = clampi ((int) lane.notes[0].vel, 60, 120);
    const std::uint8_t timbre = lane.notes[0].timbre;
    auto deg = [&] (int d) { return phrase::degreeToSemis (d, mode); };
    const int below5 = deg (4) - 12;
    const int below6 = deg (5) - 12;
    const int below7 = deg (6) - 12;
    const int above2 = deg (1);
    const int above3 = deg (2);
    const int above4 = deg (3);
    struct Shape
    {
        int n;
        int off[3];
    };
    // Offsets over the target; the last note resolves by step or by a fifth.
    const Shape shapes[7] = {
        { 1, { below7, 0, 0 } },
        { 1, { below5, 0, 0 } },
        { 1, { above2, 0, 0 } },
        { 2, { below5, below7, 0 } },
        { 2, { above3, above2, 0 } },
        { 3, { below5, below6, below7 } },
        { 3, { above4, above3, above2 } },
    };
    struct Slots
    {
        int n;
        int s[3];
    };
    // Slot 12 is beat 4, a kick step, so the pickup lives on 13-15.
    static const Slots slotSets[6] = {
        { 1, { 13, 0, 0 } },
        { 1, { 14, 0, 0 } },
        { 1, { 15, 0, 0 } },
        { 2, { 13, 15, 0 } },
        { 2, { 14, 15, 0 } },
        { 3, { 13, 14, 15 } },
    };
    auto kickAt = [&] (int slot)
    {
        const int abs = origin + slot;
        return kickSlot (slot) || (abs >= 0 && abs < kMaxSteps && g.drums.vel[Kick][abs] > 0);
    };
    auto build = [&] (const Shape& sh, const Slots& sl, Note* out) -> bool
    {
        if (sh.n != sl.n)
            return false;
        for (int j = 0; j < sh.n; ++j)
        {
            const int semi = target + sh.off[j];
            if (pcOf (semi) == 1 || ! phrase::inMode (semi, mode) || kickAt (sl.s[j]))
                return false;
            Note n;
            n.start = (float) (origin + sl.s[j]);
            const float end = j + 1 < sl.n ? (float) (origin + sl.s[j + 1]) - tick : loopEnd;
            n.length = end - n.start;
            n.chord[0] = (std::int8_t) semi;
            n.nChord = 1;
            n.vel = (std::uint8_t) clampi (baseVel - 8 + 6 * j, 40, 127);
            n.flags = 0;
            n.timbre = timbre;
            if ((clashMask (upperPcsOver (g, n)) & (1u << pcOf (semi))) != 0)
                return false;
            out[j] = n;
        }
        return true;
    };

    Rng r ((bassSeed ? bassSeed : 1u) ^ 0x7E1C4u);
    const int want = 1 + (int) (r.nextU() % 3u);
    const int shapeSpin = (int) (r.nextU() % 7u);
    const int slotSpin = (int) (r.nextU() % 6u);
    Note picked[3];
    int nPicked = 0;
    for (int k = 0; k < 3 && nPicked == 0; ++k)
    {
        const int n = (want - 1 + k) % 3 + 1;
        for (int a = 0; a < 7 && nPicked == 0; ++a)
        {
            const Shape& sh = shapes[(shapeSpin + a) % 7];
            if (sh.n != n)
                continue;
            for (int b = 0; b < 6 && nPicked == 0; ++b)
            {
                // BOUNCY tries the longest (earliest) slots first.
                const Slots& sl = slotSets[(dialect == Pop ? b : slotSpin + b) % 6];
                if (sl.n == n && build (sh, sl, picked))
                    nPicked = n;
            }
        }
    }
    if (nPicked == 0)
        return false;

    const int firstSlot = (int) std::lround (picked[0].start) - origin;
    const int cut = origin + (dialect == Pop ? firstSlot - 4 : firstSlot);
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        Note n = lane.notes[i];
        const int st = (int) std::lround (n.start);
        if (st >= cut && n.start < loopEnd)
            continue;
        if (n.start + n.length > (float) cut - tick)
        {
            n.length = (float) cut - tick - n.start;
            if (n.length < 0.25f)
                continue;
        }
        lane.notes[w++] = n;
    }
    for (int j = 0; j < nPicked && w < kMaxNotes; ++j)
        lane.notes[w++] = picked[j];
    lane.count = w;
    sortByStart (lane);
    return true;
}

void harmonizeRoll (Groove& g, const Settings& s, std::uint32_t bassSeed, std::uint32_t partMask)
{
    auto regen = [&] (int part) { return (partMask & (1u << part)) != 0; };
    const int mode = tRoll.mode;
    (void) bassSeed;
    const float cap = b2Cap (s.dialect, mode);
    for (int part : { (int) Stab, (int) Pad, (int) Pluck, (int) Hits })
        if (regen (part))
            conformUpperLane (g.lane[part], mode);
    if (regen (Bass))
    {
        addBar8Pickup (g, mode, s.dialect, bassSeed);
        conformBass (g.lane[Bass], mode, cap);
        // Outside phrygian the fixer never adds a b2; inside it stays under the cap.
        fixBassClashes (g, mode, cap, bassSeed);
        // ...but it can strand a passing b2 by moving its neighbour.
        settleStrandedB2 (g, mode);
    }
    else
    {
        for (int part : { (int) Stab, (int) Pad })
            if (regen (part))
                fixUpperClashes (g, part);
    }
    // #62's clash rule on every upper lane: pluck, hits and vox give way to the
    // bass that ships (tones a semitone from it are dropped or retargeted).
    for (int part : { (int) Pluck, (int) Hits, (int) Vox })
        if (regen (part) || regen (Bass))
            fixUpperClashes (g, part);
    // Snapping can fold two tones onto one pitch: one note-on per (onset,
    // pitch) and no overlapping same-pitch notes in any lane that was rolled.
    for (int part = 0; part < Drums; ++part)
        if (regen (part))
        {
            dedupeNoteOns (g.lane[part]);
            trimSamePitchOverlaps (g.lane[part]);
        }
}

// A&R msg 423 #1: target share of bass note-ons on kick steps, per style.
// Reference medians: ROLLER ~17%, DEEP ~7%, BOUNCY 25% (Maestro 655), JACKIN ~19%.
// The 35% hard cap (hardFail, and the composer's own kick cap) always wins.
float bassKickTarget (int dialect) noexcept
{
    // Genre model per-style medians (ROLLER 17, DEEP 7, BOUNCY 25, JACKIN 19).
    return rules::tables::kKickTarget[clampi (dialect, 0, rules::tables::kStyles - 1)];
}

// A&R msg 423 #2: how often a held bass note leaves a one-16th breath before
// the next onset. JACKIN keeps the most, so its stabs still find bass rests.
float bassBreathShare (int dialect) noexcept
{
    switch (dialect)
    {
        case Long:   return 0.25f;
        case Jackin: return 0.45f;
        default:     return 0.35f;
    }
}

std::uint32_t postHash (std::uint32_t a, std::uint32_t b) noexcept
{
    std::uint32_t h = a * 0x9E3779B1u ^ (b * 0x85EBCA6Bu + 0x7F4A7C15u);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

// The last bar of a 4- or 8-bar loop keeps its four-16th turn rest (the composer,
// ensureBar8Turn). Nothing in the post passes moves or holds into it.
float bassTailCut (int bars) noexcept
{
    return bars >= 4 ? (float) ((bars - 1) * 16 + 12) : (float) (bars * 16);
}

// A&R MIDI gate (seed 2026101852, 20 rolls a style): ROLLER / DEEP / BOUNCY bass
// played 2.9-3.75 onsets a bar against the references' 4.3-5.2
// (style_rules.json bass_onsets_per_bar targets: ROLLER 5.0, DEEP 5.21,
// BOUNCY 5.0). Per-style target at the shipped BUSY (0.40). DEEP's front half
// aims a little lower because its back-half contour adds about 0.4 a bar.
// JACKIN (reference 4.44) and BOUNCY pulse (its own 5-8 grid) are left alone.
float bassDensityTarget (int dialect) noexcept
{
    // Genre model v1.2 density medians (ROLLER 5.0, DEEP 5.2, BOUNCY 5.0).
    namespace rt = rules::tables;
    switch (dialect)
    {
        case UkMin: return rt::kBassDensityMedian[UkMin];
        case Long:  return rt::kBassDensityMedian[Long] - 0.3f; // the back-half contour adds the rest
        case Pop:   return rt::kBassDensityMedian[Pop];
        default:    return 0.f; // JACKIN: the composer's density curve already sits at 4.4
    }
}

bool bassRootAt (const Lane& lane, int abs) noexcept
{
    for (int i = 0; i < lane.count; ++i)
        if ((int) std::lround (lane.notes[i].start) == abs && pcOf (lane.notes[i].chord[0]) == 0)
            return true;
    return false;
}

// A free 16th for a new bass attack in `bar`, or -1. Never a kick step, never
// inside the last bar's turn rest, never a run of three 16th attacks. The
// order depends only on the seed and the bar's parity, so bars that repeat
// take the same fill and the 2-bar repetition holds (the "whole slots" rule
// from the ROLLER style pass). "And" 16ths come first, then e / a (the
// style_rules slot-class weights); BOUNCY's bounce prefers the e / a.
int bassFillSlot (const bool* used, int bar, int nslots, float tailCut, int dialect, std::uint32_t seed)
{
    int order[12];
    std::uint32_t prio[12];
    int n = 0;
    for (int s = 0; s < 16 && n < 12; ++s)
    {
        if (kickSlot (s))
            continue;
        const bool andSlot = (s & 3) == 2;
        const std::uint32_t rank = dialect == Pop ? (andSlot ? 1u : 0u) : (andSlot ? 0u : 1u);
        order[n] = s;
        prio[n] = (rank << 28) | (postHash (seed ^ 0xF111u, (std::uint32_t) ((bar & 1) * 16 + s)) >> 4);
        ++n;
    }
    for (int a = 1; a < n; ++a)
        for (int b = a; b > 0 && prio[b] < prio[b - 1]; --b)
        {
            std::swap (prio[b], prio[b - 1]);
            std::swap (order[b], order[b - 1]);
        }
    for (int k = 0; k < n; ++k)
    {
        const int abs = bar * 16 + order[k];
        if (abs < 0 || abs >= nslots || used[abs] || (float) abs >= tailCut)
            continue;
        int left = 0;
        for (int q = abs - 1; q >= 0 && used[q]; --q)
            ++left;
        int right = 0;
        for (int q = abs + 1; q < nslots && used[q]; ++q)
            ++right;
        if (left + right + 1 > 2)
            continue;
        return abs;
    }
    return -1;
}

// How many attacks bassFillSlot can still place in `bar` (up to `want`).
int bassFillRoom (const bool* used, int bar, int nslots, float tailCut, int dialect, std::uint32_t seed, int want)
{
    bool scratch[kMaxSteps] {};
    for (int i = 0; i < nslots && i < kMaxSteps; ++i)
        scratch[i] = used[i];
    int got = 0;
    while (got < want)
    {
        const int abs = bassFillSlot (scratch, bar, nslots, tailCut, dialect, seed);
        if (abs < 0)
            break;
        scratch[abs] = true;
        ++got;
    }
    return got;
}

// Pitch for a new attack (the lane is sorted). BOUNCY re-strikes the previous
// note, an octave away about one time in seven so its octave share holds.
// The other styles take a pitch class the bar already plays (then 5th, b3,
// b7, root), never the neighbours' own pitch class, so the held line keeps
// moving and no breath is forced. Never a b2, never the 2&4 root figure.
// harmonizeRoll still conforms the mode and the stab clashes afterwards.
int bassFillPitch (const Lane& lane, int abs, int dialect, std::uint32_t seed)
{
    int prev = -1;
    int next = -1;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < abs)
            prev = i;
        else if (st > abs && next < 0)
            next = i;
    }
    const int prevSemi = prev >= 0 ? lane.notes[prev].chord[0] : (next >= 0 ? lane.notes[next].chord[0] : 0);
    const int nextSemi = next >= 0 ? lane.notes[next].chord[0] : prevSemi;
    const int bar = abs / 16;
    const int slot = abs % 16;
    const bool rootBlocked = (slot == 1 || slot == 3) && bassRootAt (lane, bar * 16 + (slot == 1 ? 3 : 1));
    const std::uint32_t h = postHash (seed ^ 0xF177u, (std::uint32_t) ((bar & 1) * 16 + slot));
    auto place = [&] (int pc)
    {
        int semi = retarget (prevSemi, pc);
        if (semi < 0)
            semi += 12;
        return semi;
    };

    if (dialect == Pop)
    {
        int semi = prevSemi;
        if (pcOf (semi) == 1)
            semi = nextSemi;
        if (pcOf (semi) == 1 || (pcOf (semi) == 0 && rootBlocked))
            semi = place (7);
        if (h % 100u < 15u)
            semi = semi >= 12 ? semi - 12 : semi + 12;
        return semi;
    }

    int cand[12];
    int nc = 0;
    auto addPc = [&] (int pc)
    {
        for (int k = 0; k < nc; ++k)
            if (cand[k] == pc)
                return;
        if (nc < 12)
            cand[nc++] = pc;
    };
    for (int i = 0; i < lane.count; ++i)
        if ((int) std::lround (lane.notes[i].start) / 16 == bar)
            addPc (pcOf (lane.notes[i].chord[0]));
    const int fromBar = nc;
    for (int pc : { 7, 3, 10, 0 })
        addPc (pc);
    const int pPc = pcOf (prevSemi);
    const int nPc = pcOf (nextSemi);
    auto ok = [&] (int pc) { return pc != 1 && pc != pPc && pc != nPc && ! (pc == 0 && rootBlocked); };
    const int rot = fromBar > 0 ? (int) (h % (std::uint32_t) fromBar) : 0;
    for (int t = 0; t < fromBar; ++t)
        if (ok (cand[(rot + t) % fromBar]))
            return place (cand[(rot + t) % fromBar]);
    for (int k = fromBar; k < nc; ++k)
        if (ok (cand[k]))
            return place (cand[k]);
    return place (pPc == 7 ? 3 : 7);
}

// Brings the bass to its style's onset density (bassDensityTarget, scaled by
// BUSY the way applyBusyDensity scales the phrase, so the knob still moves it).
// Every bar takes the same lift, which keeps the bar-to-bar shape (BOUNCE's
// spread, the call / answer bars); BOUNCE only lifts by what every bar can take.
// DEEP's back half then moves against the front half on a seeded contour
// (after the DEEP dark contour idea): a sparse line builds into bars 5-7, a
// line already busier than the target thins there. Bar 8 keeps its turn.
// Runs before placeBassOnKicks and the holds, so both see the new attacks.
void fillBassDensity (Lane& lane, int dialect, int bars, float busy, std::uint32_t seed)
{
    const float base = bassDensityTarget (dialect);
    const int nb = std::min (bars, kMaxBars);
    if (base <= 0.f || nb < 2 || lane.count <= 0)
        return;
    sortByStart (lane);
    const int nslots = nb * 16;
    const float tailCut = bassTailCut (nb);
    const float target = base * busyFactor (busy) / busyFactor (0.40f);
    bool used[kMaxSteps] {};
    int total = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < 0 || st >= nslots)
            continue;
        used[st] = true;
        ++total;
    }
    const float mean = (float) total / (float) nb;
    const int lift = std::max (0, (int) std::lround (target - mean));
    int add[kMaxBars] {};
    int thin[kMaxBars] {};
    // BOUNCE needs its bar-to-bar spread (onset sd >= 0.7): the body bars take
    // one lift they can all hold. The turn bar (usually the thin one) takes
    // what fits; spreadBounceDensity still guards the spread after the pickup.
    int even = lift;
    if (dialect == Pop)
        for (int b = 0; b + 1 < nb && even > 0; ++b)
            even = std::min (even, bassFillRoom (used, b, nslots, tailCut, dialect, seed, lift));
    for (int b = 0; b < nb; ++b)
        add[b] = even;
    if (dialect == Long && nb == 8)
    {
        // Back-half density movement (bars 5-7) drawn per bar from the model's
        // DEEP density curve (mean, sd per bar), against the line's own mean.
        namespace rt = rules::tables;
        const int st = std::clamp ((int) Long, 0, rt::kStyles - 1);
        Rng rd (postHash (seed ^ 0xDEE5u, 5u) | 1u);
        const bool busyLine = mean >= target + 1.f;
        float front = 0.f;
        for (int j = 0; j < 4; ++j)
            front += rt::kBassDensity[st][j][0] / 4.f;
        int c[3];
        int moved = 0;
        for (int j = 0; j < 3; ++j)
        {
            const float g = (float) ((rd.next() + rd.next() + rd.next() - 1.5) * 2.0);
            const float delta = rt::kBassDensity[st][4 + j][0] - front + rt::kBassDensity[st][4 + j][1] * 0.5f * g;
            c[j] = std::clamp ((int) std::lround (std::fabs (delta)), 0, 2);
            moved += c[j];
        }
        // The back half moves against the front (A&R CONSTANT_DENSITY): a
        // draw with no movement gets one bar of it.
        if (moved == 0)
            c[(int) (rd.nextU() % 3u)] = 1;
        for (int j = 0; j < 3; ++j)
        {
            if (busyLine)
                thin[4 + j] = c[j];
            else
                add[4 + j] += c[j];
        }
    }

    const float tick = 1.f / 120.f;
    for (int b = 0; b < nb; ++b)
    {
        for (int k = 0; k < add[b] && lane.count < kMaxNotes; ++k)
        {
            const int abs = bassFillSlot (used, b, nslots, tailCut, dialect, seed);
            if (abs < 0)
                break;
            Note n;
            n.start = (float) abs;
            n.nChord = 1;
            n.chord[0] = (std::int8_t) bassFillPitch (lane, abs, dialect, seed);
            float next = (float) nslots;
            int prev = -1;
            for (int i = 0; i < lane.count; ++i)
            {
                if (lane.notes[i].start > n.start + 1.0e-3f)
                    next = std::min (next, lane.notes[i].start);
                else if (lane.notes[i].start < n.start - 1.0e-3f)
                    prev = i;
            }
            if (n.start < tailCut)
                next = std::min (next, tailCut);
            n.length = std::max (0.5f, std::min (dialect == Pop ? 1.1f : 2.f, next - n.start - tick));
            n.vel = 96;
            n.flags = 0;
            n.timbre = prev >= 0 ? lane.notes[prev].timbre : (std::uint8_t) 0;
            if (prev >= 0 && lane.notes[prev].start + lane.notes[prev].length > n.start - tick)
                lane.notes[prev].length = std::max (0.25f, n.start - lane.notes[prev].start - tick);
            lane.notes[lane.count++] = n;
            used[abs] = true;
            sortByStart (lane);
        }
        for (int k = 0; k < thin[b]; ++k)
        {
            // The bar's latest attack goes, while the bar keeps more than three.
            int inBar = 0;
            int latest = -1;
            for (int i = 0; i < lane.count; ++i)
                if ((int) std::lround (lane.notes[i].start) / 16 == b)
                {
                    ++inBar;
                    latest = i;
                }
            if (inBar <= 3 || latest < 0)
                break;
            const int st = (int) std::lround (lane.notes[latest].start);
            if (st >= 0 && st < nslots)
                used[st] = false;
            for (int i = latest; i + 1 < lane.count; ++i)
                lane.notes[i] = lane.notes[i + 1];
            --lane.count;
        }
    }
    sortByStart (lane);
}

// A&R msg 423 held bass (JACKIN sounds 0.72 or more of the loop): when a line
// sounds less than `target`, the notes before the longest gaps grow toward 0.95
// of their gap, inside the long-note band (7% of notes may reach 6-8 sixteenths),
// until it gets there.
void fillBassSounding (Lane& lane, int bars, float target)
{
    const int n = std::min (lane.count, kMaxNotes);
    const float loop = (float) (std::min (bars, kMaxBars) * 16);
    if (n <= 0 || loop <= 0.f)
        return;
    sortByStart (lane);
    float total = 0.f;
    for (int i = 0; i < n; ++i)
        total += lane.notes[i].length;
    if (total >= target * loop)
        return;
    const float tailCut = bassTailCut (bars);
    int order[kMaxNotes];
    float gap[kMaxNotes];
    for (int i = 0; i < n; ++i)
    {
        order[i] = i;
        const float next = i + 1 < n ? lane.notes[i + 1].start : loop;
        gap[i] = next - lane.notes[i].start;
    }
    std::sort (order, order + n, [&] (int a, int b) { return gap[a] > gap[b]; });
    // The long-note band allows 7% of the notes at 6 sixteenths or more: the fill
    // may use what is left of that allowance (up to 8) before it stops at 5.9.
    int longLeft = (int) std::floor (0.07f * (float) n + 1.0e-4f);
    for (int i = 0; i < n; ++i)
        if (lane.notes[i].length >= 6.f - 1.0e-4f)
            --longLeft;
    for (int k = 0; k < n && total < target * loop; ++k)
    {
        Note& note = lane.notes[order[k]];
        if (note.start >= tailCut || pcOf (note.chord[0]) == 1)
            continue;
        const bool mayGoLong = longLeft > 0 && note.length < 6.f && 0.95f * gap[order[k]] >= 6.f;
        const float grow = std::min ({ 0.95f * gap[order[k]], mayGoLong ? 8.f : 5.9f, tailCut - note.start });
        if (mayGoLong && grow >= 6.f)
            --longLeft;
        if (grow > note.length)
        {
            total += grow - note.length;
            note.length = grow;
        }
    }
    // A line too sparse to get there by length alone: a short anticipation (the
    // next note's pitch, off the kick) goes in the widest remaining gap.
    for (int added = 0; added < 8 && total < target * loop && lane.count < kMaxNotes; ++added)
    {
        sortByStart (lane);
        int best = -1;
        float bestRoom = 0.f;
        for (int i = 0; i + 1 < lane.count; ++i)
        {
            const float end = lane.notes[i].start + lane.notes[i].length;
            const float room = lane.notes[i + 1].start - end;
            if (room > bestRoom && lane.notes[i + 1].start < tailCut)
            {
                bestRoom = room;
                best = i;
            }
        }
        if (best < 0 || bestRoom < 2.f)
            break;
        const float end = lane.notes[best].start + lane.notes[best].length;
        int slot = (int) std::ceil (end + 0.25f);
        if (kickSlot (slot % 16))
            ++slot;
        const float next = lane.notes[best + 1].start;
        if ((float) slot + 0.5f > next)
            break;
        Note ant = lane.notes[best + 1];
        ant.start = (float) slot;
        ant.length = std::min (5.9f, 0.95f * (next - (float) slot));
        ant.vel = (std::uint8_t) std::max (3, (int) ant.vel - 12);
        lane.notes[lane.count++] = ant;
        total += ant.length;
    }
    sortByStart (lane);
}

// A&R lane F3 / BASS_REESE_LONG (Maestro 655: the scorer's length bands win over
// the gap ratio): notes of 6 sixteenths or more stay at or under maxShare of the
// notes. The longest are shortened to 5.9 (just under 6) first.
void capLongBassNotes (Lane& lane, float maxShare)
{
    const int n = std::min (lane.count, kMaxNotes);
    if (n <= 0)
        return;
    int order[kMaxNotes];
    int nLong = 0;
    for (int i = 0; i < n; ++i)
        if (lane.notes[i].length >= 6.f - 1.0e-4f)
            order[nLong++] = i;
    const int allowed = (int) std::floor (maxShare * (float) n + 1.0e-4f);
    if (nLong <= allowed)
        return;
    std::sort (order, order + nLong, [&] (int a, int b)
    {
        if (lane.notes[a].length != lane.notes[b].length)
            return lane.notes[a].length > lane.notes[b].length;
        return a < b;
    });
    for (int k = 0; k < nLong - allowed; ++k)
        lane.notes[order[k]].length = 5.9f;
}

// A&R bounce lane: median note 0.9-1.4 sixteenths (BASS_NOTES_TOO_SHORT under
// 0.8). Every bounce note is at least 0.9 long where the next onset allows.
void floorBounceLengths (Lane& lane, int bars, float minLen)
{
    sortByStart (lane);
    const float loop = (float) (std::min (bars, kMaxBars) * 16);
    const float tick = 1.f / 120.f;
    for (int i = 0; i < lane.count; ++i)
    {
        Note& note = lane.notes[i];
        const float next = i + 1 < lane.count ? lane.notes[i + 1].start : loop;
        const float room = std::max (0.25f, next - note.start - tick);
        if (note.length < minLen)
            note.length = std::min (minLen, room);
    }
}

// The A&R soft limit (ONKICK_HIGH over 28% of bass attacks on the kick): attacks
// on kick steps move one 16th to a free off-kick slot in the same bar until the
// share is at or under maxShare. Keyed on the position in the 2-bar cycle, so
// repeats move together. Never into the last bar's turn rest.
void capBassOnKicks (Lane& lane, int bars, float maxShare, std::uint32_t seed)
{
    const int n = std::min (lane.count, kMaxNotes);
    const int nslots = std::min (bars, kMaxBars) * 16;
    if (n <= 1 || nslots <= 0)
        return;
    sortByStart (lane);
    const int allowed = (int) std::floor (maxShare * (float) n + 1.0e-4f);
    bool used[kMaxSteps] {};
    int onKick = 0;
    for (int i = 0; i < n; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st >= 0 && st < nslots)
            used[st] = true;
        if (st >= 0 && kickSlot (st % 16))
            ++onKick;
    }
    if (onKick <= allowed)
        return;
    const float tailCut = bassTailCut (bars);
    struct Move { int i; int q; std::uint32_t prio; };
    Move moves[kMaxNotes];
    int nm = 0;
    for (int i = 0; i < n && nm < kMaxNotes; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < 0 || st >= nslots || ! kickSlot (st % 16))
            continue;
        // Later first: the 16th after a kick keeps the push; then the one before,
        // then two 16ths either way.
        for (int dir : { 1, -1, 2, -2 })
        {
            const int q = st + dir;
            if (q < 0 || q >= nslots || q / 16 != st / 16 || used[q] || (float) q >= tailCut)
                continue;
            const float nextStart = i + 1 < n ? lane.notes[i + 1].start : 1.0e9f;
            const float prevEnd = i > 0 ? lane.notes[i - 1].start : -1.0e9f;
            if ((float) q >= nextStart || (float) q <= prevEnd)
                continue;
            moves[nm++] = { i, q, postHash (seed ^ 0x0FF1Cu, (std::uint32_t) ((st % 32) * 4 + (dir > 0 ? 1 : 0))) };
            break;
        }
    }
    std::sort (moves, moves + nm, [] (const Move& a, const Move& b)
    {
        if (a.prio != b.prio)
            return a.prio < b.prio;
        return a.i < b.i;
    });
    for (int m = 0; m < nm && onKick > allowed; ++m)
    {
        Note& note = lane.notes[moves[m].i];
        const int st = (int) std::lround (note.start);
        if (used[moves[m].q])
            continue;
        used[st] = false;
        used[moves[m].q] = true;
        const float shift = (float) (moves[m].q - st);
        note.start += shift;
        note.length = std::max (0.5f, note.length - std::max (0.f, shift));
        --onKick;
    }
    // No free slot left: drop kick-step notes, latest first, keeping the loop's
    // first note. A bounce line with the bass on every downbeat is already 20% on
    // the kick; the bass leaves some downbeats to the kick.
    for (int i = lane.count - 1; i > 0 && (float) onKick > maxShare * (float) lane.count + 1.0e-4f; --i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st <= 0 || ! kickSlot (st % 16))
            continue;
        for (int j = i; j + 1 < lane.count; ++j)
            lane.notes[j] = lane.notes[j + 1];
        --lane.count;
        --onKick;
    }
    sortByStart (lane);
}

// Moves bass attacks one 16th (then two) onto a free kick step in the same
// bar until the on-kick share reaches the style target, never above 35%.
// Only empty slots are used and a two-16th move needs the slot between free,
// so note order (and every interval / octave figure) is unchanged. The pick
// is keyed on the position in the 2-bar cycle, so repeats move together.
void placeBassOnKicks (Lane& lane, int dialect, int bars, std::uint32_t seed, float target = -1.f)
{
    const int n = std::min (lane.count, kMaxNotes);
    const int nslots = std::min (bars, kMaxBars) * 16;
    if (n <= 1 || nslots <= 0)
        return;
    sortByStart (lane);
    const int cap = (int) std::floor (0.35f * (float) n + 1.0e-4f);
    const int want = std::min (cap, (int) std::lround ((target >= 0.f ? target : bassKickTarget (dialect)) * (float) n));
    bool used[kMaxSteps] {};
    int onKick = 0;
    for (int i = 0; i < n; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st >= 0 && st < nslots)
            used[st] = true;
        if (st >= 0 && kickSlot (st % 16))
            ++onKick;
    }
    const float tailCut = bassTailCut (bars);

    struct Move { int i; int q; std::uint32_t prio; };
    Move moves[kMaxNotes * 2];
    bool moved[kMaxNotes] {};
    for (int dist = 1; dist <= 2 && onKick < want; ++dist)
    {
        int nm = 0;
        for (int i = 0; i < n; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st < 0 || st >= nslots || kickSlot (st % 16) || moved[i])
                continue;
            for (int dir = -1; dir <= 1; dir += 2)
            {
                const int q = st + dir * dist;
                if (q < 0 || q >= nslots || q / 16 != st / 16 || ! kickSlot (q % 16))
                    continue;
                if ((float) q >= tailCut || nm >= kMaxNotes * 2)
                    continue;
                const auto key = (std::uint32_t) ((st % 32) * 8 + dist * 2 + (dir > 0 ? 1 : 0));
                moves[nm].i = i;
                moves[nm].q = q;
                moves[nm].prio = postHash (seed ^ 0x4B1Cu, key);
                ++nm;
            }
        }
        std::sort (moves, moves + nm, [] (const Move& a, const Move& b)
        {
            if (a.prio != b.prio)
                return a.prio < b.prio;
            return a.i < b.i;
        });
        // JACKIN repeats a 2-bar hook: once a position moves, its other repeats
        // (same key, same priority) move too, as long as the 35% cap holds.
        const bool wholeGroups = dialect == Jackin;
        bool haveLast = false;
        std::uint32_t lastPrio = 0;
        for (int m = 0; m < nm; ++m)
        {
            const bool groupTail = wholeGroups && haveLast && moves[m].prio == lastPrio && onKick < cap;
            if (onKick >= want && ! groupTail)
                break;
            const int i = moves[m].i;
            const int q = moves[m].q;
            if (moved[i] || used[q])
                continue;
            const int st = (int) std::lround (lane.notes[i].start);
            if (dist == 2 && used[(st + q) / 2])
                continue;
            used[st] = false;
            used[q] = true;
            moved[i] = true;
            lane.notes[i].start = (float) q;
            ++onKick;
            haveLast = true;
            lastPrio = moves[m].prio;
        }
    }
    sortByStart (lane);
    // A note moved earlier can now run into the one before it.
    for (int i = 0; i + 1 < n; ++i)
    {
        const float room = lane.notes[i + 1].start - lane.notes[i].start;
        if (lane.notes[i].length > room)
            lane.notes[i].length = std::max (0.25f, room);
    }
}

float bassSoundingTotal (const Lane& lane) noexcept
{
    float total = 0.f;
    for (int i = 0; i < lane.count; ++i)
        total += std::max (0.f, lane.notes[i].length);
    return total;
}

// Shortens the longest notes (never under minLen) until the bass sounds for
// at most capFrac of the loop. Shorter notes never add a clash or an overlap.
void capBassSounding (Lane& lane, int bars, float capFrac, float minLen)
{
    const float loopEnd = (float) (std::min (bars, kMaxBars) * 16);
    if (lane.count <= 0 || loopEnd <= 0.f)
        return;
    const float cap = capFrac * loopEnd - 1.0e-3f;
    float total = bassSoundingTotal (lane);
    for (int guard = 0; guard < kMaxNotes * 8 && total > cap; ++guard)
    {
        int longest = -1;
        for (int i = 0; i < lane.count; ++i)
            if (lane.notes[i].length > minLen + 1.0e-4f
                && (longest < 0 || lane.notes[i].length > lane.notes[longest].length))
                longest = i;
        if (longest < 0)
            break;
        const float cut = std::min ({ total - cap, lane.notes[longest].length - minLen, 0.5f });
        lane.notes[longest].length -= cut;
        total -= cut;
    }
}

// Onsets per bar, population sd over the loop's bars (the scorer's npb_sd).
// A&R / model v1.2: every bass note sounds lo..hi of the gap to the next onset
// (the last note's gap wraps to the loop's first onset), never under half a 16th
// and never over eight. The final word on bass lengths after every other pass.
void guardGapRatio (Lane& lane, int bars, float lo, float hi)
{
    if (lane.count <= 0)
        return;
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    const float loop = (float) (std::min (bars, kMaxBars) * 16);
    for (int i = 0; i < lane.count; ++i)
    {
        const float next = i + 1 < lane.count ? lane.notes[i + 1].start : loop + lane.notes[0].start;
        const float gap = std::max (0.5f, next - lane.notes[i].start);
        // The last bar keeps the turn's rest: there a note is only ever shortened.
        const bool lastBar = lane.notes[i].start >= loop - 16.f;
        float len = std::min (lane.notes[i].length, hi * gap);
        if (! lastBar)
            len = std::max (len, lo * gap);
        else if (lane.notes[i].start < loop - 4.f)
            // Lengthened too, but never into the turn's rest (16th 13 on).
            len = std::max (len, std::min (lo * gap, loop - 4.f - lane.notes[i].start));
        len = std::clamp (len, 0.5f, 8.f);
        // Never past the next onset (one note per onset, no overlap).
        len = std::min (len, std::max (0.25f, gap - 1.f / 120.f));
        // Never past the loop end.
        len = std::min (len, std::max (0.25f, loop - lane.notes[i].start));
        lane.notes[i].length = len;
    }
}

float barDensitySpread (const Lane& lane, int bars) noexcept
{
    const int nb = clampi (bars, 1, kMaxBars);
    int count[kMaxBars] {};
    for (int i = 0; i < lane.count; ++i)
    {
        const int b = (int) std::lround (lane.notes[i].start) / 16;
        if (b >= 0 && b < nb)
            ++count[b];
    }
    double mean = 0.0;
    for (int b = 0; b < nb; ++b)
        mean += count[b];
    mean /= nb;
    double var = 0.0;
    for (int b = 0; b < nb; ++b)
        var += (count[b] - mean) * (count[b] - mean);
    return (float) std::sqrt (var / nb);
}

// BOUNCE keeps short notes: the line sounds for lo-hi of the loop. Notes grow
// toward 1.5 sixteenths (never into the next onset) or shrink toward 0.9.
void fitBounceSounding (Lane& lane, int bars, float lo, float hi, std::uint32_t seed)
{
    const int n = std::min (lane.count, kMaxNotes);
    const float loopEnd = (float) (std::min (bars, kMaxBars) * 16);
    if (n <= 0 || loopEnd <= 0.f)
        return;
    sortByStart (lane);
    const float tailCut = bassTailCut (bars);
    float total = bassSoundingTotal (lane);
    if (total > hi * loopEnd)
    {
        capBassSounding (lane, bars, hi, 0.9f);
        return;
    }
    int order[kMaxNotes];
    std::uint32_t prio[kMaxNotes] {};
    for (int i = 0; i < n; ++i)
    {
        order[i] = i;
        prio[i] = postHash (seed ^ 0xB0C3u, (std::uint32_t) i);
    }
    std::sort (order, order + n, [&] (int a, int b) { return prio[a] < prio[b]; });
    for (int k = 0; k < n && total < lo * loopEnd; ++k)
    {
        const int i = order[k];
        Note& note = lane.notes[i];
        float next = i + 1 < n ? lane.notes[i + 1].start : loopEnd;
        if (note.start < tailCut)
            next = std::min (next, tailCut);
        const float room = std::min (next, loopEnd) - note.start - 1.0f / 120.f;
        const float grow = std::min ({ 1.5f, room }) - note.length;
        if (grow <= 0.f)
            continue;
        const float add = std::min (grow, lo * loopEnd - total);
        note.length += add;
        total += add;
    }
}

// The BOUNCE density net: thin the body bar whose loss raises the per-bar
// onset sd most, one note at a time, until it reaches minSd. Never the last
// bar (turn / pickup), never a bar's first onset, never under 0.30 sounding.
// Removing a note adds no clash, overlap or duplicate, so this is safe after
// harmonizeRoll.
// A&R loop gate (rep2 0.50 / rep4 0.47) and back-half variation: each bar's front
// half (16ths 0-7) restates the bar `lag` bars before it, so the line comes back
// every `lag` bars and changes from bar to bar live in the back halves. Bar 4
// (the answer) and the last bar (the turn) keep their own front halves; bars set
// in wholeBars (0-based) restate the whole bar.
void restoreFrontHalves (Lane& lane, int bars, int lag, std::uint32_t wholeBars = 0)
{
    const int nb = std::min (bars, kMaxBars);
    if (nb <= lag || lane.count <= 0)
        return;
    sortByStart (lane);
    auto barOf = [] (const Note& n) { return (int) std::lround (n.start) / 16; };
    auto front = [] (const Note& n) { return (int) std::lround (n.start) % 16 < 8; };
    Note out[kMaxNotes];
    int w = 0;
    for (int b = 0; b < nb; ++b)
    {
        const bool own = b < lag || b == 3 || b == nb - 1;
        // Bars in wholeBars restate the whole bar, not only its front half.
        const bool whole = ! own && (wholeBars & (1u << (unsigned) b)) != 0;
        for (int i = 0; i < lane.count && w < kMaxNotes; ++i)
        {
            const Note& n = lane.notes[i];
            if (barOf (n) == b && (own || (! whole && ! front (n))))
                out[w++] = n;
            else if (! own && barOf (n) == b - lag && (whole || front (n)))
            {
                Note c = n;
                c.start += (float) (lag * 16);
                out[w++] = c;
            }
        }
    }
    lane.count = w;
    for (int i = 0; i < w; ++i)
        lane.notes[i] = out[i];
    sortByStart (lane);
    enforceMonophonic (lane);
}

// The A&R var_backhalf measure: of the 16ths where a bar differs from the bar
// before (attack pitch class, sustain or rest), the share in 16ths 8-15.
float backHalfShare (const Lane& lane, int bars) noexcept
{
    const int nb = std::min (bars, kMaxBars);
    const int L = nb * 16;
    if (nb < 2)
        return 1.f;
    int tok[kMaxSteps];
    for (int k = 0; k < L; ++k)
        tok[k] = -1; // rest
    for (int i = 0; i < lane.count; ++i)
    {
        const Note& n = lane.notes[i];
        if (n.vel <= 2)
            continue;
        const int st = (int) std::lround (n.start);
        const int en = std::min (L, (int) std::lround (n.start + n.length));
        if (st < 0 || st >= L)
            continue;
        for (int k = st + 1; k < en; ++k)
            if (tok[k] < 0)
                tok[k] = 12; // sustain
        tok[st] = pcOf (n.chord[0]);
    }
    int diff = 0, back = 0;
    for (int i = 16; i < L; ++i)
        if (tok[i] != tok[i - 16])
        {
            ++diff;
            if (i % 16 >= 8)
                ++back;
        }
    return diff > 0 ? (float) back / (float) diff : 1.f;
}

// A&R var_backhalf (changes from bar to bar mostly in the back half). Other
// styles: a bar whose front half differs from the bar before takes that bar's
// front half, most-changed first, until the share reaches `want`. JACKIN is left
// alone here (its bars must not repeat each other); its onsets lean into the back
// half when they are drawn.
void fixBackHalfVariation (Lane& lane, int bars, int dialect, float want)
{
    const int nb = std::min (bars, kMaxBars);
    if (nb < 4 || lane.count <= 0 || backHalfShare (lane, bars) >= want)
        return;
    sortByStart (lane);
    auto barOf = [] (const Note& n) { return (int) std::lround (n.start) / 16; };
    auto slotOf = [] (const Note& n) { return (int) std::lround (n.start) % 16; };
    // JACKIN's bars must not repeat each other (rep1 <= 0.1): its onsets lean
    // into the back half at composition instead (RuleComposer, kJackinBackLean).
    if (dialect == Jackin)
        return;
    for (int guard = 0; guard < nb && backHalfShare (lane, bars) < want; ++guard)
    {
        // The bar (2..last-1) whose front half differs most from the bar before.
        int bestBar = -1;
        int bestDiff = 0;
        for (int b = 2; b < nb - 1; ++b)
        {
            bool fa[8] {}, fb[8] {};
            int pa[8] {}, pb[8] {};
            for (int i = 0; i < lane.count; ++i)
            {
                const Note& n = lane.notes[i];
                const int sl = slotOf (n);
                if (sl >= 8)
                    continue;
                if (barOf (n) == b - 1) { fa[sl] = true; pa[sl] = pcOf (n.chord[0]); }
                if (barOf (n) == b) { fb[sl] = true; pb[sl] = pcOf (n.chord[0]); }
            }
            int d = 0;
            for (int k = 0; k < 8; ++k)
                if (fa[k] != fb[k] || (fa[k] && pa[k] != pb[k]))
                    ++d;
            if (d > bestDiff)
            {
                bestDiff = d;
                bestBar = b;
            }
        }
        if (bestBar < 0)
            break;
        Note out[kMaxNotes];
        int w = 0;
        for (int i = 0; i < lane.count && w < kMaxNotes; ++i)
        {
            const Note& n = lane.notes[i];
            if (barOf (n) == bestBar && slotOf (n) < 8)
                continue;
            out[w++] = n;
            if (barOf (n) == bestBar - 1 && slotOf (n) < 8 && w < kMaxNotes)
            {
                Note c = n;
                c.start += 16.f;
                out[w++] = c;
            }
        }
        lane.count = w;
        for (int i = 0; i < w; ++i)
            lane.notes[i] = out[i];
        sortByStart (lane);
        enforceMonophonic (lane);
    }
}

// BOUNCY bounce register: the export plays the BOUNCY bass from octave 2 (MIDI
// 36 + key), which suits pulse (A&R pulse lane 25-42) but puts every bounce line
// over the bounce lane's 25-36 median. A bounce line moves down an octave when
// that brings its exported median nearer 34; intervals and octave moves unchanged.
void lowerBounceRegister (Lane& lane, int key)
{
    const int n = std::min (lane.count, kMaxNotes);
    if (n <= 0)
        return;
    int semis[kMaxNotes];
    for (int i = 0; i < n; ++i)
        semis[i] = lane.notes[i].chord[0];
    std::sort (semis, semis + n);
    const int median = semis[n / 2];
    const int k = (key % 12 + 12) % 12;
    // The octave whose exported median lands nearest 34: inside both the A&R
    // bounce lane (25-36) and the Composer's bounce band (32-40) where possible.
    const int here = 36 + k + median;
    if (std::abs (here - 12 - 34) < std::abs (here - 34))
        for (int i = 0; i < n; ++i)
            lane.notes[i].chord[0] = (std::int8_t) (lane.notes[i].chord[0] - 12);
}

// BOUNCY bounce as a 2-bar cell: the A&R loop gate wants the line to come back
// every 2 bars (rep2 0.50 and up) and the Composer wants the bar densities to
// differ (onsets-per-bar spread 0.7 and up). Bars 1-2 are the cell, written so
// their note counts differ by two or more (spread 1.0 on their own); every later
// bar restates it, and only the back halves of bars 4 and 8 keep their own notes
// (the answer and the turn).
void loopBounceCell (Lane& lane, int bars, std::uint32_t seed)
{
    const int nb = std::min (bars, kMaxBars);
    if (nb < 4 || lane.count <= 0)
        return;
    sortByStart (lane);
    auto barOf = [] (const Note& n) { return (int) std::lround (n.start) / 16; };
    int count[2] {};
    for (int i = 0; i < lane.count; ++i)
        if (barOf (lane.notes[i]) < 2)
            ++count[barOf (lane.notes[i])];
    // Thin the sparser cell bar (never its first note) until the counts differ by 2.
    const int sparse = count[0] <= count[1] ? 0 : 1;
    for (int t = 0; t < 8 && std::abs (count[0] - count[1]) < 2 && count[sparse] > 2; ++t)
    {
        int cand[16];
        int nc = 0;
        bool first = true;
        for (int i = 0; i < lane.count && nc < 16; ++i)
        {
            if (barOf (lane.notes[i]) != sparse)
                continue;
            if (first)
            {
                first = false;
                continue;
            }
            cand[nc++] = i;
        }
        if (nc <= 0)
            break;
        const int drop = cand[postHash (seed ^ 0xCE11u, (std::uint32_t) t) % (std::uint32_t) nc];
        for (int i = drop; i + 1 < lane.count; ++i)
            lane.notes[i] = lane.notes[i + 1];
        --lane.count;
        --count[sparse];
    }
    Note out[kMaxNotes];
    int w = 0;
    for (int b = 0; b < nb; ++b)
    {
        const bool ownBack = b == 3 || b == nb - 1;
        for (int i = 0; i < lane.count && w < kMaxNotes; ++i)
        {
            const Note& n = lane.notes[i];
            const int nbar = barOf (n);
            const int slot = (int) std::lround (n.start) % 16;
            if (b < 2)
            {
                if (nbar == b)
                    out[w++] = n;
                continue;
            }
            if (ownBack && slot >= 8)
            {
                if (nbar == b)
                    out[w++] = n;
                continue;
            }
            if (nbar == (b & 1))
            {
                Note c = n;
                c.start += (float) ((b - (b & 1)) * 16);
                out[w++] = c;
            }
        }
    }
    lane.count = w;
    for (int i = 0; i < w; ++i)
        lane.notes[i] = out[i];
    sortByStart (lane);
    enforceMonophonic (lane);
}

void spreadBounceDensity (Lane& lane, int bars, float minSd, std::uint32_t seed)
{
    const int nb = std::min (bars, kMaxBars);
    const float loopEnd = (float) (nb * 16);
    if (nb < 4 || lane.count <= 0)
        return;
    for (int t = 0; t < 4 && barDensitySpread (lane, nb) < minSd; ++t)
    {
        sortByStart (lane);
        int count[kMaxBars] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const int b = (int) std::lround (lane.notes[i].start) / 16;
            if (b >= 0 && b < nb)
                ++count[b];
        }
        int bestBar = -1;
        double bestSd = -1.0;
        for (int b = 0; b < nb - 1; ++b)
        {
            if (count[b] < 3)
                continue;
            count[b] -= 1;
            double mean = 0.0;
            for (int k = 0; k < nb; ++k)
                mean += count[k];
            mean /= nb;
            double var = 0.0;
            for (int k = 0; k < nb; ++k)
                var += (count[k] - mean) * (count[k] - mean);
            count[b] += 1;
            const double sd = std::sqrt (var / nb);
            if (sd > bestSd + 1.0e-9)
            {
                bestSd = sd;
                bestBar = b;
            }
        }
        if (bestBar < 0)
            return;
        int cand[16];
        int nc = 0;
        bool first = true;
        for (int i = 0; i < lane.count && nc < 16; ++i)
        {
            if ((int) std::lround (lane.notes[i].start) / 16 != bestBar)
                continue;
            if (first)
            {
                first = false;
                continue;
            }
            cand[nc++] = i;
        }
        if (nc <= 0)
            return;
        const int drop = cand[postHash (seed ^ 0xD3A5u, (std::uint32_t) t) % (std::uint32_t) nc];
        if (bassSoundingTotal (lane) - lane.notes[drop].length < 0.30f * loopEnd)
            return;
        for (int i = drop; i + 1 < lane.count; ++i)
            lane.notes[i] = lane.notes[i + 1];
        --lane.count;
    }
}

// DEEP held bass: grow notes toward the next onset (seed order) until the line
// sounds for ceilFrac of the loop and no more (A&R MIDI gate: about 0.80, it
// was overshooting to 0.86-0.90); a line already above it is shortened.
// Repeated pitches keep a breath; nothing passes the turn rest.
void holdDeepBass (Lane& lane, int bars, std::uint32_t seed, float ceilFrac)
{
    const int n = std::min (lane.count, kMaxNotes);
    const float loopEnd = (float) (std::min (bars, kMaxBars) * 16);
    if (n <= 0 || loopEnd <= 0.f)
        return;
    sortByStart (lane);
    const float tailCut = bassTailCut (bars);
    float full[kMaxNotes];
    float total = 0.f;
    for (int i = 0; i < n; ++i)
    {
        Note& note = lane.notes[i];
        float next = i + 1 < n ? lane.notes[i + 1].start : loopEnd;
        if (note.start < tailCut)
            next = std::min (next, tailCut);
        next = std::min (next, loopEnd);
        const int slot = (int) std::lround (note.start) % 16;
        if (slot >= 14 && pcOf (note.chord[0]) == 3)
            next = std::min (next, (float) (((int) std::lround (note.start) / 16 + 1) * 16));
        const float room = std::max (0.f, next - note.start);
        float orig = std::min (note.length, std::max (0.25f, loopEnd - note.start));
        if (room >= 0.25f)
            orig = std::min (orig, room);
        note.length = orig;
        float f = room;
        if (i + 1 < n && lane.notes[i + 1].start < tailCut && lane.notes[i + 1].chord[0] == note.chord[0])
            f = room - (room >= 2.f ? 1.f : 0.5f);
        full[i] = note.start >= tailCut ? orig : std::max (orig, f);
        total += orig;
    }
    const float target = ceilFrac * loopEnd;
    if (total > target)
    {
        capBassSounding (lane, bars, ceilFrac, 1.0f);
        return;
    }
    int order[kMaxNotes];
    std::uint32_t prio[kMaxNotes] {};
    for (int i = 0; i < n; ++i)
    {
        order[i] = i;
        prio[i] = postHash (seed ^ 0x401Du, (std::uint32_t) i);
    }
    std::sort (order, order + n, [&] (int a, int b) { return prio[a] < prio[b]; });
    for (int k = 0; k < n && total < target; ++k)
    {
        const int i = order[k];
        const float add = std::min (full[i] - lane.notes[i].length, target - total);
        if (add <= 0.f)
            continue;
        lane.notes[i].length += add;
        total += add;
    }
}

// Holds every bass note up to the next onset (or the turn rest / loop end),
// so the line sounds for most of the loop. Some notes leave a one-16th
// breath, a repeated pitch always does (no tie into the next note), and the
// breaths stop once the sounding fraction would fall under the floor (0.78;
// BOUNCY pulse 0.88, which keeps it over 0.85 after the bar-8 pickup).
// Nothing overlaps and nothing passes the loop end. A&R MIDI gate: the held
// line (ROLLER, JACKIN, DEEP) sounds for about 0.80 of the loop, where it
// used to land at 0.86-0.90: more breaths first, then the longest notes give
// time back. A roll that takes the bar-8 pickup stops at 0.78, since the
// pickup adds up to three 16ths later. BOUNCY pulse keeps its own band and
// BOUNCE never comes here.
float bassHoldCeiling (std::uint32_t seed) noexcept
{
    return rollWantsPickup (seed) ? 0.78f : 0.80f;
}

void holdBassNotes (Lane& lane, int dialect, int bars, std::uint32_t seed)
{
    if (dialect == Long)
    {
        holdDeepBass (lane, bars, seed, bassHoldCeiling (seed));
        return;
    }
    const int n = std::min (lane.count, kMaxNotes);
    const float loopEnd = (float) (std::min (bars, kMaxBars) * 16);
    if (n <= 0 || loopEnd <= 0.f)
        return;
    sortByStart (lane);
    const float tailCut = bassTailCut (bars);
    const bool pulse = dialect == Pop;

    float room[kMaxNotes];
    float len[kMaxNotes];
    bool forced[kMaxNotes] {};
    for (int i = 0; i < n; ++i)
    {
        const Note& note = lane.notes[i];
        float next = i + 1 < n ? lane.notes[i + 1].start : loopEnd;
        if (note.start < tailCut)
            next = std::min (next, tailCut);
        next = std::min (next, loopEnd);
        // A b3 late in the bar held over the barline is a banned figure (AV14).
        const int slot = (int) std::lround (note.start) % 16;
        if (slot >= 14 && pcOf (note.chord[0]) == 3)
            next = std::min (next, (float) (((int) std::lround (note.start) / 16 + 1) * 16));
        room[i] = std::max (0.f, next - note.start);
        const float orig = std::min (note.length, std::max (0.25f, loopEnd - note.start));
        if (note.start >= tailCut)
        {
            // Inside the turn rest: leave it as written.
            len[i] = orig;
            room[i] = -1.f;
            continue;
        }
        if (room[i] <= orig)
        {
            // No space to grow (or it already fills the space): keep it, clipped.
            len[i] = room[i] >= 0.25f ? std::min (orig, room[i]) : orig;
            room[i] = -1.f;
            continue;
        }
        len[i] = room[i];
        const bool samePitch = i + 1 < n && lane.notes[i + 1].start < tailCut
                               && lane.notes[i + 1].chord[0] == note.chord[0];
        if (samePitch)
        {
            forced[i] = true;
            // Pulse re-strikes a repeat after a short gap so it stays held.
            const float gap = pulse ? 0.25f : (room[i] >= 2.f ? 1.f : 0.5f);
            len[i] = std::max (orig, room[i] - gap);
        }
    }

    float total = 0.f;
    for (int i = 0; i < n; ++i)
        total += len[i];
    const float floorTotal = (pulse ? 0.88f : 0.78f) * loopEnd;

    int cand[kMaxNotes] {};
    std::uint32_t prio[kMaxNotes] {};
    int nc = 0;
    for (int i = 0; i < n; ++i)
    {
        if (room[i] < 2.f || forced[i])
            continue;
        cand[nc] = i;
        // JACKIN's repeats breathe in the same places: keyed on the 2-bar position.
        const std::uint32_t key = dialect == Jackin
                                      ? 0x10000u + (std::uint32_t) (((int) std::lround (lane.notes[i].start)) % 32)
                                      : (std::uint32_t) i;
        prio[i] = postHash (seed ^ 0x401Du, key);
        ++nc;
    }
    std::sort (cand, cand + nc, [&] (int a, int b) { return prio[a] != prio[b] ? prio[a] < prio[b] : a < b; });
    const int wantBreaths = (int) std::lround (bassBreathShare (dialect) * (float) nc);
    for (int k = 0; k < wantBreaths && k < nc; ++k)
    {
        const int i = cand[k];
        const float orig = lane.notes[i].length;
        const float shorter = std::max (orig, room[i] - 1.f);
        const float cost = len[i] - shorter;
        if (total - cost < floorTotal)
            continue;
        total -= cost;
        len[i] = shorter;
    }
    const float ceilFrac = bassHoldCeiling (seed);
    if (! pulse)
    {
        // Past the floor's breaths: keep breathing (seed order) down to the ceiling.
        for (int k = 0; k < nc && total > ceilFrac * loopEnd; ++k)
        {
            const int i = cand[k];
            const float shorter = std::max (lane.notes[i].length, room[i] - 1.f);
            const float cost = len[i] - shorter;
            if (cost <= 0.f)
                continue;
            total -= cost;
            len[i] = shorter;
        }
    }

    for (int i = 0; i < n; ++i)
        lane.notes[i].length = len[i];
    // BOUNCY pulse tops out at 0.975 (0.95 here: the pickup can add a little).
    if (pulse)
        capBassSounding (lane, bars, 0.95f, 1.0f);
    else
        capBassSounding (lane, bars, ceilFrac, 1.0f);
}

// BOUNCY pulse: octave moves stay at or under maxShare of the moves (Composer:
// 8%). makePulse writes none, but the late passes can (the clash fixer keeps
// the register, so 10 -> root lands on 12 next to a 0; the pickup). Such a
// note takes its neighbour's register instead: same pitch class, so no new
// clash, b2 or mode change. A same-pitch neighbour ends one tick before it.
void capPulseOctaves (Lane& lane, float maxShare)
{
    sortByStart (lane);
    const int moves = lane.count - 1;
    if (moves <= 0)
        return;
    const float tick = 1.f / 120.f;
    auto isOct = [&] (int i)
    {
        const int d = adiff (lane.notes[i].chord[0], lane.notes[i - 1].chord[0]);
        return d == 12 || d == 24;
    };
    auto breathe = [&] (int a, int b)
    {
        Note& p = lane.notes[a];
        if (p.chord[0] == lane.notes[b].chord[0] && p.start + p.length > lane.notes[b].start - tick)
            p.length = std::max (tick, lane.notes[b].start - p.start - tick);
    };
    int oct = 0;
    for (int i = 1; i < lane.count; ++i)
        if (isOct (i))
            ++oct;
    for (int i = 1; i < lane.count && (float) oct > maxShare * (float) moves + 1.0e-4f; ++i)
    {
        if (! isOct (i))
            continue;
        const bool nextWas = i + 1 < lane.count && isOct (i + 1);
        lane.notes[i].chord[0] = lane.notes[i - 1].chord[0];
        const bool nextNow = i + 1 < lane.count && isOct (i + 1);
        oct += -1 - (nextWas ? 1 : 0) + (nextNow ? 1 : 0);
        breathe (i - 1, i);
        if (i + 1 < lane.count)
            breathe (i, i + 1);
    }
}

// BOUNCE mean note length stays inside lo-hi sixteenths (Composer: 0.8-1.6).
// Too long: the longest notes shorten (never under 0.9, never under 0.30 of
// the loop sounding). Too short: notes grow toward the next onset (at most
// 1.5, never into the turn rest, never past 0.49 sounding), and only where
// the longer note meets no stab/pad semitone. No clash or overlap comes back.
void fitBounceMeanLength (Groove& g, int bars, float lo, float hi, std::uint32_t seed)
{
    Lane& lane = g.lane[Bass];
    const int n = std::min (lane.count, kMaxNotes);
    const float loopEnd = (float) (std::min (bars, kMaxBars) * 16);
    if (n <= 0 || loopEnd <= 0.f)
        return;
    sortByStart (lane);
    float total = bassSoundingTotal (lane);
    if (total > hi * (float) n)
    {
        capBassSounding (lane, bars, std::max (0.30f, hi * (float) n / loopEnd), 0.9f);
        return;
    }
    const float want = lo * (float) n;
    const float ceiling = 0.49f * loopEnd;
    if (total >= want)
        return;
    const float tailCut = bassTailCut (bars);
    int order[kMaxNotes];
    std::uint32_t prio[kMaxNotes] {};
    for (int i = 0; i < n; ++i)
    {
        order[i] = i;
        prio[i] = postHash (seed ^ 0xB0C5u, (std::uint32_t) i);
    }
    std::sort (order, order + n, [&] (int a, int b) { return prio[a] < prio[b]; });
    for (int k = 0; k < n && total < want; ++k)
    {
        const int i = order[k];
        Note& note = lane.notes[i];
        float next = i + 1 < n ? lane.notes[i + 1].start : loopEnd;
        if (note.start < tailCut)
            next = std::min (next, tailCut);
        const float room = std::min (next, loopEnd) - note.start - 1.0f / 120.f;
        const float add = std::min ({ std::min (1.5f, room) - note.length, want - total, ceiling - total });
        if (add <= 0.f)
            continue;
        Note longer = note;
        longer.length += add;
        if ((clashMask (upperPcsOver (g, longer)) & (1u << pcOf (note.chord[0]))) != 0)
            continue;
        note.length = longer.length;
        total += add;
    }
}

// 0 not BOUNCY, 1 bounce, 2 pulse. `modeSeed` is the caller's drums seed (the
// roll's batch position), so the pulse share is a quota over a batch.
int bouncyModeOf (const Settings& s, std::uint32_t modeSeed) noexcept
{
    if (s.dialect != Pop)
        return BassModeNone;
    return popIsPulse (s.bounce, modeSeed ? modeSeed : 1u) ? BassModePulse : BassModeBounce;
}

} // namespace

// Genre model v1.2: P 0.25 per roll and at least 15% of any batch. The draw is
// the position of 3 x the bass seed in [0, 2^32): over a batch's golden-ratio
// seed walk that position is spread evenly (step 3 x 0.618 mod 1), so every
// 20-roll batch carries 5 +- 1 pickups instead of a binomial count. The window
// [0.60, 0.85) keeps it apart from the BOUNCY pulse window.
bool rollWantsPickup (std::uint32_t bassSeed) noexcept
{
    const std::uint32_t s = (bassSeed ? bassSeed : 1u) * 3u;
    const auto pos = (std::uint32_t) (((std::uint64_t) s * 100u) >> 32);
    const auto width = (std::uint32_t) std::lround (rules::tables::kPickupProb * 100.f);
    return pos >= 60u && pos < 60u + width;
}

int rollMode (const Settings& settings, std::uint32_t bassSeed) noexcept
{
    // One mode per roll from the style's model weights (JACKIN aeolian or phrygian).
    return rules::drawMode (clampi (settings.dialect, 0, NumDialects - 1), settings.dark, bassSeed);
}

// One roll from these exact seeds, no originality check (generate() wraps it).
// modeSeed: the caller's drums seed, for the batch-quota BOUNCY mode.
static void generateOnce (Groove& g, const Settings& settings, const std::uint32_t seeds[NumParts], std::uint32_t partMask,
                          std::uint32_t modeSeed)
{
    Settings s = settings;
    s.bars = clampi (s.bars, 2, kMaxBars);
    if (s.bars != 2 && s.bars != 4 && s.bars != 8)
        s.bars = 8;
    s.dialect = clampi (s.dialect, 0, NumDialects - 1);
    s.key = (s.key % 12 + 12) % 12;
    s.dark = std::clamp (s.dark, 0.f, 1.f);
    s.busy = std::clamp (s.busy, 0.f, 1.f);
    s.bounce = std::clamp (s.bounce, 0.f, 1.f);
    s.stab = std::clamp (s.stab, 0.f, 1.f);
    s.styleIntensity = std::clamp (s.styleIntensity, 0.f, 1.f);
    s.freedom = std::clamp (s.freedom, 0.f, 1.f);
    s.scale = clampi (s.scale, 0, NumScales - 1);

    auto regen = [&] (int part) { return (partMask & (1u << part)) != 0; };

    Lane kept[NumParts];
    DrumLane keptDrums = g.drums;
    for (int p = 0; p < NumParts; ++p)
        kept[p] = g.lane[p];

    g.bars = s.bars;
    g.key = s.key;
    g.dialect = s.dialect;

    const std::uint32_t bassSeed = seeds[Bass] ? seeds[Bass] : 1u;
    // One mode per roll for every pitched lane (harmonizeRoll snaps to it again).
    // Groove::mode records it for the roll that ships.
    const int mode = rollMode (s, bassSeed);
    tRoll.dialect = s.dialect;
    tRoll.mode = mode;
    // BOUNCY plays one of two modes per roll: bounce, or pulse on a 35% batch quota.
    const int popMode = bouncyModeOf (s, modeSeed);
    // The composer's submode: density and length targets of the roll.
    const int bassSub = s.dialect == Pop ? (popMode == BassModePulse ? rules::SubPulse : rules::SubBounce)
                      : s.dialect == Long ? (deepIsDark (s.dark, bassSeed) ? rules::SubDeepDark : rules::SubDeepSparse)
                                          : rules::SubNone;
    Rng freedomRng (bassSeed ^ 0xC0FFEEu);
    const float freedom = effectiveFreedom (s, freedomRng);
    Rng pickRng (bassSeed ^ 0xA11CEu);

    struct Cand
    {
        Groove g;
        float penalty = 0.f;
        char tag[192] = "";
    };
    Cand pool[6];
    int nPool = 0;
    int draws = 0;
    int hardDraws = 0;
    Cand last {};
    bool haveLast = false;

    for (int attempt = 0; attempt < 24 && nPool < 6; ++attempt)
    {
        for (int p = 0; p < NumParts; ++p)
            if (! regen (p))
                g.lane[p] = kept[p];
        if (! regen (Drums))
            g.drums = keptDrums;

        // Each candidate's bass is composed fresh by the rule composer from its
        // own seed (attempt 0: the caller's bass seed). The tag records the
        // density arc the composer drew: arc|markov|cells|soft breaks.
        const char* planName = rules::tables::kArcNames[0];
        const char* contour = "markov";
        char cells[64] = "call_answer,turn_rest";
        char breaks[48] = "";
        const bool offPlan = false;
        const bool offContour = false;

        if (regen (Bass))
        {
            const std::uint32_t composeSeed = attempt == 0 ? bassSeed
                                                           : (postHash (bassSeed ^ 0xCA4Du, (std::uint32_t) attempt) | 1u);
            rules::BassParams bp;
            bp.dialect = s.dialect;
            bp.bars = s.bars;
            bp.mode = mode;
            bp.dark = s.dark;
            bp.sub = bassSub;
            const int arc = rules::composeBass (g.lane[Bass], bp, composeSeed);
            planName = rules::tables::kArcNames[clampi (arc, 0, rules::tables::kArcShapes - 1)];
            g.lane[Bass].seed = bassSeed;
        }
        else
            clampLane (g.lane[Bass], s.bars);

        if (regen (Stab))
        {
            g.lane[Stab].seed = seeds[Stab] ? seeds[Stab] : 1u;
            writeStab (g.lane[Stab], g.lane[Bass], s, mode);
        }
        else
            clampLane (g.lane[Stab], s.bars);

        if (regen (Pluck))
        {
            Rng rp (seeds[Pluck] ? seeds[Pluck] : 1u);
            g.lane[Pluck].seed = rp.s;
            writePluck (g.lane[Pluck], s, rp, mode);
        }
        else
            clampLane (g.lane[Pluck], s.bars);

        if (regen (Pad))
        {
            Rng rpad (seeds[Pad] ? seeds[Pad] : 1u);
            g.lane[Pad].seed = rpad.s;
            writePad (g.lane[Pad], s, rpad, mode);
        }
        else
            clampLane (g.lane[Pad], s.bars);

        if (regen (Vox))
        {
            Rng rv (seeds[Vox] ? seeds[Vox] : 1u);
            g.lane[Vox].seed = rv.s;
            writeVox (g.lane[Vox], s, rv);
        }
        else
            clampLane (g.lane[Vox], s.bars);

        if (regen (Hits))
        {
            Rng rh (seeds[Hits] ? seeds[Hits] : 1u);
            g.lane[Hits].seed = rh.s;
            writeHits (g.lane[Hits], g, s, rh, mode);
        }
        else
            clampLane (g.lane[Hits], s.bars);

        if (regen (Drums))
        {
            Rng rd (seeds[Drums] ? seeds[Drums] : 1u);
            writeDrums (g.drums, seeds[Drums], s, rd);
        }

        ++draws;
        std::snprintf (g.brokenRules, sizeof g.brokenRules, "%s|%s|%s|%s", planName, contour, cells, breaks);
        last.g = g;
        last.penalty = softPenalty (g, freedom, planName, offPlan, offContour)
                     + (regen (Bass) ? darkPull (g.lane[Bass], s.dark) : 0.f);
        std::snprintf (last.tag, sizeof last.tag, "%s", g.brokenRules);
        haveLast = true;
        if (hardFail (g))
        {
            ++hardDraws;
            continue;
        }
        pool[nPool].g = g;
        pool[nPool].penalty = last.penalty;
        std::snprintf (pool[nPool].tag, sizeof pool[nPool].tag, "%s", g.brokenRules);
        ++nPool;
    }

    int chosen = 0;
    bool shippedHard = nPool == 0;
    if (nPool == 0 && haveLast)
    {
        pool[0] = last;
        nPool = 1;
    }
    if (nPool > 1)
    {
        int order[6];
        for (int i = 0; i < nPool; ++i)
            order[i] = i;
        std::sort (order, order + nPool, [&] (int a, int b) { return pool[a].penalty < pool[b].penalty; });
        const float temperature = 1.f + 4.f * freedom;
        const int topN = std::min (nPool, std::max (2, (int) std::lround (2.f + 3.f * freedom)));
        double weight[6] {};
        double sum = 0.0;
        const float bestP = pool[order[0]].penalty;
        for (int i = 0; i < topN; ++i)
        {
            weight[i] = std::exp (-(double) (pool[order[i]].penalty - bestP) / (double) temperature);
            sum += weight[i];
        }
        double needle = pickRng.next() * sum;
        chosen = order[0];
        for (int i = 0; i < topN; ++i)
        {
            needle -= weight[i];
            if (needle <= 0.0)
            {
                chosen = order[i];
                break;
            }
        }
    }

    if (nPool > 0)
        g = pool[chosen].g;
    g.bars = s.bars;
    g.key = s.key;
    g.dialect = s.dialect;
    g.draws = draws;
    g.hardDraws = hardDraws;
    g.penalty = nPool > 0 ? pool[chosen].penalty : 0.f;
    if (nPool > 0)
        std::snprintf (g.brokenRules, sizeof g.brokenRules, "%s", pool[chosen].tag);
    if (regen (Bass))
        g.bassMode = popMode;
    else if (s.dialect != Pop)
        g.bassMode = BassModeNone;
    if (regen (Bass))
    {
        // The chosen candidate's composed line gets the rule passes. BOUNCE
        // needs bar-to-bar density movement (onset sd >= 0.7): further lines are
        // composed from derived seeds, at most six in all; the best one ships.
        const Lane drawn = g.lane[Bass];
        Lane best = drawn;
        float bestSd = -1.f;
        const int phraseDraws = popMode == BassModeBounce ? 6 : 1;
        for (int k = 0; k < phraseDraws; ++k)
        {
            g.lane[Bass] = drawn;
            const std::uint32_t phraseSeed = k == 0 ? bassSeed : (postHash (bassSeed ^ 0xB0B5u, (std::uint32_t) k) | 1u);
            if (k > 0)
            {
                rules::BassParams bp;
                bp.dialect = s.dialect;
                bp.bars = s.bars;
                bp.mode = mode;
                bp.dark = s.dark;
                bp.sub = bassSub;
                rules::composeBass (g.lane[Bass], bp, phraseSeed);
                g.lane[Bass].seed = bassSeed;
            }
            finishPhrase (g.lane[Bass], s, phraseSeed, popMode == BassModePulse, mode);
            if (phraseDraws == 1)
                break;
            const float sd = barDensitySpread (g.lane[Bass], s.bars);
            if (sd > bestSd)
            {
                bestSd = sd;
                best = g.lane[Bass];
            }
            if (sd >= 0.8f)
                break;
        }
        if (phraseDraws > 1)
            g.lane[Bass] = best;
    }
    if (regen (Bass))
    {
        enforceMonophonic (g.lane[Bass]);
        avoidRootTwoFour (g.lane[Bass]);
        if (s.dialect == Pop)
        {
            ensureBar8Turn (g.lane[Bass], s.bars, bassSeed);
            anchorPopRoot (g.lane[Bass], bassSeed);
        }
        else if (s.dialect == Jackin)
        {
            contrastCallAnswer (g.lane[Bass], s.bars);
            enforceMonophonic (g.lane[Bass]);
            contrastCallAnswer (g.lane[Bass], s.bars);
            // The loop repeats on the bar grid: A B A B4 A B A B8 from one bar pair.
            loopJackinBass (g.lane[Bass], s.bars, bassSeed);
        }
        enforceMonophonic (g.lane[Bass]);
        avoidRootTwoFour (g.lane[Bass]);
    }
    // A&R msg 423 #1 and #2: some bass on the kick, then hold the notes. Runs
    // before the stab rewrite so stabs see the bass that ships. BOUNCE keeps
    // its short notes (no hold); only BOUNCY pulse gets the held line. The
    // A&R MIDI gate's density fill comes first (pulse keeps its own grid).
    if (regen (Bass))
    {
        if (popMode != BassModePulse)
            fillBassDensity (g.lane[Bass], s.dialect, s.bars, s.busy, bassSeed);
        // Maestro 655: at most 28% of bass attacks on the kick in every style
        // (the A&R soft limit). BOUNCY sits at 20-28% over the batch: the A&R
        // bounce lane wants 85% off the kick, so bounce keeps the kick for the
        // pulse rolls (28%) and bounce stays at 15% or under.
        const float kickTarget = popMode == BassModePulse ? 0.28f : (popMode == BassModeBounce ? 0.14f : -1.f);
        placeBassOnKicks (g.lane[Bass], s.dialect, s.bars, bassSeed, kickTarget);
        capBassOnKicks (g.lane[Bass], s.bars, popMode == BassModeBounce ? 0.15f : 0.28f, bassSeed);
        if (popMode == BassModeBounce)
            fitBounceSounding (g.lane[Bass], s.bars, 0.33f, 0.46f, bassSeed);
        else
            holdBassNotes (g.lane[Bass], s.dialect, s.bars, bassSeed);
        enforceMonophonic (g.lane[Bass]);
    }
    // Stabs are rewritten on the bass that ships, in its gaps and over its
    // sustains by the style's shares (RuleTables kStabGapShare).
    if (regen (Stab))
    {
        const std::uint32_t stabSeed = g.lane[Stab].seed ? g.lane[Stab].seed : 1u;
        g.lane[Stab].seed = stabSeed;
        writeStab (g.lane[Stab], g.lane[Bass], s, mode);
        capStabHits (g.lane[Stab], s);
    }
    // IN STAB follows the stab that ships, so the hits are rewritten on it.
    if (s.hitsInStab && regen (Hits))
    {
        Rng rh (seeds[Hits] ? seeds[Hits] : 1u);
        g.lane[Hits].seed = rh.s;
        writeHits (g.lane[Hits], g, s, rh, mode);
    }
    for (int part = 0; part < Drums; ++part)
        dedupeNoteOns (g.lane[part]);
    enforceMonophonic (g.lane[Bass]);
    harmonizeRoll (g, s, bassSeed, partMask);
    // The bar-8 pickup can move the last bar: re-check the bands. Both passes
    // only shorten or drop notes, so no clash or overlap comes back.
    if (regen (Bass))
    {
        if (popMode == BassModeBounce)
        {
            loopBounceCell (g.lane[Bass], s.bars, bassSeed);
            spreadBounceDensity (g.lane[Bass], s.bars, 0.7f, bassSeed);
            // The restated cell carries bars 1-2's kick share and sounding to every
            // bar: both are checked again on the line that ships.
            capBassOnKicks (g.lane[Bass], s.bars, 0.15f, bassSeed);
            fitBounceSounding (g.lane[Bass], s.bars, 0.33f, 0.46f, bassSeed);
            capBassSounding (g.lane[Bass], s.bars, 0.49f, 0.9f);
            // A&R / model v1.2: bounce mean note length 0.9-1.4 sixteenths (its own
            // band; the gap ratio is for the other modes).
            fitBounceMeanLength (g, s.bars, 0.9f, 1.4f, bassSeed);
            floorBounceLengths (g.lane[Bass], s.bars, 0.9f);
            // A&R lane F7: octave moves 15-26% of the moves (OCTAVE_PUMP over 26).
            capPulseOctaves (g.lane[Bass], 0.25f);
            lowerBounceRegister (g.lane[Bass], s.key);
            if (backHalfShare (g.lane[Bass], s.bars) < 0.52f)
                fixBackHalfVariation (g.lane[Bass], s.bars, s.dialect, 0.52f);
        }
        else if (popMode == BassModePulse)
        {
            // Composer: pulse octave moves at most 8%.
            capPulseOctaves (g.lane[Bass], 0.08f);
            // Pulse sounds about 0.93 of each gap.
            guardGapRatio (g.lane[Bass], s.bars, 0.90f, 0.95f);
            capLongBassNotes (g.lane[Bass], 0.07f);
        }
        else
        {
            // ROLLER: bars 3 and 5 restate bar 1 whole (the A comes back); the
            // answer (bar 4), bars 6-7's back halves and the turn carry the change.
            if (s.dialect == UkMin)
                restoreFrontHalves (g.lane[Bass], s.bars, 2, (1u << 2) | (1u << 4));
            // DEEP: front halves only; its density movement stays in the bars.
            else if (s.dialect == Long)
            {
                restoreFrontHalves (g.lane[Bass], s.bars, 2);
                // The restated fronts change the gaps: DEEP's held line is filled again.
                holdDeepBass (g.lane[Bass], s.bars, bassSeed, 0.80f);
            }
            // A&R lane F7: octave moves stay at or under 24% (OCTAVE_PUMP over 26).
            capPulseOctaves (g.lane[Bass], 0.24f);
            // Model v1.2: 0.79-0.83 of the gap to the next note.
            guardGapRatio (g.lane[Bass], s.bars, 0.79f, 0.83f);
            // A&R var_backhalf, read on the final lengths; the lengths are fitted again.
            if (backHalfShare (g.lane[Bass], s.bars) < 0.52f)
            {
                fixBackHalfVariation (g.lane[Bass], s.bars, s.dialect, 0.52f);
                guardGapRatio (g.lane[Bass], s.bars, 0.79f, 0.83f);
            }
            // Maestro 655: the scorer's length bands win; DEEP keeps its held notes.
            if (s.dialect != Long)
                capLongBassNotes (g.lane[Bass], 0.07f);
        }
        // The length guard changes how long each pitch sounds: the b2 cap (and
        // the mode snap) are checked again on the final lengths.
        conformBass (g.lane[Bass], mode, b2Cap (s.dialect, mode));
        // JACKIN's held line (A&R 423) on the final pitches; b2 notes never grow.
        if (s.dialect == Jackin)
            fillBassSounding (g.lane[Bass], s.bars, 0.74f);
        // The final bass lengths can ring under upper notes they used to stop
        // before: every upper lane gives way to the bass that ships again.
        for (int part : { (int) Stab, (int) Pad, (int) Pluck, (int) Hits, (int) Vox })
        {
            fixUpperClashes (g, part);
            dedupeNoteOns (g.lane[part]);
            trimSamePitchOverlaps (g.lane[part]);
        }
    }
        // A locked scale is the last pitch step, after the roll-mode pass, so the
    // user's scale always wins and Auto rolls are untouched byte for byte. Only
    // regenerated lanes move: a kept lane stays as the user locked it.
    if (s.scale != ScaleAuto)
    {
        applyScale (g, s.scale, partMask);
        for (int part = 0; part < Drums; ++part)
        {
            dedupeNoteOns (g.lane[part]);
            trimSamePitchOverlaps (g.lane[part]);
        }
        enforceMonophonic (g.lane[Bass]);
    }
    // BOUNCY bounce: the A&R bounce lane wants 85% of the bass off the kick. The
    // last bass passes (pickup, turn, density) can land on kick steps, so the cap
    // is the final bass step; the upper lanes then give way to that bass.
    if (regen (Bass) && popMode == BassModeBounce)
    {
        capBassOnKicks (g.lane[Bass], s.bars, 0.15f, bassSeed);
        giveWayToBass (g, ~partMask);
    }
    g.mode = mode;
    g.guardScore = shippedHard ? 0.f : 1.f;
    g.guardLevel = shippedHard ? 2 : 0;
    std::snprintf (g.guardRef, sizeof g.guardRef, "%s", shippedHard ? "HARD" : "ORIGINAL");
    ++g.generation;
}

// One composition from `base`: up to kOrigAttempts rolls. A lane that hits the
// originality check is redrawn from its base seed + k, k counting that lane's
// redraws; a lane other than the bass that hits three times also gets a new
// bass (it sits in the bass's holes). True when a roll clears; g then holds it.
// Redraw bookkeeping across the compositions of one generate() call.
struct RedrawTally
{
    int orig = 0;
    int guard = 0;
    int logCount = 0;
    Groove::RedrawEntry log[Groove::kMaxRedrawLog] {};

    int total() const noexcept { return orig + guard; }

    void add (int lane, int metric, float score, const char* ref) noexcept
    {
        if (logCount >= Groove::kMaxRedrawLog)
            return;
        auto& e = log[logCount++];
        e.lane = (std::int8_t) clampi (lane, -2, NumParts - 1);
        e.metric = (std::int8_t) clampi (metric, 0, 127);
        e.score = score;
        std::snprintf (e.ref, sizeof e.ref, "%s", ref != nullptr ? ref : "");
    }

    void writeTo (Groove& g) const noexcept
    {
        g.origRedraws = orig;
        g.guardRedraws = guard;
        g.redrawLogCount = logCount;
        for (int i = 0; i < Groove::kMaxRedrawLog; ++i)
            g.redrawLog[i] = log[i];
    }
};

// Loop repeat of a bass line at `lag` 16ths: per 16th the attack's pitch class,
// rests and sustains as one token, pairs where both are rests skipped (the A&R
// scorer's F6 measure).
float bassRepeat (const Lane& lane, int bars, int lag)
{
    const int L = std::min (bars, kMaxBars) * 16;
    if (L <= lag)
        return -1.f;
    int tok[kMaxSteps];
    for (int i = 0; i < L; ++i)
        tok[i] = -1;
    for (int i = 0; i < lane.count; ++i)
    {
        const int a = (int) std::lround (lane.notes[i].start);
        if (a < 0 || a >= L)
            continue;
        tok[a] = ((lane.notes[i].chord[0] % 12) + 12) % 12;
    }
    int pairs = 0;
    int same = 0;
    for (int i = 0; i + lag < L; ++i)
    {
        if (tok[i] == -1 && tok[i + lag] == -1)
            continue;
        ++pairs;
        if (tok[i] == tok[i + lag])
            ++same;
    }
    return pairs > 0 ? (float) same / (float) pairs : -1.f;
}

// Model v1.2 loop identity: JACKIN repeats on the 2-bar grid (>= kRepeatM2Min)
// and not on the 1-bar grid (<= kRepeatM1Max). A line outside it is recomposed.
bool jackinRepeatOk (const Lane& lane, int bars)
{
    if (bars < 4)
        return true;
    const int st = std::clamp ((int) Jackin, 0, rules::tables::kStyles - 1);
    const float r1 = bassRepeat (lane, bars, 16);
    const float r2 = bassRepeat (lane, bars, 32);
    return r1 <= rules::tables::kRepeatM1Max[st] + 1.0e-4f && (r2 < 0.f || r2 >= rules::tables::kRepeatM2Min[st] - 1.0e-4f);
}

static bool composeOriginal (Groove& g, const Groove& input, const Settings& settings,
                             const std::uint32_t base[NumParts], std::uint32_t partMask,
                             const orig::Index* external, RedrawTally& tally, std::uint32_t modeSeed)
{
    const std::uint32_t pitched = partMask & ((1u << Drums) - 1u);
    std::uint32_t bump[NumParts] {};
    std::uint32_t tried[NumParts] {};
    for (int k = 0; k < kOrigAttempts; ++k)
    {
        for (int p = 0; p < NumParts; ++p)
            tried[p] = base[p] + bump[p];
        g = input;
        generateOnce (g, settings, tried, partMask, modeSeed);
        if (pitched == 0)
            return true;
        // A&R thin-cell guard (bars 5-8): a shipped bass whose back half plays
        // one pitch class on four or fewer attacks in two or more bars is
        // composed again (the composer avoids it; the post passes can still
        // produce it).
        // The JACKIN repeat caps hold on every attempt, the last one included:
        // a bass over them is never shipped (the round fails, as for a copy).
        if ((pitched & (1u << Bass)) != 0 && g.dialect == Jackin && ! jackinRepeatOk (g.lane[Bass], g.bars))
        {
            ++bump[Bass];
            ++tally.guard;
            tally.add (Bass, 0, bassRepeat (g.lane[Bass], g.bars, 16), "guard:jackin_repeat");
            continue;
        }
        if ((pitched & (1u << Bass)) != 0 && rules::thinRepeatedBackHalf (g.lane[Bass], g.bars))
        {
            ++bump[Bass];
            ++tally.guard;
            tally.add (Bass, 0, 1.f, "guard:thin_back_half");
            continue;
        }
        const auto rep = orig::check (g, pitched, external);
        if (rep.pass)
            return true;
        ++tally.orig;
        tally.add (rep.worst.stream, rep.worst.metric, rep.worst.score, rep.worst.refId.c_str());
        std::uint32_t lanes = rep.bumpMask & pitched;
        if (lanes == 0)
            lanes = pitched;
        if ((pitched & (1u << Bass)) != 0)
            for (int p = 0; p < Drums; ++p)
                if (p != Bass && ((lanes >> p) & 1u) != 0 && bump[p] >= 2)
                    lanes |= 1u << Bass;
        for (int p = 0; p < Drums; ++p)
            if (((lanes >> p) & 1u) != 0)
                ++bump[p];
    }
    return false;
}

// The seed a refused composition starts again from: a fixed hash of the caller
// seed, the part and the round, so the retry is as deterministic as the roll.
std::uint32_t originalityRecomposeSeed (std::uint32_t seed, int part, int round) noexcept
{
    const std::uint64_t h = orig::mix64 (((std::uint64_t) seed << 32)
                                         ^ (0x0F5EED00ull + (std::uint64_t) round * 0x100ull + (std::uint64_t) part));
    const auto s = (std::uint32_t) (h >> 32);
    return s ? s : 1u;
}

// The originality floor is a hard gate, not a score: a copy is never returned.
// Every roll is checked (orig::check: the built-in banned figures, plus the
// hashed reference index when one is loaded) and redrawn as composeOriginal
// says. If a composition never clears, the roll is dropped and composed again
// from originalityRecomposeSeed (kOrigRounds compositions in all). If none clears,
// generate() logs it, leaves g exactly as it was passed in and returns false.
// Only the seeds feed the result, so the same seeds and the same index give
// the same roll (or the same refusal). The caller seeds land on the lanes.
void giveWayToBass (Groove& g, std::uint32_t skipMask) noexcept
{
    for (int part : { (int) Stab, (int) Pad, (int) Pluck, (int) Hits, (int) Vox })
    {
        if ((skipMask & (1u << (unsigned) part)) != 0)
            continue;
        fixUpperClashes (g, part);
        dedupeNoteOns (g.lane[part]);
        trimSamePitchOverlaps (g.lane[part]);
    }
}

bool generate (Groove& g, const Settings& settings, const std::uint32_t seeds[NumParts], std::uint32_t partMask)
{
    const auto external = orig::currentIndex();
    const auto input = std::make_unique<Groove> (g);
    std::uint32_t base[NumParts] {};
    auto tally = std::make_unique<RedrawTally>();
    // The roll's batch position for the BOUNCY pulse quota: the caller's drums seed.
    const std::uint32_t modeSeed = seeds[Drums] ? seeds[Drums] : 1u;
    for (int round = 0; round < kOrigRounds; ++round)
    {
        for (int p = 0; p < NumParts; ++p)
            base[p] = (round == 0 || ((partMask >> p) & 1u) == 0) ? seeds[p] : originalityRecomposeSeed (seeds[p], p, round);
        if (! composeOriginal (g, *input, settings, base, partMask, external.get(), *tally, modeSeed))
            continue;
        const int redraws = tally->total();
        tally->writeTo (g);
        for (int p = 0; p < Drums; ++p)
            if (((partMask >> p) & 1u) != 0)
                g.lane[p].seed = seeds[p] ? seeds[p] : 1u;
        if ((partMask & (1u << Drums)) != 0)
            g.drums.seed = seeds[Drums] ? seeds[Drums] : 1u;
        g.generation = input->generation + 1;
        if (redraws > 0)
        {
            const size_t used = std::strlen (g.brokenRules);
            if (used + 1 < sizeof g.brokenRules)
            {
                if (round > 0)
                    std::snprintf (g.brokenRules + used, sizeof g.brokenRules - used, "|orig:%d|recompose:%d", redraws, round);
                else
                    std::snprintf (g.brokenRules + used, sizeof g.brokenRules - used, "|orig:%d", redraws);
            }
        }
        return true;
    }
    std::fprintf (stderr,
                  "ROLLA originality: roll REFUSED (style %d, bass seed %u, mask 0x%x): %d redraws over %d compositions "
                  "all matched a reference or a banned figure; nothing returned\n",
                  settings.dialect, (unsigned) seeds[Bass], (unsigned) partMask, tally->total(), kOrigRounds);
    g = *input;
    return false;
}

} // namespace rolla
