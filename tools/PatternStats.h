#pragma once

#include "Generator/Pattern.h"

namespace rolla
{

struct LaneStat
{
    int unique = 0;
    double meanEdit = 0.0;
};

struct StyleStat
{
    int rulesPassed = 0;
    int rolls = 0;
    LaneStat lane[NumParts];
};

struct MacroDelta
{
    float notesLo = 0, notesHi = 0;
    float oddLo = 0, oddHi = 0;
    float darkLo = 0, darkHi = 0;
    float ivLo = 0, ivHi = 0;
    float offLo = 0, offHi = 0;
    float stabLo = 0, stabHi = 0;
};

// 6000 seeds per style. Edit distance is the mean pairwise Levenshtein
// over the first 48 patterns (pitch, length, and onset; drums are step masks).
// Returns the number of styles whose first seed was not deterministic.
int measureVariety (StyleStat out[NumDialects]);
void measureMacros (MacroDelta& busy, MacroDelta& bounce, MacroDelta& dark, MacroDelta& stab);

} // namespace rolla
