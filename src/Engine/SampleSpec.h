#pragma once

#include "../Generator/Pattern.h"

#include <cstring>

namespace rolla
{

// How a lane preset uses the user's sample folder.
// SYNTH ignores files. SAMPLE replaces the synth when the file is loaded.
// LAYER blends. A missing file always keeps the synth.
enum class SampleMode : int
{
    Synth = 0,
    Sample = 1,
    Layer = 2
};

struct SampleLoop
{
    bool on = false;
    // Seconds, unless any of the three is above 32: those are file-rate frames
    // (PRESET_SPEC). end 0 means the file end.
    float start = 0.f;
    float end = 0.f;
    float xfade = 0.f;
};

// One multisample zone. lo/hi/root are MIDI note numbers (C4 = 60).
struct SampleZone
{
    char file[96] {};
    int root = 60;
    int lo = 0;
    int hi = 127;
};

// Preset sample block. Paths are relative to the samples root, with '/' separators.
// pool entries are what ROLL/SHUFFLE rotates. zones are key-mapped multisamples.
// voiceFile is the drum kit map (kick, snare, clap, rim, tomLow, tomMid, hatC, hatO, ride).
struct SampleSpec
{
    static constexpr int kPool = 8;
    static constexpr int kZones = 8;

    SampleMode mode = SampleMode::Synth;
    char file[96] {};
    int root = 38;
    float start = 0.f; // seconds
    bool reverse = false;
    bool slice = false; // vox: each note starts at the next detected transient
    SampleLoop loop;
    float layerMix = 0.5f; // 0 synth, 1 sample
    int poolCount = 0;
    char pool[kPool][96] {};
    int poolRoot[kPool] {};
    int zoneCount = 0;
    SampleZone zones[kZones] {};
    char voiceFile[NumDrumVoices][96] {};
};

inline void copySamplePath (char* dst, int n, const char* src) noexcept
{
    if (dst == nullptr || n <= 0)
        return;
    if (src == nullptr)
        src = "";
    std::strncpy (dst, src, (size_t) n - 1);
    dst[n - 1] = 0;
}

// Scientific pitch: C4 = 60, D1 = 26. "D#1" / "Db1" accepted. Empty keeps fallback.
inline int midiFromNoteName (const char* text, int fallback) noexcept
{
    if (text == nullptr || text[0] == 0)
        return fallback;
    int i = 0;
    while (text[i] == ' ')
        ++i;
    const char n = text[i];
    int semi = 0;
    switch (n)
    {
        case 'C': case 'c': semi = 0; break;
        case 'D': case 'd': semi = 2; break;
        case 'E': case 'e': semi = 4; break;
        case 'F': case 'f': semi = 5; break;
        case 'G': case 'g': semi = 7; break;
        case 'A': case 'a': semi = 9; break;
        case 'B': case 'b': semi = 11; break;
        default: return fallback;
    }
    ++i;
    if (text[i] == '#' || text[i] == 's')
    {
        ++semi;
        ++i;
    }
    else if (text[i] == 'b')
    {
        --semi;
        ++i;
    }
    int sign = 1;
    if (text[i] == '-')
    {
        sign = -1;
        ++i;
    }
    if (text[i] < '0' || text[i] > '9')
        return fallback;
    int oct = 0;
    while (text[i] >= '0' && text[i] <= '9')
    {
        oct = oct * 10 + (text[i] - '0');
        ++i;
    }
    oct *= sign;
    const int midi = (oct + 1) * 12 + semi;
    if (midi < 0 || midi > 127)
        return fallback;
    return midi;
}

inline SampleMode sampleModeFrom (const char* text) noexcept
{
    if (text == nullptr)
        return SampleMode::Synth;
    if (text[0] == 'S' || text[0] == 's')
    {
        if (text[1] == 'A' || text[1] == 'a')
            return SampleMode::Sample;
    }
    if (text[0] == 'L' || text[0] == 'l')
        return SampleMode::Layer;
    return SampleMode::Synth;
}

// Which relative path and root a note should use. Empty file means "nothing mapped".
struct SampleChoice
{
    const char* file = "";
    int root = 60;
    bool slice = false;
    int sliceIndex = 0;
};

inline SampleChoice chooseSample (const SampleSpec& spec, int midiNote, int pick, int sliceIndex) noexcept
{
    SampleChoice c;
    c.slice = spec.slice;
    c.sliceIndex = sliceIndex;
    c.root = spec.root;
    if (spec.zoneCount > 0)
    {
        for (int i = 0; i < spec.zoneCount; ++i)
        {
            const auto& z = spec.zones[i];
            if (midiNote >= z.lo && midiNote <= z.hi && z.file[0] != 0)
            {
                c.file = z.file;
                c.root = z.root;
                return c;
            }
        }
    }
    if (spec.poolCount > 0)
    {
        // Pick 0 is the primary file. Later picks rotate the shuffle pool.
        const bool primary = spec.file[0] != 0;
        const int span = spec.poolCount + (primary ? 1 : 0);
        const int i = (pick % span + span) % span;
        if (! primary || i > 0)
        {
            const int poolIndex = primary ? i - 1 : i;
            c.file = spec.pool[poolIndex];
            if (spec.poolRoot[poolIndex] > 0)
                c.root = spec.poolRoot[poolIndex];
            return c;
        }
    }
    c.file = spec.file;
    return c;
}

inline const char* drumSampleFile (const SampleSpec& spec, int voice) noexcept
{
    if (voice >= 0 && voice < NumDrumVoices && spec.voiceFile[voice][0] != 0)
        return spec.voiceFile[voice];
    if (spec.poolCount > 0)
        return spec.pool[0];
    return spec.file;
}

} // namespace rolla
