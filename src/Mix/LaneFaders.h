#pragma once

// Sample-true lane faders (EngineParams::sampleTrue). Every library sample is already
// loudness-matched at load (-24 LUFS integrated, SamplePrep::loudnessGainDb), so a lane
// is that sample x velocity x one static gain from this table. No EQ, compressor,
// saturation, auto-level or clip: the sample plays as recorded.
//
// Values: dB relative to the kick, per dialect. Seeded from tests/mix/targets.json
// styles.<D>.lanes.<L>.re_kick (the reference tracks' lane balance), then corrected once
// by the measured median miss on 20 sample-true renders per style (Music-team mix grade,
// Oct 19, batch seed 2026101851, BUSY 0.40): BASS -1.4..-2.2, PAD +2..+3.5, VOX +2.9..+5.4 dB.
// Drum voices keep the reference values (the grade does not measure them per voice). Lane order is
// the mix strip's (MixProgram kLaneNames): BASS STAB HITS PLUCK PAD VOX, then the drum
// voices KICK SNARE CLAP RIM CH OH RIDE PERC. Toms have no reference row and use PERC.

namespace rolla
{
inline constexpr int kFaderLanes = 14;
inline constexpr int kFaderPerc = 13;

inline constexpr float kLaneFaderDb[4][kFaderLanes] = {
    // BASS  STAB   HITS  PLUCK   PAD    VOX    KICK SNARE  CLAP   RIM     CH     OH   RIDE   PERC
    {  -3.2f,  -1.9f,  -4.0f,  -3.1f,  -7.0f,   0.0f,   0.0f,  -2.5f,  -2.5f,  -7.5f, -16.6f,  -4.6f, -22.6f,  -7.6f }, // UK MIN
    {  -2.6f,  -9.6f,  -4.0f,  -8.0f,  -5.8f,  -0.6f,   0.0f,  -2.3f,  -2.3f,  -7.3f, -14.5f,  -2.5f, -20.5f,  -8.7f }, // LONG
    {  -3.1f,  -4.4f,  -4.0f,  -1.1f,  -8.2f,   3.0f,   0.0f,  -5.3f,  -5.3f, -10.3f, -18.6f,  -6.6f, -24.6f, -12.5f }, // POP
    {  -2.4f,  -1.8f,   0.5f,  -0.6f,  -4.5f,   1.4f,   0.0f,  -5.2f,  -5.2f, -10.2f, -18.1f,  -6.1f, -24.1f, -11.2f }, // JACKIN
};

// Per-dialect master gain on the sample-true sum: lands the mix's short-term max near
// -19 LUFS (MIX_SPEC) from the same renders. UK MIN, LONG, POP, JACKIN.
inline constexpr float kSampleTrueMasterDb[4] = { 2.1f, 2.2f, 1.6f, 1.3f };

inline float laneFaderDb (int dialect, int lane) noexcept
{
    const int d = dialect >= 0 && dialect < 4 ? dialect : 0;
    const int l = lane >= 0 && lane < kFaderLanes ? lane : kFaderPerc;
    return kLaneFaderDb[d][l];
}

// Master true-peak guard ceiling on the sample-true path: a safety for rare peaks,
// not a level control (samples load capped at -6 dBTP each, so -6 on the sum would
// limit all the time).
inline constexpr float kSampleTrueCeilingDbtp = -1.f;
} // namespace rolla
