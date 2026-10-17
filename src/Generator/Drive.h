#pragma once

#include "Generator.h"

namespace rolla
{

// Stable entry for the macro knobs and the style control.
//
// The processor calls this when DARK, BUSY, BOUNCE, STAB, or the style
// (dialect) moves. partMask marks the unkept lanes. Seeds are the lanes'
// current seeds: this reshapes the source phrase, it does not draw new ones.
//
// INTENSITY is not part of this call. The processor applies
// planIntensity / applyIntensity to the phrase this writes, so moving the
// intensity slider changes presence and arrangement without a new seed.
//
// A generator rebuild in this directory should keep this signature. Change
// the body, not the call the processor makes.

inline void reshapeLanes (Groove& groove,
                          const Settings& settings,
                          const std::uint32_t seeds[NumParts],
                          std::uint32_t partMask)
{
    generate (groove, settings, seeds, partMask);
}

} // namespace rolla
