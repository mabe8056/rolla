#pragma once

// DEEP and BOUNCY passes (review of 18 Oct). generate() calls these for
// Long and Pop only, so ROLLER and JACKIN output stays byte-identical.
//
//  - bouncyBass: BOUNCY's bass from rules, not a shape book. About six notes
//    a bar (BUSY scales it), played legato so the bass sounds ~90% of the loop,
//    natural minor only, and octave bounces whose share rises with BOUNCE.
//    An 8-bar arc A B A B4 A B' A B8: every answer bar changes the note count.
//  - deepArc: DEEP's tiled grammar phrase gets answers that add or drop a note
//    on bars 4 and 8, and bars 5-6 vary pitch and rhythm.
//  - deepStabVoicing: DEEP's own voicing table (minor 7 / 9 / 11, sus2, b3
//    colours) as scale-step stacks over each stab's root, so it stays in key.
//  - stabsInGaps: a stab that lands on a sounding bass note moves into a gap
//    a sixteenth or two away, or the bass note ends where the stab answers.
//  - pickup: an optional short note in the last two sixteenths into bar 1.
//  - fixAnswers: the bar 4 / bar 8 count change survives the passes after it.
//
// All choices are weighted draws from the lane seed. No seed tables, no stored
// phrases, nothing taken from reference tracks. Header-only, so every target
// that builds Generator.cpp picks it up without a CMake change.

#include "Generator.h"
#include "Pattern.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rolla::deepbouncy
{

namespace detail
{
inline std::uint32_t mix (std::uint32_t seed, std::uint32_t salt) noexcept
{
    std::uint32_t x = (seed ? seed : 1u) * 0x9E3779B1u ^ (salt * 0x85EBCA6Bu + 0x27D4EB2Fu);
    x ^= x >> 15;
    x *= 0x2C1B3C6Du;
    x ^= x >> 12;
    x *= 0x297A2D39u;
    x ^= x >> 15;
    return x ? x : 1u;
}

inline bool kickSlot (int slot) noexcept
{
    return (slot & 3) == 0;
}

inline float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

inline void sortLane (Lane& lane) noexcept
{
    std::sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
}

inline int pcOf (int semi) noexcept;

inline bool minorPc (int semi) noexcept
{
    static const bool ok[12] = { true, false, true, true, false, true, false, true, true, false, true, false };
    return ok[((semi % 12) + 12) % 12];
}

inline int pcOf (int semi) noexcept
{
    return ((semi % 12) + 12) % 12;
}

template <typename T>
inline int pickWeighted (const T* w, int n, Rng& r)
{
    float sum = 0.f;
    for (int i = 0; i < n; ++i)
        sum += std::max (0.f, (float) w[i]);
    if (sum <= 0.f)
        return 0;
    float x = (float) r.next() * sum;
    for (int i = 0; i < n; ++i)
    {
        x -= std::max (0.f, (float) w[i]);
        if (x <= 0.f)
            return i;
    }
    return n - 1;
}

inline bool sounding (const Lane& lane, float t) noexcept
{
    for (int i = 0; i < lane.count; ++i)
        if (t >= lane.notes[i].start - 1e-3f && t < lane.notes[i].start + lane.notes[i].length - 1e-3f)
            return true;
    return false;
}

inline bool onsetAt (const Lane& lane, float t) noexcept
{
    for (int i = 0; i < lane.count; ++i)
        if (std::fabs (lane.notes[i].start - t) < 1e-3f)
            return true;
    return false;
}

inline float soundShare (const Lane& lane, int bars) noexcept
{
    const int steps = bars * 16;
    int on = 0;
    for (int t = 0; t < steps; ++t)
        on += sounding (lane, (float) t) ? 1 : 0;
    return steps > 0 ? (float) on / (float) steps : 0.f;
}

inline int countInBar (const Lane& lane, int bar) noexcept
{
    int n = 0;
    for (int i = 0; i < lane.count; ++i)
        if ((int) std::floor (lane.notes[i].start / 16.f + 1e-4f) == bar)
            ++n;
    return n;
}

// Every note ends where the next begins (minus a breath), never past the loop.
inline void legatoBass (Lane& lane, int bars, float breath, float maxLen)
{
    sortLane (lane);
    const float loop = (float) (bars * 16);
    for (int i = 0; i < lane.count; ++i)
    {
        const float next = i + 1 < lane.count ? lane.notes[i + 1].start : loop;
        lane.notes[i].length = std::clamp (next - lane.notes[i].start - breath, 0.5f, maxLen);
        lane.notes[i].length = std::min (lane.notes[i].length, loop - lane.notes[i].start);
    }
}

// One bar of BOUNCY rhythm: n distinct 16ths built on the off-beat 8ths, at
// most one kick-slot hit (the pulse), e / a 16ths for the bounce. No beat has
// its e, & and a all hit, and no run of four neighbouring 16ths.
inline float slotWeight (int s, float bounce) noexcept
{
    if ((s & 3) == 2)
        return 3.0f;              // off-beat 8th: the drive
    if ((s & 3) == 0)
        return (s == 0 || s == 8) ? 0.6f : 0.3f; // on the kick: the pulse
    return 0.35f + 0.35f * bounce; // e / a: the bounce
}

inline bool rhythmOk (const int* slots, int n) noexcept
{
    bool hit[16] {};
    int kicks = 0;
    for (int i = 0; i < n; ++i)
    {
        hit[slots[i]] = true;
        kicks += kickSlot (slots[i]) ? 1 : 0;
    }
    if (kicks > 1)
        return false;
    for (int beat = 0; beat < 16; beat += 4)
        if (hit[beat + 1] && hit[beat + 2] && hit[beat + 3])
            return false;
    for (int s = 0; s + 3 < 16; ++s)
        if (hit[s] && hit[s + 1] && hit[s + 2] && hit[s + 3])
            return false;
    return true;
}

inline int drawSlots (Rng& r, int n, float bounce, int lo, int* out, const int* keep, int nKeep)
{
    for (int attempt = 0; attempt < 24; ++attempt)
    {
        float w[16];
        for (int s = 0; s < 16; ++s)
            w[s] = s < lo ? 0.f : slotWeight (s, bounce);
        int m = 0;
        for (int i = 0; i < nKeep; ++i)
        {
            out[m++] = keep[i];
            w[keep[i]] = 0.f;
        }
        for (int k = 0; k < n && m < 12; ++k)
        {
            const int s = pickWeighted (w, 16, r);
            if (w[s] <= 0.f)
                break;
            out[m++] = s;
            w[s] = 0.f;
        }
        std::sort (out, out + m);
        if (rhythmOk (out, m) || attempt == 23)
            return m;
    }
    return 0;
}

inline int drawBar (Rng& r, int n, float bounce, int* out)
{
    return drawSlots (r, n, bounce, 0, out, nullptr, 0);
}

struct Bar
{
    int n = 0;
    int slot[12] {};
    int semi[12] {};
};

// Pitches: root about half the time, otherwise 5th / b7 / b3 / 4th / b6, all
// natural minor. An octave bounce repeats the previous pitch class an octave
// away; its chance is the BOUNCE share.
inline void pitchBar (Bar& b, Rng& r, float pOct, int& prev, float dark)
{
    static const int kPc[] = { 0, 7, 10, 3, 5, 8 };
    // DARK leans on the b3 / b6, light on the 5th / 4th.
    const float kW[] = { 4.6f, 2.2f - 1.2f * dark, 1.4f, 0.4f + 1.2f * dark, 1.0f - 0.8f * dark, 0.05f + 0.6f * dark };
    for (int i = 0; i < b.n; ++i)
    {
        int semi;
        if (prev != 999 && r.next() < pOct)
            semi = prev <= 5 ? prev + 12 : prev - 12;
        else
        {
            const int pc = kPc[pickWeighted (kW, 6, r)];
            // Low register mostly; b7 / b6 sit under the root.
            semi = (pc == 10 || pc == 8) ? pc - 12 : pc;
            if (prev != 999 && prev >= 10 && r.next() < 0.5)
                semi += 12; // stay up a while after a jump
        }
        semi = std::clamp (semi, -5, 19);
        if (! minorPc (semi))
            semi = 0;
        b.semi[i] = semi;
        prev = semi;
    }
}

// The answer bar keeps the front half and redraws the back half with one note
// more or fewer, so its count always differs from the call.
inline Bar answerOf (const Bar& call, Rng& r, float bounce, float pOct, float dark)
{
    int front[12];
    int nFront = 0;
    for (int i = 0; i < call.n; ++i)
        if (call.slot[i] < 8)
            front[nFront++] = call.slot[i];
    const int back = call.n - nFront;
    int want = back + ((r.next() < 0.5 && call.n < 8) || back <= 1 ? 1 : -1);
    want = std::clamp (want, 1, 5);
    Bar b;
    int slots[12];
    for (int attempt = 0; attempt < 8; ++attempt)
    {
        const int m = drawSlots (r, want, bounce, 8, slots, front, nFront);
        if (m != call.n || attempt == 7)
        {
            b.n = m;
            break;
        }
    }
    // Front half keeps the call's pitches; the back half is a new walk.
    int prev = 999;
    for (int i = 0; i < b.n; ++i)
    {
        b.slot[i] = slots[i];
        b.semi[i] = 0;
    }
    for (int i = 0, k = 0; i < b.n; ++i)
        if (b.slot[i] < 8)
        {
            while (k < call.n && call.slot[k] != b.slot[i])
                ++k;
            b.semi[i] = k < call.n ? call.semi[k] : 0;
            prev = b.semi[i];
        }
    Bar tail;
    for (int i = 0; i < b.n; ++i)
        if (b.slot[i] >= 8)
            tail.slot[tail.n++] = b.slot[i];
    pitchBar (tail, r, pOct, prev, dark);
    for (int i = 0, k = 0; i < b.n; ++i)
        if (b.slot[i] >= 8)
            b.semi[i] = tail.semi[k++];
    return b;
}

inline void repitchOne (Bar& b, Rng& r)
{
    if (b.n < 2)
        return;
    static const int kPc[] = { 7, -2, 3, 12, 5 };
    const int i = 1 + (int) (r.next() * (b.n - 1));
    for (int t = 0; t < 4; ++t)
    {
        const int semi = kPc[(int) (r.next() * 5)];
        if (semi != b.semi[i])
        {
            b.semi[i] = semi;
            return;
        }
    }
}

// Bar 8 turns: keep the first half, then one held note (5th, b7 or the octave)
// that runs to step 14. The count changes against bar 7.
inline Bar turnOf (const Bar& call, Rng& r)
{
    Bar b;
    for (int i = 0; i < call.n; ++i)
        if (call.slot[i] < 8)
        {
            b.slot[b.n] = call.slot[i];
            b.semi[b.n] = call.semi[i];
            ++b.n;
        }
    static const int kHold[] = { 7, -2, 12, 3 };
    static const float kW[] = { 1.2f, 1.0f, 0.8f, 0.5f };
    const int slot = r.next() < 0.5 ? 9 : 10;
    // (The held note is cut at step 11 after legato: bar 8 rests 4/16 before bar 1.)
    b.slot[b.n] = slot;
    b.semi[b.n] = kHold[pickWeighted (kW, 4, r)];
    ++b.n;
    if (b.n == call.n && b.n > 2)
    {
        // Keep the held note, lose the last front-half note.
        b.slot[b.n - 2] = b.slot[b.n - 1];
        b.semi[b.n - 2] = b.semi[b.n - 1];
        --b.n;
    }
    return b;
}

inline void putBar (Lane& lane, const Bar& b, int bar, int bars)
{
    for (int i = 0; i < b.n && lane.count < kMaxNotes; ++i)
    {
        if (bar >= bars)
            return;
        Note n;
        n.start = (float) (bar * 16 + b.slot[i]);
        n.length = 1.f;
        n.chord[0] = (std::int8_t) b.semi[i];
        n.nChord = 1;
        n.vel = (std::uint8_t) ((b.slot[i] & 3) == 2 ? 112 : 98);
        lane.notes[lane.count++] = n;
    }
}

// Octave share of consecutive moves, steered to a BOUNCE target that stays
// inside the reference bands (0-8.5% or 15-26%): low BOUNCE 3-8%, from 0.3 up
// 16-25%. Moves are re-pitched by octave (same pitch class), so the line and
// the key are unchanged.
inline float octaveTarget (float bounce) noexcept
{
    bounce = std::clamp (bounce, 0.f, 1.f);
    return bounce < 0.3f ? lerp (0.03f, 0.07f, bounce / 0.3f) : lerp (0.17f, 0.235f, (bounce - 0.3f) / 0.7f);
}

inline void shapeOctaves (Lane& lane, float target, Rng& r)
{
    sortLane (lane);
    auto isOct = [&] (int i) { return std::abs (lane.notes[i].chord[0] - lane.notes[i - 1].chord[0]) == 12; };
    for (int pass = 0; pass < 64; ++pass)
    {
        const int moves = lane.count - 1;
        if (moves <= 0)
            return;
        int oct = 0;
        for (int i = 1; i < lane.count; ++i)
            oct += isOct (i) ? 1 : 0;
        const float share = (float) oct / (float) moves;
        const float tol = 0.5f / (float) moves;
        if (std::fabs (share - target) <= std::max (0.02f, tol))
            return;
        const int start = 1 + (int) (r.next() * moves);
        bool done = false;
        for (int k = 0; k < moves && ! done; ++k)
        {
            const int i = 1 + (start - 1 + k) % moves;
            const int prev = lane.notes[i - 1].chord[0];
            const int semi = lane.notes[i].chord[0];
            if (share > target && isOct (i))
            {
                // Bring it back to the previous note's register.
                const int to = semi > prev ? semi - 12 : semi + 12;
                const bool nextOk = i + 1 >= lane.count || std::abs (lane.notes[i + 1].chord[0] - to) != 12;
                if (to >= -5 && to <= 19 && nextOk)
                {
                    lane.notes[i].chord[0] = (std::int8_t) to;
                    done = true;
                }
            }
            else if (share < target && pcOf (semi) == pcOf (prev) && semi == prev)
            {
                const int to = prev <= 5 ? prev + 12 : prev - 12;
                if (to >= -5 && to <= 19)
                {
                    lane.notes[i].chord[0] = (std::int8_t) to;
                    done = true;
                }
            }
        }
        if (! done)
            return;
    }
}

// Adds one in-key note in the widest back-half gap of `bar`, off the kick.
inline bool addInBar (Lane& lane, int bar, Rng& r)
{
    const float origin = (float) (bar * 16);
    float bestAt = -1.f, bestGap = 0.f;
    for (int s = 9; s < 16; ++s)
    {
        if (kickSlot (s))
            continue;
        const float t = origin + (float) s;
        if (sounding (lane, t) || onsetAt (lane, t))
            continue;
        float gap = 0.f;
        while (t + gap < origin + 16.f && ! sounding (lane, t + gap) && (gap == 0.f || ! onsetAt (lane, t + gap)))
            gap += 1.f;
        if (gap > bestGap)
        {
            bestGap = gap;
            bestAt = t;
        }
    }
    if (bestAt < 0.f || lane.count >= kMaxNotes)
        return false;
    static const int kTone[] = { 0, -2, 7, 3 };
    Note n;
    n.start = bestAt;
    n.length = std::max (0.5f, std::min (2.f, bestGap));
    n.chord[0] = (std::int8_t) kTone[(int) (r.next() * 4)];
    n.nChord = 1;
    n.vel = 104;
    lane.notes[lane.count++] = n;
    sortLane (lane);
    return true;
}

// Drops the shortest note of `bar` that is not its first and not a pickup.
inline bool dropInBar (Lane& lane, int bar, float loop)
{
    const float origin = (float) (bar * 16);
    int drop = -1;
    float shortest = 1e9f;
    bool first = true;
    for (int i = 0; i < lane.count; ++i)
    {
        const float s = lane.notes[i].start;
        if (s < origin || s >= origin + 16.f)
            continue;
        if (first)
        {
            first = false;
            continue;
        }
        if (s >= loop - 1.f - 1e-3f)
            continue;
        if (lane.notes[i].length < shortest)
        {
            shortest = lane.notes[i].length;
            drop = i;
        }
    }
    if (drop < 0)
        return false;
    for (int i = drop; i + 1 < lane.count; ++i)
        lane.notes[i] = lane.notes[i + 1];
    --lane.count;
    return true;
}

// Scale-step voicings over a root degree (index into the 7-note mode).
struct Voicing
{
    int n;
    int steps[4];
    float w;
};

inline int modeOf (const Lane& bass) noexcept
{
    bool six = false, flatTwo = false;
    for (int i = 0; i < bass.count; ++i)
    {
        const int pc = pcOf (bass.notes[i].chord[0]);
        six = six || pc == 9;
        flatTwo = flatTwo || pc == 1;
    }
    return six ? 0 : (flatTwo ? 2 : 1); // dorian, aeolian, phrygian
}

inline constexpr int kModes[3][7] = {
    { 0, 2, 3, 5, 7, 9, 10 },
    { 0, 2, 3, 5, 7, 8, 10 },
    { 0, 1, 3, 5, 7, 8, 10 },
};

inline int degSemi (int degree, int mode) noexcept
{
    const int oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
    return oct * 12 + kModes[mode][degree - oct * 7];
}
} // namespace detail

using namespace detail;

inline bool inMinor (int semi) noexcept
{
    return minorPc (semi);
}

inline void bouncyBass (Lane& lane, int bars, std::uint32_t seed, float dark, float bounce, float busy)
{
    bars = std::clamp (bars, 2, kMaxBars);
    dark = std::clamp (dark, 0.f, 1.f);
    const std::uint32_t keptSeed = lane.seed ? lane.seed : seed;
    lane.count = 0;
    lane.seed = keptSeed;
    Rng r (mix (seed, 0xB0B5u));
    const int n = std::clamp ((int) std::lround (lerp (4.6f, 7.6f, busy) + (r.next() - 0.5) * 0.9), 4, 8);
    const float pOct = lerp (0.05f, 0.50f, std::clamp (bounce, 0.f, 1.f));

    Bar a;
    a.n = drawBar (r, n, bounce, a.slot);
    int prev = 999;
    pitchBar (a, r, pOct, prev, dark);
    if (a.n > 0)
        a.semi[0] = 0; // each phrase starts on the root
    Bar b = answerOf (a, r, bounce, pOct, dark);
    Bar a2 = a;
    repitchOne (a2, r);
    Bar b4 = b;
    repitchOne (b4, r);
    Bar b6 = answerOf (a, r, bounce, pOct, dark);
    Bar b8 = turnOf (a, r);

    const Bar* arc8[8] = { &a, &b, &a, &b4, &a2, &b6, &a, &b8 };
    const Bar* arc4[4] = { &a, &b, &a, &b8 };
    const Bar* arc2[2] = { &a, &b8 };
    for (int bar = 0; bar < bars; ++bar)
    {
        const Bar* src = bars >= 8 ? arc8[bar & 7] : (bars >= 4 ? arc4[bar & 3] : arc2[bar & 1]);
        putBar (lane, *src, bar, bars);
    }
    legatoBass (lane, bars, 0.12f, 6.f);

    // Bar 8 turns: nothing sounds after step 11, so the loop rests 4/16 before
    // bar 1 (a pickup may still land on step 15).
    const float origin = (float) ((bars - 1) * 16);
    int w = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        Note note = lane.notes[i];
        if (note.start >= origin + 11.f)
            continue;
        if (note.start >= origin && note.start + note.length > origin + 11.f)
            note.length = origin + 11.f - note.start;
        lane.notes[w++] = note;
    }
    lane.count = w;
}

// BOUNCY after the cross-roll twin nudge (separateTwinRhythm) and the other
// shared bass passes: notes may have moved, so the line is made legato again
// and bar 8 keeps its 4/16 rest. Pitches and onsets are not touched.
inline void relegato (Lane& lane, int bars)
{
    if (lane.count <= 0)
        return;
    bars = std::clamp (bars, 2, kMaxBars);
    legatoBass (lane, bars, 0.12f, 6.f);
    const float origin = (float) ((bars - 1) * 16);
    for (int i = 0; i < lane.count; ++i)
    {
        Note& note = lane.notes[i];
        if (note.start >= origin && note.start < origin + 11.f && note.start + note.length > origin + 11.f)
            note.length = std::max (0.5f, origin + 11.f - note.start);
    }
}

inline void deepArc (Lane& lane, int bars, std::uint32_t seed, float busy)
{
    if (bars < 4 || lane.count <= 0)
        return;
    sortLane (lane);
    Rng r (mix (seed, 0xDEE9u));

    // Bar 5 (a copy of bar 1): one pitch moves. Rhythm stays, so the loop still repeats.
    if (bars >= 8)
    {
        static const int kTone[] = { 0, -2, 7, 3, 5 };
        int idx[16];
        int m = 0;
        for (int i = 0; i < lane.count && m < 16; ++i)
        {
            const float s = lane.notes[i].start;
            if (s > 64.f + 0.5f && s < 80.f)
                idx[m++] = i;
        }
        if (m > 0)
        {
            Note& n = lane.notes[idx[(int) (r.next() * m)]];
            for (int t = 0; t < 4; ++t)
            {
                const int semi = kTone[(int) (r.next() * 5)];
                if (semi != n.chord[0])
                {
                    n.chord[0] = (std::int8_t) semi;
                    break;
                }
            }
        }
        sortLane (lane);
    }

    // Answers on bars 4 and 8 change the note count against bars 3 and 7.
    for (int ans : { 3, 7 })
    {
        if (ans >= bars)
            continue;
        const int have = countInBar (lane, ans);
        if (have != countInBar (lane, ans - 1))
            continue;
        const bool add = have <= 3 || (have < 6 && r.next() < 0.35f + 0.4f * busy);
        if (! (add && addInBar (lane, ans, r)))
            dropInBar (lane, ans, (float) (bars * 16));
    }
    // Keep it monophonic after the moves.
    sortLane (lane);
    for (int i = 0; i + 1 < lane.count; ++i)
        if (lane.notes[i].start + lane.notes[i].length > lane.notes[i + 1].start)
            lane.notes[i].length = std::max (0.5f, lane.notes[i + 1].start - lane.notes[i].start);
}

inline void deepStabVoicing (Lane& stab, const Lane& bass, std::uint32_t seed)
{
    static const Voicing kDeep[] = {
        { 4, { 0, 2, 4, 6 }, 1.0f },  // minor 7
        { 4, { 2, 4, 6, 8 }, 1.0f },  // 9 shell (3 5 7 9)
        { 4, { 0, 3, 6, 9 }, 0.8f },  // 11 (1 4 7 b3')
        { 3, { 0, 1, 4, 0 }, 0.7f },  // sus2
        { 4, { 0, 1, 4, 6 }, 0.7f },  // sus2 + 7
        { 3, { 0, 4, 9, 0 }, 0.9f },  // open 5th + b3 on top
    };
    constexpr int kN = (int) (sizeof (kDeep) / sizeof (kDeep[0]));
    if (stab.count <= 0)
        return;
    Rng r (mix (seed, 0x57ABu));
    const int mode = modeOf (bass);

    // A roll keeps a palette of three shapes so the bars hang together.
    int palette[3];
    float w[kN];
    for (int i = 0; i < kN; ++i)
        w[i] = kDeep[i].w;
    for (int k = 0; k < 3; ++k)
    {
        palette[k] = pickWeighted (w, kN, r);
        w[palette[k]] = 0.f;
    }

    int lastBar = -1, voice = palette[0];
    for (int i = 0; i < stab.count; ++i)
    {
        Note& n = stab.notes[i];
        const int bar = (int) std::floor (n.start / 16.f + 1e-4f);
        if (bar != lastBar)
        {
            voice = palette[(int) (r.next() * 3)];
            lastBar = bar;
        }
        int lowest = n.chord[0];
        for (int c = 1; c < n.nChord && c < 4; ++c)
            lowest = std::min (lowest, (int) n.chord[c]);
        // Root degree in the mode, snapped down to the nearest scale tone.
        const int octave = (int) std::floor ((float) lowest / 12.f);
        int pc = pcOf (lowest);
        int degree = 0;
        for (int d = 6; d >= 0; --d)
            if (kModes[mode][d] <= pc)
            {
                degree = d;
                break;
            }
        const Voicing& v = kDeep[voice];
        n.nChord = (std::uint8_t) v.n;
        for (int c = 0; c < v.n; ++c)
            n.chord[c] = (std::int8_t) (octave * 12 + degSemi (degree + v.steps[c], mode));
        for (int c = v.n; c < 4; ++c)
            n.chord[c] = 0;
    }
}

inline void stabsInGaps (Lane& stab, Lane& bass, int bars, float minBassSound)
{
    const float loop = (float) (bars * 16);
    sortLane (stab);
    sortLane (bass);
    for (int i = 0; i < stab.count; ++i)
    {
        Note& s = stab.notes[i];
        if (! sounding (bass, s.start))
            continue;
        const int bar = (int) std::floor (s.start / 16.f + 1e-4f);
        const float lo = (float) (bar * 16), hi = lo + 16.f;
        bool moved = false;
        for (float d : { 1.f, -1.f, 2.f, -2.f })
        {
            const float to = s.start + d;
            if (to < lo || to >= hi || to >= loop || ((int) std::lround (to) & 3) == 0)
                continue;
            if (sounding (bass, to) || onsetAt (stab, to))
                continue;
            s.start = to;
            s.length = std::min (s.length, loop - to);
            moved = true;
            break;
        }
        if (moved)
            continue;
        // End the bass note where the stab answers.
        for (int k = 0; k < bass.count; ++k)
        {
            Note& b = bass.notes[k];
            if (s.start <= b.start + 0.75f - 1e-3f || s.start >= b.start + b.length - 1e-3f)
                continue;
            const float was = b.length;
            b.length = s.start - b.start;
            if (soundShare (bass, bars) < minBassSound)
                b.length = was;
            break;
        }
    }
    sortLane (stab);
}

inline void fixAnswers (Lane& bass, int bars)
{
    sortLane (bass);
    const float loop = (float) (bars * 16);
    Rng r (mix ((std::uint32_t) bass.count * 2654435761u + bass.seed, 0xF1Au));
    for (int ans : { 3, 7 })
    {
        if (ans >= bars || ans < 1)
            continue;
        if (countInBar (bass, ans) != countInBar (bass, ans - 1))
            continue;
        if (! dropInBar (bass, ans, loop))
            addInBar (bass, ans, r);
    }
    // Note counts across the bars span at least two (a constant density reads as a loop of one bar).
    if (bars >= 4)
    {
        int lo = 99, hi = -1;
        for (int b = 0; b < bars; ++b)
        {
            lo = std::min (lo, countInBar (bass, b));
            hi = std::max (hi, countInBar (bass, b));
        }
        if (hi - lo < 2)
        {
            const int bar = 3;
            const bool up = countInBar (bass, bar) >= hi;
            if (! (up ? addInBar (bass, bar, r) : dropInBar (bass, bar, loop)))
                (void) (up ? dropInBar (bass, bar, loop) : addInBar (bass, bar, r));
        }
    }
    // Monophonic again: a note never rings past the next onset.
    sortLane (bass);
    for (int i = 0; i + 1 < bass.count; ++i)
        if (bass.notes[i].start + bass.notes[i].length > bass.notes[i + 1].start)
            bass.notes[i].length = std::max (0.5f, bass.notes[i + 1].start - bass.notes[i].start);
}

inline void steerOctaves (Lane& bass, float bounce, std::uint32_t seed)
{
    Rng r (mix (seed, 0x0C7Au));
    shapeOctaves (bass, octaveTarget (bounce), r);
}

inline void pickup (Lane& bass, int bars, std::uint32_t seed, float chance)
{
    Rng r (mix (seed, 0x91C0u));
    if (r.next() >= chance || bass.count <= 0 || bass.count >= kMaxNotes)
        return;
    const float loop = (float) (bars * 16);
    const float at = loop - 1.f;
    const float restFrom = loop - 5.f; // four silent 16ths, then the pickup
    // The bar must keep a note before the rest, or the pickup would be all of it.
    int keep = 0;
    for (int i = 0; i < bass.count; ++i)
        if (bass.notes[i].start >= loop - 16.f && bass.notes[i].start < restFrom - 0.5f)
            ++keep;
    if (keep == 0)
        return;
    int w = 0;
    for (int i = 0; i < bass.count; ++i)
    {
        Note n = bass.notes[i];
        if (n.start >= restFrom - 1e-3f)
            continue;
        if (n.start + n.length > restFrom)
            n.length = std::max (0.5f, restFrom - n.start);
        bass.notes[w++] = n;
    }
    bass.count = w;
    static const int kTo[] = { -2, -5, 12, 7, 3 };
    static const float kW[] = { 1.4f, 1.0f, 0.9f, 0.6f, 0.4f };
    Note n;
    n.start = at;
    n.length = 1.f;
    n.chord[0] = (std::int8_t) kTo[pickWeighted (kW, 5, r)];
    n.nChord = 1;
    n.vel = 96;
    bass.notes[bass.count++] = n;
    sortLane (bass);
}

} // namespace rolla::deepbouncy
