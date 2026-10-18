#pragma once

// Standalone DEEP / JACKIN bass phrase grammar, with JACKIN bass/stab
// call-and-answer. It does not touch Generator.cpp. The generator can adopt it
// through makeBassPhrase / drawDistinctBass / makeStabAnswer / writePhrase.
//
// Ingredients live in resources/phrase_bank.json (rhythm cells, degree pools,
// contours, variation ops, call/answer pairs) and are compiled into
// PhraseBankData.h by tools/build_phrase_bank.py. Nothing is a stored phrase:
// every seed joins, mutates and re-pitches cells, so shapes are not looked up.
//
// Hard rules kept here: every note is in the chosen mode, bass attacks on kick
// steps stay at or under kMaxKickShare, no bar is empty, nothing runs past the
// phrase. Everything else is a weighted tendency.
//
// Pitches follow the Lane convention: chord[0] is semitones from the key root
// (0 root, -2 the b7 below, 12 an octave up). Starts and lengths are 16ths.

#include "Pattern.h"
#include <cstdint>

namespace rolla::phrase
{

enum class Style : int
{
    Deep = 0,
    Jackin = 1
};

// Long -> Deep, Gaskin -> Jackin. Returns false for styles the bank does not cover.
bool styleForDialect (int dialect, Style& out) noexcept;

enum Mode : int
{
    Dorian = 0,
    Aeolian = 1,
    Phrygian = 2
};

struct PhraseSettings
{
    float busy = 0.40f;   // more onsets per cell, more stab answers
    float bounce = 0.45f; // more even-bar variation
    float dark = 0.55f;   // mode tendency: low = dorian, high = phrygian
    int bars = 2;         // phrase length, 1 or 2

    static PhraseSettings from (const Settings& s, int phraseBars = 2) noexcept
    {
        PhraseSettings p;
        p.busy = s.busy;
        p.bounce = s.bounce;
        p.dark = s.dark;
        p.bars = phraseBars;
        return p;
    }
};

inline constexpr int kMaxPhraseNotes = 32;

struct Phrase
{
    int bars = 2;
    int mode = Aeolian;
    int count = 0;
    Note notes[kMaxPhraseNotes] {};
    int halfTag[4] {}; // per half bar: 0 rest, 1 sparse, 2 dense
};

// One bass phrase for any seed. Same seed + settings -> same phrase.
Phrase makeBassPhrase (Style style, std::uint32_t seed, const PhraseSettings& settings);

// Stab answers placed in the quiet halves that follow a bass call. Chords are
// scale-step stacks over an answer degree, so they share the bass's mode.
Phrase makeStabAnswer (Style style, const Phrase& bass, std::uint32_t seed, const PhraseSettings& settings);

// Rhythm + pitch outline over 32 steps (a 1-bar phrase is tiled twice).
struct Skeleton
{
    std::uint32_t onsets = 0;
    std::int8_t pitch[32] {};
};

Skeleton skeletonOf (const Phrase& p) noexcept;

// 0..1. Onset Jaccard, scaled by how many shared onsets carry the same pitch:
// sim = J * (0.5 + 0.5 * samePitchShare).
float similarity (const Skeleton& a, const Skeleton& b) noexcept;

inline constexpr float kNearIdentical = 0.85f;

// Recent skeletons, for "reject if too close to an earlier roll, redraw".
struct SkeletonHistory
{
    static constexpr int kCap = 32;
    Skeleton items[kCap] {};
    int count = 0;
    int head = 0;

    void push (const Skeleton& s) noexcept;
    float closest (const Skeleton& s) const noexcept;
    void clear() noexcept { count = head = 0; }
};

// makeBassPhrase, redrawn (seed stepped by a fixed odd constant) while the
// skeleton is at or above `threshold` against the history. Pushes the winner.
Phrase drawDistinctBass (Style style, std::uint32_t seed, const PhraseSettings& settings,
                         SkeletonHistory& history, float threshold = kNearIdentical, int maxTries = 8);

// Tiles the phrase over loopBars into lane (cleared first). Returns notes written.
int writePhrase (const Phrase& p, Lane& lane, int loopBars) noexcept;

// Hard-rule check. `why` (may be null, 96 chars) names the first failure.
bool passesHardRules (const Phrase& bass, char* why = nullptr) noexcept;

// Call/answer measures for tests and the scorer.
struct AnswerStats
{
    float inGapShare = 0.f;   // stab onsets with no bass sounding
    float callsAnswered = 0.f; // bass calls (dense/sparse halves) with a stab in the next half
    int stabHits = 0;
};

AnswerStats answerStats (const Phrase& bass, const Phrase& stab) noexcept;

// Scale-step index -> semitones from the root in a mode (-1 = 7th below).
int degreeToSemis (int degree, int mode) noexcept;

// True if the semitone offset is in the mode.
bool inMode (int semis, int mode) noexcept;

} // namespace rolla::phrase
