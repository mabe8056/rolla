// rolla-style-test: DEEP / BOUNCY review measures, and ROLLER / JACKIN frozen.
//
//   rolla-style-test [--report] [--fresh N] [--golden]
//
// Rolls are made the way rolla-rolls makes them (same per-roll seeds, the
// v2.1 skeleton redraw, INTENSITY 75 arrangement, default knobs), 20 rolls per
// seed set, on the review seeds plus N fresh random sets (printed).
// Measures (bass unless noted):
//   sound      share of the loop's 16ths a bass note covers
//   notes/bar  bass onsets per bar
//   oct        share of consecutive bass moves that are an octave
//   outmode    share of bass time on pitch classes outside natural minor
//   dens       share of rolls whose bar 4 and bar 8 change the note count vs bar 3 / bar 7
//   pskel      pitch-skeleton near-duplicate pairs among 20 rolls (first 2 bars, slot + pc, >= 0.70)
//   gap        share of stab onsets that land where the bass is silent
//   pickup     share of rolls with a bass or stab onset in the last two 16ths of the loop
// --golden prints every style's batch hashes for seeds 1 and 2.
// --write DIR [--seeds a,b,...] writes 20 rolls per style and seed as MIDI
// (bass / stab / drums at the top of each roll folder) for tools/score_rolls.py.
//
// Gates: ROLLER / JACKIN batches for seeds 1 and 2 hash to the frozen values
// (their MIDI is unchanged by the DEEP / BOUNCY pass). DEEP: answers change the
// density in >= 90% of rolls, 20-50% of stab onsets in bass gaps, pickups in
// 5-85% of rolls (optional, not every roll). BOUNCY: 6-14 of 20 rolls are the
// legato pulse (bass sounds >= 85% of the loop by duration), the rest bounce (>= 45%),
// 5-7 notes a bar, nothing outside natural minor, answers change density in
// >= 90% of rolls, octave moves 15-26% at the default BOUNCE, <= 8.5% at
// BOUNCE 0 and rising with BOUNCE.

#include <cmath> // Intensity.h uses std::floor without including it

#include "Generator/Generator.h"
#include "Generator/Intensity.h"
#include "Generator/MidiFile.h"
#include "Generator/StyleRoller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace rolla;

namespace
{
int failures = 0;

void check (bool ok, const std::string& what)
{
    std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
    if (! ok)
        ++failures;
}

constexpr int kRolls = 20;

// One roll exactly as tools/roll_batch.cpp builds it.
Groove makeRoll (int style, std::uint32_t base, int roll, std::vector<Groove>& seen, const Settings& s)
{
    std::uint32_t seeds[NumParts];
    for (int p = 0; p < NumParts; ++p)
        seeds[p] = base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u + (std::uint32_t) style * 101u;
    Groove g {};
    generate (g, s, seeds, 0x7fu);
    for (int guard = 0; guard < 8; ++guard)
    {
        bool close = false;
        for (const auto& prev : seen)
            if (bassSkeletonSimilarity (g, prev) >= 0.55f)
                close = true;
        if (! close)
            break;
        seeds[Bass] ^= 0x6D2B79F5u * (std::uint32_t) (guard + 1);
        generate (g, s, seeds, 0x7fu);
    }
    seen.push_back (g);
    Groove arranged;
    applyIntensity (g, arranged, planIntensity (style, 75, s.stab), 0, 0);
    return arranged;
}

Settings defaults (int style)
{
    Settings s;
    s.dialect = style;
    s.key = 2;
    s.bars = 8;
    return s;
}

std::uint64_t hashGroove (const Groove& g)
{
    std::uint64_t h = 1469598103934665603ull;
    auto mix = [&h] (std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
        {
            h ^= (v >> (i * 8)) & 0xffu;
            h *= 1099511628211ull;
        }
    };
    mix ((std::uint64_t) g.bars);
    for (int p = 0; p < Drums; ++p)
    {
        const Lane& l = g.lane[p];
        mix ((std::uint64_t) l.count);
        for (int i = 0; i < l.count; ++i)
        {
            const Note& n = l.notes[i];
            mix ((std::uint64_t) std::lround (n.start * 1000.f));
            mix ((std::uint64_t) std::lround (n.length * 1000.f));
            mix ((std::uint64_t) n.vel | ((std::uint64_t) n.flags << 8) | ((std::uint64_t) n.nChord << 16));
            for (int c = 0; c < n.nChord && c < 4; ++c)
                mix ((std::uint64_t) (std::uint8_t) n.chord[c]);
        }
    }
    for (int v = 0; v < NumDrumVoices; ++v)
        for (int t = 0; t < g.bars * 16; ++t)
            mix (g.drums.vel[v][t]);
    return h;
}

std::uint64_t batchHash (int style, std::uint32_t base)
{
    std::vector<Groove> seen;
    const Settings s = defaults (style);
    std::uint64_t h = 0;
    for (int roll = 0; roll < kRolls; ++roll)
        h = h * 31u + hashGroove (makeRoll (style, base, roll, seen, s));
    return h;
}

bool bassAt (const Lane& bass, float slot)
{
    for (int i = 0; i < bass.count; ++i)
        if (slot >= bass.notes[i].start - 1e-3f && slot < bass.notes[i].start + bass.notes[i].length - 1e-3f)
            return true;
    return false;
}

int pcOfSemi (int semi)
{
    return ((semi % 12) + 12) % 12;
}

bool inMinor (int semi)
{
    static const bool ok[12] = { 1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0 };
    return ok[((semi % 12) + 12) % 12];
}

struct Measures
{
    double sound = 0, timeShare = 0, npb = 0, oct = 0, outmode = 0, dens = 0, gap = 0, pickup = 0;
    int pskel = 0, rolls = 0, stabOn = 0, stabGap = 0;
    // ROLLER review measures (rolls out of 20).
    int gridLocked = 0, noTurn = 0, b2Clash = 0;
    // JACKIN loop identity: distinct bars per roll, 1- / 2-bar repetition, hooks across rolls.
    double distinctBars = 0, rep1 = 0, rep2 = 0;
    int maxDistinct = 0, hooks = 0, clones = 0;
    double onBeat = 0;
    std::vector<std::string> hookKeys, rollKeys;
    // BOUNCY modes, read from the output the way roll_test does (mean note length >= 1.8 is pulse).
    int pulseRolls = 0;
    double pulseTime = 0, bounceTime = 0;
};

struct Skel
{
    std::vector<std::pair<int, int>> ev;
};

double skelSim (const Skel& a, const Skel& b)
{
    int same = 0;
    for (const auto& e : a.ev)
        for (const auto& f : b.ev)
            if (e == f)
            {
                ++same;
                break;
            }
    const size_t n = std::max (a.ev.size(), b.ev.size());
    return n ? (double) same / (double) n : 1.0;
}

Measures measure (int style, std::uint32_t base, const Settings& s)
{
    Measures m;
    std::vector<Groove> seen;
    std::vector<Skel> skels;
    double octMoves = 0, moves = 0;
    for (int roll = 0; roll < kRolls; ++roll)
    {
        const Groove g = makeRoll (style, base, roll, seen, s);
        const Lane& bass = g.lane[Bass];
        const int steps = g.bars * 16;
        int covered = 0;
        double outT = 0, allT = 0;
        for (int t = 0; t < steps; ++t)
            covered += bassAt (bass, (float) t) ? 1 : 0;
        m.sound += (double) covered / steps;
        double dur = 0;
        for (int i = 0; i < bass.count; ++i)
            dur += bass.notes[i].length;
        m.timeShare += dur / steps;
        if (bass.count > 0 && dur / bass.count >= 1.8)
        {
            ++m.pulseRolls;
            m.pulseTime += dur / steps;
        }
        else
            m.bounceTime += dur / steps;
        m.npb += (double) bass.count / g.bars;
        std::vector<int> perBar ((size_t) g.bars, 0);
        Skel sk;
        for (int i = 0; i < bass.count; ++i)
        {
            const Note& n = bass.notes[i];
            const int bar = std::clamp ((int) (n.start / 16.f), 0, g.bars - 1);
            ++perBar[(size_t) bar];
            allT += n.length;
            if (! inMinor (n.chord[0]))
                outT += n.length;
            if (i > 0)
            {
                ++moves;
                if (std::abs (n.chord[0] - bass.notes[i - 1].chord[0]) == 12)
                    ++octMoves;
            }
            if (n.start < 32.f)
                sk.ev.push_back ({ (int) std::lround (n.start), ((n.chord[0] % 12) + 12) % 12 });
        }
        m.outmode += allT > 0 ? outT / allT : 0.0;
        if (g.bars >= 8)
            m.dens += (perBar[3] != perBar[2] && perBar[7] != perBar[6]) ? 1.0 : 0.0;
        skels.push_back (sk);
        const Lane& stab = g.lane[Stab];
        for (int i = 0; i < stab.count; ++i)
        {
            ++m.stabOn;
            if (! bassAt (bass, stab.notes[i].start))
                ++m.stabGap;
        }
        {
            // GRID_LOCKED: fewer than 3 of the 4 in-beat positions carry >= 10% of onsets.
            int pos[4] {};
            int on = 0;
            for (int i = 0; i < bass.count; ++i)
            {
                const int t = (int) std::lround (bass.notes[i].start);
                ++pos[t & 3];
                on += (t & 3) == 0 ? 1 : 0;
            }
            int used = 0;
            for (int k = 0; k < 4; ++k)
                used += pos[k] >= 0.10 * bass.count ? 1 : 0;
            m.gridLocked += used < 3 ? 1 : 0;
            m.onBeat += bass.count > 0 ? (double) on / bass.count : 0.0;
            // Bar-8 turnaround: a rest of >= 4/16 in bar 8, and bar 8 differs from bar 4.
            if (g.bars >= 8)
            {
                int run = 0, best = 0;
                for (int t = 112; t < 128; ++t)
                {
                    run = bassAt (bass, (float) t) ? 0 : run + 1;
                    best = std::max (best, run);
                }
                bool same = true;
                for (int t = 0; t < 16; ++t)
                {
                    int pa = -99, pb = -99;
                    for (int i = 0; i < bass.count; ++i)
                    {
                        if ((int) std::lround (bass.notes[i].start) == 48 + t)
                            pa = bass.notes[i].chord[0];
                        if ((int) std::lround (bass.notes[i].start) == 112 + t)
                            pb = bass.notes[i].chord[0];
                    }
                    same = same && pa == pb;
                }
                m.noTurn += (best < 4 || same) ? 1 : 0;
            }
            // b2 under a natural 2 (9th) in a sounding stab.
            bool clash = false;
            for (int i = 0; i < bass.count && ! clash; ++i)
            {
                if (pcOfSemi (bass.notes[i].chord[0]) != 1)
                    continue;
                const float a = bass.notes[i].start, b = a + bass.notes[i].length;
                for (int k = 0; k < g.lane[Stab].count && ! clash; ++k)
                {
                    const Note& st = g.lane[Stab].notes[k];
                    if (st.start >= b || st.start + st.length <= a)
                        continue;
                    for (int c = 0; c < st.nChord && c < 4; ++c)
                        clash = clash || pcOfSemi (st.chord[c]) == 2;
                }
            }
            m.b2Clash += clash ? 1 : 0;
        }
        {
            // Per-16th token: sounding pitch (or -99 for rest).
            const int steps = g.bars * 16;
            std::vector<int> tok ((size_t) steps, -99);
            for (int i = 0; i < bass.count; ++i)
            {
                const int a = (int) std::lround (bass.notes[i].start);
                const int b = (int) std::lround (bass.notes[i].start + bass.notes[i].length);
                for (int t = a; t < b && t < steps; ++t)
                    tok[(size_t) t] = bass.notes[i].chord[0];
            }
            std::vector<std::string> barKeys;
            for (int b = 0; b < g.bars; ++b)
            {
                std::string k;
                for (int i = 0; i < bass.count; ++i)
                    if ((int) (bass.notes[i].start / 16.f) == b)
                        k += std::to_string ((int) std::lround (bass.notes[i].start) - b * 16) + ":" + std::to_string (bass.notes[i].chord[0]) + ",";
                barKeys.push_back (k);
            }
            std::vector<std::string> uniq = barKeys;
            std::sort (uniq.begin(), uniq.end());
            uniq.erase (std::unique (uniq.begin(), uniq.end()), uniq.end());
            m.distinctBars += (double) uniq.size();
            m.maxDistinct = std::max (m.maxDistinct, (int) uniq.size());
            auto rep = [&] (int lag)
            {
                int same = 0, n = 0;
                for (int t = 0; t + lag * 16 < steps; ++t, ++n)
                    same += tok[(size_t) t] == tok[(size_t) (t + lag * 16)] ? 1 : 0;
                return n ? (double) same / n : 0.0;
            };
            m.rep1 += rep (1);
            m.rep2 += rep (2);
            m.hookKeys.push_back (barKeys.size() >= 2 ? barKeys[0] + "|" + barKeys[1] : "");
            m.rollKeys.push_back ([&] { std::string all; for (auto& k : barKeys) all += k + "|"; return all; }());
        }
        bool pick = false;
        for (const Lane* l : { &bass, &stab })
            for (int i = 0; i < l->count; ++i)
                if (l->notes[i].start >= (float) (steps - 2) - 1e-3f)
                    pick = true;
        m.pickup += pick ? 1.0 : 0.0;
        ++m.rolls;
    }
    for (size_t i = 0; i < skels.size(); ++i)
        for (size_t j = i + 1; j < skels.size(); ++j)
            if (skelSim (skels[i], skels[j]) >= 0.70)
                ++m.pskel;
    m.sound /= m.rolls;
    m.timeShare /= m.rolls;
    m.pulseTime = m.pulseRolls > 0 ? m.pulseTime / m.pulseRolls : 0.0;
    m.bounceTime = m.rolls > m.pulseRolls ? m.bounceTime / (m.rolls - m.pulseRolls) : 0.0;
    m.npb /= m.rolls;
    m.outmode /= m.rolls;
    m.dens /= m.rolls;
    m.pickup /= m.rolls;
    m.onBeat /= m.rolls;
    m.distinctBars /= m.rolls;
    m.rep1 /= m.rolls;
    m.rep2 /= m.rolls;
    {
        auto h = m.hookKeys;
        std::sort (h.begin(), h.end());
        m.hooks = (int) (std::unique (h.begin(), h.end()) - h.begin());
        auto r = m.rollKeys;
        std::sort (r.begin(), r.end());
        m.clones = (int) r.size() - (int) (std::unique (r.begin(), r.end()) - r.begin());
    }
    m.oct = moves > 0 ? octMoves / moves : 0.0;
    m.gap = m.stabOn > 0 ? (double) m.stabGap / m.stabOn : 0.0;
    return m;
}

// ROLLER bands from data/genre/style_rules.json (roller::kTargets), over 20 rolls.
struct RollerBands
{
    double npb = 0, npbMin = 99, npbMax = 0, onKick = 0, onKickMax = 0, ea = 0, eaMax = 0;
    double cls[4] {}, m1 = 0, m2 = 0, m4 = 0, hiBand = 0, sd = 0;
    int answers = 0, pickups = 0, drumTurns = 0, rolls = 0;
};

RollerBands rollerBands (std::uint32_t base)
{
    RollerBands b;
    std::vector<Groove> seen;
    const Settings s = defaults (UkMin);
    for (int roll = 0; roll < kRolls; ++roll)
    {
        const Groove g = makeRoll (UkMin, base, roll, seen, s);
        const Lane& bass = g.lane[Bass];
        const int steps = g.bars * 16;
        const double npb = (double) bass.count / g.bars;
        b.npb += npb;
        b.npbMin = std::min (b.npbMin, npb);
        b.npbMax = std::max (b.npbMax, npb);
        int cls[4] {};
        std::vector<int> perBar ((size_t) g.bars, 0);
        bool pickup = false;
        for (int i = 0; i < bass.count; ++i)
        {
            const int t = (int) std::lround (bass.notes[i].start);
            ++cls[t & 3];
            ++perBar[(size_t) std::clamp (t / 16, 0, g.bars - 1)];
            pickup = pickup || t >= steps - 2;
        }
        const double n = std::max (1, bass.count);
        // Kicks sit on every beat, so on-kick is the beat class.
        b.onKick += cls[0] / n;
        b.onKickMax = std::max (b.onKickMax, cls[0] / n);
        b.ea += (cls[1] + cls[3]) / n;
        b.eaMax = std::max (b.eaMax, (cls[1] + cls[3]) / n);
        for (int c = 0; c < 4; ++c)
            b.cls[c] += cls[c] / n;
        double mean = 0, var = 0;
        for (int v : perBar)
            mean += v;
        mean /= g.bars;
        for (int v : perBar)
            var += (v - mean) * (v - mean);
        b.sd += std::sqrt (var / g.bars);
        // Bar 4 answers bar 2 by density (not pitch-only).
        b.answers += perBar[3] != perBar[1] ? 1 : 0;
        b.pickups += pickup ? 1 : 0;
        // mK: share of 16ths whose sounding pitch (or rest) matches K bars later.
        std::vector<int> tok ((size_t) steps, -99);
        for (int i = 0; i < bass.count; ++i)
        {
            const int a = (int) std::lround (bass.notes[i].start);
            const int e = (int) std::lround (bass.notes[i].start + bass.notes[i].length);
            for (int t = a; t < e && t < steps; ++t)
                tok[(size_t) t] = bass.notes[i].chord[0];
        }
        auto rep = [&] (int lag)
        {
            int same = 0, k = 0;
            for (int t = 0; t + lag * 16 < steps; ++t, ++k)
                same += tok[(size_t) t] == tok[(size_t) (t + lag * 16)] ? 1 : 0;
            return k ? (double) same / k : 0.0;
        };
        b.m1 += rep (1);
        b.m2 += rep (2);
        b.m4 += rep (4);
        // Hat / perc onsets per bar: a 16th with any high voice counts once.
        int hi = 0;
        for (int t = 0; t < steps; ++t)
            hi += (g.drums.vel[HatC][t] || g.drums.vel[HatO][t] || g.drums.vel[Ride][t] || g.drums.vel[Perc][t]) ? 1 : 0;
        b.hiBand += (double) hi / g.bars;
        // Drum turn: the last bar's last beat carries more perc / rim / tom than the bars before it.
        auto fill = [&] (int bar)
        {
            int f = 0;
            for (int s16 = 12; s16 < 16; ++s16)
                for (int v : { (int) Perc, (int) Rim, (int) TomLow })
                    f += g.drums.vel[v][bar * 16 + s16] ? 1 : 0;
            return f;
        };
        double others = 0;
        for (int bar = 0; bar < g.bars - 1; ++bar)
            others += fill (bar);
        b.drumTurns += fill (g.bars - 1) > others / (g.bars - 1) ? 1 : 0;
        ++b.rolls;
    }
    const double r = b.rolls;
    b.npb /= r;
    b.onKick /= r;
    b.ea /= r;
    for (double& c : b.cls)
        c /= r;
    b.m1 /= r;
    b.m2 /= r;
    b.m4 /= r;
    b.hiBand /= r;
    b.sd /= r;
    return b;
}

void printLoop (const char* label, std::uint32_t base, const Measures& m)
{
    std::printf ("%-8s seed %-10u distinct bars %.1f (max %d)  rep1 %.2f  rep2 %.2f  hooks %2d/20  clones %d\n",
                 label, base, m.distinctBars, m.maxDistinct, m.rep1, m.rep2, m.hooks, m.clones);
}

void printRoller (const char* label, std::uint32_t base, const Measures& m)
{
    std::printf ("%-8s seed %-10u gridLocked %2d/20  noTurn %2d/20  b2clash %2d/20  onBeat %.2f  notes/bar %.1f  oct %.3f\n",
                 label, base, m.gridLocked, m.noTurn, m.b2Clash, m.onBeat, m.npb, m.oct);
}

void print (const char* label, std::uint32_t base, const Measures& m)
{
    std::printf ("%-8s seed %-10u sound %.2f time %.2f  notes/bar %.1f  oct %.3f  outmode %.3f  dens %.2f  pskel %2d  gap %.2f  pickup %.2f\n",
                 label, base, m.sound, m.timeShare, m.npb, m.oct, m.outmode, m.dens, m.pskel, m.gap, m.pickup);
}
} // namespace

int main (int argc, char** argv)
{
    bool report = false, golden = false;
    std::string writeDir;
    std::vector<std::uint32_t> writeSeeds { 1u, 2517220185u, 128383010u, 1168500024u };
    int fresh = 2;
    for (int i = 1; i < argc; ++i)
    {
        if (! std::strcmp (argv[i], "--report"))
            report = true;
        else if (! std::strcmp (argv[i], "--golden"))
            golden = true;
        else if (! std::strcmp (argv[i], "--fresh") && i + 1 < argc)
            fresh = std::atoi (argv[++i]);
        else if (! std::strcmp (argv[i], "--write") && i + 1 < argc)
            writeDir = argv[++i];
        else if (! std::strcmp (argv[i], "--seeds") && i + 1 < argc)
        {
            writeSeeds.clear();
            std::string v = argv[++i];
            size_t p = 0;
            while (p < v.size())
            {
                const size_t q = v.find (',', p);
                writeSeeds.push_back ((std::uint32_t) std::strtoul (v.substr (p, q - p).c_str(), nullptr, 10));
                if (q == std::string::npos)
                    break;
                p = q + 1;
            }
        }
    }

    // --write DIR: 20 rolls per style and review seed as MIDI, for tools/score_rolls.py.
    if (! writeDir.empty())
    {
        for (int style = 0; style < NumDialects; ++style)
            for (std::uint32_t base : writeSeeds)
            {
                std::vector<Groove> seen;
                for (int roll = 0; roll < kRolls; ++roll)
                {
                    const Groove g = makeRoll (style, base, roll, seen, defaults (style));
                    char dir[1024];
                    std::snprintf (dir, sizeof dir, "%s/%s/%u/roll_%02d", writeDir.c_str(), dialectId (style), base, roll);
                    std::string cmd = std::string ("mkdir -p '") + dir + "'";
                    if (std::system (cmd.c_str()) != 0)
                        return 1;
                    std::vector<std::string> written;
                    std::string error;
                    MidiExport::writeAll (dir, g, 2, 0.52f, 130.0, written, error);
                }
            }
        return 0;
    }

    if (golden)
    {
        for (int style : { (int) UkMin, (int) Long, (int) Pop, (int) Gaskin })
            for (std::uint32_t base : { 1u, 2u })
                std::printf ("%s seed %u: 0x%016llxull\n", dialectId (style), base, (unsigned long long) batchHash (style, base));
        return 0;
    }

    std::vector<std::uint32_t> bases { 1u, 2517220185u, 128383010u, 1168500024u };
    std::random_device rd;
    for (int i = 0; i < fresh; ++i)
        bases.push_back (rd());
    std::printf ("seed sets:");
    for (auto b : bases)
        std::printf (" %u", b);
    std::printf ("\n");

    // Every style frozen at its last intended change.
    // A style's hashes change only in the PR that means to change that style
    // (regenerate with --golden). ROLLER changed in claude/style-roller (style_rules.json
    // pass: bass density, bar-4 answer, hats); its two values below need the --golden output;
    // BOUNCY in #28 (relegato); JACKIN in #25 (STAB) and claude/jackin-identity.
    const struct { int style; std::uint32_t base; std::uint64_t hash; } frozen[] = {
        { UkMin, 1u, 0x7c9dcbee6dddc8eaull }, { UkMin, 2u, 0x47a4548b99fd9987ull },
        { Long, 1u, 0x3881ed002bc4c2ebull }, { Long, 2u, 0x8682efccde9fe80bull },
        { Pop, 1u, 0x4d7bb59ba7af16fbull }, { Pop, 2u, 0x985b45c32c605044ull },
        { Gaskin, 1u, 0x17aece70a6b89457ull }, { Gaskin, 2u, 0xdb559e8844e9ef63ull },
    };
    for (const auto& f : frozen)
    {
        const auto h = batchHash (f.style, f.base);
        char what[160];
        std::snprintf (what, sizeof what, "%s seed %u unchanged (0x%016llx)", dialectId (f.style), f.base, (unsigned long long) h);
        check (h == f.hash, what);
    }

    char what[256];
    for (auto base : bases)
    {
        const auto d = measure (Long, base, defaults (Long));
        print ("long", base, d);
        std::snprintf (what, sizeof what, "DEEP seed %u: answers change density %.2f, stabs in gaps %.2f, pickups %.2f",
                       base, d.dens, d.gap, d.pickup);
        // Stabs answer in bass gaps, but under half of them (roll_test's 50% bonus line).
        check (d.dens >= 0.9 && d.gap >= 0.2 && d.gap < 0.5 && d.pickup >= 0.05 && d.pickup <= 0.85, what);

        const auto p = measure (Pop, base, defaults (Pop));
        print ("pop", base, p);
        std::snprintf (what, sizeof what, "BOUNCY seed %u: pulse %d/%d sounding %.2f, bounce %.2f, %.1f notes/bar, out of mode %.3f, answers %.2f, octaves %.3f",
                       base, p.pulseRolls, p.rolls, p.pulseTime, p.bounceTime, p.npb, p.outmode, p.dens, p.oct);
        // Per mode: pulse rolls stay legato (>= 0.85 of the loop sounding), bounce
        // rolls >= 0.45, and the split stays about half (6-14 of 20 pulse).
        check (p.pulseTime >= 0.85 && p.bounceTime >= 0.45
                   && p.pulseRolls >= 6 && p.pulseRolls <= 14 && p.npb >= 5.0 && p.npb <= 7.0 && p.outmode == 0.0
                   && p.dens >= 0.9 && p.oct >= 0.15 && p.oct <= 0.26,
               what);

        Settings lo = defaults (Pop), hi = defaults (Pop);
        lo.bounce = 0.f;
        hi.bounce = 1.f;
        const auto a = measure (Pop, base, lo);
        const auto b = measure (Pop, base, hi);
        std::snprintf (what, sizeof what, "BOUNCY seed %u: BOUNCE raises octaves (%.3f at 0, %.3f default, %.3f at 1)", base, a.oct,
                       p.oct, b.oct);
        check (a.oct <= 0.085 && b.oct > p.oct && b.oct <= 0.26, what);
    }
    // ROLLER review: not grid-locked, beat anchors, a real bar-8 turn, no b2 under a natural-2 stab.
    for (auto base : bases)
    {
        const auto m = measure (UkMin, base, defaults (UkMin));
        printRoller ("ukmin", base, m);
        std::snprintf (what, sizeof what, "ROLLER seed %u: grid-locked %d/20, no turn %d/20, b2 clashes %d/20, on-beat %.2f",
                       base, m.gridLocked, m.noTurn, m.b2Clash, m.onBeat);
        check (m.gridLocked <= 2 && m.noTurn == 0 && m.b2Clash == 0 && m.onBeat >= 0.10 && m.onBeat <= 0.30, what);

        // ROLLER style bands (style_rules.json via roller::kTargets).
        const auto& T = roller::kTargets;
        const auto rb = rollerBands (base);
        std::printf ("ukmin    seed %-10u npb %.2f [%.2f-%.2f] sd %.2f  on-kick %.2f (max %.2f)  e+a %.2f (max %.2f)  "
                     "class %.2f/%.2f/%.2f/%.2f  m1 %.2f m2 %.2f m4 %.2f  bar4 %d/20  pickup %d/20  hi-band %.1f  drum turns %d/20\n",
                     base, rb.npb, rb.npbMin, rb.npbMax, rb.sd, rb.onKick, rb.onKickMax, rb.ea, rb.eaMax, rb.cls[0], rb.cls[1],
                     rb.cls[2], rb.cls[3], rb.m1, rb.m2, rb.m4, rb.answers, rb.pickups, rb.hiBand, rb.drumTurns);
        std::snprintf (what, sizeof what, "ROLLER seed %u bass: %.2f notes/bar (rolls %.2f-%.2f), on-kick %.2f (max %.2f), e+a %.2f (max %.2f)",
                       base, rb.npb, rb.npbMin, rb.npbMax, rb.onKick, rb.onKickMax, rb.ea, rb.eaMax);
        check (rb.npb >= 4.0 && rb.npb <= 6.0 && rb.npbMin >= T.bassNpbMin && rb.npbMax <= T.bassNpbMax
                   && rb.onKick <= T.onKickMax && rb.onKickMax <= 0.35 && rb.ea <= T.eaMax && rb.eaMax <= 0.85,
               what);
        std::snprintf (what, sizeof what, "ROLLER seed %u phrase: density sd %.2f, bar-4 density answers %d/20, pickups %d/20, m2 %.2f, m4 %.2f",
                       base, rb.sd, rb.answers, rb.pickups, rb.m2, rb.m4);
        check (rb.sd >= 0.5 && rb.answers >= 12 && rb.pickups >= (int) std::ceil (T.pickupMinShare * kRolls)
                   && rb.m2 >= 0.6 && rb.m4 >= 0.65,
               what);
        std::snprintf (what, sizeof what, "ROLLER seed %u drums: %.1f hat/perc onsets a bar, last-bar turns %d/20", base, rb.hiBand,
                       rb.drumTurns);
        check (rb.hiBand >= 9.0 && rb.hiBand <= 14.0 && rb.drumTurns >= 14, what);
    }
    {
        // Hat swing default sits in the band (swing is per style, applied by the engine).
        const auto& T = roller::kTargets;
        std::snprintf (what, sizeof what, "ROLLER hat swing default %.3f inside %.3f-%.3f", T.swing, T.swingMin, T.swingMax);
        check (T.swing >= T.swingMin && T.swing <= T.swingMax && std::fabs (T.swing - 0.537f) < 1e-4f, what);
    }

    // JACKIN review: a hooky 2-bar loop (A B A B4 A B A B8) that still differs
    // from roll to roll.
    for (auto base : bases)
    {
        const auto m = measure (Gaskin, base, defaults (Gaskin));
        printLoop ("gaskin", base, m);
        std::snprintf (what, sizeof what, "JACKIN seed %u: <= %d distinct bars, 1-bar repeat %.2f, 2-bar repeat %.2f, %d/20 hooks, %d clones",
                       base, m.maxDistinct, m.rep1, m.rep2, m.hooks, m.clones);
        check (m.maxDistinct <= 4 && m.rep1 <= 0.11 && m.rep2 >= 0.60 && m.hooks >= 18 && m.clones == 0, what);
    }

    // BUSY: bass notes per bar rise (monotonic, >= 30%), as the knob test asks of
    // every style; DARK changes BOUNCY's pitches.
    for (int style : { (int) Long, (int) Pop, (int) UkMin })
    {
        int bad = 0, total = 0;
        std::string firstBad;
        for (auto base : bases)
            for (int roll = 0; roll < 6; ++roll)
            {
                float npb[3];
                int k = 0;
                for (float busy : { 0.f, 0.5f, 1.f })
                {
                    Settings st = defaults (style);
                    st.busy = busy;
                    std::uint32_t seeds[NumParts];
                    for (int q = 0; q < NumParts; ++q)
                        seeds[q] = base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) q * 17u;
                    Groove g {};
                    generate (g, st, seeds, 0x7fu);
                    npb[k++] = (float) g.lane[Bass].count / (float) g.bars;
                }
                ++total;
                const bool ok = npb[0] <= npb[1] + 0.02f && npb[1] <= npb[2] + 0.02f && npb[2] + 1e-3f >= npb[0] * 1.30f;
                if (! ok && bad++ == 0)
                {
                    char buf[160];
                    std::snprintf (buf, sizeof buf, " (seed %u roll %d: %.2f -> %.2f -> %.2f)", base, roll, npb[0], npb[1], npb[2]);
                    firstBad = buf;
                }
            }
        std::snprintf (what, sizeof what, "%s BUSY raises bass density in %d/%d rolls%s", dialectId (style), total - bad, total,
                       firstBad.c_str());
        check (bad == 0, what);
    }
    {
        int same = 0;
        for (auto base : bases)
        {
            std::uint32_t seeds[NumParts];
            for (int q = 0; q < NumParts; ++q)
                seeds[q] = base + (std::uint32_t) q * 17u;
            Settings lo = defaults (Pop), hi = defaults (Pop);
            lo.dark = 0.f;
            hi.dark = 1.f;
            Groove a {}, b {};
            generate (a, lo, seeds, 0x7fu);
            generate (b, hi, seeds, 0x7fu);
            same += hashGroove (a) == hashGroove (b) ? 1 : 0;
        }
        std::snprintf (what, sizeof what, "BOUNCY DARK changes the roll on %d/%d seeds", (int) bases.size() - same, (int) bases.size());
        check (same == 0, what);
    }

    if (report)
        for (int style : { (int) UkMin, (int) Gaskin })
            for (auto base : bases)
            {
                const auto rm = measure (style, base, defaults (style));
                print (dialectId (style), base, rm);
                printRoller (dialectId (style), base, rm);
                printLoop (dialectId (style), base, rm);
            }
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
