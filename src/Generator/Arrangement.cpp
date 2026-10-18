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

// True when this chord root has an open shape with no major 7th.
bool rootVoices (int rootSemis, int mode) noexcept
{
    for (int i = 0; i < kNumVoicings; ++i)
    {
        const Voicing& v = kVoicings[i];
        bool majorSeventh = false;
        for (int k = 0; k < v.n; ++k)
            if (((v.iv[k] % 12) + 12) % 12 == 11)
                majorSeventh = true;
        if (! majorSeventh && voicingFits (v, rootSemis, mode))
            return true;
    }
    return false;
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
        bool majorSeventh = false;
        for (int k = 0; k < v.n; ++k)
            if (((v.iv[k] % 12) + 12) % 12 == 11)
                majorSeventh = true;
        // The measured harmony has no major-7th layer. That note was an artifact.
        if (majorSeventh)
            continue;
        float w = v.third ? thirdChance : 1.f;
        if (preferSus2 && v.sus2)
            w *= 4.f;
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
        for (int origin = 0; origin + phrase <= bars && p.count < 4; origin += phrase)
        {
            add (origin + phrase / 2 - 1, o.chance); // bar 4 of the phrase
            add (origin + phrase - 1, o.chance);     // bar 8 of the phrase
        }

    // Home is the root or the fifth. The approach is chosen in applyTurnaround
    // so the slide is a whole step or a fourth, never a semitone.
    const Weighted home[] = { { 0, 0.85f }, { 4, 0.15f } };
    const Weighted split[] = { { 8, 1.0f }, { 12, 0.5f }, { 6, 0.3f } };
    p.heldDegree[1] = pick (home, 2, r);
    p.heldDegree[0] = p.heldDegree[1] == 0 ? 6 : 0;
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
        const int target = degreeToSemis (p.heldDegree[1], mode);
        // Whole step under the target when the mode has it, otherwise a
        // fourth or a fifth. Never a chromatic passing tone.
        int approach = target - 2;
        if (! inMode (approach, mode))
            approach = target - 5;
        if (! inMode (approach, mode))
            approach = target - 7;
        if (! inMode (approach, mode))
            approach = target;
        const int semis[2] = { approach, target };
        for (int k = 0; k < 2 && bass.count < kMaxNotes; ++k)
        {
            Note n;
            n.start = (float) starts[k];
            n.length = (float) lens[k];
            n.chord[0] = (std::int8_t) (semis[k] + lift);
            n.nChord = 1;
            n.vel = 100;
            bass.notes[bass.count++] = n;
        }
        sortLane (bass);
    }
}

int degreeOfPc (int mode, int pc) noexcept
{
    pc = (pc % 12 + 12) % 12;
    for (int d = 0; d < 7; ++d)
        if (degreeToSemis (d, mode) % 12 == pc)
            return d;
    return -1;
}

int pickCyclePc (int mode, const int* pool, int n, int spin, int avoidA, int avoidB) noexcept
{
    auto usable = [&] (int pc) noexcept
    {
        if (pc == avoidA || pc == avoidB)
            return false;
        const int degree = degreeOfPc (mode, pc);
        if (degree < 0)
            return false;
        // A scale tone with no open voicing (Phrygian's fifth) would fall
        // back onto the tonic and the chord would not change.
        return rootVoices (degreeToSemis (degree, mode), mode);
    };
    for (int k = 0; k < n; ++k)
    {
        const int pc = pool[(spin + k) % n];
        if (usable (pc))
            return pc;
    }
    for (int pc = 0; pc < 12; ++pc)
        if (usable (pc))
            return pc;
    return avoidA;
}

HarmonyLoop makeThreePlusOne (std::uint32_t seed, int mode, const HarmonyOptions& o)
{
    Rng r (mix (seed, 0x7E1Du));
    HarmonyLoop h;
    h.mode = std::clamp (mode, 0, NumModes - 1);
    h.bars = 8;

    // Three chords a fourth apart, then a resolution. 10 is the flat seven
    // when the mode has it (the measured F / Eb / Bb cycle); otherwise the
    // fourth and the fifth, which every mode has. No chromatic roots.
    const int secondPool[] = { 10, 5, 3, 8, 7 };
    const int thirdPool[] = { 5, 7, 10, 3, 2 };
    const int spin = (int) (r.next() * 5.0) % 5;
    int pc[4];
    pc[0] = degreeOfPc (h.mode, 0) >= 0 ? 0 : 7;
    pc[1] = pickCyclePc (h.mode, secondPool, 5, spin, pc[0], pc[0]);
    pc[2] = pickCyclePc (h.mode, thirdPool, 5, spin, pc[0], pc[1]);
    pc[3] = pc[0];

    HarmonyOptions sus = o;
    sus.thirdChance = std::min (o.thirdChance, 0.12f);
    int prevVoicing = -1;
    for (int i = 0; i < 4; ++i)
    {
        const int degree = degreeOfPc (h.mode, pc[i]);
        const int avoid = (i == 3) ? prevVoicing : -1;
        bool voiced = degree >= 0 && chordOn (degree, h.mode, sus, true, avoid, r, h.bar[i]);
        if (! voiced && i == 3 && degree >= 0)
            voiced = chordOn (degree, h.mode, sus, true, -1, r, h.bar[i]);
        if (! voiced && i > 0 && i < 3)
        {
            for (int d = 0; d < 7 && ! voiced; ++d)
            {
                const int rpc = (degreeToSemis (d, h.mode) % 12 + 12) % 12;
                bool used = false;
                for (int j = 0; j < i; ++j)
                    if (((h.bar[j].root % 12) + 12) % 12 == rpc)
                        used = true;
                if (! used)
                    voiced = chordOn (d, h.mode, sus, true, -1, r, h.bar[i]);
            }
        }
        if (! voiced && i > 0)
            h.bar[i] = h.bar[0];
        if (i == 0)
            prevVoicing = h.bar[0].voicing;
    }
    if (r.next() < o.midVariation)
    {
        HarmonyOptions noThird = sus;
        noThird.thirdChance = 0.f;
        Chord alt;
        const int degree = degreeOfPc (h.mode, pc[3]);
        if (chordOn (degree < 0 ? 0 : degree, h.mode, noThird, true, h.bar[3].voicing, r, alt))
            h.bar[3] = alt;
    }
    for (int i = 0; i < 4; ++i)
        h.bar[i + 4] = h.bar[i];
    return h;
}

int writeHarmony (const HarmonyLoop& h, Lane& lane, int loopBars, ChordRhythm rhythm) noexcept
{
    lane.count = 0;
    const int bars = std::clamp (loopBars, 1, kMaxBars);
    for (int b = 0; b < bars; ++b)
    {
        // The cycle is 4 bars. The last bar of a short loop still resolves.
        const int idx = (b == bars - 1) ? 3 : (b % 4);
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
