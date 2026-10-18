#include "Arrangement.h"
#include "Generator.h"
#include <algorithm>
#include <cmath>

namespace rolla::arrange
{
namespace
{
constexpr int kModes[NumModes][7] = {
    { 0, 2, 4, 5, 7, 9, 11 }, // ionian
    { 0, 2, 4, 6, 7, 9, 11 }, // lydian
    { 0, 2, 4, 5, 7, 9, 10 }, // mixolydian
    { 0, 2, 3, 5, 7, 9, 10 }, // dorian
    { 0, 2, 3, 5, 7, 8, 10 }, // aeolian
    { 0, 1, 3, 5, 7, 8, 10 }, // phrygian
};

struct Voicing
{
    const char* id;
    std::int8_t iv[4]; // semitones above the chord root
    int n;
    bool third;
    bool sus2;
};

// Open shapes. Only the last three state a 3rd.
constexpr Voicing kVoicings[] = {
    { "sus2_open", { 0, 7, 14, 0 }, 3, false, true },
    { "sus2_spread", { 0, 14, 19, 0 }, 3, false, true },
    { "maj7_no3", { 0, 7, 11, 14 }, 4, false, false },
    { "maj7_shell", { 0, 11, 14, 19 }, 4, false, false },
    { "sus4_b7", { 0, 5, 10, 14 }, 4, false, false },
    { "seven_open", { 0, 7, 10, 14 }, 4, false, false },
    { "sus4_open", { 0, 7, 17, 0 }, 3, false, false },
    { "add9", { 0, 7, 14, 16 }, 4, true, false },
    { "maj7", { 0, 4, 7, 11 }, 4, true, false },
    { "m9_open", { 0, 7, 10, 15 }, 4, true, false },
};
constexpr int kNumVoicings = (int) (sizeof (kVoicings) / sizeof (kVoicings[0]));

std::uint32_t mix (std::uint32_t seed, std::uint32_t salt) noexcept
{
    std::uint32_t x = seed * 0x9E3779B1u ^ (salt + 0x632BE5ABu);
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x == 0 ? 1u : x;
}

struct Weighted
{
    int v;
    float w;
};

int pick (const Weighted* items, int n, Rng& r)
{
    float sum = 0.f;
    for (int i = 0; i < n; ++i)
        sum += std::max (0.f, items[i].w);
    if (sum <= 0.f)
        return items[0].v;
    float x = (float) r.next() * sum;
    for (int i = 0; i < n; ++i)
    {
        x -= std::max (0.f, items[i].w);
        if (x <= 0.f)
            return items[i].v;
    }
    return items[n - 1].v;
}

bool voicingFits (const Voicing& v, int rootSemis, int mode) noexcept
{
    for (int i = 0; i < v.n; ++i)
        if (! inMode (rootSemis + v.iv[i], mode))
            return false;
    return true;
}

// Pick a voicing for a chord rooted on rootSemis. Returns -1 when none fits.
int pickVoicing (int rootSemis, int mode, float thirdChance, bool preferSus2, int avoid, Rng& r)
{
    Weighted items[kNumVoicings];
    int n = 0;
    for (int i = 0; i < kNumVoicings; ++i)
    {
        const Voicing& v = kVoicings[i];
        if (i == avoid || ! voicingFits (v, rootSemis, mode))
            continue;
        float w = v.third ? thirdChance : 1.f;
        if (preferSus2 && v.sus2)
            w *= 2.5f;
        if (w > 0.f)
            items[n++] = { i, w };
    }
    if (n == 0)
        return -1;
    return pick (items, n, r);
}

Chord buildChord (int rootSemis, int voicing, int reg) noexcept
{
    Chord c;
    const Voicing& v = kVoicings[voicing];
    c.voicing = voicing;
    c.statesThird = v.third;
    c.n = v.n;
    int base = rootSemis;
    while (base < reg - 5)
        base += 12;
    while (base > reg + 6)
        base -= 12;
    c.root = base;
    for (int i = 0; i < v.n; ++i)
        c.notes[i] = (std::int8_t) (base + v.iv[i]);
    return c;
}

// Chord on a scale degree with a fitting voicing; walks to the next degree if none fits.
bool chordOn (int degree, int mode, const HarmonyOptions& o, bool preferSus2, int avoidVoicing, Rng& r, Chord& out)
{
    const int root = degreeToSemis (degree, mode);
    const int vi = pickVoicing (root, mode, o.thirdChance, preferSus2, avoidVoicing, r);
    if (vi < 0)
        return false;
    out = buildChord (root, vi, o.register_);
    return true;
}

void sortLane (Lane& lane) noexcept
{
    std::sort (lane.notes, lane.notes + lane.count, [] (const Note& a, const Note& b) { return a.start < b.start; });
}

} // namespace

int degreeToSemis (int degree, int mode) noexcept
{
    const int m = std::clamp (mode, 0, NumModes - 1);
    const int oct = (degree >= 0) ? degree / 7 : -((-degree + 6) / 7);
    return oct * 12 + kModes[m][degree - oct * 7];
}

bool inMode (int semis, int mode) noexcept
{
    const int pc = ((semis % 12) + 12) % 12;
    for (int v : kModes[std::clamp (mode, 0, NumModes - 1)])
        if (v == pc)
            return true;
    return false;
}

int numVoicings() noexcept
{
    return kNumVoicings;
}

TurnaroundPlan planTurnaround (std::uint32_t seed, int loopBars, const TurnaroundOptions& o)
{
    Rng r (mix (seed, 0x7A11u));
    TurnaroundPlan p;
    const int bars = std::clamp (loopBars, 1, kMaxBars);
    const int phrase = std::max (1, o.phraseBars);

    auto add = [&] (int bar, float chance)
    {
        if (p.count >= 4 || r.next() >= chance)
            return;
        p.bars[p.count] = bar;
        p.type[p.count] = r.next() < o.fullShare ? GapType::Full : GapType::KickDip;
        ++p.count;
    };
    if (bars < phrase)
        add (bars - 1, o.chance * 0.5f); // short loops: a turnaround every repeat would wear thin
    else
        for (int b = phrase - 1; b < bars; b += phrase)
            add (b, o.chance);

    const Weighted approach[] = { { 1, 1.0f }, { 3, 0.7f }, { 4, 0.6f }, { -1, 0.6f } };
    const Weighted home[] = { { 0, 0.85f }, { 4, 0.15f } };
    const Weighted split[] = { { 8, 1.0f }, { 12, 0.5f }, { 6, 0.3f } };
    p.heldDegree[0] = pick (approach, 4, r);
    p.heldDegree[1] = pick (home, 2, r);
    if (p.heldDegree[1] == p.heldDegree[0])
        p.heldDegree[1] = 0;
    p.splitStep = pick (split, 3, r);
    p.lift = r.next() < o.liftOctave;
    return p;
}

void applyTurnaround (const TurnaroundPlan& p, Lane& bass, DrumLane& drums, int loopBars, int mode) noexcept
{
    const int loop = std::clamp (loopBars, 1, kMaxBars) * 16;
    for (int t = 0; t < p.count; ++t)
    {
        const int a = p.bars[t] * 16;
        const int b = a + 16;
        if (a < 0 || b > loop)
            continue;
        if (p.type[t] == GapType::KickDip)
        {
            for (int s = a + 8; s < b; ++s)
                drums.vel[Kick][s] = 0;
            continue;
        }
        if (p.type[t] != GapType::Full)
            continue;

        for (int s = a; s < b; ++s)
            drums.vel[Kick][s] = 0;

        // Drop the riff in this bar and cut anything ringing into it.
        int w = 0;
        for (int i = 0; i < bass.count; ++i)
        {
            Note n = bass.notes[i];
            if (n.start >= (float) a && n.start < (float) b)
                continue;
            if (n.start < (float) a && n.start + n.length > (float) a)
                n.length = (float) a - n.start;
            bass.notes[w++] = n;
        }
        bass.count = w;

        const int lift = p.lift ? 12 : 0;
        const int split = std::clamp (p.splitStep, 2, 14);
        const int starts[2] = { a, a + split };
        const int lens[2] = { split, 16 - split };
        for (int k = 0; k < 2 && bass.count < kMaxNotes; ++k)
        {
            Note n;
            n.start = (float) starts[k];
            n.length = (float) lens[k];
            n.chord[0] = (std::int8_t) (degreeToSemis (p.heldDegree[k], mode) + lift);
            n.nChord = 1;
            n.vel = 100;
            bass.notes[bass.count++] = n;
        }
        sortLane (bass);
    }
}

HarmonyLoop makeSevenPlusOne (std::uint32_t seed, int mode, const HarmonyOptions& o)
{
    Rng r (mix (seed, 0x7E1Du));
    HarmonyLoop h;
    h.mode = std::clamp (mode, 0, NumModes - 1);

    // Home: often the 5th over a root bass ("maj7 over the root"), else the root, 4th or 6th.
    const Weighted homeDeg[] = { { 4, 1.0f }, { 0, 0.8f }, { 3, 0.4f }, { 5, 0.3f } };
    Chord home;
    int hd = pick (homeDeg, 4, r);
    for (int tries = 0; ! chordOn (hd, h.mode, o, false, -1, r, home) && tries < 7; ++tries)
        hd = (hd + 1) % 7;

    // Bar 8: a different open chord, sus2 shapes preferred.
    Weighted turnDeg[5] = { { 0, 1.0f }, { 1, 0.6f }, { 3, 0.6f }, { 4, 0.5f }, { 6, 0.3f } };
    for (auto& t : turnDeg)
        if (t.v == hd)
            t.w = 0.f;
    Chord turn;
    int td = pick (turnDeg, 5, r);
    for (int tries = 0; (td == hd || ! chordOn (td, h.mode, o, true, -1, r, turn)) && tries < 7; ++tries)
        td = (td + 1) % 7;

    for (int b = 0; b < 7; ++b)
        h.bar[b] = home;
    h.bar[7] = turn;

    // Optional bar-4 colour: same root, another shape without the 3rd.
    if (r.next() < o.midVariation)
    {
        HarmonyOptions noThird = o;
        noThird.thirdChance = 0.f;
        Chord alt;
        if (chordOn (hd, h.mode, noThird, false, home.voicing, r, alt))
            h.bar[3] = alt;
    }
    return h;
}

int writeHarmony (const HarmonyLoop& h, Lane& lane, int loopBars, ChordRhythm rhythm) noexcept
{
    lane.count = 0;
    const int bars = std::clamp (loopBars, 1, kMaxBars);
    for (int b = 0; b < bars; ++b)
    {
        // A loop shorter than 8 bars still ends on the turn chord.
        const int idx = (b == bars - 1) ? 7 : (b % 7);
        const Chord& c = h.bar[idx];
        auto put = [&] (float start, float len)
        {
            if (lane.count >= kMaxNotes)
                return;
            Note n;
            n.start = start;
            n.length = len;
            n.nChord = (std::uint8_t) std::min (4, c.n);
            for (int i = 0; i < n.nChord; ++i)
                n.chord[i] = c.notes[i];
            n.vel = 96;
            lane.notes[lane.count++] = n;
        };
        if (rhythm == ChordRhythm::Hold)
            put ((float) (b * 16), 16.f);
        else
            for (int s : { 2, 6, 10, 14 })
                put ((float) (b * 16 + s), 1.f);
    }
    return lane.count;
}

} // namespace rolla::arrange
