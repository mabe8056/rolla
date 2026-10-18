#pragma once

// Rule composer: every roll's bass and stab lanes are composed fresh from
// seeded sampling over the abstracted statistics in RuleTables.h. Nothing here
// stores or looks up a note sequence, rhythm cell, motif, skeleton or voicing:
// onsets come from per-16th probabilities, lengths from a length distribution,
// pitches from a degree / interval Markov chain under the roll's mode, stabs
// from a chord-quality palette placed by bass-gap probabilities.
// No JUCE. Same seed + same parameters -> same lane.
//
// Pitches follow the Lane convention: chord[0] is semitones from the key root.
// Starts and lengths are 16ths.

#include "Generator.h"
#include <cstdint>

// Mode helpers. The namespace name is kept from the removed phrase grammar so
// the harmony pass and the tests address modes the same way.
namespace rolla::phrase
{

enum Mode : int
{
    Dorian = 0,
    Aeolian = 1,
    Phrygian = 2
};

// Scale-step index -> semitones from the root in a mode (-1 = 7th below).
int degreeToSemis (int degree, int mode) noexcept;

// True if the semitone offset is in the mode.
bool inMode (int semis, int mode) noexcept;

} // namespace rolla::phrase

namespace rolla::rules
{

// The roll's mode: JACKIN phrygian; the others a DARK-weighted draw from the
// bass seed (RuleTables kModeDorianA / kModePhrygianB).
int drawMode (int dialect, float dark, std::uint32_t bassSeed) noexcept;

struct BassParams
{
    int dialect = UkMin;
    int bars = 8;
    int mode = phrase::Aeolian;
    float dark = 0.55f;
};

// Composes a bass line over p.bars (2, 4 or 8) into `lane` (cleared first; the
// lane seed is left alone). Structure: bar 1 the call, bar 2 the answer
// (JACKIN: drawn into the call's silence), bars 3-4 repeat them (the bar-4
// answer is a later pass), bars 5-7 carry the density arc and back-half
// variation, the last bar rests from 16th 13 on (the turn). The kick share
// never passes 35% and the back-half guard (thinRepeatedBackHalf) never fires
// on what this returns unless every retry hit it. Returns the arc index
// (RuleTables kArcNames).
int composeBass (Lane& lane, const BassParams& p, std::uint32_t seed);

// One Markov degree step for the style under the mode, from prevSemi: the
// semitone of a scale degree (0..11). Deterministic in `hash`.
int sampleDegreeSemis (int dialect, int mode, int prevSemi, std::uint32_t hash) noexcept;

struct StabParams
{
    int dialect = UkMin;
    int bars = 8;
    int mode = phrase::Aeolian;
    int perBar = 1;            // chord hits per bar (the STAB / BUSY knob count)
    bool skipOdd = false;      // only even bars
    float length = 2.f;        // hit length in 16ths
    bool varyLength = false;   // ROLLER: 1.5 / 2 / 2.5 per bar
    int vel = 98;
    bool repeatByParity = false; // JACKIN: bars of one parity make the same choices
};

// Stabs placed by the style's gap / sustain / onset shares against `bass`
// (never on a bass attack outside BOUNCY), two chord voicings per roll (one
// per bar parity) from the chord-quality palette, folded into the register band.
void composeStab (Lane& lane, const Lane& bass, const StabParams& p, std::uint32_t seed);

// One chord from the style's palette under the mode: up to 4 tones in `out`.
void stabVoicing (int dialect, int mode, std::uint32_t hash, int* out, int& n) noexcept;

// A&R guard: in the back half of the loop (bars nb/2 .. nb-1), two or more bars
// that play a single pitch class on 1..3 attacks. Such a line is composed again.
bool thinRepeatedBackHalf (const Lane& lane, int bars) noexcept;

// Share of bass attacks on kick steps (16ths 1, 5, 9, 13).
float kickShare (const Lane& lane) noexcept;

} // namespace rolla::rules
