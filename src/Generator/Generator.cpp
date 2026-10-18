#include "Generator.h"
#include "PhraseBank.h"

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
        if (s.styleIntensity <= 0.25f && n > 4)
            n = 4;
        else if (s.styleIntensity >= 0.75f && n < 10)
        {
            const int extra = s.dialect == Gaskin ? 15 : (s.dialect == Pop ? 11 : 3);
            bool clash = false;
            for (int i = 0; i < n; ++i)
                if (slots[i] == extra)
                    clash = true;
            if (! clash)
                slots[n++] = extra;
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


// JACKIN's 2-bar answer was landing in the front half of the bar, so the
// scorer read the variation as early. Keep the grammar's own back-half
// attacks, move front-half attacks onto free back-half slots (the answer bar
// takes the slots the call does not), and let a short tail — under 6
// sixteenths — carry a different pitch across the next bar's downbeat.
// Bars 5-8 (indices 4-7) shift one of those degrees so the second half of
// the phrase is not a copy of the first. Lengths stay under the long-note cap.
void pushJackinVariationBack (Lane& lane, int bars)
{
    if (lane.count <= 0 || bars <= 0)
        return;
    // The front of every bar has to keep sounding, or the answer bar shares
    // those rests and call-and-answer fails (more than 20% of 16ths match).
    // Slot 3 length 2 covers the beat, slot 5 length 4 holds across the next
    // kick, and the tail holds the next downbeat. The seed still chooses the
    // fill and which second-half notes step, so two rolls are not one
    // transposition: a shift of every note would be, and the skeleton check
    // tries every transposition.
    const std::uint32_t h = lane.seed ? lane.seed : 1u;
    const int earlySlot = 3;
    const int midSlot = 5;
    const int midLen = 4;
    const int fillSlot = 9 + (int) ((h >> 6) % 4u);           // 9..12, kick 12 skipped below
    // Two scale steps from the seed. A single added interval on every note
    // is a transposition, which the skeleton search ignores; these two are not.
    static const int kScale[] = { 1, 2, 3, 5, 7, 8, 10 };
    const int deg1 = kScale[h % 7u];
    int deg2 = kScale[(h / 7u) % 7u];
    if (deg2 == deg1)
        deg2 = kScale[(h / 7u + 3u) % 7u];

    Note src[kMaxNotes];
    const int n = std::min (lane.count, kMaxNotes);
    for (int i = 0; i < n; ++i)
        src[i] = lane.notes[i];

    Note out[kMaxNotes];
    int w = 0;
    auto push = [&] (Note nn)
    {
        if (w >= kMaxNotes || nn.length < 0.5f)
            return;
        const int st = (int) std::lround (nn.start);
        for (int i = 0; i < w; ++i)
            if ((int) std::lround (out[i].start) == st)
                return;
        out[w++] = nn;
    };

    bool backUsed[8][16] {};
    for (int b = 0; b < bars && b < 8; ++b)
    {
        const int origin = b * 16;
        for (int i = 0; i < n; ++i)
        {
            const int st = (int) std::lround (src[i].start);
            const int slot = st - origin;
            if (slot >= 8 && slot < 16)
                backUsed[b][slot] = true;
        }
    }

    // Three back-half orders whose gaps are not a one- or two-16th shift of
    // each other, so the skeleton search (shift -2..+2, every transposition)
    // cannot line two seeds up. The partner bar still skips slots this bar
    // took, which keeps the answer off the call.
    static const int kPrefer0[] = { 9, 11, 15, 10, 13, 14 };
    static const int kPrefer1[] = { 9, 13, 15, 10, 11, 14 };
    static const int kPrefer2[] = { 10, 15, 13, 9, 11, 14 };
    const int* kPrefer = (h % 3u) == 0 ? kPrefer0 : ((h % 3u) == 1 ? kPrefer1 : kPrefer2);
    for (int b = 0; b < bars && b < 8; ++b)
    {
        const int origin = b * 16;
        const int partner = b ^ 1;
        Note kept[16];
        int nk = 0;
        for (int i = 0; i < n && nk < 16; ++i)
        {
            const int st = (int) std::lround (src[i].start);
            const int slot = st - origin;
            if (st < origin || slot >= 16)
                continue;
            if (slot >= 8)
            {
                kept[nk++] = src[i];
                continue;
            }
            int dest = -1;
            for (int pi = 0; pi < 6; ++pi)
            {
                const int s = kPrefer[pi];
                if (backUsed[b][s] || kickSlot (s))
                    continue;
                if (partner < 8 && partner < bars && backUsed[partner][s])
                    continue;
                dest = s;
                break;
            }
            if (dest < 0)
                continue;
            Note moved = src[i];
            moved.start = (float) (origin + dest);
            moved.length = 2.f;
            backUsed[b][dest] = true;
            kept[nk++] = moved;
        }

        std::sort (kept, kept + nk, [] (const Note& a, const Note& c) { return a.start < c.start; });
        const bool secondHalf = b >= 4;
        // A tail on slot 14 or 15 has to be the last note of the bar, so the
        // next bar's downbeat is a sustain instead of a shared rest.
        if (b + 1 < bars)
        {
            int tailAt = -1;
            for (int i = 0; i < nk; ++i)
            {
                const int slot = (int) std::lround (kept[i].start) - origin;
                if (slot >= 14)
                    tailAt = i;
            }
            if (tailAt < 0 && nk < 16)
            {
                Note tail;
                tail.start = (float) (origin + 14);
                tail.nChord = 1;
                tail.vel = 112;
                if (nk > 0 && (int) std::lround (kept[nk - 1].start) - origin > 14)
                    kept[nk - 1].start = (float) (origin + 14);
                else
                    kept[nk++] = tail;
                tailAt = nk - 1;
                for (int i = 0; i < nk; ++i)
                    if ((int) std::lround (kept[i].start) - origin > 14 && i != tailAt)
                        kept[i].start = (float) (origin + 13);
            }
            if (tailAt >= 0)
            {
                kept[tailAt].start = (float) (origin + std::min (15, (int) std::lround (kept[tailAt].start) - origin));
                const int st = (int) std::lround (kept[tailAt].start);
                // End on the next bar's slot 3. Length stays at 4 or 5, under the long-note cap.
                kept[tailAt].length = std::max (1.f, (float) ((b + 1) * 16 + 3 - st));
                kept[tailAt].chord[0] = (std::int8_t) ((b & 1) ? deg1 : 0);
                kept[tailAt].vel = 112;
            }
        }
        std::sort (kept, kept + nk, [] (const Note& a, const Note& c) { return a.start < c.start; });
        for (int i = 0; i < nk; ++i)
        {
            const int st = (int) std::lround (kept[i].start);
            const int slot = st - origin;
            const bool tail = b + 1 < bars && slot >= 14;
            if (! tail)
            {
                const int next = (i + 1 < nk) ? (int) std::lround (kept[i + 1].start) : origin + 16;
                kept[i].length = (float) std::min (2, std::max (1, next - st));
            }
            // Spread the bar across the seed's degrees. Adjacent bars rotate
            // so the same slot does not sound the same pitch, and bars 5-8
            // rotate once more so they are not bars 1-4.
            if (! tail)
            {
                const int rot = (b & 1) + (secondHalf ? 1 : 0);
                const int degs[3] = { 0, deg1, deg2 };
                kept[i].chord[0] = (std::int8_t) degs[(i + rot) % 3];
            }
            push (kept[i]);
        }

        // Front of this bar: one off-beat at slot 3 and one at slot 6, degrees
        // from this bar's own notes so the call and the answer do not match.
        // The previous bar's tail already covers slots 0-2 when it could.
        auto cover = [&] (int slot, float len, int semi, int vel)
        {
            if (slot < 0 || slot > 15 || kickSlot (slot))
                return;
            Note nn;
            nn.start = (float) (origin + slot);
            nn.length = len;
            nn.chord[0] = (std::int8_t) (scalePc (Gaskin, semi) ? semi : snapScale (Gaskin, semi));
            nn.nChord = 1;
            nn.vel = (std::uint8_t) vel;
            push (nn);
        };
        // Second half flips which bar carries the call. The two degrees are
        // the seed's interval, not a fixed flat-2, so two rolls do not share
        // one transposed call.
        const int parity = (b & 1) ^ (secondHalf ? 1 : 0);
        cover (earlySlot, 2.f, parity ? deg1 : 0, 108);
        // Reaches slot 8 so the kick is a sustain of this bar's pitch, not a
        // rest shared with the answer. Length stays under 6.
        cover (midSlot, (float) midLen, parity ? 0 : deg2, 104);

        // A one-16th fill in the back half of bars 5-8. The last bar's trim
        // keeps anything before step 12.
        if (secondHalf)
        {
            const int fillSlots[] = { fillSlot, 10, 13, 11, 9 };
            for (int s : fillSlots)
            {
                if (kickSlot (s))
                    continue;
                bool taken = false;
                for (int i = 0; i < w; ++i)
                {
                    const int st = (int) std::lround (out[i].start);
                    if (st / 16 != b)
                        continue;
                    const int slot = st - origin;
                    const int en = slot + std::max (1, (int) std::lround (out[i].length));
                    if (s >= slot && s < en)
                        taken = true;
                }
                if (taken)
                    continue;
                cover (s, 1.f, parity ? deg2 : deg1, 100);
                break;
            }
        }
    }

    lane.count = w;
    for (int i = 0; i < w; ++i)
        lane.notes[i] = out[i];
}

// 0.75 at BUSY 0, 0.99 at the shipped 0.40, 1.35 at BUSY 1.
float busyFactor (float busy) noexcept
{
    return 0.75f + 0.60f * std::clamp (busy, 0.f, 1.f);
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
        const int keep = std::min (n, target);
        for (int k = keep; k < n; ++k)
        {
            const int st = (int) std::lround (lane.notes[idx[k]].start);
            if (st >= 0 && st / 16 == bar && st / 16 < 8)
                used[bar][st % 16] = false;
        }
        for (int k = 0; k < keep && w < kMaxNotes; ++k)
            out[w++] = lane.notes[idx[k]];

        const float fillLen = dialect == Pop ? 1.1f : dialect == Long ? 2.f : dialect == Gaskin ? 1.5f : 1.5f;
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
    // About half of mid-bounce rolls. The knob's extremes stay on the bounce grid.
    return (((seed ? seed : 1u) * 0x27D4EB2Du) % 100u) < 50u;
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

void fitRootShare (Lane& lane, float lo, float hi, std::uint32_t seed)
{
    static const int colours[] = { 3, 7, 10, 8, 5, 2 };
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
        lane.notes[best].chord[0] = (std::int8_t) colours[(seed + (std::uint32_t) guard) % 6u];
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

void densifyDeepDark (Lane& lane, int bars, std::uint32_t seed)
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
    static const int slots[] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 };
    static const int degs[] = { 3, 7, 10, 0, 8, 5, 3, 10 };
    const int last = std::min (bars, 8) - 1;
    for (int bar = 0; bar < bars && bar < 8; ++bar)
    {
        int have = 0;
        for (int s = 0; s < 16; ++s)
            if (used[bar][s])
                ++have;
        int target = 6 + (int) (((seed >> (bar & 3)) + (std::uint32_t) bar) % 4u);
        if (bar == last)
            target = std::min (target, 6);
        const int spin = (int) ((seed + (std::uint32_t) bar * 3u) % 12u);
        int guard = 0;
        while (have < target && lane.count < kMaxNotes && guard++ < 16)
        {
            int slot = -1;
            for (int t = 0; t < 12; ++t)
            {
                const int s = slots[(spin + t) % 12];
                if (bar == last && s >= 12)
                    continue;
                if (! used[bar][s])
                {
                    slot = s;
                    break;
                }
            }
            if (slot < 0)
                break;
            used[bar][slot] = true;
            Note n;
            n.start = (float) (bar * 16 + slot);
            n.length = 1.6f;
            n.chord[0] = (std::int8_t) degs[(seed + (std::uint32_t) have + (std::uint32_t) bar) % 8u];
            n.nChord = 1;
            n.vel = 104;
            for (int i = 0; i < lane.count; ++i)
                if (lane.notes[i].start < n.start && lane.notes[i].start + lane.notes[i].length > n.start)
                    lane.notes[i].length = std::max (0.5f, n.start - lane.notes[i].start);
            lane.notes[lane.count++] = n;
            ++have;
        }
    }
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
}

void moveJackinNote (Lane& lane, int bars, int parity, int want, std::uint32_t salt)
{
    static const int pool[] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13 };
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
        int dest = (want >= 0 && want < 16 && ! kickSlot (want) && ! used[want]) ? want : -1;
        if (dest < 0)
        {
            for (int t = 0; t < 10; ++t)
            {
                const int s = pool[((want < 0 ? -want : want) + t + (int) (salt % 7u)) % 10];
                if (! used[s])
                {
                    dest = s;
                    break;
                }
            }
        }
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
    static const int pool[] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13 };
    auto pick = [&] (int shift) { return pool[((seed >> shift) ^ seed) % 10u]; };
    moveJackinNote (lane, bars, 0, pick (0), seed);
    moveJackinNote (lane, bars, 0, pick (4), seed + 1u);
    moveJackinNote (lane, bars, 1, pick (8), seed + 2u);
    moveJackinNote (lane, bars, 1, pick (12), seed + 3u);
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
}

void leanJackinDotted (Lane& lane, int bars, std::uint32_t seed)
{
    static const int dotted[] = { 3, 6, 9, 15 };
    const int rot = (int) (seed % 4u);
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
            int dest = -1;
            for (int t = 0; t < 4; ++t)
            {
                const int s = dotted[(rot + (b & 1) + t) % 4];
                if (! used[s])
                {
                    dest = s;
                    break;
                }
            }
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

void ensureColour (Lane& lane)
{
    const int need[] = { 3, 7, 10 };
    for (int want : need)
    {
        bool has = false;
        for (int i = 0; i < lane.count; ++i)
            if (pcOf (lane.notes[i].chord[0]) == want)
                has = true;
        if (has)
            continue;
        for (int i = lane.count - 1; i >= 1; --i)
        {
            if (pcOf (lane.notes[i].chord[0]) != 0)
                continue;
            lane.notes[i].chord[0] = (std::int8_t) want;
            break;
        }
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

void styleJackinBass (Lane& lane, int bars, std::uint32_t seed)
{
    const int flavour = jackinFlavour (seed);
    diversifyJackin (lane, bars, seed);
    if (flavour == 0)
        leanJackinDotted (lane, bars, seed);
    ensureColour (lane);
    if (flavour == 1)
        paintFlat2 (lane);
    fitRootShare (lane, 0.40f, 0.72f, seed);
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

// Local Dub 'Reaction': a root on the 2nd and 4th 16th of the bar.
void avoidReaction (Lane& lane)
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
            lane.notes[at3].chord[0] = (std::int8_t) ((b & 1) ? 3 : 10);
    }
}

void finishBass (Lane& lane)
{
    enforceMonophonic (lane);
    avoidReaction (lane);
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
void paintOctaveShare (Lane& lane, std::uint32_t seed)
{
    if (lane.count < 2)
        return;
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    const int moves = std::max (1, lane.count - 1);
    const int want = std::clamp ((int) std::lround ((float) moves * 0.20f), 1, moves);
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

// Pulse is a steady off-kick grid with notes long enough for the 1.8-3.2 band.
// The bounce shapes sit on adjacent 16ths, so the same pitches are re-spaced.
void makePulse (Lane& lane, int bars, std::uint32_t seed)
{
    static const int grids[][4] = {
        { 1, 5, 9, 13 },
        { 2, 6, 10, 14 },
        { 3, 6, 10, 14 },
        { 1, 6, 10, 15 },
        { 3, 7, 11, 14 },
        { 2, 5, 9, 14 },
    };
    const int grid = (int) ((seed ? seed : 1u) % 6u);
    const int odd = (grid + 2) % 6;
    Note out[kMaxNotes];
    int w = 0;
    for (int bar = 0; bar < bars && bar < 8; ++bar)
    {
        int src[8];
        int n = 0;
        for (int i = 0; i < lane.count && n < 8; ++i)
            if ((int) std::floor (lane.notes[i].start / 16.f) == bar)
                src[n++] = i;
        const int turn[4] = { 1, 5, 9, 11 };
        const int* slots = bar == std::min (bars, 8) - 1
                               ? turn
                               : (((bar & 1) != 0) ? grids[odd] : grids[grid]);
        for (int k = 0; k < 4 && w < kMaxNotes; ++k)
        {
            Note nte;
            if (n > 0)
                nte = lane.notes[src[k % n]];
            nte.start = (float) (bar * 16 + slots[k]);
            nte.length = 2.2f;
            nte.nChord = 1;
            if (nte.chord[0] > 14)
                nte.chord[0] = (std::int8_t) pcOf (nte.chord[0]);
            out[w++] = nte;
        }
    }
    lane.count = w;
    for (int i = 0; i < w; ++i)
        lane.notes[i] = out[i];
}

void softenFifths (Lane& lane, std::uint32_t seed)
{
    static const int alt[] = { 3, 10, 2, 5 };
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
            lane.notes[i].chord[0] = (std::int8_t) alt[(seed + (std::uint32_t) guard + (std::uint32_t) i) % 4u];
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
void paintPhraseVariety (Lane& lane, int dialect, std::uint32_t seed)
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
    paintOctaveShare (lane, seed);
}

// Bar 4 answers bar 2 on about three fifths of rolls. The back half of bar 4
// moves, so the 8-bar loop is not a photocopy of the opening pair. The other
// two fifths keep the copy.
void differBar4 (Lane& lane, int bars, std::uint32_t seed)
{
    if (bars < 4)
        return;
    const bool answer = (((seed ? seed : 1u) * 0xC2B2AE35u) % 100u) < 60u;
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

    const int spin = (int) ((seed >> 4) & 7u);
    int dest = -1;
    for (int t = 0; t < 8 && dest < 0; ++t)
    {
        const int s = 8 + ((spin + t) % 8);
        if (! dst[s] && ! kickSlot (s))
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

// LONG / GASKIN bass is the phrase grammar (PhraseBank): half-bar cells from
// resources/phrase_bank.json, joined and re-pitched per seed. Nothing here is
// a stored phrase. Bar 8 still opens a four-16th gap so the loop can turn
// around. DARK at either end retints degrees. BOUNCE above 0.6 still shoves
// even slots, which is what the knob audit compares. The cell mix stays at
// the shipped BUSY 0.40; applyBusyDensity is what the knob actually scales.
void lockFromBook (Lane& lane, int dialect, std::uint32_t seed, int bars, float dark, float bounce, float busy)
{
    phrase::Style style;
    if (! phrase::styleForDialect (dialect, style))
        return;

    const std::uint32_t kept = lane.seed ? lane.seed : seed;
    phrase::PhraseSettings ps;
    ps.busy = 0.40f;
    ps.bounce = bounce;
    ps.dark = dark;
    ps.bars = 2;
    const phrase::Phrase phrase = phrase::makeBassPhrase (style, seed, ps);
    phrase::writePhrase (phrase, lane, bars);
    lane.seed = kept;

    if (bounce > 0.6f)
    {
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            const int slot = st % 16;
            if ((slot & 1) == 0 && slot < 15 && ! kickSlot (slot + 1))
                lane.notes[i].start = (float) (st + 1);
        }
    }

    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < 0 || kickSlot (st % 16))
            continue;
        if (w > 0 && (int) std::lround (lane.notes[w - 1].start) == st)
            continue;
        int semi = lane.notes[i].chord[0];
        if (dark <= 0.2f)
            semi = (w & 1) ? 7 : 0;
        else if (dark >= 0.8f)
            semi = (w & 1) ? 3 : ((i + 1 == lane.count) ? 1 : 0);
        if (! scalePc (dialect, semi))
            semi = snapScale (dialect, semi);
        lane.notes[w] = lane.notes[i];
        lane.notes[w].chord[0] = (std::int8_t) semi;
        lane.notes[w].start = (float) st;
        ++w;
    }
    lane.count = w;

    // JACKIN only. DEEP's even bar already retails its own back half.
    if (dialect == Gaskin)
        pushJackinVariationBack (lane, bars);

    // Last bar of an 8-bar loop: drop the latest two attacks and stop at
    // step 12, so the bar is not a copy and the tail is a real rest.
    const int last = std::min (bars, 8) - 1;
    if (last >= 0)
    {
        const float origin = (float) (last * 16);
        const float cut = origin + 12.f;
        const float barEnd = origin + 16.f;
        int inBar[32];
        int nIn = 0;
        for (int i = 0; i < lane.count && nIn < 32; ++i)
            if (lane.notes[i].start >= origin && lane.notes[i].start < barEnd)
                inBar[nIn++] = i;
        bool drop[kMaxNotes] {};
        const int nDrop = nIn > 2 ? 2 : 0;
        for (int d = 0; d < nDrop; ++d)
            drop[inBar[nIn - 1 - d]] = true;
        int keptN = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            if (drop[i])
                continue;
            Note n = lane.notes[i];
            if (n.start >= cut && n.start < barEnd)
                continue;
            if (n.start < cut && n.start + n.length > cut)
                n.length = cut - n.start;
            if (n.length < 0.5f)
                continue;
            lane.notes[keptN++] = n;
        }
        lane.count = keptN;
        int left = 0;
        for (int i = 0; i < lane.count; ++i)
            if (lane.notes[i].start >= origin && lane.notes[i].start < barEnd)
                ++left;
        if (left < 1)
        {
            // A wiped bar is a hard fail. Put a short in-scale root back.
            if (lane.count < kMaxNotes)
            {
                Note n;
                n.start = origin + 2.f;
                n.length = 2.f;
                n.chord[0] = 0;
                n.vel = 112;
                lane.notes[lane.count++] = n;
            }
        }
    }
    if (dialect == Gaskin)
        styleJackinBass (lane, bars, seed);
    else if (dialect == Long && deepIsDark (dark, seed))
        densifyDeepDark (lane, bars, seed);
    applyBusyDensity (lane, dialect, bars, busy, seed);
    if (dialect == Long)
        fitRootShare (lane, 0.42f, 0.68f, seed);
    differBar4 (lane, bars, seed);
    finishBass (lane);
}

// Repeat a 2-bar phrase so the loop matches itself. Bar rhythms come from a
// shift-resistant codebook and the pitch contour from the seed, so two rolls
// do not share a bass skeleton after a transposition or a two-16th nudge.
// DARK at either end still retints the degrees. BOUNCE above 0.6 still
// shoves even slots, which is what the knob audit compares. BUSY scales how
// many of those attacks survive, around the shipped 0.40 phrase.
void lockPhrase (Lane& lane, int dialect, std::uint32_t seed, int bars, float dark, float bounce, float busy)
{
    if (bars < 4 || lane.count <= 0)
        return;
    if (dialect == Long || dialect == Gaskin)
    {
        lockFromBook (lane, dialect, seed, bars, dark, bounce, busy);
        return;
    }

    struct Shape { int n; int slot[5]; };
    // Off-kick 5-note bars. Pairwise onset Jaccard stays under 0.50 across a
    // shift of two 16ths, which is the skeleton window the scorer searches.
    static const Shape kPhrase[] = {
        { 5, { 5, 6, 7, 9, 10 } },
        { 5, { 5, 7, 9, 13, 14 } },
        { 5, { 1, 3, 6, 9, 15 } },
        { 5, { 5, 9, 10, 13, 15 } },
        { 5, { 1, 2, 5, 11, 15 } },
        { 5, { 1, 9, 13, 14, 15 } },
        { 5, { 3, 5, 9, 10, 14 } },
        { 5, { 1, 2, 6, 9, 11 } },
        { 5, { 3, 5, 6, 13, 14 } },
        { 5, { 1, 2, 3, 9, 14 } },
        { 5, { 1, 5, 9, 11, 14 } },
        { 5, { 5, 11, 13, 14, 15 } },
        { 5, { 1, 2, 5, 6, 7 } },
    };
    // LONG needs a gap wide enough that a 2.6-16th note is not cut to a blip.
    static const Shape kLongPhrase[] = {
        { 4, { 2, 5, 11, 13 } },
        { 4, { 1, 3, 7, 14 } },
        { 4, { 2, 7, 9, 13 } },
        { 4, { 3, 5, 11, 14 } },
        { 4, { 3, 6, 10, 13 } },
        { 4, { 2, 5, 9, 11 } },
        { 4, { 1, 6, 9, 11 } },
        { 4, { 1, 3, 5, 10 } },
    };
    // GASKIN: at least three dotted off-kicks (3, 6, 9, 15).
    static const Shape kGaskinPhrase[] = {
        { 5, { 1, 3, 6, 14, 15 } },
        { 5, { 6, 7, 9, 11, 15 } },
        { 5, { 3, 6, 9, 10, 11 } },
        { 5, { 2, 6, 9, 13, 15 } },
        { 5, { 3, 9, 13, 14, 15 } },
        { 5, { 3, 5, 6, 9, 15 } },
        { 5, { 3, 6, 10, 13, 15 } },
        { 5, { 2, 3, 9, 11, 15 } },
        { 5, { 1, 3, 9, 10, 15 } },
        { 5, { 1, 2, 3, 6, 9 } },
    };
    // Three roots, the rest in scale, and no two contours agree in more than
    // two slots after a transposition. The root stays the tonic.
    static const int kDeg[][5] = {
        { 9, 0, 0, 10, 0 }, { 1, 0, 8, 0, 0 }, { 0, 10, 0, 0, 7 }, { 5, 5, 0, 0, 0 },
        { 2, 0, 0, 0, 2 }, { 0, 1, 0, 2, 0 }, { 0, 9, 9, 0, 0 }, { 0, 0, 2, 0, 1 },
        { 0, 0, 0, 8, 9 }, { 0, 0, 1, 1, 0 },
    };
    static const int kLongDeg[][4] = {
        { 3, 0, 0, 0 }, { 0, 2, 0, 0 }, { 0, 0, 5, 0 }, { 0, 0, 0, 2 },
    };
    static const int kGaskDeg[][5] = {
        { 8, 1, 0, 0, 0 }, { 0, 2, 1, 0, 0 }, { 10, 0, 0, 1, 0 }, { 2, 0, 0, 0, 1 },
        { 1, 0, 7, 0, 0 },
    };

    const std::uint32_t h = seed * 0x9E3779B9u;
    const bool gaskin = dialect == Gaskin;
    const bool longNote = dialect == Long;
    const Shape* book = gaskin ? kGaskinPhrase : (longNote ? kLongPhrase : kPhrase);
    const int nBook = gaskin ? 10 : (longNote ? 8 : 13);
    const int i0 = (int) (h % (std::uint32_t) nBook);
    int i1 = (int) ((h >> 8) % (std::uint32_t) nBook);
    if (i1 == i0)
        i1 = (i1 + 1) % nBook;
    const int nDeg = gaskin ? 5 : (longNote ? 4 : 10);
    const int c0 = (int) ((h >> 16) % (std::uint32_t) nDeg);
    int c1 = (int) ((h >> 24) % (std::uint32_t) nDeg);
    if (c1 == c0)
        c1 = (c1 + 3) % nDeg;

    auto degree = [&] (int which, int index, int count) -> int
    {
        int semi = 0;
        if (gaskin)
            semi = kGaskDeg[which][index % 5];
        else if (longNote)
            semi = kLongDeg[which][index % 4];
        else
            semi = kDeg[which][index % 5];
        if (dark <= 0.2f)
            semi = (index == count - 1) ? 7 : ((index & 1) ? 7 : 0);
        else if (dark >= 0.8f)
            semi = (index == count - 1) ? 1 : ((index & 1) ? 3 : 0);
        return semi;
    };

    auto prepSlots = [&] (const Shape& shape, int* out) -> int
    {
        int n = shape.n;
        for (int i = 0; i < n; ++i)
            out[i] = shape.slot[i];
        if (bounce > 0.6f)
        {
            for (int i = 0; i < n; ++i)
                if ((out[i] & 1) == 0 && out[i] < 15 && ! kickSlot (out[i] + 1))
                    out[i] += 1;
        }
        std::sort (out, out + n);
        int w = 0;
        for (int i = 0; i < n; ++i)
        {
            if (out[i] < 0 || out[i] > 15 || kickSlot (out[i]))
                continue;
            if (w > 0 && out[w - 1] == out[i])
                continue;
            out[w++] = out[i];
        }
        return w;
    };

    const std::uint32_t keptSeed = lane.seed ? lane.seed : seed;
    lane.count = 0;
    lane.seed = keptSeed;

    auto place = [&] (int bar, const int* slots, int n, int which)
    {
        for (int i = 0; i < n; ++i)
        {
            const int nxt = (i + 1 < n) ? slots[i + 1] : 16;
            const float gap = (float) (nxt - slots[i]);
            float len = 1.7f;
            if (dialect == Pop)
                len = 1.1f;
            else if (longNote)
                len = std::min (2.6f, gap);
            else if (gaskin)
                len = std::min (2.0f, gap);
            else
                len = std::min (1.7f, gap);
            if (len < 0.5f)
                continue;
            Ev e;
            e.start = (float) (bar * 16 + slots[i]);
            e.len = len;
            e.nChord = 1;
            e.chord[0] = (std::int8_t) degree (which, i, n);
            e.vel = (i & 1) ? 80 : 112;
            pushEv (lane, e, bars);
        }
    };

    int front[5];
    const int nFront = prepSlots (book[i0], front);
    if (nFront <= 0)
        return;
    place (0, front, nFront, c0);

    if (gaskin)
    {
        bool sound[16] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const int a = (int) std::lround (lane.notes[i].start);
            const int b = (int) std::lround (lane.notes[i].start + lane.notes[i].length);
            for (int k = a; k < b && k < 16; ++k)
                if (k >= 0)
                    sound[k] = true;
        }
        const int prefer[] = { 3, 6, 9, 15, 0, 12, 1, 2, 5, 7, 10, 11, 13, 14 };
        int ans[6];
        int nAns = 0;
        bool cover[16] {};
        for (int s : prefer)
        {
            if (nAns >= 4)
                break;
            if (s < 0 || s > 15 || sound[s] || cover[s])
                continue;
            ans[nAns++] = s;
            const int ln = s <= 14 ? 2 : 1;
            for (int k = s; k < s + ln && k < 16; ++k)
                cover[k] = true;
        }
        std::sort (ans, ans + nAns);
        int kickAtt = 0;
        for (int i = 0; i < nAns; ++i)
            if (kickSlot (ans[i]))
                ++kickAtt;
        while (nAns > 1 && (float) kickAtt / (float) (nFront + nAns) > 0.35f)
        {
            int drop = -1;
            for (int i = nAns - 1; i >= 0; --i)
                if (kickSlot (ans[i]))
                    drop = i;
            if (drop < 0)
                break;
            for (int i = drop; i + 1 < nAns; ++i)
                ans[i] = ans[i + 1];
            --nAns;
            --kickAtt;
        }
        // A shared rest in the back half is a match the call/answer check counts,
        // and it sits on the wrong side of the variation check. Sound it.
        int added = 0;
        for (int s = 8; s < 16 && nAns < 6 && added < 2; ++s)
        {
            if (sound[s] || cover[s] || kickSlot (s))
                continue;
            ans[nAns++] = s;
            cover[s] = true;
            ++added;
        }
        std::sort (ans, ans + nAns);
        place (1, ans, nAns, c1);
    }
    else
    {
        int other[5];
        const int nOther = prepSlots (book[i1], other);
        int mixed[8];
        int nMix = 0;
        for (int i = 0; i < nFront && nMix < 8; ++i)
            if (front[i] < 8)
                mixed[nMix++] = front[i];
        for (int i = 0; i < nOther && nMix < 8; ++i)
        {
            if (other[i] < 8 || kickSlot (other[i]))
                continue;
            bool clash = false;
            for (int k = 0; k < nMix; ++k)
                if (mixed[k] == other[i])
                    clash = true;
            if (! clash)
                mixed[nMix++] = other[i];
        }
        bool backDiff = false;
        for (int i = 0; i < nMix; ++i)
        {
            if (mixed[i] < 8)
                continue;
            bool inFront = false;
            for (int k = 0; k < nFront; ++k)
                if (front[k] == mixed[i])
                    inFront = true;
            if (! inFront)
                backDiff = true;
        }
        if (! backDiff && nMix < 8)
        {
            const int extra = 9 + (int) ((h >> 4) & 3u) * 2;
            if (extra < 16 && ! kickSlot (extra))
                mixed[nMix++] = extra;
        }
        std::sort (mixed, mixed + nMix);
        place (1, mixed, nMix, c0);
        // Front slots keep bar 0's degree so the token diff sits in the back half.
        for (int i = 0; i < lane.count; ++i)
        {
            if (lane.notes[i].start < 16.f || lane.notes[i].start >= 24.f)
                continue;
            const int slot = (int) std::lround (lane.notes[i].start) - 16;
            for (int k = 0; k < lane.count; ++k)
            {
                if (lane.notes[k].start >= 16.f)
                    break;
                if ((int) std::lround (lane.notes[k].start) == slot)
                    lane.notes[i].chord[0] = lane.notes[k].chord[0];
            }
        }
    }

    const int baseCount = lane.count;
    auto barOf = [] (float start) { return (int) std::floor (start / 16.f + 1.0e-4f); };
    for (int bar = 2; bar < bars && bar < 8; ++bar)
    {
        const int srcBar = bar & 1;
        for (int i = 0; i < baseCount; ++i)
        {
            if (barOf (lane.notes[i].start) != srcBar)
                continue;
            Note n = lane.notes[i];
            n.start += (float) ((bar - srcBar) * 16);
            if (lane.count < kMaxNotes && n.length > 0.05f)
                lane.notes[lane.count++] = n;
        }
    }

    const int last = std::min (bars, 8) - 1;
    const float bar0 = (float) (last * 16);
    const float cut = bar0 + 12.f;
    int idx[16];
    int nLast = 0;
    for (int i = 0; i < lane.count && nLast < 16; ++i)
        if (lane.notes[i].start >= bar0 && lane.notes[i].start < bar0 + 16.f)
            idx[nLast++] = i;
    std::sort (idx, idx + nLast, [&] (int a, int b) { return lane.notes[a].start > lane.notes[b].start; });
    int drop = std::min (2, std::max (0, nLast - 2));
    bool kill[kMaxNotes] {};
    for (int i = 0; i < drop; ++i)
        kill[idx[i]] = true;
    for (int i = 0; i < nLast; ++i)
        if (lane.notes[idx[i]].start >= cut - 0.05f && nLast - drop > 1)
            kill[idx[i]] = true;
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        if (kill[i])
            continue;
        Note n = lane.notes[i];
        if (n.start >= bar0 && n.start + n.length > cut)
            n.length = cut - n.start;
        if (n.length <= 0.05f)
            continue;
        lane.notes[w++] = n;
    }
    lane.count = w;

    applyBusyDensity (lane, dialect, bars, busy, seed);
    if (dialect != Pop)
    {
        paintPhraseVariety (lane, dialect, seed);
        differBar4 (lane, bars, seed);
        finishBass (lane);
        return;
    }
    // Bounce root share has to stay inside 30-64% of bass time. Notes are
    // equal length, so attack share is the time share. A bar over 64% tonic
    // hands the extra attacks to a colour and stops once it is inside the band.
    static const int kColour[] = { 3, 7, 10, 2, 5 };
    for (int b = 0; b < bars && b < 8; ++b)
    {
        int rootIdx[12];
        int n = 0;
        int roots = 0;
        for (int i = 0; i < lane.count && n < 12; ++i)
        {
            if ((int) std::floor (lane.notes[i].start / 16.f) != b)
                continue;
            rootIdx[n++] = i;
            if (((lane.notes[i].chord[0] % 12) + 12) % 12 == 0)
                ++roots;
        }
        for (int k = n - 1; k >= 0 && n > 0 && roots * 100 > n * 64; --k)
        {
            Note& note = lane.notes[rootIdx[k]];
            if ((((note.chord[0] % 12) + 12) % 12) != 0)
                continue;
            note.chord[0] = (std::int8_t) kColour[(seed + (std::uint32_t) b + (std::uint32_t) k) % 5u];
            --roots;
        }
    }
    softenFifths (lane, seed);
    if (popIsPulse (bounce, seed))
    {
        makePulse (lane, bars, seed);
        // The pulse grid replaces the phrase, so BUSY has to scale it again.
        applyBusyDensity (lane, dialect, bars, busy, seed);
    }
    paintPhraseVariety (lane, dialect, seed);
    differBar4 (lane, bars, seed);
    finishBass (lane);
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
    const std::uint32_t seed = lane.seed ? lane.seed : 1u;
    clearLane (lane, seed);
    int per = s.dialect == Pop ? 8 : s.dialect == Gaskin ? 6 : s.dialect == Long ? 3 : (s.stab > 0.66f ? 2 : 1);
    if (s.styleIntensity <= 0.25f)
        per = std::min (per, 1);
    else if (s.styleIntensity >= 0.75f)
        per = std::min (12, per + 2);
    // Fills follow BUSY. 0.40 keeps the count above. Low BUSY drops a hit and,
    // on a sparse line, skips the odd bars. High BUSY adds two hits a bar.
    if (s.busy > 0.75f)
        per = std::min (12, per + 2);
    else if (s.busy < 0.25f)
        per = std::max (1, per - 1);

    const int flavour = s.dialect == Gaskin ? jackinFlavour (seed) : -1;
    float len = s.dialect == Pop ? 1.f : 2.f;
    if (s.dialect == Gaskin)
        len = flavour == 2 ? 6.5f : (flavour == 3 ? 1.f : 1.9f);

    // One chord per half of a 2-bar figure, drawn for this roll.
    static const int book[][3] = {
        { 3, 10, 15 }, { 7, 10, 19 }, { 3, 7, 14 }, { 10, 14, 17 },
        { 0, 7, 12 }, { 3, 10, 17 }, { 5, 10, 15 }, { 7, 12, 19 },
    };
    int voice[2] = { (int) (seed % 8u), (int) ((seed / 8u) % 8u) };
    if (voice[1] == voice[0])
        voice[1] = (voice[1] + 3) % 8;

    int order[2][12];
    for (int par = 0; par < 2; ++par)
    {
        const int spin = (int) ((seed >> (8 + par * 4)) % 12u);
        static const int off[] = { 3, 7, 9, 14, 2, 6, 10, 11, 5, 1, 13, 15 };
        static const int early[] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 };
        static const int ea[] = { 3, 7, 9, 1, 5, 11, 13, 15, 14, 2, 6, 10 };
        const int* src = flavour == 2 ? early : (flavour == 3 ? ea : off);
        const int twist = flavour == 2 ? par * 3 : (flavour == 3 ? par : par * 5);
        for (int t = 0; t < 12; ++t)
            order[par][t] = src[(spin + t + twist) % 12];
    }

    const bool skipOdd = s.busy < 0.25f && per <= 2;
    for (int bar = 0; bar < s.bars && bar < 8; ++bar)
    {
        if (skipOdd && (bar & 1) != 0)
            continue;
        const int par = bar & 1;
        const int* voi = book[voice[par]];
        bool taken[16] {};
        int sustain[12];
        int onsetSlots[12];
        int rest[12];
        int nSus = 0;
        int nOn = 0;
        int nRest = 0;
        for (int t = 0; t < 12; ++t)
        {
            const int slot = order[par][t];
            if (slot < 0 || slot > 15 || kickSlot (slot))
                continue;
            const int abs = bar * 16 + slot;
            if (bassOnset (bass, abs))
                onsetSlots[nOn++] = slot;
            else if (bassSounds (bass, abs))
                sustain[nSus++] = slot;
            else
                rest[nRest++] = slot;
        }

        int placed = 0;
        int placedIdx[12];
        bool placedRest[12] {};
        auto place = [&] (int slot, bool isRest)
        {
            if (placed >= per || taken[slot])
                return;
            const int abs = bar * 16 + slot;
            float hold = len;
            const float barEnd = (float) ((bar + 1) * 16);
            if ((float) abs + hold > barEnd)
                hold = barEnd - (float) abs;
            if (hold < 0.5f)
                return;
            Ev e;
            e.start = (float) abs;
            e.len = hold;
            e.nChord = 3;
            e.vel = 98;
            for (int c = 0; c < 3; ++c)
                e.chord[c] = (std::int8_t) voi[c];
            if (! pushEv (lane, e, s.bars))
                return;
            taken[slot] = true;
            if (placed < 12)
            {
                placedIdx[placed] = lane.count - 1;
                placedRest[placed] = isRest;
            }
            ++placed;
            if (bassOnset (bass, abs))
                noteBreak (breaks, breaksN, "stab");
        };
        // Most stabs sit on the bass. A rest is a light preference, about 8%.
        for (int i = 0; i < nSus; ++i)
            place (sustain[i], false);
        for (int i = 0; i < nOn; ++i)
            place (onsetSlots[i], false);
        for (int i = 0; i < nRest; ++i)
            place (rest[i], true);

        int alreadyRest = 0;
        for (int i = 0; i < placed && i < 12; ++i)
            if (placedRest[i])
                ++alreadyRest;
        for (int i = 0; i < placed && i < 12 && alreadyRest == 0; ++i)
        {
            if (placedRest[i])
                continue;
            const std::uint32_t h = (seed + (std::uint32_t) bar * 17u + (std::uint32_t) i * 3u) * 0x9E3779B9u;
            if ((h % 100u) >= 8u)
                continue;
            int dest = -1;
            for (int g = 0; g < nRest; ++g)
                if (! taken[rest[g]])
                {
                    dest = rest[g];
                    break;
                }
            if (dest < 0)
                continue;
            const float start = (float) (bar * 16 + dest);
            float hold = len;
            const float barEnd = (float) ((bar + 1) * 16);
            if (start + hold > barEnd)
                hold = barEnd - start;
            if (hold < 0.5f)
                continue;
            const int old = (int) std::lround (lane.notes[placedIdx[i]].start) % 16;
            taken[old] = false;
            taken[dest] = true;
            lane.notes[placedIdx[i]].start = start;
            lane.notes[placedIdx[i]].length = hold;
            placedRest[i] = true;
        }
    }
    (void) rng;
    (void) freedom;
}

void writeHits (Lane& lane, const Groove& g, const Settings& s, Rng& rng)
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
    const int prefer[] = { 6, 14, 10, 2, 7, 15, 3, 11 };
    const int voi[][3] = {
        { 0, 7, 12 },
        { 3, 10, 15 },
        { 7, 10, 19 },
        { 0, 3, 10 },
    };
    const int voice = (int) (rng.next() * 4.0) % 4;

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
            for (int p : prefer)
                if (slot == p)
                    sc += 0.8f;
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
        e.nChord = 3;
        for (int c = 0; c < 3; ++c)
            e.chord[c] = (std::int8_t) voi[voice][c];
        e.vel = 106;
        if (pushEv (lane, e, s.bars))
            lane.notes[lane.count - 1].timbre = (std::uint8_t) ((int) (rng.next() * 5.0) % 5);
    }
}

void writePluck (Lane& lane, const Settings& s, Rng& rng)
{
    clearLane (lane, lane.seed);
    // Pluck belongs to DEEP and BOUNCY. ROLLER and JACKIN leave the lane empty.
    if (s.dialect != Long && s.dialect != Pop)
        return;
    int per = s.dialect == Pop ? 6 : 3;
    if (s.styleIntensity <= 0.25f)
        per = 1;
    else if (s.styleIntensity >= 0.75f)
        per = std::min (8, per + 2);
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
    if (s.styleIntensity >= 0.75f)
    {
        const int step = s.dialect == Long ? 5 : (s.dialect == Gaskin ? 7 : 3);
        for (int t = 2; t < s.bars * 16; t += step)
        {
            Ev e;
            e.start = (float) t;
            e.len = 1.f;
            e.nChord = 1;
            e.chord[0] = (std::int8_t) degs[(t / step) % 5];
            e.vel = 76;
            if (! pushEv (lane, e, s.bars))
                break;
        }
    }
}

void writePad (Lane& lane, const Settings& s, Rng& rng)
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
        Ev e;
        e.start = start;
        e.len = len;
        e.nChord = 3;
        e.vel = 70;
        int bag[8] = { 3, 7, 10, 14, 15, 17, 2, 8 };
        for (int i = 7; i > 0; --i)
        {
            const int j = (int) (rng.next() * (double) (i + 1));
            std::swap (bag[i], bag[std::clamp (j, 0, i)]);
        }
        for (int c = 0; c < 3; ++c)
            e.chord[c] = (std::int8_t) bag[c];
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
    // Style intensity. The middle leaves the style pattern alone. Low strips
    // percussion. High lays a 3, 5, or 7 step perc loop on top of it.
    if (s.styleIntensity <= 0.25f)
    {
        for (int t = 0; t < steps; ++t)
            if ((t % 16) != 5)
                drums.vel[Perc][t] = 0;
    }
    else if (s.styleIntensity >= 0.75f)
    {
        const int step = s.dialect == Long ? 5 : (s.dialect == Gaskin ? 7 : 3);
        for (int t = 1; t < steps; t += step)
        {
            if (kickSlot (t % 16))
                continue;
            if (drums.vel[Perc][t] == 0)
                drums.vel[Perc][t] = 62;
        }
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
        return lane.notes[a].vel > lane.notes[b].vel;
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

struct RhythmRing
{
    std::uint64_t lo[20] {};
    std::uint64_t hi[20] {};
    int n = 0;
    int head = 0;
};

RhythmRing& rhythmRing (int dialect)
{
    static RhythmRing rings[NumDialects];
    if (dialect < 0 || dialect >= NumDialects)
        dialect = 0;
    return rings[dialect];
}

void rhythmOf (const Lane& lane, std::uint64_t& lo, std::uint64_t& hi)
{
    lo = 0;
    hi = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < 0 || st >= 128)
            continue;
        if (st < 64)
            lo |= 1ull << st;
        else
            hi |= 1ull << (st - 64);
    }
}

bool rhythmSeen (const RhythmRing& ring, std::uint64_t lo, std::uint64_t hi)
{
    const int n = std::min (ring.n, 20);
    for (int i = 0; i < n; ++i)
        if (ring.lo[i] == lo && ring.hi[i] == hi)
            return true;
    return false;
}

void rememberRhythm (RhythmRing& ring, std::uint64_t lo, std::uint64_t hi)
{
    ring.lo[ring.head] = lo;
    ring.hi[ring.head] = hi;
    ring.head = (ring.head + 1) % 20;
    if (ring.n < 20)
        ++ring.n;
}

bool nudgeRhythm (Lane& lane, std::uint32_t salt)
{
    if (lane.count <= 1)
        return false;
    bool used[8][16] {};
    int idx[8][16];
    int nIn[8] {};
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < 0 || st >= 128)
            continue;
        const int b = st / 16;
        const int s = st % 16;
        if (b < 0 || b >= 8)
            continue;
        used[b][s] = true;
        if (nIn[b] < 16)
            idx[b][nIn[b]++] = i;
    }
    // Bar 4's answer and the last-bar turn stay put.
    const int barsTry[] = { 0, 2, 5, 6, 1, 3 };
    for (int attempt = 0; attempt < 6; ++attempt)
    {
        const int b = barsTry[attempt];
        if (nIn[b] < 2)
            continue;
        const int ni = idx[b][(int) ((salt + (std::uint32_t) attempt) % (std::uint32_t) nIn[b])];
        const int from = (int) std::lround (lane.notes[ni].start) % 16;
        for (int step = 1; step < 16; ++step)
        {
            const int dest = (from + step * 2) % 16;
            if (kickSlot (dest) || used[b][dest])
                continue;
            used[b][from] = false;
            used[b][dest] = true;
            lane.notes[ni].start = (float) (b * 16 + dest);
            return true;
        }
    }
    return false;
}

// A batch of 20 should not reprint the same 8-bar bass rhythm. One off-kick
// move is enough to leave an exact twin of a roll still in the session.
void separateTwinRhythm (Lane& lane, int dialect, std::uint32_t seed)
{
    RhythmRing& ring = rhythmRing (dialect);
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    rhythmOf (lane, lo, hi);
    for (int t = 0; t < 6 && rhythmSeen (ring, lo, hi); ++t)
    {
        if (! nudgeRhythm (lane, seed + (std::uint32_t) t * 17u))
            break;
        rhythmOf (lane, lo, hi);
    }
    rememberRhythm (ring, lo, hi);
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
    s.styleIntensity = std::clamp (s.styleIntensity, 0.f, 1.f);
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
    if (regen (Bass))
        lockPhrase (g.lane[Bass], s.dialect, bassSeed, s.bars, s.dark, s.bounce, s.busy);
    if (regen (Bass))
    {
        separateTwinRhythm (g.lane[Bass], s.dialect, bassSeed);
        enforceMonophonic (g.lane[Bass]);
        avoidReaction (g.lane[Bass]);
    }
    // Stabs are rewritten on the bass that ships. A bass rest is only a light
    // preference: the reference rolls put about 8% of stabs there.
    if (regen (Stab))
    {
        const std::uint32_t stabSeed = g.lane[Stab].seed ? g.lane[Stab].seed : 1u;
        g.lane[Stab].seed = stabSeed;
        Rng rs (stabSeed);
        char stabBreaks[48] = "";
        writeStab (g.lane[Stab], g.lane[Bass], s, rs, freedom, stabBreaks, (int) sizeof stabBreaks);
    }
    for (int part = 0; part < Drums; ++part)
        dedupeNoteOns (g.lane[part]);
    enforceMonophonic (g.lane[Bass]);
    g.guardScore = shippedHard ? 0.f : 1.f;
    g.guardLevel = shippedHard ? 2 : 0;
    std::snprintf (g.guardRef, sizeof g.guardRef, "%s", shippedHard ? "HARD" : "ORIGINAL");
    ++g.generation;
}

} // namespace rolla
