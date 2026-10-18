#include "PatternStats.h"
#include "Generator/Generator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace rolla
{
namespace
{

constexpr int kRolls = 6000;
constexpr int kEditSample = 48;

std::string bassKey (const Lane& lane)
{
    std::string s;
    s.reserve ((size_t) lane.count * 8);
    for (int i = 0; i < lane.count; ++i)
    {
        const auto& n = lane.notes[i];
        s.push_back ((char) (1 + (int) std::lround (n.start)));
        s.push_back ((char) (1 + (int) std::lround (n.length * 2.f)));
        s.push_back ((char) (n.chord[0] + 40));
        s.push_back ((char) ((n.flags & NoteGhost) ? 2 : 1));
    }
    return s;
}

std::string stabKey (const Lane& lane)
{
    std::string s;
    for (int i = 0; i < lane.count; ++i)
    {
        const auto& n = lane.notes[i];
        s.push_back ((char) (1 + (int) std::lround (n.start)));
        s.push_back ((char) (1 + (int) std::lround (n.length * 2.f)));
        s.push_back ((char) n.nChord);
        for (int c = 0; c < n.nChord; ++c)
            s.push_back ((char) (n.chord[c] + 40));
    }
    return s;
}

std::string drumKey (const DrumLane& d, int bars)
{
    std::string s;
    s.resize ((size_t) bars * 16);
    for (int t = 0; t < bars * 16; ++t)
    {
        unsigned bits = 0;
        for (int v = 0; v < NumDrumVoices; ++v)
            if (d.vel[v][t])
                bits |= 1u << v;
        s[(size_t) t] = (char) bits;
    }
    return s;
}

std::string laneKey (const Groove& g, int part)
{
    if (part == Drums)
        return drumKey (g.drums, g.bars);
    if (part == Stab || part == Hits || part == Pad)
        return stabKey (g.lane[part]);
    return bassKey (g.lane[part]);
}

std::vector<int> tokens (const std::string& s)
{
    std::vector<int> t;
    t.reserve (s.size());
    for (char c : s)
        t.push_back ((int) (unsigned char) c);
    return t;
}

int edit (const std::vector<int>& a, const std::vector<int>& b)
{
    const int n = (int) a.size();
    const int m = (int) b.size();
    std::vector<int> prev ((size_t) m + 1), cur ((size_t) m + 1);
    for (int j = 0; j <= m; ++j)
        prev[(size_t) j] = j;
    for (int i = 1; i <= n; ++i)
    {
        cur[0] = i;
        for (int j = 1; j <= m; ++j)
        {
            const int cost = a[(size_t) i - 1] == b[(size_t) j - 1] ? 0 : 1;
            const int del = prev[(size_t) j] + 1;
            const int ins = cur[(size_t) j - 1] + 1;
            const int sub = prev[(size_t) j - 1] + cost;
            cur[(size_t) j] = std::min (del, std::min (ins, sub));
        }
        prev.swap (cur);
    }
    return prev[(size_t) m];
}

double meanEdit (const std::vector<std::string>& sample)
{
    const int ns = (int) sample.size();
    std::vector<std::vector<int>> tok;
    tok.reserve ((size_t) ns);
    for (const auto& s : sample)
        tok.push_back (tokens (s));
    double dist = 0;
    int pairs = 0;
    for (int a = 0; a < ns; ++a)
        for (int b = a + 1; b < ns; ++b)
        {
            dist += edit (tok[(size_t) a], tok[(size_t) b]);
            ++pairs;
        }
    return pairs ? dist / (double) pairs : 0;
}

struct BassStats
{
    float notes = 0;
    float offKick = 0;
    float dark = 0;
    float odd = 0;
    float iv = 0;
};

BassStats measureBass (const Groove& g)
{
    BassStats s;
    int off = 0, on = 0, odd = 0, dark = 0, pitches = 0;
    float iv = 0;
    for (int i = 0; i < g.lane[Bass].count; ++i)
    {
        const auto& n = g.lane[Bass].notes[i];
        const int st = (int) std::floor (n.start + 1.0e-3f);
        if (st >= 0 && st < g.bars * 16 && g.drums.vel[Kick][st] > 0)
            ++on;
        else
            ++off;
        if ((st & 1) != 0)
            ++odd;
        const int pc = (n.chord[0] % 12 + 12) % 12;
        if (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10)
            ++dark;
        if (pc != 0)
            ++pitches;
        iv += n.chord[0];
    }
    const int notes = g.lane[Bass].count;
    s.notes = (float) notes / (float) std::max (1, g.bars);
    s.offKick = (on + off) ? (float) off / (float) (on + off) : 0;
    s.odd = notes ? (float) odd / (float) notes : 0;
    s.dark = pitches ? (float) dark / (float) pitches : 0;
    s.iv = notes ? iv / (float) notes : 0;
    return s;
}

Settings baseSettings()
{
    Settings base;
    base.dark = 0.55f;
    base.busy = 0.40f;
    base.bounce = 0.45f;
    base.stab = 0.55f;
    base.key = 2;
    base.bars = 8;
    base.fill = false;
    return base;
}

void fillMacro (MacroDelta& d, float notesLo, float notesHi, float oddLo, float oddHi,
                float darkLo, float darkHi, float ivLo, float ivHi, float offLo, float offHi,
                float stabLo, float stabHi)
{
    d.notesLo = notesLo;
    d.notesHi = notesHi;
    d.oddLo = oddLo;
    d.oddHi = oddHi;
    d.darkLo = darkLo;
    d.darkHi = darkHi;
    d.ivLo = ivLo;
    d.ivHi = ivHi;
    d.offLo = offLo;
    d.offHi = offHi;
    d.stabLo = stabLo;
    d.stabHi = stabHi;
}

} // namespace

int measureVariety (StyleStat out[NumDialects])
{
    const int lanes[] = { Bass, Stab, Hits, Pluck, Pad, Vox, Drums };
    Settings base = baseSettings();
    int nondet = 0;
    for (int style = 0; style < NumDialects; ++style)
    {
        base.dialect = style;
        std::unordered_set<std::string> uniq[NumParts];
        std::vector<std::string> sample[NumParts];
        int passed = 0;
        for (int i = 0; i < kRolls; ++i)
        {
            std::uint32_t seeds[NumParts];
            for (int p = 0; p < NumParts; ++p)
                seeds[p] = (std::uint32_t) (i + 1) * 0x9E3779B9u + (std::uint32_t) (p * 17 + style * 101);
            Groove g {};
            generate (g, base, seeds, 0x7fu);
            if (i == 0)
            {
                Groove again {};
                generate (again, base, seeds, 0x7fu);
                if (bassKey (g.lane[Bass]) != bassKey (again.lane[Bass]))
                    ++nondet;
            }
            if (std::strcmp (g.guardRef, "ORIGINAL") == 0)
                ++passed;
            for (int part : lanes)
            {
                auto key = laneKey (g, part);
                uniq[part].insert (key);
                if ((int) sample[part].size() < kEditSample)
                    sample[part].push_back (std::move (key));
            }
        }
        out[style].rolls = kRolls;
        out[style].rulesPassed = passed;
        for (int part : lanes)
        {
            out[style].lane[part].unique = (int) uniq[part].size();
            out[style].lane[part].meanEdit = meanEdit (sample[part]);
        }
    }
    return nondet;
}

void measureMacros (MacroDelta& busy, MacroDelta& bounce, MacroDelta& dark, MacroDelta& stab)
{
    Settings base = baseSettings();
    base.dialect = UkMin;
    constexpr int n = 200;
    auto sweep = [&] (float Settings::* field, float lo, float hi, MacroDelta& dest)
    {
        BassStats a {}, b {};
        int stA = 0, stB = 0;
        for (int i = 0; i < n; ++i)
        {
            Settings s = base;
            s.*field = lo;
            std::uint32_t seeds[NumParts];
            for (int p = 0; p < NumParts; ++p)
                seeds[p] = (std::uint32_t) (1000 + i) * 17u + (std::uint32_t) p * 3u;
            Groove g {};
            generate (g, s, seeds, 0x7fu);
            const auto ma = measureBass (g);
            a.notes += ma.notes;
            a.offKick += ma.offKick;
            a.dark += ma.dark;
            a.odd += ma.odd;
            a.iv += ma.iv;
            stA += g.lane[Stab].count;
            s.*field = hi;
            generate (g, s, seeds, 0x7fu);
            const auto mb = measureBass (g);
            b.notes += mb.notes;
            b.offKick += mb.offKick;
            b.dark += mb.dark;
            b.odd += mb.odd;
            b.iv += mb.iv;
            stB += g.lane[Stab].count;
        }
        const float inv = 1.f / (float) n;
        fillMacro (dest,
                   a.notes * inv, b.notes * inv,
                   a.odd * inv, b.odd * inv,
                   a.dark * inv, b.dark * inv,
                   a.iv * inv, b.iv * inv,
                   a.offKick * inv, b.offKick * inv,
                   (float) stA * inv, (float) stB * inv);
    };
    sweep (&Settings::busy, 0.05f, 0.95f, busy);
    sweep (&Settings::bounce, 0.05f, 0.95f, bounce);
    sweep (&Settings::dark, 0.05f, 0.95f, dark);
    sweep (&Settings::stab, 0.0f, 1.0f, stab);
}

} // namespace rolla
