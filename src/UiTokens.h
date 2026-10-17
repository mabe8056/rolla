#pragma once

#include "Generator/Pattern.h"

#include <juce_graphics/juce_graphics.h>

namespace rolla
{

// concept_FINAL tokens. Accent is the face only.
inline constexpr juce::uint32 kPaper = 0xff111214;
inline constexpr juce::uint32 kCard = 0xff1a1b1e;
inline constexpr juce::uint32 kHov = 0xff222427;
inline constexpr juce::uint32 kSel = 0xff26282c;
inline constexpr juce::uint32 kLine = 0xff2a2c30;
inline constexpr juce::uint32 kLine2 = 0xff3a3d42;
inline constexpr juce::uint32 kInk = 0xffeceae4;
inline constexpr juce::uint32 kInk2 = 0xffadaaa3;
inline constexpr juce::uint32 kInk3 = 0xff8e8b84;
inline constexpr juce::uint32 kTrack = 0xff33363b;
inline constexpr juce::uint32 kCap = 0xff2c2f34;
inline constexpr juce::uint32 kCapHov = 0xff36393f;
inline constexpr juce::uint32 kMark = 0xffe9b32a;
inline constexpr juce::uint32 kMute = 0xffadaaa3;
inline constexpr juce::uint32 kAcc = 0xffff6a3d;
inline constexpr juce::uint32 kAcc2 = 0xffff7d55;
inline constexpr juce::uint32 kFaceInk = 0xff111214;
inline constexpr juce::uint32 kPress = 0xff1e2023;

inline juce::Colour col (juce::uint32 argb) noexcept
{
    return juce::Colour (argb);
}

inline juce::Colour laneColour (int part) noexcept
{
    static const juce::uint32 colours[] = {
        0xff3cc49b, // bass
        0xffb9d66b, // stab
        0xffe9b32a, // hits
        0xff6ea2e3, // pluck
        0xff93a9b9, // pad
        0xffe46ba4, // vox
        0xffeceae4, // drums
    };
    const int n = (int) (sizeof (colours) / sizeof (colours[0]));
    return col (colours[(part % n + n) % n]);
}

inline const char* laneShortName (int part) noexcept
{
    static const char* names[] = { "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX", "DRUMS" };
    return names[(part % NumParts + NumParts) % NumParts];
}

// The panel is these seven. Toms stay in the engine and the state.
inline constexpr int kPanelVoices[] = { Kick, Snare, Clap, Rim, HatC, HatO, Ride };

inline const char* panelVoiceLabel (int voice) noexcept
{
    switch (voice)
    {
        case Kick: return "KICK";
        case Snare: return "SNARE";
        case Clap: return "CLAP";
        case Rim: return "RIM";
        case HatC: return "CH";
        case HatO: return "OH";
        case Ride: return "RIDE";
        default: return "";
    }
}

inline const char* panelVoiceCode (int voice) noexcept
{
    switch (voice)
    {
        case Kick: return "BD";
        case Snare: return "SD";
        case Clap: return "CP";
        case Rim: return "RS";
        case HatC: return "CH";
        case HatO: return "OH";
        case Ride: return "RD";
        default: return "";
    }
}

inline const char* channelLine (int part) noexcept
{
    switch (part)
    {
        case Bass: return "ch 1 · sound";
        case Stab: return "ch 2 · sound";
        case Hits: return "ch 3 · sound";
        case Pluck: return "ch 4 · sound";
        case Pad: return "ch 5 · sound";
        case Vox: return "ch 6 · sound";
        default: return "909 kit · 7 voices";
    }
}

} // namespace rolla
