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
    Jackin,
    NumDialects
};

inline constexpr int kMaxBars = 8;
inline constexpr int kStepsPerBar = 16;
inline constexpr int kMaxSteps = kMaxBars * kStepsPerBar;
inline constexpr int kMaxNotes = 96;

enum NoteFlags : std::uint8_t
{
    NoteGlide = 1,
    NoteGhost = 2,
    // A STYLE overlay note: a line phrased over the whole loop (the pluck's 3/5/7-sixteenth
    // line). The hook restatement leaves it where it was written (PhraseLock.h).
    NoteLayer = 4
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
    // BOUNCY bass mode of the last bass roll: 0 not BOUNCY, 1 bounce, 2 pulse.
    // MidiExport writes it into the bass track ("mode:bounce" / "mode:pulse").
    int bassMode = 0;
    // The roll's mode (phrase::Mode: 0 dorian, 1 aeolian, 2 phrygian) for every
    // pitched lane, as shipped (a redraw can change it with the bass seed).
    int mode = 1;
    // Redraws behind the last generate(): orig = the originality check (#76
    // guard: built-in banned figures and the reference index) refused the
    // roll, guard = the thin-cell guard (bars 5-8) refused it. One log entry
    // per redraw (up to kMaxRedrawLog): the lane that hit (-1 merged, -2 none),
    // the opaque reference stream id (or "banned:..." / "guard:..."), metric
    // and score as orig::check reported them.
    int origRedraws = 0;
    int guardRedraws = 0;
    struct RedrawEntry
    {
        std::int8_t lane = -2;
        std::int8_t metric = 0;
        float score = 0.f;
        char ref[32] = "";
    };
    static constexpr int kMaxRedrawLog = 16;
    RedrawEntry redrawLog[kMaxRedrawLog] {};
    int redrawLogCount = 0;
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
    // A drum fill on the last beat of the phrase (snare / tom run into the downbeat).
    bool fill = false;
    // HITS land on the stab's onsets (its answer slots) instead of avoiding them.
    bool hitsInStab = false;
    // 0 hugs the soft bands, 0.35 is the normal lane, 0.6 is wild.
    // BUSY and BOUNCE extremes raise the effective value. Soft rules never reject.
    float freedom = 0.35f;
    // Optional global chaos. 0 leaves the hidden per-roll bump as the only extra.
    float chaos = 0.f;
    // ScaleMode (Generator/Scale.h). 0 is Auto: the style's own minor, as before.
    // A locked scale quantizes every regenerated melodic lane after the draw.
    int scale = 0;
    // Pack-learned harmony (PackHarmony.h): 0 off, 1 the stab plays the roll's chord progression,
    // 2 the bass and pluck move with it too, 3 DEEP's chord writer for every style (rules::jazzRevoice).
    int harmony = 0;
    // BASSMATCH (Oct 20 bass redesign): 0 as before, 1 sound fixes (engine), 2 also the pack-learned bass notes.
    int bassMatch = 0;
    // DROP (Melak, Oct 20: "make sure youre generating the right part of a song ... the peak drop"):
    // the roll is the 8-bar peak of a drop. Drums: kick on every beat (bar 8 may only dip the kick),
    // a hat on every offbeat 8th plus a few 16ths, the ride for all 8 bars when it plays.
    bool drop = false;
    // RIFF (Oct 20: "its not good bass riffs"): the bass is a designed riff (RiffWriter.h), not
    // the note-by-note rule composer; its finishing passes leave the riff alone.
    bool riff = false;
};

enum BassMode : int
{
    BassModeNone = 0,
    BassModeBounce,
    BassModePulse
};

// Text written into the exported bass track for BOUNCY, nullptr otherwise.
inline const char* bassModeText (const Groove& g) noexcept
{
    if (g.dialect != Pop)
        return nullptr;
    if (g.bassMode == BassModeBounce)
        return "mode:bounce";
    if (g.bassMode == BassModePulse)
        return "mode:pulse";
    return nullptr;
}

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
    static const char* ids[] = { "ukmin", "long", "pop", "jackin" };
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
          "Groove\tswing 53.7%\n"
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
        { kStyleLabels[Jackin],
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
