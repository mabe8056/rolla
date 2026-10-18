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

// Weighted draw over n <= 32 entries with the entropy floor: each legal entry
// gets (1 - floor) * w + floor * mean(w). -1 when nothing is legal.
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
        adj[i] = tot > 0.f ? (1.f - kEntropyFloor) * std::max (0.f, w[i]) + kEntropyFloor * mean : 1.f;
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

// Triangular draw in [-1, 1].
float tri (Rng& r) noexcept
{
    return (float) (r.next() + r.next() - 1.0);
}

// ------------------------------------------------------------------ rhythm

float slotWeight (int st, int slot, int bar, int nb) noexcept
{
    float w;
    if (kickStep (slot))
    {
        // The beat class at the composer's raw kick share: k / (1 - k) of the
        // off-kick classes, so about k of the attacks land on a kick.
        const float off = kSlotClass[st][1] + kSlotClass[st][2] + kSlotClass[st][3];
        const float k = kKickShareRaw[st];
        w = off * k / std::max (0.05f, 1.f - k);
    }
    else
        w = kSlotClass[st][slot & 3];
    if (slot == 0)
        w *= kDownbeatFactor;
    if (slot >= 14 && (bar == 3 || bar == 7) && bar != nb - 1)
        w *= kAnswerTailFactor;
    return w;
}

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

// Adds onsets to `on` until it holds `count` (or nothing is legal). Slots at
// or past `limit` stay silent; bias (nullable) multiplies the slot weight.
void drawOnsets (int st, int bar, int nb, int count, const float* bias, int limit, Rng& r, bool* on)
{
    count = std::clamp (count, 1, 10);
    for (int guard = 0; guard < 16 && countOn (on) < count; ++guard)
    {
        const int have = countOn (on);
        int kicks = 0;
        for (int s = 0; s < kBar; s += 4)
            if (on[s])
                ++kicks;
        // At most about a third of a bar's attacks on kicks (the 35% cap holds bar by bar).
        const bool kickOk = (float) (kicks + 1) <= 0.34f * (float) count;
        float w[kBar];
        bool legal[kBar];
        for (int s = 0; s < kBar; ++s)
        {
            w[s] = slotWeight (st, s, bar, nb) * (bias != nullptr ? bias[s] : 1.f);
            legal[s] = ! on[s] && s < limit && ! makesRun (on, s) && (! kickStep (s) || kickOk);
        }
        const int pick = pickWeighted (w, legal, kBar, r);
        if (pick < 0)
            break;
        on[pick] = true;
        (void) have;
    }
}

int barCount (int st, Rng& r, int delta) noexcept
{
    const float n = kOnsetsPerBar[st] + tri (r) * kOnsetSpread[st] + (float) delta;
    return std::clamp ((int) std::lround (n), 3, 8);
}

// ------------------------------------------------------------------ pitch

void degreeWeights (int st, int mode, float dark, int prevSemi, float* w) noexcept
{
    float e[7];
    for (int d = 0; d < 7; ++d)
        e[d] = kDegreeEmphasis[st][d];
    if (dark <= 0.2f)
    {
        e[4] *= kBrightFifth;
        e[2] *= 0.5f;
    }
    else if (dark >= 0.8f)
    {
        e[2] *= kDarkThird;
        e[1] *= kDarkSecond;
        e[4] *= 0.6f;
    }
    const int prevPc = pcOf (prevSemi);
    for (int d = 0; d < 7; ++d)
    {
        const int semi = phrase::degreeToSemis (d, mode);
        const int dist = std::abs (semi - prevPc);
        w[d] = e[d] * kIntervalPref[st][std::clamp (dist, 0, 12)];
    }
}

int stepDegree (int st, int mode, float dark, int prevSemi, bool allowRoot, Rng& r) noexcept
{
    float w[7];
    bool legal[7];
    degreeWeights (st, mode, dark, prevSemi, w);
    for (int d = 0; d < 7; ++d)
        legal[d] = d != 0 || allowRoot;
    const int d = pickWeighted (w, legal, 7, r);
    return phrase::degreeToSemis (d < 0 ? 0 : d, mode);
}

// Pitches the onsets of one bar from `from` on. Slots below `from` keep the
// semis already in `semi`. The reaction figure (root on 16ths 2 and 4) is never
// drawn. lastOpen: the bar's last onset avoids the root.
void pitchBar (int st, int mode, float dark, const bool* on, int from, int prevSemi, bool callStart,
               bool lastOpen, Rng& r, int* semi)
{
    int last = -1;
    for (int s = 0; s < kBar; ++s)
        if (on[s])
            last = s;
    int prev = prevSemi;
    bool first = true;
    for (int s = 0; s < kBar; ++s)
    {
        if (! on[s])
            continue;
        if (s < from)
        {
            prev = semi[s];
            first = false;
            continue;
        }
        bool allowRoot = ! (lastOpen && s == last);
        if (s == 3 && on[1] && pcOf (semi[1]) == 0)
            allowRoot = false;
        if (first && callStart && allowRoot && r.next() < (double) kFirstRoot[st])
            semi[s] = 0;
        else
            semi[s] = stepDegree (st, mode, dark, prev, allowRoot, r);
        prev = semi[s];
        first = false;
    }
}

int lastSemi (const bool* on, const int* semi, int fallback) noexcept
{
    int v = fallback;
    for (int s = 0; s < kBar; ++s)
        if (on[s])
            v = semi[s];
    return v;
}

struct Bar
{
    bool on[kBar] {};
    int semi[kBar] {};
};

// Redraws slots 8-15 of `b` to `backCount` attacks (the back-half variation).
void redrawBack (int st, int mode, float dark, int bar, int nb, int backCount, Rng& r, Bar& b)
{
    float bias[kBar];
    for (int s = 0; s < kBar; ++s)
        bias[s] = (s >= 8 && b.on[s]) ? 0.5f : 1.f;
    const int front = countOn (b.on, 0, 8);
    int prevFront = 0;
    for (int s = 0; s < 8; ++s)
        if (b.on[s])
            prevFront = b.semi[s];
    for (int s = 8; s < kBar; ++s)
        b.on[s] = false;
    // Front slots are fixed: only the back half can take new attacks.
    float w[kBar];
    for (int s = 0; s < kBar; ++s)
        w[s] = s < 8 ? 0.f : bias[s];
    drawOnsets (st, bar, nb, front + std::clamp (backCount, 1, 6), w, kBar, r, b.on);
    pitchBar (st, mode, dark, b.on, 8, prevFront, false, false, r, b.semi);
}

void turnBar (Rng& r, Bar& b)
{
    for (int s = kTurnRestFrom; s < kBar; ++s)
        b.on[s] = false;
    if (countOn (b.on) > 2 && r.next() < (double) kTurnDropOne)
    {
        for (int s = kTurnRestFrom - 1; s >= 0; --s)
            if (b.on[s])
            {
                b.on[s] = false;
                break;
            }
    }
    if (countOn (b.on) == 0)
    {
        b.on[2] = true;
        b.semi[2] = 0;
    }
}

void writeLane (Lane& lane, const Bar* bars, int nb, int st, Rng& r)
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
            float lw[kLenChoices];
            bool legal[kLenChoices];
            for (int k = 0; k < kLenChoices; ++k)
            {
                lw[k] = kLenWeights[st][k];
                legal[k] = true;
            }
            const int k = pickWeighted (lw, legal, kLenChoices, r);
            n.length = kLenMenu[k < 0 ? 2 : k];
            lane.notes[lane.count++] = n;
            firstInBar = false;
        }
    }
    // Lengths never reach the next attack, the loop end, or (for a b3 late in
    // a bar) the barline.
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        float next = i + 1 < lane.count ? lane.notes[i + 1].start : loopEnd;
        const int st16 = (int) std::lround (n.start);
        if (st16 % kBar >= 14 && pcOf (n.chord[0]) == 3)
            next = std::min (next, (float) ((st16 / kBar + 1) * kBar));
        // The last bar's turn rest stays silent.
        if (st16 / kBar == nb - 1)
            next = std::min (next, (float) ((nb - 1) * kBar + kTurnRestFrom));
        const float room = next - n.start;
        n.length = std::max (0.5f, std::min (n.length, room));
        if (n.length > room)
            n.length = room;
    }
}

// Hard rule: no more than 35% of the attacks on kick steps. The latest kick
// attack moves one 16th later when that slot is free, else it goes.
void enforceKickCap (Lane& lane, int nb)
{
    for (int guard = 0; guard < kMaxNotes && lane.count > 0; ++guard)
    {
        int kicks = 0;
        for (int i = 0; i < lane.count; ++i)
            if (kickStep (((int) std::lround (lane.notes[i].start)) % kBar))
                ++kicks;
        if ((float) kicks <= 0.35f * (float) lane.count + 1.0e-4f)
            return;
        int victim = -1;
        for (int i = lane.count - 1; i >= 0 && victim < 0; --i)
            if (kickStep (((int) std::lround (lane.notes[i].start)) % kBar))
                victim = i;
        if (victim < 0)
            return;
        const int st = (int) std::lround (lane.notes[victim].start);
        const int to = st + 1;
        bool free = to < nb * kBar;
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

int composeOnce (Lane& lane, const BassParams& p, Rng& r)
{
    const int st = styleOf (p.dialect);
    const int nb = p.bars >= 8 ? 8 : (p.bars >= 4 ? 4 : 2);
    Bar bars[8];

    // Density arc for bars 5-7.
    bool arcLegal[kArcShapes];
    for (int a = 0; a < kArcShapes; ++a)
        arcLegal[a] = true;
    int arc = pickWeighted (kArcWeights[st], arcLegal, kArcShapes, r);
    if (arc < 0)
        arc = 0;

    // Bar 1: the call.
    Bar& call = bars[0];
    drawOnsets (st, 0, nb, barCount (st, r, 0), nullptr, kBar, r, call.on);
    pitchBar (st, p.mode, p.dark, call.on, 0, 0, true, false, r, call.semi);

    // Bar 2: the answer.
    Bar& ans = bars[1];
    {
        float bias[kBar];
        int from = 0;
        if (st == styleOf (Gaskin))
        {
            for (int s = 0; s < kBar; ++s)
                bias[s] = call.on[s] ? kAnswerCallSlotWeight[st] : kAnswerSilentBoost[st];
        }
        else if (r.next() < (double) kAnswerKeepFront[st])
        {
            for (int s = 0; s < 8; ++s)
            {
                ans.on[s] = call.on[s];
                ans.semi[s] = call.semi[s];
            }
            for (int s = 0; s < kBar; ++s)
                bias[s] = s < 8 ? 0.f : (call.on[s] ? kAnswerCallSlotWeight[st] : 1.f);
            from = 8;
        }
        else
        {
            for (int s = 0; s < kBar; ++s)
                bias[s] = call.on[s] ? kAnswerCallSlotWeight[st] : 1.f;
        }
        const int want = std::max (barCount (st, r, 0), countOn (ans.on, 0, 8) + 1);
        drawOnsets (st, 1, nb, want, bias, kBar, r, ans.on);
        const bool open = r.next() < (double) kOpenAnswer[st];
        pitchBar (st, p.mode, p.dark, ans.on, from, lastSemi (call.on, call.semi, 0), from == 0, open, r, ans.semi);
    }

    for (int b = 2; b < nb; ++b)
        bars[b] = bars[b & 1];

    if (nb == 8)
    {
        // Bars 5-7: the arc's density change, and the back-half variation.
        for (int j = 0; j < 3; ++j)
        {
            const int b = 4 + j;
            const int delta = kArcDelta[arc][j];
            const bool vary = j < 2 && r.next() < (double) kBackHalfVariation[st];
            if (delta == 0 && ! vary)
                continue;
            const int back = countOn (bars[b].on, 8, kBar) + delta;
            redrawBack (st, p.mode, p.dark, b, nb, std::max (1, back), r, bars[b]);
        }
    }
    turnBar (r, bars[nb - 1]);

    writeLane (lane, bars, nb, st, r);
    enforceKickCap (lane, nb);
    return arc;
}

} // namespace

int drawMode (int dialect, float dark, std::uint32_t bassSeed) noexcept
{
    if (dialect == Gaskin)
        return phrase::Phrygian;
    dark = std::clamp (dark, 0.f, 1.f);
    Rng r ((bassSeed ? bassSeed : 1u) ^ 0x3D0DE5u);
    const float pDor = kModeDorianA * (1.f - dark);
    const float pPhr = kModePhrygianB * dark * dark;
    const float x = (float) r.next();
    if (x < pDor)
        return phrase::Dorian;
    if (x < pDor + pPhr)
        return phrase::Phrygian;
    return phrase::Aeolian;
}

int composeBass (Lane& lane, const BassParams& p, std::uint32_t seed)
{
    Rng r (mix32 (seed ? seed : 1u, 0xB455u + (std::uint32_t) styleOf (p.dialect)));
    const int nb = p.bars >= 8 ? 8 : (p.bars >= 4 ? 4 : 2);
    int arc = 0;
    for (int t = 0; t < kComposeRetries; ++t)
    {
        arc = composeOnce (lane, p, r);
        if (! thinRepeatedBackHalf (lane, nb))
            break;
    }
    return arc;
}

int sampleDegreeSemis (int dialect, int mode, int prevSemi, std::uint32_t hash) noexcept
{
    Rng r (hash ? hash : 1u);
    return stepDegree (styleOf (dialect), mode, 0.55f, prevSemi, true, r);
}

void stabVoicing (int dialect, int mode, std::uint32_t hash, int* out, int& n) noexcept
{
    const int st = styleOf (dialect);
    Rng r (hash ? hash : 1u);
    float w[kVoicings];
    bool legal[kVoicings];
    for (int v = 0; v < kVoicings; ++v)
    {
        w[v] = kStabVoicings[st][v].w;
        legal[v] = w[v] > 0.f && ! (st == styleOf (Gaskin) && v == kJackinB2Cluster && mode != phrase::Phrygian);
    }
    int vi = pickWeighted (w, legal, kVoicings, r);
    if (vi < 0)
        vi = 0;
    const Voicing& V = kStabVoicings[st][vi];

    bool rootLegal[7];
    for (int d = 0; d < 7; ++d)
        rootLegal[d] = true;
    int d = pickWeighted (kStabRootDegree, rootLegal, 7, r);
    if (d < 0)
        d = 0;
    const int root = phrase::degreeToSemis (d, mode);
    const int lo = kStabRegLo[st];
    const int hi = kStabRegHi[st];

    int tones[4];
    int m = 0;
    for (int i = 0; i < std::clamp (V.n, 1, 4); ++i)
    {
        int t = root + V.iv[i];
        if (! phrase::inMode (t, mode))
            t = phrase::inMode (t - 1, mode) ? t - 1 : t + 1;
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
        int t = root + 7;
        while (t > hi)
            t -= 12;
        while (t < lo)
            t += 12;
        if (m == 0 || tones[0] != t)
            tones[m++] = t;
        else
        {
            t = t + 12 <= hi ? t + 12 : t - 12;
            if (t >= lo - 12)
                tones[m++] = t;
        }
    }
    std::sort (tones, tones + m);
    if (m > 0 && r.next() < (double) kStabOctaveMove[st] && tones[m - 1] + 12 <= hi)
        for (int k = 0; k < m; ++k)
            tones[k] += 12;
    n = m;
    for (int k = 0; k < m; ++k)
        out[k] = tones[k];
}

void composeStab (Lane& lane, const Lane& bass, const StabParams& p, std::uint32_t seed)
{
    lane.count = 0;
    const int st = styleOf (p.dialect);
    const int nb = std::clamp (p.bars, 1, kMaxBars);
    const int per = std::clamp (p.perBar, 0, 12);
    if (per <= 0)
        return;

    // Two voicings per roll, one per bar parity, never the same chord twice.
    int voi[2][4] {};
    int nv[2] {};
    stabVoicing (p.dialect, p.mode, mix32 (seed, 0x570Bu), voi[0], nv[0]);
    for (int t = 0; t < 6; ++t)
    {
        stabVoicing (p.dialect, p.mode, mix32 (seed, 0x571Bu + (std::uint32_t) t * 31u), voi[1], nv[1]);
        bool same = nv[1] == nv[0];
        for (int k = 0; same && k < nv[0]; ++k)
            same = voi[1][k] == voi[0][k];
        if (! same)
            break;
    }

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

    for (int bar = 0; bar < nb; ++bar)
    {
        if (p.skipOdd && (bar & 1) != 0)
            continue;
        const int par = bar & 1;
        Rng r (mix32 (seed, p.repeatByParity ? 0x100u + (std::uint32_t) par : 0x200u + (std::uint32_t) bar));
        // 0 gap, 1 sustain, 2 onset
        int cls[kBar];
        for (int s = 0; s < kBar; ++s)
        {
            const int abs = bar * kBar + s;
            if (kickStep (s))
                cls[s] = -1;
            else if (onset (abs))
                cls[s] = 2;
            else if (covers (abs))
                cls[s] = 1;
            else
                cls[s] = 0;
        }
        const int k = per;
        int gapN = (int) std::floor ((float) k * kStabGapShare[st] + (float) r.next());
        int onN = (int) std::floor ((float) k * kStabOnsetShare[st] + (float) r.next());
        gapN = std::clamp (gapN, 0, k);
        onN = std::clamp (onN, 0, k - gapN);
        const int susN = k - gapN - onN;
        int quota[3] = { gapN, susN, onN };
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
                    for (int s = 0; s < kBar; ++s)
                    {
                        legal[s] = cls[s] == want && ! taken[s];
                        w[s] = kickStep (s) ? 0.f : kStabSlotClass[(s & 3) - 1];
                        if (s >= 14 && (bar == 3 || bar == 7))
                            w[s] *= kAnswerTailFactor;
                    }
                    pick = pickWeighted (w, legal, kBar, r);
                }
                if (pick < 0)
                    continue;
                const float start = (float) (bar * kBar + pick);
                const float len = std::min (hold, (float) ((bar + 1) * kBar) - start);
                if (len < 0.5f || lane.count >= kMaxNotes)
                    continue;
                taken[pick] = true;
                Note n;
                n.start = start;
                n.length = len;
                n.nChord = (std::uint8_t) std::clamp (nv[par], 1, 4);
                for (int t = 0; t < 4; ++t)
                    n.chord[t] = (std::int8_t) (t < nv[par] ? voi[par][t] : 0);
                n.vel = (std::uint8_t) std::clamp (p.vel, 1, 127);
                lane.notes[lane.count++] = n;
            }
        }
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

} // namespace rolla::rules
