#include "Generator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace rolla
{
namespace
{

float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

int clampi (int v, int lo, int hi) noexcept
{
    return std::max (lo, std::min (hi, v));
}

struct DialectDef
{
    float w[16];
    float maxLen;
    float minLen;
    float dens;
    bool fixedPartner;
    int partner;
    float octPop;
    bool grid;
    int voice; // 0 quartal, 1 five+two, 2 dyad, 3 pad
    float rootShare;
    int octave; // 1 = C1 register
};

const DialectDef& dialectDef (int d) noexcept
{
    static const DialectDef defs[] = {
        { { 0, 4, 6, 4, 0, 2, 5, 5, 0, 3, 5, 2, 0, 2, 5, 4 }, 4.5f, 1.f, 1.f, false, 0, 0.f, false, 0, 0.70f, 1 },
        { { 2, 1, 4, 1, 0, 1, 5, 1, 0, 1, 4, 1, 0, 1, 4, 3 }, 8.f, 2.f, 0.65f, true, -2, 0.f, false, 1, 0.55f, 1 },
        { { 0, 1, 6, 1, 0, 5, 1, 5, 0, 1, 2, 6, 0, 1, 2, 1 }, 1.25f, 1.f, 0.85f, true, 7, 0.22f, false, 2, 0.80f, 2 },
        { { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 2.f, 2.f, 1.f, false, 0, 0.f, true, 3, 0.30f, 1 },
    };
    return defs[clampi (d, 0, NumDialects - 1)];
}

int ladderAt (float dark) noexcept
{
    static const int ladder[] = { -5, 2, -2, 3, 1 };
    const int i = clampi ((int) std::round (dark * 4.f), 0, 4);
    return ladder[i];
}

int secondOf (int prim) noexcept
{
    switch (prim)
    {
        case -5: return -2;
        case 2: return 3;
        case -2: return 3;
        case 3: return 1;
        case 1: return 3;
        case 7: return -5;
        default: return 3;
    }
}

struct Voicing
{
    int8_t v[4];
    int nv;
    int8_t lift[4];
    int nl;
};

Voicing voicingFor (int voice) noexcept
{
    switch (voice)
    {
        case 1: return { { 7, 14, 0, 0 }, 2, { 5, 12, 0, 0 }, 2 };
        case 2: return { { 3, 10, 0, 0 }, 2, { 5, 10, 0, 0 }, 2 };
        case 3: return { { -4, 0, 3, 0 }, 3, { -2, 2, 5, 0 }, 3 };
        default: return { { -5, 0, 5, 10 }, 4, { -7, -2, 3, 8 }, 4 };
    }
}

int pickW (const int* items, const float* w, int n, Rng& r)
{
    float sum = 0.f;
    for (int i = 0; i < n; ++i)
        sum += w[i];
    if (n <= 0)
        return 1;
    if (sum <= 0.f)
        return items[std::min (n - 1, (int) (r.next() * n))];
    float x = (float) r.next() * sum;
    for (int i = 0; i < n; ++i)
    {
        x -= w[i];
        if (x <= 0.f)
            return items[i];
    }
    return items[n - 1];
}

void sortUnique (int* v, int& n)
{
    std::sort (v, v + n);
    int w = 0;
    for (int i = 0; i < n; ++i)
        if (w == 0 || v[i] != v[w - 1])
            v[w++] = v[i];
    n = w;
}

bool contains (const int* v, int n, int x)
{
    for (int i = 0; i < n; ++i)
        if (v[i] == x)
            return true;
    return false;
}

void pushNote (Lane& lane, float start, float len, int pitch, int vel, std::uint8_t flags)
{
    if (lane.count >= kMaxNotes || len <= 0.05f)
        return;
    Note n;
    n.start = start;
    n.length = len;
    n.chord[0] = (std::int8_t) pitch;
    n.nChord = 1;
    n.vel = (std::uint8_t) std::clamp (vel, 1, 127);
    n.flags = flags;
    lane.notes[lane.count++] = n;
}

void finishLens (Lane& lane, float minLen, float maxLen, int bars, Rng& r, bool fixed)
{
    std::sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
    const float loop = (float) bars * 16.f;
    for (int i = 0; i < lane.count; ++i)
    {
        Note& n = lane.notes[i];
        const float next = (i + 1 < lane.count) ? lane.notes[i + 1].start : loop;
        const float gap = std::max (0.f, next - n.start);
        float len = n.length;
        if (! fixed)
        {
            if (r.next() < 0.55)
                len = gap;
            else
                len = std::max (minLen, gap * (float) lerp (0.4f, 0.9f, (float) r.next()));
        }
        len = std::min (len, maxLen);
        len = std::min (len, gap);
        len = std::min (len, loop - n.start);
        n.length = std::max (0.5f, std::round (len * 2.f) / 2.f);
        if (n.start + n.length > loop)
            n.length = std::max (0.f, loop - n.start);
    }
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
        if (lane.notes[i].length > 0.05f && lane.notes[i].start < loop)
            lane.notes[w++] = lane.notes[i];
    lane.count = w;

    // No run of more than 3 consecutive 16ths (playbook: no noodle rollers).
    for (int i = 3; i < lane.count; ++i)
    {
        const float a = lane.notes[i - 3].start;
        const float b = lane.notes[i].start;
        if (b - a < 3.1f)
        {
            for (int k = i; k < lane.count - 1; ++k)
                lane.notes[k] = lane.notes[k + 1];
            --lane.count;
            --i;
        }
    }
}

void genGaskinBass (Lane& lane, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int grid[] = { 1, 4, 7, 10, 13, 16 };
    int callA[6], callB[6];
    for (int i = 0; i < 6; ++i)
    {
        const int a = (i % 3 == 0) ? -2 : 0;
        callA[i] = (r.next() < 0.3) ? (a == 0 ? -2 : 0) : a;
        const int b = (i % 2 == 0) ? 1 : 3;
        callB[i] = (r.next() < 0.25) ? (b == 1 ? 3 : 1) : b;
    }
    const float density = lerp (0.55f, 1.f, s.busy);
    for (int b = 0; b < s.bars; ++b)
    {
        const int* cell = (b % 2) ? callB : callA;
        for (int i = 0; i < 6; ++i)
        {
            if (i > 0 && r.next() > density)
                continue;
            const int slot = grid[i];
            if (b == s.bars - 1 && s.fill && slot > 10)
                continue;
            pushNote (lane, (float) (b * 16 + slot - 1), 2.f, cell[i], 112, 0);
        }
        if (r.next() < 0.6)
            pushNote (lane, (float) (b * 16 + 11), 1.f, 0, 70, NoteGhost);
    }
    finishLens (lane, 2.f, 2.f, s.bars, r, true);
}

void makeBassNotes (Lane& lane, const int* cell, int nCell, int bar, int partner, int varStart,
                    float pPartner, float octPop, float glideKnob, int cut, Rng& r)
{
    for (int i = 0; i < nCell; ++i)
    {
        const int slot = cell[i];
        if (cut < 17 && slot >= cut)
            continue;
        int pitch = 0;
        int vel = 112;
        std::uint8_t flags = 0;
        if (slot >= varStart && r.next() < pPartner)
            pitch = partner;
        if (slot >= varStart && octPop > 0.f && pitch == 0 && r.next() < octPop)
            pitch = 12;
        if (slot >= 15 && i == nCell - 1 && r.next() < 0.65)
        {
            pitch = -2;
            vel = 80;
            flags = (std::uint8_t) (NoteGhost | (glideKnob > 0.15f ? NoteGlide : 0));
        }
        else if (i > 0 && r.next() < 0.22)
        {
            vel = 80;
            flags = NoteGhost;
        }
        pushNote (lane, (float) (bar * 16 + slot - 1), 1.f, pitch, vel, flags);
    }
}

void genBass (Lane& lane, const Settings& s, Rng& r)
{
    const auto& D = dialectDef (s.dialect);
    if (D.grid)
    {
        genGaskinBass (lane, s, r);
        return;
    }

    const float rootShare = std::clamp (D.rootShare + (0.5f - s.dark) * 0.3f, 0.f, 1.f);
    const float holes = std::clamp (1.f - 0.85f * s.busy, 0.f, 1.f);
    const float varpos = std::clamp (1.f - s.bounce, 0.f, 1.f);
    const int nAtt = (int) std::round (lerp (3.f, 7.f, s.busy));
    const int n = std::max (2, (int) std::round (nAtt * D.dens));
    const int vs = (int) std::round (lerp (7.f, 13.f, varpos));
    const int prim = D.fixedPartner ? D.partner : ladderAt (s.dark);
    const int sec = D.fixedPartner ? D.partner : secondOf (prim);
    const float pPartner = std::clamp ((1.f - lerp (0.5f, 0.9f, rootShare)) * 2.6f, 0.f, 1.f);

    const int openItems[] = { 2, 3, 4 };
    const float openW[] = { D.w[1], D.w[2], D.w[3] };
    const int open = pickW (openItems, openW, 3, r);

    int cellA[16];
    int nA = 0;
    cellA[nA++] = open;
    int guard = 0;
    while (nA < n && guard++ < 200)
    {
        int items[16];
        float ws[16];
        int m = 0;
        for (int s1 = open + 1; s1 <= 16; ++s1)
        {
            items[m] = s1;
            ws[m] = D.w[s1 - 1];
            ++m;
        }
        if (m == 0)
            break;
        cellA[nA++] = pickW (items, ws, m, r);
        sortUnique (cellA, nA);
    }
    if (contains (cellA, nA, 2) && contains (cellA, nA, 4))
    {
        int w = 0;
        for (int i = 0; i < nA; ++i)
            if (cellA[i] != 4)
                cellA[w++] = cellA[i];
        nA = w;
    }
    sortUnique (cellA, nA);

    int cellB[16];
    int nB = 0;
    for (int i = 0; i < nA; ++i)
        if (cellA[i] < vs)
            cellB[nB++] = cellA[i];
    int backItems[16];
    float backW[16];
    int nBack = 0;
    for (int s1 = std::max (vs, open + 1); s1 <= 16; ++s1)
    {
        if (D.w[s1 - 1] > 0.f)
        {
            backItems[nBack] = s1;
            backW[nBack] = D.w[s1 - 1];
            ++nBack;
        }
    }
    if (nBack > 0)
        cellB[nB++] = pickW (backItems, backW, nBack, r);
    sortUnique (cellB, nB);
    if (r.next() < holes)
    {
        int w = 0;
        for (int i = 0; i < nB; ++i)
            if (cellB[i] < 13)
                cellB[w++] = cellB[i];
        nB = w;
    }

    auto vary = [&] (int* dst, int& nDst)
    {
        nDst = 0;
        for (int i = 0; i < nB; ++i)
            if (cellB[i] < 13)
                dst[nDst++] = cellB[i];
        for (int s1 : { 14, 15, 16 })
            if (D.w[s1 - 1] > 0.f && r.next() < 0.45)
                dst[nDst++] = s1;
        sortUnique (dst, nDst);
    };

    lane.count = 0;
    for (int b = 0; b < s.bars; ++b)
    {
        const bool last = b == s.bars - 1;
        const int pos = b % 4;
        if (last && (s.fill || holes > 0.25f))
        {
            const int cut = holes > 0.25f ? 11 : 17;
            makeBassNotes (lane, cellB, nB, b, prim, vs, pPartner, D.octPop, s.bounce, cut, r);
            continue;
        }
        if (pos == 0)
            makeBassNotes (lane, cellA, nA, b, prim, vs, pPartner, D.octPop, s.bounce, 17, r);
        else if (pos == 1)
            makeBassNotes (lane, cellB, nB, b, prim, vs, pPartner, D.octPop, s.bounce, 17, r);
        else if (pos == 2)
            makeBassNotes (lane, cellA, nA, b, sec, vs, pPartner, D.octPop, s.bounce, 17, r);
        else
        {
            int varied[16];
            int nV = 0;
            vary (varied, nV);
            makeBassNotes (lane, varied, nV, b, prim, vs, pPartner, D.octPop, s.bounce, 17, r);
        }
    }
    finishLens (lane, D.minLen, D.maxLen, s.bars, r, false);
}

struct Fp
{
    const char* name;
    const char* tok[8];
    int n;
};

float guardScore (const Lane& bass, int bars, const char*& ref)
{
    static const Fp prints[] = {
        { "Reaction", { "2:0", "4:0", "11:10", "15:3" }, 4 },
        { "V Line", { "1:*", "3:*", "6:*", "8:*", "10:*", "11:*", "13:*", "15:*" }, 8 },
        { "Guess You Know", { "3:*", "6:*", "8:*", "12:*" }, 4 },
        { "Memphis", { "1:0", "7:10", "15:*" }, 3 },
        { "Way Back", { "15:*", "3:0", "11:3" }, 3 },
        { "Something To Me", { "16:*", "4:2", "8:3" }, 3 },
        { "House of Dub", { "3:0", "7:2", "11:0", "14:2" }, 4 },
        { "Lovers", { "3:*", "7:*", "10:*", "14:*" }, 4 },
        { "Wax Master", { "1:10", "4:0", "7:1", "10:3" }, 4 },
    };
    float maxJ = 0.f;
    ref = "";
    const int useBars = std::min (bars, 4);
    for (int b = 0; b < useBars; ++b)
    {
        bool slot[17] {};
        bool tokHit[17][12] {};
        int nNotes = 0;
        for (int i = 0; i < bass.count; ++i)
        {
            const int t = (int) std::floor (bass.notes[i].start);
            if (t / 16 != b)
                continue;
            const int s1 = (t % 16) + 1;
            const int pc = ((bass.notes[i].chord[0] % 12) + 12) % 12;
            if (s1 >= 1 && s1 <= 16)
            {
                slot[s1] = true;
                tokHit[s1][pc] = true;
                ++nNotes;
            }
        }
        if (nNotes == 0)
            continue;
        for (const auto& f : prints)
        {
            float hit = 0.f;
            bool unionSlot[17] {};
            for (int i = 0; i < f.n; ++i)
            {
                int s1 = 0;
                const char* p = f.tok[i];
                while (*p >= '0' && *p <= '9')
                {
                    s1 = s1 * 10 + (*p - '0');
                    ++p;
                }
                if (s1 < 1 || s1 > 16)
                    continue;
                unionSlot[s1] = true;
                if (p[0] == ':' && p[1] == '*')
                {
                    if (slot[s1])
                        hit += 0.6f;
                }
                else if (p[0] == ':')
                {
                    int pc = 0;
                    ++p;
                    while (*p >= '0' && *p <= '9')
                    {
                        pc = pc * 10 + (*p - '0');
                        ++p;
                    }
                    pc = ((pc % 12) + 12) % 12;
                    if (tokHit[s1][pc])
                        hit += 1.f;
                }
            }
            for (int s1 = 1; s1 <= 16; ++s1)
                if (slot[s1])
                    unionSlot[s1] = true;
            int uni = 0;
            for (int s1 = 1; s1 <= 16; ++s1)
                if (unionSlot[s1])
                    ++uni;
            const float j = uni > 0 ? hit / (float) uni : 0.f;
            if (j > maxJ)
            {
                maxJ = j;
                ref = f.name;
            }
        }
    }
    return maxJ;
}

void genStab (Lane& lane, const Lane& bass, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    bool busy[kMaxSteps] {};
    for (int i = 0; i < bass.count; ++i)
    {
        const int a = std::max (0, (int) std::floor (bass.notes[i].start));
        const int b = std::min (loop, (int) std::ceil (bass.notes[i].start + bass.notes[i].length));
        for (int t = a; t < b; ++t)
            busy[t] = true;
    }
    if (s.stab <= 0.02f)
        return;

    const auto& D = dialectDef (s.dialect);
    const Voicing V = voicingFor (D.voice);

    auto pushChord = [&] (float start, float len, const int8_t* offs, int nOff, int vel)
    {
        if (lane.count >= kMaxNotes || len <= 0.05f)
            return;
        Note n;
        n.start = start;
        n.length = std::min (len, (float) loop - start);
        n.nChord = (std::uint8_t) std::min (4, nOff);
        for (int i = 0; i < n.nChord; ++i)
            n.chord[i] = offs[i];
        n.vel = (std::uint8_t) vel;
        lane.notes[lane.count++] = n;
    };

    if (D.voice == 3)
    {
        for (int b = 0; b < s.bars; b += 2)
        {
            const int on = b * 16 + (r.next() < 0.5 ? 1 : 2);
            const bool lift = ((b / 2) % 2) != 0;
            pushChord ((float) on, std::min (14.f, (float) loop - on), lift ? V.lift : V.v, lift ? V.nl : V.nv, 90);
        }
        return;
    }

    for (int b = 1; b < s.bars; b += 2)
    {
        const bool last = b == s.bars - 1;
        if (! last && r.next() > lerp (0.35f, 1.f, s.stab))
            continue;
        int cand[16];
        int nc = 0;
        for (int t = b * 16 + 6; t <= b * 16 + 15 && t < loop; ++t)
            if (! busy[t] && (t % 4) != 0)
                cand[nc++] = t;
        if (nc == 0)
            continue;
        int want = last ? std::max (1, (int) std::round (1.f + 2.f * s.stab))
                        : (r.next() < lerp (0.1f, 0.9f, s.stab) ? 2 : 1);
        if (s.fill && last)
            want = std::min (3, want + 1);
        int pick[16];
        int np = 0;
        // Prefer later slots, with a little jitter, spaced by >= 2.
        int order[16];
        float score[16];
        for (int i = 0; i < nc; ++i)
        {
            order[i] = i;
            score[i] = (float) cand[i] + ((float) r.next() - 0.5f) * 6.f;
        }
        std::sort (order, order + nc, [&] (int a, int c) { return score[a] > score[c]; });
        for (int i = 0; i < nc && np < want; ++i)
        {
            const int t = cand[order[i]];
            bool ok = true;
            for (int k = 0; k < np; ++k)
                if (std::abs (pick[k] - t) < 2)
                    ok = false;
            if (ok)
                pick[np++] = t;
        }
        std::sort (pick, pick + np);
        for (int i = 0; i < np; ++i)
        {
            const bool lift = last && i == np - 1;
            const float len = std::min (r.next() < 0.5 ? 1.f : 2.f, (float) loop - pick[i]);
            pushChord ((float) pick[i], len, lift ? V.lift : V.v, lift ? V.nl : V.nv, lift ? 110 : 96);
        }
    }
}

void markBusy (bool* busy, int loop, const Lane& lane)
{
    for (int i = 0; i < lane.count; ++i)
    {
        const int a = std::max (0, (int) std::floor (lane.notes[i].start));
        const int b = std::min (loop, (int) std::ceil (lane.notes[i].start + lane.notes[i].length));
        for (int t = a; t < b; ++t)
            busy[t] = true;
    }
}

void genHits (Lane& lane, const Groove& g, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    bool busy[kMaxSteps] {};
    markBusy (busy, loop, g.lane[Bass]);
    markBusy (busy, loop, g.lane[Stab]);

    int cand[kMaxSteps];
    int nc = 0;
    for (int t = 0; t < loop; ++t)
        if ((t % 4) != 0 && ! busy[t])
            cand[nc++] = t;
    int want = (int) std::round (lerp (2.f, 7.f, s.busy * 0.45f + s.stab * 0.55f));
    if (s.stab < 0.08f)
        want = std::min (want, 3);
    want = std::min (want, 10);

    for (int n = 0; n < want && lane.count < kMaxNotes; ++n)
    {
        int best = -1;
        float bestS = -1.f;
        for (int i = 0; i < nc; ++i)
        {
            const int t = cand[i];
            bool clash = false;
            for (int k = 0; k < lane.count; ++k)
                if (std::fabs (lane.notes[k].start - (float) t) < 3.f)
                    clash = true;
            if (clash)
                continue;
            float sc = (float) r.next();
            if (((t / 16) % 2) == 1)
                sc += 0.4f;
            if ((t % 16) >= 8)
                sc += 0.2f;
            // Prefer sitting just after a stab.
            for (int k = 0; k < g.lane[Stab].count; ++k)
            {
                const float d = (float) t - g.lane[Stab].notes[k].start;
                if (d >= 1.f && d <= 4.f)
                    sc += 0.45f;
            }
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
        note.length = r.next() < 0.6 ? 1.f : 1.5f;
        if (note.start + note.length > (float) loop)
            note.length = (float) loop - note.start;
        int type = (int) (r.next() * 5.0);
        if (type > 4)
            type = 4;
        if (s.dialect == Gaskin && r.next() < 0.6)
            type = 0; // organ
        note.timbre = (std::uint8_t) type;
        if (type == 2)
        {
            note.chord[0] = 0;
            note.chord[1] = 7;
            note.chord[2] = 12;
            note.nChord = 3;
        }
        else if (s.dialect == Pop)
        {
            note.chord[0] = 0;
            note.chord[1] = 7;
            note.nChord = 2;
        }
        else
        {
            note.chord[0] = 0;
            note.chord[1] = 5;
            note.chord[2] = 10;
            note.nChord = 3;
        }
        if (r.next() < 0.35)
            for (int c = 0; c < note.nChord; ++c)
                note.chord[c] = (std::int8_t) (note.chord[c] + 12);
        note.vel = 106;
        lane.notes[lane.count++] = note;
    }
}

void genPluck (Lane& lane, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    const int period = 2;
    int slots[4];
    int nSlots = s.busy > 0.65f ? 4 : 3;
    // Off the kick: & and a positions, 0-based.
    const int pool[] = { 2, 6, 10, 14, 3, 7, 11, 15 };
    for (int i = 0; i < nSlots; ++i)
        slots[i] = pool[(int) (r.next() * 8.0) % 8];
    std::sort (slots, slots + nSlots);
    int w = 0;
    for (int i = 0; i < nSlots; ++i)
        if (w == 0 || slots[i] != slots[w - 1])
            slots[w++] = slots[i];
    nSlots = w;
    if (nSlots == 0)
    {
        slots[0] = 6;
        nSlots = 1;
    }

    const int dark = ladderAt (s.dark);
    int pitches[4];
    for (int i = 0; i < nSlots; ++i)
    {
        const double u = r.next();
        if (u < 0.55)
            pitches[i] = 0;
        else if (u < 0.8)
            pitches[i] = 7;
        else if (s.dialect == Pop)
            pitches[i] = 12;
        else
            pitches[i] = dark;
    }

    for (int b = 0; b < s.bars; ++b)
    {
        if ((b % period) != 0 && s.busy < 0.75f && (b % 2) == 1 && r.next() < 0.35)
            continue;
        for (int i = 0; i < nSlots; ++i)
        {
            // Repeat the cell, drop one note on answer bars so it doesn't nag.
            if ((b % 2) == 1 && i == nSlots - 1 && r.next() < 0.5)
                continue;
            const float start = (float) (b * 16 + slots[i]);
            if (start >= loop)
                continue;
            pushNote (lane, start, 1.f, pitches[i], i == 0 ? 108 : 92, 0);
        }
    }
    if (s.fill && s.bars > 0 && lane.count < kMaxNotes)
        pushNote (lane, (float) ((s.bars - 1) * 16 + 15), 1.f, 12, 112, 0);
    finishLens (lane, 0.5f, 1.5f, s.bars, r, true);
}

void genPad (Lane& lane, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    const Voicing V = voicingFor (0); // quartal, no third
    const int every = (s.dialect == Long || s.dialect == Gaskin) ? 2 : 4;
    for (int b = 0; b < s.bars; b += every)
    {
        float start = (float) (b * 16) - (r.next() < 0.7 ? 1.f : 0.f); // anacrusis, one 16th early
        if (start < 0.f)
            start = 0.f;
        float len = s.dialect == Gaskin ? 14.f : 12.f;
        if (start + len > (float) loop)
            len = (float) loop - start;
        if (len < 2.f || lane.count >= kMaxNotes)
            continue;
        Note n;
        n.start = start;
        n.length = len;
        n.nChord = (std::uint8_t) V.nv;
        for (int i = 0; i < V.nv; ++i)
            n.chord[i] = V.v[i];
        n.vel = 86;
        n.timbre = 0;
        lane.notes[lane.count++] = n;
    }
}

void genVox (Lane& lane, const Groove& g, const Settings& s, Rng& r)
{
    lane.count = 0;
    const int loop = s.bars * 16;
    bool busy[kMaxSteps] {};
    markBusy (busy, loop, g.lane[Bass]);
    markBusy (busy, loop, g.lane[Stab]);
    markBusy (busy, loop, g.lane[Hits]);

    int want = (int) std::round (lerp (3.f, 8.f, s.busy * 0.4f + s.stab * 0.6f));
    static const int vowels[] = { 0, 1, 2, 3 }; // oh ah hey uh
    int vi = (int) (r.next() * 4.0) % 4;
    for (int n = 0; n < want && lane.count < kMaxNotes; ++n)
    {
        int best = -1;
        float bestS = -1.f;
        for (int t = 0; t < loop; ++t)
        {
            if (busy[t] || (t % 4) == 0)
                continue;
            bool clash = false;
            for (int k = 0; k < lane.count; ++k)
                if (std::fabs (lane.notes[k].start - (float) t) < 4.f)
                    clash = true;
            if (clash)
                continue;
            float sc = (float) r.next();
            if (((t / 16) % 2) == 1)
                sc += 0.35f;
            for (int k = 0; k < g.lane[Stab].count; ++k)
            {
                const float d = (float) t - g.lane[Stab].notes[k].start;
                if (d >= 1.f && d <= 5.f)
                    sc += 0.5f;
            }
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
        if (note.start + note.length > (float) loop)
            note.length = (float) loop - note.start;
        note.chord[0] = (r.next() < 0.7f) ? 0 : 7;
        note.nChord = 1;
        note.vel = 100;
        note.timbre = (std::uint8_t) vowels[vi++ % 4];
        lane.notes[lane.count++] = note;
        busy[best] = true;
    }
}

void genDrums (DrumLane& d, const Settings& s, Rng& r)
{
    clearDrums (d);
    const int steps = s.bars * 16;
    const bool gaskin = s.dialect == Gaskin;
    for (int t = 0; t < steps; ++t)
    {
        const int s16 = t % 16;
        const int bar = t / 16;
        if (s16 == 0 || s16 == 4 || s16 == 8 || s16 == 12)
        {
            d.vel[Kick][t] = 120;
            d.accent[t] = 1;
        }
        if (s16 == 4 || s16 == 12)
        {
            d.vel[Clap][t] = 116;
            d.vel[Snare][t] = 78;
            d.accent[t] = 1;
        }
        if (! gaskin && (s16 == 7 || s16 == 15))
            d.vel[Clap][t] = 62;

        if (gaskin)
        {
            // 8th-note open hats on the offbeat, no 16th layer.
            if (s16 == 2 || s16 == 6 || s16 == 10 || s16 == 14)
                d.vel[HatO][t] = 104;
        }
        else
        {
            if (s16 == 2 || s16 == 6 || s16 == 10 || s16 == 14)
                d.vel[HatC][t] = 110;
            if (s.busy > 0.35f && (s16 % 2) == 1 && d.vel[HatC][t] == 0)
                d.vel[HatC][t] = (std::uint8_t) ((s16 % 4 == 3) ? 74 : 46);
            if (s16 == 14 && (bar % 2) == 1)
            {
                d.vel[HatO][t] = 102;
                d.vel[HatC][t] = 0;
                if (t + 1 < steps)
                    d.vel[HatC][t + 1] = std::max<std::uint8_t> (d.vel[HatC][t + 1], 92);
            }
        }

        if (s16 == 3 && (bar % 4) == 1 && r.next() < 0.85)
            d.vel[Rim][t] = 82;
        if (s16 == 11 && (bar % 4) == 3 && r.next() < 0.55)
            d.vel[Rim][t] = 70;
    }

    if (s.fill && s.bars > 0)
    {
        const int b0 = (s.bars - 1) * 16;
        for (int s16 = 8; s16 < 16; ++s16)
            d.vel[Snare][b0 + s16] = (std::uint8_t) std::min (127, 88 + (s16 - 8) * 4);
        d.vel[TomMid][b0 + 10] = 104;
        d.vel[TomLow][b0 + 13] = 108;
        d.vel[Ride][b0 + 14] = 72;
        d.vel[HatO][b0 + 6] = 100;
        if (b0 + 7 < steps)
            d.vel[HatC][b0 + 7] = std::max<std::uint8_t> (d.vel[HatC][b0 + 7], 96);
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

} // namespace

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

    g.bars = s.bars;
    g.key = s.key;
    g.dialect = s.dialect;

    auto regen = [&] (int part) { return (partMask & (1u << part)) != 0; };

    if (regen (Bass))
    {
        const char* ref = "";
        float score = 1.f;
        std::uint32_t seed = seeds[Bass] ? seeds[Bass] : 1u;
        for (int attempt = 0; attempt < 25; ++attempt)
        {
            Rng r (seed);
            g.lane[Bass].seed = seed;
            genBass (g.lane[Bass], s, r);
            score = guardScore (g.lane[Bass], s.bars, ref);
            if (score < 0.55f)
                break;
            seed = seed * 1664525u + 1013904223u + (std::uint32_t) (attempt + 1);
        }
        g.guardScore = score;
        g.guardLevel = score >= 0.55f ? 2 : (score >= 0.40f ? 1 : 0);
        std::snprintf (g.guardRef, sizeof (g.guardRef), "%s", g.guardLevel == 0 ? "ORIGINAL" : (ref != nullptr ? ref : ""));
    }
    else
    {
        clampLane (g.lane[Bass], s.bars);
    }

    if (regen (Drums))
    {
        Rng r (seeds[Drums] ? seeds[Drums] : 1u);
        g.drums.seed = seeds[Drums];
        genDrums (g.drums, s, r);
    }
    else if (s.bars * 16 < kMaxSteps)
    {
        for (int v = 0; v < NumDrumVoices; ++v)
            for (int t = s.bars * 16; t < kMaxSteps; ++t)
                g.drums.vel[v][t] = 0;
    }

    if (regen (Stab))
    {
        Rng r (seeds[Stab] ? seeds[Stab] : 1u);
        g.lane[Stab].seed = seeds[Stab];
        genStab (g.lane[Stab], g.lane[Bass], s, r);
    }
    else
        clampLane (g.lane[Stab], s.bars);

    if (regen (Hits))
    {
        Rng r (seeds[Hits] ? seeds[Hits] : 1u);
        g.lane[Hits].seed = seeds[Hits];
        genHits (g.lane[Hits], g, s, r);
    }
    else
        clampLane (g.lane[Hits], s.bars);

    if (regen (Pluck))
    {
        Rng r (seeds[Pluck] ? seeds[Pluck] : 1u);
        g.lane[Pluck].seed = seeds[Pluck];
        genPluck (g.lane[Pluck], s, r);
    }
    else
        clampLane (g.lane[Pluck], s.bars);

    if (regen (Pad))
    {
        Rng r (seeds[Pad] ? seeds[Pad] : 1u);
        g.lane[Pad].seed = seeds[Pad];
        genPad (g.lane[Pad], s, r);
    }
    else
        clampLane (g.lane[Pad], s.bars);

    if (regen (Vox))
    {
        Rng r (seeds[Vox] ? seeds[Vox] : 1u);
        g.lane[Vox].seed = seeds[Vox];
        genVox (g.lane[Vox], g, s, r);
    }
    else
        clampLane (g.lane[Vox], s.bars);

    ++g.generation;
}

} // namespace rolla
