#include "Generator.h"

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
    const int bright[] = { 7, 2, 5 };
    const int darkd[] = { 10, 3, 8 };
    const int* pool = dark >= 0.5f ? darkd : bright;
    int p = pool[(int) (rng.next() * 3.0) % 3];
    if (dialect == Pop)
        p = rng.next() < 0.8 ? 7 : p;
    if (dialect == Gaskin && rng.next() < 0.25)
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
                float freedom, char* breaks, int breaksN, float dark)
{
    int ord[16];
    const int m = std::min (n, 16);
    for (int i = 0; i < m; ++i)
        ord[i] = slots[i];
    std::sort (ord, ord + m);
    const int limit = hole ? 12 : 16;
    bool rootAt1 = false;
    int prevSemi = 0;
    for (int i = 0; i < m; ++i)
    {
        if (ord[i] >= limit)
            continue;
        const int next = (i + 1 < m) ? ord[i + 1] : limit;
        const float menu[4] = { 1.5f, 2.f, 2.f, 2.5f };
        float len = menu[(int) (rng.next() * 4.0) % 4];
        if (dialect == Pop)
            len = rng.next() < 0.5 ? 1.f : 1.25f;
        else if (dialect == Long || dialect == Gaskin)
            len = std::max (len, 2.f);
        else
            len = std::max (1.f, std::min (len, wantLen + 0.5f));
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
            if (breaks != nullptr && breaksN > 0 && std::strstr (breaks, "iv") == nullptr)
            {
                const int lenB = (int) std::strlen (breaks);
                if (lenB + 3 < breaksN)
                    std::strncat (breaks, lenB ? ",iv" : "iv", (size_t) (breaksN - lenB - 1));
            }
        }
        else if (i > 0 && rng.next() < 0.04 * (double) freedom)
        {
            const int step = rng.next() < 0.5 ? -1 : 1;
            semi = prevSemi + step;
            if (breaks != nullptr && breaksN > 0 && std::strstr (breaks, "iv") == nullptr)
            {
                const int lenB = (int) std::strlen (breaks);
                if (lenB + 3 < breaksN)
                    std::strncat (breaks, lenB ? ",iv" : "iv", (size_t) (breaksN - lenB - 1));
            }
        }
        if (ord[i] == 3 && rootAt1 && (semi % 12) == 0)
            semi = partner == 0 ? 7 : partner;
        if (ord[i] == 1 && (semi % 12) == 0)
            rootAt1 = true;
        // DARK 0 and DARK 1 have to move the bass. Several contours ignore the
        // partner degree, so the extremes retint the bar: a 5th when it is
        // bright, a b2 when it is dark. The middle of the knob leaves the contour.
        if (dark <= 0.2f)
            semi = (i == m - 1) ? 7 : ((i % 2) ? 7 : 0);
        else if (dark >= 0.8f)
            semi = (i == m - 1) ? 1 : ((i % 2) ? 3 : 0);
        prevSemi = semi;
        if (semi == 1 && i + 1 < m)
        {
            // Leave toward root on the next note when the contour did not.
        }
        Ev e;
        e.start = (float) (bar * 16 + ord[i]);
        e.len = len;
        e.nChord = 1;
        e.chord[0] = (std::int8_t) clampi (semi, -24, 36);
        const bool ghost = i > 0 && rng.next() < (double) ghostP;
        e.vel = ghost ? 80 : 112;
        e.flags = ghost ? (std::uint8_t) NoteGhost : 0;
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
    for (int bar = 0; bar < s.bars && bar < 8; ++bar)
    {
        Role role = roles[bar];
        int slots[16];
        int n = 0;
        float want = 2.f;
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
        if (inbeatUsed (slots, n) < 3)
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
                   contour, s.dialect, role, partner, rng, ghostP, freedom, breaks, breaksN, s.dark);
    }
    std::snprintf (cells, (size_t) cellsN, "%s",
                   s.dialect == Pop ? "off_8th_pump,fifth_pop,rest"
                   : s.dialect == Gaskin ? "dotted_grid,call_answer_2bar"
                   : s.dialect == Long ? "hold_lean,answer_tag,pedal,rest"
                                        : "beat1_signature,hold_lean,syncop_pair,answer_tag,rest");
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
            float len = s.dialect == Gaskin ? 8.f : s.dialect == Pop ? 1.f : 2.f;
            const float barEnd = (float) ((bar + 1) * 16);
            if ((float) abs + len > barEnd)
                len = barEnd - (float) abs;
            if (len < 0.5f)
                continue;
            Ev e;
            e.start = (float) abs;
            e.len = len;
            e.nChord = 3;
            e.vel = 98;
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

void writePluck (Lane& lane, const Settings& s, Rng& rng)
{
    clearLane (lane, lane.seed);
    const int per = s.dialect == Pop ? 6 : 3;
    const int degs[] = { 0, 3, 7, 10, 2 };
    for (int bar = 0; bar < s.bars && bar < 8; ++bar)
    {
        int placed = 0;
        for (int pass = 0; pass < 16 && placed < per; ++pass)
        {
            const int slot = 1 + (int) (rng.next() * 14);
            Ev e;
            e.start = (float) (bar * 16 + slot);
            e.len = 0.5f + (float) rng.next();
            if (e.len > 1.5f)
                e.len = 1.5f;
            e.nChord = 1;
            e.chord[0] = (std::int8_t) degs[(bar + placed) % 5];
            e.vel = 84;
            if (pushEv (lane, e, s.bars))
                ++placed;
        }
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

void writeVox (Lane& lane, const Settings& s, Rng& rng)
{
    clearLane (lane, lane.seed);
    const int nCuts = 1 + (int) (rng.next() * 3);
    int slots[3];
    for (int i = 0; i < nCuts; ++i)
        slots[i] = (int) (2 + rng.next() * 12);
    const int shift = (int) (rng.next() * 11) - 5;
    const bool ghost = rng.next() < 0.3;
    for (int bar : { 0, 2, 4, 6 })
    {
        if (bar >= s.bars)
            continue;
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
    for (int bar = 0; bar < s.bars; ++bar)
    {
        const int hats[] = { 2, 6, 10, 14 };
        for (int k = 0; k < 4; ++k)
        {
            int slot = hats[k];
            const bool open = (slot == 14) && ((bar & 1) == 1);
            if (open)
                drums.vel[HatO][bar * 16 + slot] = (std::uint8_t) (70 + (int) (rng.next() * 30));
            else
                drums.vel[HatC][bar * 16 + slot] = (std::uint8_t) (64 + (int) (rng.next() * 40));
        }
        const int drop = hats[(int) (rng.next() * 4) % 4];
        drums.vel[HatC][bar * 16 + drop] = 0;
        drums.vel[HatO][bar * 16 + drop] = 0;
        auto place = [&] (int voice)
        {
            int slot = 1 + (int) (rng.next() * 14);
            if (kickSlot (slot))
                slot = (slot + 1) % 16;
            drums.vel[voice][bar * 16 + slot] = (std::uint8_t) (55 + (int) (rng.next() * 40));
        };
        place (Perc);
        place (Perc);
        place (Rim);
        place (TomLow);
        if (rng.next() < 0.5)
            place (Snare);
    }
    // Ride belongs in the pattern, for four bars at a time. BUSY opens that
    // spell and adds hats; a quiet BUSY keeps a single ride and thins the hats.
    // The overlay does not draw from the drum seed, so min and max cannot tie.
    const int rideBars = s.busy > 0.7f ? s.bars : (s.busy < 0.3f ? 1 : std::min (4, s.bars));
    const int rideVel = s.busy > 0.7f ? 96 : 80;
    for (int bar = 0; bar < rideBars && bar < s.bars; ++bar)
        for (int slot : { 2, 6, 10, 14 })
            drums.vel[Ride][bar * 16 + slot] = (std::uint8_t) rideVel;
    if (s.busy < 0.3f)
    {
        for (int t = 0; t < steps; ++t)
            if (t != 10)
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
            if ((t & 1) == 0 || drums.vel[HatC][t] != 0 || drums.vel[HatO][t] != 0 || kickSlot (t % 16))
                continue;
            drums.vel[HatC][t] = 40;
        }
        for (int bar = 0; bar < s.bars; ++bar)
            if (drums.vel[Perc][bar * 16 + 3] == 0)
                drums.vel[Perc][bar * 16 + 3] = 70;
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
        char cells[64] = "rest";
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
            writePluck (g.lane[Pluck], s, rp);
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
            writeVox (g.lane[Vox], s, rv);
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
            writeDrums (g.drums, seeds[Drums], s, rd);
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
