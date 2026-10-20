#pragma once

#include <vector>

namespace rolla
{

struct PrepReport
{
    float gainDb = 0.f;
    float lufs = -70.f;
    bool capped = false;
    bool silent = false;
    int leadTrimFrames = 0;
    int frames = 0;
};

struct PreparedBuffer
{
    std::vector<float> left;
    std::vector<float> right;
    int frames = 0;
    double rate = 48000.0;
    PrepReport report;
};

// In-memory MIX_SPEC §4 prep. The caller's file is never written.
// channels may be one pointer (mono) or more; extra channels fold to stereo.
PreparedBuffer prepareSample (const float* const* channels, int numChannels, int frames, double rate);

// -1 when the region is not voiced enough (under 25% of hops).
int detectRootMidi (const float* mono, int frames, double rate, bool tail);

// Last `_C#`, `_Dm`, `_Ds`, `_C2` token. Octave defaults to 3. -1 when none.
int midiFromFilename (const char* path) noexcept;
// True when the key token midiFromFilename found is minor (Cm, C_min, C#min, ...).
bool minorFromFilename (const char* path) noexcept;

// Load-time targets from MIX_SPEC §4 and the mix gate: prep.lufs_target and
// prep.tp_cap / invariants.sum_tp (sum held at -6 dBTP). The file is not written.
inline constexpr float kLoadLufsTarget = -24.f;
inline constexpr float kLoadTruePeakCapDb = -6.f;

// Gain in dB that brings the buffer to kLoadLufsTarget, capped at kLoadTruePeakCapDb.
float loudnessGainDb (const std::vector<float>& left, const std::vector<float>& right, double rate);

// Lanes: pitch over the first 3 s, then the filename, then C3.
// Kick: tail f0, then the filename, then C3. Other drums stay on C3.
int chooseSlotRoot (int slot, const float* mono, int frames, double rate, const char* path);

} // namespace rolla
