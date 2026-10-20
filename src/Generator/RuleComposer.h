#pragma once

// Rule composer: every roll's bass and stab lanes are composed fresh from
// seeded sampling over the genre model (resources/genre/genre_writing_model.json
// v1.2, compiled into RuleTables.h by tools/gen_rule_tables.py). Nothing here
// stores, caches or looks up a note sequence, rhythm cell, motif, skeleton or
// voicing, and nothing is kept between rolls:
//   - one mode per roll, drawn from the style's mode weights (all inside the
//     scorer's scale);
//   - notes per bar from the density curve, onset slots from the per-16th
//     onset weights (kick steps weighted to the style's on-kick median);
//   - pitches from the first-order degree table times the degree-by-beat-
//     strength table, masked to the mode; octave placement from the interval
//     distribution;
//   - lengths as a fraction of the gap to the next onset, with median-length
//     and long-note guards;
//   - every table meets the model's entropy floors (transition cells capped at
//     0.60) at run time, before the mode mask;
//   - stabs placed by P(stab onset | bass state) with the gap weight, at most
//     3 hits a bar, voicings from the chord-quality palette that are in the
//     mode and make no m2 / m9 with the bass under them (so a stab on a bass
//     attack agrees with that note).
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

// The roll's mode from the style's model weights, tilted by DARK (dorian
// brighter, phrygian darker). JACKIN draws aeolian or phrygian, never dorian.
int drawMode (int dialect, float dark, std::uint32_t bassSeed) noexcept;

// Bass submode of the roll (sets the density and length targets).
enum Submode : int
{
    SubNone = 0,
    SubDeepDark,
    SubDeepSparse,
    SubBounce,
    SubPulse
};

struct BassParams
{
    int dialect = UkMin;
    int bars = 8;
    int mode = phrase::Aeolian;
    float dark = 0.55f;
    int sub = SubNone;
};

// Composes a bass line over p.bars (2, 4 or 8) into `lane` (cleared first; the
// lane seed is left alone). Bar 1 is the call, bar 2 its answer (JACKIN: drawn
// into the call's silence), bars 3-4 restate them, bar 4 answers in its back
// half, bars 5-7 restate with back-half mutations toward the density curve,
// and the last bar turns (rest from 16th 13 on, plus a bar-8 palette move).
// Guards, each a recompose: kick share <= 35%, the output onset entropy, the
// tonic guard, the thin-cell guard, one note per onset. Returns the density
// shape index (RuleTables kArcNames).
int composeBass (Lane& lane, const BassParams& p, std::uint32_t seed);

// One first-order degree step for the style under the mode, from prevSemi:
// returns a pitch class (0..11) in the mode. Deterministic in `hash`.
int sampleDegreeSemis (int dialect, int mode, int prevSemi, std::uint32_t hash) noexcept;

// One onset slot (0..15) for the style's bass, drawn from the model's per-16th
// onset weights for bar group `group` (0 body, 1 bar 4, 2 bar 8) after the
// per-roll kick weighting and entropy floor. Only slots with allowed[s] can be
// drawn; -1 if none is allowed. The rewrite passes use this instead of fixed
// slot lists. Deterministic in `hash`.
int sampleBassSlot (int dialect, int group, const bool* allowed, std::uint32_t hash) noexcept;

struct StabParams
{
    int dialect = UkMin;
    int bars = 8;
    int mode = phrase::Aeolian;
    int perBar = 1;            // chord hits per bar (the STAB / BUSY knob count), capped at 3
    std::uint8_t lighterMask = 0; // bars (bit b) that take one hit fewer than perBar
    bool skipOdd = false;      // only even bars
    float length = 2.f;        // hit length in 16ths
    bool varyLength = false;   // ROLLER: 1.5 / 2 / 2.5 per bar
    int vel = 98;
    bool repeatByParity = false; // JACKIN: bars of one parity make the same choices
};

// Stabs placed against `bass`: per bar the style's gap / sustain / onset
// shares (the model's, so a stab can land on a bass attack; beats up to the style's
// on-beat ceiling), slots
// weighted by the model's P(stab onset | bass state) with the gap weight and
// the answer after a bass phrase end. Two voicings per roll (one per bar
// parity), in the mode, with no m2 / m9 against the bass under their hits.
// DEEP voices its two chords as jazz-house colours instead (minor 7, minor 9 or
// major 7 stacked in thirds in the mode, the top voice a step apart, the root
// left to the bass when the bass plays it); the palette pair is its fallback.
void composeStab (Lane& lane, const Lane& bass, const StabParams& p, std::uint32_t seed);

// One chord from the style's palette under the mode: up to 4 tones in `out`.
void stabVoicing (int dialect, int mode, std::uint32_t hash, int* out, int& n) noexcept;

// Thin-cell guard (A&R): in the back half of the loop (bars nb/2 .. nb-1), two
// or more bars that play a single pitch class on 1..4 attacks. Such a line is
// composed again.
bool thinRepeatedBackHalf (const Lane& lane, int bars) noexcept;

// Share of bass attacks on kick steps (16ths 1, 5, 9, 13).
float kickShare (const Lane& lane) noexcept;

// Attacks that share an onset with an earlier note of the lane (DUP_NOTES).
int duplicateOnsets (const Lane& lane) noexcept;

// Tonic guard: the root is the most-sounding pitch class and the root plus the
// next pitch class hold at least the style's top-2 share of bass time.
bool tonicGuardOk (const Lane& lane, int dialect) noexcept;

// Median note length in 16ths (0 for an empty lane).
float medianLength (const Lane& lane) noexcept;

} // namespace rolla::rules
