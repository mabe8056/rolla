// rolla-phrase-test: rule composer checks (RuleComposer.h). No JUCE.
//
//   rolla-phrase-test [--seeds a,b,c]
//
// Deterministic: a fixed set of seed bases (the CI gate bases the roll tests
// use) plus a fixed seed sweep. No random_device, so a failure always replays.
// Extra bases can be added with --seeds. Nothing here tunes constants against
// particular seeds.

#include "Generator/RuleComposer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace rolla;

namespace
{
int failures = 0;

void check (bool ok, const char* what, const std::string& detail = {})
{
    std::printf ("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
    if (! ok)
        ++failures;
}

const char* kNames[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };

constexpr int kRolls = 200;
constexpr int kBlock = 20;
constexpr int kMaxPairsPerBlock = 2;
constexpr float kNearIdentical = 0.85f;

// Same derivation as rolla-rolls: base + roll * 0x9E3779B9 + dialect * 101.
std::uint32_t rollSeed (std::uint32_t base, int roll, int dialect)
{
    return base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) dialect * 101u;
}

int pcOf (int semi)
{
    const int pc = semi % 12;
    return pc < 0 ? pc + 12 : pc;
}

// Rhythm + pitch outline of bars 1-2.
struct Skeleton
{
    std::uint32_t onsets = 0;
    std::int8_t pitch[32] {};
};

Skeleton skeletonOf (const Lane& lane)
{
    Skeleton k;
    for (int i = 0; i < lane.count; ++i)
    {
        const int s = (int) std::lround (lane.notes[i].start);
        if (s < 0 || s >= 32)
            continue;
        k.onsets |= 1u << s;
        k.pitch[s] = (std::int8_t) pcOf (lane.notes[i].chord[0]);
    }
    return k;
}

int pop32 (std::uint32_t x)
{
    int c = 0;
    while (x)
    {
        x &= x - 1;
        ++c;
    }
    return c;
}

// Onset Jaccard scaled by how many shared onsets carry the same pitch class.
float similarity (const Skeleton& a, const Skeleton& b)
{
    const std::uint32_t inter = a.onsets & b.onsets;
    const int nu = pop32 (a.onsets | b.onsets);
    if (nu == 0)
        return 1.f;
    const int ni = pop32 (inter);
    int same = 0;
    for (int s = 0; s < 32; ++s)
        if (((inter >> s) & 1u) && a.pitch[s] == b.pitch[s])
            ++same;
    const float j = (float) ni / (float) nu;
    return j * (0.5f + 0.5f * (ni > 0 ? (float) same / (float) ni : 0.f));
}

int worstBlockPairs (const std::vector<Skeleton>& sk, float& worstSim)
{
    int worst = 0;
    worstSim = 0.f;
    for (int b = 0; b + kBlock <= (int) sk.size(); b += kBlock)
    {
        int pairs = 0;
        for (int i = b; i < b + kBlock; ++i)
            for (int j = i + 1; j < b + kBlock; ++j)
            {
                const float s = similarity (sk[(size_t) i], sk[(size_t) j]);
                worstSim = std::max (worstSim, s);
                if (s >= kNearIdentical)
                    ++pairs;
            }
        worst = std::max (worst, pairs);
    }
    return worst;
}

int distinctShapes (const std::vector<Skeleton>& sk)
{
    std::vector<Skeleton> reps;
    for (const auto& s : sk)
    {
        bool found = false;
        for (const auto& r : reps)
            if (similarity (s, r) >= kNearIdentical)
            {
                found = true;
                break;
            }
        if (! found)
            reps.push_back (s);
    }
    return (int) reps.size();
}

// Hard rules for a composed bass lane. `why` names the first failure.
bool hardRules (const Lane& lane, int bars, int mode, std::string& why)
{
    const float loopEnd = (float) (bars * 16);
    int onKick = 0;
    bool barHas[8] {};
    for (int i = 0; i < lane.count; ++i)
    {
        const Note& n = lane.notes[i];
        if (! phrase::inMode (n.chord[0], mode))
        {
            why = "out of mode: " + std::to_string (n.chord[0]);
            return false;
        }
        if (n.start < 0.f || n.start + n.length > loopEnd + 1.0e-3f || n.length < 0.25f)
        {
            why = "note outside the loop at " + std::to_string (n.start);
            return false;
        }
        if (i + 1 < lane.count && n.start + n.length > lane.notes[i + 1].start + 1.0e-3f)
        {
            why = "overlap at " + std::to_string (n.start);
            return false;
        }
        const int st = (int) std::lround (n.start);
        if (st % 4 == 0)
            ++onKick;
        if (st / 16 < 8)
            barHas[st / 16] = true;
        if (st / 16 == bars - 1 && st % 16 >= 12)
        {
            why = "attack in the bar-8 turn rest at " + std::to_string (st);
            return false;
        }
    }
    for (int b = 0; b < bars; ++b)
        if (! barHas[b])
        {
            why = "empty bar " + std::to_string (b + 1);
            return false;
        }
    if (lane.count > 0 && (float) onKick > 0.35f * (float) lane.count + 1.0e-4f)
    {
        why = std::to_string (onKick) + " of " + std::to_string (lane.count) + " attacks on kick steps";
        return false;
    }
    if (rules::thinRepeatedBackHalf (lane, bars))
    {
        why = "thin repeated back half";
        return false;
    }
    return true;
}

void runSet (int dialect, std::uint32_t base, const std::string& label, int rolls)
{
    for (int bars : { 8, 4, 2 })
    {
        std::vector<Skeleton> sk;
        int hardFails = 0;
        std::string firstWhy;
        for (int roll = 0; roll < rolls; ++roll)
        {
            const std::uint32_t seed = rollSeed (base, roll, dialect);
            rules::BassParams p;
            p.dialect = dialect;
            p.bars = bars;
            p.dark = (float) ((roll * 37) % 101) / 100.f; // a fixed spread over DARK
            p.mode = rules::drawMode (dialect, p.dark, seed);
            Lane lane;
            rules::composeBass (lane, p, seed);
            std::string why;
            if (! hardRules (lane, bars, p.mode, why) && hardFails++ == 0)
                firstWhy = "seed " + std::to_string (seed) + ": " + why;
            sk.push_back (skeletonOf (lane));
        }
        char d[256];
        std::snprintf (d, sizeof d, "%s %s %d-bar: hard fails %d/%d %s", kNames[dialect], label.c_str(), bars, hardFails,
                       rolls, firstWhy.c_str());
        check (hardFails == 0, "hard rules (mode, loop, overlap, empty bar, turn rest, kick <= 35%, back-half guard)", d);
        if (rolls >= kBlock)
        {
            float ws = 0.f;
            const int worst = worstBlockPairs (sk, ws);
            std::snprintf (d, sizeof d, "%s %s %d-bar: worst block %d pairs (max sim %.2f)", kNames[dialect], label.c_str(),
                           bars, worst, ws);
            check (worst <= kMaxPairsPerBlock, "<=2 near-identical skeletons per 20 rolls", d);
        }
        if (rolls >= kRolls)
        {
            const int shapes = distinctShapes (sk);
            std::snprintf (d, sizeof d, "%s %s %d-bar: %d distinct shapes in %d rolls", kNames[dialect], label.c_str(), bars,
                           shapes, rolls);
            check (shapes >= 120, ">=120 distinct bar 1-2 shapes in 200 rolls", d);
        }
    }
}

// Stabs: in mode, never on a bass attack outside BOUNCY, at most perBar hits a bar.
void runStabs (int dialect, std::uint32_t base)
{
    int onBass = 0;
    int outOfMode = 0;
    int overCap = 0;
    int hits = 0;
    for (int roll = 0; roll < 100; ++roll)
    {
        const std::uint32_t seed = rollSeed (base, roll, dialect);
        rules::BassParams p;
        p.dialect = dialect;
        p.mode = rules::drawMode (dialect, 0.55f, seed);
        Lane bass;
        rules::composeBass (bass, p, seed);
        rules::StabParams sp;
        sp.dialect = dialect;
        sp.mode = p.mode;
        sp.perBar = 3;
        Lane stab;
        rules::composeStab (stab, bass, sp, seed ^ 0x5717u);
        int perBar[8] {};
        for (int i = 0; i < stab.count; ++i)
        {
            const Note& n = stab.notes[i];
            ++hits;
            const int st = (int) std::lround (n.start);
            if (st / 16 < 8)
                ++perBar[st / 16];
            for (int k = 0; k < bass.count; ++k)
                if ((int) std::lround (bass.notes[k].start) == st)
                    ++onBass;
            for (int c = 0; c < n.nChord && c < 4; ++c)
                if (! phrase::inMode (n.chord[c], p.mode))
                    ++outOfMode;
        }
        for (int b = 0; b < 8; ++b)
            if (perBar[b] > sp.perBar)
                ++overCap;
    }
    char d[200];
    std::snprintf (d, sizeof d, "%s: %d hits, %d on a bass attack, %d out-of-mode tones, %d bars over the cap", kNames[dialect],
                   hits, onBass, outOfMode, overCap);
    check (hits > 0 && outOfMode == 0 && overCap == 0 && (dialect == Pop || onBass == 0),
           "stabs in mode, under the cap, off the bass attacks (BOUNCY excepted)", d);
}

void runBasics()
{
    rules::BassParams p;
    p.dialect = Long;
    Lane a;
    Lane b;
    rules::composeBass (a, p, 1234u);
    rules::composeBass (b, p, 1234u);
    bool same = a.count == b.count;
    for (int i = 0; same && i < a.count; ++i)
        same = a.notes[i].start == b.notes[i].start && a.notes[i].chord[0] == b.notes[i].chord[0]
               && a.notes[i].length == b.notes[i].length;
    check (same, "deterministic for a seed");

    check (phrase::degreeToSemis (-1, phrase::Aeolian) == -2 && phrase::degreeToSemis (-3, phrase::Aeolian) == -5
               && phrase::degreeToSemis (7, phrase::Dorian) == 12 && phrase::degreeToSemis (-7, phrase::Phrygian) == -12
               && phrase::degreeToSemis (2, phrase::Phrygian) == 3 && phrase::degreeToSemis (5, phrase::Dorian) == 9,
           "degree -> semitone mapping");
    check (rules::drawMode (Gaskin, 0.1f, 99u) == phrase::Phrygian, "JACKIN is phrygian");

    // The guard itself: a back half of single-pitch-class bars with 2-3 attacks.
    Lane thin;
    for (int bar = 0; bar < 8; ++bar)
        for (int slot : { 2, 5, 9, 13 })
        {
            if (bar >= 4 && slot > 5 && ! (slot == 9 && (bar & 1)))
                continue;
            Note& n = thin.notes[thin.count++];
            n.start = (float) (bar * 16 + slot);
            n.length = 1.f;
            n.chord[0] = (std::int8_t) (bar >= 4 ? 0 : (slot == 9 ? 7 : 0));
            n.vel = 100;
        }
    check (rules::thinRepeatedBackHalf (thin, 8), "back-half guard fires on a thin repeated-root back half");
    Lane full;
    rules::composeBass (full, p, 77u);
    check (! rules::thinRepeatedBackHalf (full, 8), "back-half guard stays quiet on a composed line");
}

} // namespace

int main (int argc, char** argv)
{
    std::vector<std::uint32_t> extra;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--seeds") == 0 && i + 1 < argc)
        {
            std::string v = argv[++i];
            size_t p = 0;
            while (p < v.size())
            {
                const size_t q = v.find (',', p);
                extra.push_back ((std::uint32_t) std::strtoul (v.substr (p, q - p).c_str(), nullptr, 10));
                if (q == std::string::npos)
                    break;
                p = q + 1;
            }
        }
    }

    // Fixed bases: the gate bases of the roll tests.
    const std::uint32_t fixed[] = { 1u, 77u, 913u, 423423u, 20261018u, 2026101852u, 2517220185u };
    runBasics();
    for (int dialect = 0; dialect < NumDialects; ++dialect)
    {
        for (std::uint32_t base : fixed)
            runSet (dialect, base, "base " + std::to_string (base), kRolls);
        for (std::uint32_t base : extra)
            runSet (dialect, base, "seed " + std::to_string (base), kRolls);
        // Deterministic sweep: 32 more bases, 40 rolls each.
        for (int k = 0; k < 32; ++k)
        {
            const std::uint32_t base = 0x2545F491u * (std::uint32_t) (k + 1) + 0x9E37u;
            runSet (dialect, base, "sweep " + std::to_string (k), 40);
        }
        runStabs (dialect, 2026101852u);
    }
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
