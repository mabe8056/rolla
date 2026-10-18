#pragma once

#include <juce_graphics/juce_graphics.h>

namespace rolla
{

// Font D (Rave) and the step-2 control tokens. Lane rows and the ROLL tile
// keep their current geometry; they read these tokens for type, colour and shape.
struct Theme
{
    enum class Role
    {
        wordmark,
        roll,
        section,
        macro,
        tile,
        preset,
        lane,
        voice,
        pill,
        button,
        caption,
        micro,
        value,
        macroval,
        microval,
        meta,
        keycap
    };

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
    static constexpr juce::uint32 warn = 0xffff4d5e;
    static constexpr juce::uint32 mute = 0xffadaaa3;
    // Lane solo fill. The amber mark, so a solo reads apart from the grey mute.
    static constexpr juce::uint32 solo = mark;
    static constexpr juce::uint32 acc = 0xffff6a3d;
    static constexpr juce::uint32 acc2 = 0xffff7d55;
    static constexpr juce::uint32 faceInk = 0xff111214;
    static constexpr juce::uint32 press = 0xff1e2023;

    static constexpr juce::uint32 lane[7] = {
        0xff3cc49b, 0xffb9d66b, 0xffe9b32a, 0xff6ea2e3, 0xff93a9b9, 0xffe46ba4, 0xffeceae4,
    };

    static constexpr float alphaDisabled = 0.35f;
    static constexpr float alphaMuted = 0.40f;
    static constexpr float alphaMutedText = 0.55f;

    static constexpr float radiusPanel = 8.f;
    static constexpr float radiusSlot = 8.f;
    static constexpr float radiusKey = 4.f;
    static constexpr float radiusNote = 1.f;

    static float pillRadius (float height) noexcept { return height * 0.5f; }
    static float focusRadius (float controlRadius) noexcept { return controlRadius + 3.f; }

    static constexpr int space4 = 4;
    static constexpr int space8 = 8;
    static constexpr int space16 = 16;
    static constexpr int space24 = 24;
    static constexpr int space32 = 32;

    static constexpr int heightSmall = 24;
    static constexpr int heightIcon = 32;
    static constexpr int heightControl = 40;
    static constexpr int heightHeader = 48;
    static constexpr int knobSmall = 32;
    static constexpr int knobLarge = 64;

    static constexpr int segmentLarge = 96;
    static constexpr int segmentSmall = 72;
    static constexpr int buttonWidth = 80;
    static constexpr int iconLarge = 40;
    static constexpr int stepperPreset = 256;
    static constexpr int stepperKey = 136;
    static constexpr int stepperKit = 176;
    static constexpr int glyphBox = 16;
    static constexpr float padPillLarge = 12.f;
    static constexpr float padPillSmall = 10.f;

    static constexpr float strokeHair = 1.f;
    static constexpr float strokeIcon = 1.5f;
    static constexpr float strokeFocus = 2.f;
    static constexpr float strokeArcSmall = 3.f;
    static constexpr float strokeArcLarge = 4.f;
    static constexpr float strokeFace = 6.f;

    static constexpr int msHover = 120;
    static constexpr int msPress = 90;
    static constexpr int msRoll = 340;

    static juce::Colour colour (juce::uint32 argb) noexcept
    {
        return juce::Colour (argb);
    }

    // Face, point size and kerning for one role. Every drawn string goes through this.
    static juce::Font font (Role role);
    static float lineHeight (Role role) noexcept;

    // Loads the seven faces once for the process. Safe to call from a background thread.
    static void warmTypefaces();
};

} // namespace rolla
