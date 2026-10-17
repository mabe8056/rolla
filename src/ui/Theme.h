#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rolla
{

// Current concept_FINAL values, in one place. A restyle changes this header;
// controls read colours, radii, spacing, sizes, and fonts from here.
struct Theme
{
    static constexpr juce::uint32 paper = 0xff111214;
    static constexpr juce::uint32 card = 0xff1a1b1e;
    static constexpr juce::uint32 hov = 0xff222427;
    static constexpr juce::uint32 sel = 0xff26282c;
    static constexpr juce::uint32 line = 0xff2a2c30;
    static constexpr juce::uint32 line2 = 0xff3a3d42;
    static constexpr juce::uint32 ink = 0xffeceae4;
    static constexpr juce::uint32 ink2 = 0xffadaaa3;
    static constexpr juce::uint32 ink3 = 0xff8e8b84;
    static constexpr juce::uint32 track = 0xff33363b;
    static constexpr juce::uint32 cap = 0xff2c2f34;
    static constexpr juce::uint32 capHov = 0xff36393f;
    static constexpr juce::uint32 mark = 0xffe9b32a;
    static constexpr juce::uint32 mute = 0xffadaaa3;
    static constexpr juce::uint32 acc = 0xffff6a3d;
    static constexpr juce::uint32 acc2 = 0xffff7d55;
    static constexpr juce::uint32 faceInk = 0xff111214;
    static constexpr juce::uint32 press = 0xff1e2023;

    static constexpr juce::uint32 lane[7] = {
        0xff3cc49b, 0xffb9d66b, 0xffe9b32a, 0xff6ea2e3, 0xff93a9b9, 0xffe46ba4, 0xffeceae4,
    };

    static constexpr float radiusPanel = 8.f;
    static constexpr float radiusSlot = 6.f;
    static constexpr float radiusKey = 4.f;
    static constexpr float radiusFocus = 4.f;

    static constexpr int space4 = 4;
    static constexpr int space8 = 8;
    static constexpr int space16 = 16;
    static constexpr int space24 = 24;
    static constexpr int space32 = 32;

    static constexpr int heightSmall = 24;
    static constexpr int heightSwitch = 28;
    static constexpr int heightIcon = 32;
    static constexpr int heightControl = 40;
    static constexpr int heightHeader = 48;
    static constexpr int knobSmall = 32;
    static constexpr int knobLarge = 64;

    // Point sizes (em square). Use Theme::sansSemi / mono, which call withPointHeight.
    static constexpr float typeMicro = 9.f;
    static constexpr float typeCaption = 10.f;
    static constexpr float typeBody = 11.f;
    static constexpr float typeControl = 12.f;
    static constexpr float typePreset = 13.f;
    static constexpr float typeValue = 14.f;
    static constexpr float typeTitle = 16.f;
    static constexpr float typeLogo = 18.f;
    static constexpr float typeHero = 40.f;

    static juce::Colour colour (juce::uint32 argb) noexcept
    {
        return juce::Colour (argb);
    }

    static juce::Font sansSemi (float height);
    static juce::Font sansBold (float height);
    static juce::Font mono (float height);
    static juce::Font monoMed (float height);
    static juce::Font monoSemi (float height);

    // Loads the five faces once for the process. Safe to call from a background thread.
    static void warmTypefaces();
};

} // namespace rolla
