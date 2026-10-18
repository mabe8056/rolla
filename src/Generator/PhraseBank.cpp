#include "PhraseBank.h"
#include "Generator.h"
#include "PhraseBankData.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rolla::phrase
{
namespace
{
using namespace data;

const PbStyle& styleData (Style s) noexcept
{
    return s == Style::Jackin ? kJackin : kDeep;
}

std::uint32_t mix (std::uint32_t seed, std::uint32_t salt) noexcept
{
    std::uint32_t x = seed * 0x9E3779B1u ^ (salt + 0x7F4A7C15u);
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x == 0 ? 1u : x;
}

template <typename T>
int pickIndex (const T* items, int n, Rng& r, float (*weight) (const T&, const void*), const void* ctx)
{
    float sum = 0.f;
    for (int i = 0; i < n; ++i)
        sum += std::max (0.f, weight (items[i], ctx));
    if (n <= 0)
        return -1;
    if (sum <= 0.f)
        return std::min (n - 1, (int) (r.next() * n));
    float x = (float) r.next() * sum;
    for (int i = 0; i < n; ++i)
    {
        x -= std::max (0.f, weight (items[i], ctx));
        if (x <= 0.f)
            return i;
    }
    return n - 1;
}

template <typename T>
float plainWeight (const T& t, const void*)
{
    return t.w;
}

int pickDegree (const PbDeg* pool, int n, Rng& r)
{
    const int i = pickIndex (pool, n, r, &plainWeight<PbDeg>, nullptr);
    return i < 0 ? 0 : pool[i].deg;
}

bool isKickStep (int step) noexcept
{
    const int s = step % 16;
    for (int k : kKickSteps)
        if (s == k)
            return true;
    return false;
}

// One onset while the phrase is being built.
struct Hit
{
    int step = 0;   // 0 .. bars*16-1
    float len = 1.f;
    int degree = 0;
    int vel = 110;
    std::uint8_t flags = 0;
};

struct Work
{
    Hit hits[kMaxPhraseNotes];
    int n = 0;

    bool has (int step) const noexcept
    {
        for (int i = 0; i < n; ++i)
            if (hits[i].step == step)
                return true;
        return false;
    }

    void add (const Hit& h) noexcept
    {
        if (n < kMaxPhraseNotes && ! has (h.step))
            hits[n++] = h;
    }

    void sort() noexcept
    {
        std::sort (hits, hits + n, [] (const Hit& a, const Hit& b) { return a.step < b.step; });
    }

    void removeAt (int i) noexcept
    {
        for (int k = i; k < n - 1; ++k)
            hits[k] = hits[k + 1];
        --n;
    }

    int countIn (int lo, int hi) const noexcept
    {
        int c = 0;
        for (int i = 0; i < n; ++i)
            if (hits[i].step >= lo && hits[i].step < hi)
                ++c;
        return c;
    }
};

struct CellCtx
{
    float busy;
    int avoid; // cell index to damp (the other half's pick), -1 none
    int avoidTag;
};

float cellWeight (const PbCell& c, const void* ctx)
{
    const auto* k = static_cast<const CellCtx*> (ctx);
    // busy 0 favours one-hit and rest cells, busy 1 favours dense cells.
    const float dens = (float) c.n;
    float w = c.w * std::pow (1.f + dens, (k->busy - 0.45f) * 1.6f);
    if (k->avoidTag == TagRest && c.tag == TagRest)
        w *= 0.15f; // two rest halves would empty the bar
    return w;
}

int pickCell (const PbStyle& S, Rng& r, float busy, int avoidCell, int avoidTag)
{
    CellCtx ctx { busy, avoidCell, avoidTag };
    int pick = pickIndex (S.cells, S.nCells, r, &cellWeight, &ctx);
    if (pick == avoidCell && r.next() < 0.6) // same cell twice in a bar is allowed, just rarer
        pick = pickIndex (S.cells, S.nCells, r, &cellWeight, &ctx);
    return std::max (0, pick);
}

void addCell (Work& w, const PbCell& c, int offset, const PbStyle& S, Rng& r)
{
    for (int i = 0; i < c.n; ++i)
    {
        Hit h;
        h.step = offset + c.steps[i];
        const float scale = S.lenLo + (S.lenHi - S.lenLo) * (float) r.next();
        h.len = std::max (1.f, (float) c.len[i] * scale);
        w.add (h);
    }
}

// Move one onset in [lo, hi) by +-1 step, staying inside the half bar.
void nudgeOne (Work& w, int lo, int hi, Rng& r)
{
    int idx[kMaxPhraseNotes];
    int m = 0;
    for (int i = 0; i < w.n; ++i)
        if (w.hits[i].step >= lo && w.hits[i].step < hi)
            idx[m++] = i;
    if (m == 0)
        return;
    const int i = idx[std::min (m - 1, (int) (r.next() * m))];
    const int dir = r.next() < 0.5 ? -1 : 1;
    const int to = w.hits[i].step + dir;
    const int halfLo = (w.hits[i].step / 8) * 8;
    if (to < halfLo || to >= halfLo + 8 || w.has (to))
        return;
    w.hits[i].step = to;
}

int degreeFor (const PbStyle& S, const PbContour& c, int k, Rng& r)
{
    int d = c.deg[k % std::max (1, c.n)];
    if (r.next() < S.stray)
        d = pickDegree (S.degrees, S.nDegrees, r);
    return d;
}

void pitchHits (Work& w, int lo, int hi, const PbStyle& S, const PbContour& c, Rng& r, bool firstIsPhraseStart)
{
    int k = 0;
    for (int i = 0; i < w.n; ++i)
    {
        Hit& h = w.hits[i];
        if (h.step < lo || h.step >= hi)
            continue;
        h.degree = degreeFor (S, c, k, r);
        if (k == 0 && firstIsPhraseStart && r.next() < S.firstRoot)
            h.degree = 0;
        if (h.degree == 0)
        {
            const float x = (float) r.next();
            if (x < S.popUp)
                h.degree = 7;
            else if (x < S.popUp + S.dropDown)
                h.degree = -7;
        }
        ++k;
    }
}

void shapeHits (Work& w, int lo, int hi, const PbStyle& S, Rng& r)
{
    w.sort();
    for (int i = 0; i < w.n; ++i)
    {
        Hit& h = w.hits[i];
        if (h.step < lo || h.step >= hi)
            continue;
        const bool first = i == 0 || w.hits[i - 1].step < lo;
        h.vel = (h.step % 4 == 2) ? 116 : 108;
        h.flags = 0;
        if (! first && r.next() < S.ghost)
        {
            h.vel = 76;
            h.flags |= NoteGhost;
        }
        if (i > 0 && w.hits[i - 1].degree != h.degree && r.next() < S.glide)
            h.flags |= NoteGlide;
        if (r.next() < S.legato)
            h.len = 64.f; // trimmed to the next onset below
    }
}

void trimLens (Work& w, int phraseSteps, float maxLen)
{
    w.sort();
    for (int i = 0; i < w.n; ++i)
    {
        const int next = i + 1 < w.n ? w.hits[i + 1].step : phraseSteps;
        const float gap = (float) (next - w.hits[i].step);
        w.hits[i].len = std::max (0.5f, std::min ({ w.hits[i].len, gap, maxLen }));
        w.hits[i].len = std::round (w.hits[i].len * 2.f) / 2.f;
    }
}

int tagOf (const Work& w, int lo) noexcept
{
    const int c = w.countIn (lo, lo + 8);
    return c == 0 ? TagRest : (c == 1 ? TagSparse : TagDense);
}

void applyVariation (Work& w, int op, const PbStyle& S, const PbContour& c, float busy, Rng& r)
{
    // Bar 2 is steps 16..31. Ops touch mostly its back half (24..31).
    switch (op)
    {
        case OpRetail:
        {
            for (int i = w.n - 1; i >= 0; --i)
                if (w.hits[i].step >= 24)
                    w.removeAt (i);
            const int ci = pickCell (S, r, busy, -1, tagOf (w, 16));
            addCell (w, S.cells[ci], 24, S, r);
            pitchHits (w, 24, 32, S, c, r, false);
            // keep the first new note off the root more often: it is the answer end
            for (int i = 0; i < w.n; ++i)
                if (w.hits[i].step >= 24)
                {
                    if (w.hits[i].degree == 0 && r.next() < 0.5)
                        w.hits[i].degree = pickDegree (S.degrees, S.nDegrees, r);
                    break;
                }
            break;
        }
        case OpNudge:
            nudgeOne (w, 24, 32, r);
            break;
        case OpLastDegree:
        {
            for (int i = w.n - 1; i >= 0; --i)
                if (w.hits[i].step >= 16)
                {
                    const int was = w.hits[i].degree;
                    for (int t = 0; t < 4 && w.hits[i].degree == was; ++t)
                        w.hits[i].degree = pickDegree (S.degrees, S.nDegrees, r);
                    break;
                }
            break;
        }
        case OpDropOne:
        {
            int idx[kMaxPhraseNotes];
            int m = 0;
            for (int i = 0; i < w.n; ++i)
                if (w.hits[i].step > 16)
                    idx[m++] = i;
            if (m > 1)
                w.removeAt (idx[std::min (m - 1, (int) (r.next() * m))]);
            break;
        }
        case OpPickupFill:
        {
            const int step = r.next() < 0.5 ? 30 : 31;
            Hit h;
            h.step = step;
            h.len = 1.f;
            const int fills[] = { -1, 4, -3, 2 };
            h.degree = fills[std::min (3, (int) (r.next() * 4))];
            w.add (h);
            break;
        }
        case OpOctaveSwap:
        {
            for (int i = 0; i < w.n; ++i)
                if (w.hits[i].step >= 16 && w.hits[i].degree == 0 && r.next() < 0.6)
                {
                    w.hits[i].degree = 7;
                    break;
                }
            break;
        }
        default:
            break;
    }
}

// Hard rule: bass attacks on kick steps stay at or under kMaxKickShare.
void enforceKick (Work& w, int phraseSteps)
{
    for (int guard = 0; guard < 16; ++guard)
    {
        int onKick = 0;
        for (int i = 0; i < w.n; ++i)
            if (isKickStep (w.hits[i].step))
                ++onKick;
        if (w.n == 0 || (float) onKick <= kMaxKickShare * (float) w.n)
            return;
        // Move the latest kick-step onset off the kick, else drop it.
        for (int i = w.n - 1; i >= 0; --i)
        {
            if (! isKickStep (w.hits[i].step))
                continue;
            const int s = w.hits[i].step;
            const int tries[] = { s + 2, s + 1, s + 3 };
            bool moved = false;
            for (int to : tries)
                if (to < phraseSteps && (to / 8) == (s / 8) && ! w.has (to) && ! isKickStep (to))
                {
                    w.hits[i].step = to;
                    moved = true;
                    break;
                }
            if (! moved)
                w.removeAt (i);
            break;
        }
        w.sort();
    }
}

// Hard rule: no empty bar.
void enforceBars (Work& w, int bars)
{
    for (int b = 0; b < bars; ++b)
        if (w.countIn (b * 16, b * 16 + 16) == 0)
        {
            Hit h;
            h.step = b * 16 + 2;
            h.len = 4.f;
            h.degree = 0;
            w.add (h);
        }
    w.sort();
}

Phrase toPhrase (const Work& w, int bars, int mode)
{
    Phrase p;
    p.bars = bars;
    p.mode = mode;
    for (int i = 0; i < w.n && p.count < kMaxPhraseNotes; ++i)
    {
        const Hit& h = w.hits[i];
        Note n;
        n.start = (float) h.step;
        n.length = h.len;
        n.chord[0] = (std::int8_t) degreeToSemis (h.degree, mode);
        n.nChord = 1;
        n.vel = (std::uint8_t) std::clamp (h.vel, 1, 127);
        n.flags = h.flags;
        p.notes[p.count++] = n;
    }
    for (int h = 0; h < bars * 2; ++h)
        p.halfTag[h] = tagOf (w, h * 8);
    return p;
}

int pickMode (float dark, Rng& r)
{
    const float pDor = 0.35f * (1.f - dark);
    const float pPhr = 0.22f * dark * dark;
    const float x = (float) r.next();
    if (x < pDor)
        return Dorian;
    if (x < pDor + pPhr)
        return Phrygian;
    return Aeolian;
}

bool sounding (const Phrase& p, int step) noexcept
{
    for (int i = 0; i < p.count; ++i)
    {
        const float a = p.notes[i].start;
        if ((float) step >= a && (float) step < a + p.notes[i].length)
            return true;
    }
    return false;
}

bool onsetAt (const Phrase& p, int step) noexcept
{
    for (int i = 0; i < p.count; ++i)
        if ((int) std::floor (p.notes[i].start) == step)
            return true;
    return false;
}

} // namespace

int degreeToSemis (int degree, int mode) noexcept
{
    const int m = std::clamp (mode, 0, 2);
    const int oct = (degree >= 0) ? degree / 7 : -((-degree + 6) / 7);
    const int idx = degree - oct * 7;
    return oct * 12 + kModes[m][idx];
}

bool inMode (int semis, int mode) noexcept
{
    const int pc = ((semis % 12) + 12) % 12;
    for (int v : kModes[std::clamp (mode, 0, 2)])
        if (v == pc)
            return true;
    return false;
}

bool styleForDialect (int dialect, Style& out) noexcept
{
    if (dialect == Long)
    {
        out = Style::Deep;
        return true;
    }
    if (dialect == Gaskin)
    {
        out = Style::Jackin;
        return true;
    }
    return false;
}

Phrase makeBassPhrase (Style style, std::uint32_t seed, const PhraseSettings& s)
{
    const PbStyle& S = styleData (style);
    Rng r (mix (seed, 0xB455u + (std::uint32_t) style));
    const int bars = std::clamp (s.bars, 1, 2);
    const float busy = std::clamp (s.busy, 0.f, 1.f);
    const int mode = pickMode (std::clamp (s.dark, 0.f, 1.f), r);

    Work w;
    const int front = pickCell (S, r, busy, -1, -1);
    const int back = pickCell (S, r, busy, front, S.cells[front].tag);
    addCell (w, S.cells[front], 0, S, r);
    addCell (w, S.cells[back], 8, S, r);
    if (r.next() < 0.35)
        nudgeOne (w, 0, 16, r);
    if (r.next() < 0.25 + 0.2f * busy)
    {
        Hit g;
        g.step = (r.next() < 0.5 ? 7 : 15);
        g.len = 1.f;
        w.add (g);
    }
    w.sort();

    const int ci = std::max (0, pickIndex (S.contours, S.nContours, r, &plainWeight<PbContour>, nullptr));
    const PbContour& c = S.contours[ci];
    pitchHits (w, 0, 16, S, c, r, true);

    if (bars == 2)
    {
        const int n1 = w.n;
        for (int i = 0; i < n1; ++i)
        {
            Hit h = w.hits[i];
            h.step += 16;
            w.add (h);
        }
        const int nOps = r.next() < 0.25f + 0.5f * std::clamp (s.bounce, 0.f, 1.f) ? 2 : 1;
        for (int k = 0; k < nOps; ++k)
        {
            const int vi = std::max (0, pickIndex (S.vars, S.nVars, r, &plainWeight<PbVar>, nullptr));
            applyVariation (w, S.vars[vi].op, S, c, busy, r);
        }
        w.sort();
    }

    const int steps = bars * 16;
    enforceKick (w, steps);
    enforceBars (w, bars);
    shapeHits (w, 0, steps, S, r);
    trimLens (w, steps, S.maxLen);
    return toPhrase (w, bars, mode);
}

Phrase makeStabAnswer (Style style, const Phrase& bass, std::uint32_t seed, const PhraseSettings& s)
{
    // Call/answer pairs and voicings exist for JACKIN only. DEEP can borrow them.
    (void) style;
    const PbStyle& S = kJackin;
    Rng r (mix (seed, 0x57ABu));
    Phrase out;
    out.bars = bass.bars;
    out.mode = bass.mode;
    const int halves = bass.bars * 2;
    const float busy = std::clamp (s.busy, 0.f, 1.f);

    const int vi = std::max (0, pickIndex (S.voicings, S.nVoicings, r, &plainWeight<PbVoicing>, nullptr));
    const PbVoicing& V = S.voicings[vi];
    bool answered[4] {};

    auto placeIn = [&] (int half, int zoneLo, int zoneHi, int hits) -> int
    {
        int cand[8];
        float wts[8];
        int m = 0;
        for (int k = zoneLo; k <= zoneHi && k < 8; ++k)
        {
            const int step = half * 8 + k;
            if (sounding (bass, step) || onsetAt (bass, step) || onsetAt (out, step))
                continue;
            cand[m] = step;
            wts[m] = (step % 2 == 1 || step % 4 == 2) ? 1.f + S.offbeatBias * 2.f : 1.f;
            ++m;
        }
        int placed = 0;
        for (int h = 0; h < hits && m > 0; ++h)
        {
            float sum = 0.f;
            for (int i = 0; i < m; ++i)
                sum += wts[i];
            float x = (float) r.next() * sum;
            int pick = m - 1;
            for (int i = 0; i < m; ++i)
            {
                x -= wts[i];
                if (x <= 0.f)
                {
                    pick = i;
                    break;
                }
            }
            const int step = cand[pick];
            const int deg = pickDegree (S.answerDegrees, S.nAnswerDegrees, r);
            Note n;
            n.start = (float) step;
            int len = S.answerLenLo + (int) (r.next() * (S.answerLenHi - S.answerLenLo + 1));
            int free = 0;
            while (free < len && ! onsetAt (bass, step + free) && step + free < bass.bars * 16)
                ++free;
            n.length = (float) std::max (1, free);
            n.nChord = (std::uint8_t) std::min (4, V.n);
            for (int i = 0; i < n.nChord; ++i)
                n.chord[i] = (std::int8_t) degreeToSemis (deg + V.stack[i], bass.mode);
            n.vel = (std::uint8_t) (96 + (int) (r.next() * 16));
            if (out.count < kMaxPhraseNotes)
                out.notes[out.count++] = n;
            ++placed;
            for (int i = pick; i < m - 1; ++i)
            {
                cand[i] = cand[i + 1];
                wts[i] = wts[i + 1];
            }
            --m;
        }
        return placed;
    };

    for (int h = 0; h < halves; ++h)
    {
        const int a = (h + 1) % halves;
        if (answered[a])
            continue;
        const int call = bass.halfTag[h];
        const int ans = bass.halfTag[a];
        int matches[8];
        float mw[8];
        int m = 0;
        for (int i = 0; i < S.nPairs && m < 8; ++i)
            if (S.pairs[i].call == call && S.pairs[i].answer == ans)
            {
                matches[m] = i;
                mw[m] = S.pairs[i].w;
                ++m;
            }
        if (m == 0)
            continue;
        // Answer every call at busy 1, about two thirds at busy 0.
        if (r.next() > 0.65f + 0.35f * busy)
            continue;
        float sum = 0.f;
        for (int i = 0; i < m; ++i)
            sum += mw[i];
        float x = (float) r.next() * sum;
        int pi = matches[m - 1];
        for (int i = 0; i < m; ++i)
        {
            x -= mw[i];
            if (x <= 0.f)
            {
                pi = matches[i];
                break;
            }
        }
        const PbPair& P = S.pairs[pi];
        const int span = P.hitsHi - P.hitsLo;
        const int hits = P.hitsLo + (int) std::round ((float) span * (0.3f + 0.7f * busy) * (float) r.next() * 1.4f);
        if (placeIn (a, P.zoneLo, P.zoneHi, std::clamp (hits, P.hitsLo, P.hitsHi)) > 0)
            answered[a] = true;
    }

    // At least one answer per bar, in the widest quiet stretch.
    for (int b = 0; b < bass.bars; ++b)
    {
        bool any = false;
        for (int i = 0; i < out.count; ++i)
            if ((int) out.notes[i].start / 16 == b)
                any = true;
        if (any)
            continue;
        int bestHalf = b * 2 + 1;
        int bestFree = -1;
        for (int h = b * 2; h < b * 2 + 2; ++h)
        {
            int f = 0;
            for (int k = 0; k < 8; ++k)
                if (! sounding (bass, h * 8 + k))
                    ++f;
            if (f > bestFree)
            {
                bestFree = f;
                bestHalf = h;
            }
        }
        if (placeIn (bestHalf, 0, 7, 1) == 0)
        {
            // The bass holds through the whole bar: answer over the hold, never on a bass onset.
            for (int k = 2; k < 16; k += 2)
            {
                const int step = b * 16 + (k + 1) % 16;
                if (onsetAt (bass, step))
                    continue;
                Note n;
                n.start = (float) step;
                n.length = 1.f;
                const int deg = pickDegree (S.answerDegrees, S.nAnswerDegrees, r);
                n.nChord = (std::uint8_t) std::min (4, V.n);
                for (int i = 0; i < n.nChord; ++i)
                    n.chord[i] = (std::int8_t) degreeToSemis (deg + V.stack[i], bass.mode);
                n.vel = 100;
                if (out.count < kMaxPhraseNotes)
                    out.notes[out.count++] = n;
                break;
            }
        }
    }

    std::sort (out.notes, out.notes + out.count, [] (const Note& x, const Note& y) { return x.start < y.start; });
    return out;
}

Skeleton skeletonOf (const Phrase& p) noexcept
{
    Skeleton k;
    const int steps = std::max (1, p.bars) * 16;
    for (int rep = 0; rep * steps < 32; ++rep)
        for (int i = 0; i < p.count; ++i)
        {
            const int s = (int) std::floor (p.notes[i].start) + rep * steps;
            if (s < 0 || s >= 32)
                continue;
            k.onsets |= (1u << s);
            k.pitch[s] = p.notes[i].chord[0];
        }
    return k;
}

float similarity (const Skeleton& a, const Skeleton& b) noexcept
{
    const std::uint32_t inter = a.onsets & b.onsets;
    const std::uint32_t uni = a.onsets | b.onsets;
    auto pop = [] (std::uint32_t x)
    {
        int c = 0;
        while (x)
        {
            x &= x - 1;
            ++c;
        }
        return c;
    };
    const int nu = pop (uni);
    if (nu == 0)
        return 1.f;
    const int ni = pop (inter);
    int same = 0;
    for (int s = 0; s < 32; ++s)
        if ((inter >> s) & 1u)
            if (a.pitch[s] == b.pitch[s])
                ++same;
    const float j = (float) ni / (float) nu;
    const float pitchShare = ni > 0 ? (float) same / (float) ni : 0.f;
    return j * (0.5f + 0.5f * pitchShare);
}

void SkeletonHistory::push (const Skeleton& s) noexcept
{
    items[head] = s;
    head = (head + 1) % kCap;
    count = std::min (kCap, count + 1);
}

float SkeletonHistory::closest (const Skeleton& s) const noexcept
{
    float best = 0.f;
    for (int i = 0; i < count; ++i)
        best = std::max (best, similarity (s, items[i]));
    return best;
}

Phrase drawDistinctBass (Style style, std::uint32_t seed, const PhraseSettings& settings,
                         SkeletonHistory& history, float threshold, int maxTries)
{
    Phrase best;
    float bestSim = 2.f;
    std::uint32_t sd = seed;
    for (int t = 0; t < std::max (1, maxTries); ++t)
    {
        Phrase p = makeBassPhrase (style, sd, settings);
        const float sim = history.closest (skeletonOf (p));
        if (sim < bestSim)
        {
            best = p;
            bestSim = sim;
        }
        if (sim < threshold)
            break;
        sd += 0x2545F491u;
    }
    history.push (skeletonOf (best));
    return best;
}

int writePhrase (const Phrase& p, Lane& lane, int loopBars) noexcept
{
    lane.count = 0;
    const int steps = std::max (1, p.bars) * 16;
    const int loop = std::max (1, loopBars) * 16;
    for (int off = 0; off < loop; off += steps)
        for (int i = 0; i < p.count && lane.count < kMaxNotes; ++i)
        {
            Note n = p.notes[i];
            n.start += (float) off;
            if (n.start >= (float) loop)
                continue;
            n.length = std::min (n.length, (float) loop - n.start);
            lane.notes[lane.count++] = n;
        }
    return lane.count;
}

bool passesHardRules (const Phrase& p, char* why) noexcept
{
    auto fail = [why] (const char* msg, int a)
    {
        if (why != nullptr)
            std::snprintf (why, 96, msg, a);
        return false;
    };
    const int steps = p.bars * 16;
    int onKick = 0;
    for (int i = 0; i < p.count; ++i)
    {
        const Note& n = p.notes[i];
        for (int c = 0; c < n.nChord; ++c)
            if (! inMode (n.chord[c], p.mode))
                return fail ("out of key: %d semitones", n.chord[c]);
        if (n.start < 0.f || n.start + n.length > (float) steps + 1e-3f)
            return fail ("note past the phrase at step %d", (int) n.start);
        if (isKickStep ((int) std::floor (n.start)))
            ++onKick;
    }
    for (int b = 0; b < p.bars; ++b)
    {
        bool any = false;
        for (int i = 0; i < p.count; ++i)
            if ((int) p.notes[i].start / 16 == b)
                any = true;
        if (! any)
            return fail ("empty bar %d", b + 1);
    }
    if (p.count > 0 && (float) onKick > kMaxKickShare * (float) p.count + 1e-3f)
        return fail ("%d bass attacks on kick steps", onKick);
    return true;
}

AnswerStats answerStats (const Phrase& bass, const Phrase& stab) noexcept
{
    AnswerStats st;
    st.stabHits = stab.count;
    int inGap = 0;
    for (int i = 0; i < stab.count; ++i)
        if (! sounding (bass, (int) std::floor (stab.notes[i].start)))
            ++inGap;
    st.inGapShare = stab.count > 0 ? (float) inGap / (float) stab.count : 0.f;

    const int halves = bass.bars * 2;
    int calls = 0;
    int answered = 0;
    for (int h = 0; h < halves; ++h)
    {
        if (bass.halfTag[h] == TagRest)
            continue;
        const int a = (h + 1) % halves;
        if (bass.halfTag[a] == TagDense)
            continue; // no quiet half to answer in
        ++calls;
        for (int i = 0; i < stab.count; ++i)
            if ((int) std::floor (stab.notes[i].start) / 8 == a)
            {
                ++answered;
                break;
            }
    }
    st.callsAnswered = calls > 0 ? (float) answered / (float) calls : 1.f;
    return st;
}

} // namespace rolla::phrase
