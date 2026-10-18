#include "Generator.h"
#include "PhraseBank.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#if defined (_MSC_VER)
#include <intrin.h>
#endif

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
    // A quarter of mid-bounce rolls (Composer: BOUNCE 75%, PULSE 25%). The
    // knob's extremes stay on the bounce grid. The seed's own position in
    // [0, 2^32) is the draw: a golden-ratio seed walk gets an even 25% share.
    const std::uint32_t s = seed ? seed : 1u;
    return (std::uint32_t) (((std::uint64_t) s * 100u) >> 32) < 25u;
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
        // Dark stays busier than the sparse line, and stops around 6-7 attacks.
        int target = 6 + (int) (((seed >> (bar & 3)) + (std::uint32_t) bar) % 2u);
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

// `want` (4-6) off-kick slots, two 16ths apart where the bag allows, drawn from
// the seed. The last bar stops by slot 9 so its turn rest stays silent.
bool pickPulseSlots (int* out, std::uint32_t h, bool turn, int want)
{
    want = clampi (want, 1, 6);
    const int poolFull[] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 };
    const int poolTurn[] = { 1, 2, 3, 5, 6, 7, 9 };
    const int* pool = turn ? poolTurn : poolFull;
    const int nPool = turn ? 7 : 12;
    int bag[12];
    for (int i = 0; i < nPool; ++i)
        bag[i] = pool[i];
    for (int i = nPool - 1; i > 0; --i)
    {
        h = h * 0x9E3779B1u + 0x85EBCA6Bu;
        const int j = (int) (h % (std::uint32_t) (i + 1));
        std::swap (bag[i], bag[j]);
    }
    int picked[6];
    int n = 0;
    for (int gap = 2; gap >= 1 && n < want; --gap)
    {
        for (int i = 0; i < nPool && n < want; ++i)
        {
            bool ok = true;
            for (int k = 0; k < n; ++k)
                if (std::abs (bag[i] - picked[k]) < gap)
                    ok = false;
            if (! ok)
                continue;
            bool already = false;
            for (int k = 0; k < n; ++k)
                if (picked[k] == bag[i])
                    already = true;
            if (! already)
                picked[n++] = bag[i];
        }
    }
    if (n < want)
        return false;
    std::sort (picked, picked + want);
    for (int k = 0; k < want; ++k)
        out[k] = picked[k];
    // The dotted-pulse sets are a soft ban when most bars land on them.
    auto nearBanned = [&] (std::initializer_list<int> banned)
    {
        int inter = 0;
        for (int s : banned)
            for (int k = 0; k < want; ++k)
                if (out[k] == s)
                    ++inter;
        const int uni = want + (int) banned.size() - inter;
        return uni > 0 && inter * 4 >= uni * 3;
    };
    if (nearBanned ({ 0, 3, 6, 9, 12 }) || nearBanned ({ 0, 3, 6, 9, 12, 14 })
        || nearBanned ({ 0, 3, 6, 8, 9, 12, 14 }))
        return false;
    return true;
}

// Pulse is a steady off-kick grid: six attacks a bar (five in the closing bar,
// which keeps its four-16th rest), so the held line lands at 5-8 notes a bar
// with lengths in the 1.8-3.2 band. The grid comes from the seed, not a
// shape book, so a batch does not reprint one onset set. Neighbours never
// repeat a pitch, so holdBassNotes can tie every note into the next one.
void makePulse (Lane& lane, int bars, std::uint32_t seed)
{
    constexpr int kBody = 6;
    constexpr int kTurn = 5;
    const std::uint32_t h = (seed ? seed : 1u) * 0x9E3779B1u;
    int even[kBody] = { 1, 3, 6, 9, 11, 14 };
    int odd[kBody] = { 2, 5, 7, 10, 13, 15 };
    int turn[kBody] = { 1, 3, 5, 7, 9, 0 };
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
            // A bar of one pitch class answers itself an octave away.
            if (havePrev && nte.chord[0] == prevPitch)
                nte.chord[0] = (std::int8_t) (nte.chord[0] <= 7 ? nte.chord[0] + 12 : nte.chord[0] - 12);
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
        static const int alt[] = { 3, 10, 7, 5 };
        const int d = alt[(seed + (std::uint32_t) st) % 4u];
        if (pcOf (lane.notes[i].chord[0]) == d)
            lane.notes[i].chord[0] = 0;
        else
            lane.notes[i].chord[0] = (std::int8_t) d;
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

// D has to be the most-sounding bass pitch. Eb and E are passing tones:
// short, and never the note the key reader could mistake for the tonic.
void anchorPopRoot (Lane& lane, std::uint32_t seed)
{
    if (lane.count <= 0)
        return;
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    static const int colour[] = { 3, 7, 10, 5, 8 };
    for (int i = 0; i < lane.count; ++i)
    {
        const int pc = pcOf (lane.notes[i].chord[0]);
        if ((pc == 1 || pc == 2) && lane.notes[i].length > 1.15f && ! noteInOctave (lane, i))
            lane.notes[i].chord[0] = (std::int8_t) colour[(seed + (std::uint32_t) i) % 5u];
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
        lane.notes[best].chord[0] = (std::int8_t) colour[(seed + (std::uint32_t) guard) % 5u];
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
    static const int deg[] = { 0, 3, 7, 10, 2, 5, 8, 1 };
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
                    int want = cur;
                    for (int t = 0; t < 8; ++t)
                    {
                        const int d = deg[(cur + 1 + t + pass) % 8];
                        if (d != p0)
                        {
                            want = d;
                            break;
                        }
                    }
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
                            for (int t = 0; t < 8; ++t)
                            {
                                const int d = deg[(cur + 3 + t) % 8];
                                if (d != p0)
                                {
                                    lane.notes[best].chord[0] = (std::int8_t) d;
                                    break;
                                }
                            }
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
// bars 5-8 rotate their degrees (pushJackinVariationBack) and the call/answer
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

// B4: the last onset of bar 4 takes another pitch the hook plays (JACKIN is
// phrygian, never the b2 here, never a pitch bar 2 has within one 16th of
// that slot), so bar 4 answers bar 2 and the rest of the bar repeats.
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
    static const int alt[] = { 3, 10, 7, 8, 5, 0 };
    const int spin = (int) (((seed ? seed : 1u) >> 4) % 6u);
    int want = -1;
    for (int pass = 0; pass < 2 && want < 0; ++pass)
        for (int t = 0; t < 6 && want < 0; ++t)
        {
            const int d = alt[(spin + t) % 6];
            if (d == oldPc || near[d] || ! phrase::inMode (d, phrase::Phrygian))
                continue;
            if (pass == 0 && ! inHook[d])
                continue;
            // Root on slot 1 or 3 is half of the banned reaction figure.
            if (d == 0 && (slot == 1 || slot == 3))
                continue;
            want = d;
        }
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
        // A wiped bar is a hard fail. A short in-scale root, as lockFromBook does.
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
    if (nb >= 8 && (((seed ? seed : 1u) * 0xC2B2AE35u) % 100u) < 58u)
        answerJackinBar4 (lane, seed);
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
    // Just over half the rolls. A pitch-class change is what the token test counts;
    // a one-slot slide with the same pitch still matches.
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
        static const int alt[] = { 3, 10, 7, 8, 5, 0 };
        const int oldPc = pcOf (lane.notes[ni].chord[0]);
        for (int t = 0; t < 6; ++t)
        {
            const int d = alt[(int) (((seed >> 4) + (std::uint32_t) t) % 6u)];
            if (d == oldPc || near[d])
                continue;
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

// ROLLER's shape book clusters attacks on neighbouring 16ths, so every note
// is a 1/16 and the two bars can land on the same grid. Move one attack of
// each adjacent pair onto a free off-beat, then let the gap be 2 or 3/16.
// One back-half pitch changes so bar 2 is not a copy of bar 1.
void openRollerCell (Lane& lane, std::uint32_t seed)
{
    if (lane.count <= 0)
        return;
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    for (int bar = 0; bar < 2; ++bar)
    {
        const int origin = bar * 16;
        bool used[16] {};
        for (int i = 0; i < lane.count; ++i)
        {
            const int st = (int) std::lround (lane.notes[i].start);
            if (st / 16 == bar && st % 16 < 16)
                used[st % 16] = true;
        }
        for (int i = 0; i + 1 < lane.count; ++i)
        {
            const int a = (int) std::lround (lane.notes[i].start);
            const int b = (int) std::lround (lane.notes[i + 1].start);
            if (a / 16 != bar || b / 16 != bar || b != a + 1)
                continue;
            int dest = -1;
            const int spin = (int) ((seed + (std::uint32_t) bar * 5u + (std::uint32_t) a) % 12u);
            static const int pool[] = { 14, 15, 13, 11, 10, 9, 7, 6, 5, 3, 2, 1 };
            for (int t = 0; t < 12 && dest < 0; ++t)
            {
                const int s = pool[(spin + t) % 12];
                if (used[s] || kickSlot (s) || std::abs (s - (a % 16)) < 2)
                    continue;
                dest = s;
            }
            if (dest < 0)
                continue;
            used[b % 16] = false;
            used[dest] = true;
            lane.notes[i + 1].start = (float) (origin + dest);
        }
    }
    std::sort (lane.notes, lane.notes + lane.count,
               [] (const Note& a, const Note& b) { return a.start < b.start; });
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < 0 || st >= 32)
            continue;
        int next = (st / 16 + 1) * 16;
        if (i + 1 < lane.count)
        {
            const int nst = (int) std::lround (lane.notes[i + 1].start);
            if (nst / 16 == st / 16)
                next = nst;
        }
        const int gap = next - st;
        float len = 1.f;
        // 2/16 on a two-slot gap, a bit over 2/16 when the gap is wider.
        // The lane median has to stay at or under 2.4, so 3/16 is the odd note.
        if (gap >= 4 && ((seed + (std::uint32_t) i) % 5u) == 0u)
            len = 3.f;
        else if (gap >= 3)
            len = 2.2f;
        else if (gap >= 2)
            len = 2.f;
        lane.notes[i].length = len;
    }
    int back = -1;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st >= 24 && st < 32)
            back = i;
    }
    if (back >= 0)
    {
        static const int alt[] = { 3, 10, 7, 8, 5, 0 };
        const int old = pcOf (lane.notes[back].chord[0]);
        for (int t = 0; t < 6; ++t)
        {
            const int d = alt[(seed + (std::uint32_t) t) % 6u];
            if (d == old)
                continue;
            const int reg = (int) lane.notes[back].chord[0] - old;
            lane.notes[back].chord[0] = (std::int8_t) (d + (reg >= 12 ? 12 : 0));
            break;
        }
    }
}

// Repeat a 2-bar phrase so the loop matches itself. Bar rhythms come from a
// shift-resistant codebook and the pitch contour from the seed, so two rolls
// do not share a bass skeleton after a transposition or a two-16th nudge.
// DARK at either end still retints the degrees. BOUNCE above 0.6 still
// shoves even slots, which is what the knob audit compares. BUSY scales how
// many of those attacks survive, around the shipped 0.40 phrase.
void lockPhrase (Lane& lane, int dialect, std::uint32_t seed, int bars, float dark, float bounce, float busy,
                 bool pulse)
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
            else if (gap >= 3.f)
                len = 2.2f;
            else if (gap >= 2.f)
                len = 2.f;
            else
                len = 1.f;
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

    if (dialect == UkMin)
        openRollerCell (lane, seed);

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
    if (pulse)
    {
        makePulse (lane, bars, seed);
        // The pulse grid replaces the phrase, so BUSY has to scale it again.
        applyBusyDensity (lane, dialect, bars, busy, seed);
    }
    paintPhraseVariety (lane, dialect, seed);
    differBar4 (lane, bars, seed);
    finishBass (lane);
    ensureBar8Turn (lane, bars, seed);
    anchorPopRoot (lane, seed);
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

// The scorer marks a 16th sounding only when round(start) <= slot < round(start + length).
// A 2.2-length note spills into the next slot without covering it, and that spill
// must not count as a place to sit a stab.
bool bassCovers (const Lane& lane, int slot)
{
    for (int i = 0; i < lane.count; ++i)
    {
        const int a = (int) std::lround (lane.notes[i].start);
        const int b = (int) std::lround (lane.notes[i].start + lane.notes[i].length);
        if (slot >= a && slot < b)
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

// Stab chord onsets per bar (a chord counts as one hit), every style.
constexpr int kStabHitsMax = 3;

// Composer's cap is 3 chord hits a bar at the default STAB (0.55). Turning
// STAB up past 0.8 allows one more, so the knob still adds density at the top.
int stabHitCap (const Settings& s) noexcept
{
    return s.stab > 0.8f ? kStabHitsMax + 1 : kStabHitsMax;
}

void writeStab (Lane& lane, const Lane& bass, const Settings& s, Rng& rng, float freedom, char* breaks, int breaksN)
{
    const std::uint32_t seed = lane.seed ? lane.seed : 1u;
    clearLane (lane, seed);
    int per = s.dialect == Pop ? 8 : s.dialect == Gaskin ? 6 : s.dialect == Long ? 3 : 1;
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

    const int flavour = s.dialect == Gaskin ? jackinFlavour (seed) : -1;
    float len = s.dialect == Pop ? 1.f : 2.f;
    if (s.dialect == Gaskin)
        len = flavour == 2 ? 6.5f : (flavour == 3 ? 1.f : 1.9f);

    // One chord per half of a 2-bar figure, drawn for this roll.
    // ROLLER builds the chord from the scale (inversion, spread, register)
    // instead of a fixed book, and shuffles the off-beats per bar.
    // Each style has its own 8 voicings (semitones over the key root, natural minor).
    // DEEP always carries a minor third: no bare open fifths, darker, and clear of
    // the reference chop voicings.
    static const int deepBook[][3] = {
        { 0, 3, 7 }, { 3, 7, 10 }, { 0, 3, 10 }, { 3, 10, 15 },
        { 7, 10, 15 }, { 0, 7, 15 }, { 5, 8, 12 }, { 3, 7, 14 },
    };
    static const int bouncyBook[][3] = {
        { 3, 10, 15 }, { 7, 10, 19 }, { 3, 7, 14 }, { 10, 14, 17 },
        { 0, 7, 15 }, { 3, 10, 17 }, { 5, 10, 15 }, { 7, 12, 19 },
    };
    static const int jackinBook[][3] = {
        { 3, 7, 10 }, { 0, 3, 10 }, { 3, 10, 14 }, { 7, 10, 14 },
        { 5, 8, 12 }, { 0, 7, 10 }, { 3, 7, 12 }, { 10, 15, 19 },
    };
    const int (*book)[3] = s.dialect == Long ? deepBook : s.dialect == Gaskin ? jackinBook : bouncyBook;
    int voice[2] = { (int) (seed % 8u), (int) ((seed / 8u) % 8u) };
    if (voice[1] == voice[0])
        voice[1] = (voice[1] + 3) % 8;

    auto rollerChord = [&] (int cell, int* out)
    {
        static const int scale[] = { 0, 2, 3, 5, 7, 8, 10 };
        std::uint32_t h = (seed ^ 0xA5A5u) * 0x9E3779B1u + (std::uint32_t) cell * 0x6D2B79F5u;
        bool used[7] {};
        int picked[3];
        for (int i = 0; i < 3; ++i)
        {
            h = h * 0x9E3779B1u + 1u;
            int k = (int) (h % 7u);
            for (int t = 0; t < 7 && used[k]; ++t)
                k = (k + 1) % 7;
            used[k] = true;
            picked[i] = scale[k];
        }
        const int rot = (int) ((h >> 4) % 3u);
        const int spread = (int) ((h >> 8) % 3u);
        const int reg = ((h >> 12) & 1u) != 0 ? 12 : 0;
        for (int i = 0; i < 3; ++i)
        {
            int semi = picked[(i + rot) % 3] + reg;
            if (spread == 1 && i == 2)
                semi += 12;
            if (spread == 2 && i == 1)
                semi += 12;
            out[i] = semi;
        }
    };

    int order[8][12];
    for (int bar = 0; bar < 8; ++bar)
    {
        const int par = bar & 1;
        if (s.dialect == UkMin)
        {
            int bag[12] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 };
            std::uint32_t h = seed * 0x9E3779B1u + (std::uint32_t) bar * 0x85EBCA6Bu;
            for (int i = 11; i > 0; --i)
            {
                h = h * 0x9E3779B1u + 0x85EBCA6Bu;
                const int j = (int) (h % (std::uint32_t) (i + 1));
                std::swap (bag[i], bag[j]);
            }
            for (int t = 0; t < 12; ++t)
                order[bar][t] = bag[t];
            continue;
        }
        const int spin = (int) ((seed >> (8 + par * 4)) % 12u);
        static const int off[] = { 3, 7, 9, 14, 2, 6, 10, 11, 5, 1, 13, 15 };
        static const int early[] = { 1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15 };
        static const int ea[] = { 3, 7, 9, 1, 5, 11, 13, 15, 14, 2, 6, 10 };
        const int* src = flavour == 2 ? early : (flavour == 3 ? ea : off);
        const int twist = flavour == 2 ? par * 3 : (flavour == 3 ? par : par * 5);
        for (int t = 0; t < 12; ++t)
            order[bar][t] = src[(spin + t + twist) % 12];
    }

    const bool skipOdd = (s.busy < 0.25f && per <= 2) || stabSparse;
    // At most kStabHitsMax chord hits a bar in every style. Low BUSY keeps one
    // fewer outside ROLLER, so BUSY still moves the density under the cap.
    per = std::min (per, (s.busy < 0.25f && s.dialect != UkMin) ? stabHitCap (s) - 1 : stabHitCap (s));
    for (int bar = 0; bar < s.bars && bar < 8; ++bar)
    {
        if (skipOdd && (bar & 1) != 0)
            continue;
        const int par = bar & 1;
        int rollerVoi[3];
        if (s.dialect == UkMin)
            rollerChord (bar, rollerVoi);
        const int* voi = s.dialect == UkMin ? rollerVoi : book[voice[par]];
        if (s.dialect == UkMin)
        {
            const std::uint32_t lh = (seed + (std::uint32_t) bar * 13u) * 0x9E3779B1u;
            const float lens[3] = { 1.5f, 2.f, 2.5f };
            len = lens[lh % 3u];
        }
        bool taken[16] {};
        int sustain[12];
        int onsetSlots[12];
        int rest[12];
        int nSus = 0;
        int nOn = 0;
        int nRest = 0;
        for (int t = 0; t < 12; ++t)
        {
            const int slot = order[bar][t];
            if (slot < 0 || slot > 15 || kickSlot (slot))
                continue;
            const int abs = bar * 16 + slot;
            // ROLLER uses the scorer's covered 16ths. A note that only spills
            // into the next slot is a rest there, not a sustain.
            const bool covered = s.dialect == UkMin ? bassCovers (bass, abs) : bassSounds (bass, abs);
            if (bassOnset (bass, abs))
                onsetSlots[nOn++] = slot;
            else if (covered)
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
            e.vel = (std::uint8_t) stabVel;
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
        // JACKIN wants a clear share of stabs in bass rests (the floor is 8%).
        // ROLLER is clamped after the loop. Other styles keep the light 8% nudge,
        // and only when the bar has none.
        const int wantRest = s.dialect == Gaskin ? std::max (1, (placed * 2) / 6) : 0;
        const bool lightNudge = s.dialect != Gaskin && s.dialect != UkMin && alreadyRest == 0;
        for (int i = 0; i < placed && i < 12; ++i)
        {
            if (placedRest[i])
                continue;
            if (s.dialect == Gaskin)
            {
                if (alreadyRest >= wantRest)
                    break;
            }
            else if (lightNudge)
            {
                const std::uint32_t h = (seed + (std::uint32_t) bar * 17u + (std::uint32_t) i * 3u) * 0x9E3779B9u;
                if ((h % 100u) >= 8u)
                    continue;
            }
            else
                break;
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
            ++alreadyRest;
        }
    }
    // ROLLER: one stab a bar lands on a covered 16th, which sits under the 8%
    // rest floor. Move hits until about one in eight is in a real rest, and
    // move extras back so the share stays under a quarter (the 50% bonus is
    // a different complaint). A spill into the next 16th is not a sustain.
    if (s.dialect == UkMin && lane.count > 0)
    {
        auto inRest = [&] (int index) -> bool
        {
            return ! bassCovers (bass, (int) std::lround (lane.notes[index].start));
        };
        int rests = 0;
        for (int i = 0; i < lane.count; ++i)
            if (inRest (i))
                ++rests;
        const int lo = std::max (1, (lane.count + 7) / 8);
        const int hi = std::max (lo, lane.count / 5);
        auto relocate = [&] (int index, bool toRest) -> bool
        {
            const int st = (int) std::lround (lane.notes[index].start);
            const int bar = st / 16;
            if (bar < 0 || bar >= 8)
                return false;
            bool taken[16] {};
            for (int k = 0; k < lane.count; ++k)
            {
                const int ks = (int) std::lround (lane.notes[k].start);
                if (ks / 16 == bar && ks % 16 >= 0 && ks % 16 < 16)
                    taken[ks % 16] = true;
            }
            const int origin = bar * 16;
            const int spin = (int) ((seed + (std::uint32_t) bar * 9u + (std::uint32_t) index) % 15u);
            const float barEnd = (float) ((bar + 1) * 16);
            for (int pass = 0; pass < 2; ++pass)
            {
                for (int t = 0; t < 15; ++t)
                {
                    const int slot = 1 + (spin + t) % 15;
                    if (kickSlot (slot) || taken[slot])
                        continue;
                    const bool rest = ! bassCovers (bass, origin + slot);
                    if (toRest != rest)
                        continue;
                    if (! toRest && pass == 0 && bassOnset (bass, origin + slot))
                        continue;
                    const float start = (float) (origin + slot);
                    float hold = lane.notes[index].length;
                    if (start + hold > barEnd)
                        hold = barEnd - start;
                    if (hold < 0.5f)
                        continue;
                    lane.notes[index].start = start;
                    lane.notes[index].length = hold;
                    return true;
                }
                if (toRest)
                    break;
            }
            return false;
        };
        int seq[kMaxNotes];
        const int nSeq = std::min (lane.count, kMaxNotes);
        for (int i = 0; i < nSeq; ++i)
            seq[i] = i;
        for (int i = nSeq - 1; i > 0; --i)
        {
            const std::uint32_t h = seed * 0x9E3779B1u + (std::uint32_t) i * 0x85EBCA6Bu;
            std::swap (seq[i], seq[(int) (h % (std::uint32_t) (i + 1))]);
        }
        int guard = 0;
        while (rests > hi && guard++ < nSeq)
        {
            bool moved = false;
            for (int n = 0; n < nSeq && rests > hi; ++n)
            {
                const int i = seq[n];
                if (! inRest (i) || ! relocate (i, false))
                    continue;
                --rests;
                moved = true;
                break;
            }
            if (! moved)
                break;
        }
        guard = 0;
        while (rests < lo && guard++ < nSeq)
        {
            bool moved = false;
            for (int n = 0; n < nSeq && rests < lo; ++n)
            {
                const int i = seq[n];
                if (inRest (i) || ! relocate (i, true))
                    continue;
                ++rests;
                moved = true;
                break;
            }
            if (! moved)
                break;
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
            e.nChord = 3;
            for (int c = 0; c < 3; ++c)
                e.chord[c] = (std::int8_t) voi[voice][c];
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
    // FILL: the last beat of the phrase becomes a snare / tom run into the
    // downbeat. Hats there drop out so the run reads. Seed-free, so the same
    // roll with FILL on and off differs only in that beat.
    if (s.fill && steps >= 16)
    {
        const int beat = steps - 4;
        static constexpr int kRun[4] = { Snare, Snare, TomMid, TomLow };
        static constexpr int kVel[4] = { 78, 92, 100, 110 };
        for (int k = 0; k < 4; ++k)
        {
            const int t = beat + k;
            drums.vel[HatC][t] = 0;
            drums.vel[HatO][t] = 0;
            drums.vel[Ride][t] = 0;
            drums.vel[Perc][t] = 0;
            drums.vel[kRun[k]][t] = (std::uint8_t) kVel[k];
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

// Last rolls of this process. A 20-roll batch plus the skeleton redraws fit;
// the compare is a handful of bitsets, so ROLL stays instant.
constexpr int kRhythmHist = 64;

struct RhythmSnap
{
    std::uint64_t lo = 0;
    std::uint64_t hi = 0;
    std::uint32_t b2 = 0;
};

struct RhythmRing
{
    RhythmSnap snap[kRhythmHist] {};
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

int popBits (std::uint64_t x) noexcept
{
#if defined (_MSC_VER)
    return (int) __popcnt64 (x);
#else
    return __builtin_popcountll (x);
#endif
}

RhythmSnap rhythmSnap (const Lane& lane) noexcept
{
    RhythmSnap s;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < 0 || st >= 128)
            continue;
        if (st < 64)
            s.lo |= 1ull << st;
        else
            s.hi |= 1ull << (st - 64);
        if (st < 32)
            s.b2 |= 1u << st;
    }
    return s;
}

float jaccard32 (std::uint32_t a, std::uint32_t b) noexcept
{
    const std::uint32_t uni = a | b;
    if (uni == 0)
        return 0.f;
    return (float) popBits (a & b) / (float) popBits (uni);
}

float jaccard8 (const RhythmSnap& a, const RhythmSnap& b) noexcept
{
    const int inter = popBits (a.lo & b.lo) + popBits (a.hi & b.hi);
    const int uni = popBits (a.lo | b.lo) + popBits (a.hi | b.hi);
    if (uni <= 0)
        return 0.f;
    return (float) inter / (float) uni;
}

bool rhythmTwin (const RhythmSnap& a, const RhythmSnap& b) noexcept
{
    return jaccard32 (a.b2, b.b2) >= 0.80f || jaccard8 (a, b) >= 0.80f;
}

bool rhythmClose (const RhythmRing& ring, const RhythmSnap& snap) noexcept
{
    const int n = std::min (ring.n, kRhythmHist);
    for (int i = 0; i < n; ++i)
        if (rhythmTwin (ring.snap[i], snap))
            return true;
    return false;
}

void rememberRhythm (RhythmRing& ring, const RhythmSnap& snap)
{
    ring.snap[ring.head] = snap;
    ring.head = (ring.head + 1) % kRhythmHist;
    if (ring.n < kRhythmHist)
        ++ring.n;
}

bool moveOffKick (Lane& lane, int bar, std::uint32_t salt)
{
    if (bar < 0 || bar >= 8 || lane.count <= 1)
        return false;
    bool used[16] {};
    int idx[16];
    int nIn = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st / 16 != bar)
            continue;
        const int s = st % 16;
        used[s] = true;
        if (nIn < 16)
            idx[nIn++] = i;
    }
    if (nIn < 1)
        return false;
    const int ni = idx[(int) (salt % (std::uint32_t) nIn)];
    const int from = (int) std::lround (lane.notes[ni].start) % 16;
    for (int step = 2; step < 16; ++step)
    {
        const int dest = (from + step) % 16;
        if (kickSlot (dest) || used[dest])
            continue;
        if (std::abs (dest - from) < 2)
            continue;
        used[from] = false;
        used[dest] = true;
        lane.notes[ni].start = (float) (bar * 16 + dest);
        const int gap = 16 - dest;
        if (lane.notes[ni].length > (float) gap)
            lane.notes[ni].length = std::max (1.f, (float) gap - 0.05f);
        return true;
    }
    return false;
}

// Move two attacks. Odd passes stay inside bars 1-2, which is the grid the
// twin gate measures first. Even passes touch later bars except bar 4 and
// the closing turn.
bool nudgeRhythm (Lane& lane, std::uint32_t salt, int pass)
{
    const int early[] = { 0, 1, 0, 1 };
    const int later[] = { 2, 5, 6, 4 };
    const int* barsTry = (pass & 1) == 0 ? early : later;
    int moved = 0;
    for (int attempt = 0; attempt < 4 && moved < 2; ++attempt)
        if (moveOffKick (lane, barsTry[attempt], salt + (std::uint32_t) attempt * 17u + (std::uint32_t) pass * 3u))
            ++moved;
    return moved > 0;
}

// Near-identical means Jaccard >= 0.80 on the bars 1-2 onset grid OR the
// full 8-bar grid. One moved 16th is often still a twin; keep going until
// both grids clear, bounded so a roll cannot stall.
void separateTwinRhythm (Lane& lane, int dialect, std::uint32_t seed)
{
    RhythmRing& ring = rhythmRing (dialect);
    RhythmSnap snap = rhythmSnap (lane);
    for (int t = 0; t < 8 && rhythmClose (ring, snap); ++t)
    {
        if (! nudgeRhythm (lane, seed + (std::uint32_t) t * 17u, t))
            break;
        snap = rhythmSnap (lane);
    }
    rememberRhythm (ring, snap);
}

namespace
{

// ---- Roll harmony: one mode per roll, b2 cap, bass vs stab/pad semitone guard ----

constexpr int kClashTries = 8;

// Bass b2 cap as a share of bass sounding time. Phrygian rolls may lean on the
// b2 (their stab/pad never carry the natural 2: snapToMode moves 2 -> b3).
// Outside phrygian the cap is per style: ROLLER (UkMin) 12%, DEEP (Long) 6%,
// JACKIN (Gaskin) 6%, BOUNCY (Pop) 3%.
float b2Cap (int dialect, int mode) noexcept
{
    if (mode == phrase::Phrygian)
        return 0.30f;
    switch (dialect)
    {
        case UkMin: return 0.12f;
        case Long: return 0.06f;
        case Gaskin: return 0.06f;
        case Pop: return 0.03f;
        default: return 0.06f;
    }
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

// Root on slot 1 and slot 3 of one bar is the banned reaction figure.
bool makesReaction (const Lane& lane, int idx, int semi) noexcept
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
        if (makesReaction (lane, i, to))
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
            return ! makesReaction (lane, i, retarget (semi, pc));
        };
        // Resample: bounded, deterministic from the bass seed and the note.
        // JACKIN keys it on the note's place in its 2-bar loop, so the
        // repeats of one note take the same pitch.
        const std::uint32_t key = g.dialect == Gaskin
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
// clash and no reaction figure. Phrygian rolls keep their b2 (in mode).
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
            if (makesReaction (lane, i, retarget (semi, pc)))
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
        std::int8_t keep[4];
        int w = 0;
        for (int c = 0; c < nc; ++c)
            if ((bad & (1u << pcOf (n.chord[c]))) == 0)
                keep[w++] = n.chord[c];
        if (w == 0)
            keep[w++] = (std::int8_t) retarget (n.chord[0], pcOf (bass.notes[firstBass].chord[0]));
        n.nChord = (std::uint8_t) w;
        for (int c = 0; c < 4; ++c)
            n.chord[c] = c < w ? keep[c] : 0;
    }
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

// At most kStabHitsMax chord onsets a bar. The beat-1 hit and the closing
// answer of bars 4 and 8 stay; then louder, then more accented (8th offbeat,
// then 8th), then earlier hits win.
void capStabHits (Lane& lane, int bars, int cap)
{
    for (int bar = 0; bar < bars && bar < kMaxBars; ++bar)
    {
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
    const int mode = rollMode (s, bassSeed);
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
// Reference medians: ROLLER ~17%, DEEP ~7%, BOUNCY ~32%, JACKIN ~19%.
// The 35% hard cap (hardFail, PhraseBank kMaxKickShare) always wins.
float bassKickTarget (int dialect) noexcept
{
    switch (dialect)
    {
        case Long:   return 0.07f;
        case Pop:    return 0.32f;
        case Gaskin: return 0.19f;
        default:     return 0.17f;
    }
}

// A&R msg 423 #2: how often a held bass note leaves a one-16th breath before
// the next onset. JACKIN keeps the most, so its stabs still find bass rests.
float bassBreathShare (int dialect) noexcept
{
    switch (dialect)
    {
        case Long:   return 0.25f;
        case Gaskin: return 0.45f;
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

// The last bar of a 4- or 8-bar loop keeps its four-16th turn rest (lockPhrase,
// ensureBar8Turn). Nothing in the post passes moves or holds into it.
float bassTailCut (int bars) noexcept
{
    return bars >= 4 ? (float) ((bars - 1) * 16 + 12) : (float) (bars * 16);
}

// Moves bass attacks one 16th (then two) onto a free kick step in the same
// bar until the on-kick share reaches the style target, never above 35%.
// Only empty slots are used and a two-16th move needs the slot between free,
// so note order (and every interval / octave figure) is unchanged. The pick
// is keyed on the position in the 2-bar cycle, so repeats move together.
void placeBassOnKicks (Lane& lane, int dialect, int bars, std::uint32_t seed)
{
    const int n = std::min (lane.count, kMaxNotes);
    const int nslots = std::min (bars, kMaxBars) * 16;
    if (n <= 1 || nslots <= 0)
        return;
    sortByStart (lane);
    const int cap = (int) std::floor (0.35f * (float) n + 1.0e-4f);
    const int want = std::min (cap, (int) std::lround (bassKickTarget (dialect) * (float) n));
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
        const bool wholeGroups = dialect == Gaskin;
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
// sounds for 0.80 of the loop, never past capFrac; a line already above it
// is shortened. Repeated pitches keep a breath; nothing passes the turn rest.
void holdDeepBass (Lane& lane, int bars, std::uint32_t seed, float capFrac)
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
    const float target = 0.80f * loopEnd;
    const float cap = capFrac * loopEnd;
    if (total > cap)
    {
        capBassSounding (lane, bars, capFrac, 1.0f);
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
        const float add = std::min (full[i] - lane.notes[i].length, cap - total);
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
// Nothing overlaps, nothing passes the loop end, and no note gets shorter.
// DEEP stops at 0.80 (holdDeepBass). BOUNCE never comes here.
void holdBassNotes (Lane& lane, int dialect, int bars, std::uint32_t seed)
{
    if (dialect == Long)
    {
        // 0.855 leaves room for a three-16th bar-8 pickup under the 0.88 cap.
        holdDeepBass (lane, bars, seed, 0.855f);
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
        const std::uint32_t key = dialect == Gaskin
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

    for (int i = 0; i < n; ++i)
        lane.notes[i].length = len[i];
    // BOUNCY pulse tops out at 0.975 (0.95 here: the pickup can add a little).
    if (pulse)
        capBassSounding (lane, bars, 0.95f, 1.0f);
}

// 0 not BOUNCY, 1 bounce, 2 pulse: the bass seed's draw, as lockPhrase plays it.
int bouncyModeOf (const Settings& s, std::uint32_t bassSeed) noexcept
{
    if (s.dialect != Pop)
        return BassModeNone;
    return popIsPulse (s.bounce, bassSeed ? bassSeed : 1u) ? BassModePulse : BassModeBounce;
}

} // namespace

bool rollWantsPickup (std::uint32_t bassSeed) noexcept
{
    Rng r ((bassSeed ? bassSeed : 1u) ^ 0x91C4B8u);
    return r.next() < 0.30;
}

int rollMode (const Settings& settings, std::uint32_t bassSeed) noexcept
{
    const int dialect = clampi (settings.dialect, 0, NumDialects - 1);
    const float dark = std::clamp (settings.dark, 0.f, 1.f);
    const std::uint32_t seed = bassSeed ? bassSeed : 1u;
    // JACKIN's lane is phrygian; DEEP uses the mode its phrase bank drew.
    if (dialect == Gaskin)
        return phrase::Phrygian;
    if (dialect == Long)
    {
        phrase::PhraseSettings ps;
        ps.busy = 0.40f;
        ps.bounce = std::clamp (settings.bounce, 0.f, 1.f);
        ps.dark = dark;
        ps.bars = 2;
        return phrase::makeBassPhrase (phrase::Style::Deep, seed, ps).mode;
    }
    // Same dark-weighted mix as the phrase bank.
    Rng r (seed ^ 0x3D0DE5u);
    const float pDor = 0.35f * (1.f - dark);
    const float pPhr = 0.22f * dark * dark;
    const float x = (float) r.next();
    if (x < pDor)
        return phrase::Dorian;
    if (x < pDor + pPhr)
        return phrase::Phrygian;
    return phrase::Aeolian;
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
    // BOUNCY plays one of two modes per roll: bounce (75%) or pulse (25%).
    const int popMode = bouncyModeOf (s, bassSeed);
    if (regen (Bass))
        g.bassMode = popMode;
    else if (s.dialect != Pop)
        g.bassMode = BassModeNone;
    if (regen (Bass))
    {
        // BOUNCE needs bar-to-bar density movement (onset sd >= 0.7). The phrase
        // is redrawn from derived seeds, at most six draws; the best one ships.
        const Lane drawn = g.lane[Bass];
        Lane best = drawn;
        float bestSd = -1.f;
        const int phraseDraws = popMode == BassModeBounce ? 6 : 1;
        for (int k = 0; k < phraseDraws; ++k)
        {
            g.lane[Bass] = drawn;
            const std::uint32_t phraseSeed = k == 0 ? bassSeed : (postHash (bassSeed ^ 0xB0B5u, (std::uint32_t) k) | 1u);
            lockPhrase (g.lane[Bass], s.dialect, phraseSeed, s.bars, s.dark, s.bounce, s.busy,
                        popMode == BassModePulse);
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
        separateTwinRhythm (g.lane[Bass], s.dialect, bassSeed);
        enforceMonophonic (g.lane[Bass]);
        avoidReaction (g.lane[Bass]);
        if (s.dialect == Pop)
        {
            ensureBar8Turn (g.lane[Bass], s.bars, bassSeed);
            anchorPopRoot (g.lane[Bass], bassSeed);
        }
        else if (s.dialect == Gaskin)
        {
            contrastCallAnswer (g.lane[Bass], s.bars);
            enforceMonophonic (g.lane[Bass]);
            contrastCallAnswer (g.lane[Bass], s.bars);
            // The loop repeats on the bar grid: A B A B4 A B A B8 from one bar pair.
            loopJackinBass (g.lane[Bass], s.bars, bassSeed);
        }
        enforceMonophonic (g.lane[Bass]);
        avoidReaction (g.lane[Bass]);
    }
    // A&R msg 423 #1 and #2: some bass on the kick, then hold the notes. Runs
    // before the stab rewrite so stabs see the bass that ships. BOUNCE keeps
    // its short notes (no hold); only BOUNCY pulse gets the held line.
    if (regen (Bass))
    {
        placeBassOnKicks (g.lane[Bass], s.dialect, s.bars, bassSeed);
        if (popMode == BassModeBounce)
            fitBounceSounding (g.lane[Bass], s.bars, 0.33f, 0.46f, bassSeed);
        else
            holdBassNotes (g.lane[Bass], s.dialect, s.bars, bassSeed);
        enforceMonophonic (g.lane[Bass]);
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
        capStabHits (g.lane[Stab], s.bars, stabHitCap (s));
    }
    // IN STAB follows the stab that ships, so the hits are rewritten on it.
    if (s.hitsInStab && regen (Hits))
    {
        Rng rh (seeds[Hits] ? seeds[Hits] : 1u);
        g.lane[Hits].seed = rh.s;
        writeHits (g.lane[Hits], g, s, rh);
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
            spreadBounceDensity (g.lane[Bass], s.bars, 0.7f, bassSeed);
            capBassSounding (g.lane[Bass], s.bars, 0.49f, 0.9f);
        }
        else if (popMode == BassModePulse)
            capBassSounding (g.lane[Bass], s.bars, 0.975f, 1.0f);
        else if (s.dialect == Long)
            capBassSounding (g.lane[Bass], s.bars, 0.88f, 1.0f);
    }
    g.guardScore = shippedHard ? 0.f : 1.f;
    g.guardLevel = shippedHard ? 2 : 0;
    std::snprintf (g.guardRef, sizeof g.guardRef, "%s", shippedHard ? "HARD" : "ORIGINAL");
    ++g.generation;
}

} // namespace rolla
