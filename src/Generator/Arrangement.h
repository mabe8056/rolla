#pragma once

// Arrangement helpers taken from the BOUNCY lead reference ("In The Zone",
// Ellia Jaya, extended mix). Only tendencies are encoded, never the riff:
//
//  - Bar-8 turnaround. On the last bar of an 8-bar phrase the kick and sub
//    step out and the bass holds long notes (an approach degree, then the
//    root) instead of its riff. A lighter variant only dips the kick.
//  - 7+1 harmony. One open chord holds for seven bars and a different open
//    chord takes bar 8. Voicings are sus2 / sus4 / maj7 shapes that rarely
//    state the 3rd, so the loop never commits to happy or sad.
//
// Every choice is weighted by the seed. The generator calls these as soft
// options; none of it is a hard rule. No JUCE, does not touch Generator.cpp.
// Pitches are semitones from the key root (Lane convention), steps are 16ths.

#include "Pattern.h"
#include <cstdint>

namespace rolla::arrange
{

enum Mode : int
{
    Ionian = 0,
    Lydian,
    Mixolydian,
    Dorian,
    Aeolian,
    Phrygian,
    NumModes
};

int degreeToSemis (int degree, int mode) noexcept;
bool inMode (int semis, int mode) noexcept;

// ---------------------------------------------------------------- turnaround

enum class GapType : int
{
    None = 0,
    Full,    // kick out for the bar, bass holds above the sub
    KickDip, // kick out on beats 3-4 only, bass riff stays
};

struct TurnaroundOptions
{
    float chance = 0.75f;   // per phrase end; the rest play straight through
    float fullShare = 0.7f; // Full vs KickDip when a turnaround happens
    float liftOctave = 0.5f; // hold the bass an octave up so the sub really empties
    int phraseBars = 8;
};

struct TurnaroundPlan
{
    int bars[4] {}; // 0-based bars that get a turnaround
    GapType type[4] {};
    int count = 0;
    int heldDegree[2] { 1, 0 }; // approach degree, then home
    int splitStep = 8;          // where the second held note starts inside the bar
    bool lift = false;
};

// Phrase ends inside a loop of loopBars (8 -> bar 8; 16-bar arrangement -> 8 and 16;
// loops shorter than a phrase get one on their last bar at a reduced chance).
TurnaroundPlan planTurnaround (std::uint32_t seed, int loopBars, const TurnaroundOptions& opts = {});

// Rewrites the planned bars: Full replaces the bass notes in the bar with the
// two held notes and clears the kick for the bar; KickDip clears the kick on
// beats 3-4. Other bars are untouched. `mode` keeps the held notes in key.
void applyTurnaround (const TurnaroundPlan& plan, Lane& bass, DrumLane& drums, int loopBars, int mode) noexcept;

// ------------------------------------------------------------------ harmony

struct Chord
{
    int root = 0;            // semitones from key root of the chord root
    std::int8_t notes[4] {}; // semitones from key root
    int n = 0;
    bool statesThird = false;
    int voicing = 0; // index into the voicing table, for tests
};

struct HarmonyOptions
{
    float thirdChance = 0.12f; // how often a voicing may state the 3rd
    float midVariation = 0.2f; // chance bar 4 borrows a neighbour shape (still 7+1 overall)
    int register_ = 12;        // lowest chord note around this many semitones above the key root
};

struct HarmonyLoop
{
    int mode = Lydian;
    Chord bar[8] {};
    int bars = 8;
};

// Bars 1-7 hold the home chord (rooted on the home degree, often the 5th
// over a root bass for the "maj7 over the root" colour), bar 8 turns to a
// different open chord. Every note is in `mode`.
HarmonyLoop makeSevenPlusOne (std::uint32_t seed, int mode, const HarmonyOptions& opts = {});

enum class ChordRhythm : int
{
    Hold,    // one chord per bar, held
    Offbeat, // short chords on the off-8ths (2, 6, 10, 14)
};

// Writes the loop into a chord lane (stab or pad), tiled to loopBars.
int writeHarmony (const HarmonyLoop& h, Lane& lane, int loopBars, ChordRhythm rhythm) noexcept;

// Number of voicing shapes, for tests.
int numVoicings() noexcept;

} // namespace rolla::arrange
