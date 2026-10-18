#pragma once

#include "Generator/Pattern.h"
#include "ui/Theme.h"

#include <juce_graphics/juce_graphics.h>

namespace rolla
{

// Names kept for the editor. The values live in Theme.
inline constexpr juce::uint32 kPaper = Theme::paper;
inline constexpr juce::uint32 kCard = Theme::card;
inline constexpr juce::uint32 kHov = Theme::hov;
inline constexpr juce::uint32 kSel = Theme::sel;
inline constexpr juce::uint32 kLine = Theme::line;
inline constexpr juce::uint32 kLine2 = Theme::line2;
inline constexpr juce::uint32 kInk = Theme::ink;
inline constexpr juce::uint32 kInk2 = Theme::ink2;
inline constexpr juce::uint32 kInk3 = Theme::ink3;
inline constexpr juce::uint32 kTrack = Theme::track;
inline constexpr juce::uint32 kCap = Theme::cap;
inline constexpr juce::uint32 kCapHov = Theme::capHov;
inline constexpr juce::uint32 kMark = Theme::mark;
inline constexpr juce::uint32 kWarn = Theme::warn;
inline constexpr juce::uint32 kMute = Theme::mute;
inline constexpr juce::uint32 kSolo = Theme::solo;
inline constexpr juce::uint32 kAcc = Theme::acc;
inline constexpr juce::uint32 kAcc2 = Theme::acc2;
inline constexpr juce::uint32 kFaceInk = Theme::faceInk;
inline constexpr juce::uint32 kPress = Theme::press;

inline juce::Colour col (juce::uint32 argb) noexcept
{
    return Theme::colour (argb);
}

inline juce::Colour laneColour (int part) noexcept
{
    const int n = (int) (sizeof (Theme::lane) / sizeof (Theme::lane[0]));
    return col (Theme::lane[(part % n + n) % n]);
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

// juce::String(const char*) is ASCII-only, so a UTF-8 middle dot has to go through fromUTF8.
inline juce::String utf8 (const char* text)
{
    return juce::String::fromUTF8 (text);
}

inline juce::String channelLine (int part)
{
    switch (part)
    {
        case Bass: return utf8 ("ch 1 · sound");
        case Stab: return utf8 ("ch 2 · sound");
        case Hits: return utf8 ("ch 3 · sound");
        case Pluck: return utf8 ("ch 4 · sound");
        case Pad: return utf8 ("ch 5 · sound");
        case Vox: return utf8 ("ch 6 · sound");
        default: return utf8 ("909 kit · 7 voices");
    }
}

} // namespace rolla
