#pragma once

#include "../Generator/Generator.h"

namespace rolla
{

// Writes the drum lane from the kit rules (PRESET_SPEC §9.6).
//
// Fixed, and stable across SHUFFLE: 4/4 kick, clap on 2 and 4, and the kit's
// hat grid. The seed only moves the sparse layer (hat accent jitter ±1 dB,
// ghost, tom, bar-8 open-hat lift). KEEP is the generator's part mask: this
// function is called only when the drums lane is being regenerated.
//
// Swing stays in the engine (swungStart). Kick and clap sit on even 16ths, so
// they stay straight. Hats on "e" / "a", the shaker's "e" / "a", the ghost,
// and the fill land on odd 16ths and take the swing delay. "&" (open hat, and
// the shaker accent) is an even 16th and stays put. Jackin has no shaker.
void writeDrumPattern (Groove& groove, const Settings& settings, Rng& rng);

} // namespace rolla
