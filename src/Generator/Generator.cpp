#include "Generator.h"
#include "GenreVocab.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

namespace rolla
{
namespace
{

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
    if (dialect == Gaskin)
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

void noteBreak (char* breaks, int breaksN, const char* id);

bool tagHas (const char* cells, const char* id) noexcept
{
    if (cells == nullptr || id == nullptr || id[0] == 0)
        return false;
    const std::size_t n = std::strlen (id);
    for (const char* p = cells; (p = std::strstr (p, id)) != nullptr; p += n)
    {
        const bool left = p == cells || p[-1] == ',';
        const bool right = p[n] == 0 || p[n] == ',';
        if (left && right)
            return true;
    }
    return false;
}

void tagAdd (char* cells, int cellsN, const char* id)
{
    if (cells == nullptr || cellsN <= 1 || id == nullptr || id[0] == 0 || tagHas (cells, id))
        return;
    const int len = (int) std::strlen (cells);
    const int need = (int) std::strlen (id) + (len ? 1 : 0);
    if (len + need + 1 >= cellsN)
        return;
    if (len)
        cells[len] = ',';
    std::memcpy (cells + len + (len ? 1 : 0), id, std::strlen (id) + 1);
}

float laneSwing (int part, int dialect) noexcept
{
    float amt = dialect == Gaskin ? 0.08f : dialect == Long ? 0.055f : dialect == Pop ? 0.03f : 0.045f;
    if (part == Pluck)
        amt += 0.03f;
    else if (part == Vox)
        amt += 0.02f;
    else if (part == Stab)
        amt += 0.015f;
    return amt;
}

int velCurve (int slot, int accent) noexcept
{
    if (accent > 0 && accent < 55)
        return accent;
    const int gallop[] = { 100, 116, 86, 70 };
    int vel = gallop[slot & 3];
    if (accent >= 115)
        vel = std::min (124, vel + 10);
    return vel;
}

struct GridBar
{
    int n = 0;
    int slot[16] {};
    int vel[16] {};
    int len[16] {};
};

bool decodeGrid (const char* row, GridBar& bar)
{
    bar.n = 0;
    if (row == nullptr)
        return false;
    for (int i = 0; i < 16; ++i)
    {
        const char c = row[i];
        if (c == 0 || c == '-' || c == '=')
            continue;
        int len = 1;
        for (int j = i + 1; j < 16 && row[j] == '='; ++j)
            ++len;
        int vel = 100;
        if (c == 'X' || c == 'O')
            vel = 118;
        else if (c == 'o')
            vel = 40;
        bar.slot[bar.n] = i;
        bar.vel[bar.n] = vel;
        bar.len[bar.n] = len;
        ++bar.n;
    }
    return bar.n > 0;
}

const genre::Cell* pickCell (int lane, int dialect, Rng& rng, int voice)
{
    const int d = clampi (dialect, 0, 3);
    double sum = 0.0;
    for (int i = 0; i < genre::kN; ++i)
    {
        const auto& cell = genre::kCells[i];
        if (cell.lane != lane)
            continue;
        if (voice >= 0 && cell.voice != voice)
            continue;
        sum += (double) genre::weight (cell, d);
    }
    if (sum <= 0.0)
        return nullptr;
    double needle = rng.next() * sum;
    const genre::Cell* last = nullptr;
    for (int i = 0; i < genre::kN; ++i)
    {
        const auto& cell = genre::kCells[i];
        if (cell.lane != lane)
            continue;
        if (voice >= 0 && cell.voice != voice)
            continue;
        const double w = (double) genre::weight (cell, d);
        if (w <= 0.0)
            continue;
        last = &cell;
        needle -= w;
        if (needle <= 0.0)
            return &cell;
    }
    return last;
}

bool gridCoversBars (const genre::Cell& cell, int bars)
{
    for (int b = 0; b < bars && b < 8; ++b)
    {
        GridBar bar;
        if (! decodeGrid (cell.grid + 16 * (b % cell.bars), bar))
            return false;
    }
    return true;
}

int pickPolyStep (int dialect, Rng& rng, bool hatsOnly)
{
    struct Row { int step; float w[4]; };
    // PM_3_16 / PM_5_16 / PM_7_16, and PM_hat_len_3or5 for hats (3 or 5, restarted each bar).
    static constexpr Row rows[] = {
        { 3, { 0.50f, 0.90f, 0.20f, 0.20f } },
        { 5, { 0.30f, 0.80f, 0.10f, 0.10f } },
        { 7, { 0.20f, 0.60f, 0.10f, 0.10f } },
    };
    const int n = hatsOnly ? 2 : 3;
    const int d = clampi (dialect, 0, 3);
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
        sum += (double) rows[i].w[d];
    double needle = rng.next() * (sum > 0.0 ? sum : 1.0);
    for (int i = 0; i < n; ++i)
    {
        needle -= (double) rows[i].w[d];
        if (needle <= 0.0)
            return rows[i].step;
    }
    return 3;
}

int shadeInterval (int semi, float dark, int partner, int index, bool& colored)
{
    if (dark > 0.2f && dark < 0.8f)
    {
        if ((semi % 12 + 12) % 12 != 0)
            colored = true;
        return semi;
    }
    int pc = semi % 12;
    if (pc < 0)
        pc += 12;
    int out = semi;
    if (dark >= 0.8f)
    {
        const int small = (partner > 0 && partner <= 4) ? partner : 1;
        if (pc == 0)
            out = (index % 3 == 2) ? small : 0;
        else if (pc > 4)
            out = small;
        else
            out = pc;
    }
    else
    {
        if (pc == 0)
            out = (index % 3 == 2) ? 7 : 0;
        else if (pc < 5)
            out = 7;
        else
            out = pc;
    }
    if ((out % 12 + 12) % 12 != 0)
        colored = true;
    return out;
}

bool genericSet (const int* slots, int n) noexcept
{
    bool on[16] {};
    for (int i = 0; i < n; ++i)
        if (slots[i] >= 0 && slots[i] < 16)
            on[slots[i]] = true;
    auto exact = [&] (std::initializer_list<int> s)
    {
        int c = 0;
        for (int i = 0; i < 16; ++i)
            if (on[i])
                ++c;
        if (c != (int) s.size())
            return false;
        for (int v : s)
            if (! on[v])
                return false;
        return true;
    };
    if (n == 0)
        return true;
    return exact ({ 0, 2, 4, 6, 8, 10, 12, 14 })
        || exact ({ 1, 3, 5, 7, 9, 11, 13, 15 })
        || exact ({ 0, 4, 8, 12 })
        || exact ({ 2, 6, 10, 14 })
        || n == 16;
}

int inbeatUsed (const int* slots, int n) noexcept
{
    bool u[4] {};
    for (int i = 0; i < n; ++i)
        if (slots[i] >= 0 && slots[i] < 16)
            u[slots[i] & 3] = true;
    return (int) u[0] + (int) u[1] + (int) u[2] + (int) u[3];
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

// Phrase plans from GENERATOR_RULES §2. Roles: A B B' A' A'' C D D'.
enum Role : int { RA = 0, RB, RBp, RAp, RApp, RC, RD, RDp };

Role planOf (int dialect, Rng& rng, const char*& name, float freedom, bool& offPlan)
{
    offPlan = freedom > 0.f && rng.next() < 0.22 * (double) freedom;
    if (offPlan)
    {
        const char* any[] = { "P1", "P2", "P3", "P4", "P5" };
        name = any[(int) (rng.next() * 5.0) % 5];
        return RA;
    }
    const double x = rng.next();
    if (dialect == Gaskin)
    {
        if (x < 0.80) { name = "P4"; return RA; }
        name = "P2";
        return RB;
    }
    if (dialect == Pop)
    {
        if (x < 0.60) { name = "P3"; return RA; }
        name = "P2";
        return RB;
    }
    if (dialect == Long)
    {
        if (x < 0.40) { name = "P1"; return RA; }
        if (x < 0.70) { name = "P2"; return RB; }
        name = "P3";
        return RAp;
    }
    if (x < 0.35) { name = "P1"; return RA; }
    if (x < 0.60) { name = "P2"; return RB; }
    if (x < 0.75) { name = "P3"; return RAp; }
    name = "P5";
    return RD;
}

void rolesFor (const char* plan, Role out[8])
{
    if (std::strcmp (plan, "P2") == 0)
    {
        Role r[8] = { RA, RB, RA, RBp, RA, RB, RAp, RC };
        std::memcpy (out, r, sizeof r);
    }
    else if (std::strcmp (plan, "P3") == 0)
    {
        Role r[8] = { RA, RAp, RA, RApp, RA, RAp, RA, RC };
        std::memcpy (out, r, sizeof r);
    }
    else if (std::strcmp (plan, "P4") == 0)
    {
        Role r[8] = { RA, RB, RA, RB, RA, RB, RA, RC };
        std::memcpy (out, r, sizeof r);
    }
    else if (std::strcmp (plan, "P5") == 0)
    {
        Role r[8] = { RA, RB, RD, RBp, RA, RB, RD, RDp };
        std::memcpy (out, r, sizeof r);
    }
    else
    {
        Role r[8] = { RA, RB, RA, RBp, RA, RB, RA, RC };
        std::memcpy (out, r, sizeof r);
    }
}

const char* contourFor (int dialect, Rng& rng, float freedom, bool& offContour)
{
    offContour = freedom > 0.f && rng.next() < 0.22 * (double) freedom;
    if (offContour)
    {
        const char* any[] = { "pedal", "neighbour", "ladder_down", "fifth_pop", "step_up", "call_answer_2bar" };
        return any[(int) (rng.next() * 6.0) % 6];
    }
    if (dialect == Gaskin)
        return rng.next() < 0.7 ? "call_answer_2bar" : "neighbour";
    if (dialect == Pop)
    {
        const double x = rng.next();
        if (x < 0.5) return "fifth_pop";
        if (x < 0.75) return "pedal";
        return "step_up";
    }
    if (dialect == Long)
    {
        const double x = rng.next();
        if (x < 0.4) return "pedal";
        if (x < 0.7) return "neighbour";
        return "ladder_down";
    }
    const char* all[] = { "pedal", "neighbour", "ladder_down", "fifth_pop", "step_up" };
    return all[(int) (rng.next() * 5.0) % 5];
}

int partnerFor (int dialect, float dark, Rng& rng)
{
    // Bright to dark, matching the knob: 5, 2, b7, b3, b2.
    const int ladder[] = { 7, 2, 10, 3, 1 };
    int idx = (int) std::floor (std::clamp (dark, 0.f, 1.f) * 4.999f);
    if (rng.next() < 0.30)
        idx = clampi (idx + (rng.next() < 0.5 ? -1 : 1), 0, 4);
    int p = ladder[idx];
    if (dialect == Pop && dark < 0.5f && rng.next() < 0.8)
        p = 7;
    if (dialect == Gaskin && dark >= 0.5f && rng.next() < 0.25)
        p = 1;
    return p;
}

void baseSlots (int dialect, Role role, Rng& rng, int* slots, int& n, float& wantLen)
{
    n = 0;
    wantLen = 2.f;
    auto push = [&] (int s)
    {
        if (s < 0 || s > 15 || n >= 10)
            return;
        for (int i = 0; i < n; ++i)
            if (slots[i] == s)
                return;
        slots[n++] = s;
    };

    if (dialect == Pop)
    {
        wantLen = 1.f + (float) rng.next() * 0.25f;
        const int pump[4][5] = {
            { 1, 2, 6, 10, 15 },
            { 2, 5, 6, 10, 15 },
            { 1, 6, 10, 11, 15 },
            { 2, 6, 9, 10, 15 },
        };
        const int* row = pump[(int) (rng.next() * 4) % 4];
        for (int i = 0; i < 5; ++i)
            push (row[i]);
        if (role == RC || role == RDp)
        {
            n = 0;
            push (1); push (2); push (6); push (9);
        }
        return;
    }
    if (dialect == Gaskin)
    {
        wantLen = 2.f;
        if (role == RB || role == RBp)
        {
            const int row[] = { 1, 5, 7, 10, 13, 15 };
            for (int s : row) push (s);
        }
        else if (role == RC || role == RDp)
        {
            push (0); push (3); push (6); push (10);
        }
        else
        {
            const int row[] = { 0, 3, 6, 9, 12, 15 };
            for (int s : row) push (s);
            if (rng.next() < 0.5)
                push (7);
        }
        return;
    }
    if (dialect == Long)
    {
        wantLen = 2.15f;
        if (role == RB || role == RBp)
        {
            const int row[] = { 2, 5, 7, 9, 11, 14, 15 };
            for (int s : row) push (s);
        }
        else if (role == RC)
        {
            push (1); push (5); push (9);
            wantLen = 2.5f;
        }
        else
        {
            const int row[] = { 1, 3, 5, 7, 9, 11, 14 };
            for (int s : row) push (s);
        }
        return;
    }

    wantLen = 1.75f;
    if (role == RB || role == RBp || role == RD)
    {
        const int row[] = { 2, 5, 9, 11, 14 };
        for (int s : row) push (s);
        if (rng.next() < 0.5)
            push (7);
    }
    else if (role == RC || role == RDp)
    {
        push (1); push (5); push (7); push (9);
        wantLen = 1.5f;
    }
    else
    {
        const int shapes[3][6] = {
            { 1, 3, 6, 10, 13, 15 },
            { 1, 2, 6, 9, 11, 14 },
            { 2, 3, 5, 10, 13, 15 },
        };
        const int* row = shapes[(int) (rng.next() * 3) % 3];
        for (int i = 0; i < 6; ++i)
            push (row[i]);
    }
}

void mutateBack (Role role, Rng& rng, int* slots, int& n)
{
    if (n <= 0)
        return;
    const int from = (role == RBp || role == RAp || role == RApp || role == RDp) ? 8 : 12;
    if (role == RA || role == RB || role == RD)
        return;
    for (int i = 0; i < n; ++i)
    {
        if (slots[i] < from)
            continue;
        const int dir = rng.next() < 0.5 ? -1 : 1;
        int s = slots[i] + dir;
        if (s < from || s > 15 || kickSlot (s))
            continue;
        bool clash = false;
        for (int k = 0; k < n; ++k)
            if (k != i && slots[k] == s)
                clash = true;
        if (! clash)
            slots[i] = s;
    }
}

void stripKick (int* slots, int& n, bool allow)
{
    if (allow)
        return;
    int w = 0;
    for (int i = 0; i < n; ++i)
        if (! kickSlot (slots[i]))
            slots[w++] = slots[i];
    n = w;
}

void breakRuns (int* slots, int& n)
{
    bool on[16] {};
    for (int i = 0; i < n; ++i)
        if (slots[i] >= 0 && slots[i] < 16)
            on[slots[i]] = true;
    for (int s = 0; s < 13; ++s)
    {
        if (on[s] && on[s + 1] && on[s + 2] && on[s + 3])
            on[s + 1] = false;
    }
    n = 0;
    for (int s = 0; s < 16; ++s)
        if (on[s])
            slots[n++] = s;
}

void enforceHole (int* slots, int& n)
{
    int w = 0;
    for (int i = 0; i < n; ++i)
        if (slots[i] <= 11)
            slots[w++] = slots[i];
    n = w;
    if (n == 0)
    {
        slots[n++] = 1;
        slots[n++] = 5;
        slots[n++] = 9;
    }
}

void ungeneric (int* slots, int& n, bool allowKick)
{
    if (! genericSet (slots, n) && n >= 3)
        return;
    const int extra[] = { 1, 3, 5, 7, 9, 11, 13, 15, 2, 6, 10, 14 };
    for (int s : extra)
    {
        if (! allowKick && kickSlot (s))
            continue;
        bool clash = false;
        for (int i = 0; i < n; ++i)
            if (slots[i] == s)
                clash = true;
        if (clash)
            continue;
        if (n < 10)
            slots[n++] = s;
        if (! genericSet (slots, n))
            return;
    }
}

int assignSemi (const char* contour, int dialect, Role role, int index, int count, int partner, bool octave)
{
    (void) count;
    int semi = 0;
    if (std::strcmp (contour, "neighbour") == 0)
        semi = (index % 2) ? partner : 0;
    else if (std::strcmp (contour, "ladder_down") == 0)
    {
        // Two pitches carry the bar. A third degree shows up once so the
        // ladder is still audible and the top two stay above the floor.
        const int lad[] = { 7, 0, 7, 0, 3, 0 };
        semi = lad[index % 6];
    }
    else if (std::strcmp (contour, "fifth_pop") == 0)
        semi = (index % 2) ? 7 : 0;
    else if (std::strcmp (contour, "step_up") == 0)
    {
        const int up[] = { 0, 2, 0, 3, 0 };
        semi = up[index % 5];
    }
    else if (std::strcmp (contour, "call_answer_2bar") == 0)
    {
        // b2 sits next to the root, one semitone away.
        if (role == RB || role == RBp)
            semi = (index % 2) ? 0 : 1;
        else
            semi = (index % 2) ? 0 : 10;
    }
    else if (dialect == Long || dialect == Gaskin || dialect == Pop)
        semi = (index % 3 == 0) ? 0 : partner;
    else
        semi = (index % 3 == 2) ? partner : 0;

    if (octave && index > 0 && index == count - 1 && semi == 0)
        semi = 12;
    return semi;
}

void commitBar (Lane& lane, int bar, const int* slots, int n, float wantLen, bool hole,
                const char* contour, int dialect, Role role, int partner, Rng& rng, float ghostP,
                float freedom, char* breaks, int breaksN, float dark, float swingAmt,
                const int* velBySlot, const int* lenBySlot)
{
    int ord[16];
    const int m = std::min (n, 16);
    for (int i = 0; i < m; ++i)
        ord[i] = slots[i];
    std::sort (ord, ord + m);
    const int limit = hole ? 12 : 16;
    bool rootAt1 = false;
    int prevSemi = 0;
    bool colored = false;
    for (int i = 0; i < m; ++i)
    {
        if (ord[i] < 0 || ord[i] > 15 || ord[i] >= limit)
            continue;
        const int next = (i + 1 < m && ord[i + 1] < limit) ? ord[i + 1] : limit;
        const int hold = (lenBySlot != nullptr) ? lenBySlot[ord[i]] : 0;
        const float shape[] = { 1.05f, 1.45f, 1.15f, 0.8f };
        float len = hold > 1 ? (float) hold : wantLen * shape[ord[i] & 3];
        if (dialect == Pop)
            len = std::clamp (len, 0.75f, 1.75f);
        else if (dialect == Long || dialect == Gaskin)
            len = std::clamp (len, 1.5f, 3.25f);
        else
            len = std::clamp (len, 1.0f, 2.6f);
        if (rng.next() < 0.12 * (double) freedom)
            len = 0.5f + (float) (rng.next() * 3.5);
        len = std::min (len, (float) (next - ord[i]));
        if (len < 0.5f)
            continue;
        const bool oct = dialect == Pop && i == m - 1;
        int semi = assignSemi (contour, dialect, role, i, m, partner, oct);
        if (i == 0 && rng.next() < 0.18 * (double) freedom)
        {
            const int surprise[] = { 1, 2, 3, 5, 8, 10, 7 };
            semi = surprise[(int) (rng.next() * 7.0) % 7];
            noteBreak (breaks, breaksN, "iv");
        }
        else if (i > 0 && rng.next() < 0.04 * (double) freedom)
        {
            const int step = rng.next() < 0.5 ? -1 : 1;
            semi = prevSemi + step;
            noteBreak (breaks, breaksN, "iv");
        }
        if (ord[i] == 3 && rootAt1 && (semi % 12) == 0)
            semi = partner == 0 ? 7 : partner;
        if (ord[i] == 1 && (semi % 12) == 0)
            rootAt1 = true;
        const bool lastKept = (i + 1 == m) || (i + 1 < m && ord[i + 1] >= limit);
        semi = shadeInterval (semi, dark, partner, i, colored);
        if (lastKept && ! colored)
        {
            semi = dark >= 0.8f ? ((partner > 0 && partner <= 4) ? partner : 1) : (dark <= 0.2f ? 7 : semi);
            if ((semi % 12 + 12) % 12 != 0)
                colored = true;
        }
        prevSemi = semi;
        Ev e;
        e.start = (float) (bar * 16 + ord[i]);
        if ((ord[i] & 1) != 0)
            e.start += swingAmt;
        e.len = len;
        e.nChord = 1;
        e.chord[0] = (std::int8_t) clampi (semi, -24, 36);
        const int marked = (velBySlot != nullptr) ? velBySlot[ord[i]] : 0;
        const bool ghost = marked == 0 && i > 0 && rng.next() < (double) ghostP;
        e.vel = (std::uint8_t) clampi (ghost ? 42 : velCurve (ord[i], marked), 28, 124);
        e.flags = (e.vel < 50) ? (std::uint8_t) NoteGhost : 0;
        if (i > 0 && std::abs (semi) <= 3 && semi != 0 && rng.next() < 0.08)
            e.flags = (std::uint8_t) (e.flags | NoteGlide);
        pushEv (lane, e, 8);
    }
}

// The Ev helper above used a bogus method. Notes are filled in commitBar directly.
// (semiStored was a draft; the real fields are set below.)

void noteBreak (char* breaks, int breaksN, const char* id)
{
    if (breaks == nullptr || breaksN <= 0 || id == nullptr || std::strstr (breaks, id) != nullptr)
        return;
    const int lenB = (int) std::strlen (breaks);
    const int need = (int) std::strlen (id) + (lenB ? 1 : 0);
    if (lenB + need + 1 >= breaksN)
        return;
    if (lenB)
        std::strncat (breaks, ",", (size_t) (breaksN - lenB - 1));
    std::strncat (breaks, id, (size_t) (breaksN - std::strlen (breaks) - 1));
}

void writeBass (Lane& lane, const Settings& s, Rng& rng, const char* plan, const char* contour,
                int partner, char* cells, int cellsN, float freedom, char* breaks, int breaksN)
{
    clearLane (lane, lane.seed);
    Role roles[8];
    rolesFor (plan, roles);
    const bool allowKick = s.dialect == Gaskin || s.dialect == Long;
    const float ghostP = 0.22f;
    const genre::Cell* gcell = nullptr;
    {
        double sum = 0.0;
        for (int i = 0; i < genre::kN; ++i)
            if (genre::kCells[i].lane == genre::Bass)
                sum += (double) genre::weight (genre::kCells[i], s.dialect);
        // Transcription skeletons keep half the draw. Genre cells, including the
        // sparse ones, take the other half. Inference is already down-weighted.
        if (sum > 0.0 && rng.next() >= 0.5)
            gcell = pickCell (genre::Bass, s.dialect, rng, -1);
    }
    bool spaceCell = false;
    const genre::Cell* spaceDef = nullptr;
    for (int i = 0; i < genre::kN; ++i)
        if (std::strcmp (genre::kCells[i].id, "B_sparse_space") == 0)
            spaceDef = &genre::kCells[i];
    // One interior bar can be the space cell without pulling the whole phrase
    // out of the density band, so sparse bass still ships.
    const bool spaceBar = spaceDef != nullptr && rng.next() < 0.40;
    const int spaceAt = spaceBar ? (rng.next() < 0.5 ? 2 : 5) : -1;
    for (int bar = 0; bar < s.bars && bar < 8; ++bar)
    {
        Role role = roles[bar];
        int slots[16];
        int n = 0;
        float want = 2.f;
        int velBy[16] {};
        int lenBy[16] {};
        bool fromCell = false;
        if (spaceDef != nullptr && bar == spaceAt && role != RC && role != RDp)
        {
            GridBar decoded;
            if (decodeGrid (spaceDef->grid, decoded))
            {
                fromCell = true;
                spaceCell = true;
                want = 1.5f;
                for (int i = 0; i < decoded.n && n < 12; ++i)
                {
                    slots[n] = decoded.slot[i];
                    velBy[decoded.slot[i]] = decoded.vel[i];
                    lenBy[decoded.slot[i]] = decoded.len[i];
                    ++n;
                }
            }
        }
        if (! fromCell && gcell != nullptr)
        {
            GridBar decoded;
            if (decodeGrid (gcell->grid + 16 * (bar % std::max (1, gcell->bars)), decoded))
            {
                fromCell = true;
                want = s.dialect == Pop ? 1.1f : s.dialect == UkMin ? 1.75f : 2.1f;
                for (int i = 0; i < decoded.n && n < 12; ++i)
                {
                    slots[n] = decoded.slot[i];
                    velBy[decoded.slot[i]] = decoded.vel[i];
                    lenBy[decoded.slot[i]] = decoded.len[i];
                    ++n;
                }
                if (n > 0 && n <= 3)
                    spaceCell = true;
            }
        }
        if (! fromCell)
            baseSlots (s.dialect, role, rng, slots, n, want);
        if (s.busy > 0.7f && n > 0 && n < 8 && role != RC)
        {
            if (n < 12)
                slots[n++] = (slots[0] + 7) % 12;
        }
        else if (s.busy < 0.25f && n > 4)
            --n;
        mutateBack (role, rng, slots, n);
        if (s.bounce > 0.6f)
        {
            for (int i = 0; i < n; ++i)
                if ((slots[i] & 1) == 0 && slots[i] < 15 && ! kickSlot (slots[i] + 1))
                    slots[i] += 1;
        }
        const bool turn = role == RC || role == RDp || bar == s.bars - 1;
        if (turn && rng.next() > 0.55 * (double) freedom)
            enforceHole (slots, n);
        else if (turn)
            noteBreak (breaks, breaksN, "rest");
        for (int i = 0; i < n; ++i)
        {
            if (rng.next() < 0.20 + 0.50 * (double) freedom)
                continue;
            const int dir = rng.next() < 0.5 ? -1 : 1;
            const int moved = slots[i] + dir;
            if (moved < 0 || moved > 15)
                continue;
            if (turn && moved > 11 && rng.next() > 0.55 * (double) freedom)
                continue;
            if (! (allowKick && role != RC) && kickSlot (moved) && rng.next() > 0.40 * (double) freedom)
                continue;
            bool clash = false;
            for (int k = 0; k < n; ++k)
                if (slots[k] == moved)
                    clash = true;
            if (! clash)
                slots[i] = moved;
        }
        stripKick (slots, n, (allowKick && role != RC) || rng.next() < 0.20 * (double) freedom);
        if (rng.next() < 0.10 * (double) freedom && n > 0 && n < 12)
        {
            const int odd[] = { 1, 3, 7, 11, 13, 15 };
            const int sslot = odd[(int) (rng.next() * 6.0) % 6];
            bool clash = false;
            for (int i = 0; i < n; ++i)
                if (slots[i] == sslot)
                    clash = true;
            if (! clash)
            {
                slots[n++] = sslot;
                noteBreak (breaks, breaksN, "slot");
            }
        }
        breakRuns (slots, n);
        if (! fromCell)
            ungeneric (slots, n, allowKick);
        if (turn && rng.next() > 0.55 * (double) freedom)
            enforceHole (slots, n);
        int kicks = 0;
        for (int i = 0; i < n; ++i)
            if (kickSlot (slots[i]))
                ++kicks;
        while (n > 1 && kicks > 0 && (float) kicks / (float) n > 0.35f)
        {
            for (int i = n - 1; i >= 0; --i)
            {
                if (! kickSlot (slots[i]))
                    continue;
                const int alt = slots[i] < 15 ? slots[i] + 1 : slots[i] - 1;
                bool clash = false;
                for (int j = 0; j < n; ++j)
                    if (slots[j] == alt)
                        clash = true;
                if (! clash)
                    slots[i] = alt;
                else
                {
                    slots[i] = slots[n - 1];
                    --n;
                }
                break;
            }
            kicks = 0;
            for (int i = 0; i < n; ++i)
                if (kickSlot (slots[i]))
                    ++kicks;
        }
        if (n == 0)
        {
            slots[n++] = 2;
            slots[n++] = 6;
        }
        if (! fromCell && inbeatUsed (slots, n) < 3)
        {
            const int add[] = { 1, 6, 11 };
            for (int sslot : add)
            {
                bool clash = false;
                for (int i = 0; i < n; ++i)
                    if (slots[i] == sslot)
                        clash = true;
                if (! clash && n < 12)
                    slots[n++] = sslot;
            }
        }
        if (n > 10)
            n = 10;
        const bool wantHole = turn && (breaks == nullptr || std::strstr (breaks, "rest") == nullptr);
        commitBar (lane, bar, slots, n, want, wantHole,
                   contour, s.dialect, role, partner, rng, ghostP, freedom, breaks, breaksN,
                   s.dark, laneSwing (Bass, s.dialect), fromCell ? velBy : nullptr, fromCell ? lenBy : nullptr);
    }
    if (gcell != nullptr)
        std::snprintf (cells, (size_t) cellsN, "%s", gcell->id);
    else
        std::snprintf (cells, (size_t) cellsN, "%s",
                       s.dialect == Pop ? "off_8th_pump,fifth_pop,rest"
                       : s.dialect == Gaskin ? "dotted_grid,call_answer_2bar"
                       : s.dialect == Long ? "hold_lean,answer_tag,pedal,rest"
                                            : "beat1_signature,hold_lean,syncop_pair,answer_tag,rest");
    if (spaceCell)
        tagAdd (cells, cellsN, "space");
}

bool bassSounds (const Lane& lane, int slot)
{
    for (int i = 0; i < lane.count; ++i)
    {
        const float a = lane.notes[i].start;
        const float b = a + lane.notes[i].length;
        if (a < (float) slot + 1.f - 0.05f && b > (float) slot + 0.05f)
            return true;
    }
    return false;
}

bool bassOnset (const Lane& lane, int slot)
{
    for (int i = 0; i < lane.count; ++i)
        if ((int) std::lround (lane.notes[i].start) == slot)
            return true;
    return false;
}

void writeStab (Lane& lane, const Lane& bass, const Settings& s, Rng& rng, float freedom, char* breaks, int breaksN)
{
    clearLane (lane, lane.seed);
    const int per = s.dialect == Pop ? 8 : s.dialect == Gaskin ? 6 : s.dialect == Long ? 3 : (s.stab > 0.66f ? 2 : 1);
    const int voicings[][3] = { { 3, 10, 15 }, { 7, 10, 19 }, { 3, 7, 14 }, { 10, 14, 17 } };
    int overlaps = 0;
    int total = 0;
    for (int bar = 0; bar < s.bars && bar < 8; ++bar)
    {
        const int* voi = voicings[(bar / 2) & 3];
        int placed = 0;
        for (int pass = 0; pass < 16 && placed < per; ++pass)
        {
            int slot = (int) (rng.next() * 16);
            if (kickSlot (slot))
                continue;
            const int abs = bar * 16 + slot;
            const bool onBass = bassOnset (bass, abs);
            const double keepOnBass = std::min (0.70, 0.25 * (double) freedom);
            if (onBass && rng.next() > keepOnBass)
                continue;
            if (s.dialect == UkMin && bassSounds (bass, abs) && ! onBass && rng.next() > std::min (0.80, 0.40 * (double) freedom))
                continue;
            if (onBass)
                noteBreak (breaks, breaksN, "stab");
            const float shape[] = { 1.4f, 1.0f, 1.8f, 0.75f };
            float len = (s.dialect == Gaskin ? 6.f : s.dialect == Pop ? 1.f : 2.f) * shape[slot & 3];
            if (s.dialect == Pop)
                len = std::clamp (len, 0.75f, 1.75f);
            else
                len = std::clamp (len, 1.f, s.dialect == Gaskin ? 10.f : 4.f);
            const float barEnd = (float) ((bar + 1) * 16);
            if ((float) abs + len > barEnd)
                len = barEnd - (float) abs;
            if (len < 0.5f)
                continue;
            Ev e;
            e.start = (float) abs + ((slot & 1) ? laneSwing (Stab, s.dialect) : 0.f);
            e.len = len;
            e.nChord = 3;
            e.vel = (std::uint8_t) velCurve (slot, placed == 0 ? 118 : 100);
            for (int c = 0; c < 3; ++c)
                e.chord[c] = (std::int8_t) voi[c];
            if (pushEv (lane, e, s.bars))
            {
                ++placed;
                ++total;
                if (onBass)
                    ++overlaps;
            }
        }
    }
}

void writeHits (Lane& lane, const Groove& g, const Settings& s, Rng& rng)
{
    clearLane (lane, lane.seed);
    const int bars[4] = { 1, 3, 5, 7 };
    const int nHits = 2 + (int) (rng.next() * 3);
    for (int h = 0; h < nHits; ++h)
    {
        const int bar = bars[(int) (rng.next() * 4) % 4];
        if (bar >= s.bars)
            continue;
        for (int slot : { 13, 15, 14 })
        {
            const int abs = bar * 16 + slot;
            if (kickSlot (slot))
                continue;
            bool taken = false;
            for (int p = 0; p < Drums; ++p)
            {
                if (p == Hits)
                    continue;
                for (int i = 0; i < g.lane[p].count; ++i)
                    if ((int) std::lround (g.lane[p].notes[i].start) == abs)
                        taken = true;
            }
            if (taken)
                continue;
            float len = 1.5f + (float) ((int) (rng.next() * 8)) * 0.5f;
            const float end = (float) (s.bars * 16);
            if ((float) abs + len > end)
                len = end - (float) abs;
            if ((float) abs + len > (float) ((bar + 1) * 16))
                len = (float) ((bar + 1) * 16 - abs);
            if (len < 1.f)
                continue;
            Ev e;
            e.start = (float) abs;
            e.len = len;
            e.nChord = 1;
            const int degs[] = { 0, 2, 3, 5, 7, 10 };
            e.chord[0] = (std::int8_t) degs[(int) (rng.next() * 6.0) % 6];
            e.vel = 90;
            pushEv (lane, e, s.bars);
            break;
        }
    }
}

void writePluck (Lane& lane, const Settings& s, Rng& rng, char* cells, int cellsN)
{
    clearLane (lane, lane.seed);
    const int degs[] = { 0, 7, 3, 10, 2, 5, 8 };
    const float lens[] = { 0.5f, 1.0f, 1.5f, 2.0f };
    const int vels[] = { 112, 84, 58 };
    const bool useCell = rng.next() < 0.28;
    const genre::Cell* cell = useCell ? pickCell (genre::Pluck, s.dialect, rng, -1) : nullptr;
    const int step = pickPolyStep (s.dialect, rng, false);
    const int phase = (int) (rng.next() * (double) step);
    const int holeEvery = 4 + (int) (rng.next() * 4.0);
    const int nudge = s.bounce >= 0.6f ? 1 : 0;
    const float sw = laneSwing (Pluck, s.dialect);
    int placed = 0;
    if (cell != nullptr && cell->bars > 0 && std::strcmp (cell->id, "PL_rest") != 0)
    {
        tagAdd (cells, cellsN, cell->id);
        for (int bar = 0; bar < s.bars && bar < 8; ++bar)
        {
            GridBar decoded;
            if (! decodeGrid (cell->grid + 16 * (bar % cell->bars), decoded))
                continue;
            for (int i = 0; i < decoded.n; ++i)
            {
                const int slot = (decoded.slot[i] + nudge) % 16;
                Ev e;
                e.start = (float) (bar * 16 + slot) + ((slot & 1) ? sw : 0.f);
                e.len = std::min ((float) std::max (1, decoded.len[i]), lens[placed % 4] + 0.5f);
                e.nChord = 1;
                e.chord[0] = (std::int8_t) snapScale (s.dialect, degs[placed % 7]);
                e.vel = (std::uint8_t) (decoded.vel[i] < 55 ? 42 : vels[placed % 3]);
                e.flags = e.vel < 50 ? (std::uint8_t) NoteGhost : 0;
                if (pushEv (lane, e, s.bars))
                    ++placed;
            }
        }
    }
    if (lane.count == 0)
    {
        char mark[8];
        std::snprintf (mark, sizeof mark, "p%d", step);
        tagAdd (cells, cellsN, mark);
        for (int t = (phase + nudge) % step; t < s.bars * 16; t += step)
        {
            if (holeEvery > 0 && (placed % holeEvery) == holeEvery - 1)
            {
                ++placed;
                continue;
            }
            const int slot = t % 16;
            Ev e;
            e.start = (float) t + ((slot & 1) ? sw : 0.f);
            e.len = std::min (lens[placed % 4], (float) (step));
            if (e.start + e.len > (float) (s.bars * 16))
                e.len = (float) (s.bars * 16) - e.start;
            e.nChord = 1;
            e.chord[0] = (std::int8_t) snapScale (s.dialect, degs[(placed + phase) % 7]);
            e.vel = (std::uint8_t) vels[placed % 3];
            e.flags = e.vel < 50 ? (std::uint8_t) NoteGhost : 0;
            if (pushEv (lane, e, s.bars))
                ++placed;
            if (placed >= 48)
                break;
        }
    }
    if (lane.count == 0)
    {
        Ev e;
        e.start = 6.f;
        e.len = 1.f;
        e.nChord = 1;
        e.chord[0] = 0;
        e.vel = 84;
        pushEv (lane, e, s.bars);
    }
}

void writePad (Lane& lane, const Settings& s, Rng& rng)
{
    clearLane (lane, lane.seed);
    const int bars[6] = { 0, 1, 2, 3, 4, 5 };
    const int b0 = bars[(int) (rng.next() * 4) % 4];
    int b1 = b0 + 3 + (int) (rng.next() * 2);
    if (b1 > 5)
        b1 = 5;
    if (b1 == b0)
        b1 = std::min (5, b0 + 2);
    const int voi[2][3] = { { 3, 10, 15 }, { 7, 14, 17 } };
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
        const int pool[] = { 3, 7, 10, 14, 15, 17, 2, 8 };
        Ev e;
        e.start = start;
        e.len = len;
        e.nChord = 3;
        e.vel = 70;
        for (int c = 0; c < 3; ++c)
            e.chord[c] = (std::int8_t) pool[(int) (rng.next() * 8) % 8];
        (void) voi;
        pushEv (lane, e, s.bars);
    }
}

void writeVox (Lane& lane, const Settings& s, Rng& rng, char* cells, int cellsN)
{
    clearLane (lane, lane.seed);
    const float sw = laneSwing (Vox, s.dialect);
    const int degs[] = { 0, 3, 7, 10, 2 };
    auto place = [&] (int bar, int slot, float len, int vel, int deg)
    {
        if (bar < 0 || bar >= s.bars || slot < 0 || slot > 15)
            return;
        Ev e;
        e.start = (float) (bar * 16 + slot) + ((slot & 1) ? sw : 0.f);
        e.len = len;
        const float barEnd = (float) ((bar + 1) * 16);
        if (e.start + e.len > barEnd)
            e.len = barEnd - e.start;
        if (e.len < 0.5f)
            return;
        e.nChord = 1;
        e.chord[0] = (std::int8_t) (snapScale (s.dialect, deg) - 4);
        e.vel = (std::uint8_t) clampi (vel, 36, 120);
        e.flags = e.vel < 50 ? (std::uint8_t) NoteGhost : 0;
        pushEv (lane, e, s.bars);
    };

    if (s.dialect == Pop || s.dialect == Gaskin)
    {
        // Phrases sit in the back half of bars 4 and 8 (VX_phrase_bar4 / VX_word_a4 / chops).
        int phrase[2];
        int nPhrase = 0;
        if (s.bars >= 8)
        {
            phrase[0] = 3;
            phrase[1] = 7;
            nPhrase = 2;
        }
        else if (s.bars >= 4)
        {
            phrase[0] = 3;
            nPhrase = 1;
        }
        else
        {
            phrase[0] = s.bars - 1;
            nPhrase = 1;
        }
        if (s.dialect == Pop)
        {
            tagAdd (cells, cellsN, "VX_phrase_bar4");
            const int slots[] = { 8, 10, 12, 14 };
            const float lens[] = { 4.f, 1.5f, 1.0f, 1.25f };
            const int vel[] = { 112, 86, 64, 96 };
            for (int p = 0; p < nPhrase; ++p)
                for (int i = 0; i < 4; ++i)
                    place (phrase[p], slots[i], lens[i], vel[i], degs[(p + i) % 5]);
        }
        else
        {
            tagAdd (cells, cellsN, "VX_chop_syncop");
            const int slots[] = { 9, 11, 14 };
            const float lens[] = { 1.0f, 1.5f, 2.0f };
            const int vel[] = { 108, 78, 118 };
            for (int p = 0; p < nPhrase; ++p)
                for (int i = 0; i < 3; ++i)
                    place (phrase[p], slots[i], lens[i], vel[i], degs[(p + i) % 5]);
            tagAdd (cells, cellsN, "VX_word_a4");
        }
        return;
    }

    if (rng.next() < 0.7 && s.bars > 0)
    {
        const int bar = s.bars >= 4 ? 3 : s.bars - 1;
        place (bar, 10, 2.f, 72, 0);
        tagAdd (cells, cellsN, "VX_breath_tex");
    }
}

void writeDrums (DrumLane& drums, std::uint32_t seed, const Settings& s, Rng& rng, char* cells, int cellsN)
{
    drums.seed = seed ? seed : 1u;
    std::memset (drums.vel, 0, sizeof drums.vel);
    std::memset (drums.accent, 0, sizeof drums.accent);
    const int steps = s.bars * 16;
    const int nudge = s.bounce >= 0.6f ? 1 : 0;

    bool twoStep = false;
    const genre::Cell* kickCell = nullptr;
    if (s.dialect == Gaskin)
    {
        for (int attempt = 0; attempt < 4 && kickCell == nullptr; ++attempt)
        {
            const genre::Cell* cand = pickCell (genre::Drum, s.dialect, rng, Kick);
            if (cand != nullptr && gridCoversBars (*cand, s.bars))
                kickCell = cand;
        }
        if (kickCell != nullptr && std::strcmp (kickCell->id, "K_2step") == 0)
            twoStep = true;
    }
    if (kickCell != nullptr)
        tagAdd (cells, cellsN, kickCell->id);
    if (twoStep)
        tagAdd (cells, cellsN, "2step");

    for (int bar = 0; bar < s.bars; ++bar)
    {
        if (kickCell != nullptr)
        {
            GridBar decoded;
            decodeGrid (kickCell->grid + 16 * (bar % kickCell->bars), decoded);
            for (int i = 0; i < decoded.n; ++i)
                drums.vel[Kick][bar * 16 + decoded.slot[i]] = (std::uint8_t) clampi (decoded.vel[i], 70, 124);
        }
        else
        {
            for (int slot : { 0, 4, 8, 12 })
                drums.vel[Kick][bar * 16 + slot] = 112;
        }
        drums.vel[Clap][bar * 16 + 4] = 104;
        drums.vel[Clap][bar * 16 + 12] = 104;
        if (twoStep || s.busy > 0.65f || rng.next() < 0.55)
        {
            drums.vel[Snare][bar * 16 + 4] = twoStep ? 96 : 72;
            drums.vel[Snare][bar * 16 + 12] = twoStep ? 88 : 64;
        }
        const int rimSlot = (2 + bar * 3 + nudge) % 16;
        if (! kickSlot (rimSlot))
            drums.vel[Rim][bar * 16 + rimSlot] = 74;
        const int tomSlot = (7 + bar * 5) % 16;
        if (! kickSlot (tomSlot))
            drums.vel[TomLow][bar * 16 + tomSlot] = (std::uint8_t) (60 + (bar & 1) * 18);
    }

    const int hatStep = pickPolyStep (s.dialect, rng, true);
    const int hatPhase = (int) (rng.next() * (double) hatStep);
    const int lenCycle = rng.next() < 0.5 ? 3 : 5;
    char hatMark[8];
    std::snprintf (hatMark, sizeof hatMark, "h%d", hatStep);
    tagAdd (cells, cellsN, hatMark);
    int hatN = 0;
    const int hatDrop = (int) (rng.next() * 7.0);
    for (int bar = 0; bar < s.bars; ++bar)
    {
        int placedSlot[16];
        int placedN = 0;
        for (int slot = 0; slot < 16; ++slot)
        {
            if (((slot + hatPhase + nudge) % hatStep) != 0)
                continue;
            const bool open = (hatN % lenCycle) == 0;
            const int vel = (hatN % 3 == 0) ? 112 : (hatN % 3 == 1) ? 78 : 46;
            const int voice = open ? HatO : HatC;
            drums.vel[voice][bar * 16 + slot] = (std::uint8_t) vel;
            if (placedN < 16)
                placedSlot[placedN++] = slot;
            ++hatN;
        }
        if (placedN >= 3)
        {
            const int kill = placedSlot[(bar + hatDrop) % placedN];
            drums.vel[HatC][bar * 16 + kill] = 0;
            drums.vel[HatO][bar * 16 + kill] = 0;
        }
    }

    const int percStep = pickPolyStep (s.dialect, rng, false);
    const int percPhase = (int) (rng.next() * (double) percStep);
    char percMark[8];
    std::snprintf (percMark, sizeof percMark, "r%d", percStep);
    tagAdd (cells, cellsN, percMark);
    int percN = 0;
    const int percDrop = (int) (rng.next() * 5.0);
    int percBarCount[8] {};
    int percBarSlot[8][16] {};
    for (int t = (percPhase + nudge) % percStep; t < steps; t += percStep)
    {
        if (s.busy < 0.3f && (percN & 1))
        {
            ++percN;
            continue;
        }
        const int vel = (percN % 3 == 0) ? 104 : (percN % 3 == 1) ? 74 : 42;
        if (drums.vel[Perc][t] == 0)
            drums.vel[Perc][t] = (std::uint8_t) vel;
        const int bar = t / 16;
        if (bar >= 0 && bar < 8 && percBarCount[bar] < 16)
            percBarSlot[bar][percBarCount[bar]++] = t % 16;
        ++percN;
    }
    for (int bar = 0; bar < s.bars && bar < 8; ++bar)
    {
        if (percBarCount[bar] < 2)
            continue;
        const int kill = percBarSlot[bar][(bar + percDrop) % percBarCount[bar]];
        drums.vel[Perc][bar * 16 + kill] = 0;
    }

    const int rideHome = (rng.next() < 0.5 && s.bars > 4) ? 4 : 0;
    const int rideSpan = s.busy > 0.7f ? s.bars : std::min (4, s.bars);
    for (int bar = rideHome; bar < s.bars && bar < rideHome + rideSpan; ++bar)
        for (int slot : { 2, 6, 10, 14 })
            drums.vel[Ride][bar * 16 + ((slot + nudge) % 16)] = 86;
    if (s.busy < 0.3f)
    {
        for (int t = 0; t < steps; ++t)
            drums.vel[Ride][t] = 0;
        drums.vel[Ride][10] = 80;
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
            if ((t & 1) == 0 || drums.vel[HatC][t] || drums.vel[HatO][t] || kickSlot (t % 16))
                continue;
            drums.vel[HatC][t] = 36;
        }
        for (int bar = 0; bar < s.bars; ++bar)
            for (int slot : { 2, 6, 10, 14 })
                if (drums.vel[Ride][bar * 16 + slot] == 0)
                    drums.vel[Ride][bar * 16 + slot] = 78;
    }
    if (drums.vel[Ride][0] == 0)
    {
        bool anyRide = false;
        for (int t = 0; t < steps; ++t)
            anyRide = anyRide || drums.vel[Ride][t] > 0;
        if (! anyRide)
            drums.vel[Ride][std::min (10, steps - 1)] = 80;
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
    else if (g.dialect == Gaskin)
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

void generate (Groove& g, const Settings& settings, const std::uint32_t seeds[NumParts], std::uint32_t partMask)
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
    s.freedom = std::clamp (s.freedom, 0.f, 1.f);

    auto regen = [&] (int part) { return (partMask & (1u << part)) != 0; };

    Lane kept[NumParts];
    DrumLane keptDrums = g.drums;
    for (int p = 0; p < NumParts; ++p)
        kept[p] = g.lane[p];

    g.bars = s.bars;
    g.key = s.key;
    g.dialect = s.dialect;

    const std::uint32_t bassSeed = seeds[Bass] ? seeds[Bass] : 1u;
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

        Rng rng (bassSeed + (std::uint32_t) attempt * 0x9E3779B9u);
        const char* planName = "P1";
        bool offPlan = false;
        bool offContour = false;
        planOf (s.dialect, rng, planName, freedom, offPlan);
        const char* contour = contourFor (s.dialect, rng, freedom, offContour);
        const int partner = partnerFor (s.dialect, s.dark, rng);
        char cells[96] = "";
        char breaks[48] = "";
        if (offPlan)
            noteBreak (breaks, (int) sizeof breaks, "plan");
        if (offContour)
            noteBreak (breaks, (int) sizeof breaks, "contour");

        if (regen (Bass))
        {
            g.lane[Bass].seed = bassSeed;
            writeBass (g.lane[Bass], s, rng, planName, contour, partner, cells, (int) sizeof cells,
                       freedom, breaks, (int) sizeof breaks);
            g.lane[Bass].seed = bassSeed;
        }
        else
            clampLane (g.lane[Bass], s.bars);

        if (regen (Stab))
        {
            Rng rs ( (seeds[Stab] ? seeds[Stab] : 1u) + (std::uint32_t) attempt * 17u);
            g.lane[Stab].seed = seeds[Stab] ? seeds[Stab] : 1u;
            writeStab (g.lane[Stab], g.lane[Bass], s, rs, freedom, breaks, (int) sizeof breaks);
        }
        else
            clampLane (g.lane[Stab], s.bars);

        if (regen (Pluck))
        {
            Rng rp (seeds[Pluck] ? seeds[Pluck] : 1u);
            g.lane[Pluck].seed = rp.s;
            writePluck (g.lane[Pluck], s, rp, cells, (int) sizeof cells);
        }
        else
            clampLane (g.lane[Pluck], s.bars);

        if (regen (Pad))
        {
            Rng rpad (seeds[Pad] ? seeds[Pad] : 1u);
            g.lane[Pad].seed = rpad.s;
            writePad (g.lane[Pad], s, rpad);
        }
        else
            clampLane (g.lane[Pad], s.bars);

        if (regen (Vox))
        {
            Rng rv (seeds[Vox] ? seeds[Vox] : 1u);
            g.lane[Vox].seed = rv.s;
            writeVox (g.lane[Vox], s, rv, cells, (int) sizeof cells);
        }
        else
            clampLane (g.lane[Vox], s.bars);

        if (regen (Hits))
        {
            Rng rh (seeds[Hits] ? seeds[Hits] : 1u);
            g.lane[Hits].seed = rh.s;
            writeHits (g.lane[Hits], g, s, rh);
        }
        else
            clampLane (g.lane[Hits], s.bars);

        if (regen (Drums))
        {
            Rng rd (seeds[Drums] ? seeds[Drums] : 1u);
            writeDrums (g.drums, seeds[Drums], s, rd, cells, (int) sizeof cells);
        }

        {
            int opts[5];
            int nOpt = 0;
            const int cands[] = { Stab, Hits, Pluck, Pad, Vox };
            for (int lane : cands)
            {
                if (! regen (lane) || g.lane[lane].count < 2)
                    continue;
                if (lane == Vox && (s.dialect == Pop || s.dialect == Gaskin))
                    continue;
                opts[nOpt++] = lane;
            }
            if (nOpt > 0)
            {
                const int lane = opts[(int) (rng.next() * (double) nOpt) % nOpt];
                const float cut = (float) ((s.bars / 2) * 16);
                Note keptNotes[kMaxNotes];
                int keptN = 0;
                for (int i = 0; i < g.lane[lane].count; ++i)
                    if (g.lane[lane].notes[i].start < cut && keptN < kMaxNotes)
                        keptNotes[keptN++] = g.lane[lane].notes[i];
                if (keptN > 0 && keptN < g.lane[lane].count)
                {
                    g.lane[lane].count = keptN;
                    for (int i = 0; i < keptN; ++i)
                        g.lane[lane].notes[i] = keptNotes[i];
                    const char* mark = lane == Stab ? "subS" : lane == Hits ? "subH" : lane == Pluck ? "subP"
                                      : lane == Pad ? "subD" : "subV";
                    tagAdd (cells, (int) sizeof cells, mark);
                }
            }
        }

        ++draws;
        std::snprintf (g.brokenRules, sizeof g.brokenRules, "%s|%s|%s|%s", planName, contour, cells, breaks);
        last.g = g;
        last.penalty = softPenalty (g, freedom, planName, offPlan, offContour);
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
    g.guardScore = shippedHard ? 0.f : 1.f;
    g.guardLevel = shippedHard ? 2 : 0;
    std::snprintf (g.guardRef, sizeof g.guardRef, "%s", shippedHard ? "HARD" : "ORIGINAL");
    ++g.generation;
}

} // namespace rolla
