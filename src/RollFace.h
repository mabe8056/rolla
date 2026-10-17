#pragma once

#include "UiTokens.h"

#include <cmath>

namespace rolla
{

struct FacePose
{
    float hover = 0.f;
    float press = 0.f;
    float roll = -1.f; // < 0 idle, else 0..1 through the 340 ms wink
    float dark = 0.55f;
    bool snap = false; // reduced motion: wink, lifted brows, wide mouth, no tween
};

inline float bezierEase (float t, float x1, float y1, float x2, float y2) noexcept
{
    t = std::clamp (t, 0.f, 1.f);
    auto sample = [] (float u, float a, float b)
    {
        const float c = 1.f - u;
        return 3.f * c * c * u * a + 3.f * c * u * u * b + u * u * u;
    };
    float u = t;
    for (int i = 0; i < 5; ++i)
    {
        const float x = sample (u, x1, x2);
        const float dx = sample (std::min (1.f, u + 0.002f), x1, x2)
                       - sample (std::max (0.f, u - 0.002f), x1, x2);
        if (std::abs (dx) < 1.0e-4f)
            break;
        u = std::clamp (u - (x - t) * 0.002f / dx, 0.f, 1.f);
    }
    return sample (u, y1, y2);
}

inline float ramp (float t, float t0, float t1, float v0, float v1) noexcept
{
    if (t <= t0)
        return v0;
    if (t >= t1)
        return v1;
    return v0 + (v1 - v0) * ((t - t0) / (t1 - t0));
}

// 96×96 face.svg, drawn in `area`. DARK tilts both brows by ±6°.
inline void paintRollFace (juce::Graphics& g, juce::Rectangle<float> area, const FacePose& pose) noexcept
{
    const float s = area.getWidth() / 96.f;
    if (s <= 0.f)
        return;

    const float squint = std::max (pose.hover, pose.press);
    const bool rolling = pose.roll >= 0.f || pose.snap;
    const float eased = pose.snap ? 0.35f
                                  : (pose.roll >= 0.f ? bezierEase (pose.roll, 0.2f, 0.7f, 0.2f, 1.f) : 0.f);

    float eyeSy = ramp (eased, 0.f, 0.18f, 1.f, 0.15f);
    if (eased >= 0.18f && eased < 0.66f)
        eyeSy = 0.15f;
    else if (eased >= 0.66f)
        eyeSy = ramp (eased, 0.66f, 1.f, 0.15f, 1.f);

    float wink = 0.f;
    if (eased > 0.18f && eased < 0.22f)
        wink = ramp (eased, 0.18f, 0.22f, 0.f, 1.f);
    else if (eased >= 0.22f && eased <= 0.62f)
        wink = 1.f;
    else if (eased > 0.62f && eased < 0.66f)
        wink = ramp (eased, 0.62f, 0.66f, 1.f, 0.f);

    float browDy = 0.f;
    float browRoll = 0.f;
    float mouthK = 0.f;
    if (rolling)
    {
        if (pose.snap)
        {
            wink = 1.f;
            eyeSy = 0.15f;
            browDy = -3.f;
            browRoll = -8.f;
            mouthK = 1.f;
        }
        else
        {
            if (eased < 0.30f)
                browDy = ramp (eased, 0.f, 0.30f, 0.f, -3.f);
            else
                browDy = ramp (eased, 0.30f, 0.70f, -3.f, 0.f);
            if (eased < 0.30f)
                browRoll = ramp (eased, 0.f, 0.30f, 0.f, -8.f);
            else
                browRoll = ramp (eased, 0.30f, 0.70f, -8.f, 0.f);
            if (eased < 0.35f)
                mouthK = ramp (eased, 0.f, 0.35f, 0.f, 1.f);
            else
                mouthK = ramp (eased, 0.35f, 0.75f, 1.f, 0.f);
        }
    }

    const float darkDeg = (pose.dark - 0.5f) * 12.f;

    g.saveState();
    g.addTransform (juce::AffineTransform::scale (1.f + 0.04f * pose.press,
                                                  1.f - 0.04f * pose.press,
                                                  48.f, 96.f)
                        .scaled (s)
                        .translated (area.getX(), area.getY()));

    g.setColour (col (kAcc).interpolatedWith (col (kAcc2), pose.hover));
    g.fillEllipse (0.f, 0.f, 96.f, 96.f);

    const juce::PathStrokeType stroke (6.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (col (kFaceInk));

    auto brow = [&] (float x0, float y0, float x1, float y1, float cx, float cy, float extraDeg)
    {
        g.saveState();
        g.addTransform (juce::AffineTransform::rotation (juce::degreesToRadians (darkDeg + extraDeg), cx, cy)
                            .translated (0.f, browDy));
        juce::Path line;
        line.startNewSubPath (x0, y0);
        line.lineTo (x1, y1);
        g.strokePath (line, stroke);
        g.restoreState();
    };
    brow (24.f, 24.f, 42.f, 32.f, 33.f, 28.f, 0.f);
    brow (72.f, 24.f, 54.f, 32.f, 63.f, 28.f, rolling ? browRoll : 0.f);

    auto eye = [&] (float cx, float cy, float sy, float alpha)
    {
        if (alpha <= 0.01f)
            return;
        g.saveState();
        g.addTransform (juce::AffineTransform::scale (1.f, sy, cx, cy));
        g.setColour (col (kFaceInk).withAlpha (alpha));
        g.fillEllipse (cx - 6.f, cy - 6.f, 12.f, 12.f);
        g.restoreState();
    };

    const float leftA = rolling ? (1.f - wink) : 1.f;
    const float leftSy = rolling ? eyeSy : (1.f - 0.5f * squint);
    const float rightSy = 1.f - 0.5f * squint;
    eye (36.f, 44.f, leftSy, leftA);
    eye (60.f, 44.f, rightSy, 1.f);

    if (rolling && wink > 0.01f)
    {
        juce::Path arc;
        arc.startNewSubPath (30.f, 45.f);
        arc.quadraticTo (36.f, 40.f, 42.f, 45.f);
        g.setColour (col (kFaceInk).withAlpha (wink));
        g.strokePath (arc, stroke);
    }

    g.saveState();
    g.addTransform (juce::AffineTransform::scale (1.f + 0.10f * mouthK, 1.f + 0.15f * mouthK, 48.f, 66.f));
    juce::Path mouth;
    mouth.startNewSubPath (24.f, 60.f);
    mouth.cubicTo (32.f, 72.f, 64.f, 72.f, 72.f, 60.f);
    g.setColour (col (kFaceInk));
    g.strokePath (mouth, stroke);
    g.restoreState();

    g.restoreState();
}

} // namespace rolla
