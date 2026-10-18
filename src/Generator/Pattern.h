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
inline constexpr int kMaxNotes = 96;

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
    // plan|contour|cells|soft breaks. Soft breaks are the freedom budget, not a reject.
    char brokenRules[192] = "";
    float penalty = 0.f;
    // Candidates drawn this call, and how many a hard rule threw out.
    int draws = 0;
    int hardDraws = 0;
    Lane lane[NumParts] {};
    DrumLane drums {};
};

struct Settings
{
    float dark = 0.55f;
    float busy = 0.40f;
    float bounce = 0.45f;
    float stab = 0.55f;
    // How hard the selected style's own weights apply. 0 is a stripped skeleton,
    // 0.5 is the normal style, 1 is the full style: denser cells, more lanes,
    // and polymeter. This is not the 0-100 arrangement slider.
    float styleIntensity = 0.5f;
    int dialect = UkMin;
    int key = 2;
    int bars = 8;
    bool fill = false;
    // 0 hugs the soft bands, 0.35 is the normal lane, 0.6 is wild.
    // BUSY and BOUNCE extremes raise the effective value. Soft rules never reject.
    float freedom = 0.35f;
    // Optional global chaos. 0 leaves the hidden per-roll bump as the only extra.
    float chaos = 0.f;
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

// Provisional until Melak signs off. This one line renames the pills,
// the dialect parameter, and the preset line together.
inline constexpr const char* kStyleLabels[NumDialects] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };

// Display names and the hover card for the four style pills.
// The ids above stay put.
struct StyleCopy
{
    const char* name;
    const char* tip;
};

inline const StyleCopy& styleCopy (int d) noexcept
{
    static const StyleCopy rows[] = {
        { kStyleLabels[UkMin],
          "Groove\tswing 52%\n"
          "Notes\t1-4.5 sixteenths\n"
          "Bass\tpluck\n"
          "Stab\tdub\n"
          "Pad\tair\n"
          "Plays\tbass, stab, pluck, pad, vox, drums" },
        { kStyleLabels[Long],
          "Groove\tswing 54%\n"
          "Notes\t2-8 sixteenths\n"
          "Bass\tround\n"
          "Stab\tswell\n"
          "Pad\twide\n"
          "Plays\tbass, stab, pluck, pad, vox, drums" },
        { kStyleLabels[Pop],
          "Groove\tswing 56.7%\n"
          "Notes\t1-1.25 sixteenths\n"
          "Bass\tclick\n"
          "Stab\tclick\n"
          "Pad\tair\n"
          "Plays\tbass, stab, pluck, pad, vox, drums" },
        { kStyleLabels[Gaskin],
          "Groove\tswing 56.5%\n"
          "Notes\t2 sixteenths\n"
          "Bass\tround\n"
          "Stab\tswell\n"
          "Pad\tdark\n"
          "Plays\tbass, stab, hits, pluck, pad, vox, drums" },
    };
    return rows[d < 0 ? 0 : (d >= NumDialects ? NumDialects - 1 : d)];
}

inline const char* dialectName (int d) noexcept
{
    return styleCopy (d).name;
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
