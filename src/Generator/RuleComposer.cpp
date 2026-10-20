#include "RuleComposer.h"
#include "RuleTables.h"
#include "PhraseLock.h"

#include <algorithm>
#include <bitset>
#include <cmath>

namespace rolla::phrase
{
namespace
{
// Music theory, not data: the three minor modes as semitone sets.
constexpr int kModeSteps[3][7] = {
    { 0, 2, 3, 5, 7, 9, 10 }, // dorian
    { 0, 2, 3, 5, 7, 8, 10 }, // aeolian
    { 0, 1, 3, 5, 7, 8, 10 }, // phrygian
};
} // namespace

int degreeToSemis (int degree, int mode) noexcept
{
    const int m = std::clamp (mode, 0, 2);
    const int oct = (degree >= 0) ? degree / 7 : -((-degree + 6) / 7);
    const int idx = degree - oct * 7;
    return oct * 12 + kModeSteps[m][idx];
}

bool inMode (int semis, int mode) noexcept
{
    const int pc = ((semis % 12) + 12) % 12;
    for (int v : kModeSteps[std::clamp (mode, 0, 2)])
        if (v == pc)
            return true;
    return false;
}

} // namespace rolla::phrase

namespace rolla::rules
{
namespace
{
using namespace tables;

constexpr int kBar = 16;

int styleOf (int dialect) noexcept
{
    return std::clamp (dialect, 0, kStyles - 1);
}

bool kickStep (int slot) noexcept
{
    return (slot & 3) == 0;
}

int pcOf (int semi) noexcept
{
    const int pc = semi % 12;
    return pc < 0 ? pc + 12 : pc;
}

std::uint32_t mix32 (std::uint32_t a, std::uint32_t b) noexcept
{
    std::uint32_t h = a * 0x9E3779B1u ^ (b * 0x85EBCA6Bu + 0x7F4A7C15u);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h == 0 ? 1u : h;
}

// Weighted draw over n <= 32 entries with a uniform mix: each legal entry gets
// (1 - kUniformMix) * w + kUniformMix * mean(w). -1 when nothing is legal.
int pickWeighted (const float* w, const bool* legal, int n, Rng& r) noexcept
{
    n = std::min (n, 32);
    float tot = 0.f;
    int nl = 0;
    for (int i = 0; i < n; ++i)
        if (legal[i])
        {
            tot += std::max (0.f, w[i]);
            ++nl;
        }
    if (nl == 0)
        return -1;
    float adj[32] {};
    float sum = 0.f;
    const float mean = tot > 0.f ? tot / (float) nl : 1.f;
    for (int i = 0; i < n; ++i)
    {
        if (! legal[i])
            continue;
        adj[i] = tot > 0.f ? (1.f - kUniformMix) * std::max (0.f, w[i]) + kUniformMix * mean : 1.f;
        sum += adj[i];
    }
    float x = (float) r.next() * sum;
    int last = -1;
    for (int i = 0; i < n; ++i)
    {
        if (! legal[i])
            continue;
        last = i;
        x -= adj[i];
        if (x <= 0.f)
            return i;
    }
    return last;
}

int pickAll (const float* w, int n, Rng& r) noexcept
{
    bool legal[32];
    for (int i = 0; i < 32; ++i)
        legal[i] = i < n;
    return pickWeighted (w, legal, n, r);
}

// Normalised Shannon entropy H(p) / log(n) of a non-negative table.
float entropyNorm (const float* v, int n) noexcept
{
    float s = 0.f;
    for (int i = 0; i < n; ++i)
        s += std::max (0.f, v[i]);
    if (s <= 0.f || n < 2)
        return 1.f;
    double h = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double p = std::max (0.f, v[i]) / s;
        if (p > 0.0)
            h -= p * std::log (p);
    }
    return (float) (h / std::log ((double) n));
}

// The model's run-time entropy floor: mix `v` toward `target` in
// kEntropyMixStep steps until H(v) >= floor.
void enforceFloor (float* v, const float* target, int n, float floor) noexcept
{
    float base[32];
    n = std::min (n, 32);
    for (int i = 0; i < n; ++i)
        base[i] = v[i];
    for (float a = 0.f; entropyNorm (v, n) < floor && a < 1.f;)
    {
        a = std::min (1.f, a + kEntropyMixStep);
        for (int i = 0; i < n; ++i)
            v[i] = (1.f - a) * base[i] + a * target[i];
    }
}

// Transition row: cap every cell at kCellCeiling (the excess spread over the
// row in proportion to the prior), then the entropy floor toward the prior.
void prepareRow (float* row, const float* prior, float floor) noexcept
{
    float s = 0.f;
    for (int i = 0; i < 12; ++i)
        s += std::max (0.f, row[i]);
    for (int i = 0; i < 12; ++i)
        row[i] = s > 0.f ? std::max (0.f, row[i]) / s : 1.f / 12.f;
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int guard = 0; guard < 12; ++guard)
        {
            int over = -1;
            for (int i = 0; i < 12 && over < 0; ++i)
                if (row[i] > kCellCeiling + 1.0e-6f)
                    over = i;
            if (over < 0)
                break;
            const float ex = row[over] - kCellCeiling;
            row[over] = kCellCeiling;
            float ps = 0.f;
            for (int i = 0; i < 12; ++i)
                if (i != over)
                    ps += prior[i];
            for (int i = 0; i < 12; ++i)
                if (i != over)
                    row[i] += ps > 0.f ? ex * prior[i] / ps : ex / 11.f;
        }
        if (pass == 0)
            enforceFloor (row, prior, 12, floor);
    }
}

// Per-roll working copies of the bass tables, after the per-roll reshaping
// (kick weighting) and the entropy floors, before the mode mask. Built for
// each composition and dropped with it: nothing is kept between rolls.
struct BassTables
{
    float grid[3][kBar];
    float trans[12][12];
    // The degree-by-strength rows and the bar-end degree tables the composer draws from: the
    // style's model tables, or (BASSMATCH 2) the pooled pack tables.
    const float (*choice)[12] = nullptr;
    const float* endBar4 = nullptr;
    const float* endBar8 = nullptr;
    // The onset draw's per-bar kick cap, and P(16th 1 is placed first) (0: no beat-1 anchor).
    float kickCapBar = kBassKickHardCap;
    float beat1 = 0.f;
    int match = 0;
};

void prepareBass (int st, BassTables& t, int match = 0) noexcept
{
    // BASSMATCH 2 (Oct 20 bass redesign): the pooled pack tables. Below 2 every table is the
    // style's own, exactly as before.
    const bool pack = match >= 2;
    t.match = match;
    t.choice = pack ? kPackBassChoice : kBassChoice[st];
    t.endBar4 = pack ? kPackBassEndBar : kBassEndBar4[st];
    t.endBar8 = pack ? kPackBassEndBar : kBassEndBar8[st];
    t.kickCapBar = pack ? kMatchKickCapBar : kBassKickHardCap;
    t.beat1 = pack ? kMatchBeat1[st] : 0.f;
    float priorMean = 0.f;
    for (float x : kPackBassSlot)
        priorMean += x / (float) kBar;
    for (int g = 0; g < 3; ++g)
    {
        float kick = 0.f;
        float off = 0.f;
        // Level 2: half the style's grid, half the pack slot prior scaled to the style grid's mean.
        float styleMean = 0.f;
        if (pack)
            for (int s = 0; s < kBar; ++s)
                styleMean += kBassOnset[st][g][s] / (float) kBar;
        const float priorScale = priorMean > 0.f ? styleMean / priorMean : 0.f;
        for (int s = 0; s < kBar; ++s)
        {
            t.grid[g][s] = kBassOnset[st][g][s];
            if (pack)
                t.grid[g][s] = (1.f - kPackSlotMix) * t.grid[g][s] + kPackSlotMix * kPackBassSlot[s] * priorScale;
            (kickStep (s) ? kick : off) += t.grid[g][s];
        }
        // Kick steps weighted so the expected on-kick share is the style median.
        const float k = kKickTarget[st];
        const float fac = kick > 0.f ? std::clamp (k * off / ((1.f - k) * kick), 0.05f, 3.f) : 1.f;
        float mean = 0.f;
        for (int s = 0; s < kBar; ++s)
        {
            if (kickStep (s))
                t.grid[g][s] *= fac;
            mean += t.grid[g][s] / (float) kBar;
        }
        float flat[kBar];
        for (float& x : flat)
            x = mean;
        // Level 2: the entropy floor mixes toward the pack slot prior (at the grid's mean), not flat.
        if (pack && priorMean > 0.f)
            for (int s = 0; s < kBar; ++s)
                flat[s] = kPackBassSlot[s] * (mean / priorMean);
        enforceFloor (t.grid[g], flat, kBar, kFloorOnsetBass);
    }
    for (int r = 0; r < 12; ++r)
    {
        for (int c = 0; c < 12; ++c)
            t.trans[r][c] = pack ? kPackBassTrans[r][c] : kBassTrans[st][r][c];
        // The cell ceiling and the entropy floor hold for the pack rows too (prior: the pack unigram).
        prepareRow (t.trans[r], pack ? kPackBassUnigram : kBassUnigram[st], kFloorRowBass);
    }
}

// Triangular-ish draw with sd 1 (three uniforms).
float gauss (Rng& r) noexcept
{
    return (float) ((r.next() + r.next() + r.next() - 1.5) * 2.0);
}

int strengthOf (int slot) noexcept
{
    return (slot & 3) == 0 ? 0 : ((slot & 3) == 2 ? 1 : 2);
}

// ------------------------------------------------------------------ rhythm

bool makesRun (const bool* on, int slot) noexcept
{
    int left = 0;
    for (int s = slot - 1; s >= 0 && on[s]; --s)
        ++left;
    int right = 0;
    for (int s = slot + 1; s < kBar && on[s]; ++s)
        ++right;
    return left + right + 1 > kMaxAdjacentRun;
}

int countOn (const bool* on, int from = 0, int to = kBar) noexcept
{
    int c = 0;
    for (int s = from; s < to; ++s)
        if (on[s])
            ++c;
    return c;
}

// Adds onsets to `on` (weights w, slots < limit) until it holds `count` or
// nothing is legal. No run of more than kMaxAdjacentRun attacks, and the kick
// steps stay under the kickCap share (35%; BASSMATCH 2: 50%) bar by bar.
// beat1 > 0 (BASSMATCH 2): 16th 1 is placed first with that probability.
void drawOnsets (const float* w, int count, int limit, Rng& r, bool* on, float kickCap = kBassKickHardCap,
                 float beat1 = 0.f)
{
    count = std::clamp (count, 1, 10);
    if (beat1 > 0.f && ! on[0] && limit > 0 && w[0] > 0.f && countOn (on) < count
        && 1.f <= kickCap * (float) count && r.next() < (double) beat1)
        on[0] = true;
    for (int guard = 0; guard < 16 && countOn (on) < count; ++guard)
    {
        int kicks = 0;
        for (int s = 0; s < kBar; s += 4)
            if (on[s])
                ++kicks;
        const bool kickOk = (float) (kicks + 1) <= kickCap * (float) count;
        bool legal[kBar];
        for (int s = 0; s < kBar; ++s)
            legal[s] = ! on[s] && s < limit && w[s] > 0.f && ! makesRun (on, s) && (! kickStep (s) || kickOk);
        const int pick = pickWeighted (w, legal, kBar, r);
        if (pick < 0)
            break;
        on[pick] = true;
    }
}

float densityTarget (int st, int sub) noexcept
{
    switch (sub)
    {
        case SubDeepDark: return kDeepDarkNpb;
        case SubDeepSparse: return kDeepSparseNpb;
        case SubBounce: return kBounceNpb;
        case SubPulse: return kPulseNpb;
        default: return kBassDensityMedian[st];
    }
}

// Notes for bar `bar` (0-7) of the phrase from the density curve.
int drawCount (int st, int bar, float scale, Rng& r) noexcept
{
    const int b = std::clamp (bar, 0, 7);
    const float mean = kBassDensity[st][b][0] * scale;
    const float sd = kBassDensity[st][b][1] * scale;
    return std::clamp ((int) std::lround (mean + sd * gauss (r)), kBarCountLo, kBarCountHi);
}

// ------------------------------------------------------------------ pitch

// One first-order step: P(next | prev) = transition x degree-by-strength,
// masked to the mode, with the DARK tilt. Returns a pitch class.
int nextPc (int st, int mode, float dark, const BassTables& t, int prevPc, int slot, bool allowRoot, Rng& r) noexcept
{
    (void) st; // the style's rows are in t (t.choice)
    float w[12];
    bool legal[12];
    const int k = strengthOf (slot);
    for (int pc = 0; pc < 12; ++pc)
    {
        float x = t.trans[prevPc][pc] * (t.choice[k][pc] + 1.0e-3f);
        if (dark <= 0.2f)
        {
            if (pc == 7)
                x *= kBrightFifth;
            if (pc == 3)
                x *= 0.5f;
        }
        else if (dark >= 0.8f)
        {
            if (pc == 3)
                x *= kDarkThird;
            if (pc == 1 || pc == 2)
                x *= kDarkSecond;
            if (pc == 7)
                x *= 0.6f;
        }
        w[pc] = x;
        legal[pc] = phrase::inMode (pc, mode) && (pc != 0 || allowRoot);
    }
    const int pick = pickWeighted (w, legal, 12, r);
    return pick < 0 ? (allowRoot ? 0 : 7) : pick;
}

// BASSMATCH 2: the degree on 16th 1 of a bar, from the beat-1 weights (root, 4th, b6, 5th, b3,
// b7), masked to the mode.
int beat1Pc (int mode, bool allowRoot, Rng& r) noexcept
{
    bool legal[12];
    for (int pc = 0; pc < 12; ++pc)
        legal[pc] = kMatchSlot0[pc] > 0.f && phrase::inMode (pc, mode) && (pc != 0 || allowRoot);
    const int pick = pickWeighted (kMatchSlot0, legal, 12, r);
    return pick < 0 ? (allowRoot ? 0 : 7) : pick;
}

// Octave placement: the 5th and above may sit under the root; the interval
// distribution from the previous pitch decides.
int placeOctave (int st, int pc, int prevSemi, Rng& r) noexcept
{
    if (pc < 7)
        return pc;
    // The interval distribution gets the model's entropy floor at run time.
    float iv[27];
    float flat[27];
    float mean = 0.f;
    for (int i = 0; i < 27; ++i)
        mean += kBassInterval[st][i] / 27.f;
    for (int i = 0; i < 27; ++i)
    {
        iv[i] = kBassInterval[st][i];
        flat[i] = mean;
    }
    enforceFloor (iv, flat, 27, kFloorIntervalBass);
    float w[2];
    for (int k = 0; k < 2; ++k)
    {
        const int c = pc - 12 * k;
        w[k] = iv[std::clamp (c - prevSemi + 13, 0, 26)] + 1.0e-3f;
    }
    return pc - 12 * pickAll (w, 2, r);
}

struct Bar
{
    bool on[kBar] {};
    int semi[kBar] {};
};

// Pitches the onsets of one bar from `from` on. Slots below `from` keep their
// pitches. The 2&4 root figure (root on 16ths 2 and 4) is never drawn.
// lastOpen: the bar's last onset avoids the root.
void pitchBar (int st, int mode, float dark, const BassTables& t, Bar& b, int from, int prevSemi, bool callStart,
               bool lastOpen, Rng& r)
{
    int last = -1;
    for (int s = 0; s < kBar; ++s)
        if (b.on[s])
            last = s;
    int prev = prevSemi;
    bool first = true;
    for (int s = 0; s < kBar; ++s)
    {
        if (! b.on[s])
            continue;
        if (s < from)
        {
            prev = b.semi[s];
            first = false;
            continue;
        }
        bool allowRoot = ! (lastOpen && s == last);
        if (s == 3 && b.on[1] && pcOf (b.semi[1]) == 0)
            allowRoot = false;
        if (t.match >= 2 && s == 0)
            b.semi[s] = placeOctave (st, beat1Pc (mode, allowRoot, r), prev, r);
        else if (first && callStart && allowRoot && r.next() < (double) kFirstRoot[st])
            b.semi[s] = 0;
        else
            b.semi[s] = placeOctave (st, nextPc (st, mode, dark, t, pcOf (prev), s, allowRoot, r), prev, r);
        prev = b.semi[s];
        first = false;
    }
}

int lastSemi (const Bar& b, int fallback) noexcept
{
    int v = fallback;
    for (int s = 0; s < kBar; ++s)
        if (b.on[s])
            v = b.semi[s];
    return v;
}

int prevSemiBefore (const Bar* bars, int bar, int slot) noexcept
{
    for (int b = bar; b >= 0; --b)
        for (int s = (b == bar ? slot - 1 : kBar - 1); s >= 0; --s)
            if (bars[b].on[s])
                return bars[b].semi[s];
    return 0;
}

// Redraws slots 8-15 of `b` to `backCount` attacks from `grid` (the back-half
// mutation: the front half, slots 0-7, never changes).
void redrawBack (int st, int mode, float dark, const BassTables& t, const float* grid, int backCount, int limit,
                 Rng& r, Bar& b)
{
    float w[kBar];
    for (int s = 0; s < kBar; ++s)
        w[s] = s < 8 ? 0.f : grid[s] * (b.on[s] ? 0.5f : 1.f);
    const int front = countOn (b.on, 0, 8);
    int prevFront = 0;
    for (int s = 0; s < 8; ++s)
        if (b.on[s])
            prevFront = b.semi[s];
    for (int s = 8; s < kBar; ++s)
        b.on[s] = false;
    drawOnsets (w, front + std::clamp (backCount, 1, 6), limit, r, b.on, t.kickCapBar);
    pitchBar (st, mode, dark, t, b, 8, prevFront, false, false, r);
}

int changedSlots (const Bar& a, const Bar& b) noexcept
{
    int n = 0;
    for (int s = 0; s < kBar; ++s)
        if (a.on[s] != b.on[s] || (a.on[s] && pcOf (a.semi[s]) != pcOf (b.semi[s])))
            ++n;
    return n;
}

// The last onset of `b` gets a degree from an ending distribution (masked).
void endOn (const float* dist, int mode, Rng& r, Bar& b) noexcept
{
    int last = -1;
    for (int s = 0; s < kBar; ++s)
        if (b.on[s])
            last = s;
    if (last < 0)
        return;
    bool legal[12];
    for (int pc = 0; pc < 12; ++pc)
        legal[pc] = phrase::inMode (pc, mode) && ! (last == 3 && b.on[1] && pcOf (b.semi[1]) == 0 && pc == 0);
    const int pc = pickWeighted (dist, legal, 12, r);
    if (pc >= 0)
        b.semi[last] = pc;
}

void dropLatest (Bar& b, int from, int to) noexcept
{
    for (int s = to - 1; s >= from; --s)
        if (b.on[s])
        {
            b.on[s] = false;
            return;
        }
}

// The turn bar: rest from kTurnRestFrom on, then one bar-8 palette move.
void turnBar (int st, int mode, float dark, const BassTables& t, const Bar* bars, int idx, Rng& r, Bar& b)
{
    for (int s = kTurnRestFrom; s < kBar; ++s)
        b.on[s] = false;
    if (r.next() < (double) kBar8AnswerProb[st])
    {
        const int move = pickAll (kBar8Palette[st], kPaletteMoves, r);
        switch (move)
        {
            case PalDropOut:
                if (countOn (b.on) > 2)
                    dropLatest (b, 8, kTurnRestFrom);
                break;
            case PalTruncate:
                if (countOn (b.on) > 2)
                    dropLatest (b, 0, kTurnRestFrom);
                break;
            case PalFill:
            {
                float w[kBar] {};
                for (int s = 8; s < kTurnRestFrom; ++s)
                    w[s] = t.grid[2][s];
                bool before[kBar];
                for (int s = 0; s < kBar; ++s)
                    before[s] = b.on[s];
                const int want = countOn (b.on) + 1 + (int) (r.nextU() % 2u);
                drawOnsets (w, want, kTurnRestFrom, r, b.on, t.kickCapBar);
                for (int s = 8; s < kTurnRestFrom; ++s)
                    if (b.on[s] && ! before[s])
                    {
                        const int prev = prevSemiBefore (bars, idx, s);
                        b.semi[s] = placeOctave (st, nextPc (st, mode, dark, t, pcOf (prev), s, true, r), prev, r);
                    }
                break;
            }
            case PalDisplace:
            {
                for (int s = 8; s < kTurnRestFrom; ++s)
                {
                    if (! b.on[s])
                        continue;
                    const int to = s + ((r.nextU() & 1u) ? 1 : -1);
                    if (to < 8 || to >= kTurnRestFrom || b.on[to] || kickStep (to))
                        continue;
                    b.on[s] = false;
                    if (makesRun (b.on, to))
                    {
                        b.on[s] = true;
                        continue;
                    }
                    b.on[to] = true;
                    b.semi[to] = b.semi[s];
                    break;
                }
                break;
            }
            default:
                break;
        }
        if (move == PalPitchAnswer || move == PalOctaveLift)
        {
            int last = -1;
            for (int s = 0; s < kBar; ++s)
                if (b.on[s])
                    last = s;
            if (last >= 0 && move == PalPitchAnswer)
            {
                const int cand[3] = { 5, 7, 10 };
                for (int k = 0, o = (int) (r.nextU() % 3u); k < 3; ++k)
                    if (phrase::inMode (cand[(o + k) % 3], mode))
                    {
                        b.semi[last] = cand[(o + k) % 3];
                        break;
                    }
            }
            else if (last >= 0 && b.semi[last] + 12 <= 19)
                b.semi[last] += 12;
        }
        else if (move != PalPickup)
            endOn (t.endBar8, mode, r, b);
    }
    if (countOn (b.on) == 0)
    {
        b.on[2] = true;
        b.semi[2] = 0;
    }
}

// Bars nb/2 .. nb-1 each carry at least kThinBarMinPcs pitch classes: a
// one-pitch bar gets a different degree on its last back-half attack, or a
// new back-half attack when it has only one.
void repairThinBars (int st, int mode, float dark, const BassTables& t, Bar* bars, int nb, Rng& r)
{
    if (nb < 4)
        return;
    for (int b = nb / 2; b < nb; ++b)
    {
        Bar& bar = bars[b];
        const int limit = b == nb - 1 ? kTurnRestFrom : kBar;
        int n = 0;
        int pc0 = -1;
        bool one = true;
        int last = -1;
        for (int s = 0; s < kBar; ++s)
        {
            if (! bar.on[s])
                continue;
            ++n;
            last = s;
            if (pc0 < 0)
                pc0 = pcOf (bar.semi[s]);
            else if (pcOf (bar.semi[s]) != pc0)
                one = false;
        }
        if (n == 0 || ! one || kThinBarMinPcs < 2)
            continue;
        if (n == 1)
        {
            // One attack: add a back-half attack, which takes the new degree.
            float w[kBar] {};
            for (int s = 8; s < limit; ++s)
                w[s] = bar.on[s] ? 0.f : t.grid[0][s];
            bool before[kBar];
            for (int s = 0; s < kBar; ++s)
                before[s] = bar.on[s];
            drawOnsets (w, 2, limit, r, bar.on, t.kickCapBar);
            int added = -1;
            for (int s = 0; s < kBar; ++s)
                if (bar.on[s] && ! before[s])
                    added = s;
            if (added < 0)
                continue;
            last = added;
        }
        float w[12];
        bool legal[12];
        const int prev = pcOf (prevSemiBefore (bars, b, last));
        for (int pc = 0; pc < 12; ++pc)
        {
            w[pc] = t.trans[prev][pc] + 1.0e-3f;
            legal[pc] = pc != pc0 && phrase::inMode (pc, mode) && ! (last == 3 && bar.on[1] && pcOf (bar.semi[1]) == 0 && pc == 0);
        }
        const int pc = pickWeighted (w, legal, 12, r);
        if (pc >= 0)
            bar.semi[last] = pc;
        (void) st;
        (void) dark;
    }
}

int barCountOf (const Bar& b) noexcept
{
    return countOn (b.on);
}

// The density curve's range rule: max - min notes a bar >= kDensityRangeMin
// over 8 bars; the change goes into a back half (bars 5-7).
void spreadDensity (const BassTables& t, int st, int mode, float dark, Bar* bars, int nb, Rng& r)
{
    if (nb < 8)
        return;
    for (int guard = 0; guard < 3; ++guard)
    {
        int lo = 99;
        int hi = -1;
        for (int b = 0; b < nb; ++b)
        {
            lo = std::min (lo, barCountOf (bars[b]));
            hi = std::max (hi, barCountOf (bars[b]));
        }
        if (hi - lo >= kDensityRangeMin)
            return;
        // Lift bar 6's back half to one over the busiest bar: the range opens upward.
        Bar& b = bars[5];
        const int back = countOn (b.on, 8, kBar);
        redrawBack (st, mode, dark, t, t.grid[0], back + (hi - barCountOf (b)) + 1, kBar, r, b);
    }
}

// Lengths: a fraction of the gap to the next onset, then the median-length
// and long-note guards. Notes never reach the next attack, the loop end, the
// turn rest or (a b3 late in a bar) the barline.
void writeLane (Lane& lane, const Bar* bars, int nb, int st, int sub, Rng& r)
{
    lane.count = 0;
    const float loopEnd = (float) (nb * kBar);
    for (int b = 0; b < nb; ++b)
    {
        bool firstInBar = true;
        for (int s = 0; s < kBar && lane.count < kMaxNotes; ++s)
        {
            if (! bars[b].on[s])
                continue;
            Note n;
            n.start = (float) (b * kBar + s);
            n.chord[0] = (std::int8_t) bars[b].semi[s];
            n.nChord = 1;
            n.vel = (std::uint8_t) (firstInBar ? 112 : 104);
            n.flags = 0;
            lane.notes[lane.count++] = n;
            firstInBar = false;
        }
    }
    float frac = kGapFraction[st];
    float lo = kLenMedLo[st];
    float hi = kLenMedHi[st];
    if (sub == SubDeepDark)
    {
        frac = kDeepDarkGap;
        lo = kDeepDarkLenLo;
        hi = kDeepDarkLenHi;
    }
    else if (sub == SubDeepSparse)
    {
        frac = kDeepSparseGap;
        lo = kDeepSparseLenLo;
        hi = kDeepSparseLenHi;
    }
    else if (sub == SubPulse)
    {
        frac = kPulseGap;
        lo = kPulseLenLo;
        hi = kPulseLenHi;
    }
    const bool bounce = sub == SubBounce || (st == styleOf (Pop) && sub != SubPulse);
    float gap[kMaxNotes];
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        float next = i + 1 < lane.count ? lane.notes[i + 1].start : loopEnd;
        const int st16 = (int) std::lround (n.start);
        if (st16 % kBar >= 14 && pcOf (n.chord[0]) == 3)
            next = std::min (next, (float) ((st16 / kBar + 1) * kBar));
        if (st16 / kBar == nb - 1)
            next = std::min (next, (float) ((nb - 1) * kBar + kTurnRestFrom));
        gap[i] = std::max (0.25f, next - n.start);
        float len;
        if (bounce)
            len = kBounceLenLo + (kBounceLenHi - kBounceLenLo) * (float) r.next();
        else
            len = gap[i] * (frac + 0.04f * (float) (r.next() * 2.0 - 1.0));
        len = std::floor (len * 4.f) / 4.f;
        n.length = std::min (gap[i], std::max (0.5f, len));
    }
    if (lane.count <= 0 || bounce)
        return;
    // Median guard.
    const float med = medianLength (lane);
    if (med > hi && med > 0.f)
        for (int i = 0; i < lane.count; ++i)
            lane.notes[i].length = std::max (0.5f, std::floor (lane.notes[i].length * hi / med * 4.f) / 4.f);
    else if (med < lo && med > 0.f)
        for (int i = 0; i < lane.count; ++i)
            lane.notes[i].length = std::min (gap[i], std::floor (lane.notes[i].length * lo / med * 4.f) / 4.f);
    // Long-note guard outside DEEP: nothing reaches 6 16ths.
    if (kLongGe6Max[st] < 1.f)
        for (int i = 0; i < lane.count; ++i)
            lane.notes[i].length = std::min (lane.notes[i].length, 5.75f);
}

// Hard rule: no more than 35% of the attacks on kick steps. The latest kick
// attack moves one 16th later when that slot is free, else it goes.
void enforceKickCap (Lane& lane, int nb)
{
    for (int guard = 0; guard < kMaxNotes && lane.count > 0; ++guard)
    {
        if (kickShare (lane) <= kBassKickHardCap + 1.0e-4f)
            return;
        int victim = -1;
        for (int i = lane.count - 1; i >= 0 && victim < 0; --i)
            if (kickStep (((int) std::lround (lane.notes[i].start)) % kBar))
                victim = i;
        if (victim < 0)
            return;
        const int st = (int) std::lround (lane.notes[victim].start);
        const int to = st + 1;
        bool free = to < nb * kBar && (to % kBar) < kBar && ! (to / kBar == nb - 1 && to % kBar >= kTurnRestFrom);
        for (int i = 0; i < lane.count && free; ++i)
            if ((int) std::lround (lane.notes[i].start) == to)
                free = false;
        if (free && lane.count > 1)
        {
            lane.notes[victim].start = (float) to;
            const float room = (victim + 1 < lane.count ? lane.notes[victim + 1].start : (float) (nb * kBar)) - (float) to;
            lane.notes[victim].length = std::max (0.5f, std::min (lane.notes[victim].length, room));
        }
        else if (lane.count > 1)
        {
            for (int i = victim; i + 1 < lane.count; ++i)
                lane.notes[i] = lane.notes[i + 1];
            --lane.count;
        }
        else
            return;
    }
}

// The 8-bar line's own onset histogram stays above the floor minus the margin.
bool onsetEntropyOk (const Lane& lane, int nb) noexcept
{
    if (nb < 8)
        return true;
    float h[kBar] {};
    for (int i = 0; i < lane.count; ++i)
        h[((int) std::lround (lane.notes[i].start)) % kBar] += 1.f;
    return entropyNorm (h, kBar) >= kFloorOnsetBass - kOutputOnsetMargin;
}

// Time-weighted pitch-class shares of a mono lane.
void pcTime (const Lane& lane, float* t, float& total) noexcept
{
    total = 0.f;
    for (int pc = 0; pc < 12; ++pc)
        t[pc] = 0.f;
    for (int i = 0; i < lane.count; ++i)
    {
        float len = std::max (0.f, lane.notes[i].length);
        if (i + 1 < lane.count)
            len = std::min (len, std::max (0.f, lane.notes[i + 1].start - lane.notes[i].start));
        t[pcOf (lane.notes[i].chord[0])] += len;
        total += len;
    }
}

// Last resort for the tonic guard: the longest non-root notes become roots.
void repairTonic (Lane& lane, int st)
{
    for (int guard = 0; guard < lane.count && ! tonicGuardOk (lane, st); ++guard)
    {
        int best = -1;
        for (int i = 0; i < lane.count; ++i)
        {
            const int pc = pcOf (lane.notes[i].chord[0]);
            if (pc == 0)
                continue;
            const int slot = ((int) std::lround (lane.notes[i].start)) % kBar;
            if (slot == 3 || slot == 1)
                continue; // never the 2&4 root figure
            if (best < 0 || lane.notes[i].length > lane.notes[best].length)
                best = i;
        }
        if (best < 0)
            return;
        const int semi = lane.notes[best].chord[0];
        lane.notes[best].chord[0] = (std::int8_t) (semi - pcOf (semi));
    }
}

int arcOf (const Bar* bars, int nb) noexcept
{
    if (nb < 8)
        return 0;
    float d[3];
    float front = 0.f;
    for (int b = 0; b < 3; ++b)
        front += (float) barCountOf (bars[b]) / 3.f;
    float sum = 0.f;
    for (int j = 0; j < 3; ++j)
    {
        d[j] = (float) barCountOf (bars[4 + j]) - front;
        sum += d[j];
    }
    if (sum >= 2.f)
        return 1; // build
    if (sum <= -2.f)
        return 2; // dip
    if (std::max (d[0], std::max (d[1], d[2])) - std::min (d[0], std::min (d[1], d[2])) >= 2.f)
        return 3; // swell
    return 0;
}

int composeOnce (Lane& lane, const BassParams& p, const BassTables& t, Rng& r, bool& rawKickOk)
{
    const int st = styleOf (p.dialect);
    const int nb = p.bars >= 8 ? 8 : (p.bars >= 4 ? 4 : 2);
    const float scale = densityTarget (st, p.sub) / std::max (0.5f, kBassDensityMedian[st]);
    Bar bars[8];

    // JACKIN hooks lean into beats 3-4: its onset weights shift toward the back
    // half of the bar, so the bar-to-bar changes (its bars never repeat each
    // other) land mostly there (A&R var_backhalf).
    float lean[kBar];
    for (int s = 0; s < kBar; ++s)
        lean[s] = t.grid[0][s] * (st == styleOf (Jackin) ? (s < 8 ? kJackinBackLean[0] : kJackinBackLean[1]) : 1.f);

    // Bar 1: the call.
    Bar& call = bars[0];
    drawOnsets (lean, drawCount (st, 0, scale, r), kBar, r, call.on, t.kickCapBar, t.beat1);
    pitchBar (st, p.mode, p.dark, t, call, 0, 0, true, false, r);

    // Bar 2: the answer.
    Bar& ans = bars[1];
    {
        float w[kBar];
        int from = 0;
        if (st == styleOf (Jackin))
        {
            for (int s = 0; s < kBar; ++s)
                w[s] = lean[s] * (call.on[s] ? kAnswerCallSlotWeight[st] : kAnswerSilentBoost[st]);
        }
        else if (r.next() < (double) kAnswerKeepFront[st])
        {
            for (int s = 0; s < 8; ++s)
            {
                ans.on[s] = call.on[s];
                ans.semi[s] = call.semi[s];
            }
            for (int s = 0; s < kBar; ++s)
                w[s] = s < 8 ? 0.f : t.grid[0][s] * (call.on[s] ? kAnswerCallSlotWeight[st] : 1.f);
            from = 8;
        }
        else
        {
            for (int s = 0; s < kBar; ++s)
                w[s] = t.grid[0][s] * (call.on[s] ? kAnswerCallSlotWeight[st] : 1.f);
        }
        const int want = std::max (drawCount (st, 1, scale, r), countOn (ans.on, 0, 8) + 1);
        drawOnsets (w, want, kBar, r, ans.on, t.kickCapBar, t.beat1);
        const bool open = r.next() < 0.5;
        pitchBar (st, p.mode, p.dark, t, ans, from, lastSemi (call, 0), from == 0, open, r);
    }

    for (int b = 2; b < nb; ++b)
        bars[b] = bars[b & 1];

    if (nb == 8)
    {
        // Bars 5-7 restate with back-half mutations toward the density curve.
        for (int j = 0; j < 3; ++j)
        {
            const int b = 4 + j;
            int delta = std::clamp (drawCount (st, b, scale, r) - barCountOf (bars[b]), -2, 2);
            if (std::abs (delta) < kRestateDeltaMin[st])
                delta = 0;
            const bool vary = j < 2 && r.next() < (double) kBackHalfVariation[st];
            if (delta == 0 && ! vary)
                continue;
            redrawBack (st, p.mode, p.dark, t, t.grid[0], std::max (1, countOn (bars[b].on, 8, kBar) + delta), kBar, r, bars[b]);
        }
        // Bar 4 answers bar 2 in its back half (BASSMATCH 2: less often, exact repeats are allowed).
        if (r.next() < (double) (t.match >= 2 ? kBar4AnswerProb[st] * kMatchBar4AnswerScale : kBar4AnswerProb[st]))
        {
            for (int tries = 0; tries < 4; ++tries)
            {
                Bar a = bars[3];
                const int delta = std::clamp (pickAll (kBassDeltaBar4[st], 9, r) - 4, -3, 3);
                redrawBack (st, p.mode, p.dark, t, t.grid[1], std::max (1, countOn (bars[1].on, 8, kBar) + delta), kBar, r, a);
                endOn (t.endBar4, p.mode, r, a);
                if (changedSlots (a, bars[1]) >= kAnswerMinChanged || tries == 3)
                {
                    bars[3] = a;
                    break;
                }
            }
        }
    }
    turnBar (st, p.mode, p.dark, t, bars, nb - 1, r, bars[nb - 1]);
    spreadDensity (t, st, p.mode, p.dark, bars, nb, r);
    for (int s = kTurnRestFrom; s < kBar; ++s)
        bars[nb - 1].on[s] = false;
    repairThinBars (st, p.mode, p.dark, t, bars, nb, r);

    writeLane (lane, bars, nb, st, p.sub, r);
    rawKickOk = kickShare (lane) <= kBassKickHardCap + 1.0e-4f;
    enforceKickCap (lane, nb);
    return arcOf (bars, nb);
}

// A voicing (chord bottom + interval set) is legal when every tone is in the
// mode and no tone sits a semitone from a bass pitch class in `bassMask`.
bool voicingLegal (const Voicing& v, int bottom, int mode, unsigned bassMask) noexcept
{
    if (v.n <= 0)
        return false;
    for (int i = 0; i < std::min (v.n, 4); ++i)
    {
        const int pc = pcOf (bottom + v.iv[i]);
        if (! phrase::inMode (pc, mode))
            return false;
        if ((bassMask & (1u << ((pc + 1) % 12))) != 0 || (bassMask & (1u << ((pc + 11) % 12))) != 0)
            return false;
    }
    return true;
}

float consonanceOf (int st, const Voicing& v, int bottom, unsigned bassMask) noexcept
{
    float sum = 0.f;
    int n = 0;
    for (int b = 0; b < 12; ++b)
    {
        if ((bassMask & (1u << b)) == 0)
            continue;
        for (int i = 0; i < std::min (v.n, 4); ++i)
        {
            sum += kConsonance[st][pcOf (bottom + v.iv[i] - b)];
            ++n;
        }
    }
    return n > 0 ? 0.25f + 12.f * sum / (float) n : 1.f;
}

// Picks a chord bottom (from the chord-bottom chain: the unigram, or the row of
// `prevBottom`, times the degree-by-strength weight of `strength`) and a voicing
// for it. Tones folded into the style's register band.
void chooseVoicing (int st, int mode, unsigned bassMask, int prevBottom, int strength, Rng& r, int* out, int& n,
                    int& bottomOut) noexcept
{
    float row[12];
    for (int pc = 0; pc < 12; ++pc)
        row[pc] = prevBottom >= 0 ? kStabTrans[st][prevBottom][pc] : kStabUnigram[st][pc];
    // Cell cap and entropy floor at run time, as for the bass rows.
    prepareRow (row, kStabUnigram[st], kFloorRowStab);
    unsigned mask = bassMask;
    int bottom = -1;
    int vi = -1;
    for (int pass = 0; pass < 2 && vi < 0; ++pass, mask = 0u)
    {
        float bw[12];
        bool bl[12];
        for (int pc = 0; pc < 12; ++pc)
        {
            bw[pc] = row[pc] * (kStabChoice[st][std::clamp (strength, 0, 2)][pc] + 1.0e-3f);
            bl[pc] = false;
            for (int v = 0; v < kVoicingCount[st] && ! bl[pc]; ++v)
                bl[pc] = voicingLegal (kStabVoicings[st][v], pc, mode, mask);
        }
        bottom = pickWeighted (bw, bl, 12, r);
        if (bottom < 0)
            continue;
        float vw[kMaxVoicings];
        bool vl[kMaxVoicings];
        for (int v = 0; v < kMaxVoicings; ++v)
        {
            vl[v] = v < kVoicingCount[st] && voicingLegal (kStabVoicings[st][v], bottom, mode, mask);
            vw[v] = vl[v] ? kStabVoicings[st][v].w * consonanceOf (st, kStabVoicings[st][v], bottom, mask) : 0.f;
        }
        vi = pickWeighted (vw, vl, kMaxVoicings, r);
    }
    if (vi < 0 || bottom < 0)
    {
        // Nothing in the palette fits the mode: the tonic and its fifth.
        bottom = 0;
        static constexpr Voicing kFifth { 2, { 0, 7, 0, 0 }, 1.f };
        vi = -1;
        const int lo = kStabRegLo[st];
        int t0 = 0;
        while (t0 < lo)
            t0 += 12;
        out[0] = t0;
        out[1] = t0 + kFifth.iv[1];
        n = 2;
        bottomOut = 0;
        return;
    }
    const Voicing& V = kStabVoicings[st][vi];
    const int lo = kStabRegLo[st];
    const int hi = kStabRegHi[st];
    int tones[4];
    int m = 0;
    for (int i = 0; i < std::clamp (V.n, 1, 4); ++i)
    {
        int t = bottom + V.iv[i];
        while (t > hi)
            t -= 12;
        while (t < lo)
            t += 12;
        bool dup = false;
        for (int k = 0; k < m; ++k)
            if (tones[k] == t)
                dup = true;
        if (! dup && m < 4)
            tones[m++] = t;
    }
    if (m < 2)
    {
        int t = bottom + 7;
        while (t > hi)
            t -= 12;
        while (t < lo)
            t += 12;
        if (phrase::inMode (t, mode) && (m == 0 || tones[0] != t))
            tones[m++] = t;
        else if (m == 1 && tones[0] + 12 <= hi)
            tones[m++] = tones[0] + 12;
    }
    std::sort (tones, tones + m);
    if (m > 0 && r.next() < (double) kStabOctaveMove[st] && tones[m - 1] + 12 <= hi)
        for (int k = 0; k < m; ++k)
            tones[k] += 12;
    n = m;
    for (int k = 0; k < m; ++k)
        out[k] = tones[k];
    bottomOut = bottom;
}

// ------------------------------------------------------------- DEEP colour
// Catchiness research, DEEP: the chords are the hook. Jazz-house colours (minor 7, major 7,
// minor 9), the top note of each voicing moving by a step (1-2 semitones) between the two
// chords, the root left to the bass when the bass plays it, and no m2 / m9 against the bass
// under any hit. Nothing is stored or taken from a reference: the chord roots come from the
// model's chord-bottom chain (the unigram, transition and strength tables chooseVoicing
// reads), the quality from stacking thirds on that step of the roll's own mode, and the
// voicing is built here around the bass under the roll's own hits.

enum JazzQuality : int
{
    JazzM7 = 0,
    JazzM9,
    JazzMaj7,
    JazzQualities
};
// Preference among the colours a root allows: minor 9 is DEEP's signature minor colour.
constexpr float kJazzQualityW[JazzQualities] = { 0.35f, 0.45f, 0.20f };
// How a chord sits on the bass under one hit: the bass plays its root, another chord tone,
// nothing, or a note outside the chord (a pedal under it). The class weight is the mean.
constexpr float kJazzFitRoot = 1.f;
constexpr float kJazzFitTone = 0.75f;
constexpr float kJazzFitNone = 0.6f;
constexpr float kJazzFitPedal = 0.35f;
// Register preference for the top voice (semitones over the stab base): the band's upper middle.
constexpr int kJazzTopCentre = 14;
// Widest voicing (top to bottom, semitones): a drop voicing stays inside a 10th-ish hand.
constexpr int kJazzMaxSpan = 16;
// The top voice may reach a minor 3rd over the stab band (kStabRegHi): the key's iv minor 9
// (its 9th over its b3) and bVI major 7 (its 7th over its root) only fit with the top there.
constexpr int kJazzTopReach = 3;
constexpr int kJazzMaxChords = 32;
constexpr int kJazzMaxVoicings = 32;

struct JazzChord
{
    int root = 0;
    int quality = JazzM7;
    int n = 0;
    int iv[5] {};      // over the root, the stack in thirds: root, 3rd, 5th, 7th (, 9th)
    unsigned pcs = 0u; // pitch classes of the whole chord
};

struct JazzHit
{
    int cls = 0;            // which of the two chords (hit-by-hit alternation)
    int slot = 0;           // 16th in its bar
    int bassPc = -1;        // bass pitch class sounding at the onset, -1 for none
    unsigned bassMask = 0u; // every bass pitch class sounding during the hit
};

struct JazzVoicing
{
    int top = 0;
    int topIdx = 1;  // which chord tone is on top
    int pitch[5] {}; // per chord tone (index into JazzChord::iv), all under the top
};

// The diatonic seventh chord on pitch class `root`, stacked in thirds in the mode. True only
// for the DEEP colours: minor 7, minor 9 (the mode's 9th a whole step over the root) and
// major 7. Half-diminished and dominant stacks are not DEEP colours.
bool jazzChordOn (int root, int mode, int quality, JazzChord& c) noexcept
{
    int deg = -1;
    for (int d = 0; d < 7 && deg < 0; ++d)
        if (phrase::degreeToSemis (d, mode) == root)
            deg = d;
    if (deg < 0)
        return false;
    auto above = [&] (int k) { return phrase::degreeToSemis (deg + k, mode) - root; };
    const int third = above (2);
    const int fifth = above (4);
    const int seventh = above (6);
    const int ninth = above (8);
    const bool minor = third == 3 && fifth == 7 && seventh == 10;
    const bool major = third == 4 && fifth == 7 && seventh == 11;
    if (quality == JazzMaj7 ? ! major : (! minor || (quality == JazzM9 && ninth != 14)))
        return false;
    const int iv[5] = { 0, third, fifth, seventh, ninth };
    c.root = root;
    c.quality = quality;
    c.n = quality == JazzM9 ? 5 : 4;
    c.pcs = 0u;
    for (int k = 0; k < c.n; ++k)
    {
        c.iv[k] = iv[k];
        c.pcs |= 1u << (unsigned) pcOf (root + iv[k]);
    }
    return true;
}

// The chord tones written for one hit (bit k = c.iv[k]): the root goes to the bass when the
// bass plays it, and a minor 9 keeps four tones (its 5th goes while the root sounds).
unsigned jazzHitTones (const JazzChord& c, int bassPc) noexcept
{
    unsigned m = (1u << (unsigned) c.n) - 1u;
    if (bassPc == c.root)
        m &= ~1u;
    if (c.n == 5 && (m & 1u) != 0)
        m &= ~(1u << 2);
    return m;
}

// ...less every tone a semitone from a bass note sounding during the hit (no m2 / m9 / M7
// against the bass): the stab gives way to the bass, as fixUpperClashes does later.
unsigned jazzSounding (const JazzChord& c, const JazzHit& h) noexcept
{
    unsigned m = jazzHitTones (c, h.bassPc);
    for (int k = 0; k < c.n; ++k)
    {
        const int pc = pcOf (c.root + c.iv[k]);
        if ((h.bassMask & ((1u << (unsigned) ((pc + 1) % 12)) | (1u << (unsigned) ((pc + 11) % 12)))) != 0)
            m &= ~(1u << (unsigned) k);
    }
    return m;
}

// A chord can take a class when every hit keeps at least two tones after giving way to the
// bass. Returns how much of the chord survives: the mean share of tones kept times the share
// of hits that keep a 7th or 9th (the colour), squared; 0 when the chord cannot take the class.
float jazzKeep (const JazzChord& c, int cls, const JazzHit* hits, int nh) noexcept
{
    const unsigned colour = (1u << 3) | (c.n == 5 ? (1u << 4) : 0u);
    float sum = 0.f;
    int coloured = 0;
    int n = 0;
    for (int h = 0; h < nh; ++h)
    {
        if (hits[h].cls != cls)
            continue;
        const unsigned on = jazzSounding (c, hits[h]);
        const int kept = (int) std::bitset<5> (on).count();
        if (kept < 2)
            return 0.f;
        sum += (float) kept / (float) std::bitset<5> (jazzHitTones (c, hits[h].bassPc)).count();
        coloured += (on & colour) != 0 ? 1 : 0;
        ++n;
    }
    const float f = n > 0 ? (sum / (float) n) * ((float) coloured / (float) n) : 1.f;
    return f * f;
}

float jazzFit (const JazzChord& c, int cls, const JazzHit* hits, int nh) noexcept
{
    float sum = 0.f;
    int n = 0;
    for (int h = 0; h < nh; ++h)
    {
        if (hits[h].cls != cls)
            continue;
        const int b = hits[h].bassPc;
        sum += b < 0 ? kJazzFitNone
                     : (b == c.root ? kJazzFitRoot : ((c.pcs & (1u << (unsigned) b)) != 0 ? kJazzFitTone : kJazzFitPedal));
        ++n;
    }
    return n > 0 ? sum / (float) n : 1.f;
}

// Voicings of `c` with the top inside [lo, hi]: every tone in close position under the top,
// or up to two of them an octave lower (drop voicings, which open a 9th / 7th cluster into a
// 7th / 9th), spanning at most kJazzMaxSpan. Close voicings come first. The top is a colour
// tone (3rd, 7th, 9th; the 5th of a 7th chord), never the root, which the bass can take.
int jazzVoicings (const JazzChord& c, int lo, int hi, JazzVoicing* out) noexcept
{
    int n = 0;
    for (int drops = 0; drops <= 2; ++drops)
        for (int t = 1; t < c.n; ++t)
        {
            if (c.n == 5 && t == 2)
                continue;
            const int topPc = pcOf (c.root + c.iv[t]);
            for (int top = lo; top <= hi; ++top)
            {
                if (pcOf (top) != topPc)
                    continue;
                for (unsigned m = 0; m < (1u << (unsigned) c.n); ++m)
                {
                    if ((int) std::bitset<5> (m).count() != drops || (m & (1u << (unsigned) t)) != 0 || n >= kJazzMaxVoicings)
                        continue;
                    JazzVoicing v;
                    v.top = top;
                    v.topIdx = t;
                    int low = top;
                    for (int k = 0; k < c.n; ++k)
                    {
                        v.pitch[k] = top - pcOf (c.iv[t] - c.iv[k]) - ((m & (1u << (unsigned) k)) != 0 ? 12 : 0);
                        low = std::min (low, v.pitch[k]);
                    }
                    bool dropLow = false;
                    for (int k = 0; k < c.n; ++k)
                        dropLow = dropLow || ((m & (1u << (unsigned) k)) != 0 && v.pitch[k] < lo);
                    if (! dropLow && top - low <= kJazzMaxSpan)
                        out[n++] = v;
                }
            }
        }
    return n;
}

// A voicing can take a class when, on every hit of the class, its top tone sounds and every
// sounding tone sits at or over the register floor, with no two voices a minor 2nd or minor
// 9th apart over the class (a minor 9 keeps its 9th a major 7th over the b3, a major 7 its
// 7th a major 7th over the root).
bool jazzVoicingFits (const JazzChord& c, const JazzVoicing& v, int cls, int lo, const JazzHit* hits, int nh) noexcept
{
    unsigned used = 0u;
    for (int h = 0; h < nh; ++h)
    {
        if (hits[h].cls != cls)
            continue;
        const unsigned on = jazzSounding (c, hits[h]);
        if ((on & (1u << (unsigned) v.topIdx)) == 0)
            return false;
        used |= on;
    }
    for (int j = 0; j < c.n; ++j)
    {
        if ((used & (1u << (unsigned) j)) == 0)
            continue;
        if (v.pitch[j] < lo)
            return false;
        for (int k = 0; k < c.n; ++k)
            if ((used & (1u << (unsigned) k)) != 0 && (v.pitch[j] - v.pitch[k] == 1 || v.pitch[j] - v.pitch[k] == 13))
                return false;
    }
    return true;
}

// How far the voices travel from one voicing to the next (each tone to its nearest).
int jazzMotion (const JazzChord& a, const JazzVoicing& va, const JazzChord& b, const JazzVoicing& vb) noexcept
{
    int sum = 0;
    for (int k = 0; k < b.n; ++k)
    {
        int best = 99;
        for (int j = 0; j < a.n; ++j)
            best = std::min (best, std::abs (vb.pitch[k] - va.pitch[j]));
        sum += best;
    }
    return sum;
}

// How far a voicing reaches past an octave (0 for close position): stabs stay compact.
int jazzSpan (const JazzChord& c, const JazzVoicing& v) noexcept
{
    int low = v.top;
    for (int k = 0; k < c.n; ++k)
        low = std::min (low, v.pitch[k]);
    return std::max (0, v.top - low - 11);
}

bool jazzStep (const JazzVoicing& a, const JazzVoicing& b) noexcept
{
    const int d = std::abs (a.top - b.top);
    return d == 1 || d == 2;
}

int jazzStrength (int cls, const JazzHit* hits, int nh) noexcept
{
    int vote[3] {};
    for (int h = 0; h < nh; ++h)
        if (hits[h].cls == cls)
            ++vote[strengthOf (hits[h].slot)];
    int s = 1;
    for (int k = 0; k < 3; ++k)
        if (vote[k] > vote[s])
            s = k;
    return s;
}

// DEEP's two stab chords as jazz-house colours. Writes each hit's tones (ascending) and
// returns true, or false when no pair of colours fits the bass with a stepwise top voice
// (the caller then falls back to the palette voicings).
bool deepJazzStab (int mode, std::uint32_t seed, const JazzHit* hits, int nh, int (*tones)[4], int* nTones) noexcept
{
    const int st = styleOf (Long);
    const int lo = kStabRegLo[st];
    const int hi = kStabRegHi[st] + kJazzTopReach;
    JazzChord cand[kJazzMaxChords];
    int nc = 0;
    for (int root = 0; root < 12; ++root)
        for (int q = 0; q < JazzQualities && nc < kJazzMaxChords; ++q)
            if (jazzChordOn (root, mode, q, cand[nc]))
                ++nc;
    if (nc == 0)
        return false;
    // Per class (0 = the first chord's hits, 1 = the second's): which chords keep their
    // colour over the bass, how well they sit on it, and which voicings keep their top.
    bool clear[2][kJazzMaxChords] {};
    float weight[2][kJazzMaxChords] {};
    JazzVoicing voic[kJazzMaxChords][kJazzMaxVoicings];
    int nvoic[kJazzMaxChords] {};
    bool topOk[2][kJazzMaxChords][kJazzMaxVoicings] {};
    for (int k = 0; k < nc; ++k)
    {
        nvoic[k] = jazzVoicings (cand[k], lo, hi, voic[k]);
        for (int cls = 0; cls < 2; ++cls)
        {
            const float keep = jazzKeep (cand[k], cls, hits, nh);
            for (int v = 0; v < nvoic[k]; ++v)
            {
                topOk[cls][k][v] = keep > 0.f && jazzVoicingFits (cand[k], voic[k][v], cls, lo, hits, nh);
                clear[cls][k] = clear[cls][k] || topOk[cls][k][v];
            }
            weight[cls][k] = kJazzQualityW[cand[k].quality] * jazzFit (cand[k], cls, hits, nh) * keep;
        }
    }
    const int strength[2] = { jazzStrength (0, hits, nh), jazzStrength (1, hits, nh) };
    float rowU[12];
    for (int pc = 0; pc < 12; ++pc)
        rowU[pc] = kStabUnigram[st][pc];
    prepareRow (rowU, kStabUnigram[st], kFloorRowStab);
    auto choiceW = [&] (int cls, int pc) { return kStabChoice[st][std::clamp (strength[cls], 0, 2)][pc] + 1.0e-3f; };
    // A voicing pair: 0 unless both voicings fit their class and the top moves by a step;
    // then the smoothest inner motion, compact, near the register's upper middle, and over
    // the stab band only when the colour needs it.
    auto pairWeight = [&] (int a, int i, int b, int j) -> float
    {
        if (! topOk[0][a][i] || ! topOk[1][b][j] || ! jazzStep (voic[a][i], voic[b][j]))
            return 0.f;
        const JazzVoicing& va = voic[a][i];
        const JazzVoicing& vb = voic[b][j];
        const float motion = (float) jazzMotion (cand[a], va, cand[b], vb);
        const float reg = 1.f / (1.f + 0.2f * (float) (std::abs (va.top - kJazzTopCentre) + std::abs (vb.top - kJazzTopCentre)));
        const float open = 1.f / (1.f + 0.15f * (float) (jazzSpan (cand[a], va) + jazzSpan (cand[b], vb)));
        const float band = (va.top > kStabRegHi[st] ? 0.4f : 1.f) * (vb.top > kStabRegHi[st] ? 0.4f : 1.f);
        return reg * open * band / (1.f + 0.25f * motion);
    };

    Rng r (mix32 (seed, 0xDEE9C010u));
    bool tried[kJazzMaxChords] {};
    for (int attempt = 0; attempt < 6; ++attempt)
    {
        float w[kJazzMaxChords];
        bool legal[kJazzMaxChords];
        for (int k = 0; k < nc; ++k)
        {
            legal[k] = clear[0][k] && ! tried[k];
            w[k] = std::sqrt (rowU[cand[k].root] * choiceW (0, cand[k].root)) * weight[0][k];
        }
        const int a = pickWeighted (w, legal, nc, r);
        if (a < 0)
            return false;
        tried[a] = true;
        // The second chord follows the chord-bottom chain from the first, is a different
        // chord (pitch-class Jaccard under 0.75, as the palette pair), and has a voicing
        // whose top note is a step from one of the first chord's.
        float rowT[12];
        for (int pc = 0; pc < 12; ++pc)
            rowT[pc] = kStabTrans[st][cand[a].root][pc];
        prepareRow (rowT, kStabUnigram[st], kFloorRowStab);
        bool any = false;
        for (int k = 0; k < nc; ++k)
        {
            const unsigned both = cand[a].pcs & cand[k].pcs;
            const unsigned either = cand[a].pcs | cand[k].pcs;
            legal[k] = clear[1][k] && cand[k].root != cand[a].root
                    && (float) std::bitset<12> (both).count() < 0.75f * (float) std::bitset<12> (either).count();
            // The best voicing pair this chord allows weighs in, so a stepwise pair inside
            // the band is preferred over one that needs the top over it.
            float best = 0.f;
            for (int i = 0; i < nvoic[a] && legal[k]; ++i)
                for (int j = 0; j < nvoic[k]; ++j)
                    best = std::max (best, pairWeight (a, i, k, j));
            legal[k] = legal[k] && best > 0.f;
            any = any || legal[k];
            w[k] = std::sqrt (rowT[cand[k].root] * choiceW (1, cand[k].root)) * weight[1][k] * best;
        }
        if (! any)
            continue;
        const int b = pickWeighted (w, legal, nc, r);
        if (b < 0)
            continue;
        float total = 0.f;
        float pw[kJazzMaxVoicings * kJazzMaxVoicings] {};
        for (int i = 0; i < nvoic[a]; ++i)
            for (int j = 0; j < nvoic[b]; ++j)
            {
                pw[i * kJazzMaxVoicings + j] = pairWeight (a, i, b, j);
                total += pw[i * kJazzMaxVoicings + j];
            }
        if (total <= 0.f)
            continue;
        // Weighted draw; rounding that leaves x just over zero keeps the last pair with weight.
        float x = (float) r.next() * total;
        int vi = -1;
        int vj = -1;
        bool drawn = false;
        for (int i = 0; i < nvoic[a] && ! drawn; ++i)
            for (int j = 0; j < nvoic[b] && ! drawn; ++j)
            {
                const float pwij = pw[i * kJazzMaxVoicings + j];
                if (pwij <= 0.f)
                    continue;
                vi = i;
                vj = j;
                x -= pwij;
                drawn = x <= 0.f;
            }
        const JazzChord* chord[2] = { &cand[a], &cand[b] };
        const JazzVoicing* voicing[2] = { &voic[a][vi], &voic[b][vj] };
        for (int h = 0; h < nh; ++h)
        {
            const JazzChord& c = *chord[hits[h].cls & 1];
            const JazzVoicing& v = *voicing[hits[h].cls & 1];
            const unsigned on = jazzSounding (c, hits[h]);
            int m = 0;
            for (int k = 0; k < c.n && m < 4; ++k)
                if ((on & (1u << (unsigned) k)) != 0)
                    tones[h][m++] = v.pitch[k];
            std::sort (tones[h], tones[h] + m);
            nTones[h] = m;
        }
        return true;
    }
    return false;
}

} // namespace

// HARMONY 3 (Melak, Oct 20: "deeps chords sound the most like music"): DEEP's own chord writer
// (deepJazzStab, its tables unchanged) on a finished stab lane of any style. One chord per
// distinct (source bar, 16th) so restated bars keep identical chords; onsets, lengths and
// velocities are never touched.
bool jazzRevoice (Lane& stab, const Lane& bass, int mode, int bars, bool byHit, std::uint32_t seed) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    constexpr int kCap = kMaxBars * 4;
    struct Key
    {
        int src = 0;
        int slot = 0;
    };
    Key keys[kCap];
    int nk = 0;
    auto keyOf = [&] (const Note& n) noexcept
    {
        const int bar = std::clamp (phrase_lock::barOf (n.start), 0, nb - 1);
        Key k;
        k.src = nb >= 4 ? phrase_lock::sourceBar (bar, nb) : bar;
        k.slot = std::clamp (phrase_lock::slotIn (n.start, bar), 0, kBar - 1);
        return k;
    };
    auto find = [&] (const Key& k) noexcept
    {
        for (int i = 0; i < nk; ++i)
            if (keys[i].src == k.src && keys[i].slot == k.slot)
                return i;
        return -1;
    };
    for (int i = 0; i < stab.count; ++i)
    {
        if (stab.notes[i].vel <= 2)
            continue;
        const Key k = keyOf (stab.notes[i]);
        if (find (k) < 0)
        {
            if (nk >= kCap)
                return false;
            keys[nk++] = k;
        }
    }
    if (nk == 0)
        return false;
    std::sort (keys, keys + nk, [] (const Key& a, const Key& b) { return a.src != b.src ? a.src < b.src : a.slot < b.slot; });

    auto bassPcAt = [&] (float t) noexcept
    {
        int pc = -1;
        for (int i = 0; i < bass.count; ++i)
        {
            const Note& b = bass.notes[i];
            if (b.vel > 2 && b.start <= t + 1.0e-3f && b.start + b.length > t + 1.0e-3f)
                pc = pcOf (b.chord[0]);
        }
        return pc;
    };
    JazzHit jh[kCap];
    for (int k = 0; k < nk; ++k)
    {
        jh[k].cls = byHit ? (k & 1) : (keys[k].src & 1);
        jh[k].slot = keys[k].slot;
        jh[k].bassPc = -2;
        jh[k].bassMask = 0u;
    }
    for (int i = 0; i < stab.count; ++i)
    {
        const Note& n = stab.notes[i];
        if (n.vel <= 2)
            continue;
        const int k = find (keyOf (n));
        const int pc = bassPcAt (n.start);
        jh[k].bassPc = jh[k].bassPc == -2 ? pc : (jh[k].bassPc == pc ? pc : -1);
        const float a = n.start;
        const float b = n.start + std::max (0.25f, n.length);
        for (int j = 0; j < bass.count; ++j)
        {
            const Note& bn = bass.notes[j];
            if (bn.vel > 2 && bn.start < b - 1.0e-3f && a < bn.start + bn.length - 1.0e-3f)
                jh[k].bassMask |= 1u << (unsigned) pcOf (bn.chord[0]);
        }
    }
    for (int k = 0; k < nk; ++k)
        if (jh[k].bassPc == -2)
            jh[k].bassPc = -1;
    int tones[kCap][4] {};
    int nTones[kCap] {};
    if (! deepJazzStab (mode, seed, jh, nk, tones, nTones))
        return false;
    for (int i = 0; i < stab.count; ++i)
    {
        Note& n = stab.notes[i];
        if (n.vel <= 2)
            continue;
        const int k = find (keyOf (n));
        const int m = std::clamp (nTones[k], 1, 4);
        n.nChord = (std::uint8_t) m;
        for (int c = 0; c < 4; ++c)
            n.chord[c] = (std::int8_t) (c < m ? tones[k][c] : 0);
    }
    return true;
}

int drawMode (int dialect, float dark, std::uint32_t bassSeed) noexcept
{
    const int st = styleOf (dialect);
    dark = std::clamp (dark, 0.f, 1.f);
    float w[3];
    for (int m = 0; m < 3; ++m)
        w[m] = kModeWeights[st][m];
    w[phrase::Dorian] *= std::max (0.f, 1.f + kModeDarkTilt * (0.5f - dark));
    w[phrase::Phrygian] *= std::max (0.f, 1.f + kModeDarkTilt * (dark - 0.5f));
    Rng r ((bassSeed ? bassSeed : 1u) ^ 0x3D0DE5u);
    float sum = w[0] + w[1] + w[2];
    if (sum <= 0.f)
        return phrase::Aeolian;
    float x = (float) r.next() * sum;
    for (int m = 0; m < 3; ++m)
    {
        if (w[m] <= 0.f)
            continue;
        x -= w[m];
        if (x <= 0.f)
            return m;
    }
    return w[phrase::Aeolian] > 0.f ? phrase::Aeolian : phrase::Phrygian;
}

int composeBass (Lane& lane, const BassParams& p, std::uint32_t seed)
{
    const int st = styleOf (p.dialect);
    Rng r (mix32 (seed ? seed : 1u, 0xB455u + (std::uint32_t) st));
    const int nb = p.bars >= 8 ? 8 : (p.bars >= 4 ? 4 : 2);
    BassTables t;
    prepareBass (st, t, p.match);
    int arc = 0;
    for (int k = 0; k < kComposeRetries; ++k)
    {
        bool kickOk = true;
        arc = composeOnce (lane, p, t, r, kickOk);
        if (kickOk && ! thinRepeatedBackHalf (lane, nb) && onsetEntropyOk (lane, nb) && tonicGuardOk (lane, st)
            && duplicateOnsets (lane) == 0)
            break;
    }
    if (! tonicGuardOk (lane, st))
        repairTonic (lane, st);
    return arc;
}

int sampleDegreeSemis (int dialect, int mode, int prevSemi, std::uint32_t hash) noexcept
{
    const int st = styleOf (dialect);
    Rng r (hash ? hash : 1u);
    float w[12];
    bool legal[12];
    float row[12];
    for (int c = 0; c < 12; ++c)
        row[c] = kBassTrans[st][pcOf (prevSemi)][c];
    prepareRow (row, kBassUnigram[st], kFloorRowBass);
    for (int pc = 0; pc < 12; ++pc)
    {
        w[pc] = row[pc];
        legal[pc] = phrase::inMode (pc, mode);
    }
    const int pc = pickWeighted (w, legal, 12, r);
    return pc < 0 ? 0 : pc;
}

int sampleBassSlot (int dialect, int group, const bool* allowed, std::uint32_t hash) noexcept
{
    const int st = styleOf (dialect);
    BassTables t;
    prepareBass (st, t);
    Rng r (hash ? hash : 1u);
    bool legal[kBar];
    for (int s = 0; s < kBar; ++s)
        legal[s] = allowed == nullptr || allowed[s];
    return pickWeighted (t.grid[std::clamp (group, 0, 2)], legal, kBar, r);
}

void stabVoicing (int dialect, int mode, std::uint32_t hash, int* out, int& n) noexcept
{
    Rng r (hash ? hash : 1u);
    int bottom = 0;
    chooseVoicing (styleOf (dialect), mode, 0u, -1, 1, r, out, n, bottom);
}

void composeStab (Lane& lane, const Lane& bass, const StabParams& p, std::uint32_t seed)
{
    lane.count = 0;
    const int st = styleOf (p.dialect);
    const int nb = std::clamp (p.bars, 1, kMaxBars);
    const int per = std::clamp (p.perBar, 0, kStabHitsCap);
    if (per <= 0)
        return;

    auto covers = [&] (int abs)
    {
        for (int i = 0; i < bass.count; ++i)
        {
            const int a = (int) std::lround (bass.notes[i].start);
            const int b = (int) std::lround (bass.notes[i].start + bass.notes[i].length);
            if (abs >= a && abs < b)
                return true;
        }
        return false;
    };
    auto onset = [&] (int abs)
    {
        for (int i = 0; i < bass.count; ++i)
            if ((int) std::lround (bass.notes[i].start) == abs)
                return true;
        return false;
    };
    auto bassPcAt = [&] (int abs) -> int
    {
        int pc = -1;
        for (int i = 0; i < bass.count; ++i)
        {
            const float a = bass.notes[i].start;
            if (a <= (float) abs + 1.0e-3f && a + bass.notes[i].length > (float) abs + 1.0e-3f)
                pc = pcOf (bass.notes[i].chord[0]);
        }
        return pc;
    };

    // The stab answers a bass phrase end (a rest of >= 2 16ths after >= 2
    // notes) k 16ths later, k from the model's answer distribution.
    bool answer[kMaxSteps] {};
    {
        int run = 0;
        for (int i = 0; i < bass.count; ++i)
        {
            ++run;
            const int end = (int) std::lround (bass.notes[i].start + bass.notes[i].length);
            const int next = i + 1 < bass.count ? (int) std::lround (bass.notes[i + 1].start) : nb * kBar;
            if (next - end < 2)
                continue;
            if (run >= 2)
            {
                const std::uint32_t key = p.repeatByParity ? (std::uint32_t) (end % 32) : (std::uint32_t) end;
                Rng ra (mix32 (seed, 0xA45u + key));
                const int k = pickAll (kStabAnswer[st], 5, ra);
                if (k > 0 && end + k < nb * kBar && end + k < kMaxSteps)
                    answer[end + k] = true;
            }
            run = 0;
        }
    }

    struct Hit
    {
        int bar;
        int slot;
        float len;
    };
    Hit hits[kMaxBars * 4];
    int nh = 0;
    unsigned bassMask[2] {};
    int strengthVote[2][3] {};

    int placedAll = 0;
    int onBeat = 0;
    const int plannedHits = std::clamp (per, 1, kStabHitsCap) * (p.skipOdd ? (nb + 1) / 2 : nb);
    // Per-roll copies of the stab onset weights with the model's onset entropy floor.
    float given[3][kBar];
    for (int c = 0; c < 3; ++c)
    {
        float mean = 0.f;
        for (int s = 0; s < kBar; ++s)
        {
            given[c][s] = kStabGiven[st][c][s];
            mean += given[c][s] / (float) kBar;
        }
        float flat[kBar];
        for (float& x : flat)
            x = mean;
        enforceFloor (given[c], flat, kBar, kFloorOnsetStab);
    }
    for (int bar = 0; bar < nb; ++bar)
    {
        if (p.skipOdd && (bar & 1) != 0)
            continue;
        const int par = bar & 1;
        Rng r (mix32 (seed, p.repeatByParity ? 0x100u + (std::uint32_t) par : 0x200u + (std::uint32_t) bar));
        int cls[kBar];
        for (int s = 0; s < kBar; ++s)
        {
            const int abs = bar * kBar + s;
            if (onset (abs))
                cls[s] = 2;
            else if (covers (abs))
                cls[s] = 1;
            else
                cls[s] = 0;
        }
        const int k = std::max (1, per - (((p.lighterMask >> bar) & 1u) != 0 ? 1 : 0));
        int gapN = (int) std::floor ((float) k * kStabGapShare[st] + (float) r.next());
        int onN = (int) std::floor ((float) k * kStabOnsetShare[st] + (float) r.next());
        gapN = std::clamp (gapN, 0, k);
        onN = std::clamp (onN, 0, k - gapN);
        const int susN = k - gapN - onN;
        const int quota[3] = { gapN, susN, onN };
        // Fallback when a class has no free slot: never onto a bass attack
        // unless the style places stabs there.
        const bool onsetOk = kStabOnsetShare[st] > 0.f;
        const int order[3][3] = { { 0, 1, 2 }, { 1, 0, 2 }, { 2, 1, 0 } };

        float hold = p.length;
        if (p.varyLength)
        {
            const float lens[3] = { 1.5f, 2.f, 2.5f };
            hold = lens[r.nextU() % 3u];
        }
        bool taken[kBar] {};
        int placed = 0;
        for (int c = 0; c < 3; ++c)
        {
            for (int q = 0; q < quota[c]; ++q)
            {
                int pick = -1;
                for (int f = 0; f < 3 && pick < 0; ++f)
                {
                    const int want = order[c][f];
                    if (want == 2 && ! onsetOk)
                        continue;
                    float w[kBar];
                    bool legal[kBar];
                    // Beats (kick steps) take stabs too, up to the style's on-beat
                    // ceiling of the roll's hits so far (A&R: DEEP 20%, JACKIN 30%).
                    // Measured against the roll's planned hits, so bars of one parity
                    // make the same choice (JACKIN's 2-bar stab repeat).
                    const bool beatOk = (float) (onBeat + 1) <= kStabOnBeatCeil[st] * (float) std::max (1, plannedHits);
                    for (int s = 0; s < kBar; ++s)
                    {
                        legal[s] = cls[s] == want && ! taken[s] && (beatOk || ! kickStep (s));
                        w[s] = cls[s] < 0 ? 0.f : given[cls[s]][s];
                        if (cls[s] == 0)
                            w[s] *= kStabGapWeight;
                        if (answer[bar * kBar + s])
                            w[s] *= kStabAnswerBoost;
                        if (s >= 14 && (bar == 3 || bar == 7))
                            w[s] *= kAnswerTailFactor;
                    }
                    pick = pickWeighted (w, legal, kBar, r);
                }
                if (pick < 0 || placed >= kStabHitsCap)
                    continue;
                const float start = (float) (bar * kBar + pick);
                const float len = std::min (hold, (float) ((bar + 1) * kBar) - start);
                if (len < 0.5f || nh >= kMaxBars * 4)
                    continue;
                taken[pick] = true;
                ++placed;
                ++placedAll;
                if (kickStep (pick))
                    ++onBeat;
                hits[nh++] = { bar, pick, len };
                const int bpc = bassPcAt (bar * kBar + pick);
                if (bpc >= 0)
                    bassMask[par] |= 1u << bpc;
                ++strengthVote[par][strengthOf (pick)];
            }
        }
    }

    // Two voicings per roll, one per bar parity, each clear of a semitone against
    // the bass under its hits. The second is a different chord, not the first give
    // or take one note (pitch-class Jaccard under 0.75): one chord on every hit
    // lined up with a reference's stab line (the originality voicing match).
    auto pcMask = [] (const int* tones, int n) noexcept
    {
        unsigned m = 0;
        for (int k = 0; k < n; ++k)
            m |= 1u << (unsigned) pcOf (tones[k]);
        return m;
    };
    auto popc = [] (unsigned m) noexcept
    {
        int c = 0;
        for (; m != 0; m &= m - 1)
            ++c;
        return c;
    };
    int voi[2][4] {};
    int nv[2] {};
    int bottom0 = 0;
    // ROLLER and DEEP alternate the two chords hit by hit (below), so each has to
    // clear the bass under every hit, not only its own bar parity.
    const bool byHit = st == styleOf (UkMin) || st == styleOf (Long);
    const unsigned allBass = bassMask[0] | bassMask[1];

    // ROLLER and DEEP: the two chords alternate hit by hit in time order. Their
    // few hits can all fall in bars of one parity, which left one chord on every
    // hit. BOUNCY and JACKIN keep one chord per bar parity: their busier stab
    // line alternating hit by hit reads as a figure.
    int order[kMaxNotes];
    for (int h = 0; h < nh; ++h)
        order[h] = h;
    std::sort (order, order + nh, [&] (int a, int b)
    {
        return hits[a].bar * kBar + hits[a].slot < hits[b].bar * kBar + hits[b].slot;
    });
    int turn[kMaxNotes] {};
    for (int k = 0; k < nh; ++k)
        turn[order[k]] = byHit ? (k & 1) : (hits[order[k]].bar & 1);

    // DEEP: jazz-house colours with a stepwise top voice (deepJazzStab above); the
    // palette pair below stays the fallback when no colour pair clears the bass.
    bool jazz = false;
    int jazzTones[kMaxBars * 4][4] {};
    int jazzN[kMaxBars * 4] {};
    if (st == styleOf (Long) && nh > 0)
    {
        JazzHit jh[kMaxBars * 4];
        for (int h = 0; h < nh; ++h)
        {
            const float a = (float) (hits[h].bar * kBar + hits[h].slot);
            const float b = a + hits[h].len;
            jh[h].cls = turn[h];
            jh[h].slot = hits[h].slot;
            jh[h].bassPc = bassPcAt (hits[h].bar * kBar + hits[h].slot);
            jh[h].bassMask = 0u;
            for (int i = 0; i < bass.count; ++i)
            {
                const Note& bn = bass.notes[i];
                if (bn.vel > 2 && bn.start < b - 1.0e-3f && a < bn.start + bn.length - 1.0e-3f)
                    jh[h].bassMask |= 1u << (unsigned) pcOf (bn.chord[0]);
            }
        }
        jazz = deepJazzStab (p.mode, seed, jh, nh, jazzTones, jazzN);
    }
    for (int par = 0; par < 2 && ! jazz; ++par)
    {
        int strength = 1;
        for (int k = 0; k < 3; ++k)
            if (strengthVote[par][k] > strengthVote[par][strength])
                strength = k;
        int bestV[4] {};
        int bestN = 0;
        float bestJ = 2.f;
        for (int t = 0; t < 12; ++t)
        {
            Rng rv (mix32 (seed, (par == 0 ? 0x570Bu : 0x571Bu) + (std::uint32_t) t * 31u));
            int bottom = 0;
            chooseVoicing (st, p.mode, byHit ? allBass : bassMask[par], par == 0 ? -1 : bottom0, strength, rv, voi[par], nv[par], bottom);
            if (par == 0)
            {
                bottom0 = bottom;
                break;
            }
            const unsigned a = pcMask (voi[0], nv[0]);
            const unsigned b = pcMask (voi[1], nv[1]);
            const float j = (float) popc (a & b) / (float) std::max (1, popc (a | b));
            if (j < bestJ)
            {
                bestJ = j;
                bestN = nv[1];
                for (int k = 0; k < 4; ++k)
                    bestV[k] = voi[1][k];
            }
            if (j < 0.75f)
                break;
        }
        if (par == 1)
        {
            nv[1] = bestN;
            for (int k = 0; k < 4; ++k)
                voi[1][k] = bestV[k];
        }
    }

    for (int h = 0; h < nh && lane.count < kMaxNotes; ++h)
    {
        const int par = turn[h];
        const int nt = jazz ? jazzN[h] : nv[par];
        Note n;
        n.start = (float) (hits[h].bar * kBar + hits[h].slot);
        n.length = hits[h].len;
        n.nChord = (std::uint8_t) std::clamp (nt, 1, 4);
        for (int t = 0; t < 4; ++t)
            n.chord[t] = (std::int8_t) (t < nt ? (jazz ? jazzTones[h][t] : voi[par][t]) : 0);
        n.vel = (std::uint8_t) std::clamp (p.vel, 1, 127);
        lane.notes[lane.count++] = n;
    }
    std::sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
}

bool thinRepeatedBackHalf (const Lane& lane, int bars) noexcept
{
    const int nb = std::clamp (bars, 1, kMaxBars);
    if (nb < 4)
        return false;
    int thin = 0;
    for (int b = nb / 2; b < nb; ++b)
    {
        int attacks = 0;
        int firstPc = -1;
        bool onePc = true;
        for (int i = 0; i < lane.count; ++i)
        {
            const Note& n = lane.notes[i];
            if (n.vel <= 2)
                continue;
            const int st = (int) std::lround (n.start);
            if (st < b * kBar || st >= (b + 1) * kBar)
                continue;
            ++attacks;
            const int pc = pcOf (n.chord[0]);
            if (firstPc < 0)
                firstPc = pc;
            else if (pc != firstPc)
                onePc = false;
        }
        if (attacks >= 1 && attacks <= kThinBarMaxAttacks && onePc)
            ++thin;
    }
    return thin >= kThinBarsToFail;
}

float kickShare (const Lane& lane) noexcept
{
    if (lane.count <= 0)
        return 0.f;
    int k = 0;
    for (int i = 0; i < lane.count; ++i)
        if (kickStep (((int) std::lround (lane.notes[i].start)) % kBar))
            ++k;
    return (float) k / (float) lane.count;
}

int duplicateOnsets (const Lane& lane) noexcept
{
    int d = 0;
    for (int i = 0; i < lane.count; ++i)
        for (int j = 0; j < i; ++j)
            if (std::lround (lane.notes[i].start) == std::lround (lane.notes[j].start))
            {
                ++d;
                break;
            }
    return d;
}

bool tonicGuardOk (const Lane& lane, int dialect) noexcept
{
    if (lane.count <= 0)
        return true;
    float t[12];
    float total = 0.f;
    pcTime (lane, t, total);
    if (total <= 0.f)
        return true;
    float other = 0.f;
    for (int pc = 1; pc < 12; ++pc)
    {
        if (t[pc] > t[0] + 1.0e-4f)
            return false;
        other = std::max (other, t[pc]);
    }
    return (t[0] + other) / total >= kTop2Min[styleOf (dialect)] - 1.0e-4f;
}

float medianLength (const Lane& lane) noexcept
{
    if (lane.count <= 0)
        return 0.f;
    float v[kMaxNotes];
    const int n = std::min (lane.count, kMaxNotes);
    for (int i = 0; i < n; ++i)
        v[i] = lane.notes[i].length;
    std::sort (v, v + n);
    return (n & 1) ? v[n / 2] : 0.5f * (v[n / 2 - 1] + v[n / 2]);
}

} // namespace rolla::rules
