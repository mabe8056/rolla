#include "Generator.h"
#include "CorpusData.h"
#include "VocabCells.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace rolla
{
namespace
{

constexpr int kCap = 12;

float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

int clampi (int v, int lo, int hi) noexcept
{
    return std::max (lo, std::min (hi, v));
}

int pcOf (int iv) noexcept
{
    int p = iv % 12;
    return p < 0 ? p + 12 : p;
}

bool darkPc (int pc) noexcept
{
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10 || pc == 11;
}

bool brightPc (int pc) noexcept
{
    return pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9;
}

std::uint64_t fnvByte (std::uint64_t h, unsigned b) noexcept
{
    h ^= (std::uint64_t) (b & 0xffu);
    h *= 0x100000001B3ull;
    return h;
}

std::uint64_t hashTriples (const int* slot, const int* len2, const int* pc, int n) noexcept
{
    std::uint64_t h = 14695981039346656037ull;
    for (int i = 0; i < n; ++i)
    {
        h = fnvByte (h, (unsigned) slot[i]);
        h = fnvByte (h, (unsigned) len2[i]);
        h = fnvByte (h, (unsigned) pc[i]);
    }
    return h;
}

std::uint64_t pairHash (std::uint64_t a, std::uint64_t b) noexcept
{
    return (a * 0x9E3779B97F4A7C15ull) ^ (b + 0xC0FFEEull);
}

bool banned (std::uint64_t h) noexcept
{
    return std::binary_search (corpus::kBan, corpus::kBan + corpus::kBanN, h);
}

std::uint32_t mixSeed (std::uint32_t seed, int attempt) noexcept
{
    std::uint32_t s = seed == 0 ? 1u : seed;
    for (int i = 0; i < attempt; ++i)
        s = s * 1664525u + 1013904223u;
    return s == 0 ? 1u : s;
}

struct BarNote
{
    int slot = 0;
    int len2 = 2;
    int iv = 0;
    int vel = 112;
    std::uint8_t flags = 0;
};

int pickWeighted (const double* w, int n, Rng& r)
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += w[i];
    if (n <= 0)
        return 0;
    if (sum <= 1.0e-12)
        return std::min (n - 1, (int) (r.next() * n));
    double x = r.next() * sum;
    for (int i = 0; i < n; ++i)
    {
        x -= w[i];
        if (x <= 0.0)
            return i;
    }
    return n - 1;
}

float artistW (int dialect, int artist) noexcept
{
    if (artist < 0 || artist >= corpus::ArCount)
        return 0.f;
    return corpus::kStyleWeight[clampi (dialect, 0, 3)][artist];
}

void densityBand (const Settings& s, float& target, float& lo, float& hi) noexcept
{
    float a = 3.2f, b = 7.2f;
    if (s.dialect == Long) { a = 2.2f; b = 5.4f; }
    else if (s.dialect == Pop) { a = 4.0f; b = 8.2f; }
    else if (s.dialect == Gaskin) { a = 5.0f; b = 7.2f; }
    target = lerp (a, b, s.busy);
    lo = std::max (1.5f, target - 2.4f);
    hi = target + 2.4f;
}

int foldIv (int iv, float dark, int dialect) noexcept
{
    if (dialect == Gaskin)
    {
        while (iv > 5) iv -= 12;
        while (iv < -5) iv += 12;
        return clampi (iv, -5, 5);
    }
    int ceiling = 10;
    if (dark < 0.28f || (dialect == Pop && dark < 0.72f))
        ceiling = 19;
    else if (dark < 0.48f)
        ceiling = 12;
    while (iv > ceiling) iv -= 12;
    while (iv < -5) iv += 12;
    return clampi (iv, -5, ceiling);
}

int snapScale (int iv, int dialect) noexcept
{
    const int pc = pcOf (iv);
    const bool gaskin = dialect == Gaskin;
    const bool colour = pc == 0 || pc == 2 || pc == 3 || pc == 5 || pc == 7 || pc == 8 || pc == 9 || pc == 10 || pc == 1;
    if (colour || (gaskin && pc == 1))
        return iv;
    return iv - pc;
}

void compactChord (std::int8_t* iv, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        int v = iv[i];
        while (v > 14) v -= 12;
        while (v < -7) v += 12;
        iv[i] = (std::int8_t) v;
    }
}

int pickPartner (const Settings& s, Rng& r)
{
    struct Choice { int iv; float d; float b; };
    const bool gaskin = s.dialect == Gaskin;
    const bool pop = s.dialect == Pop;
    const Choice choices[] = {
        { 10, 1.5f, 0.25f }, { 3, 1.4f, 0.45f }, { 1, 1.3f, 0.08f },
        { 8, 0.7f, 0.05f }, { -2, 1.2f, 0.2f }, { -1, 0.9f, 0.05f },
        { 7, 0.35f, 1.6f }, { 2, 0.3f, 1.3f }, { 5, 0.15f, 0.7f },
        { 12, 0.05f, 1.2f }, { -12, 0.55f, 0.05f },
    };
    double w[12];
    const int n = 11;
    for (int i = 0; i < n; ++i)
    {
        float weight = lerp (choices[i].b, choices[i].d, s.dark);
        if (gaskin && (choices[i].iv == 1 || choices[i].iv == 3 || choices[i].iv == -2 || choices[i].iv == 10))
            weight *= 2.4f;
        if (gaskin && (choices[i].iv == 12 || choices[i].iv == 7))
            weight *= 0.05f;
        if (pop && (choices[i].iv == 7 || choices[i].iv == 2 || choices[i].iv == 12))
            weight *= 1.8f;
        if (s.dialect == Long && (choices[i].iv == 3 || choices[i].iv == 10 || choices[i].iv == 1))
            weight *= 1.5f;
        w[i] = std::max (0.0, (double) weight);
    }
    return foldIv (choices[pickWeighted (w, n, r)].iv, s.dark, s.dialect);
}

bool slotTaken (const BarNote* n, int count, int slot, int skip) noexcept
{
    for (int i = 0; i < count; ++i)
        if (i != skip && n[i].slot == slot)
            return true;
    return false;
}

void sortBar (BarNote* n, int count)
{
    std::sort (n, n + count, [] (const BarNote& a, const BarNote& b) { return a.slot < b.slot; });
}

void loadBassCell (const corpus::BassCell& cell, int slotOffset, BarNote* n, int& count)
{
    count = 0;
    for (int i = 0; i < cell.n && count < kCap; ++i)
    {
        const auto& e = corpus::kBassEv[cell.off + i];
        const int slot = (int) e.slot - slotOffset;
        if (slot < 0 || slot > 15)
            continue;
        n[count].slot = slot;
        n[count].len2 = std::max (1, (int) e.len2);
        n[count].iv = e.iv;
        n[count].vel = e.vel;
        n[count].flags = e.flags;
        ++count;
    }
    sortBar (n, count);
}

int pickBassCell (const Settings& s, Rng& r, int barsWant, bool preferFill)
{
    double w[768];
    const int n = std::min (corpus::kBassN, 768);
    float target, lo, hi;
    densityBand (s, target, lo, hi);
    const float oddTarget = lerp (0.18f, 0.72f, s.bounce);
    for (int i = 0; i < n; ++i)
    {
        const auto& c = corpus::kBass[i];
        if ((int) c.bars != barsWant)
        {
            w[i] = 0.0;
            continue;
        }
        const float dens = (float) c.n / (float) std::max (1, (int) c.bars);
        const float densMatch = 1.f - std::min (1.f, std::fabs (dens - target) / 6.f);
        const float oddMatch = 1.f - std::min (1.f, std::fabs ((float) c.odd / 100.f - oddTarget));
        const float darkMatch = 1.f - std::min (1.f, std::fabs ((float) c.dark / 100.f - s.dark));
        float fit = 0.35f + 0.65f * densMatch * (0.45f + 0.55f * oddMatch) * (0.45f + 0.55f * darkMatch);
        if (preferFill)
            fit *= c.fill ? 2.4f : 0.55f;
        if (s.dialect == Gaskin && c.artist != corpus::ArGaskin)
            fit *= 0.35f;
        w[i] = (double) artistW (s.dialect, c.artist) * fit;
    }
    return pickWeighted (w, n, r);
}

int pickContour (const Settings& s, Rng& r)
{
    double w[768];
    const int n = std::min (corpus::kContourN, 768);
    for (int i = 0; i < n; ++i)
    {
        const auto& c = corpus::kContour[i];
        const float darkMatch = 1.f - std::min (1.f, std::fabs ((float) c.dark / 100.f - s.dark));
        w[i] = (double) artistW (s.dialect, c.artist) * (0.4f + 0.6f * darkMatch);
    }
    return pickWeighted (w, n, r);
}

void applyContour (BarNote* n, int count, int contour, float dark, int dialect)
{
    if (count <= 0 || contour < 0 || contour >= corpus::kContourN)
        return;
    const auto& c = corpus::kContour[contour];
    if (c.n <= 0)
        return;
    for (int i = 0; i < count; ++i)
        n[i].iv = foldIv (corpus::kContourIv[c.off + (i % c.n)], dark, dialect);
}

void steerDark (BarNote* n, int count, const Settings& s, Rng& r)
{
    const int partner = pickPartner (s, r);
    for (int i = 0; i < count; ++i)
    {
        const int pc = pcOf (n[i].iv);
        if (pc == 0 && r.next() > 0.18)
            continue;
        if (s.dark > 0.5f && brightPc (pc) && r.next() < (double) ((s.dark - 0.35f) * 1.6f))
            n[i].iv = partner;
        else if (s.dark < 0.5f && darkPc (pc) && r.next() < (double) ((0.65f - s.dark) * 1.6f))
            n[i].iv = partner;
        n[i].iv = foldIv (n[i].iv, s.dark, s.dialect);
    }
    if (s.dark < 0.32f && s.dialect != Gaskin && count > 0 && r.next() < (double) ((0.32f - s.dark) * 2.2f))
    {
        const int i = (int) (r.next() * count);
        if (n[i].iv >= 0 && n[i].iv < 12)
            n[i].iv += 12;
    }
    if (s.dark > 0.72f)
    {
        for (int i = 0; i < count; ++i)
            if (n[i].iv >= 12)
                n[i].iv -= 12;
    }
}

void leanTwoPitches (BarNote* n, int count, int partner)
{
    if (count <= 0)
        return;
    float time[12] {};
    float total = 0.f;
    for (int i = 0; i < count; ++i)
    {
        const float t = (float) std::max (1, n[i].len2);
        time[pcOf (n[i].iv)] += t;
        total += t;
    }
    if (total <= 0.f)
        return;
    int a = 0, b = partner >= 0 ? pcOf (partner) : 10;
    for (int p = 0; p < 12; ++p)
        if (time[p] > time[a])
            a = p;
    b = a == 0 ? pcOf (partner) : 0;
    for (int p = 0; p < 12; ++p)
        if (p != a && time[p] > time[b])
            b = p;
    auto nearest = [&] (int pc)
    {
        const int da = std::min (std::abs (pc - a), 12 - std::abs (pc - a));
        const int db = std::min (std::abs (pc - b), 12 - std::abs (pc - b));
        return da <= db ? a : b;
    };
    for (int guard = 0; guard < 8; ++guard)
    {
        float top = time[a] + time[b];
        if (top >= total * 0.64f)
            break;
        int worst = -1;
        float worstT = 0.f;
        for (int p = 0; p < 12; ++p)
            if (p != a && p != b && time[p] > worstT)
            {
                worst = p;
                worstT = time[p];
            }
        if (worst < 0)
            break;
        for (int i = 0; i < count; ++i)
            if (pcOf (n[i].iv) == worst)
                n[i].iv = nearest (worst) == a ? (a == 0 ? 0 : n[i].iv) : (b == 0 ? 0 : partner);
        std::memset (time, 0, sizeof (time));
        total = 0.f;
        for (int i = 0; i < count; ++i)
        {
            const float t = (float) std::max (1, n[i].len2);
            time[pcOf (n[i].iv)] += t;
            total += t;
        }
    }
}

void steerBounce (BarNote* n, int count, const Settings& s, Rng& r)
{
    const bool gaskin = s.dialect == Gaskin;
        const float oddTarget = gaskin ? 0.55f : lerp (0.08f, 0.86f, s.bounce);
        bool hole = true;
        for (int i = 0; i < count; ++i)
            if (n[i].slot >= 12)
                hole = false;
        auto nearestAnd = [&] (int self, int slot) -> int
        {
            static const int ands[] = { 2, 6, 10, 14 };
            int best = -1;
            int bestD = 99;
            for (int a : ands)
            {
                if (hole && a >= 12)
                    continue;
                if (slotTaken (n, count, a, self))
                    continue;
                const int dist = std::abs (a - slot);
                if (dist < bestD)
                {
                    bestD = dist;
                    best = a;
                }
            }
            return best;
        };
        for (int guard = 0; guard < 16; ++guard)
        {
            int odd = 0;
            for (int i = 0; i < count; ++i)
                if ((n[i].slot & 1) != 0)
                    ++odd;
            const float ratio = count > 0 ? (float) odd / (float) count : 0.f;
            if (std::fabs (ratio - oddTarget) < 0.08f)
                break;
            if (ratio < oddTarget)
            {
                int idx = -1;
                for (int k = 0; k < count; ++k)
                {
                    const int i = (k + (int) (r.next() * count)) % count;
                    if ((n[i].slot & 1) == 0)
                    {
                        idx = i;
                        break;
                    }
                }
                if (idx < 0)
                    break;
                const int dir = r.next() < 0.5 ? -1 : 1;
                const int neu = clampi (n[idx].slot + dir, 0, 15);
                if ((neu & 1) != 0 && ! (hole && neu >= 12) && ! slotTaken (n, count, neu, idx))
                    n[idx].slot = neu;
                else
                    break;
            }
            else
            {
                int idx = -1;
                for (int k = 0; k < count; ++k)
                {
                    const int i = (k + (int) (r.next() * count)) % count;
                    if ((n[i].slot & 1) != 0)
                    {
                        idx = i;
                        break;
                    }
                }
                if (idx < 0)
                    break;
                const int neu = nearestAnd (idx, n[idx].slot);
                if (neu >= 0)
                    n[idx].slot = neu;
                else
                    break;
            }
        }
    if (gaskin)
    {
        static const int dotted[] = { 0, 3, 6, 9, 12, 15 };
        for (int i = 0; i < count; ++i)
        {
            bool on = false;
            for (int d : dotted)
                if (n[i].slot == d)
                    on = true;
            if (on || r.next() > 0.7)
                continue;
            int best = dotted[0];
            int bestD = 99;
            for (int d : dotted)
            {
                const int dist = std::abs (d - n[i].slot);
                if (dist < bestD && ! slotTaken (n, count, d, i))
                {
                    bestD = dist;
                    best = d;
                }
            }
            if (bestD <= 2)
                n[i].slot = best;
        }
    }
    sortBar (n, count);
}

void unkick (BarNote* n, int count, const Settings& s, const bool* kick)
{
    if (s.dialect == Gaskin)
        return;
    for (int i = 0; i < count; ++i)
    {
        const int step = n[i].slot;
        const bool onKick = kick != nullptr ? kick[step] : (step % 4) == 0;
        if (! onKick)
            continue;
        const int prefer = s.bounce > 0.55f ? 1 : 2;
        int moved = -1;
        for (int dir : { prefer, prefer == 1 ? 2 : 1, -1, -2 })
        {
            const int neu = n[i].slot + dir;
            if (neu < 0 || neu > 15 || slotTaken (n, count, neu, i))
                continue;
            const bool neuKick = kick != nullptr ? kick[neu] : (neu % 4) == 0;
            if (! neuKick)
            {
                moved = neu;
                break;
            }
        }
        if (moved >= 0)
            n[i].slot = moved;
    }
    sortBar (n, count);
}

void avoidStabAttacks (BarNote* n, int count, const bool* stabAt, int bar)
{
    if (stabAt == nullptr)
        return;
    for (int i = 0; i < count; ++i)
    {
        const int step = bar * 16 + n[i].slot;
        if (step < 0 || step >= kMaxSteps || ! stabAt[step])
            continue;
        for (int dir : { 1, -1, 2, -2 })
        {
            const int neu = n[i].slot + dir;
            const int ns = bar * 16 + neu;
            if (neu < 0 || neu > 15 || slotTaken (n, count, neu, i))
                continue;
            if (ns >= 0 && ns < kMaxSteps && stabAt[ns])
                continue;
            n[i].slot = neu;
            break;
        }
    }
    sortBar (n, count);
}

void fitDensity (BarNote* n, int& count, const Settings& s, Rng& r, int role, int partner)
{
    float target, lo, hi;
    densityBand (s, target, lo, hi);
    (void) lo;
    (void) hi;
    if (role == 1)
        target *= s.dialect == Gaskin ? 0.9f : 0.72f;
    if (role == 3)
        target *= 0.8f;
    int want = clampi ((int) std::round (target), 3, 8);
    // Answer and turn bars sit two notes under the call so the bar-to-bar
    // range stays at least 2. A one-note trim reads as a flat loop.
    if (role == 1 || role == 3)
        want = std::max (2, want - 2);
    auto dropOne = [&] ()
    {
        int idx = count - 1;
        for (int i = 1; i < count; ++i)
            if (n[i].vel < n[idx].vel || (n[i].flags & NoteGhost))
                idx = i;
        if (idx == 0 && count > 1)
            idx = count - 1;
        for (int k = idx; k < count - 1; ++k)
            n[k] = n[k + 1];
        --count;
    };
    int guard = 0;
    while (count > want && count > 2 && guard++ < 8)
        dropOne();
    guard = 0;
    while (count < want && count < kCap && guard++ < 12)
    {
        int slot = 2;
        if (s.dialect == Gaskin)
        {
            static const int dotted[] = { 0, 3, 6, 9, 12, 15 };
            slot = dotted[(int) (r.next() * 6.0) % 6];
        }
        else if (s.bounce >= 0.5f)
            slot = 1 + 2 * ((int) (r.next() * 8.0) % 8);
        else
            slot = 2 + 4 * ((int) (r.next() * 4.0) % 4);
        if (role == 1 && slot >= 12)
            continue;
        if (slotTaken (n, count, slot, -1))
            continue;
        n[count].slot = slot;
        n[count].len2 = s.dialect == Pop ? 2 : 3;
        n[count].iv = (r.next() < 0.55) ? 0 : partner;
        n[count].vel = r.next() < 0.3 ? 80 : 112;
        n[count].flags = n[count].vel < 100 ? NoteGhost : 0;
        ++count;
    }
    sortBar (n, count);
}

void shapeRole (BarNote* n, int& count, const Settings& s, Rng& r, int role, bool strongTurn, int partner)
{
    if (s.dialect == Gaskin)
    {
        if (role == 1)
        {
            for (int i = 0; i < count; ++i)
                if (n[i].iv == 0 && r.next() < 0.55)
                    n[i].iv = r.next() < 0.5 ? 1 : 3;
        }
        return;
    }
    if (role == 1)
    {
        int w = 0;
        for (int i = 0; i < count; ++i)
            if (n[i].slot < 12 || (n[i].flags & NoteGhost))
                n[w++] = n[i];
        if (w >= 2)
            count = w;
    }
    if (role == 3 && strongTurn)
    {
        const int cut = s.fill ? 8 : 10;
        int w = 0;
        for (int i = 0; i < count; ++i)
            if (n[i].slot < cut)
                n[w++] = n[i];
        count = std::max (2, w);
        if (count < kCap && ! slotTaken (n, count, 14, -1))
        {
            n[count].slot = 14;
            n[count].len2 = 2;
            n[count].iv = partner == 0 ? -2 : partner;
            n[count].vel = 80;
            n[count].flags = (std::uint8_t) (NoteGhost | NoteGlide);
            ++count;
        }
    }
    else if (role == 3)
    {
        for (int i = 0; i < count; ++i)
            if (n[i].slot >= 8 && r.next() < 0.7)
                n[i].iv = partner;
    }
    sortBar (n, count);
}

void addGhost (BarNote* n, int& count, const Settings& s, Rng& r, int partner)
{
    if (count >= kCap || r.next() > lerp (0.15f, 0.75f, s.busy))
        return;
    int host = -1;
    for (int i = 0; i < count; ++i)
        if (n[i].iv == 0 && n[i].slot > 0)
            host = i;
    if (host < 0)
        return;
    const int slot = n[host].slot - 1;
    if (slot < 0 || slotTaken (n, count, slot, -1))
        return;
    if (s.dialect != Gaskin && slot % 4 == 0)
        return;
    n[count].slot = slot;
    n[count].len2 = 2;
    n[count].iv = s.dark > 0.4f ? (r.next() < 0.5 ? -2 : 1) : partner;
    n[count].iv = foldIv (n[count].iv, s.dark, s.dialect);
    n[count].vel = 80;
    n[count].flags = (std::uint8_t) (NoteGhost | NoteGlide);
    ++count;
    sortBar (n, count);
}

void forceOneChange (BarNote* n, int count, const Settings& s, Rng& r, int partner)
{
    if (count <= 0)
        return;
    const int i = 1 + (count > 1 ? (int) (r.next() * (count - 1)) : 0);
    const int idx = std::min (count - 1, i);
    if (r.next() < 0.85)
        n[idx].iv = foldIv (partner, s.dark, s.dialect);
    else
        n[idx].flags = (std::uint8_t) (n[idx].flags ^ NoteGhost);
}

void prepareBar (BarNote* n, int& count, const Settings& s, Rng& r, int role, bool strongTurn,
                 int partner, int bar, const bool* kick, const bool* stabAt)
{
    if (r.next() < 0.62)
        applyContour (n, count, pickContour (s, r), s.dark, s.dialect);
    steerDark (n, count, s, r);
    addGhost (n, count, s, r, partner);
    steerBounce (n, count, s, r);
    shapeRole (n, count, s, r, role, strongTurn, partner);
    fitDensity (n, count, s, r, role, partner);
    unkick (n, count, s, kick);
    avoidStabAttacks (n, count, stabAt, bar);
    forceOneChange (n, count, s, r, partner);
    steerBounce (n, count, s, r);
    leanTwoPitches (n, count, partner);
    for (int i = 0; i < count; ++i)
    {
        n[i].iv = snapScale (foldIv (n[i].iv, s.dark, s.dialect), s.dialect);
        if ((n[i].flags & NoteGhost) != 0)
            n[i].vel = 80;
        else
            n[i].vel = 112;
        if (std::abs (partner - n[i].iv) <= 2 && partner != n[i].iv)
            n[i].flags = (std::uint8_t) (n[i].flags | NoteGlide);
    }
    sortBar (n, count);
}

void copyBar (const BarNote* src, int n, BarNote* dst, int& nd)
{
    nd = n;
    for (int i = 0; i < n; ++i)
        dst[i] = src[i];
}

void mutateSeed (BarNote* n, int count, Rng& r)
{
    int moved = 0;
    for (int i = 0; i < count && moved < 2; ++i)
    {
        const int dir = r.next() < 0.5 ? 1 : -1;
        int neu = n[i].slot + dir;
        if (neu < 0 || neu > 15 || (neu % 4) == 0 || slotTaken (n, count, neu, i))
            neu = n[i].slot + dir * 2;
        if (neu < 0 || neu > 15 || slotTaken (n, count, neu, i))
            continue;
        n[i].slot = neu;
        ++moved;
    }
    if (count > 3 && r.next() < 0.8)
    {
        const int idx = 1 + (int) (r.next() * (count - 2));
        for (int k = idx; k < count - 1; ++k)
            n[k] = n[k + 1];
        // caller keeps the count; dropping is applied by the return via the last slot sentinel
        n[count - 1].slot = -1;
    }
    sortBar (n, count);
}

int compactDropped (BarNote* n, int count)
{
    int w = 0;
    for (int i = 0; i < count; ++i)
        if (n[i].slot >= 0 && n[i].slot <= 15)
            n[w++] = n[i];
    return w;
}

bool loadVocabBar (BarNote* n, int& count, const Settings& s, Rng& r, int lane, bool fill)
{
    double w[96];
    int idx[96];
    int nW = 0;
    for (int i = 0; i < vocab::kCellN && nW < 96; ++i)
    {
        const auto& c = vocab::kCells[i];
        if (c.lane != lane)
            continue;
        if (c.fillOnly && ! fill)
            continue;
        if (! c.fillOnly && fill && c.w[s.dialect] < 0.15f)
            continue;
        idx[nW] = i;
        w[nW] = std::max (0.01, (double) c.w[s.dialect]);
        ++nW;
    }
    if (nW == 0)
        return false;
    const auto& c = vocab::kCells[idx[pickWeighted (w, nW, r)]];
    count = 0;
    for (int i = 0; i < c.n && count < kCap; ++i)
    {
        n[count].slot = c.slot[i];
        n[count].len2 = std::max (1, c.len2[i]);
        n[count].iv = c.iv[i];
        n[count].vel = c.vel[i];
        n[count].flags = c.vel[i] < 100 ? NoteGhost : 0;
        ++count;
    }
    if (c.seedOnly)
        mutateSeed (n, count, r);
    count = compactDropped (n, count);
    sortBar (n, count);
    return count > 0;
}

void ensureInbeat (BarNote* a, int na, BarNote* b, int nb)
{
    const int total = na + nb;
    if (total < 4)
        return;
    int cnt[4] {};
    for (int i = 0; i < na; ++i)
        ++cnt[a[i].slot & 3];
    for (int i = 0; i < nb; ++i)
        ++cnt[b[i].slot & 3];
    for (int need : { 1, 3, 2 })
    {
        if (cnt[need] * 10 >= total)
            continue;
        BarNote* n = na >= nb ? a : b;
        const int c = na >= nb ? na : nb;
        for (int i = 0; i < c; ++i)
        {
            if ((n[i].slot & 3) == need || (n[i].slot & 3) == 0)
                continue;
            int neu = (n[i].slot & ~3) + need;
            if (neu > 15)
                neu = need;
            if ((neu % 4) == 0 || slotTaken (n, c, neu, i))
                continue;
            n[i].slot = neu;
            ++cnt[need];
            break;
        }
    }
    sortBar (a, na);
    sortBar (b, nb);
}

void complementAnswer (const BarNote* call, int nc, BarNote* ans, int& na)
{
    bool used[16] {};
    int covered = 0;
    for (int i = 0; i < nc; ++i)
    {
        const int len = std::max (2, call[i].len2 / 2);
        for (int k = 0; k < len && call[i].slot + k < 16; ++k)
            if (! used[call[i].slot + k])
            {
                used[call[i].slot + k] = true;
                ++covered;
            }
    }
    const int want = clampi (std::max (nc + 2, 16 - covered - 2), 2, 8);
    na = 0;
    // Fill the back half first so bar-to-bar differences land on slots 8–15.
    for (int pass = 0; pass < 2 && na < want; ++pass)
    {
        const int begin = pass == 0 ? 8 : 0;
        const int end = pass == 0 ? 16 : 8;
        for (int s = begin; s < end && na < want && na < kCap; ++s)
        {
            if (used[s] || (s % 4) == 0)
                continue;
            ans[na].slot = s;
            ans[na].len2 = 2;
            ans[na].iv = (na & 1) ? 1 : 3;
            ans[na].vel = (na & 2) ? 80 : 112;
            ans[na].flags = 0;
            ++na;
        }
    }
    sortBar (ans, na);
}

// Consecutive bars should differ in the back half. The answer keeps the call's
// front half and carries the pitch/slot change from slot 8 onward.
void alignFrontHalf (const BarNote* call, int nc, BarNote* ans, int& na, int partner)
{
    BarNote back[kCap];
    int nb = 0;
    for (int i = 0; i < na; ++i)
        if (ans[i].slot >= 8 && nb < kCap)
            back[nb++] = ans[i];
    na = 0;
    for (int i = 0; i < nc && na < kCap; ++i)
        if (call[i].slot < 8)
            ans[na++] = call[i];
    for (int i = 0; i < nb && na < kCap; ++i)
    {
        bool same = false;
        for (int j = 0; j < nc; ++j)
            if (call[j].slot == back[i].slot && call[j].iv == back[i].iv)
                same = true;
        if (same)
            back[i].iv = partner == back[i].iv ? (partner == 0 ? 10 : 0) : partner;
        ans[na++] = back[i];
    }
    bool differed = false;
    for (int i = 0; i < na; ++i)
    {
        if (ans[i].slot < 8)
            continue;
        for (int j = 0; j < nc; ++j)
            if (call[j].slot == ans[i].slot && call[j].iv == ans[i].iv)
                differed = false;
        differed = true;
    }
    if (! differed && na < kCap)
    {
        ans[na].slot = 10;
        ans[na].len2 = 4;
        ans[na].iv = partner == 0 ? 10 : partner;
        ans[na].vel = 100;
        ans[na].flags = 0;
        if (! slotTaken (ans, na, ans[na].slot, -1))
            ++na;
    }
    sortBar (ans, na);
}

void soundingPc (const BarNote* n, int count, int pc[16])
{
    for (int s = 0; s < 16; ++s)
        pc[s] = -1;
    for (int i = 0; i < count; ++i)
    {
        const int len = std::max (1, n[i].len2 / 2);
        const int p = pcOf (n[i].iv);
        for (int k = 0; k < len && n[i].slot + k < 16; ++k)
            pc[n[i].slot + k] = p;
    }
}

int nextSlotOf (const BarNote* n, int count, int slot)
{
    int nxt = 16;
    for (int i = 0; i < count; ++i)
        if (n[i].slot > slot && n[i].slot < nxt)
            nxt = n[i].slot;
    return nxt;
}

bool extendOver (BarNote* n, int count, int slot, int lenCap)
{
    int best = -1;
    for (int i = 0; i < count; ++i)
    {
        if (n[i].slot > slot)
            continue;
        if (nextSlotOf (n, count, n[i].slot) <= slot)
            continue;
        if (best < 0 || n[i].slot > n[best].slot)
            best = i;
    }
    if (best < 0)
        return false;
    const int need = (slot - n[best].slot + 1) * 2;
    if (need > lenCap)
        return false;
    if (n[best].len2 < need)
        n[best].len2 = need;
    return true;
}

// Call keeps its rhythm. The answer stays the complement, back half first, and
// the two bars sound different pitches. Mutual rests would read as a repeated
// bar; a sustain across the kick covers them without an on-kick attack.
void paintGaskinPair (BarNote* call, int nc, BarNote* ans, int& na, Rng& r)
{
    const int lenCap = 6;
    bool skipped[16] {};
    for (int guard = 0; guard < 16; ++guard)
    {
        int cp[16], ap[16];
        soundingPc (call, nc, cp);
        soundingPc (ans, na, ap);
        int slot = -1;
        for (int s = 0; s < 16; ++s)
            if (! skipped[s] && cp[s] < 0 && ap[s] < 0)
            {
                slot = s;
                break;
            }
        if (slot < 0)
            break;
        if (extendOver (ans, na, slot, lenCap) || extendOver (call, nc, slot, lenCap))
            continue;
        if (na >= kCap || (slot % 4) == 0)
        {
            skipped[slot] = true;
            continue;
        }
        ans[na].slot = slot;
        ans[na].len2 = 2;
        ans[na].iv = 1;
        ans[na].vel = 80;
        ans[na].flags = 0;
        ++na;
        sortBar (ans, na);
    }

    const int pool[] = { 0, 1, 3, 10, 7, 8, 5, 2 };
    for (int i = 0; i < nc; ++i)
    {
        if (r.next() < 0.85)
            call[i].iv = pool[(int) (r.next() * 8.0) % 8];
        call[i].vel = (i & 2) ? 80 : 112;
    }
    int cp[16];
    soundingPc (call, nc, cp);
    for (int i = 0; i < na; ++i)
    {
        const int len = std::max (1, ans[i].len2 / 2);
        int iv = pool[(int) (r.next() * 8.0) % 8];
        for (int c = 0; c < 8; ++c)
        {
            iv = pool[(c + i) % 8];
            bool hit = false;
            for (int k = 0; k < len && ans[i].slot + k < 16; ++k)
                if (cp[ans[i].slot + k] == pcOf (iv))
                    hit = true;
            if (! hit)
                break;
        }
        ans[i].iv = iv;
        ans[i].vel = (i & 1) ? 80 : 112;
    }

    soundingPc (call, nc, cp);
    for (int i = 0; i < na; ++i)
    {
        const int len = std::max (1, ans[i].len2 / 2);
        bool hit = false;
        for (int k = 0; k < len && ans[i].slot + k < 16; ++k)
            if (cp[ans[i].slot + k] == pcOf (ans[i].iv))
                hit = true;
        if (! hit)
            continue;
        const int choices[] = { 3, 10, 7, 1, 0 };
        for (int c = 0; c < 5; ++c)
        {
            bool again = false;
            for (int k = 0; k < len && ans[i].slot + k < 16; ++k)
                if (cp[ans[i].slot + k] == pcOf (choices[c]))
                    again = true;
            if (! again)
            {
                ans[i].iv = choices[c];
                break;
            }
        }
    }
    sortBar (call, nc);
    sortBar (ans, na);
}

void lengthenGaps (BarNote* n, int count, int minLen2)
{
    sortBar (n, count);
    for (int i = 0; i < count; ++i)
    {
        const int gap = nextSlotOf (n, count, n[i].slot) - n[i].slot;
        const int room = std::min (minLen2, gap * 2);
        if (room > n[i].len2)
            n[i].len2 = std::max (2, room);
    }
}

bool placeNote (BarNote* n, int& count, int slot, int iv, int len2)
{
    if (count >= kCap || slot < 0 || slot > 15 || (slot % 4) == 0)
        return false;
    if (slotTaken (n, count, slot, -1))
        return false;
    n[count].slot = slot;
    n[count].len2 = len2;
    n[count].iv = iv;
    n[count].vel = 112;
    n[count].flags = 0;
    ++count;
    sortBar (n, count);
    return true;
}

// Long cells sit under the lane's attacks-per-bar floor. Add a fixed handful
// so the low-busy and high-busy ends of the macro sweep rise together.
void thickenPair (BarNote* call, int& nc, BarNote* ans, int& na, int partner, float busy)
{
    const int addCall = std::max (1, (int) std::lround (1.2f + 1.2f * busy));
    const int addAns = addCall + 1;
    const int callSlots[] = { 10, 2, 6, 14, 1, 5 };
    const int ansSlots[] = { 9, 11, 13, 15, 3, 7, 14 };
    auto add = [&] (BarNote* n, int& count, const int* slots, int nSlots, int want, int iv)
    {
        int placed = 0;
        for (int i = 0; i < nSlots && placed < want; ++i)
        {
            const int slot = slots[i];
            const int len2 = (slot + 1 > 15 || slotTaken (n, count, slot + 1, -1)) ? 2 : 4;
            if (placeNote (n, count, slot, iv, len2))
                ++placed;
        }
    };
    add (call, nc, callSlots, 6, addCall, 0);
    add (ans, na, ansSlots, 7, addAns, partner == 0 ? 10 : partner);
}

float attackMatch (const BarNote* a, int na, const BarNote* b, int nb)
{
    bool pa[16] {}, pb[16] {};
    int ia[16] {}, ib[16] {};
    for (int i = 0; i < na; ++i)
        if (a[i].slot >= 0 && a[i].slot < 16)
        {
            pa[a[i].slot] = true;
            ia[a[i].slot] = a[i].iv;
        }
    for (int i = 0; i < nb; ++i)
        if (b[i].slot >= 0 && b[i].slot < 16)
        {
            pb[b[i].slot] = true;
            ib[b[i].slot] = b[i].iv;
        }
    int either = 0, match = 0;
    for (int s = 0; s < 16; ++s)
    {
        if (! pa[s] && ! pb[s])
            continue;
        ++either;
        if (pa[s] && pb[s] && pcOf (ia[s]) == pcOf (ib[s]))
            ++match;
    }
    return either == 0 ? 0.f : (float) match / (float) either;
}

void loosenAnswer (BarNote* call, int nc, BarNote* ans, int& na, int partner)
{
    const int extra[] = { 15, 13, 11, 9, 14, 7 };
    for (int guard = 0; guard < 6 && attackMatch (call, nc, ans, na) > 0.64f; ++guard)
    {
        bool flipped = false;
        for (int i = na - 1; i >= 0 && ! flipped; --i)
        {
            if (ans[i].slot < 8)
                continue;
            for (int j = 0; j < nc; ++j)
                if (call[j].slot == ans[i].slot && pcOf (call[j].iv) == pcOf (ans[i].iv))
                {
                    const int neu = pcOf (ans[i].iv) == pcOf (partner) ? (partner == 0 ? 10 : 0) : partner;
                    ans[i].iv = neu;
                    flipped = true;
                    break;
                }
        }
        if (flipped)
            continue;
        bool placed = false;
        for (int slot : extra)
            if (placeNote (ans, na, slot, partner == 0 ? 3 : partner, 4))
            {
                placed = true;
                break;
            }
        if (! placed)
            break;
    }
}

void spreadInbeat (BarNote bars[][kCap], int* counts, int nb)
{
    for (int guard = 0; guard < 24; ++guard)
    {
        int cnt[4] {}, total = 0;
        for (int b = 0; b < nb; ++b)
            for (int i = 0; i < counts[b]; ++i)
            {
                ++cnt[bars[b][i].slot & 3];
                ++total;
            }
        if (total < 8)
            return;
        const int need = (int) std::ceil (0.12 * (double) total);
        int poor = -1;
        for (int p : { 1, 3, 2 })
            if (cnt[p] < need)
            {
                poor = p;
                break;
            }
        if (poor < 0)
            return;
        int donor = 1;
        for (int p = 2; p <= 3; ++p)
            if (cnt[p] > cnt[donor])
                donor = p;
        if (cnt[donor] <= need || donor == poor)
            return;
        bool moved = false;
        for (int b = 0; b < nb && ! moved; ++b)
            for (int i = counts[b] - 1; i >= 0 && ! moved; --i)
            {
                if ((bars[b][i].slot & 3) != donor)
                    continue;
                const int from = bars[b][i].slot;
                const int neu = (from & ~3) + poor;
                if (neu > 15 || (neu % 4) == 0)
                    continue;
                bool ok = true;
                int hits = 0;
                for (int bb = 0; bb < nb; ++bb)
                    for (int j = 0; j < counts[bb]; ++j)
                        if (bars[bb][j].slot == from)
                        {
                            ++hits;
                            if (slotTaken (bars[bb], counts[bb], neu, j))
                                ok = false;
                        }
                if (! ok || hits == 0)
                    continue;
                for (int bb = 0; bb < nb; ++bb)
                    for (int j = 0; j < counts[bb]; ++j)
                        if (bars[bb][j].slot == from)
                            bars[bb][j].slot = neu;
                moved = true;
            }
        if (! moved)
            return;
        for (int b = 0; b < nb; ++b)
            sortBar (bars[b], counts[b]);
    }
}

void openDensity (BarNote bars[][kCap], int* counts, int nb)
{
    int mx = 0, mn = 99;
    for (int b = 0; b < nb; ++b)
    {
        mx = std::max (mx, counts[b]);
        mn = std::min (mn, counts[b]);
    }
    if (mx - mn >= 2)
        return;
    const int bar = nb >= 6 ? 5 : nb - 1;
    int dropped = 0;
    while (dropped < 2 && counts[bar] > 2)
    {
        int idx = -1;
        for (int i = counts[bar] - 1; i >= 0; --i)
            if (bars[bar][i].slot >= 8)
            {
                idx = i;
                break;
            }
        if (idx < 0)
            idx = counts[bar] - 1;
        for (int k = idx; k < counts[bar] - 1; ++k)
            bars[bar][k] = bars[bar][k + 1];
        --counts[bar];
        ++dropped;
    }
}

void openBar8 (BarNote* bar, int& count, const BarNote* other, int nOther)
{
    int w = 0;
    for (int i = 0; i < count; ++i)
        if (bar[i].slot < 12)
            bar[w++] = bar[i];
    count = w;
    if (count == 0)
    {
        bar[0].slot = 3;
        bar[0].len2 = 4;
        bar[0].iv = 0;
        bar[0].vel = 112;
        bar[0].flags = 0;
        count = 1;
    }
    for (int i = 0; i < count; ++i)
        if (bar[i].slot + bar[i].len2 / 2 > 12)
            bar[i].len2 = std::max (2, (12 - bar[i].slot) * 2);
    sortBar (bar, count);
    bool same = count == nOther;
    if (same)
    {
        for (int i = 0; i < count; ++i)
        {
            const int ap = (bar[i].iv % 12 + 12) % 12;
            const int bp = (other[i].iv % 12 + 12) % 12;
            if (bar[i].slot != other[i].slot || ap != bp)
                same = false;
        }
    }
    if (same)
        bar[count - 1].iv = bar[count - 1].iv == 0 ? 10 : 0;
}

void capOctaves (Lane& lane)
{
    int guard = 0;
    while (guard++ < 8 && lane.count > 1)
    {
        int moves = 0, oct = 0;
        for (int i = 1; i < lane.count; ++i)
        {
            const int d = lane.notes[i].chord[0] - lane.notes[i - 1].chord[0];
            ++moves;
            if (std::abs (d) == 12 || std::abs (d) == 24)
                ++oct;
        }
        if (moves == 0 || oct * 4 <= moves)
            return;
        for (int i = 1; i < lane.count; ++i)
        {
            const int d = lane.notes[i].chord[0] - lane.notes[i - 1].chord[0];
            if (std::abs (d) == 12 || std::abs (d) == 24)
            {
                lane.notes[i].chord[0] = (std::int8_t) (lane.notes[i].chord[0] - (d > 0 ? 12 : -12));
                break;
            }
        }
    }
}

void genBass (Lane& lane, const Settings& s, Rng& r, const bool* kick, const bool* stabAt)
{
    const int nb = s.bars;
    BarNote bars[8][kCap];
    int counts[8] {};
    const int partner = pickPartner (s, r);
    const bool gaskin = s.dialect == Gaskin;
    if (! (r.next() < 0.45 && loadVocabBar (bars[0], counts[0], s, r, 0, false)))
    {
        const bool useTwo = r.next() < 0.45 && corpus::kBassN > 0;
        const int call = pickBassCell (s, r, useTwo ? 2 : 1, false);
        loadBassCell (corpus::kBass[call], 0, bars[0], counts[0]);
        if (nb > 1 && useTwo)
            loadBassCell (corpus::kBass[call], 16, bars[1], counts[1]);
    }
    if (nb > 1 && counts[1] == 0)
    {
        if (! (r.next() < 0.55 && loadVocabBar (bars[1], counts[1], s, r, 0, false)))
        {
            const int answer = pickBassCell (s, r, 1, false);
            loadBassCell (corpus::kBass[answer], 0, bars[1], counts[1]);
        }
    }
    prepareBar (bars[0], counts[0], s, r, 0, false, partner, 0, kick, stabAt);
    if (nb > 1)
        prepareBar (bars[1], counts[1], s, r, 1, false, partner, 1, kick, stabAt);
    if (gaskin && nb > 1)
    {
        complementAnswer (bars[0], counts[0], bars[1], counts[1]);
        paintGaskinPair (bars[0], counts[0], bars[1], counts[1], r);
    }
    else if (nb > 1)
    {
        alignFrontHalf (bars[0], counts[0], bars[1], counts[1], partner);
        ensureInbeat (bars[0], counts[0], bars[1], counts[1]);
        alignFrontHalf (bars[0], counts[0], bars[1], counts[1], partner);
        if (s.dialect == Long)
        {
            lengthenGaps (bars[0], counts[0], 4);
            lengthenGaps (bars[1], counts[1], 4);
            thickenPair (bars[0], counts[0], bars[1], counts[1], partner, s.busy);
            loosenAnswer (bars[0], counts[0], bars[1], counts[1], partner);
        }
    }
    for (int b = 0; b < std::min (nb, 2); ++b)
    {
        if (counts[b] > 0)
            continue;
        bars[b][0].slot = gaskin ? 3 : 2;
        bars[b][0].len2 = 4;
        bars[b][0].iv = 0;
        bars[b][0].vel = 112;
        bars[b][0].flags = 0;
        counts[b] = 1;
    }
    for (int b = 2; b < nb; ++b)
        copyBar (bars[b & 1], counts[b & 1], bars[b], counts[b]);
    if (nb >= 4)
    {
        auto nudge = [&] (int bar)
        {
            for (int i = 0; i < counts[bar]; ++i)
            {
                if (bars[bar][i].slot < 8)
                    continue;
                bars[bar][i].iv = foldIv (partner == 0 ? 10 : partner, s.dark, s.dialect);
                break;
            }
        };
        if (! gaskin)
            nudge (std::min (3, nb - 1));
        if (nb == 8)
            openBar8 (bars[nb - 1], counts[nb - 1], bars[3], counts[3]);
    }
    if (! gaskin && nb > 2)
    {
        spreadInbeat (bars, counts, nb);
        openDensity (bars, counts, nb);
    }

    lane.count = 0;
    const int reach = s.dialect == Long ? 6 : (s.dialect == Pop ? 0 : 3);
    for (int b = 0; b < nb; ++b)
    {
        for (int i = 0; i < counts[b] && lane.count < kMaxNotes; ++i)
        {
            Note note;
            note.start = (float) (b * 16 + bars[b][i].slot);
            float len = (float) bars[b][i].len2 * 0.5f;
            if (reach > 0)
            {
                const int nextKick = (bars[b][i].slot / 4 + 1) * 4;
                const int gap = nextKick - bars[b][i].slot;
                if (gap > 0 && gap <= reach && len < (float) gap)
                    len = (float) gap;
            }
            if (gaskin)
                len = std::min (len, 2.5f);
            if (b == nb - 1)
            {
                const float end = (float) (b * 16 + 12);
                if (note.start >= end)
                    continue;
                if (note.start + len > end)
                    len = std::max (0.5f, end - note.start);
            }
            note.length = std::max (0.5f, len);
            note.chord[0] = (std::int8_t) bars[b][i].iv;
            note.nChord = 1;
            note.vel = (std::uint8_t) bars[b][i].vel;
            note.flags = bars[b][i].flags;
            lane.notes[lane.count++] = note;
        }
    }
    std::sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
    const float loop = (float) nb * 16.f;
    for (int i = 0; i < lane.count; ++i)
    {
        const float next = (i + 1 < lane.count) ? lane.notes[i + 1].start : loop;
        if (lane.notes[i].start + lane.notes[i].length > next)
            lane.notes[i].length = std::max (0.5f, next - lane.notes[i].start);
        if (lane.notes[i].start + lane.notes[i].length > loop)
            lane.notes[i].length = std::max (0.f, loop - lane.notes[i].start);
    }
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
        if (lane.notes[i].length > 0.2f)
            lane.notes[w++] = lane.notes[i];
    lane.count = w;
    for (int i = 3; i < lane.count; ++i)
    {
        if (lane.notes[i].start - lane.notes[i - 3].start < 3.1f)
        {
            for (int k = i; k < lane.count - 1; ++k)
                lane.notes[k] = lane.notes[k + 1];
            --lane.count;
            --i;
        }
    }
    capOctaves (lane);
}

int pickStabCell (const Settings& s, Rng& r, bool wantPad)
{
    double w[768];
    const int n = std::min (corpus::kStabN, 768);
    for (int i = 0; i < n; ++i)
    {
        const auto& c = corpus::kStab[i];
        float fit = 0.4f;
        if (wantPad)
            fit *= c.pad ? 1.6f : 0.25f;
        else
            fit *= c.pad ? 0.2f : 1.f;
        const float on = (float) c.onbeat / 100.f;
        fit *= 1.1f - 0.8f * on;
        const bool darkClass = c.klass <= corpus::VcMin11;
        const bool brightClass = c.klass == corpus::VcFifth || c.klass == corpus::VcDyad || c.klass == corpus::VcSingle;
        if (s.dark > 0.55f && darkClass)
            fit *= 1.6f;
        if (s.dark < 0.45f && brightClass)
            fit *= 1.6f;
        if (s.dialect == Gaskin && (c.klass == corpus::VcDyad || c.klass == corpus::VcOther || c.pad))
            fit *= 1.4f;
        w[i] = (double) artistW (s.dialect, c.artist) * fit;
    }
    return pickWeighted (w, n, r);
}

int pickVoicing (const Settings& s, Rng& r)
{
    double w[768];
    const int n = std::min (corpus::kVoicingN, 768);
    for (int i = 0; i < n; ++i)
    {
        const auto& v = corpus::kVoicing[i];
        float fit = 0.45f;
        const bool darkClass = v.klass <= corpus::VcMin11;
        const bool brightClass = v.klass == corpus::VcFifth || v.klass == corpus::VcDyad;
        if (v.klass == corpus::VcOther)
            fit *= 0.04f;
        if (v.klass == corpus::VcQuartal)
            fit *= s.dark > 0.35f ? 2.4f : 1.2f;
        if (v.klass == corpus::VcMin7 || v.klass == corpus::VcMin9 || v.klass == corpus::VcMin11)
            fit *= s.dark > 0.4f ? 1.8f : 0.7f;
        if (s.dark > 0.5f && darkClass)
            fit *= 1.5f;
        if (s.dark < 0.5f && brightClass)
            fit *= 1.8f;
        if (s.dialect == Pop && v.klass == corpus::VcFifth)
            fit *= 1.8f;
        if (s.dialect == Gaskin && v.n >= 2 && v.klass != corpus::VcOther)
            fit *= 1.4f;
        if (v.n == 0 || v.klass == corpus::VcSingle)
            fit *= 0.35f;
        w[i] = (double) artistW (s.dialect, v.artist) * fit;
    }
    return pickWeighted (w, n, r);
}

void genStab (Lane& lane, const Lane& bass, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    // Default knob stays in the dub band (about one hit a bar). The top of the
    // knob crosses into the riff band, 4-8 hits a bar, mostly on the e and a.
    const bool riff = s.stab >= 0.72f;
    const float perBar = s.stab < 0.03f ? 0.f
                         : (riff ? lerp (4.f, 8.f, (s.stab - 0.72f) / 0.28f)
                                 : 1.6f * s.stab);
    const int want = clampi ((int) std::round (perBar * (float) s.bars), 0, 48);
    if (want <= 0 || corpus::kStabN <= 0)
        return;

    bool attack[kMaxSteps] {};
    bool covered[kMaxSteps] {};
    for (int i = 0; i < bass.count; ++i)
    {
        const int a = clampi ((int) std::floor (bass.notes[i].start + 1.0e-3f), 0, loop);
        const int b = clampi ((int) std::ceil (bass.notes[i].start + bass.notes[i].length - 1.0e-3f), 0, loop);
        if (a < loop)
            attack[a] = true;
        for (int t = a; t < b && t < loop; ++t)
            covered[t] = true;
    }

    const bool gaskin = s.dialect == Gaskin;
    const int cell = pickStabCell (s, r, gaskin);
    const auto& sc = corpus::kStab[cell];
    int voiceA = pickVoicing (s, r);
    int voiceB = pickVoicing (s, r);
    if (voiceB == voiceA && corpus::kVoicingN > 1)
        voiceB = (voiceA + 1 + (int) (r.next() * 5.0)) % corpus::kVoicingN;

    auto writeChord = [&] (int step, float len, int voice, int vel)
    {
        if (lane.count >= kMaxNotes || step < 0 || step >= loop)
            return;
        const auto& v = corpus::kVoicing[voice];
        Note note;
        note.start = (float) step;
        note.length = std::min (len, (float) loop - (float) step);
        note.nChord = (std::uint8_t) clampi (v.n, 1, 4);
        for (int c = 0; c < note.nChord; ++c)
        {
            int iv = v.iv[c];
            if (s.dark > 0.7f && iv >= 12)
                iv -= 12;
            if (s.dark < 0.3f && c == note.nChord - 1 && iv < 12 && r.next() < 0.4)
                iv += 12;
            note.chord[c] = (std::int8_t) snapScale (clampi (iv, -12, 24), s.dialect);
        }
        compactChord (note.chord, note.nChord);
        for (int c = 0; c < note.nChord; ++c)
            note.chord[c] = (std::int8_t) snapScale (note.chord[c], s.dialect);
        // Keep the corpus colour and move only the top tone a semitone off
        // the root. A minor 2nd is not a reference quartal or triad, and the
        // other voices stay so stabs do not collapse onto one dyad.
        if (note.nChord >= 2)
        {
            const int top = note.nChord - 1;
            const int root = note.chord[0];
            int spice = snapScale (root + 1, s.dialect);
            for (int c = 0; c < top; ++c)
                if (pcOf (note.chord[c]) == pcOf (spice))
                    spice = snapScale (root + 10, s.dialect);
            if (pcOf (spice) == pcOf (note.chord[top]))
                spice = snapScale (root + (pcOf (spice) == pcOf (root) ? 10 : 3), s.dialect);
            note.chord[top] = (std::int8_t) spice;
            compactChord (note.chord, note.nChord);
        }
        note.vel = (std::uint8_t) vel;
        lane.notes[lane.count++] = note;
    };

    if (gaskin)
    {
        const float len = lerp (8.f, 14.f, 1.f - s.busy * 0.3f);
        for (int b = 0; b < s.bars; b += 2)
        {
            int step = b * 16 + (r.next() < 0.5 ? 1 : 2);
            if (attack[step] && step + 1 < loop && ! attack[step + 1])
                ++step;
            writeChord (step, len, ((b / 2) & 1) ? voiceB : voiceA, 92);
        }
        return;
    }

    // Prefer the stab cell's own off-beat slots, then snap each one into a bass hole.
    int slots[8];
    int nSlots = 0;
    for (int i = 0; i < sc.n && nSlots < 8; ++i)
    {
        const int slot = corpus::kStabHit[sc.off + i].slot;
        if (slot % 4 != 0)
            slots[nSlots++] = slot;
    }
    if (nSlots == 0)
    {
        slots[0] = 13;
        slots[1] = 15;
        nSlots = 2;
    }

    int placed = 0;
    for (int b = 0; b < s.bars && placed < want; ++b)
    {
        const int role = s.bars == 2 ? (b == 0 ? 0 : 3) : (b % 4);
        float chance = (role == 1 || role == 3) ? 0.95f : 0.35f;
        chance *= lerp (0.15f, 1.f, s.stab);
        if (b == s.bars - 1)
            chance = 1.f;
        if (r.next() > chance && placed > 0)
            continue;
        const int budget = (role == 3 && b == s.bars - 1) ? std::min (3, want - placed) : (r.next() < lerp (0.2f, 0.85f, s.stab) ? 2 : 1);
        int got = 0;
        for (int i = 0; i < nSlots && got < budget && placed < want; ++i)
        {
            int step = b * 16 + slots[(i + b) % nSlots];
            if (step >= loop)
                continue;
            bool found = false;
            for (int rad = 0; rad <= 3 && ! found; ++rad)
            {
                for (int dir : { 0, 1, -1 })
                {
                    if (rad == 0 && dir != 0)
                        continue;
                    const int t = step + dir * std::max (1, rad);
                    if (t < b * 16 || t >= std::min (loop, (b + 1) * 16))
                        continue;
                    if ((t % 4) == 0)
                        continue;
                    if (riff && (t % 2) == 0 && r.next() < 0.7)
                        continue;
                    // Rests are a preference. About half of stab hits may share a bass attack.
                    if (attack[t] && r.next() < 0.5)
                        continue;
                    bool clash = false;
                    for (int k = 0; k < lane.count; ++k)
                        if (std::fabs (lane.notes[k].start - (float) t) < 1.5f)
                            clash = true;
                    if (clash)
                        continue;
                    // Holes score better; a sustaining bass is still allowed.
                    if (rad == 0 || ! covered[t] || r.next() < 0.5)
                    {
                        const float len16 = gaskin ? 8.f : (s.dialect == Long ? (r.next() < 0.5 ? 2.f : 4.f) : (r.next() < 0.6 ? 1.f : 2.f));
                        const int voice = (b >= s.bars - 1 && got == budget - 1) ? voiceB : voiceA;
                        writeChord (t, len16, voice, (b == s.bars - 1 && got == budget - 1) ? 112 : 98);
                        ++got;
                        ++placed;
                        found = true;
                        break;
                    }
                }
            }
        }
    }
}

int pickDrumCell (const Settings& s, Rng& r, bool preferFill)
{
    double w[768];
    const int n = std::min (corpus::kDrumN, 768);
    const float oddTarget = lerp (0.1f, 0.85f, s.bounce) * 100.f;
    const float hatTarget = lerp (4.f, 12.f, s.busy);
    for (int i = 0; i < n; ++i)
    {
        const auto& c = corpus::kDrum[i];
        float fit = 0.4f + 0.6f * (1.f - std::min (1.f, std::fabs ((float) c.odd - oddTarget) / 100.f));
        fit *= 1.f - 0.5f * std::min (1.f, std::fabs ((float) c.hats - hatTarget) / 10.f);
        // Corpus swing above 57 is the Ellia outlier. High bounce may use it; low bounce avoids it.
        const float swing = (float) c.swing;
        if (swing > 57.f && s.bounce < 0.75f)
            fit *= 0.2f;
        if (s.bounce >= 0.75f && swing >= 62.f && swing <= 66.f)
            fit *= 1.6f;
        if (preferFill)
            fit *= c.fill ? 3.f : 0.4f;
        if (s.dialect == Gaskin && c.artist == corpus::ArGaskin)
            fit *= 2.f;
        w[i] = (double) artistW (s.dialect, c.artist) * fit;
    }
    return pickWeighted (w, n, r);
}

int scaleVel (int v, int lo, int hi) noexcept
{
    const float t = std::clamp ((v - 64) / 63.f, 0.f, 1.f);
    return clampi (lo + (int) std::lround ((hi - lo) * t), 1, 127);
}

void paintDrumCell (std::uint8_t grid[corpus::ArCount][16], const corpus::DrumCell& cell)
{
    std::memset (grid, 0, corpus::ArCount * 16);
    for (int i = 0; i < cell.n; ++i)
    {
        const auto& h = corpus::kDrumHit[cell.off + i];
        if (h.voice < 10 && h.slot < 16)
            grid[h.voice][h.slot] = h.vel;
    }
}

bool stabCoversStep (const Lane& stab, int step) noexcept
{
    const float t = (float) step;
    for (int i = 0; i < stab.count; ++i)
    {
        const float a = stab.notes[i].start;
        const float b = a + stab.notes[i].length;
        if (t + 0.2f >= a && t <= b + 0.05f)
            return true;
    }
    return false;
}

void genDrums (DrumLane& d, const Groove& g, const Settings& s, Rng& r)
{
    clearDrums (d);
    const int steps = s.bars * 16;
    const bool gaskin = s.dialect == Gaskin;
    const int mainI = pickDrumCell (s, r, false);
    const int altI = pickDrumCell (s, r, false);
    const int fillI = pickDrumCell (s, r, true);
    std::uint8_t mainG[10][16], altG[10][16], fillG[10][16];
    paintDrumCell (mainG, corpus::kDrum[mainI]);
    paintDrumCell (altG, corpus::kDrum[altI]);
    paintDrumCell (fillG, corpus::kDrum[fillI]);

    static const int shaker[4] = { 56, 14, 100, 45 };
    for (int t = 0; t < steps; ++t)
    {
        const int s16 = t % 16;
        const int bar = t / 16;
        int role = 0;
        if (s.bars == 2)
            role = bar == 0 ? 0 : 3;
        else
            role = bar % 4;
        const std::uint8_t (*src)[16] = (role == 1) ? altG : mainG;
        if (role == 3)
            src = fillG;

        d.vel[Kick][t] = (s16 % 4 == 0) ? 112 : 0;
        d.accent[t] = (s16 % 4 == 0) ? 1 : 0;
        if (s.bounce > 0.55f && ! gaskin && (s16 == 14 || s16 == 6) && src[Kick][s16] > 0 && r.next() < (double) s.bounce * 0.7)
            d.vel[Kick][t] = 72;

        if (s16 == 4 || s16 == 12)
            d.vel[Clap][t] = 108;
        else if (! gaskin && src[Clap][s16] > 0 && (s16 & 1) && r.next() < lerp (0.05, 0.55, s.bounce))
            d.vel[Clap][t] = (std::uint8_t) scaleVel (src[Clap][s16], 40, 78);

        const bool odd = (s16 & 1) != 0;
        const float keepOdd = gaskin ? (s.bounce * 0.45f) : lerp (0.08f, 0.95f, s.bounce) * lerp (0.45f, 1.f, s.busy);
        const float keepAnd = gaskin ? 0.95f : lerp (0.9f, 0.45f, s.bounce);
        if (src[HatO][s16] || src[HatC][s16])
        {
            const bool keep = odd ? (r.next() < keepOdd) : ((s16 % 4 == 2) ? (r.next() < keepAnd) : (r.next() < 0.35f * s.busy));
            if (keep)
            {
                if (src[HatO][s16] >= src[HatC][s16])
                    d.vel[HatO][t] = (std::uint8_t) scaleVel (src[HatO][s16], 88, 116);
                else
                    d.vel[HatC][t] = (std::uint8_t) scaleVel (src[HatC][s16], 36, 84);
            }
        }
        else if (! odd && s16 % 4 == 2 && r.next() < keepAnd)
            d.vel[HatO][t] = gaskin ? 104 : 100;
        else if (odd && r.next() < keepOdd * 0.55)
            d.vel[HatC][t] = (std::uint8_t) (s16 % 4 == 3 ? 70 : 42);

        if (gaskin)
        {
            if (s16 % 4 == 2)
            {
                d.vel[HatO][t] = (bar == s.bars - 1) ? 116 : 104;
                d.vel[HatC][t] = 0;
                d.vel[Ride][t] = 96;
            }
            if (s16 % 4 == 0)
                d.vel[HatC][t] = 48;
        }

        if (src[Snare][s16] && (role == 3 || s.fill))
            d.vel[Snare][t] = (std::uint8_t) scaleVel (src[Snare][s16], 70, 118);
        if (src[TomLow][s16] && role == 3)
            d.vel[TomLow][t] = (std::uint8_t) scaleVel (src[TomLow][s16], 80, 114);
        if (src[TomMid][s16] && role == 3)
            d.vel[TomMid][t] = (std::uint8_t) scaleVel (src[TomMid][s16], 80, 114);
        if (src[Rim][s16] && ! stabCoversStep (g.lane[Stab], t) && r.next() < 0.8)
            d.vel[Rim][t] = (std::uint8_t) scaleVel (src[Rim][s16], 50, 96);
        if (src[Ride][s16] && gaskin)
            d.vel[Ride][t] = (std::uint8_t) scaleVel (src[Ride][s16], 70, 110);

        if (! gaskin)
        {
            const float p = lerp (0.15f, 0.95f, s.busy) * (odd ? lerp (0.2f, 1.f, s.bounce) : lerp (1.f, 0.55f, s.bounce));
            if (r.next() < p)
                d.vel[Perc][t] = (std::uint8_t) shaker[s16 & 3];
        }
    }

    auto differ = [&] (int a, int b)
    {
        if (a < 0 || b < 0 || a >= s.bars || b >= s.bars)
            return true;
        for (int s16 = 0; s16 < 16; ++s16)
        {
            const int ta = a * 16 + s16;
            const int tb = b * 16 + s16;
            if ((d.vel[HatC][ta] > 0) != (d.vel[HatC][tb] > 0))
                return true;
            if ((d.vel[HatO][ta] > 0) != (d.vel[HatO][tb] > 0))
                return true;
            if ((d.vel[Perc][ta] > 0) != (d.vel[Perc][tb] > 0))
                return true;
            if ((d.vel[Snare][ta] > 0) != (d.vel[Snare][tb] > 0))
                return true;
        }
        return false;
    };
    const int varBar = s.bars >= 4 ? 3 : s.bars - 1;
    if (! differ (0, varBar) && varBar * 16 + 10 < steps)
    {
        d.vel[TomLow][varBar * 16 + 14] = 100;
        if (d.vel[HatO][varBar * 16 + 10] == 0)
            d.vel[HatC][varBar * 16 + 10] = 64;
    }
    const int last = s.bars - 1;
    if (last != varBar && ! differ (0, last))
        d.vel[TomLow][last * 16 + 14] = 104;
    if (s.fill && steps >= 16)
    {
        const int b0 = last * 16;
        for (int s16 = 8; s16 < 16; ++s16)
            d.vel[Snare][b0 + s16] = (std::uint8_t) std::min (127, 74 + (s16 - 8) * 6);
        d.vel[TomMid][b0 + 10] = 100;
        d.vel[TomLow][b0 + 13] = std::max<std::uint8_t> (d.vel[TomLow][b0 + 13], 108);
        d.vel[HatO][b0 + 14] = std::max<std::uint8_t> (d.vel[HatO][b0 + 14], 110);
    }
    else if (last >= 0)
    {
        // Bar 8 dropout: the last quarter rests so the bass hole can speak.
        const int b0 = last * 16;
        d.vel[Kick][b0 + 12] = 0;
        d.accent[b0 + 12] = 0;
        d.vel[HatO][b0 + 14] = std::max<std::uint8_t> (d.vel[HatO][b0 + 14], 108);
    }

    for (int t = 0; t < steps; ++t)
        if (d.vel[Rim][t] && stabCoversStep (g.lane[Stab], t))
            d.vel[Rim][t] = 0;
}

int pickChop (const Settings& s, Rng& r)
{
    double w[128];
    const int n = std::min (corpus::kChopN, 128);
    if (n <= 0)
        return -1;
    for (int i = 0; i < n; ++i)
        w[i] = artistW (s.dialect, corpus::kChop[i].artist);
    return pickWeighted (w, n, r);
}

bool laneAttacks (const Lane& lane, int step) noexcept
{
    for (int i = 0; i < lane.count; ++i)
        if ((int) std::floor (lane.notes[i].start + 1.0e-3f) == step)
            return true;
    return false;
}

void genHits (Lane& lane, const Groove& g, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    int want = (int) std::round (lerp (1.f, 9.f, s.busy * 0.45f + s.stab * 0.55f));
    if (s.stab < 0.08f)
        want = std::min (want, 3);
    want = clampi (want, 0, 10);
    const int chop = pickChop (s, r);
    const int voice = pickVoicing (s, r);
    int slots[8];
    int nSlots = 0;
    if (chop >= 0)
    {
        const auto& c = corpus::kChop[chop];
        for (int i = 0; i < c.n && nSlots < 8; ++i)
            slots[nSlots++] = corpus::kChopSlot[c.off + i];
    }
    if (nSlots == 0)
    {
        slots[0] = 6;
        slots[1] = 14;
        nSlots = 2;
    }
    const int spin = (int) (r.next() * 15.0);
    for (int n = 0; n < want && lane.count < kMaxNotes; ++n)
    {
        int best = -1;
        float bestS = -1.f;
        for (int t = 0; t < loop; ++t)
        {
            if ((t % 4) == 0 || laneAttacks (g.lane[Bass], t) || laneAttacks (g.lane[Stab], t))
                continue;
            if (g.drums.vel[Kick][t] > 0)
                continue;
            bool clash = false;
            for (int k = 0; k < lane.count; ++k)
                if (std::fabs (lane.notes[k].start - (float) t) < 3.f)
                    clash = true;
            if (clash)
                continue;
            float sc = (float) r.next();
            const int slot = (t + spin) % 16;
            for (int i = 0; i < nSlots; ++i)
                if (slot == slots[i])
                    sc += 0.8f;
            if (((t / 16) % 2) == 1)
                sc += 0.25f;
            if (sc > bestS)
            {
                bestS = sc;
                best = t;
            }
        }
        if (best < 0)
            break;
        Note note;
        note.start = (float) best;
        note.length = r.next() < 0.65 ? 1.f : 1.5f;
        const auto& v = corpus::kVoicing[voice];
        note.nChord = (std::uint8_t) clampi (std::max (1, (int) v.n), 1, 3);
        for (int c = 0; c < note.nChord; ++c)
            note.chord[c] = v.iv[c];
        compactChord (note.chord, note.nChord);
        if (s.dialect == Pop)
        {
            note.chord[0] = 0;
            note.chord[1] = 7;
            note.nChord = 2;
        }
        note.vel = 106;
        note.timbre = (std::uint8_t) ((int) (r.next() * 5.0) % 5);
        lane.notes[lane.count++] = note;
    }
}

void genPluck (Lane& lane, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    const int chopA = pickChop (s, r);
    const int chopB = pickChop (s, r);
    const int contourA = pickContour (s, r);
    const int contourB = pickContour (s, r);
    const int spin = (int) (r.next() * 14.0);
    int baseSlots[8];
    int nBase = 0;
    const int chop = chopA;
    if (chop >= 0 && corpus::kChop[chop].n > 0)
    {
        const auto& c = corpus::kChop[chop];
        for (int i = 0; i < c.n && nBase < 8; ++i)
            baseSlots[nBase++] = corpus::kChopSlot[c.off + i];
    }
    if (nBase == 0)
    {
        baseSlots[0] = 2;
        baseSlots[1] = 6;
        baseSlots[2] = 10;
        baseSlots[3] = 14;
        nBase = 4;
    }
    int nSlots = clampi ((int) std::round (lerp (2.f, 5.f, s.busy)) + ((int) (r.next() * 3.0) - 1), 2, 5);
    nSlots = std::min (nSlots, std::max (2, nBase));
    const int partner = pickPartner (s, r);
    const int dropAt = (int) (r.next() * nSlots);
    for (int b = 0; b < s.bars; ++b)
    {
        const bool turn = b == s.bars - 1 || (s.bars >= 4 && b == 3);
        const int contour = turn ? contourB : contourA;
        const int srcChop = turn ? chopB : chopA;
        for (int i = 0; i < nSlots && lane.count < kMaxNotes; ++i)
        {
            if ((b % 4) == 1 && i == dropAt)
                continue;
            int slot = baseSlots[i % nBase];
            if (srcChop >= 0 && turn && corpus::kChop[srcChop].n > 0)
                slot = corpus::kChopSlot[corpus::kChop[srcChop].off + (i % corpus::kChop[srcChop].n)];
            slot = (slot + spin) % 16;
            if (s.dialect != Gaskin && (slot % 4) == 0)
                slot = (slot + (s.bounce > 0.5f ? 1 : 2)) % 16;
            if (turn && i == 0)
                slot = (slot + 1) % 16;
            const float start = (float) (b * 16 + slot);
            if (start >= loop)
                continue;
            bool clash = false;
            for (int k = 0; k < lane.count; ++k)
                if (std::fabs (lane.notes[k].start - start) < 0.5f)
                    clash = true;
            if (clash)
                continue;
            int iv = 0;
            if (contour >= 0 && corpus::kContour[contour].n > 0)
                iv = corpus::kContourIv[corpus::kContour[contour].off + ((b * 3 + i) % corpus::kContour[contour].n)];
            iv = foldIv (iv, s.dark, s.dialect);
            if (s.dark > 0.6f && brightPc (pcOf (iv)))
                iv = partner;
            if (s.dark < 0.35f && (i == (int) (r.next() * nSlots)))
                iv = r.next() < 0.5 ? 7 : 12;
            Note note;
            note.start = start;
            note.length = (b == s.bars - 1 && i == nSlots - 1) ? 1.5f : 1.f;
            note.chord[0] = (std::int8_t) iv;
            note.nChord = 1;
            note.vel = i == 0 ? 108 : 92;
            lane.notes[lane.count++] = note;
        }
    }
}

void genPad (Lane& lane, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    const int every = (s.dialect == Long || s.dialect == Gaskin || s.busy < 0.35f) ? 4 : 2;
    const int v0 = pickVoicing (s, r);
    int v1 = pickVoicing (s, r);
    if (v1 == v0 && corpus::kVoicingN > 1)
        v1 = (v0 + 1) % corpus::kVoicingN;
    for (int b = 0; b < s.bars && lane.count < kMaxNotes; b += every)
    {
        const bool lift = (b + every >= 4 && ((b / every) % 2) == 1) || b + every >= s.bars;
        const auto& v = corpus::kVoicing[lift ? v1 : v0];
        const int nudge = (int) (r.next() * 3.0) - 1;
        float start = (float) (b * 16 + nudge);
        float len = (float) (12 + (int) (r.next() * 5.0));
        if (s.dialect == Gaskin)
            len = (float) (12 + (int) (r.next() * 4.0));
        else
            len = std::min (len, lerp (10.f, 14.f, s.dark) + (float) nudge);
        if (start + len > (float) loop)
            len = (float) loop - start;
        if (len < 2.f)
            continue;
        Note note;
        note.start = std::max (0.f, start);
        note.length = len;
        note.nChord = (std::uint8_t) clampi (std::max (1, (int) v.n), 1, 4);
        for (int c = 0; c < note.nChord; ++c)
        {
            int iv = v.iv[c];
            if (s.dark > 0.65f && iv > 12)
                iv -= 12;
            note.chord[c] = (std::int8_t) clampi (iv, -12, 24);
        }
        compactChord (note.chord, note.nChord);
        note.vel = (std::uint8_t) (s.dark > 0.6f ? 80 : 90);
        lane.notes[lane.count++] = note;
    }
}

void genVox (Lane& lane, const Groove& g, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    int want = (int) std::round (lerp (2.f, 8.f, s.busy * 0.4f + s.stab * 0.6f));
    const int chop = pickChop (s, r);
    int slots[8];
    int nSlots = 0;
    if (chop >= 0)
    {
        const auto& c = corpus::kChop[chop];
        for (int i = 0; i < c.n && nSlots < 8; ++i)
            slots[nSlots++] = corpus::kChopSlot[c.off + i];
    }
    int vowel = (int) (r.next() * 4.0) % 4;
    for (int n = 0; n < want && lane.count < kMaxNotes; ++n)
    {
        int best = -1;
        float bestS = -1.f;
        for (int t = 0; t < loop; ++t)
        {
            if ((t % 4) == 0 || laneAttacks (g.lane[Bass], t) || laneAttacks (g.lane[Stab], t) || laneAttacks (g.lane[Hits], t))
                continue;
            bool clash = false;
            for (int k = 0; k < lane.count; ++k)
                if (std::fabs (lane.notes[k].start - (float) t) < 4.f)
                    clash = true;
            if (clash)
                continue;
            float sc = (float) r.next();
            for (int i = 0; i < nSlots; ++i)
                if ((t % 16) == slots[i])
                    sc += 0.7f;
            if (((t / 16) % 2) == 1)
                sc += 0.3f;
            if (sc > bestS)
            {
                bestS = sc;
                best = t;
            }
        }
        if (best < 0)
            break;
        Note note;
        note.start = (float) best;
        note.length = r.next() < 0.5 ? 1.f : 2.f;
        note.chord[0] = (r.next() < lerp (0.3f, 0.8f, 1.f - s.dark)) ? 0 : 7;
        note.nChord = 1;
        note.vel = 100;
        note.timbre = (std::uint8_t) (vowel++ % 4);
        lane.notes[lane.count++] = note;
    }
}

void clampLane (Lane& lane, int bars)
{
    const float loop = (float) bars * 16.f;
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        if (lane.notes[i].start >= loop)
            continue;
        if (lane.notes[i].start + lane.notes[i].length > loop)
            lane.notes[i].length = loop - lane.notes[i].start;
        if (lane.notes[i].length > 0.05f)
            lane.notes[w++] = lane.notes[i];
    }
    lane.count = w;
}

std::uint64_t hashBassBar (const Lane& lane, int bar)
{
    int slot[32], len2[32], pc[32];
    int n = 0;
    for (int i = 0; i < lane.count && n < 32; ++i)
    {
        const int st = (int) std::floor (lane.notes[i].start + 1.0e-3f);
        if (st / 16 != bar)
            continue;
        slot[n] = st % 16;
        len2[n] = clampi ((int) std::lround (lane.notes[i].length * 2.f), 1, 24);
        pc[n] = pcOf (lane.notes[i].chord[0]);
        ++n;
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (slot[j] < slot[i])
            {
                std::swap (slot[i], slot[j]);
                std::swap (len2[i], len2[j]);
                std::swap (pc[i], pc[j]);
            }
    return hashTriples (slot, len2, pc, n);
}

std::uint64_t hashStabBar (const Lane& lane, int bar)
{
    int slot[32], len2[32], pc[32];
    int n = 0;
    for (int i = 0; i < lane.count && n < 32; ++i)
    {
        const int st = (int) std::floor (lane.notes[i].start + 1.0e-3f);
        if (st / 16 != bar)
            continue;
        slot[n] = st % 16;
        len2[n] = clampi ((int) std::lround (lane.notes[i].length * 2.f), 1, 32);
        pc[n] = pcOf (lane.notes[i].chord[0]);
        ++n;
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (slot[j] < slot[i])
            {
                std::swap (slot[i], slot[j]);
                std::swap (len2[i], len2[j]);
                std::swap (pc[i], pc[j]);
            }
    return hashTriples (slot, len2, pc, n);
}

std::uint64_t hashDrumBar (const DrumLane& d, int bar)
{
    int slot[160], voice[160], bucket[160];
    int n = 0;
    for (int s16 = 0; s16 < 16; ++s16)
        for (int v = 0; v < NumDrumVoices && n < 160; ++v)
        {
            const int vel = d.vel[v][bar * 16 + s16];
            if (vel <= 0)
                continue;
            slot[n] = s16;
            voice[n] = v;
            bucket[n] = std::min (15, vel / 8);
            ++n;
        }
    return hashTriples (slot, voice, bucket, n);
}

bool pairBanned (std::uint64_t a, std::uint64_t b) noexcept
{
    if (a == 14695981039346656037ull || b == 14695981039346656037ull)
        return false;
    return banned (pairHash (a, b));
}

bool assess (const Groove& g, const Settings& s, std::uint32_t mask, float& score)
{
    score = 100.f;
    bool ok = true;
    // Soft weights steer the best-of attempt. Only the four hard rejects re-roll.
    auto bad = [&] (float penalty) { score -= penalty; };
    auto hard = [&] (float penalty)
    {
        score -= penalty;
        ok = false;
    };
    const bool doBass = (mask & (1u << Bass)) != 0;
    const bool doStab = (mask & (1u << Stab)) != 0;
    const bool doDrums = (mask & (1u << Drums)) != 0;
    const int nb = s.bars;
    const bool gaskin = s.dialect == Gaskin;

        if (doBass)
    {
        if (g.lane[Bass].count < 4)
            hard (30.f);
        int off = 0, on = 0;
        int dotted = 0, attacks = 0;
        float time[12] {};
        float total = 0.f;
        float lens[kMaxNotes];
        int nLen = 0;
        int perBar[8] {};
        for (int i = 0; i < g.lane[Bass].count; ++i)
        {
            const auto& note = g.lane[Bass].notes[i];
            const int st = (int) std::floor (note.start + 1.0e-3f);
            if (st < 0 || st >= nb * 16)
                continue;
            const bool kickOn = doDrums ? (g.drums.vel[Kick][st] > 0) : ((st % 4) == 0);
            if (kickOn) ++on; else ++off;
            ++attacks;
            if ((st % 16) % 3 == 0)
                ++dotted;
            time[pcOf (note.chord[0])] += note.length;
            total += note.length;
            if (nLen < kMaxNotes)
                lens[nLen++] = note.length;
            if (st / 16 < 8)
                ++perBar[st / 16];
            if (snapScale (note.chord[0], s.dialect) != note.chord[0])
                hard (20.f);
            if (note.chord[0] < -12 || note.chord[0] > 19)
                bad (8.f);
        }
        if (on + off > 0)
        {
            // H2: 35% on the kick is the hard cap in every dialect. The tighter
            // per-style numbers are soft weights, not rejects.
            const float onKick = (float) on / (float) (on + off);
            if (onKick > 0.35f)
                hard (25.f);
        }
        if (gaskin && attacks > 0 && (float) dotted / (float) attacks < 0.45f)
            bad (15.f);
        if (! gaskin && total > 0.f)
        {
            float a = 0.f, b = 0.f;
            for (float t : time)
            {
                if (t > a) { b = a; a = t; }
                else if (t > b) b = t;
            }
            if ((a + b) < total * 0.60f)
                bad (12.f);
        }
        if (nLen > 0)
        {
            std::sort (lens, lens + nLen);
            const float med = lens[nLen / 2];
            float loL = 0.8f, hiL = 4.2f;
            if (s.dialect == Long) { loL = 1.2f; hiL = 7.f; }
            if (s.dialect == Pop) { loL = 0.5f; hiL = 2.6f; }
            if (gaskin) { loL = 1.0f; hiL = 3.2f; }
            if (med < loL || med > hiL)
                bad (8.f);
        }
        float target, lo, hi;
        densityBand (s, target, lo, hi);
        float sum = 0.f;
        for (int b = 0; b < nb; ++b)
            sum += (float) perBar[b];
        const float mean = sum / (float) nb;
        if (mean < lo || mean > hi)
            bad (10.f);
        for (int b = 0; b < nb; ++b)
            if (perBar[b] == 0)
                hard (8.f);

        // H2 register and H3 holes. Notes are intervals from the engine bass base.
        {
            const int bassBase = 12 * ((s.dialect == Pop ? 2 : 1) + 1) + s.key;
            int pitches[kMaxNotes];
            int np = 0;
            bool sounding[kMaxSteps] {};
            float timePc[12] {};
            float timeAll = 0.f;
            for (int i = 0; i < g.lane[Bass].count; ++i)
            {
                const auto& note = g.lane[Bass].notes[i];
                const int st = (int) std::floor (note.start + 1.0e-3f);
                if (st < 0 || st >= nb * 16 || note.length <= 0.05f)
                {
                    hard (20.f);
                    continue;
                }
                if (note.start + note.length > (float) nb * 16.f + 0.05f)
                    hard (20.f);
                pitches[np++] = bassBase + note.chord[0];
                const int pc = pcOf (note.chord[0]);
                const int last = clampi ((int) std::ceil (note.start + note.length - 1.0e-3f), 0, nb * 16);
                for (int t = st; t < last && t < nb * 16; ++t)
                    sounding[t] = true;
                timePc[pc] += note.length;
                timeAll += note.length;
                for (int j = 0; j < i; ++j)
                {
                    const auto& o = g.lane[Bass].notes[j];
                    if (pcOf (o.chord[0]) != pc)
                        continue;
                    const float a0 = o.start, a1 = o.start + o.length;
                    const float b0 = note.start, b1 = note.start + note.length;
                    if (b0 < a1 - 0.05f && a0 < b1 - 0.05f)
                        hard (15.f);
                }
            }
            if (np > 0)
            {
                std::sort (pitches, pitches + np);
                const int med = pitches[np / 2];
                if (med < 20 || med > 45)
                    hard (25.f);
            }
            int silent = 0, covered = 0;
            for (int t = 0; t < nb * 16; ++t)
            {
                if (sounding[t]) ++covered;
                else ++silent;
            }
            if (silent < 1 || (nb * 16 > 0 && (float) covered / (float) (nb * 16) > 0.975f))
                hard (18.f);
            if (timeAll > 0.f)
            {
                float top = 0.f, second = 0.f;
                for (float t : timePc)
                {
                    if (t > top) { second = top; top = t; }
                    else if (t > second) second = t;
                }
                const bool tonicTop = timePc[0] >= second - 1.0e-3f;
                if (! tonicTop)
                    hard (20.f);
            }
            int root24 = 0, b3Tie = 0;
            for (int b = 0; b < nb; ++b)
            {
                bool s2 = false, s4 = false;
                for (int i = 0; i < g.lane[Bass].count; ++i)
                {
                    const auto& note = g.lane[Bass].notes[i];
                    const int st = (int) std::floor (note.start + 1.0e-3f);
                    if (st / 16 != b)
                        continue;
                    const int slot = st % 16;
                    const int pc = pcOf (note.chord[0]);
                    if (pc == 0 && slot == 1) s2 = true;
                    if (pc == 0 && slot == 3) s4 = true;
                    if (pc == 3 && slot == 14 && note.start + note.length > (float) (b + 1) * 16.f)
                        ++b3Tie;
                }
                if (s2 && s4)
                    ++root24;
            }
            if (root24 > nb / 2 || b3Tie > nb / 2)
                hard (30.f);
        }

        auto barKey = [&] (int bar)
        {
            std::uint64_t h = 14695981039346656037ull;
            for (int i = 0; i < g.lane[Bass].count; ++i)
            {
                const int st = (int) std::floor (g.lane[Bass].notes[i].start + 1.0e-3f);
                if (st / 16 != bar)
                    continue;
                h = fnvByte (h, (unsigned) (st % 16));
                h = fnvByte (h, (unsigned) pcOf (g.lane[Bass].notes[i].chord[0]));
            }
            return h;
        };
        const int varBar = nb >= 4 ? 3 : nb - 1;
        const bool varied = barKey (0) != barKey (varBar) || (nb == 8 && barKey (0) != barKey (7));
        if (! varied)
            bad (20.f);
        bool allSame = true;
        for (int b = 1; b < nb; ++b)
            if (barKey (b) != barKey (0))
                allSame = false;
        if (allSame)
            bad (20.f);

        for (int b = 0; b < nb - 1; ++b)
            if (pairBanned (hashBassBar (g.lane[Bass], b), hashBassBar (g.lane[Bass], b + 1)))
                hard (40.f);
    }

    if (doStab)
    {
        int onBeat = 0, n = 0, clash = 0;
        for (int i = 0; i < g.lane[Stab].count; ++i)
        {
            const int st = (int) std::floor (g.lane[Stab].notes[i].start + 1.0e-3f);
            ++n;
            if ((st % 4) == 0)
                ++onBeat;
            if (laneAttacks (g.lane[Bass], st))
                ++clash;
            for (int c = 0; c < g.lane[Stab].notes[i].nChord; ++c)
            {
                const int iv = g.lane[Stab].notes[i].chord[c];
                if (snapScale (iv, s.dialect) != iv)
                    hard (12.f);
                if (iv < -12 || iv > 24)
                    bad (6.f);
            }
        }
        if (s.stab < 0.03f && n > 0)
            bad (10.f);
        if (n > 0 && (float) onBeat / (float) n > 0.34f)
            bad (12.f);
        if (n > 0 && (float) clash / (float) n > 0.34f)
            bad (12.f);
        const int expect = s.stab < 0.03f ? 0 : (int) std::round (lerp (0.f, (float) s.bars * 1.6f, s.stab));
        if (std::abs (n - expect) > std::max (2, expect / 2))
            bad (6.f);
        for (int b = 0; b < nb - 1; ++b)
            if (pairBanned (hashStabBar (g.lane[Stab], b), hashStabBar (g.lane[Stab], b + 1)))
                hard (30.f);
    }

    if (doDrums)
    {
        int quarters = 0, hit = 0;
        for (int t = 0; t < nb * 16; ++t)
            if ((t % 4) == 0)
            {
                ++quarters;
                if (g.drums.vel[Kick][t] > 0)
                    ++hit;
            }
        // The last bar may drop beat 4. Every other quarter stays a kick.
        if (quarters > 0 && hit + 1 < quarters)
            bad (15.f);
        for (int b = 0; b < nb - 1; ++b)
            if (pairBanned (hashDrumBar (g.drums, b), hashDrumBar (g.drums, b + 1)))
                hard (20.f);
        if (gaskin)
        {
            for (int t = 0; t < nb * 16; ++t)
                if (g.drums.vel[Perc][t])
                    bad (10.f);
        }
    }
    return ok;
}

void markKicks (const DrumLane& d, int steps, bool trust, bool* out)
{
    std::memset (out, 0, (size_t) kMaxSteps);
    bool any = false;
    if (trust)
    {
        for (int t = 0; t < steps; ++t)
            if (d.vel[Kick][t] > 0)
            {
                out[t] = true;
                any = true;
            }
    }
    if (! any)
        for (int t = 0; t < steps; ++t)
            if ((t % 4) == 0)
                out[t] = true;
}

void markStabs (const Lane& lane, int steps, bool* out)
{
    std::memset (out, 0, (size_t) kMaxSteps);
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::floor (lane.notes[i].start + 1.0e-3f);
        if (st >= 0 && st < steps)
            out[st] = true;
    }
}

float curve (float d) noexcept
{
    if (d <= 0.f)
        return 0.f;
    if (d <= 1.f)
        return d * d;
    return std::min (4.f, 2.f * d - 1.f);
}

float bandPen (float x, float lo, float hi, float tol, float w, float stick, float f) noexcept
{
    const float tolE = std::max (1.0e-3f, tol * (1.f + 2.f * f));
    const float wE = w * (1.f - f * (1.f - stick));
    float dist = 0.f;
    if (x < lo)
        dist = lo - x;
    else if (x > hi)
        dist = x - hi;
    return wE * curve (dist / tolE);
}

float softPenalty (const Groove& g, const Settings& s) noexcept
{
    const float f = std::clamp (s.freedom, 0.f, 1.f);
    const int nb = s.bars;
    const int L = nb * 16;
    const auto& bass = g.lane[Bass];
    if (bass.count < 4 || L <= 0)
        return 8.f;

    int att[kMaxNotes];
    int pc[kMaxNotes];
    float len[kMaxNotes];
    int n = 0;
    int perBar[8] {};
    bool sounding[kMaxSteps] {};
    int soundingPc[kMaxSteps];
    for (int t = 0; t < L; ++t)
        soundingPc[t] = -1;
    for (int i = 0; i < bass.count && n < kMaxNotes; ++i)
    {
        const int st = (int) std::floor (bass.notes[i].start + 1.0e-3f);
        if (st < 0 || st >= L)
            continue;
        att[n] = st;
        pc[n] = pcOf (bass.notes[i].chord[0]);
        const float next = (i + 1 < bass.count) ? bass.notes[i + 1].start : (float) L;
        len[n] = std::max (0.05f, std::min (bass.notes[i].length, next - bass.notes[i].start));
        if (st / 16 < 8)
            ++perBar[st / 16];
        const int last = clampi ((int) std::ceil (bass.notes[i].start + len[n] - 1.0e-3f), 0, L);
        for (int t = st; t < last; ++t)
        {
            sounding[t] = true;
            soundingPc[t] = bass.notes[i].chord[0];
        }
        ++n;
    }
    if (n < 4)
        return 8.f;

    int on = 0;
    for (int i = 0; i < n; ++i)
        if ((att[i] % 4) == 0)
            ++on;
    const float onKick = 100.f * (float) on / (float) n;
    float onLo = 0.f, onHi = 28.f;
    if (s.dialect == Long) onHi = 30.f;
    else if (s.dialect == Pop) onHi = 10.f;
    else if (s.dialect == Gaskin) onHi = 35.f;
    float p = bandPen (onKick, onLo, onHi, 5.f, 2.f, 0.6f, f);

    float sum = 0.f, loN = 1.0e9f, hiN = 0.f;
    for (int b = 0; b < nb; ++b)
    {
        sum += (float) perBar[b];
        loN = std::min (loN, (float) perBar[b]);
        hiN = std::max (hiN, (float) perBar[b]);
    }
    const float npb = sum / (float) nb;
    float mean = npb;
    float var = 0.f;
    for (int b = 0; b < nb; ++b)
    {
        const float d = (float) perBar[b] - mean;
        var += d * d;
    }
    const float sd = std::sqrt (var / (float) nb);
    float npbLo = 3.9f, npbHi = 8.7f;
    if (s.dialect == Long) npbHi = 7.5f;
    else if (s.dialect == Pop) { npbLo = 4.f; npbHi = 6.f; }
    else if (s.dialect == Gaskin) { npbLo = 6.f; npbHi = 8.f; }
    const float npbW = std::fabs (s.busy - 0.40f) > 0.25f ? 0.f : 1.f;
    p += bandPen (npb, npbLo, npbHi, 1.f, npbW, 0.3f, f);
    if (sd < 0.5f)
        p += bandPen (sd, 0.5f, 4.f, 0.3f, 2.5f, 0.7f, f);
    if (hiN - loN < 2.f)
        p += bandPen (hiN - loN, 2.f, 8.f, 0.3f, 2.5f, 0.7f, f);

    float lens[kMaxNotes];
    for (int i = 0; i < n; ++i)
        lens[i] = len[i];
    std::sort (lens, lens + n);
    const float med = lens[n / 2];
    float lenLo = 1.f, lenHi = 2.4f;
    if (s.dialect == Long) { lenLo = 1.8f; lenHi = 4.f; }
    else if (s.dialect == Pop) { lenLo = 0.9f; lenHi = 1.4f; }
    else if (s.dialect == Gaskin) { lenLo = 1.5f; lenHi = 2.6f; }
    p += bandPen (med, lenLo, lenHi, 0.4f, 1.f, 0.3f, f);

    float time[12] {};
    float total = 0.f;
    for (int i = 0; i < n; ++i)
    {
        time[pc[i]] += len[i];
        total += len[i];
    }
    float a = 0.f, b = 0.f;
    for (float t : time)
    {
        if (t > a) { b = a; a = t; }
        else if (t > b) b = t;
    }
    const float root = total > 0.f ? 100.f * time[0] / total : 0.f;
    const float top2 = total > 0.f ? 100.f * (a + b) / total : 0.f;
    p += bandPen (root, 0.f, 80.f, 5.f, 3.f, 0.7f, f);
    float rootLo = 28.f, rootHi = 76.f;
    if (s.dialect == Long) rootHi = 45.f;
    else if (s.dialect == Pop) { rootLo = 30.f; rootHi = 42.f; }
    else if (s.dialect == Gaskin) { rootLo = 28.f; rootHi = 40.f; }
    p += bandPen (root, rootLo, rootHi, 8.f, 0.7f, 0.3f, f);
    const float topLo = s.dialect == Gaskin ? 55.f : 57.f;
    const float topHi = s.dialect == Gaskin ? 70.f : 99.f;
    p += bandPen (top2, topLo, topHi, 8.f, 1.5f, 0.3f, f);

    int oct = 0;
    for (int i = 1; i < n; ++i)
        if (std::abs (bass.notes[i].chord[0] - bass.notes[i - 1].chord[0]) == 12)
            ++oct;
    const float octPct = 100.f * (float) oct / (float) std::max (1, n - 1);
    float octHi = 26.f, octLo = 0.f;
    if (s.dialect == Long) octHi = 8.f;
    else if (s.dialect == Pop) { octLo = 15.f; octHi = 30.f; }
    p += bandPen (octPct, octLo, octHi, 6.f, 0.7f, 0.3f, f);

    int ib[4] {};
    for (int i = 0; i < n; ++i)
        ++ib[att[i] & 3];
    int pos = 0;
    for (int k = 0; k < 4; ++k)
        if (ib[k] * 10 >= n)
            ++pos;
    if (pos < 3)
        p += bandPen ((float) pos, 3.f, 4.f, 1.f, 3.f, 0.7f, f);

    auto match = [&] (int lag)
    {
        if (L <= lag)
            return 1.f;
        int same = 0;
        for (int i = 0; i < L - lag; ++i)
            if (soundingPc[i] == soundingPc[i + lag])
                ++same;
        return (float) same / (float) (L - lag);
    };
    const float m1 = match (16);
    const float m2 = match (32);
    const float m4 = nb >= 8 ? match (64) : m2;
    p += bandPen (m2, 0.50f, 0.93f, 0.10f, 1.5f, 0.6f, f);
    p += bandPen (m4, 0.47f, 0.95f, 0.10f, 1.0f, 0.3f, f);
    if (m2 < 0.50f && m4 < 0.47f)
    {
        const float closer = std::min (0.50f - m2, 0.47f - m4);
        p += bandPen (0.f, closer, closer, 0.10f, 3.f, 0.7f, f);
    }
    p += bandPen (m1, 0.f, s.dialect == Gaskin ? 0.20f : 0.95f, s.dialect == Gaskin ? 0.15f : 0.03f,
                  s.dialect == Gaskin ? 2.f : 3.f, s.dialect == Gaskin ? 0.6f : 0.7f, f);

    int covered = 0;
    for (int t = 0; t < L; ++t)
        if (sounding[t])
            ++covered;
    const float soundingFrac = (float) covered / (float) L;
    float sndLo = 0.36f, sndHi = 0.95f;
    if (s.dialect == Long) { sndLo = 0.75f; sndHi = 0.975f; }
    else if (s.dialect == Pop) { sndLo = 0.28f; sndHi = 0.45f; }
    else if (s.dialect == Gaskin) { sndLo = 0.70f; sndHi = 0.95f; }
    p += bandPen (soundingFrac, sndLo, sndHi, 0.1f, 1.f, 0.3f, f);

    const auto& stab = g.lane[Stab];
    if (stab.count > 0)
    {
        int onBeat = 0;
        for (int i = 0; i < stab.count; ++i)
            if (((int) std::floor (stab.notes[i].start + 1.0e-3f) % 4) == 0)
                ++onBeat;
        const float onBeatPct = 100.f * (float) onBeat / (float) stab.count;
        p += bandPen (onBeatPct, 0.f, s.dialect == Gaskin ? 50.f : 30.f, 10.f, 1.f, 0.3f, f);
        const float hpb = (float) stab.count / (float) nb;
        const bool wantRiff = s.dialect == Pop || s.dialect == Gaskin;
        const float modePen = wantRiff ? std::min (bandPen (hpb, 4.f, 10.f, 1.f, 0.8f, 0.3f, f),
                                                    bandPen (hpb, 0.5f, 3.f, 1.f, 0.8f, 0.3f, f) + 0.15f)
                                       : std::min (bandPen (hpb, 0.5f, 3.f, 1.f, 0.8f, 0.3f, f),
                                                    bandPen (hpb, 4.f, 10.f, 1.f, 0.8f, 0.3f, f) + 0.15f);
        if (std::fabs (s.stab - 0.55f) < 0.2f)
            p += modePen;
        if (s.dialect == UkMin)
        {
            int inRest = 0;
            for (int i = 0; i < stab.count; ++i)
            {
                const int st = (int) std::floor (stab.notes[i].start + 1.0e-3f);
                if (st >= 0 && st < L && ! sounding[st])
                    ++inRest;
            }
            if (100.f * (float) inRest / (float) stab.count >= 50.f)
                p -= 0.5f;
        }
    }
    return std::max (0.f, p);
}

void doBreak (Groove& g, const Settings& s, Rng& r, int which)
{
    (void) r;
    auto& bass = g.lane[Bass];
    if (which == 0 && bass.count > 2)
    {
        const int goal = std::max (2, bass.count / 5);
        int changed = 0;
        for (int i = 1; i < bass.count && changed < goal; i += 2)
        {
            int iv = bass.notes[i].chord[0];
            const int neu = iv > 7 ? iv - 12 : iv + 12;
            if (neu < -5 || neu > 19)
                continue;
            bass.notes[i].chord[0] = (std::int8_t) neu;
            ++changed;
        }
    }
    else if (which == 1 && bass.count > 1)
    {
        for (int i = 0; i < bass.count; ++i)
        {
            if ((i % 2) != 0)
                continue;
            const float next = (i + 1 < bass.count) ? bass.notes[i + 1].start : (float) s.bars * 16.f;
            bass.notes[i].length = std::min (next - bass.notes[i].start, bass.notes[i].length + 1.2f);
        }
    }
    else if (which == 2 && bass.count > 2)
    {
        for (int i = 0; i < bass.count; ++i)
        {
            const int st = (int) std::floor (bass.notes[i].start + 1.0e-3f);
            if (st / 16 == 1 && (st % 16) < 8)
            {
                bass.notes[i].chord[0] = (std::int8_t) snapScale (bass.notes[i].chord[0] + 3, s.dialect);
                break;
            }
        }
    }
    else if (which == 3 && g.lane[Stab].count > 0)
    {
        int moved = 0;
        const int want = std::max (1, g.lane[Stab].count / 2);
        for (int i = 0; i < g.lane[Stab].count && moved < want; ++i)
        {
            int st = (int) std::floor (g.lane[Stab].notes[i].start + 1.0e-3f);
            if ((st % 4) == 0)
                continue;
            const int beat = st - (st % 4);
            bool taken = false;
            for (int k = 0; k < g.lane[Stab].count; ++k)
                if (k != i && std::fabs (g.lane[Stab].notes[k].start - (float) beat) < 0.5f)
                    taken = true;
            if (taken || beat < 0)
                continue;
            g.lane[Stab].notes[i].start = (float) beat;
            ++moved;
        }
    }
    else if (which == 4 && bass.count > 4)
    {
        int idx = bass.count - 1;
        for (int i = 0; i < bass.count; ++i)
            if (bass.notes[i].length > bass.notes[idx].length)
                idx = i;
        bass.notes[idx].length = std::max (0.5f, bass.notes[idx].length - 2.f);
    }
}

void applyBreaks (Groove& g, const Settings& s, Rng& r, int k, char* log, int logN, std::uint32_t mask)
{
    static const char* ids[] = { "B_octave", "B_len_med", "S_var_back_half", "ST_on_beat", "B_rest4" };
    const bool bassOpen = (mask & (1u << Bass)) != 0;
    const bool stabOpen = (mask & (1u << Stab)) != 0;
    bool used[5] {};
    log[0] = 0;
    for (int n = 0; n < k; ++n)
    {
        int choice = -1;
        for (int t = 0; t < 8 && choice < 0; ++t)
        {
            const int c = (int) (r.next() * 5.0) % 5;
            const bool bassBreak = c != 3;
            if (used[c])
                continue;
            if (bassBreak && ! bassOpen)
                continue;
            if (! bassBreak && ! stabOpen)
                continue;
            choice = c;
        }
        if (choice < 0)
            break;
        used[choice] = true;
        Groove saved = g;
        doBreak (g, s, r, choice);
        float score = 0.f;
        if (! assess (g, s, 0x7fu, score))
            g = saved;
        else
        {
            if (log[0] != 0 && (int) std::strlen (log) + 1 < logN - 1)
                std::strncat (log, ",", (size_t) logN - std::strlen (log) - 1);
            std::strncat (log, ids[choice], (size_t) logN - std::strlen (log) - 1);
        }
    }
}

} // namespace

float bassSkeletonSimilarity (const Groove& a, const Groove& b) noexcept
{
    struct Tok { int bar; int slot; int pc; };
    Tok A[72], B[72];
    int na = 0, nb = 0;
    int aBars = 0, bBars = 0;
    auto load = [] (const Lane& lane, Tok* t, int& n, int& bars)
    {
        n = 0;
        bars = 0;
        for (int i = 0; i < lane.count && n < 72; ++i)
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
                        auto put = [] (bool* g, int& u, int local, int slot, int pc)
                        {
                            if (local < 0 || local >= 4 || slot < 0 || slot >= 16)
                                return;
                            const int key = (local * 16 + slot) * 12 + pc;
                            if (! g[key])
                            {
                                g[key] = true;
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

void generate (Groove& g, const Settings& settings, const std::uint32_t seeds[NumParts], std::uint32_t partMask)
{
    Settings s = settings;
    s.bars = std::clamp (s.bars, 2, kMaxBars);
    if (s.bars != 2 && s.bars != 4 && s.bars != 8)
        s.bars = 8;
    s.dialect = clampi (s.dialect, 0, NumDialects - 1);
    s.key = (s.key % 12 + 12) % 12;
    s.dark = std::clamp (s.dark, 0.f, 1.f);
    s.busy = std::clamp (s.busy, 0.f, 1.f);
    s.bounce = std::clamp (s.bounce, 0.f, 1.f);
    s.stab = std::clamp (s.stab, 0.f, 1.f);
    s.freedom = std::clamp (s.freedom, 0.f, 1.f);

    g.bars = s.bars;
    g.key = s.key;
    g.dialect = s.dialect;
    g.brokenRules[0] = 0;
    g.penalty = 0.f;

    auto regen = [&] (int part) { return (partMask & (1u << part)) != 0; };

    Lane keptLane[NumParts];
    DrumLane keptDrums = g.drums;
    for (int p = 0; p < NumParts; ++p)
        keptLane[p] = g.lane[p];

    // Six hard-legal candidates, then a penalty-weighted draw. Soft rules never reject.
    constexpr int kCandidates = 6;
    Groove candidates[kCandidates];
    float pens[kCandidates];
    char logs[kCandidates][64];
    int nC = 0;
    Groove fallback = g;
    float fallbackScore = -1.0e9f;
    const int nBreaks = (int) std::lround (3.f * s.freedom);
    for (int attempt = 0; attempt < 36 && nC < kCandidates; ++attempt)
    {
        for (int p = 0; p < NumParts; ++p)
            if (! regen (p))
                g.lane[p] = keptLane[p];
        if (! regen (Drums))
            g.drums = keptDrums;
        for (int p = 0; p < NumParts; ++p)
            if (! regen (p) && p != Drums)
                clampLane (g.lane[p], s.bars);
        if (! regen (Drums) && s.bars * 16 < kMaxSteps)
            for (int v = 0; v < NumDrumVoices; ++v)
                for (int t = s.bars * 16; t < kMaxSteps; ++t)
                    g.drums.vel[v][t] = 0;

        bool kick[kMaxSteps];
        bool stabAt[kMaxSteps];
        markKicks (g.drums, s.bars * 16, ! regen (Drums), kick);
        markStabs (g.lane[Stab], s.bars * 16, stabAt);

        if (regen (Bass))
        {
            Rng r (mixSeed (seeds[Bass], attempt));
            g.lane[Bass].seed = seeds[Bass] ? seeds[Bass] : 1u;
            genBass (g.lane[Bass], s, r, kick, regen (Stab) ? nullptr : stabAt);
        }

        if (regen (Stab))
        {
            Rng r (mixSeed (seeds[Stab], attempt));
            g.lane[Stab].seed = seeds[Stab] ? seeds[Stab] : 1u;
            genStab (g.lane[Stab], g.lane[Bass], s, r);
        }

        if (regen (Drums))
        {
            Rng r (mixSeed (seeds[Drums], attempt));
            g.drums.seed = seeds[Drums] ? seeds[Drums] : 1u;
            genDrums (g.drums, g, s, r);
        }

        float score = 0.f;
        if (! assess (g, s, partMask, score))
        {
            if (score > fallbackScore)
            {
                fallback = g;
                fallbackScore = score;
            }
            continue;
        }
        if (nBreaks > 0)
        {
            Rng br (mixSeed (seeds[Bass] ? seeds[Bass] : 1u, attempt + 17));
            applyBreaks (g, s, br, nBreaks, logs[nC], 64, partMask);
        }
        else
            logs[nC][0] = 0;
        candidates[nC] = g;
        pens[nC] = softPenalty (g, s);
        ++nC;
    }
    bool passed = nC > 0;
    if (! passed)
        g = fallback;
    else
    {
        const float temp = 1.f + 4.f * s.freedom;
        float bestPen = pens[0];
        for (int i = 1; i < nC; ++i)
            bestPen = std::min (bestPen, pens[i]);
        double w[kCandidates];
        for (int i = 0; i < nC; ++i)
            w[i] = std::exp (-(double) (pens[i] - bestPen) / (double) temp);
        Rng pick (mixSeed (seeds[Bass] ? seeds[Bass] : 1u, 90));
        const int chosen = pickWeighted (w, nC, pick);
        g = candidates[chosen];
        g.penalty = pens[chosen];
        std::snprintf (g.brokenRules, sizeof (g.brokenRules), "%s", logs[chosen]);
        fallbackScore = 100.f - pens[chosen];
    }

    if (regen (Hits))
    {
        Rng r (seeds[Hits] ? seeds[Hits] : 1u);
        g.lane[Hits].seed = seeds[Hits] ? seeds[Hits] : 1u;
        genHits (g.lane[Hits], g, s, r);
    }
    if (regen (Pluck))
    {
        Rng r (seeds[Pluck] ? seeds[Pluck] : 1u);
        g.lane[Pluck].seed = seeds[Pluck] ? seeds[Pluck] : 1u;
        genPluck (g.lane[Pluck], s, r);
    }
    if (regen (Pad))
    {
        Rng r (seeds[Pad] ? seeds[Pad] : 1u);
        g.lane[Pad].seed = seeds[Pad] ? seeds[Pad] : 1u;
        genPad (g.lane[Pad], s, r);
    }
    if (regen (Vox))
    {
        Rng r (seeds[Vox] ? seeds[Vox] : 1u);
        g.lane[Vox].seed = seeds[Vox] ? seeds[Vox] : 1u;
        genVox (g.lane[Vox], g, s, r);
    }

    g.guardScore = std::clamp (passed ? (1.f / (1.f + g.penalty)) : (fallbackScore / 100.f), 0.f, 1.f);
    g.guardLevel = passed ? 0 : 1;
    std::snprintf (g.guardRef, sizeof (g.guardRef), "%s", passed ? "ORIGINAL" : "NEAR");
    ++g.generation;
}

} // namespace rolla
