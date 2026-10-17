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

    // One lane, one type-0 SMF. Swing is baked into the ticks. .mid, not .midi.
    static bool writePart (const std::string& path,
                           const Groove& groove,
                           int part,
                           int key,
                           float swing,
                           double bpm,
                           std::string& error);

    // One type-1 file. Track 0 is tempo. Each lane after that is its own track.
    static bool writeMultitrack (const std::string& path,
                                 const Groove& groove,
                                 int key,
                                 float swing,
                                 double bpm,
                                 std::string& error);
};

} // namespace rolla
