#pragma once

// Locked scale for the KEY / SCALE picker (issue #13). No JUCE: the generator,
// the engine, the processor and the tests share it.
//
// Auto is today's behaviour: each style keeps its own tolerant minor (see
// scalePc in Generator.cpp) and nothing here runs. A locked scale maps every
// melodic tone by scale degree, read in natural minor, so a phrase keeps its
// contour and a triad stays a triad: b3 becomes 3 in Major, b6 becomes 6 in
// Dorian, 2 becomes b2 in Phrygian. Tones outside natural minor (the b2, the
// dorian 6, chromatic neighbours) take the nearest degree. Every tone moves at
// most one semitone, so register rules (bass median, nothing under C2) hold.

#include "Pattern.h"

#include <cstdint>

namespace rolla
{

enum ScaleMode : int
{
    ScaleAuto = 0,
    ScaleMinor,
    ScaleDorian,
    ScalePhrygian,
    ScaleMajor,
    ScaleMixolydian,
    NumScales
};

inline const char* scaleName (int scale) noexcept
{
    static const char* names[] = { "Auto", "Minor", "Dorian", "Phrygian", "Major", "Mixolydian" };
    return names[scale < 0 || scale >= NumScales ? 0 : scale];
}

// Header label: short enough for the compact dropdown.
inline const char* scaleShort (int scale) noexcept
{
    static const char* names[] = { "AUTO", "MIN", "DOR", "PHR", "MAJ", "MIX" };
    return names[scale < 0 || scale >= NumScales ? 0 : scale];
}

// Semitones of the seven degrees. Auto reads as natural minor.
inline const int* scaleSteps (int scale) noexcept
{
    static const int steps[NumScales][7] = {
        { 0, 2, 3, 5, 7, 8, 10 },  // auto (natural minor frame)
        { 0, 2, 3, 5, 7, 8, 10 },  // minor
        { 0, 2, 3, 5, 7, 9, 10 },  // dorian
        { 0, 1, 3, 5, 7, 8, 10 },  // phrygian
        { 0, 2, 4, 5, 7, 9, 11 },  // major
        { 0, 2, 4, 5, 7, 9, 10 },  // mixolydian
    };
    return steps[scale < 0 || scale >= NumScales ? 0 : scale];
}

inline int scalePitchClass (int semi) noexcept
{
    const int pc = semi % 12;
    return pc < 0 ? pc + 12 : pc;
}

inline bool inLockedScale (int scale, int semi) noexcept
{
    const int pc = scalePitchClass (semi);
    const int* steps = scaleSteps (scale);
    for (int i = 0; i < 7; ++i)
        if (steps[i] == pc)
            return true;
    return false;
}

// Degree each source pitch class stands for, read in natural minor.
// 1 (b2) is degree 2, 4 (a raised b3) is degree 3, 6 (b5) leans to the fifth,
// 9 (the dorian 6) is degree 6, 11 (leading tone) is degree 7.
inline int minorDegreeOf (int pc) noexcept
{
    static const int degree[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
    return degree[scalePitchClass (pc)];
}

// Semitones from the key root, mapped into the locked scale. Auto returns semi.
inline int quantizeToScale (int scale, int semi) noexcept
{
    if (scale <= ScaleAuto || scale >= NumScales)
        return semi;
    const int pc = scalePitchClass (semi);
    const int octave = (semi - pc) / 12;
    return octave * 12 + scaleSteps (scale)[minorDegreeOf (pc)];
}

// Sounding bias of a lane's chord values over the key root. Vox is written from
// MIDI 64 (key + 4), so chord 0 there is a major third; every other lane is 0.
inline int scaleBiasFor (int part) noexcept
{
    return part == Vox ? 4 : 0;
}

// One lane into the locked scale. A chord that folds two tones onto one pitch
// drops the duplicate, so a voicing never stacks a unison.
inline void quantizeLaneToScale (Lane& lane, int part, int scale) noexcept
{
    if (scale <= ScaleAuto || scale >= NumScales)
        return;
    const int bias = scaleBiasFor (part);
    for (int i = 0; i < lane.count && i < kMaxNotes; ++i)
    {
        Note& n = lane.notes[i];
        int count = n.nChord < 1 ? 1 : (n.nChord > 4 ? 4 : (int) n.nChord);
        int kept = 0;
        for (int c = 0; c < count; ++c)
        {
            int q = quantizeToScale (scale, (int) n.chord[c] + bias) - bias;
            while (q > 127)
                q -= 12;
            while (q < -128)
                q += 12;
            bool dup = false;
            for (int k = 0; k < kept; ++k)
                if ((int) n.chord[k] == q)
                    dup = true;
            if (dup)
                continue;
            n.chord[kept++] = (std::int8_t) q;
        }
        for (int c = kept; c < 4; ++c)
            n.chord[c] = 0;
        n.nChord = (std::uint8_t) (kept < 1 ? 1 : kept);
    }
}

// Lanes the scale applies to: everything pitched. Drums are never touched.
inline bool scaleAppliesTo (int part) noexcept
{
    return part >= 0 && part < Drums;
}

// partMask bit N quantizes that lane. Kept lanes stay exactly as they were.
inline void applyScale (Groove& g, int scale, std::uint32_t partMask) noexcept
{
    if (scale <= ScaleAuto || scale >= NumScales)
        return;
    for (int part = 0; part < Drums; ++part)
        if (scaleAppliesTo (part) && (partMask & (1u << part)) != 0)
            quantizeLaneToScale (g.lane[part], part, scale);
}

} // namespace rolla
