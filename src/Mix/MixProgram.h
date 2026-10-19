#pragma once

#include <cstddef>

namespace rolla
{

// Baked lane chain from mix_profiles.json (MIX_SPEC §3, §8, §9).
// Fourteen strips: BASS STAB HITS PLUCK PAD VOX, then KICK SNARE CLAP RIM CH OH RIDE PERC.

inline constexpr int kMixLanes = 14;
inline constexpr int kMixDialects = 4;
inline constexpr int kMixMelodic = 6;

enum class MixEqType : int { Off = 0, Bell, LowShelf, HighShelf };
enum class MixSatType : int { None = 0, Softclip, Tape, Tube };

struct MixEqBand
{
    MixEqType type = MixEqType::Off;
    float freqHz = 1000.f;
    float gainDb = 0.f;
    float q = 0.7f;
};

struct MixChain
{
    float hpfHz = 20.f;
    int hpfSlope = 12;
    bool lpfOn = false;
    float lpfHz = 20000.f;
    int lpfSlope = 12;
    MixEqBand eq[2] {};
    int nEq = 0;
    float compThresholdDb = 0.f;
    float compRatio = 1.f;
    float compAttackMs = 10.f;
    float compReleaseMs = 100.f;
    float compKneeDb = 0.f;
    float compTargetGrDb = 0.f;
    MixSatType sat = MixSatType::None;
    float satDriveDb = 0.f;
    float satMix = 0.f;
    float tiltDbPerOct = 0.f;
    float tiltPivotHz = 1000.f;
    float monoBelowHz = 0.f;
    bool forceMono = false;
    float inputTrimDb = 0.f;
    float outputTrimDb = 0.f;
    float tpCeilDb = 24.f;
    float stTargetLufs = -24.f;
    bool valid = false;
};

struct MixOverride
{
    bool used = false;
    bool hasHpfHz = false, hasHpfSlope = false;
    bool hasLpfHz = false, hasLpfSlope = false;
    bool hasEqFreq[2] {}, hasEqGain[2] {}, hasEqQ[2] {};
    bool hasCompAttack = false, hasCompRelease = false, hasCompRatio = false, hasCompKnee = false, hasCompGr = false;
    bool hasSatType = false, hasSatDrive = false, hasSatMix = false;
    bool hasMonoBelow = false;
    float hpfHz = 0.f, lpfHz = 0.f;
    int hpfSlope = 12, lpfSlope = 12;
    float eqFreq[2] {}, eqGain[2] {}, eqQ[2] {};
    float compAttackMs = 0.f, compReleaseMs = 0.f, compRatio = 1.f, compKneeDb = 0.f, compGr = 0.f;
    MixSatType sat = MixSatType::None;
    float satDriveDb = 0.f, satMix = 0.f, monoBelowHz = 0.f;
};

// Keyed like the profile: part 0..5 (bass..vox) and a small preset-id table.
inline constexpr int kMixOverrideSlots = 8;

struct MixProgram
{
    MixChain chain[kMixDialects][kMixLanes] {};
    float sumTrimDb[kMixDialects][64] {};
    float makeupDb[kMixDialects][kMixLanes] {};
    float dialectTrimDb[kMixDialects] {};
    // Extra constant on the MAIN sum, measured so the default full mix sits on the SUM gate.
    // Frozen for a render (same gain on FULL / MUTE / SOLO). Not a limiter.
    float sumAlignDb[kMixDialects] {};
    // Absolute ceiling for the per-lane oversampled clip, after output trim. 24 dB = off.
    // A ceiling under (st_target_lufs + the lane's natural PLR) forces the clip to flatten
    // every hit to hold the target, which squares transients into clicks. KICK, RIM and
    // PERC therefore sit on their targets.json tp_max, and lane level holds the ST target.
    float tpCeilDb[kMixDialects][kMixLanes] {};
    char overrideId[kMixMelodic][kMixOverrideSlots][24] {};
    MixOverride overrideChain[kMixMelodic][kMixOverrideSlots] {};
    int overrideCount[kMixMelodic] {};
    bool loaded = false;

    bool loadFromMemory (const char* profilesJson, int profilesBytes,
                         const char* trimJson, int trimBytes,
                         const char* makeupJson, int makeupBytes);
    void applyOverride (MixChain& dst, int part, const char* presetId) const;
};

const char* mixLaneName (int lane) noexcept;
int mixLaneIndex (const char* name) noexcept;

} // namespace rolla
