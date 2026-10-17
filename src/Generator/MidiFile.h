#pragma once

#include "Pattern.h"
#include <cstdint>
#include <string>
#include <vector>

namespace rolla
{

struct MidiExport
{
    // One SMF per part plus a type-1 file with every lane.
    // Swing is baked into the tick times. Files are .mid (FL will not import .midi).
    static bool writeAll (const std::string& directory,
                          const Groove& groove,
                          int key,
                          float swing,
                          double bpm,
                          std::vector<std::string>& writtenPaths,
                          std::string& error);
};

} // namespace rolla
