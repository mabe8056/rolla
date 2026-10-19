#include "RuleComposer.h"
#include "RuleTables.h"

#include <algorithm>
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
};

void prepareBass (int st, BassTables& t) noexcept
{
    for (int g = 0; g < 3; ++g)
    {
        float kick = 0.f;
        float off = 0.f;
        for (int s = 0; s < kBar; ++s)
        {
            t.grid[g][s] = kBassOnset[st][g][s];
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
        enforceFloor (t.grid[g], flat, kBar, kFloorOnsetBass);
    }
    for (int r = 0; r < 12; ++r)
    {
        for (int c = 0; c < 12; ++c)
            t.trans[r][c] = kBassTrans[st][r][c];
        prepareRow (t.trans[r], kBassUnigram[st], kFloorRowBass);
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
// steps stay under the 35% cap bar by bar.
void drawOnsets (const float* w, int count, int limit, Rng& r, bool* on)
{
    count = std::clamp (count, 1, 10);
    for (int guard = 0; guard < 16 && countOn (on) < count; ++guard)
    {
        int kicks = 0;
        for (int s = 0; s < kBar; s += 4)
            if (on[s])
                ++kicks;
        const bool kickOk = (float) (kicks + 1) <= kBassKickHardCap * (float) count;
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
    float w[12];
    bool legal[12];
    const int k = strengthOf (slot);
    for (int pc = 0; pc < 12; ++pc)
    {
        float x = t.trans[prevPc][pc] * (kBassChoice[st][k][pc] + 1.0e-3f);
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
        if (first && callStart && allowRoot && r.next() < (double) kFirstRoot[st])
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
    drawOnsets (w, front + std::clamp (backCount, 1, 6), limit, r, b.on);
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
                drawOnsets (w, want, kTurnRestFrom, r, b.on);
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
            endOn (kBassEndBar8[st], mode, r, b);
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
            drawOnsets (w, 2, limit, r, bar.on);
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

    // Bar 1: the call.
    Bar& call = bars[0];
    drawOnsets (t.grid[0], drawCount (st, 0, scale, r), kBar, r, call.on);
    pitchBar (st, p.mode, p.dark, t, call, 0, 0, true, false, r);

    // Bar 2: the answer.
    Bar& ans = bars[1];
    {
        float w[kBar];
        int from = 0;
        if (st == styleOf (Jackin))
        {
            for (int s = 0; s < kBar; ++s)
                w[s] = t.grid[0][s] * (call.on[s] ? kAnswerCallSlotWeight[st] : kAnswerSilentBoost[st]);
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
        drawOnsets (w, want, kBar, r, ans.on);
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
            const int delta = std::clamp (drawCount (st, b, scale, r) - barCountOf (bars[b]), -2, 2);
            const bool vary = j < 2 && r.next() < (double) kBackHalfVariation[st];
            if (delta == 0 && ! vary)
                continue;
            redrawBack (st, p.mode, p.dark, t, t.grid[0], std::max (1, countOn (bars[b].on, 8, kBar) + delta), kBar, r, bars[b]);
        }
        // Bar 4 answers bar 2 in its back half.
        if (r.next() < (double) kBar4AnswerProb[st])
        {
            for (int tries = 0; tries < 4; ++tries)
            {
                Bar a = bars[3];
                const int delta = std::clamp (pickAll (kBassDeltaBar4[st], 9, r) - 4, -3, 3);
                redrawBack (st, p.mode, p.dark, t, t.grid[1], std::max (1, countOn (bars[1].on, 8, kBar) + delta), kBar, r, a);
                endOn (kBassEndBar4[st], p.mode, r, a);
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

} // namespace

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
    prepareBass (st, t);
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

    // Two voicings per roll, one per bar parity, never the same chord twice,
    // each clear of a semitone against the bass under its hits.
    int voi[2][4] {};
    int nv[2] {};
    int bottom0 = 0;
    for (int par = 0; par < 2; ++par)
    {
        int strength = 1;
        for (int k = 0; k < 3; ++k)
            if (strengthVote[par][k] > strengthVote[par][strength])
                strength = k;
        for (int t = 0; t < 6; ++t)
        {
            Rng rv (mix32 (seed, (par == 0 ? 0x570Bu : 0x571Bu) + (std::uint32_t) t * 31u));
            int bottom = 0;
            chooseVoicing (st, p.mode, bassMask[par], par == 0 ? -1 : bottom0, strength, rv, voi[par], nv[par], bottom);
            if (par == 0)
            {
                bottom0 = bottom;
                break;
            }
            bool same = nv[1] == nv[0];
            for (int k = 0; same && k < nv[0]; ++k)
                same = voi[1][k] == voi[0][k];
            if (! same)
                break;
        }
    }

    for (int h = 0; h < nh && lane.count < kMaxNotes; ++h)
    {
        const int par = hits[h].bar & 1;
        Note n;
        n.start = (float) (hits[h].bar * kBar + hits[h].slot);
        n.length = hits[h].len;
        n.nChord = (std::uint8_t) std::clamp (nv[par], 1, 4);
        for (int t = 0; t < 4; ++t)
            n.chord[t] = (std::int8_t) (t < nv[par] ? voi[par][t] : 0);
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
