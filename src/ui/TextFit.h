#pragma once

#include "Theme.h"

#include <juce_graphics/juce_graphics.h>

#include <functional>

namespace rolla
{

// One drawn string, as seen by a test. box and clip are in the painting component's
// local (design-space) coordinates; width is the measured advance of the whole string.
struct TextRecord
{
    juce::String text;
    Theme::Role role;
    float point = 0.f;
    juce::Rectangle<float> box;
    juce::Rectangle<float> clip;
    juce::Justification justification { juce::Justification::centredLeft };
    float width = 0.f;
    bool ellipsis = false;
};

// Empty in the plugin. knob_test installs a sink and paints the editor to audit every label.
inline std::function<void (const TextRecord&)>& textAudit()
{
    static std::function<void (const TextRecord&)> sink;
    return sink;
}

inline float textWidth (const juce::Font& font, const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (font, text);
}

inline float textWidth (Theme::Role role, const juce::String& text)
{
    return textWidth (Theme::font (role), text);
}

// Every editor string goes through here: a Theme role (never a raw size) and a box.
// ellipsis = true only for user or bank content (file names, preset names, help text).
inline void drawLabel (juce::Graphics& g, const juce::String& text, Theme::Role role,
                       juce::Rectangle<float> box, juce::Justification justification,
                       bool ellipsis = false)
{
    if (text.isEmpty())
        return;
    const auto font = Theme::font (role);
    g.setFont (font);
    if (auto& sink = textAudit())
        sink ({ text, role, Theme::pointSize (role), box, g.getClipBounds().toFloat(),
                justification, textWidth (font, text), ellipsis });
    g.drawText (text, box, justification, ellipsis);
}

inline void drawLabel (juce::Graphics& g, const juce::String& text, Theme::Role role,
                       juce::Rectangle<int> box, juce::Justification justification,
                       bool ellipsis = false)
{
    drawLabel (g, text, role, box.toFloat(), justification, ellipsis);
}

} // namespace rolla
