#pragma once

#include <cstdint>
#include <cstring>

namespace rolla
{

enum Part : int
{
    Bass = 0,
    Stab,
    Hits,
    Pluck,
    Pad,
    Vox,
    Drums,
    NumParts
};

enum DrumVoice : int
{
    Kick = 0,
    Snare,
    Clap,
    Rim,
    TomLow,
    TomMid,
    HatC,
    HatO,
    Ride,
    Perc, // shaker / UK-minimal percussion; maps to the PERC sample slot
    NumDrumVoices
};

enum Dialect : int
{
    UkMin = 0,
    Long,
    Pop,
    Gaskin,
    NumDialects
};

inline constexpr int kMaxBars = 8;
inline constexpr int kStepsPerBar = 16;
inline constexpr int kMaxSteps = kMaxBars * kStepsPerBar;
inline constexpr int kMaxNotes = 72;

enum NoteFlags : std::uint8_t
{
    NoteGlide = 1,
    NoteGhost = 2
};

struct Note
{
    float start = 0.f;
    float length = 1.f;
    std::int8_t chord[4] { 0, 0, 0, 0 };
    std::uint8_t nChord = 1;
    std::uint8_t vel = 100;
    std::uint8_t flags = 0;
    std::uint8_t timbre = 0;
};

struct Lane
{
    std::uint32_t seed = 1;
    int count = 0;
    Note notes[kMaxNotes] {};
};

struct DrumLane
{
    std::uint32_t seed = 1;
    std::uint8_t vel[NumDrumVoices][kMaxSteps] {};
    std::uint8_t accent[kMaxSteps] {};
};

struct Groove
{
    int bars = 8;
    int key = 2;
    int dialect = UkMin;
    std::uint32_t generation = 1;
    float guardScore = 0.f;
    int guardLevel = 0; // 0 pass, 1 flag, 2 fail
    char guardRef[24] = "ORIGINAL";
    Lane lane[NumParts] {};
    DrumLane drums {};
};

struct Settings
{
    float dark = 0.55f;
    float busy = 0.40f;
    float bounce = 0.45f;
    float stab = 0.55f;
    int dialect = UkMin;
    int key = 2;
    int bars = 8;
    bool fill = false;
};

inline const char* partId (int p) noexcept
{
    static const char* ids[] = { "bass", "stab", "hits", "pluck", "pad", "vox", "drums" };
    return ids[p];
}

inline const char* partName (int p) noexcept
{
    static const char* names[] = { "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX CUTS", "DRUMS" };
    return names[p];
}

inline const char* dialectId (int d) noexcept
{
    static const char* ids[] = { "ukmin", "long", "pop", "gaskin" };
    return ids[d];
}

inline const char* dialectName (int d) noexcept
{
    static const char* names[] = { "UK MIN", "LONG", "POP", "GASKIN 90s" };
    return names[d];
}

inline const char* drumName (int v) noexcept
{
    static const char* names[] = { "KICK", "SNARE", "CLAP", "RIM", "LOW", "MID", "CH", "OH", "RIDE", "PERC" };
    return names[v];
}

inline int barsFromIndex (int index) noexcept
{
    return index <= 0 ? 2 : (index == 1 ? 4 : 8);
}

inline int indexFromBars (int bars) noexcept
{
    return bars <= 2 ? 0 : (bars <= 4 ? 1 : 2);
}

inline const char* keyName (int key) noexcept
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return names[(key % 12 + 12) % 12];
}

inline int drumMidiNote (int voice) noexcept
{
    static const int notes[] = { 36, 38, 39, 37, 45, 47, 42, 46, 51, 70 };
    return notes[voice];
}

inline void clearLane (Lane& lane) noexcept
{
    lane.count = 0;
}

inline void clearDrums (DrumLane& d) noexcept
{
    std::memset (d.vel, 0, sizeof (d.vel));
    std::memset (d.accent, 0, sizeof (d.accent));
}

} // namespace rolla
