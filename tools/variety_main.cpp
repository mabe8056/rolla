#include "PatternStats.h"
#include "Generator/Generator.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>

namespace
{

std::string bassKey (const rolla::Lane& lane)
{
    std::string s;
    for (int i = 0; i < lane.count; ++i)
    {
        const auto& n = lane.notes[i];
        s.push_back ((char) (1 + (int) std::lround (n.start)));
        s.push_back ((char) (1 + (int) std::lround (n.length * 2.f)));
        s.push_back ((char) (n.chord[0] + 40));
        s.push_back ((char) ((n.flags & rolla::NoteGhost) ? 2 : 1));
    }
    return s;
}

std::string stabKey (const rolla::Lane& lane)
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

void printMacro (const char* name, const rolla::MacroDelta& d)
{
    std::cout << name
              << " notes " << d.notesLo << " -> " << d.notesHi
              << " odd " << d.oddLo << " -> " << d.oddHi
              << " dark " << d.darkLo << " -> " << d.darkHi
              << " iv " << d.ivLo << " -> " << d.ivHi
              << " off " << d.offLo << " -> " << d.offHi
              << " stabs " << d.stabLo << " -> " << d.stabHi
              << "\n";
}

} // namespace

int main()
{
    const int lanes[] = { rolla::Bass, rolla::Stab, rolla::Hits, rolla::Pluck, rolla::Pad, rolla::Vox, rolla::Drums };
    rolla::StyleStat stats[rolla::NumDialects] {};
    int failures = rolla::measureVariety (stats);
    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        std::cout << rolla::dialectName (style) << " rules " << stats[style].rulesPassed
                  << "/" << stats[style].rolls << "\n";
        for (int part : lanes)
        {
            const auto& lane = stats[style].lane[part];
            std::cout << "  " << rolla::partId (part) << " unique " << lane.unique
                      << " edit " << lane.meanEdit << "\n";
            if (lane.unique < 5000)
                ++failures;
        }
        if (stats[style].rulesPassed != stats[style].rolls)
            ++failures;
    }

    rolla::MacroDelta busy, bounce, dark, stab;
    rolla::measureMacros (busy, bounce, dark, stab);
    printMacro ("busy", busy);
    printMacro ("bounce", bounce);
    printMacro ("dark", dark);
    printMacro ("stab", stab);
    if (busy.notesHi < busy.notesLo + 2.f)
        ++failures;
    if (bounce.oddHi < bounce.oddLo + 0.25f)
        ++failures;
    if (dark.darkHi < dark.darkLo + 0.40f)
        ++failures;
    if (dark.ivHi > dark.ivLo - 1.5f)
        ++failures;
    if (stab.stabHi < stab.stabLo + 6.f)
        ++failures;

    rolla::Settings s;
    s.dark = 0.55f;
    s.busy = 0.40f;
    s.bounce = 0.45f;
    s.stab = 0.55f;
    s.dialect = rolla::UkMin;
    s.key = 2;
    s.bars = 8;
    s.fill = false;
    std::uint32_t seeds[rolla::NumParts] = { 11, 22, 33, 44, 55, 66, 77 };
    rolla::Groove g {};
    rolla::generate (g, s, seeds, 0x7fu);
    const auto bass = bassKey (g.lane[rolla::Bass]);
    const auto stabBefore = stabKey (g.lane[rolla::Stab]);
    seeds[rolla::Stab] = 99;
    rolla::generate (g, s, seeds, 1u << rolla::Stab);
    if (bassKey (g.lane[rolla::Bass]) != bass)
    {
        std::cerr << "keep failed: bass moved\n";
        ++failures;
    }
    if (stabKey (g.lane[rolla::Stab]) == stabBefore)
    {
        std::cerr << "shuffle failed: stab unchanged\n";
        ++failures;
    }
    int clash = 0, n = 0;
    for (int i = 0; i < g.lane[rolla::Stab].count; ++i)
    {
        const int st = (int) std::floor (g.lane[rolla::Stab].notes[i].start + 1.0e-3f);
        ++n;
        for (int k = 0; k < g.lane[rolla::Bass].count; ++k)
            if ((int) std::floor (g.lane[rolla::Bass].notes[k].start + 1.0e-3f) == st)
                ++clash;
    }
    std::cout << "locked bass, new stab clash " << clash << "/" << n << "\n";
    if (n > 0 && (float) clash / (float) n > 0.80f)
        ++failures;

    std::cout << (failures ? "FAIL\n" : "PASS\n");
    return failures ? 1 : 0;
}
