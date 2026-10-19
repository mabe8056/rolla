#pragma once

#include "../Generator/Pattern.h"

#include <cstring>

namespace rolla
{

// How a lane preset uses the user's sample folder.
// SYNTH ignores files. SAMPLE replaces the synth when the file is loaded.
// LAYER blends, and only when the lane is set that way. A missing factory
// file tries another sample from the same lane or drum pool. If that pool
// is empty, or the library itself is absent, the matching synth voice plays.
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

// A factory recipe draws from the scanned lane folder. Empty fields are unrestricted.
// lane -1 means the part that is playing. voice -1 is any drum voice. rootPc -1 is any pitch class.
struct PoolFilter
{
    bool on = false;
    int lane = -1;
    int voice = -1;
    int rootPc = -1;
    // -1 any, 1 only tempo-locked phrase loops, 0 excludes them.
    int loop = -1;
    char pack[64] {};
    char category[32] {};
    // -1 does not gate by style. 0..3 is UK MIN, LONG, POP, JACKIN.
    int dialect = -1;
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
// voiceFile is the drum kit map (kick, snare, clap, rim, tomLow, tomMid, hatC, hatO, ride, perc).
// styles is a dialect mask: bit 0 UK MIN, 1 LONG, 2 POP, 3 JACKIN. 0 fits every dialect.
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
    std::uint8_t styles = 0;
    // When on, ROLL picks a file from the scanned pool instead of a hard-coded list.
    PoolFilter filter;
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

inline int partFromLaneName (const char* text) noexcept
{
    if (text == nullptr || text[0] == 0)
        return -1;
    char buf[16] {};
    int n = 0;
    for (int i = 0; text[i] != 0 && n < 15; ++i)
    {
        char c = text[i];
        if (c >= 'a' && c <= 'z')
            c = (char) (c - 32);
        buf[n++] = c;
    }
    if (std::strcmp (buf, "BASS") == 0) return Bass;
    if (std::strcmp (buf, "STAB") == 0) return Stab;
    if (std::strcmp (buf, "HITS") == 0) return Hits;
    if (std::strcmp (buf, "PLUCK") == 0) return Pluck;
    if (std::strcmp (buf, "PAD") == 0) return Pad;
    if (std::strcmp (buf, "VOX") == 0) return Vox;
    if (std::strcmp (buf, "DRUMS") == 0 || std::strcmp (buf, "DRUM") == 0) return Drums;
    return -1;
}

inline int voiceFromFolderName (const char* text) noexcept
{
    if (text == nullptr || text[0] == 0)
        return -1;
    char buf[16] {};
    int n = 0;
    for (int i = 0; text[i] != 0 && n < 15; ++i)
    {
        char c = text[i];
        if (c >= 'a' && c <= 'z')
            c = (char) (c - 32);
        else if (c == '_' || c == ' ')
            continue;
        buf[n++] = c;
    }
    if (std::strcmp (buf, "KICK") == 0) return Kick;
    if (std::strcmp (buf, "SNARE") == 0) return Snare;
    if (std::strcmp (buf, "CLAP") == 0) return Clap;
    if (std::strcmp (buf, "RIM") == 0) return Rim;
    if (std::strcmp (buf, "CH") == 0 || std::strcmp (buf, "HATC") == 0 || std::strcmp (buf, "HATCLOSED") == 0) return HatC;
    if (std::strcmp (buf, "OH") == 0 || std::strcmp (buf, "HATO") == 0 || std::strcmp (buf, "HATOPEN") == 0) return HatO;
    if (std::strcmp (buf, "RIDE") == 0) return Ride;
    if (std::strcmp (buf, "PERC") == 0 || std::strcmp (buf, "PERCUSSION") == 0) return Perc;
    if (std::strcmp (buf, "TOMLOW") == 0 || std::strcmp (buf, "TOMLO") == 0) return TomLow;
    if (std::strcmp (buf, "TOMMID") == 0 || std::strcmp (buf, "TOMMIDDLE") == 0) return TomMid;
    return -1;
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
    bool phraseLoop = false;
    int fileBpm = 0;
};

// Host tempo divided by the loop's tempo. 1 keeps the file's own speed.
inline float phraseTempoStretch (int fileBpm, float hostBpm) noexcept
{
    if (fileBpm < 40 || hostBpm < 20.f)
        return 1.f;
    const float ratio = hostBpm / (float) fileBpm;
    if (ratio < 0.25f)
        return 0.25f;
    if (ratio > 4.f)
        return 4.f;
    return ratio;
}

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
