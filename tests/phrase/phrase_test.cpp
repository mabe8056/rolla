// rolla-phrase-test: rule composer checks (RuleComposer.h). No JUCE.
//
//   rolla-phrase-test [--seeds a,b,c]
//
// Deterministic: a fixed set of seed bases (the CI gate bases the roll tests
// use) plus a fixed seed sweep. No random_device, so a failure always replays.
// Extra bases can be added with --seeds. Nothing here tunes constants against
// particular seeds.

#include "Generator/RuleComposer.h"
#include "Generator/RuleTables.h"
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

// Stabs: in mode, at most perBar hits a bar. A stab may land on a bass attack
// (the model's share, as in the references) only when it agrees with that bass
// note: no m2 / m9, and no M7 outside DEEP. Most hits stay off the attacks.
void runStabs (int dialect, std::uint32_t base)
{
    int onBass = 0;
    int clashes = 0;
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
                {
                    ++onBass;
                    const int bpc = ((bass.notes[k].chord[0] % 12) + 12) % 12;
                    for (int c = 0; c < n.nChord && c < 4; ++c)
                    {
                        const int ic = ((n.chord[c] - bpc) % 12 + 12) % 12;
                        if (ic == 1 || (ic == 11 && dialect != Long))
                            ++clashes;
                    }
                }
            for (int c = 0; c < n.nChord && c < 4; ++c)
                if (! phrase::inMode (n.chord[c], p.mode))
                    ++outOfMode;
        }
        for (int b = 0; b < 8; ++b)
            if (perBar[b] > sp.perBar)
                ++overCap;
    }
    char d[200];
    std::snprintf (d, sizeof d, "%s: %d hits, %d on a bass attack (%d clashing), %d out-of-mode tones, %d bars over the cap",
                   kNames[dialect], hits, onBass, clashes, outOfMode, overCap);
    check (hits > 0 && outOfMode == 0 && overCap == 0 && clashes == 0 && onBass * 100 <= hits * 60,
           "stabs in mode, under the cap; on a bass attack only when consonant, and mostly off them", d);
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
    // Genre model v1.2 (A&R fix 561): auto rolls stay inside the scorer's scale,
    // so no dorian (or mixolydian) in any style. BOUNCY is aeolian only; the
    // other styles draw aeolian and phrygian.
    for (int dialect = 0; dialect < NumDialects; ++dialect)
    {
        int seen[3] {};
        for (std::uint32_t k = 0; k < 400; ++k)
            ++seen[std::clamp (rules::drawMode (dialect, (float) (k % 11) / 10.f, 1u + k * 0x9E3779B9u), 0, 2)];
        char d[160];
        std::snprintf (d, sizeof d, "%s: dorian %d aeolian %d phrygian %d of 400", kNames[dialect], seen[0], seen[1], seen[2]);
        bool ok = seen[phrase::Aeolian] > 0 && seen[phrase::Dorian] == 0;
        if (dialect == Pop)
            ok = ok && seen[phrase::Phrygian] == 0;
        else
            ok = ok && seen[phrase::Phrygian] > 0;
        check (ok, "mode weights follow the v1.2 model (no dorian in auto rolls, BOUNCY aeolian only)", d);
    }

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

float entropyOf (const float* v, int n)
{
    float s = 0.f;
    for (int i = 0; i < n; ++i)
        s += v[i];
    double h = 0.0;
    for (int i = 0; i < n; ++i)
        if (v[i] > 0.f)
            h -= (v[i] / s) * std::log (v[i] / s);
    return (float) (h / std::log ((double) n));
}

// The compiled model (RuleTables.h, generated from genre_writing_model.json
// v1.2): distributions sum to 1, no transition cell over 0.60, every table at
// or over its entropy floor, single-source voicings gone, stab cap 3.
void runModelTables()
{
    using namespace rules::tables;
    int bad = 0;
    std::string why;
    for (int st = 0; st < kStyles; ++st)
    {
        for (int r = 0; r < 12; ++r)
        {
            float sb = 0.f, ss = 0.f, mb = 0.f, ms = 0.f;
            for (int c = 0; c < 12; ++c)
            {
                sb += kBassTrans[st][r][c];
                ss += kStabTrans[st][r][c];
                mb = std::max (mb, kBassTrans[st][r][c]);
                ms = std::max (ms, kStabTrans[st][r][c]);
            }
            if (std::fabs (sb - 1.f) > 0.002f || std::fabs (ss - 1.f) > 0.002f || mb > kCellCeiling + 1.0e-4f || ms > kCellCeiling + 1.0e-4f
                || entropyOf (kBassTrans[st][r], 12) < kFloorRowBass - 0.005f || entropyOf (kStabTrans[st][r], 12) < kFloorRowStab - 0.005f)
            {
                ++bad;
                why = std::string (kNames[st]) + " transition row " + std::to_string (r);
            }
        }
        for (int g = 0; g < 3; ++g)
            if (entropyOf (kBassOnset[st][g], 16) < kFloorOnsetBass - 0.005f)
            {
                ++bad;
                why = std::string (kNames[st]) + " bass onset grid " + std::to_string (g);
            }
        for (int g = 0; g < 3; ++g)
            if (entropyOf (kStabGiven[st][g], 16) < kFloorOnsetStab - 0.005f)
            {
                ++bad;
                why = std::string (kNames[st]) + " stab grid " + std::to_string (g);
            }
        float vw = 0.f;
        for (int v = 0; v < kVoicingCount[st]; ++v)
        {
            const auto& V = kStabVoicings[st][v];
            vw += V.w;
            // A&R single-source voicings, removed in v1.2.
            const int shapes[7][4] = { { 0, 2, 3, 7 }, { 0, 4, 7, 11 }, { 0, 1, 3, 11 }, { 0, 1, 3, -1 }, { 0, 5, 9, -1 }, { 0, 10, -1, -1 }, { 0, 11, -1, -1 } };
            for (const auto& sh : shapes)
            {
                int n = 0;
                while (n < 4 && sh[n] >= 0)
                    ++n;
                bool same = n == V.n;
                for (int i = 0; same && i < n; ++i)
                    same = V.iv[i] == sh[i];
                if (same)
                {
                    ++bad;
                    why = std::string (kNames[st]) + " carries a removed single-source voicing";
                }
            }
        }
        if (std::fabs (vw - 1.f) > 0.003f)
        {
            ++bad;
            why = std::string (kNames[st]) + " voicing weights do not sum to 1";
        }
        const float m = kModeWeights[st][0] + kModeWeights[st][1] + kModeWeights[st][2];
        if (std::fabs (m - 1.f) > 0.002f)
        {
            ++bad;
            why = std::string (kNames[st]) + " mode weights do not sum to 1";
        }
    }
    check (bad == 0 && kStabHitsCap <= 3 && kHatSwingLo >= 0.53f && kHatSwingHi <= 0.57f,
           "model tables: sums, 0.60 cell ceiling, entropy floors, removed voicings, stab cap 3, swing band", why);

    // v1.4 vox chop grid (catchiness research #4): one all-style grid, every weight inside the
    // model's vox floor / ceiling (0.02 / 0.40), at or over the vox onset entropy floor (0.55),
    // and the off-bass share the model's avoid_bass_onsets_pct_min (60).
    bool voxOk = entropyOf (kVoxOnset, 16) >= 0.55f && std::fabs (kVoxOffBassMin - 0.6f) < 1.0e-4f;
    for (float w : kVoxOnset)
        voxOk = voxOk && w >= kVoxOnsetFloor - 1.0e-6f && w <= 0.40f + 1.0e-6f;
    const float chopsW = kVoxChopsWeight[0] + kVoxChopsWeight[1] + kVoxChopsWeight[2];
    check (voxOk && std::fabs (chopsW - 1.f) < 0.002f, "vox grid: floor / ceiling, entropy floor, off-bass 60%, chop weights sum to 1");
}

// Composer-level v1.2 checks per style: density near the style median,
// on-kick share near its target, median length inside the guard band,
// mutations in the back half, >= 2 pitch classes in bars 5-8, one note per
// onset, the tonic guard, and stabs with no m2 / m9 against the bass under them.
void runV12 (int dialect, std::uint32_t base)
{
    using namespace rules::tables;
    double notes = 0.0;
    double kick = 0.0;
    int lenOk = 0;
    int diffBack = 0;
    int diffAll = 0;
    int thinBars = 0;
    int backBars = 0;
    int dups = 0;
    int tonicBad = 0;
    int stabHits = 0;
    int stabClash = 0;
    const int rolls = 200;
    for (int roll = 0; roll < rolls; ++roll)
    {
        const std::uint32_t seed = rollSeed (base, roll, dialect);
        rules::BassParams p;
        p.dialect = dialect;
        p.mode = rules::drawMode (dialect, 0.55f, seed);
        Lane bass;
        rules::composeBass (bass, p, seed);
        notes += (double) bass.count / 8.0;
        kick += rules::kickShare (bass);
        const float med = rules::medianLength (bass);
        if (dialect == Pop || (med >= kLenMedLo[dialect] - 0.26f && med <= kLenMedHi[dialect] + 0.01f))
            ++lenOk;
        dups += rules::duplicateOnsets (bass);
        if (! rules::tonicGuardOk (bass, dialect))
            ++tonicBad;
        int grid[8][16];
        for (auto& row : grid)
            for (int& x : row)
                x = -1;
        for (int i = 0; i < bass.count; ++i)
        {
            const int st = (int) std::lround (bass.notes[i].start);
            if (st >= 0 && st < 128)
                grid[st / 16][st % 16] = pcOf (bass.notes[i].chord[0]);
        }
        // Mutations: bars 4-7 against the cell bar of the same parity.
        for (int b = 3; b < 7; ++b)
            for (int s = 0; s < 16; ++s)
                if (grid[b][s] != grid[b & 1][s])
                {
                    ++diffAll;
                    if (s >= 8)
                        ++diffBack;
                }
        for (int b = 4; b < 8; ++b)
        {
            int n = 0;
            int pcs = 0;
            for (int s = 0; s < 16; ++s)
                if (grid[b][s] >= 0)
                {
                    ++n;
                    pcs |= 1 << grid[b][s];
                }
            ++backBars;
            if (n >= 1 && (pcs & (pcs - 1)) == 0)
                ++thinBars;
        }
        rules::StabParams sp;
        sp.dialect = dialect;
        sp.mode = p.mode;
        sp.perBar = 3;
        Lane stab;
        rules::composeStab (stab, bass, sp, seed ^ 0x5717u);
        for (int i = 0; i < stab.count; ++i)
        {
            ++stabHits;
            const float t = stab.notes[i].start;
            for (int k = 0; k < bass.count; ++k)
            {
                if (bass.notes[k].start > t + 1.0e-3f || bass.notes[k].start + bass.notes[k].length <= t + 1.0e-3f)
                    continue;
                const int bpc = pcOf (bass.notes[k].chord[0]);
                for (int c = 0; c < stab.notes[i].nChord && c < 4; ++c)
                {
                    const int ic = pcOf (stab.notes[i].chord[c] - bpc);
                    if (ic == 1 || ic == 11)
                    {
                        ++stabClash;
                        c = 4;
                    }
                }
            }
        }
    }
    const double npb = notes / rolls;
    const double ks = kick / rolls;
    const double back = diffAll > 0 ? (double) diffBack / diffAll : 1.0;
    char d[320];
    std::snprintf (d, sizeof d,
                   "%s: notes/bar %.2f (median %.1f), on-kick %.3f (target %.2f), length guard %d/%d, back-half share %.2f, "
                   "one-pitch bars 5-8 %d/%d, dup onsets %d, tonic guard misses %d, stab m2/m9 %d of %d",
                   kNames[dialect], npb, kBassDensityMedian[dialect], ks, kKickTarget[dialect], lenOk, rolls, back, thinBars, backBars,
                   dups, tonicBad, stabClash, stabHits);
    check (npb >= kBassDensityMedian[dialect] - 1.5 && npb <= kBassDensityMedian[dialect] + 1.0, "v1.2 composer density near the style median", d);
    check (ks <= kKickTarget[dialect] + 0.08 && ks <= 0.35, "v1.2 composer on-kick share near its target, under 35%", d);
    check (lenOk * 10 >= rolls * 9, "v1.2 median note length inside the guard band in >= 90% of rolls", d);
    check (back >= kBackHalfShareMin, ">= 60% of bar-to-bar mutations in the back half (slots 9-16)", d);
    check (thinBars * 20 <= backBars, "bars 5-8 carry >= 2 pitch classes (>= 95% of bars)", d);
    check (dups == 0, "one note per onset (DUP_NOTES)", d);
    check (tonicBad == 0, "tonic guard: root most-sounding and the top-2 share held", d);
    check (stabClash * 50 <= stabHits, "stabs make no m2 / m9 against the sounding bass (<= 2% before the harmony pass)", d);
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
    runModelTables();
    for (int dialect = 0; dialect < NumDialects; ++dialect)
        runV12 (dialect, 2026101852u);
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
