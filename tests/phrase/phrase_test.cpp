// rolla-phrase-test: DEEP / JACKIN phrase grammar checks. No JUCE.
//
//   rolla-phrase-test [--fresh N] [--seeds a,b,c]
//
// Runs on the CI gate seed set (the rolla-rolls formula) plus N fresh random
// seed sets (default 3, printed so a failure can be replayed with --seeds).
// Nothing here tunes constants against particular seeds.

#include "Generator/PhraseBank.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace rolla;
using namespace rolla::phrase;

namespace
{
int failures = 0;

void check (bool ok, const char* what, const std::string& detail = {})
{
    std::printf ("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
    if (! ok)
        ++failures;
}

const char* styleName (Style s)
{
    return s == Style::Deep ? "DEEP" : "JACKIN";
}

constexpr int kRolls = 200;
constexpr int kBlock = 20;
constexpr int kMaxPairsPerBlock = 2;

struct SeedSet
{
    std::string label;
    std::vector<std::uint32_t> base; // one base seed; rolls derive from it
};

// Same derivation as rolla-rolls: 1 + roll * 0x9E3779B9 + dialect * 101, from base.
std::uint32_t rollSeed (std::uint32_t base, int roll, int dialect)
{
    return base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) dialect * 101u;
}

PhraseSettings randomSettings (std::mt19937& g, int bars)
{
    std::uniform_real_distribution<float> u (0.f, 1.f);
    PhraseSettings s;
    s.busy = u (g);
    s.bounce = u (g);
    s.dark = u (g);
    s.bars = bars;
    return s;
}

// Near-identical pairs inside each block of 20 rolls. Returns the worst block.
int worstBlockPairs (const std::vector<Skeleton>& sk, float* worstSim = nullptr)
{
    int worst = 0;
    float ws = 0.f;
    for (int b = 0; b + kBlock <= (int) sk.size(); b += kBlock)
    {
        int pairs = 0;
        for (int i = b; i < b + kBlock; ++i)
            for (int j = i + 1; j < b + kBlock; ++j)
            {
                const float s = similarity (sk[(size_t) i], sk[(size_t) j]);
                ws = std::max (ws, s);
                if (s >= kNearIdentical)
                    ++pairs;
            }
        worst = std::max (worst, pairs);
    }
    if (worstSim != nullptr)
        *worstSim = ws;
    return worst;
}

// Greedy clusters at the near-identical threshold = distinct shapes.
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

void runSet (Style style, int dialect, const SeedSet& set)
{
    const std::uint32_t base = set.base[0];
    for (int bars : { 2, 1 })
    {
        for (int mode = 0; mode < 2; ++mode) // 0 default knobs, 1 random knobs
        {
            std::mt19937 g (base ^ 0xA5A5u ^ (std::uint32_t) bars);
            std::vector<Skeleton> raw, guarded;
            SkeletonHistory hist;
            int hardFails = 0;
            char why[96] = "";
            char firstWhy[160] = "";
            for (int roll = 0; roll < kRolls; ++roll)
            {
                PhraseSettings ps;
                ps.bars = bars;
                if (mode == 1)
                    ps = randomSettings (g, bars);
                const std::uint32_t seed = rollSeed (base, roll, dialect);
                const Phrase p = makeBassPhrase (style, seed, ps);
                if (! passesHardRules (p, why))
                {
                    if (hardFails++ == 0)
                        std::snprintf (firstWhy, sizeof (firstWhy), "seed %u: %s", seed, why);
                }
                raw.push_back (skeletonOf (p));
                if (roll % kBlock == 0)
                    hist.clear();
                guarded.push_back (skeletonOf (drawDistinctBass (style, seed, ps, hist)));
            }
            float ws = 0.f;
            const int worstRaw = worstBlockPairs (raw, &ws);
            const int worstGuard = worstBlockPairs (guarded);
            const int shapes = distinctShapes (raw);
            // Rhythm alone (pitch ignored), reported, not gated.
            std::vector<Skeleton> rhythm = raw;
            for (auto& k : rhythm)
                std::memset (k.pitch, 0, sizeof (k.pitch));
            const int worstRhythm = worstBlockPairs (rhythm);
            char label[200];
            std::snprintf (label, sizeof (label), "%s %s %d-bar %s knobs", styleName (style), set.label.c_str(), bars,
                           mode == 0 ? "default" : "random");
            char d[200];
            std::snprintf (d, sizeof (d), "%s: hard fails %d/%d %s", label, hardFails, kRolls, firstWhy);
            check (hardFails == 0, "hard rules", d);
            std::snprintf (d, sizeof (d), "%s: worst block %d pairs (max sim %.2f), with redraw %d", label, worstRaw, ws, worstGuard);
            check (worstRaw <= kMaxPairsPerBlock && worstGuard <= kMaxPairsPerBlock, "<=2 near-identical skeletons per 20 rolls", d);
            std::printf ("INFO %s: rhythm-only worst block %d pairs\n", label, worstRhythm);
            std::snprintf (d, sizeof (d), "%s: %d distinct shapes in %d rolls", label, shapes, kRolls);
            check (shapes >= 40, ">=40 distinct phrase shapes", d);
        }
    }
}

void runAnswers (const SeedSet& set)
{
    const std::uint32_t base = set.base[0];
    std::mt19937 g (base ^ 0x5151u);
    float gapSum = 0.f, gapMin = 1.f, callSum = 0.f;
    int barsWithout = 0, outOfKey = 0, n = 0, hits = 0;
    for (int roll = 0; roll < kRolls; ++roll)
    {
        const PhraseSettings ps = roll % 2 ? randomSettings (g, 2) : PhraseSettings {};
        const std::uint32_t seed = rollSeed (base, roll, Gaskin);
        const Phrase bass = makeBassPhrase (Style::Jackin, seed, ps);
        const Phrase stab = makeStabAnswer (Style::Jackin, bass, seed, ps);
        const AnswerStats st = answerStats (bass, stab);
        gapSum += st.inGapShare;
        gapMin = std::min (gapMin, st.inGapShare);
        callSum += st.callsAnswered;
        hits += st.stabHits;
        ++n;
        for (int b = 0; b < stab.bars; ++b)
        {
            bool any = false;
            for (int i = 0; i < stab.count; ++i)
                if ((int) stab.notes[i].start / 16 == b)
                    any = true;
            if (! any)
                ++barsWithout;
        }
        for (int i = 0; i < stab.count; ++i)
            for (int c = 0; c < stab.notes[i].nChord; ++c)
                if (! inMode (stab.notes[i].chord[c], stab.mode))
                    ++outOfKey;
    }
    char d[200];
    std::snprintf (d, sizeof (d), "JACKIN %s: stab onsets in bass gaps mean %.2f min %.2f", set.label.c_str(), gapSum / n, gapMin);
    check (gapSum / n >= 0.95f && gapMin >= 0.75f, "call/answer: stabs answer in the gaps", d);
    std::snprintf (d, sizeof (d), "JACKIN %s: calls answered %.2f, %.1f stab hits per phrase", set.label.c_str(), callSum / n, (float) hits / n);
    check (callSum / n >= 0.70f, "call/answer: bass calls get an answer", d);
    std::snprintf (d, sizeof (d), "JACKIN %s: %d bars without a stab, %d out-of-key stab notes", set.label.c_str(), barsWithout, outOfKey);
    check (barsWithout == 0 && outOfKey == 0, "call/answer: every bar answered, all in key", d);
}

void runBasics()
{
    PhraseSettings ps;
    const Phrase a = makeBassPhrase (Style::Deep, 1234, ps);
    const Phrase b = makeBassPhrase (Style::Deep, 1234, ps);
    bool same = a.count == b.count;
    for (int i = 0; same && i < a.count; ++i)
        same = a.notes[i].start == b.notes[i].start && a.notes[i].chord[0] == b.notes[i].chord[0]
               && a.notes[i].length == b.notes[i].length;
    check (same, "deterministic for a seed");

    Lane lane;
    const int written = writePhrase (a, lane, 8);
    bool tiled = written == a.count * 4;
    for (int i = 0; tiled && i < a.count; ++i)
        tiled = lane.notes[i + a.count * 3].start == a.notes[i].start + 96.f;
    check (tiled, "writePhrase tiles a 2-bar phrase over 8 bars");

    check (degreeToSemis (-1, Aeolian) == -2 && degreeToSemis (-3, Aeolian) == -5 && degreeToSemis (7, Dorian) == 12
               && degreeToSemis (-7, Phrygian) == -12 && degreeToSemis (2, Phrygian) == 3 && degreeToSemis (5, Dorian) == 9,
           "degree -> semitone mapping");

    Style s;
    check (styleForDialect (Long, s) && s == Style::Deep && styleForDialect (Gaskin, s) && s == Style::Jackin
               && ! styleForDialect (UkMin, s),
           "dialect mapping (DEEP = long, JACKIN = gaskin)");

    // DEEP and JACKIN should not sound alike: compare onset density and note length.
    float deepLen = 0.f, jackLen = 0.f;
    int deepN = 0, jackN = 0;
    for (std::uint32_t sd = 1; sd <= 200; ++sd)
    {
        const Phrase d = makeBassPhrase (Style::Deep, sd, ps);
        const Phrase j = makeBassPhrase (Style::Jackin, sd, ps);
        for (int i = 0; i < d.count; ++i)
            deepLen += d.notes[i].length;
        for (int i = 0; i < j.count; ++i)
            jackLen += j.notes[i].length;
        deepN += d.count;
        jackN += j.count;
    }
    char d[160];
    std::snprintf (d, sizeof (d), "mean length DEEP %.2f vs JACKIN %.2f sixteenths; notes/phrase %.1f vs %.1f",
                   deepLen / deepN, jackLen / jackN, deepN / 200.f, jackN / 200.f);
    check (deepLen / deepN > 1.4f * (jackLen / jackN), "DEEP holds longer than JACKIN", d);
}
} // namespace

int main (int argc, char** argv)
{
    int fresh = 3;
    std::vector<std::uint32_t> fixed;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--fresh") == 0 && i + 1 < argc)
            fresh = std::atoi (argv[++i]);
        else if (std::strcmp (argv[i], "--seeds") == 0 && i + 1 < argc)
        {
            std::string v = argv[++i];
            size_t p = 0;
            while (p < v.size())
            {
                const size_t q = v.find (',', p);
                fixed.push_back ((std::uint32_t) std::strtoul (v.substr (p, q - p).c_str(), nullptr, 10));
                if (q == std::string::npos)
                    break;
                p = q + 1;
            }
        }
    }

    std::vector<SeedSet> sets;
    sets.push_back ({ "gate(base 1)", { 1u } });
    for (auto s : fixed)
        sets.push_back ({ "seed " + std::to_string (s), { s } });
    std::random_device rd;
    for (int i = 0; i < fresh; ++i)
    {
        const std::uint32_t s = rd();
        sets.push_back ({ "fresh " + std::to_string (s), { s } });
    }
    std::printf ("seed sets:");
    for (const auto& s : sets)
        std::printf (" %u", s.base[0]);
    std::printf ("  (replay: --fresh 0 --seeds a,b,c)\n");

    runBasics();
    for (const auto& set : sets)
    {
        runSet (Style::Deep, Long, set);
        runSet (Style::Jackin, Gaskin, set);
        runAnswers (set);
    }
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
