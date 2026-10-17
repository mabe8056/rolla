// Dump generated grooves as JSON lines for tools/score_rolls.
// Pitch numbers are absolute MIDI, using the same bases as the engine.

#include "Generator/Generator.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>

namespace
{

int basePitch (int part, int key, int dialect)
{
    const int k = (key % 12 + 12) % 12;
    if (part == rolla::Bass)
        return 12 * ((dialect == rolla::Pop ? 2 : 1) + 1) + k;
    if (part == rolla::Pluck)
        return 72 + k;
    if (part == rolla::Vox)
        return 64 + k;
    return 60 + k;
}

void writeLane (std::ostream& out, const rolla::Lane& lane, int part, int key, int dialect)
{
    const int base = basePitch (part, key, dialect);
    out << "[";
    for (int i = 0; i < lane.count; ++i)
    {
        const auto& n = lane.notes[i];
        if (i)
            out << ",";
        out << "{\"start\":" << n.start
            << ",\"len\":" << n.length
            << ",\"vel\":" << (int) n.vel
            << ",\"glide\":" << (((n.flags & rolla::NoteGlide) != 0) ? "true" : "false")
            << ",\"ghost\":" << (((n.flags & rolla::NoteGhost) != 0) ? "true" : "false")
            << ",\"pitches\":[";
        const int nc = n.nChord < 1 ? 1 : (int) n.nChord;
        for (int c = 0; c < nc; ++c)
        {
            if (c)
                out << ",";
            out << (base + (int) n.chord[c]);
        }
        out << "]}";
    }
    out << "]";
}

} // namespace

int main (int argc, char** argv)
{
    const std::string path = argc > 1 ? argv[1] : "/tmp/grooves.jsonl";
    const int rolls = argc > 2 ? std::max (1, std::atoi (argv[2])) : 1000;
    std::ofstream out (path);
    if (! out)
    {
        std::cerr << "cannot write " << path << "\n";
        return 1;
    }

    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        rolla::Settings settings;
        settings.dialect = style;
        settings.bars = 8;
        settings.key = 2;
        for (int i = 0; i < rolls; ++i)
        {
            std::uint32_t seeds[rolla::NumParts];
            for (int p = 0; p < rolla::NumParts; ++p)
                seeds[p] = (std::uint32_t) (i + 1) * 0x9E3779B9u + (std::uint32_t) (p * 17 + style * 101);
            rolla::Groove g {};
            rolla::generate (g, settings, seeds, 0x7fu);
            out << "{\"style\":\"" << rolla::dialectId (style)
                << "\",\"index\":" << i
                << ",\"key\":" << g.key
                << ",\"bars\":" << g.bars
                << ",\"guard\":\"" << g.guardRef
                << "\",\"tag\":\"" << g.brokenRules
                << "\",\"lanes\":{";
            for (int p = 0; p < rolla::Drums; ++p)
            {
                if (p)
                    out << ",";
                out << "\"" << rolla::partId (p) << "\":";
                writeLane (out, g.lane[p], p, g.key, g.dialect);
            }
            out << "},\"drums\":[";
            bool any = false;
            for (int v = 0; v < rolla::NumDrumVoices; ++v)
            {
                for (int t = 0; t < g.bars * 16; ++t)
                {
                    if (g.drums.vel[v][t] == 0)
                        continue;
                    if (any)
                        out << ",";
                    any = true;
                    out << "{\"voice\":" << v << ",\"step\":" << t
                        << ",\"vel\":" << (int) g.drums.vel[v][t] << "}";
                }
            }
            out << "]}\n";
        }
        std::cerr << "dumped " << rolls << " " << rolla::dialectId (style) << "\n";
    }
    return 0;
}
