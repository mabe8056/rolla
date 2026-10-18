#include "PluginEditor.h"
#include "Engine/RollaSettings.h"

#include "DspUtil.h"
#include "Generator/Intensity.h"
#include "Generator/Scale.h"
#include "LaneSampleUi.h"
#include "RollFace.h"
#include "UiTokens.h"
#include "ui/TextFit.h"

#include <cmath>

namespace rolla
{
using namespace juce;

namespace
{

void bindHelp (Component& component, const String& text)
{
    component.getProperties().set ("rollaHelp", text);
    if (auto* tip = dynamic_cast<SettableTooltipClient*> (&component))
        tip->setTooltip (text);
}

String helpUnder (const Component* component)
{
    while (component != nullptr)
    {
        const auto text = component->getProperties()["rollaHelp"].toString();
        if (text.isNotEmpty())
            return text;
        component = component->getParentComponent();
    }
    return {};
}

float paramRaw (const AudioProcessorValueTreeState& tree, const String& id)
{
    if (auto* v = tree.getRawParameterValue (id))
        return v->load();
    return 0.f;
}

int choiceIndex (AudioProcessorValueTreeState& tree, const String& id)
{
    if (auto* p = dynamic_cast<AudioParameterChoice*> (tree.getParameter (id)))
        return p->getIndex();
    return 0;
}

void setChoiceIndex (RollaProcessor& proc, const String& id, int index)
{
    auto* p = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (id));
    if (p == nullptr)
        return;
    if (index < 0 || index >= p->choices.size() || index == p->getIndex())
        return;
    proc.undo.beginNewTransaction();
    p->beginChangeGesture();
    p->setValueNotifyingHost (p->convertTo0to1 ((float) index));
    p->endChangeGesture();
}

void setBool (RollaProcessor& proc, const String& id, bool on)
{
    if (auto* p = proc.apvts.getParameter (id))
    {
        proc.undo.beginNewTransaction();
        p->beginChangeGesture();
        p->setValueNotifyingHost (on ? 1.f : 0.f);
        p->endChangeGesture();
    }
}

bool audioFile (const String& path)
{
    return path.endsWithIgnoreCase (".wav")
        || path.endsWithIgnoreCase (".aif")
        || path.endsWithIgnoreCase (".aiff")
        || path.endsWithIgnoreCase (".flac");
}

StringArray presetsForLane (const PresetBank& bank, int part)
{
    StringArray names;
    const int n = bank.count[part] > 0 ? bank.count[part] : PresetBank::kPer;
    for (int i = 0; i < n; ++i)
    {
        const char* raw = nullptr;
        switch (part)
        {
            case Bass: raw = bank.bass[i].name; break;
            case Stab: raw = bank.stab[i].name; break;
            case Hits: raw = bank.hits[i].name; break;
            case Pluck: raw = bank.pluck[i].name; break;
            case Pad: raw = bank.pad[i].name; break;
            case Vox: raw = bank.vox[i].name; break;
            case Drums: raw = bank.drums[i].name; break;
            default: break;
        }
        names.add (raw != nullptr && raw[0] != 0 ? String (raw) : ("Preset " + String (i + 1)));
    }
    return names;
}

String signed1 (float v)
{
    String s (v, 1);
    if (v > 0.05f)
        s = "+" + s;
    return s;
}

String darkCaption (float dark)
{
    static const char* steps[] = { "5", "2", "b7", "b3", "b2" };
    const int i = jlimit (0, 4, roundToInt (dark * 4.f));
    return "2nd bass note " + String (utf8 ("·")) + " " + steps[i];
}

String busyCaption (float busy)
{
    const int notes = jlimit (3, 7, roundToInt (3.f + 4.f * busy));
    return "notes per bar " + String (utf8 ("·")) + " " + String (notes);
}

String bounceCaption (float bounce)
{
    static const char* slot[] = { "2&", "2a", "3", "3e", "3&", "3a", "4" };
    const int i = jlimit (0, 6, roundToInt (13.f - 6.f * bounce) - 7);
    return "groove flips on " + String (slot[i]);
}

String stabCaption (float stab)
{
    if (stab <= 0.02f)
        return "bass only";
    if (stab > 0.66f)
        return "answers 4/4 bars";
    if (stab > 0.33f)
        return "answers 2/4 bars";
    return "answers 1/4 bars";
}

void focusRing (Graphics& g, Component& c, float controlRadius = -1.f)
{
    if (! c.hasKeyboardFocus (false))
        return;
    const float radius = controlRadius >= 0.f ? controlRadius
                         : 0.5f * (float) jmin (c.getWidth(), c.getHeight());
    g.setColour (col (kInk));
    g.drawRoundedRectangle (c.getLocalBounds().toFloat().expanded (3.f),
                            Theme::focusRadius (radius), Theme::strokeFocus);
}

void paintMissingIcon (Graphics& g, float x, float y, float s)
{
    Path tri;
    tri.addTriangle (x, y + s, x + s * 0.5f, y, x + s, y + s);
    g.setColour (col (kWarn));
    g.fillPath (tri);
    g.setColour (col (kPaper));
    const float cx = x + s * 0.5f;
    g.fillRect (cx - s * 0.055f, y + s * 0.36f, s * 0.11f, s * 0.30f);
    g.fillEllipse (cx - s * 0.065f, y + s * 0.72f, s * 0.13f, s * 0.13f);
}

struct Look : LookAndFeel_V4
{
    Look()
    {
        setColour (PopupMenu::backgroundColourId, col (kCard));
        setColour (PopupMenu::textColourId, col (kInk));
        setColour (PopupMenu::highlightedBackgroundColourId, col (kHov));
        setColour (PopupMenu::highlightedTextColourId, col (kInk));
        setColour (ResizableWindow::backgroundColourId, col (kPaper));
    }

    Font getPopupMenuFont() override { return Theme::font (Theme::Role::preset); }
    Font getLabelFont (Label&) override { return Theme::font (Theme::Role::meta); }
    Font getTextButtonFont (TextButton&, int) override { return Theme::font (Theme::Role::button); }

    // drawPopupMenuItem insets the text 28 px each side; size items so nothing is cut. 32 px rows per spec.
    void getIdealPopupMenuItemSize (const String& text, bool isSeparator, int /*standardMenuItemHeight*/,
                                    int& idealWidth, int& idealHeight) override
    {
        if (isSeparator)
        {
            idealWidth = 50;
            idealHeight = Theme::space8;
            return;
        }
        idealWidth = (int) std::ceil (textWidth (Theme::Role::preset, text)) + 56;
        idealHeight = Theme::heightIcon;
    }

    void drawPopupMenuBackground (Graphics& g, int width, int height) override
    {
        g.setColour (col (kCard));
        g.fillRoundedRectangle (0.f, 0.f, (float) width, (float) height, Theme::radiusPanel);
        g.setColour (col (kLine2));
        g.drawRoundedRectangle (0.5f, 0.5f, (float) width - 1.f, (float) height - 1.f, Theme::radiusPanel, 1.f);
    }

    void drawPopupMenuItem (Graphics& g, const Rectangle<int>& area, bool isSeparator, bool isActive,
                            bool isHighlighted, bool isTicked, bool /*hasSubMenu*/, const String& text,
                            const String& /*shortcut*/, const Drawable* /*icon*/, const Colour*) override
    {
        if (isSeparator)
        {
            g.setColour (col (kLine));
            g.fillRect (area.reduced (8, 0).withHeight (1).withY (area.getCentreY()));
            return;
        }
        if (isHighlighted && isActive)
        {
            g.setColour (col (kHov));
            g.fillRect (area);
        }
        g.setColour (isActive ? col (kInk) : col (kInk3));
        ::rolla::drawLabel (g, text, Theme::Role::preset, area.reduced (28, 0), Justification::centredLeft, true);
        if (isTicked)
        {
            g.setColour (col (kInk));
            Path tick;
            tick.startNewSubPath (10.f, (float) area.getCentreY());
            tick.lineTo (13.f, (float) area.getCentreY() + 3.f);
            tick.lineTo (18.f, (float) area.getCentreY() - 4.f);
            g.strokePath (tick, PathStrokeType (Theme::strokeIcon, PathStrokeType::curved, PathStrokeType::rounded));
        }
    }

    Rectangle<int> getTooltipBounds (const String& tip, Point<int> pos, Rectangle<int> parent) override
    {
        const bool rich = tip.containsChar ('\t');
        const bool intensity = tip.startsWith ("INTENSITY");
        const int maxW = intensity ? 368 : (rich ? 352 : 240);
        const int padX = 12;
        const int padY = 8;
        const auto font = Theme::font (Theme::Role::meta);
        int lines = 1;
        int textW = 0;
        if (rich)
        {
            lines = 0;
            for (auto line : StringArray::fromLines (tip))
            {
                if (line.isEmpty())
                    continue;
                const auto key = line.upToFirstOccurrenceOf ("\t", false, false);
                const auto value = line.fromFirstOccurrenceOf ("\t", false, false);
                GlyphArrangement row;
                row.addLineOfText (font, value, 0.f, 0.f);
                const int keyW = (intensity && lines == 0) ? 96 : 64;
                textW = jmax (textW, key.isEmpty()
                                         ? (int) std::ceil (row.getBoundingBox (0, -1, true).getWidth())
                                         : keyW + 8 + (int) std::ceil (row.getBoundingBox (0, -1, true).getWidth()));
                ++lines;
            }
            lines = jmax (1, lines);
        }
        else
        {
            AttributedString sentence;
            sentence.append (tip, font, col (kInk));
            TextLayout layout;
            layout.createLayout (sentence, (float) (maxW - padX * 2));
            textW = (int) std::ceil (layout.getWidth());
            lines = jlimit (1, 2, ((int) std::ceil (layout.getHeight()) + 15) / 16);
        }
        auto b = Rectangle<int> (pos.x, pos.y + 8, jlimit (48, maxW, textW + padX * 2), lines * 16 + padY * 2);
        if (b.getRight() > parent.getRight())
            b.setX (parent.getRight() - b.getWidth());
        if (b.getX() < parent.getX())
            b.setX (parent.getX());
        if (b.getBottom() > parent.getBottom())
            b.setY (pos.y - 8 - b.getHeight());
        if (b.getY() < parent.getY())
            b.setY (parent.getY());
        return b;
    }

    void drawTooltip (Graphics& g, const String& text, int width, int height) override
    {
        auto b = Rectangle<float> (0.f, 0.f, (float) width, (float) height);
        g.setColour (col (kHov));
        g.fillRoundedRectangle (b, Theme::radiusPanel);
        g.setColour (col (kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), Theme::radiusPanel - 0.5f, Theme::strokeHair);

        auto inner = b.reduced (12.f, 8.f);
        if (text.containsChar ('\t'))
        {
            StringArray lines;
            for (auto line : StringArray::fromLines (text))
                if (line.isNotEmpty())
                    lines.add (line);
            const float rowH = inner.getHeight() / (float) jmax (1, lines.size());
            for (int i = 0; i < lines.size(); ++i)
            {
                auto row = inner.withY (inner.getY() + rowH * (float) i).withHeight (rowH);
                const auto key = lines[i].upToFirstOccurrenceOf ("\t", false, false);
                const auto value = lines[i].fromFirstOccurrenceOf ("\t", false, false);
                if (key.isEmpty())
                {
                    g.setColour (col (kInk));
                    ::rolla::drawLabel (g, value, Theme::Role::meta, row, Justification::centredLeft, true);
                    continue;
                }
                const float keyW = (text.startsWith ("INTENSITY") && i == 0) ? 96.f : 64.f;
                g.setColour (col (kInk3));
                ::rolla::drawLabel (g, key, Theme::Role::caption, row.removeFromLeft (keyW), Justification::centredLeft);
                g.setColour (col (kInk));
                ::rolla::drawLabel (g, value, Theme::Role::meta, row.withTrimmedLeft (8.f), Justification::centredLeft, true);
            }
            return;
        }

        AttributedString sentence;
        sentence.setJustification (Justification::centredLeft);
        sentence.append (text, Theme::font (Theme::Role::meta), col (kInk));
        TextLayout layout;
        layout.createLayout (sentence, inner.getWidth());
        layout.draw (g, inner);
    }
};

struct QuietButton : Button
{
    explicit QuietButton (const String& name = {}) : Button (name) {}

    bool keyPressed (const KeyPress& key) override
    {
        if (key == KeyPress::spaceKey)
            return false;
        return Button::keyPressed (key);
    }

    void paintButton (Graphics&, bool, bool) override {}
};

struct KnobSlider : Slider
{
    KnobSlider()
    {
        setSliderStyle (RotaryHorizontalVerticalDrag);
        setTextBoxStyle (NoTextBox, false, 0, 0);
        setRotaryParameters (degreesToRadians (-135.f), degreesToRadians (135.f), true);
        setPopupDisplayEnabled (false, false, nullptr, 0);
        setScrollWheelEnabled (false);
        setWantsKeyboardFocus (true);
        setPaintingIsUnclipped (true);
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key == KeyPress::spaceKey)
            return false;
        return Slider::keyPressed (key);
    }

    void paint (Graphics& g) override
    {
        const float size = (float) getWidth();
        const float stroke = size >= 48.f ? Theme::strokeArcLarge : Theme::strokeArcSmall;
        const float r = size * 0.5f;
        const float arcR = r - stroke * 0.5f;
        const float capR = r * (size >= 48.f ? 0.64f : 0.60f);
        const float ptrW = size >= 48.f ? 2.f : Theme::strokeIcon;
        const auto centre = getLocalBounds().toFloat().getCentre();
        const float start = degreesToRadians (-135.f);
        const float end = degreesToRadians (135.f);
        const float pos = (float) valueToProportionOfLength (getValue());
        const float ang = start + pos * (end - start);
        const bool bipolar = getProperties()["bipolar"];
        const bool hot = isMouseOver() || isMouseButtonDown();

        auto arc = [&] (float a0, float a1)
        {
            if (a1 < a0)
                std::swap (a0, a1);
            if (a1 - a0 < 0.01f)
                return;
            Path path;
            path.addCentredArc (centre.x, centre.y, arcR, arcR, 0.f, a0, a1, true);
            g.strokePath (path, PathStrokeType (stroke, PathStrokeType::curved, PathStrokeType::butt));
        };

        g.setColour (col (kTrack));
        arc (start, end);
        g.setColour (col (kInk));
        arc (bipolar ? (start + end) * 0.5f : start, ang);

        g.setColour (col (hot ? kCapHov : kCap));
        g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.f, capR * 2.f);

        const float inner = capR * 0.20f;
        const float outer = capR * 0.85f;
        g.setColour (col (kInk));
        g.drawLine (centre.x + std::sin (ang) * inner,
                    centre.y - std::cos (ang) * inner,
                    centre.x + std::sin (ang) * outer,
                    centre.y - std::cos (ang) * outer,
                    ptrW);
        focusRing (g, *this);
    }
};

struct LevelSlider : Slider
{
    LevelSlider()
    {
        setSliderStyle (LinearHorizontal);
        setTextBoxStyle (NoTextBox, false, 0, 0);
        setPopupDisplayEnabled (false, false, nullptr, 0);
        setScrollWheelEnabled (false);
        setWantsKeyboardFocus (true);
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key == KeyPress::spaceKey)
            return false;
        return Slider::keyPressed (key);
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float rad = b.getHeight() * 0.5f;
        const float pos = (float) valueToProportionOfLength (getValue());
        g.setColour (col (kPaper));
        g.fillRoundedRectangle (b, rad);
        g.saveState();
        g.reduceClipRegion (b.withWidth (jmax (0.f, b.getWidth() * pos)).toNearestInt());
        g.setColour (col (kLine2));
        g.fillRoundedRectangle (b, rad);
        g.restoreState();
        const bool hot = isMouseOver();
        const bool held = isMouseButtonDown();
        g.setColour (col (held ? kInk : (hot ? kInk2 : kLine2)));
        g.drawRoundedRectangle (b.reduced (0.5f), rad - 0.5f, Theme::strokeHair);

        g.setColour (col (kInk));
        drawLabel (g, "LVL", Theme::Role::micro, b.reduced (8.f, 0.f), Justification::centredLeft);
        drawLabel (g, String (getValue(), 1), Theme::Role::microval, b.reduced (8.f, 0.f), Justification::centredRight);
        focusRing (g, *this);
    }
};

struct SwingSlider : Slider
{
    SwingSlider()
    {
        setSliderStyle (LinearHorizontal);
        setTextBoxStyle (NoTextBox, false, 0, 0);
        setPopupDisplayEnabled (false, false, nullptr, 0);
        setScrollWheelEnabled (false);
        setWantsKeyboardFocus (true);
        setPaintingIsUnclipped (true);
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key == KeyPress::spaceKey)
            return false;
        return Slider::keyPressed (key);
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float y = b.getCentreY();
        const float pos = (float) valueToProportionOfLength (getValue());
        g.setColour (col (kLine2));
        g.fillRoundedRectangle (b.getX(), y - 2.f, b.getWidth(), 4.f, 2.f);
        g.setColour (col (kInk));
        g.fillRoundedRectangle (b.getX(), y - 2.f, b.getWidth() * pos, 4.f, 2.f);
        const float x = b.getX() + b.getWidth() * pos;
        const float thumbW = isMouseOver() || isMouseButtonDown() ? 6.f : 4.f;
        g.fillRoundedRectangle (x - thumbW * 0.5f, y - 8.f, thumbW, 16.f, 2.f);
        focusRing (g, *this, Theme::radiusKey);
    }
};

struct IntensitySlider : Slider
{
    String word { "FULL ROLL" };
    double dragOrigin = 75.0;
    Point<float> dragPos;

    IntensitySlider()
    {
        setRange (0.0, 100.0, 1.0);
        setValue (75.0, dontSendNotification);
        setSliderStyle (LinearBar);
        setTextBoxStyle (NoTextBox, false, 0, 0);
        setPopupDisplayEnabled (false, false, nullptr, 0);
        setScrollWheelEnabled (true);
        setWantsKeyboardFocus (true);
        setPaintingIsUnclipped (true);
        setDoubleClickReturnValue (true, 75.0, ModifierKeys::noModifiers);
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (e.getNumberOfClicks() >= 2)
        {
            setValue (75.0, sendNotificationSync);
            return;
        }
        dragOrigin = getValue();
        dragPos = e.position;
    }

    void mouseDrag (const MouseEvent& e) override
    {
        const float dx = e.position.x - dragPos.x;
        const float dy = dragPos.y - e.position.y;
        const float delta = std::abs (dx) >= std::abs (dy) ? dx : dy;
        const float fine = e.mods.isShiftDown() ? 0.1f : 1.f;
        setValue (jlimit (0.0, 100.0, dragOrigin + (double) (delta * (100.f / 200.f) * fine)),
                  sendNotificationSync);
    }

    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails& wheel) override
    {
        if (std::abs (wheel.deltaY) < 0.0001f && std::abs (wheel.deltaX) < 0.0001f)
            return;
        const int dir = wheel.deltaY > 0.f || wheel.deltaX > 0.f ? 1 : -1;
        setValue (jlimit (0.0, 100.0, getValue() + (double) dir), sendNotificationSync);
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key == KeyPress::spaceKey)
            return false;
        const int step = key.getModifiers().isShiftDown() ? 10 : 1;
        if (key == KeyPress::leftKey)
        {
            setValue (jlimit (0.0, 100.0, getValue() - (double) step), sendNotificationSync);
            return true;
        }
        if (key == KeyPress::rightKey)
        {
            setValue (jlimit (0.0, 100.0, getValue() + (double) step), sendNotificationSync);
            return true;
        }
        return false;
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const bool hot = isMouseOver();
        const bool held = isMouseButtonDown();
        const float rad = Theme::pillRadius (b.getHeight());
        g.setColour (col (kPaper));
        g.fillRoundedRectangle (b, rad);
        g.setColour (col (held ? kInk : (hot ? kInk2 : kLine2)));
        g.drawRoundedRectangle (b.reduced (0.5f), rad - 0.5f, Theme::strokeHair);

        const float pos = (float) valueToProportionOfLength (getValue());
        g.setColour (col (kLine2));
        g.fillRoundedRectangle (16.f, 18.f, 40.f, 4.f, 2.f);
        g.setColour (col (kInk));
        g.fillRoundedRectangle (16.f, 18.f, 40.f * pos, 4.f, 2.f);
        const float thumb = 16.f + 40.f * pos;
        g.fillEllipse (thumb - 6.f, 14.f, 12.f, 12.f);

        // Text column: from the end of the mini track to the pill's right inner edge.
        const float textX = 64.f;
        const float textW = jmax (0.f, b.getWidth() - textX - (float) Theme::space16);
        g.setColour (col (kInk3));
        drawLabel (g, "INTENSITY " + String (roundToInt (getValue())), Theme::Role::meta,
                   Rectangle<float> (textX, 4.f, textW, 14.f), Justification::centredLeft);
        g.setColour (col (kInk));
        drawLabel (g, word, Theme::Role::pill, Rectangle<float> (textX, 18.f, textW, 16.f), Justification::centredLeft);
        focusRing (g, *this, rad);
    }
};

struct LabeledKnob : Component
{
    KnobSlider slider;
    String caption, readout, detail;
    bool compact = false;
    bool macro = false;

    // Labels draw inside the component's own bounds; the parent sizes it wide enough
    // for the caption and readout (compact 40, sound 80, macro 136) and the knob sits centred.
    LabeledKnob()
    {
        addAndMakeVisible (slider);
    }

    void setReadout (const String& value, const String& extra = {})
    {
        if (value == readout && extra == detail)
            return;
        readout = value;
        detail = extra;
        repaint();
    }

    void resized() override
    {
        const int size = compact ? Theme::knobSmall : Theme::knobLarge;
        slider.setBounds ((getWidth() - size) / 2, compact ? 14 : Theme::space24, size, size);
    }

    void paint (Graphics& g) override
    {
        const auto full = getLocalBounds().toFloat();
        g.setColour (col (kInk2));
        drawLabel (g, caption,
                   compact ? Theme::Role::micro : (macro ? Theme::Role::macro : Theme::Role::caption),
                   full.withHeight (compact ? 12.f : 16.f), Justification::centred);

        if (macro)
        {
            g.setColour (col (kInk));
            drawLabel (g, readout, Theme::Role::macroval, full.withY (96.f).withHeight (16.f), Justification::centred);
            g.setColour (col (kInk2));
            drawLabel (g, detail, Theme::Role::meta, full.withY (112.f).withHeight (16.f), Justification::centred);
        }
        else
        {
            g.setColour (col (kInk));
            drawLabel (g, readout, compact ? Theme::Role::microval : Theme::Role::value,
                       full.withTrimmedTop (full.getHeight() - (compact ? 12.f : 16.f)), Justification::centred);
        }
    }
};

struct PatternStrip : Component
{
    int part = 0;
    const Groove* groove = nullptr;
    float swing = 0.52f;
    double pos16 = 0.0;
    bool playing = false;

    float xOf (float step, float width) const
    {
        const float loop = (float) jmax (1, (groove != nullptr ? groove->bars : 8) * 16);
        return (std::clamp (swungStart (step, swing), 0.f, loop) / loop) * width;
    }

    void paint (Graphics& g) override
    {
        if (groove == nullptr)
            return;
        auto area = getLocalBounds().toFloat();
        const int steps = jmax (1, groove->bars * 16);
        g.setColour (col (kLine).withAlpha (0.85f));
        for (int s = 16; s < steps; s += 16)
        {
            const float x = area.getX() + xOf ((float) s, area.getWidth());
            g.drawVerticalLine (roundToInt (x), area.getY() + 2.f, area.getBottom() - 2.f);
        }

        if (part == Drums)
        {
            // kick / snare+clap / hat — ink / ink / ink2
            const Colour rows[] = { col (kInk), col (kInk), col (kInk2) };
            const float rh = (area.getHeight() - 4.f) / 3.f;
            for (int t = 0; t < steps && t < kMaxSteps; ++t)
            {
                int vel[3] = {};
                for (int v = 0; v < NumDrumVoices; ++v)
                {
                    const int row = (v == Kick) ? 0 : ((v == HatC || v == HatO || v == Ride) ? 2 : 1);
                    vel[row] = jmax (vel[row], (int) groove->drums.vel[v][t]);
                }
                for (int row = 0; row < 3; ++row)
                {
                    if (vel[row] <= 0)
                        continue;
                    const float x = area.getX() + xOf ((float) t, area.getWidth());
                    const float y = area.getY() + 2.f + (float) (2 - row) * rh;
                    const float h = jmax (2.f, (rh - 2.f) * (0.45f + 0.55f * vel[row] / 127.f));
                    g.setColour (rows[row].withAlpha (0.45f + 0.55f * vel[row] / 127.f));
                    g.fillRoundedRectangle (x, y + (rh - 2.f - h) * 0.5f,
                                            jmax (1.5f, area.getWidth() / (float) steps * 0.72f), h, 0.5f);
                }
            }
        }
        else
        {
            const auto& lane = groove->lane[part];
            const float h = jmin (10.f, area.getHeight() - 8.f);
            for (int i = 0; i < lane.count; ++i)
            {
                const auto& n = lane.notes[i];
                const float x0 = area.getX() + xOf (n.start, area.getWidth());
                const float x1 = area.getX() + xOf (n.start + jmax (0.35f, n.length), area.getWidth());
                const bool ghost = (n.flags & NoteGhost) != 0;
                g.setColour (laneColour (part).withAlpha (ghost ? 0.45f : 0.95f));
                g.fillRoundedRectangle (x0, area.getCentreY() - h * 0.5f, jmax (2.f, x1 - x0), h, 1.f);
            }
        }

        if (playing)
        {
            float wrapped = (float) std::fmod (pos16, (double) steps);
            if (wrapped < 0.f)
                wrapped += (float) steps;
            const float x = area.getX() + xOf (wrapped, area.getWidth());
            g.setColour (col (kInk).withAlpha (0.85f));
            g.drawLine (x, area.getY(), x, area.getBottom(), 1.f);
        }
    }
};

struct IconButton : QuietButton
{
    enum Kind { mute, dice, lock };

    Kind kind;
    RollaProcessor* proc = nullptr;
    int part = 0;

    IconButton (Kind k, const String& name)
        : QuietButton (name), kind (k)
    {
        setWantsKeyboardFocus (true);
        setPaintingIsUnclipped (true);
        setMouseCursor (MouseCursor::NormalCursor);
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key == KeyPress::spaceKey || key == KeyPress::returnKey)
            return false;
        return QuietButton::keyPressed (key);
    }

    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const bool on = getToggleState();
        g.saveState();
        if (down)
            g.addTransform (AffineTransform::translation (0.f, 1.f));

        if (kind == mute && on)
        {
            g.setColour (col (kMute));
            g.fillEllipse (b.reduced (1.f));
        }
        else if (kind == lock && on)
        {
            g.setColour (col (kInk));
            g.fillEllipse (b.reduced (1.f));
        }
        else if (down)
        {
            g.setColour (col (kLine2));
            g.fillEllipse (b.reduced (1.f));
        }
        else if (over)
        {
            g.setColour (col (kLine));
            g.fillEllipse (b.reduced (1.f));
        }

        const auto glyph = (kind == mute && on) || (kind == lock && on) ? col (kPaper)
                         : (over ? col (kInk) : col (kInk2));
        g.setColour (glyph);

        if (kind == mute)
        {
            drawLabel (g, "M", Theme::Role::button, getLocalBounds(), Justification::centred);
        }
        else if (kind == dice)
        {
            auto die = b.withSizeKeepingCentre ((float) Theme::glyphBox, (float) Theme::glyphBox);
            g.drawRoundedRectangle (die, 2.2f, Theme::strokeIcon);
            auto pip = [&] (float x, float y)
            {
                g.fillEllipse (x - 1.15f, y - 1.15f, 2.3f, 2.3f);
            };
            pip (die.getX() + 3.1f, die.getY() + 3.1f);
            pip (die.getRight() - 3.1f, die.getY() + 3.1f);
            pip (die.getCentreX(), die.getCentreY());
            pip (die.getX() + 3.1f, die.getBottom() - 3.1f);
            pip (die.getRight() - 3.1f, die.getBottom() - 3.1f);
        }
        else
        {
            auto body = b.reduced (9.f, 8.f).removeFromBottom (11.f);
            if (on)
                g.fillRoundedRectangle (body, 1.6f);
            else
                g.drawRoundedRectangle (body, 1.6f, Theme::strokeIcon);
            Path shackle;
            if (on)
            {
                shackle.startNewSubPath (body.getX() + 2.4f, body.getY());
                shackle.lineTo (body.getX() + 2.4f, body.getY() - 3.2f);
                shackle.addCentredArc (body.getCentreX(), body.getY() - 3.2f,
                                       body.getWidth() * 0.5f - 2.4f, 3.4f,
                                       0.f, MathConstants<float>::pi, 0.f, true);
                shackle.lineTo (body.getRight() - 2.4f, body.getY());
            }
            else
            {
                shackle.startNewSubPath (body.getX() + 2.4f, body.getY());
                shackle.lineTo (body.getX() + 2.4f, body.getY() - 4.f);
                shackle.addCentredArc (body.getCentreX(), body.getY() - 4.f,
                                       body.getWidth() * 0.5f - 2.4f, 3.6f,
                                       0.f, MathConstants<float>::pi, MathConstants<float>::pi * 0.15f, true);
            }
            g.strokePath (shackle, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        }

        g.restoreState();

        if (kind == lock && proc != nullptr && proc->laneEdited (part))
        {
            g.setColour (col (kSel));
            g.fillEllipse (b.getRight() - 10.f, b.getY() - 2.f, 12.f, 12.f);
            g.setColour (col (kMark));
            g.fillEllipse (b.getRight() - 8.f, b.getY(), 8.f, 8.f);
        }
        focusRing (g, *this);
    }
};

void dragFilesOut (Component* source, const StringArray& files)
{
    if (source == nullptr || files.isEmpty())
        return;
    if (auto* dnd = DragAndDropContainer::findParentDragContainerFor (source))
        dnd->performExternalDragDropOfFiles (files, false, source);
}

// One lane. Drag writes a MIDI clip. Alt, Ctrl, or Command (or the click
// toggle) writes one loop (LENGTH) of that lane as WAV instead.
struct LaneDrag : Component
{
    RollaProcessor* proc = nullptr;
    int part = 0;
    bool audioMode = false;
    bool armed = false;

    LaneDrag()
    {
        setMouseCursor (MouseCursor::DraggingHandCursor);
        setRepaintsOnMouseActivity (true);
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const bool hot = isMouseOver() || isMouseButtonDown();
        g.setColour (audioMode ? col (kLine2) : (hot ? col (kLine) : Colours::transparentBlack));
        g.fillEllipse (b.reduced (1.f));
        g.setColour (hot || audioMode ? col (kInk2) : col (kLine2));
        g.drawEllipse (b.reduced (1.5f), 1.f);

        if (audioMode)
        {
            g.setColour (col (kInk));
            drawLabel (g, "WAV", Theme::Role::micro, getLocalBounds(), Justification::centred);
        }
        else
        {
            g.setColour (hot ? col (kInk) : col (kInk2));
            Path arrow;
            const float x = b.getCentreX();
            const float y = b.getCentreY();
            arrow.startNewSubPath (x, y - 6.f);
            arrow.lineTo (x, y + 5.f);
            arrow.startNewSubPath (x - 4.f, y + 1.f);
            arrow.lineTo (x, y + 5.f);
            arrow.lineTo (x + 4.f, y + 1.f);
            g.strokePath (arrow, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        }
        focusRing (g, *this);
    }

    void mouseDown (const MouseEvent&) override { armed = false; }

    void mouseDrag (const MouseEvent& e) override
    {
        if (proc == nullptr || armed || e.getDistanceFromDragStart() <= 4.f)
            return;
        armed = true;
        const bool audio = audioMode || e.mods.isAltDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
        dragFilesOut (this, audio ? proc->writeLaneAudio (part) : proc->writeLaneMidi (part));
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (! armed && e.getDistanceFromDragStart() <= 4.f && e.mouseWasClicked())
        {
            audioMode = ! audioMode;
            repaint();
        }
    }
};

struct LaneRow : Component, FileDragAndDropTarget
{
    RollaProcessor* proc = nullptr;
    int part = 0;
    bool selected = false;
    bool hovered = false;
    bool hot = false;
    bool sampleMissing = false;
    bool laneOut = false;
    int comesIn = 75;
    std::function<void (int)> onSelect;
    std::function<void (int)> onNudge;
    IconButton mute { IconButton::mute, "Mute" };
    IconButton shuffle { IconButton::dice, "Shuffle" };
    IconButton keep { IconButton::lock, "Keep" };
    LaneDrag drag;
    PatternStrip strip;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> muteA, keepA;

    LaneRow()
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (MouseCursor::PointingHandCursor);
        addAndMakeVisible (strip);
        addAndMakeVisible (drag);
        addAndMakeVisible (mute);
        addAndMakeVisible (shuffle);
        addAndMakeVisible (keep);
        mute.setMouseCursor (MouseCursor::NormalCursor);
        shuffle.setMouseCursor (MouseCursor::NormalCursor);
        keep.setMouseCursor (MouseCursor::NormalCursor);
        strip.setInterceptsMouseClicks (false, false);
        mute.setClickingTogglesState (true);
        keep.setClickingTogglesState (true);
    }

    void configure (RollaProcessor& processor, int index)
    {
        proc = &processor;
        part = index;
        mute.proc = &processor;
        mute.part = index;
        keep.proc = &processor;
        keep.part = index;
        shuffle.proc = &processor;
        shuffle.part = index;
        strip.part = index;
        drag.proc = &processor;
        drag.part = index;
        bindHelp (drag, "Drag this lane. MIDI clip, or WAV (click the handle, or hold Alt) for one loop.");
        const auto id = String (partId (part));
        muteA = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id + "Mute", mute);
        keepA = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, id + "Keep", keep);
        bindHelp (mute, "Mute this lane. Roll leaves it alone, the same as Keep.");
        bindHelp (shuffle, "Shuffle this lane only. Re-rolls it even if it is muted or kept, and clears an automatic keep.");
        bindHelp (keep, "Keep this lane. Roll leaves a kept lane alone.");
        shuffle.onClick = [this] { if (proc != nullptr) proc->triggerShuffle (part); };
        keep.onClick = [this]
        {
            if (proc != nullptr && ! keep.getToggleState())
                proc->clearEdited (part);
        };
    }

    void syncHover()
    {
        const bool now = isMouseOver (true);
        if (now == hovered)
            return;
        hovered = now;
        repaint();
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (e.eventComponent == this && onSelect)
            onSelect (part);
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key == KeyPress::spaceKey)
            return false;
        if (key == KeyPress::returnKey)
        {
            if (onSelect)
                onSelect (part);
            return true;
        }
        if (key == KeyPress::upKey || key == KeyPress::downKey)
        {
            if (onNudge)
                onNudge (key == KeyPress::downKey ? 1 : -1);
            return true;
        }
        const auto ch = key.getTextCharacter();
        if (ch == 'm' || ch == 'M')
        {
            mute.triggerClick();
            return true;
        }
        if (ch == 'd' || ch == 'D')
        {
            if (shuffle.isEnabled())
                shuffle.triggerClick();
            return true;
        }
        if (ch == 'l' || ch == 'L')
        {
            keep.triggerClick();
            return true;
        }
        return false;
    }

    void paint (Graphics& g) override
    {
        const float rowH = (float) getHeight();
        Path clip;
        clip.addRoundedRectangle (0.f, -rowH * (float) part, (float) getWidth(), rowH * (float) NumParts, Theme::radiusPanel);
        g.reduceClipRegion (clip);

        auto b = getLocalBounds().toFloat();
        const int y0 = jmax (0, (getHeight() - 40) / 2);
        if (selected)
        {
            g.setColour (col (kSel));
            g.fillRect (b);
        }
        else if (hovered || hot)
        {
            g.setColour (col (kHov));
            g.fillRect (b);
        }

        if (part < Drums)
        {
            g.setColour (col (kLine));
            g.fillRect (0.f, b.getBottom() - 1.f, b.getWidth(), 1.f);
        }

        if (selected)
        {
            g.setColour (laneColour (part));
            g.fillRect (0.f, 0.f, 4.f, b.getHeight());
            g.setColour (col (kLine2));
            g.drawRect (b.reduced (0.5f), 1.f);
        }

        const bool muted = mute.getToggleState();
        float dotA = muted ? Theme::alphaMuted : 1.f;
        if (laneOut && ! muted)
            dotA = 0.35f;
        g.setColour (laneColour (part).withAlpha (dotA));
        g.fillEllipse (16.f, 16.f + (float) y0, 8.f, 8.f);
        const auto name = String (laneShortName (part));
        const auto nameFont = Theme::font (Theme::Role::lane);
        const auto nameCol = muted ? col (kInk2).withAlpha (Theme::alphaMutedText)
                                   : ((selected || hovered) ? col (kInk)
                                                            : (laneOut ? col (kInk3) : col (kInk2)));
        g.setColour (nameCol);
        drawLabel (g, name, Theme::Role::lane, Rectangle<int> (32, 12 + y0, 72, 16), Justification::centredLeft);
        if (sampleMissing)
        {
            float x = 32.f + textWidth (nameFont, name) + 4.f;
            if (x > 92.f)
                x = 92.f;
            paintMissingIcon (g, x, 14.f + (float) y0, 12.f);
        }

        if (laneOut)
        {
            // Same box as the (hidden) pattern strip, so it never runs under the drag handle.
            g.setColour (col (kInk3));
            drawLabel (g, "out " + String (utf8 ("·")) + " comes in at " + String (comesIn) + "%",
                       Theme::Role::meta, strip.getBounds(), Justification::centredLeft);
        }

        if (hot)
        {
            g.setColour (col (kInk));
            g.drawRect (b.reduced (0.5f), 1.f);
        }

        if (hasKeyboardFocus (false))
        {
            g.setColour (col (kInk));
            g.drawRoundedRectangle (b.reduced (2.f), 4.f, Theme::strokeFocus);
        }
    }

    bool isInterestedInFileDrag (const StringArray& files) override
    {
        // The drums lane's drops land on the 909 voice pads, which have their own slots.
        return part >= Bass && part <= Vox && files.size() > 0 && audioFile (files[0]);
    }

    void fileDragEnter (const StringArray&, int, int) override
    {
        hot = true;
        repaint();
    }

    void fileDragExit (const StringArray&) override
    {
        hot = false;
        repaint();
    }

    void filesDropped (const StringArray& files, int, int) override
    {
        hot = false;
        const int slot = userSlotForPart (part);
        if (proc != nullptr && slot >= 0 && ! files.isEmpty())
        {
            String err;
            proc->loadUserSlot (slot, File (files[0]), err);
        }
        repaint();
    }

    void resized() override
    {
        // 40px row → 24px strip. 80px row → 48px strip. Both stay centred.
        // The strip stops short of the drag handle, which sits just before mute.
        const float rh = (float) getHeight();
        const float stripH = 24.f + (rh - 40.f) * (24.f / 40.f);
        const int sh = jmax (1, roundToInt (stripH));
        const int sy = jmax (0, roundToInt ((rh - (float) sh) * 0.5f));
        strip.setBounds (112, sy, 352, sh);
        const int yIcon = jmax (0, roundToInt ((rh - 32.f) * 0.5f));
        drag.setBounds (476, yIcon, 32, 32);
        mute.setBounds (520, yIcon, 32, 32);
        shuffle.setBounds (560, yIcon, 32, 32);
        keep.setBounds (600, yIcon, 32, 32);
    }
};

void chevron (Graphics& g, Rectangle<float> b, bool right)
{
    Path p;
    const float x = b.getCentreX();
    const float y = b.getCentreY();
    if (right)
    {
        p.startNewSubPath (x - 2.f, y - 3.5f);
        p.lineTo (x + 1.5f, y);
        p.lineTo (x - 2.f, y + 3.5f);
    }
    else
    {
        p.startNewSubPath (x + 2.f, y - 3.5f);
        p.lineTo (x - 1.5f, y);
        p.lineTo (x + 2.f, y + 3.5f);
    }
    g.strokePath (p, PathStrokeType (Theme::strokeIcon, PathStrokeType::curved, PathStrokeType::rounded));
}

struct Stepper : Component
{
    QuietButton prev { "Previous" }, next { "Next" };
    String title, sub;
    bool strike = false;
    std::function<void (int)> onStep;
    std::function<void()> onBrowse;

    Stepper()
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
        addAndMakeVisible (prev);
        addAndMakeVisible (next);
        prev.setWantsKeyboardFocus (true);
        next.setWantsKeyboardFocus (true);
        prev.setMouseCursor (MouseCursor::PointingHandCursor);
        next.setMouseCursor (MouseCursor::PointingHandCursor);
        prev.onClick = [this] { if (onStep && prev.isEnabled()) onStep (-1); };
        next.onClick = [this] { if (onStep && next.isEnabled()) onStep (1); };
        prev.onStateChange = [this] { repaint(); };
        next.onStateChange = [this] { repaint(); };
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (e.eventComponent == this && onBrowse)
            onBrowse();
    }

    void setEnds (bool atStart, bool atEnd)
    {
        prev.setEnabled (! atStart);
        next.setEnabled (! atEnd);
        prev.setAlpha (atStart ? Theme::alphaDisabled : 1.f);
        next.setAlpha (atEnd ? Theme::alphaDisabled : 1.f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int w = jmin (40, r.getWidth() / 4);
        prev.setBounds (r.removeFromLeft (w));
        next.setBounds (r.removeFromRight (w));
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col (kPaper));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (col (isMouseOver (false) ? kInk2 : kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f - 0.5f, Theme::strokeHair);

        const bool kit = getHeight() < 36;
        const auto titleRole = sub.isEmpty() && ! kit ? Theme::Role::pill : Theme::Role::preset;
        // Text column between the two arrow buttons. Preset names are bank content: ellipsis allowed.
        auto mid = getLocalBounds().reduced (prev.getWidth(), 0).toFloat();
        g.setColour (strike ? col (kInk3) : col (kInk));
        if (sub.isEmpty())
            drawLabel (g, title, titleRole, mid, Justification::centred, true);
        else
        {
            drawLabel (g, title, titleRole, mid.removeFromTop (mid.getHeight() * 0.58f), Justification::centred, true);
            if (strike)
            {
                const float tw = jmin (mid.getWidth(), textWidth (titleRole, title));
                g.drawLine (mid.getCentreX() - tw * 0.5f, mid.getY() - 1.f, mid.getCentreX() + tw * 0.5f, mid.getY() - 1.f, 1.f);
            }
            g.setColour (col (kInk3));
            drawLabel (g, sub, Theme::Role::meta, mid, Justification::centred, true);
        }
    }

    void paintOverChildren (Graphics& g) override
    {
        auto arrow = [&] (QuietButton& btn, bool right)
        {
            const float disc = getHeight() >= 36 ? 32.f : 24.f;
            auto b = btn.getBounds().toFloat().withSizeKeepingCentre (disc, disc);
            if (btn.isEnabled() && btn.isDown())
            {
                g.setColour (col (kLine));
                g.fillEllipse (b);
            }
            else if (btn.isEnabled() && btn.isOver())
            {
                g.setColour (col (kHov));
                g.fillEllipse (b);
            }
            g.setColour (btn.isEnabled() ? (btn.isOver() || btn.isDown() ? col (kInk) : col (kInk2))
                                         : col (kInk2).withAlpha (Theme::alphaDisabled));
            auto glyph = btn.getBounds().toFloat();
            if (btn.isEnabled() && btn.isDown())
                glyph.translate (0.f, 1.f);
            chevron (g, glyph, right);
        };
        arrow (prev, false);
        arrow (next, true);
    }
};

// Compact header dropdown (KEY, SCALE): the value over a caption, and a caret.
// Same pill as Stepper, without the arrows, so two fit in the stepperKey slot.
struct DropPill : Component
{
    String title, sub;
    std::function<void()> onOpen;

    DropPill()
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }

    void mouseDown (const MouseEvent&) override
    {
        if (onOpen)
            onOpen();
    }

    bool keyPressed (const KeyPress& key) override
    {
        if (key == KeyPress::spaceKey || key == KeyPress::returnKey)
        {
            if (onOpen)
                onOpen();
            return true;
        }
        return false;
    }

    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col (kPaper));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (col (isMouseOver (false) ? kInk2 : kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f - 0.5f, Theme::strokeHair);

        // 64 px pill: 8 px left inset, 16 px right for the caret, so a 40 px text column.
        // Longest strings are "AUTO" / "SCALE" (5 mono glyphs at 10 pt, about 33 px).
        auto text = getLocalBounds().toFloat().withTrimmedLeft ((float) Theme::space8).withTrimmedRight ((float) Theme::space16);
        g.setColour (col (kInk));
        drawLabel (g, title, Theme::Role::pill, text.removeFromTop (text.getHeight() * 0.58f), Justification::centred);
        g.setColour (col (kInk3));
        drawLabel (g, sub, Theme::Role::meta, text, Justification::centredTop);

        const float cx = b.getRight() - (float) Theme::space8 - 4.f;
        const float cy = b.getCentreY();
        Path caret;
        caret.startNewSubPath (cx - 3.5f, cy - 1.5f);
        caret.lineTo (cx, cy + 2.f);
        caret.lineTo (cx + 3.5f, cy - 1.5f);
        g.setColour (col (isMouseOver (false) ? kInk : kInk2));
        g.strokePath (caret, PathStrokeType (Theme::strokeIcon, PathStrokeType::curved, PathStrokeType::rounded));
    }
};

struct DialectBar : Component
{
    static constexpr Theme::Role labelRole = Theme::Role::pill;
    QuietButton buttons[NumDialects];
    int current = 0;
    std::function<void (int)> onPick;

    DialectBar()
    {
        for (int i = 0; i < NumDialects; ++i)
        {
            buttons[i].setButtonText (dialectName (i));
            buttons[i].setWantsKeyboardFocus (true);
            buttons[i].setMouseCursor (MouseCursor::PointingHandCursor);
            addAndMakeVisible (buttons[i]);
            bindHelp (buttons[i], styleCopy (i).tip);
            buttons[i].onClick = [this, i] { if (onPick) onPick (i); };
            buttons[i].onStateChange = [this] { repaint(); };
        }
#if JUCE_DEBUG
        {
            GlyphArrangement glyphs;
            const auto pill = Theme::font (Theme::Role::pill);
            glyphs.addLineOfText (pill, "GASKIN 90s", 0.f, 0.f);
            const float measured = glyphs.getBoundingBox (0, -1, true).getWidth();
            jassert (std::abs (measured - 71.4f) <= 0.5f);
            jassert (measured + 2.f * Theme::padPillLarge <= (float) Theme::segmentLarge);
        }
#endif
    }

    void setCurrent (int index)
    {
        if (current == index)
            return;
        current = index;
        repaint();
    }

    void resized() override
    {
        const int h = getHeight() - Theme::space8;
        for (int i = 0; i < NumDialects; ++i)
            buttons[i].setBounds (Theme::space4 + i * Theme::segmentLarge, Theme::space4,
                                  Theme::segmentLarge, h);
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float rad = b.getHeight() * 0.5f;
        g.setColour (col (kPaper));
        g.fillRoundedRectangle (b, rad);
        g.setColour (col (kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), rad - 0.5f, Theme::strokeHair);
    }

    void paintOverChildren (Graphics& g) override
    {
        for (int i = 0; i < NumDialects; ++i)
        {
            auto b = buttons[i].getBounds().toFloat();
            const bool on = i == current;
            const bool down = buttons[i].isDown();
            const bool over = buttons[i].isOver();
            const float rad = b.getHeight() * 0.5f;
            if (on)
            {
                g.setColour (col (kInk));
                g.fillRoundedRectangle (b, rad);
            }
            else if (down)
            {
                g.setColour (col (kLine));
                g.fillRoundedRectangle (b, rad);
            }
            else if (over)
            {
                g.setColour (col (kHov));
                g.fillRoundedRectangle (b, rad);
            }
            g.setColour (on ? col (kPaper) : ((over || down) ? col (kInk) : col (kInk2)));
            auto label = buttons[i].getBounds();
            if (down && ! on)
                label.translate (0, 1);
            drawLabel (g, buttons[i].getButtonText(), labelRole, label, Justification::centred);
            if (buttons[i].hasKeyboardFocus (false))
            {
                g.setColour (col (kInk));
                g.drawRoundedRectangle (b.expanded (3.f), Theme::focusRadius (rad), Theme::strokeFocus);
            }
        }
    }
};

struct ModeBar : Component
{
    static constexpr Theme::Role labelRole = Theme::Role::button;
    QuietButton buttons[3];
    int current = 0;
    std::function<void (int)> onPick;

    ModeBar()
    {
        for (int i = 0; i < 3; ++i)
        {
            buttons[i].setButtonText (kLaneModeNames[i]);
            buttons[i].setWantsKeyboardFocus (true);
            buttons[i].setMouseCursor (MouseCursor::PointingHandCursor);
            addAndMakeVisible (buttons[i]);
            buttons[i].onClick = [this, i] { if (onPick) onPick (i); };
            buttons[i].onStateChange = [this] { repaint(); };
        }
    }

    void setCurrent (int index)
    {
        index = jlimit (0, 2, index);
        if (current == index)
            return;
        current = index;
        repaint();
    }

    void resized() override
    {
        const int inner = getWidth() - Theme::space8;
        const int seg = inner / 3;
        const int h = getHeight() - Theme::space8;
        for (int i = 0; i < 3; ++i)
        {
            const int x = Theme::space4 + i * seg;
            const int w = (i == 2) ? getWidth() - Theme::space4 - x : seg;
            buttons[i].setBounds (x, Theme::space4, w, h);
        }
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float rad = b.getHeight() * 0.5f;
        g.setColour (col (kPaper));
        g.fillRoundedRectangle (b, rad);
        g.setColour (col (kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), rad - 0.5f, Theme::strokeHair);
    }

    void paintOverChildren (Graphics& g) override
    {
        for (int i = 0; i < 3; ++i)
        {
            auto b = buttons[i].getBounds().toFloat();
            const bool on = i == current;
            const bool down = buttons[i].isDown();
            const bool over = buttons[i].isOver();
            const float rad = b.getHeight() * 0.5f;
            if (on)
            {
                g.setColour (col (kInk));
                g.fillRoundedRectangle (b, rad);
            }
            else if (down)
            {
                g.setColour (col (kLine));
                g.fillRoundedRectangle (b, rad);
            }
            else if (over)
            {
                g.setColour (col (kHov));
                g.fillRoundedRectangle (b, rad);
            }
            g.setColour (on ? col (kPaper) : ((over || down) ? col (kInk) : col (kInk2)));
            auto label = buttons[i].getBounds();
            if (down && ! on)
                label.translate (0, 1);
            drawLabel (g, buttons[i].getButtonText(), labelRole, label, Justification::centred);
            if (buttons[i].hasKeyboardFocus (false))
            {
                g.setColour (col (kInk));
                g.drawRoundedRectangle (b.expanded (3.f), Theme::focusRadius (rad), Theme::strokeFocus);
            }
        }
    }
};

struct HistoryButton : QuietButton
{
    bool redo = false;

    explicit HistoryButton (bool isRedo)
        : QuietButton (isRedo ? "Redo" : "Undo"), redo (isRedo)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (down ? col (kLine) : (over ? col (kHov) : col (kPaper)));
        g.fillEllipse (b);
        g.setColour (col (kLine2));
        g.drawEllipse (b.reduced (0.5f), Theme::strokeHair);

        Path icon;
        icon.startNewSubPath (5.f, 4.f);
        icon.lineTo (2.f, 7.f);
        icon.lineTo (5.f, 10.f);
        icon.startNewSubPath (2.5f, 7.f);
        icon.lineTo (10.f, 7.f);
        icon.addCentredArc (10.f, 10.5f, 3.5f, 3.5f, 0.f,
                            -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
        icon.lineTo (7.f, 14.f);
        auto placed = icon;
        placed.applyTransform (AffineTransform::translation (12.f, down ? 13.f : 12.f));
        if (redo)
            placed.applyTransform (AffineTransform::scale (-1.f, 1.f, 20.f, down ? 21.f : 20.f));
        g.setColour (isEnabled() ? ((over || down) ? col (kInk) : col (kInk2)) : col (kInk2));
        g.strokePath (placed, PathStrokeType (Theme::strokeIcon, PathStrokeType::curved, PathStrokeType::rounded));
        focusRing (g, *this);
    }
};

struct FaceRollButton : QuietButton, private Timer
{
    RollaProcessor* proc = nullptr;
    String countText { "7 lanes" };
    String keptText, keptShort;
    bool blocked = false;

    struct Tween
    {
        float v = 0.f, from = 0.f, to = 0.f;
        double t0 = 0.0;
        bool run = false;

        void go (float target, double now)
        {
            if (std::abs (target - (run ? to : v)) < 1.0e-3f)
                return;
            from = v;
            to = target;
            t0 = now;
            run = true;
        }

        void tick (double now, float ms, bool reduced)
        {
            if (! run)
                return;
            if (reduced || ms <= 1.f)
            {
                v = to;
                run = false;
                return;
            }
            const float t = (float) ((now - t0) / ms);
            if (t >= 1.f)
            {
                v = to;
                run = false;
                return;
            }
            v = from + (to - from) * bezierEase (t, 0.3f, 0.7f, 0.4f, 1.f);
        }
    };

    Tween hover, press;
    bool rolling = false;
    bool snap = false;
    double rollStart = 0.0;
    bool clockOn = false;
    VBlankAttachment vblank;

    FaceRollButton()
        : QuietButton ("Roll")
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (MouseCursor::PointingHandCursor);
        setPaintingIsUnclipped (true);
    }

    // kept: "  ·  BASS + STAB kept". shortKept ("  ·  2 kept") replaces it when it would not fit.
    void setCaption (const String& count, const String& kept, const String& shortKept, bool disable)
    {
        if (count == countText && kept == keptText && shortKept == keptShort && disable == blocked)
            return;
        countText = count;
        keptText = kept;
        keptShort = shortKept;
        blocked = disable;
        setEnabled (! disable);
        setAlpha (disable ? Theme::alphaDisabled : 1.f);
        repaint();
    }

    void play()
    {
        const bool reduced = proc != nullptr
                          && paramRaw (proc->apvts, "reduceMotion") >= 0.5f;
        rolling = true;
        snap = reduced;
        rollStart = Time::getMillisecondCounterHiRes();
        armClock();
        repaint();
    }

    void mouseEnter (const MouseEvent& e) override { QuietButton::mouseEnter (e); kick(); }
    void mouseExit (const MouseEvent& e) override { QuietButton::mouseExit (e); kick(); }
    void mouseDown (const MouseEvent& e) override { QuietButton::mouseDown (e); kick(); }
    void mouseUp (const MouseEvent& e) override { QuietButton::mouseUp (e); kick(); }

    void kick()
    {
        const double now = Time::getMillisecondCounterHiRes();
        const bool reduced = proc != nullptr && paramRaw (proc->apvts, "reduceMotion") >= 0.5f;
        const float h = (! blocked && (isOver() || isDown())) ? 1.f : 0.f;
        const float p = (! blocked && isDown()) ? 1.f : 0.f;
        hover.go (h, now);
        press.go (p, now);
        if (reduced)
        {
            hover.v = h;
            hover.run = false;
            press.v = p;
            press.run = false;
        }
        if (hover.run || press.run || rolling || isOver() || isDown())
            armClock();
        repaint();
    }

    void armClock()
    {
        if (clockOn && (getPeer() == nullptr || ! vblank.isEmpty()))
            return;
        clockOn = true;
        if (getPeer() != nullptr)
        {
            stopTimer();
            if (vblank.isEmpty())
                vblank = VBlankAttachment (this, [this] (double) { advance(); });
        }
        else if (! isTimerRunning())
        {
            startTimerHz (60);
        }
    }

    void disarmClock()
    {
        clockOn = false;
        stopTimer();
        if (vblank.isEmpty())
            return;
        if (auto* mm = MessageManager::getInstanceWithoutCreating())
        {
            Component::SafePointer<FaceRollButton> alive (this);
            mm->callAsync ([alive]
            {
                if (alive != nullptr && ! alive->clockOn)
                    alive->vblank = VBlankAttachment();
            });
        }
        else
        {
            vblank = VBlankAttachment();
        }
    }

    void advance()
    {
        const double now = Time::getMillisecondCounterHiRes();
        const bool reduced = proc != nullptr && paramRaw (proc->apvts, "reduceMotion") >= 0.5f;
        hover.tick (now, 120.f, reduced);
        press.tick (now, 90.f, reduced);
        if (rolling)
        {
            const float elapsed = (float) (now - rollStart);
            if (reduced)
            {
                if (elapsed >= 300.f)
                    rolling = false;
            }
            else if (elapsed >= 340.f)
                rolling = false;
        }
        repaint();
        const bool busy = rolling || hover.run || press.run || hover.v > 0.001f || press.v > 0.001f || isOver() || isDown();
        if (! busy)
            disarmClock();
        else if (getPeer() != nullptr && vblank.isEmpty())
            armClock();
    }

    void timerCallback() override
    {
        advance();
    }

    void paintButton (Graphics& g, bool /*over*/, bool /*down*/) override
    {
        auto b = getLocalBounds().toFloat();
        const bool hot = ! blocked && isOver();
        const bool held = ! blocked && isDown();
        g.setColour (held ? col (kPress) : (hot ? col (kHov) : col (kCard)));
        g.fillRoundedRectangle (b, Theme::radiusPanel);
        g.setColour (col (hot ? kLine2 : kLine));
        g.drawRoundedRectangle (b.reduced (0.5f), Theme::radiusPanel, Theme::strokeHair);

        FacePose pose;
        pose.hover = hover.v;
        pose.press = press.v;
        pose.snap = snap && rolling;
        pose.roll = (! pose.snap && rolling) ? (float) ((Time::getMillisecondCounterHiRes() - rollStart) / 340.0) : -1.f;
        pose.roll = std::clamp (pose.roll, -1.f, 1.f);
        if (proc != nullptr)
            pose.dark = paramRaw (proc->apvts, "dark");
        paintRollFace (g, { 16.f, 16.f, 96.f, 96.f }, pose);

        // Text column: right of the face (16 + 96 + 16) to the tile's 16 px inner margin.
        const float textX = 128.f;
        const float textRight = b.getRight() - (float) Theme::space16;
        const auto keycap = Rectangle<float> (textRight - 48.f, 40.f, 48.f, 20.f);

        g.setColour (col (kInk));
        drawLabel (g, "Roll", Theme::Role::roll,
                   Rectangle<float> (textX, 32.f, keycap.getX() - (float) Theme::space8 - textX, 40.f),
                   Justification::centredLeft);

        g.setColour (col (kInk2));
        g.drawRoundedRectangle (keycap, Theme::radiusKey, Theme::strokeHair);
        drawLabel (g, "SPACE", Theme::Role::keycap, keycap, Justification::centred);

        const float room = jmax (0.f, textRight - textX);
        const float left = jmin (room, std::ceil (textWidth (Theme::Role::meta, countText)));
        g.setColour (col (kInk));
        drawLabel (g, countText, Theme::Role::meta, Rectangle<float> (textX, 80.f, left, 16.f), Justification::centredLeft);
        const auto tail = textWidth (Theme::Role::meta, keptText) <= room - left ? keptText : keptShort;
        g.setColour (col (kInk2));
        drawLabel (g, tail, Theme::Role::meta, Rectangle<float> (textX + left, 80.f, room - left, 16.f),
                   Justification::centredLeft);
        focusRing (g, *this, Theme::radiusPanel);
    }
};

struct SampleSlot : Component, FileDragAndDropTarget
{
    RollaProcessor* proc = nullptr;
    int voice = -1; // <0 is the vox slot
    bool hot = false;
    std::function<void()> onLoaded;

    SampleSlot()
    {
        setWantsKeyboardFocus (true);
    }

    int slotIndex() const
    {
        return voice < 0 ? SlotVox : userSlotForDrum (voice);
    }

    bool slotActive() const
    {
        const int slot = slotIndex();
        return proc != nullptr && slot >= 0 && proc->userSlotMode (slot) != 0;
    }

    String slotLabel() const
    {
        if (! slotActive())
            return {};
        return File (proc->userSlotFile (slotIndex())).getFileName();
    }

    void paint (Graphics& g) override
    {
        const bool vox = voice < 0;
        const bool fromSlot = slotActive();
        const bool loaded = fromSlot || (proc != nullptr && (vox ? proc->hasVoxSample() : proc->drumVoiceLoaded (voice)));
        auto b = getLocalBounds().toFloat();
        const float rad = Theme::radiusSlot;
        g.setColour (loaded ? col (kPaper) : col (kCard));
        g.fillRoundedRectangle (b, rad);

        Path outline;
        outline.addRoundedRectangle (b.reduced (0.5f), rad);
        Path dashed;
        const float dash[] = { 3.f, 3.f };
        PathStrokeType (1.f).createDashedStroke (dashed, outline, dash, 2);
        if (loaded || hot)
        {
            g.setColour (hot ? col (kInk) : col (kInk2));
            g.strokePath (outline, PathStrokeType (1.f));
        }
        else
        {
            g.setColour (col (kInk3));
            g.fillPath (dashed);
        }

        if (proc == nullptr)
            return;

        if (vox)
        {
            auto text = b.reduced (12.f, 0.f);
            if (loaded)
                text.removeFromRight (32.f);
            // File names are user content: ellipsis allowed.
            g.setColour (col (kInk));
            drawLabel (g, fromSlot ? slotLabel() : (loaded ? proc->getVoxSampleName() : String ("drop")),
                       Theme::Role::meta, text.removeFromTop (b.getHeight() * 0.55f),
                       Justification::centredLeft, true);
            g.setColour (col (kInk2));
            drawLabel (g, loaded ? String ("overrides preset") : utf8 ("wav · aif · aiff · flac"),
                       Theme::Role::meta, text, Justification::centredLeft);
            if (loaded)
            {
                g.setColour (col (kInk2));
                g.drawLine (b.getRight() - 28.f, b.getCentreY() - 4.f, b.getRight() - 20.f, b.getCentreY() + 4.f, Theme::strokeIcon);
                g.drawLine (b.getRight() - 20.f, b.getCentreY() - 4.f, b.getRight() - 28.f, b.getCentreY() + 4.f, Theme::strokeIcon);
            }
        }
        else
        {
            g.setColour (col (kInk));
            auto top = b.reduced (6.f, 1.f).removeFromTop (b.getHeight() * 0.5f);
            drawLabel (g, loaded ? "YOUR WAV" : "PACK", Theme::Role::micro, top, Justification::centredLeft);
            g.setColour (col (kInk2));
            const auto name = fromSlot ? slotLabel()
                                       : (loaded ? proc->getDrumSampleName (voice) : String (panelVoiceCode (voice)));
            // Truncate by measured width (68 px column), not by character count: 12 mono glyphs did not fit.
            drawLabel (g, name, Theme::Role::microval, b.reduced (6.f, 1.f).removeFromBottom (b.getHeight() * 0.5f),
                       Justification::centredLeft, true);
        }
        focusRing (g, *this, Theme::radiusSlot);
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (proc == nullptr)
            return;
        const bool vox = voice < 0;
        const int slot = slotIndex();
        const bool fromSlot = slotActive();
        const bool loaded = fromSlot || (vox ? proc->hasVoxSample() : proc->drumVoiceLoaded (voice));
        if (! loaded)
            return;
        if (vox)
        {
            auto eject = getLocalBounds().removeFromRight (36);
            if (! eject.contains (e.getPosition()) && ! e.mods.isPopupMenu())
                return;
        }
        if (fromSlot)
            proc->clearUserSlot (slot);
        else if (vox)
            proc->clearVoxSample();
        else
            proc->clearDrumVoice (voice);
        if (onLoaded)
            onLoaded();
        repaint();
    }

    bool isInterestedInFileDrag (const StringArray& files) override
    {
        return files.size() > 0 && audioFile (files[0]);
    }

    void fileDragEnter (const StringArray&, int, int) override { hot = true; repaint(); }
    void fileDragExit (const StringArray&) override { hot = false; repaint(); }

    void filesDropped (const StringArray& files, int, int) override
    {
        hot = false;
        if (proc == nullptr || files.isEmpty())
        {
            repaint();
            return;
        }
        String err;
        const int slot = slotIndex();
        if (slot >= 0)
            proc->loadUserSlot (slot, File (files[0]), err);
        if (onLoaded)
            onLoaded();
        repaint();
    }
};

struct AccentButton : QuietButton
{
    RollaProcessor* proc = nullptr;
    int voice = 0;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> attachment;

    AccentButton()
        : QuietButton ("ACCENT")
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (true);
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void paintButton (Graphics& g, bool over, bool) override
    {
        auto b = getLocalBounds().toFloat();
        const bool on = getToggleState();
        const bool down = isDown();
        const float rad = b.getHeight() * 0.5f;
        if (on)
            g.setColour (col (kInk));
        else if (down)
            g.setColour (col (kLine));
        else if (over)
            g.setColour (col (kHov));
        else
            g.setColour (col (kPaper));
        g.fillRoundedRectangle (b, rad);
        g.setColour (on ? col (kInk) : ((over || down) ? col (kInk2) : col (kLine2)));
        g.drawRoundedRectangle (b.reduced (0.5f), rad - 0.5f, Theme::strokeHair);
        g.setColour (on ? col (kPaper) : ((over || down) ? col (kInk) : col (kInk2)));
        auto label = getLocalBounds();
        if (down && ! on)
            label.translate (0, 1);
        drawLabel (g, "ACCENT", Theme::Role::button, label, Justification::centred);
        focusRing (g, *this);
    }

    void mouseDown (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() && proc != nullptr)
        {
            struct Amount : Component
            {
                SwingSlider slider;
                std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> att;
                Amount (RollaProcessor& p, int v)
                {
                    addAndMakeVisible (slider);
                    att = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (p.apvts, "dAccAmt" + String (v), slider);
                    slider.onDragEnd = [&p] { p.markEdited (Drums); };
                    setSize (188, 36);
                }
                void resized() override { slider.setBounds (getLocalBounds().reduced (12, 10)); }
                void paint (Graphics& g) override
                {
                    g.fillAll (col (kCard));
                }
            };
            auto panel = std::make_unique<Amount> (*proc, voice);
            CallOutBox::launchAsynchronously (std::move (panel), getScreenBounds(), nullptr);
            return;
        }
        QuietButton::mouseDown (e);
    }
};

struct DrumColumn : Component
{
    int voice = 0;
    SampleSlot slot;
    LabeledKnob tune, decay;
    LevelSlider level;
    AccentButton accent;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> tuneA, decayA, levelA;

    void configure (RollaProcessor& proc, int v)
    {
        voice = v;
        const auto id = String (v);
        slot.proc = &proc;
        slot.voice = v;
        slot.onLoaded = [this] { repaint(); };
        tune.caption = "TUNE";
        tune.compact = true;
        tune.slider.getProperties().set ("bipolar", true);
        decay.caption = "DECAY";
        decay.compact = true;
        addAndMakeVisible (slot);
        addAndMakeVisible (tune);
        addAndMakeVisible (decay);
        addAndMakeVisible (level);
        addAndMakeVisible (accent);
        tuneA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "dTune" + id, tune.slider);
        decayA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "dDecay" + id, decay.slider);
        levelA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "dLevel" + id, level);
        accent.proc = &proc;
        accent.voice = v;
        accent.attachment = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "dAccent" + id, accent);
        auto edited = [&proc] { proc.markEdited (Drums); };
        tune.slider.onDragEnd = edited;
        decay.slider.onDragEnd = edited;
        level.onDragEnd = edited;
        accent.onClick = edited;
        bindHelp (tune, "Pitch of this voice in semitones.");
        bindHelp (decay, "How long this voice rings.");
        bindHelp (level, "This voice's volume.");
    }

    void sync()
    {
        tune.setReadout (signed1 ((float) tune.slider.getValue()));
        decay.setReadout (String (decay.slider.getValue() * 10.0, 1));
        slot.repaint();
    }

    void resized() override
    {
        // Knob columns are 40 wide so "DECAY" (micro, ~33 px) fits; knob centres stay at x 20 / 60.
        slot.setBounds (0, 24, 80, 32);
        tune.setBounds (0, 66, 40, 60);
        decay.setBounds (40, 66, 40, 60);
        level.setBounds (0, 132, 80, 24);
        accent.setBounds (0, 164, 80, 24);
    }

    void paint (Graphics& g) override
    {
        g.setColour (col (kInk));
        drawLabel (g, panelVoiceLabel (voice), Theme::Role::voice, Rectangle<int> (0, 0, 80, 16), Justification::centredLeft);
    }
};

struct DragTile : Component
{
    RollaProcessor* proc = nullptr;
    QuietButton exportBtn { "EXPORT" };
    String caption { utf8 ("7 tracks · 8 bars") };
    std::unique_ptr<FileChooser> chooser;
    bool audioMode = false;
    bool armed = false;

    DragTile()
    {
        addAndMakeVisible (exportBtn);
        exportBtn.setWantsKeyboardFocus (true);
        exportBtn.setMouseCursor (MouseCursor::PointingHandCursor);
        setMouseCursor (MouseCursor::DraggingHandCursor);
        exportBtn.onClick = [this] { choose(); };
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        Path outline;
        outline.addRoundedRectangle (b.reduced (0.5f), Theme::radiusPanel);
        Path dashed;
        const float dash[] = { 3.f, 3.f };
        PathStrokeType (1.f).createDashedStroke (dashed, outline, dash, 2);
        g.setColour (isMouseOver (true) ? col (kInk) : col (kInk3));
        g.fillPath (dashed);

        g.setColour (col (kInk2));
        Path arrow;
        arrow.startNewSubPath (28.f, 30.f);
        arrow.lineTo (28.f, 48.f);
        arrow.startNewSubPath (22.f, 42.f);
        arrow.lineTo (28.f, 48.f);
        arrow.lineTo (34.f, 42.f);
        arrow.startNewSubPath (18.f, 56.f);
        arrow.lineTo (38.f, 56.f);
        g.strokePath (arrow, PathStrokeType (Theme::strokeIcon, PathStrokeType::curved, PathStrokeType::rounded));

        // Text column from the arrow to 8 px before EXPORT.
        const int textW = exportBtn.getX() - Theme::space8 - 48;
        g.setColour (col (kInk));
        drawLabel (g, audioMode ? "DRAG STEMS" : "DRAG MIDI", Theme::Role::tile,
                   Rectangle<int> (48, 28, textW, 18), Justification::centredLeft);
        g.setColour (col (kInk2));
        drawLabel (g, caption, Theme::Role::meta, Rectangle<int> (48, 48, textW, 16), Justification::centredLeft);
    }

    void paintOverChildren (Graphics& g) override
    {
        auto b = exportBtn.getBounds().toFloat();
        const bool over = exportBtn.isOver();
        const bool down = exportBtn.isDown();
        const float rad = b.getHeight() * 0.5f;
        if (down)
            g.setColour (col (kLine));
        else if (over)
            g.setColour (col (kHov));
        else
            g.setColour (col (kPaper));
        g.fillRoundedRectangle (b, rad);
        g.setColour ((over || down) ? col (kInk2) : col (kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), rad - 0.5f, Theme::strokeHair);
        g.setColour ((over || down) ? col (kInk) : col (kInk2));
        auto label = exportBtn.getBounds();
        if (down)
            label.translate (0, 1);
        drawLabel (g, "EXPORT", Theme::Role::button, label, Justification::centred);
    }

    void resized() override
    {
        exportBtn.setBounds (240, 28, Theme::buttonWidth, Theme::heightIcon);
    }

    void mouseDown (const MouseEvent&) override { armed = false; }

    void mouseDrag (const MouseEvent& e) override
    {
        if (e.eventComponent != this || proc == nullptr || armed)
            return;
        if (e.getDistanceFromDragStart() <= 4.f)
            return;
        armed = true;
        const bool audio = audioMode || e.mods.isAltDown() || e.mods.isCommandDown() || e.mods.isCtrlDown();
        dragFilesOut (this, audio ? proc->writeStemAudio() : proc->writeTempMidi());
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (e.eventComponent != this || armed)
            return;
        if (e.getDistanceFromDragStart() <= 4.f && e.mouseWasClicked())
        {
            audioMode = ! audioMode;
            caption = audioMode ? utf8 ("7 stems · 8 bars") : utf8 ("7 tracks · 8 bars");
            repaint();
        }
    }

    void choose()
    {
        if (proc == nullptr)
            return;
        const auto start = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("ROLLA");
        chooser = std::make_unique<FileChooser> (audioMode ? "Export stems" : "Export MIDI", start, String(), true);
        const bool stems = audioMode;
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this, stems] (const FileChooser& fc)
                              {
                                  const auto dir = fc.getResult();
                                  if (dir == File() || proc == nullptr)
                                      return;
                                  if (stems)
                                      proc->exportStemsTo (dir);
                                  else
                                      proc->exportMidiTo (dir);
                              });
    }
};

struct SwitchButton : QuietButton
{
    SwitchButton()
        : QuietButton ("DUCK")
    {
        setClickingTogglesState (true);
        setWantsKeyboardFocus (true);
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void paintButton (Graphics& g, bool, bool) override
    {
        auto b = getLocalBounds().toFloat();
        const bool on = getToggleState();
        const bool over = isOver();
        const float rad = b.getHeight() * 0.5f;
        g.setColour (on ? col (kInk) : col (kLine2));
        g.fillRoundedRectangle (b, rad);
        if (over)
        {
            g.setColour (col (kInk2));
            g.drawRoundedRectangle (b.reduced (0.5f), rad - 0.5f, Theme::strokeHair);
        }
        const float d = 16.f;
        const float x = on ? b.getRight() - 4.f - d : b.getX() + 4.f;
        g.setColour (on ? col (kPaper) : (over ? col (kInk) : col (kInk2)));
        g.fillEllipse (x, b.getCentreY() - d * 0.5f, d, d);
        focusRing (g, *this);
    }
};

struct BpmReadout : Component
{
    String bpm { "130.0" };

    void setBpm (float value)
    {
        auto next = String (value, 1);
        if (next == bpm)
            return;
        bpm = next;
        repaint();
    }

    void paint (Graphics& g) override
    {
        const int w = jmin (getWidth(), (int) std::ceil (textWidth (Theme::Role::value, bpm)));
        g.setColour (col (kInk));
        drawLabel (g, bpm, Theme::Role::value, Rectangle<int> (0, 0, w, getHeight()), Justification::centredLeft);
        g.setColour (col (kInk2));
        drawLabel (g, " BPM", Theme::Role::meta, Rectangle<int> (w, 0, getWidth() - w, getHeight()), Justification::centredLeft);
    }
};

struct LogoButton : QuietButton
{
    LogoButton()
        : QuietButton ("ROLLA")
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void paintButton (Graphics& g, bool, bool) override
    {
        g.setColour (col (kInk));
        drawLabel (g, "ROLLA", Theme::Role::wordmark, getLocalBounds(), Justification::centredLeft);
    }
};

// Bottom line: hover help on the left; a processor notice (RollaProcessor::setUiNotice,
// e.g. the Key + Scale guard's "NO SAFE FILE: ...") on the right in the warn colour.
struct StatusStrip : Component
{
    String text { "Hover a control." };
    String notice;

    void setHelp (const String& next)
    {
        // Rich hover cards are "key\tvalue" lines; the strip shows the first line only.
        auto shown = next.upToFirstOccurrenceOf ("\n", false, false).replace ("\t", "  ").trim();
        if (shown.isEmpty())
            shown = "Hover a control.";
        if (shown == text)
            return;
        text = shown;
        repaint();
    }

    void setNotice (const String& next)
    {
        if (next == notice)
            return;
        notice = next;
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto area = getLocalBounds().toFloat();
        if (notice.isNotEmpty())
        {
            const auto font = Theme::Role::micro;
            const float w = jmin (area.getWidth() * 0.6f, std::ceil (textWidth (font, notice)));
            auto slot = area.removeFromRight (w);
            area.removeFromRight ((float) Theme::space16);
            g.setColour (col (kWarn));
            drawLabel (g, notice, font, slot, Justification::centredRight, true);
        }
        g.setColour (col (kInk2));
        drawLabel (g, text, Theme::Role::meta, area, Justification::centredLeft, true);
    }
};

} // namespace

struct RollaEditor::Content : Component
{
    struct SoundBody : Component
    {
        Content* host = nullptr;
        void paint (Graphics& g) override;
    };
    RollaProcessor& proc;
    RollaEditor& editor;
    LogoButton logo;
    DialectBar dialects;
    IntensitySlider intensity;
    DropPill keyDrop, scaleDrop;
    BpmReadout bpm;
    HistoryButton undoBtn { false }, redoBtn { true };
    LaneRow rows[NumParts];
    LabeledKnob tone, punch, level;
    LabeledKnob dark, busy, bounce, stab;
    Stepper presetStep, kitStep;
    ModeBar source;
    LaneSampleProbe samples;
    SoundBody sound;
    SampleSlot voxSlot;
    SwitchButton inStab, duck;
    DrumColumn drums[7];
    FaceRollButton face;
    SwingSlider swing;
    DragTile drag;
    StatusStrip status;
    std::unique_ptr<FileChooser> sampleChooser;
    int selected = Bass;
    float panelAmt = 0.f;     // 0 collapsed, 1 open. The only layout driver.
    float panelFrom = 0.f;
    float panelTarget = 0.f;
    float contentAlpha = 0.f; // panel contents, not the card
    float alphaFrom = 0.f;
    double panelT0 = 0.0;
    bool panelRun = false;
    bool panelOpening = false;
    bool panelClockOn = false;
    bool switchRun = false;
    bool switchIn = false;
    int switchPart = 0;
    double switchT0 = 0.0;
    int seenIntensity = -1;
    std::uint32_t seenNoticeSerial = ~std::uint32_t (0);
    VBlankAttachment panelVblank;
    Groove groove;
    uint32_t seenGen = 0;
    bool haveGroove = false;

    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> toneA, punchA, levelA;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> darkA, busyA, bounceA, stabA, swingA, intensityA;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> duckA, inStabA;

    Content (RollaProcessor& p, Look& lf, RollaEditor& ed)
        : proc (p), editor (ed), samples (p)
    {
        setLookAndFeel (&lf);
        setOpaque (true);

        addAndMakeVisible (logo);
        addAndMakeVisible (dialects);
        addAndMakeVisible (intensity);
        addAndMakeVisible (keyDrop);
        addAndMakeVisible (scaleDrop);
        intensityA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "intensity", intensity);
        addAndMakeVisible (bpm);
        addAndMakeVisible (undoBtn);
        addAndMakeVisible (redoBtn);
        logo.onClick = [this] { showScaleMenu (&logo); };
        dialects.onPick = [this] (int i) { setChoiceIndex (proc, "dialect", i); };
        keyDrop.onOpen = [this] { showKeyMenu(); };
        scaleDrop.onOpen = [this] { showScaleModeMenu(); };
        undoBtn.onClick = [this] { proc.undo.undo(); };
        redoBtn.onClick = [this] { proc.undo.redo(); };

        for (int i = 0; i < NumParts; ++i)
        {
            rows[i].configure (proc, i);
            rows[i].onSelect = [this] (int part) { select (part); };
            rows[i].onNudge = [this] (int dir) { nudgeRow (dir); };
            rows[i].strip.groove = &groove;
            addAndMakeVisible (rows[i]);
        }

        tone.caption = "TONE";
        punch.caption = "PUNCH";
        level.caption = "LEVEL";
        tone.slider.getProperties().set ("bipolar", true);
        sound.host = this;
        addAndMakeVisible (sound);
        for (auto* k : { &tone, &punch, &level })
            sound.addAndMakeVisible (k);
        presetStep.onStep = [this] (int dir) { stepPreset (dir); };
        presetStep.onBrowse = [this] { browseLanePresets (presetStep); };
        sound.addAndMakeVisible (presetStep);
        kitStep.onStep = [this] (int dir) { stepPreset (dir); };
        kitStep.onBrowse = [this] { browseLanePresets (kitStep); };
        sound.addAndMakeVisible (kitStep);
        source.onPick = [this] (int mode)
        {
            if (samples.mode (selected) == mode)
                return;
            samples.setMode (selected, mode);
            proc.markEdited (selected);
            sync();
        };
        sound.addAndMakeVisible (source);

        voxSlot.proc = &proc;
        voxSlot.voice = -1;
        voxSlot.onLoaded = [this] { repaint(); };
        sound.addAndMakeVisible (voxSlot);
        inStab.onClick = [this] { proc.markEdited (Hits); };
        sound.addAndMakeVisible (inStab);
        inStabA = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "hitsInStab", inStab);

        const int voices[] = { Kick, Snare, Clap, Rim, HatC, HatO, Ride };
        for (int i = 0; i < 7; ++i)
        {
            drums[i].configure (proc, voices[i]);
            sound.addAndMakeVisible (drums[i]);
        }

        dark.caption = "DARK";
        busy.caption = "BUSY";
        bounce.caption = "BOUNCE";
        stab.caption = "STAB";
        for (auto* k : { &dark, &busy, &bounce, &stab })
        {
            k->macro = true;
            addAndMakeVisible (k);
        }
        darkA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "dark", dark.slider);
        busyA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "busy", busy.slider);
        bounceA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "bounce", bounce.slider);
        stabA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "stab", stab.slider);

        addAndMakeVisible (swing);
        addAndMakeVisible (duck);
        swingA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, "swing", swing);
        duckA = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "duck", duck);

        face.proc = &proc;
        face.onClick = [this]
        {
            if (! allKept())
            {
                face.play();
                proc.triggerRoll();
            }
        };
        addAndMakeVisible (face);

        drag.proc = &proc;
        addAndMakeVisible (drag);
        addAndMakeVisible (status);

        bindHelp (logo, "Menu. Window scale, reduce motion, and the samples folder.");
        bindHelp (dialects, "Style. Each pill sets the groove, the sounds, and which lanes play.");
        bindHelp (keyDrop, "Key. Auto keeps the session key; pick a note to lock it through every roll.");
        bindHelp (scaleDrop, "Scale. Auto plays each style's own minor; lock Minor, Dorian, Phrygian, Major or Mixolydian.");
        bindHelp (intensity, "INTENSITY 75\t" + String (dialectName (0)));
        bindHelp (bpm, "Host tempo, in beats per minute.");
        bindHelp (undoBtn, "Undo the last edit.");
        bindHelp (redoBtn, "Redo the edit you just undid.");
        bindHelp (presetStep, "Preset for this lane. Click the name to see only this lane's presets.");
        bindHelp (kitStep, "Drum kit. Click the name to choose another kit.");
        bindHelp (source, "Source. Synth, a sample, or both layered.");
        bindHelp (tone, "Brightness; the top third adds drive.");
        bindHelp (punch, "From round and lazy to snappy and pumping.");
        bindHelp (level, "This lane's volume.");
        bindHelp (dark, "Sets how dark the bass's second note is: 5, 2, b7, b3, then b2.");
        bindHelp (busy, "Sets how many bass notes play per bar (3 to 7); higher leaves fewer gaps.");
        bindHelp (bounce, "Sets how early in the bar the groove changes, and adds glides on pickups.");
        bindHelp (stab, "Sets how often the stab answers the bass; 0 means bass only.");
        bindHelp (swing, "Swing, from 50 to 57 percent.");
        bindHelp (duck, "Duck. The bass dips under the stab.");
        bindHelp (face, "Roll. Re-rolls every lane that is not kept.");
        bindHelp (drag, "Drag all. One MIDI file, one track per lane. Click the tile, or hold Alt, for a WAV stem of every lane.");
        bindHelp (voxSlot, "Vox sample. Drop a file to override the preset.");
        bindHelp (inStab, "In stab. Hits land on the stab's answer slots.");
        for (int i = 0; i < NumParts; ++i)
            bindHelp (rows[i], String (laneShortName (i)) + ". Click to open this lane. Click again to close it. Mute, shuffle, or keep it from here.");

        groove = proc.copyGroove();
        haveGroove = true;
        seenGen = groove.generation;
        restorePanel();
        // Bass is the selected lane while the panel is closed. Bind it now so
        // the tone, punch, and level knobs follow that lane before it is opened.
        bindSound();
        sync();
    }

    ~Content() override
    {
        panelVblank = VBlankAttachment();
        setLookAndFeel (nullptr);
    }

    bool allKept() const
    {
        for (int i = 0; i < NumParts; ++i)
            if (paramRaw (proc.apvts, String (partId (i)) + "Keep") < 0.5f)
                return false;
        return true;
    }

    void showStatus (const String& text)
    {
        status.setHelp (text);
    }

    void notifyRoll()
    {
        if (! allKept())
            face.play();
    }

    void showScaleMenu (Component* target)
    {
        PopupMenu menu;
        const float scale = editor.getWidth() / 1024.f;
        struct ScaleChoice { int id; const char* label; float scale; };
        const ScaleChoice choices[] = {
            { 1, "75%", 0.75f },
            { 2, "100%", 1.f },
            { 3, "125%", 1.25f },
            { 4, "150%", 1.5f },
            { 5, "200%", 2.f },
        };
        for (const auto& choice : choices)
            menu.addItem (choice.id, choice.label, true, std::abs (scale - choice.scale) < 0.02f);
        menu.addSeparator();
        menu.addItem (6, "Reduce motion", true, paramRaw (proc.apvts, "reduceMotion") >= 0.5f);
        menu.addSeparator();
        menu.addItem (7, utf8 ("Samples folder…"));
        const auto status = proc.samplesStatus();
        menu.addItem (8, status.root.getFullPathName() + (status.bundled ? "  (built in)" : "  (not built in)"), false, false);
        menu.addItem (9, proc.sampleInventoryText(), false, false);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (target != nullptr ? target : &logo),
                            [this] (int result)
                            {
                                if (result == 1) editor.setEditorScale (0.75f);
                                else if (result == 2) editor.setEditorScale (1.f);
                                else if (result == 3) editor.setEditorScale (1.25f);
                                else if (result == 4) editor.setEditorScale (1.5f);
                                else if (result == 5) editor.setEditorScale (2.f);
                                else if (result == 6)
                                    setBool (proc, "reduceMotion", paramRaw (proc.apvts, "reduceMotion") < 0.5f);
                                else if (result == 7)
                                    chooseSamplesFolder();
                            });
    }

    // KEY dropdown: Auto (the session key, unlocked) or a locked note.
    void showKeyMenu()
    {
        PopupMenu menu;
        const bool lockedNow = paramRaw (proc.apvts, "keyLock") >= 0.5f;
        const int current = choiceIndex (proc.apvts, "key");
        menu.addItem (1, String ("Auto (") + keyName (current) + ")", true, ! lockedNow);
        menu.addSeparator();
        for (int k = 0; k < 12; ++k)
            menu.addItem (k + 2, keyName (k), true, lockedNow && k == current);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&keyDrop),
                            [this] (int result)
                            {
                                if (result == 1)
                                    setBool (proc, "keyLock", false);
                                else if (result >= 2 && result <= 13)
                                {
                                    setChoiceIndex (proc, "key", result - 2);
                                    if (paramRaw (proc.apvts, "keyLock") < 0.5f)
                                        setBool (proc, "keyLock", true);
                                }
                            });
    }

    // SCALE dropdown: Auto or one of the five locked scales.
    void showScaleModeMenu()
    {
        PopupMenu menu;
        const int current = choiceIndex (proc.apvts, "scale");
        menu.addItem (1, "Auto (style minor)", true, current == ScaleAuto);
        menu.addSeparator();
        for (int s = ScaleMinor; s < NumScales; ++s)
            menu.addItem (s + 1, scaleName (s), true, s == current);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&scaleDrop),
                            [this] (int result)
                            {
                                if (result >= 1 && result <= NumScales)
                                    setChoiceIndex (proc, "scale", result - 1);
                            });
    }

    void chooseSamplesFolder()
    {
        sampleChooser = std::make_unique<FileChooser> ("ROLLA samples", proc.samplesRoot(), String(), true);
        sampleChooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                     [this] (const FileChooser& fc)
                                     {
                                         const auto dir = fc.getResult();
                                         if (dir != File())
                                             proc.setSamplesRoot (dir);
                                     });
    }

    bool soundIsOpen() const noexcept { return panelTarget > 0.5f; }

    void showLane (int part)
    {
        selected = juce::jlimit (0, NumParts - 1, part);
        setOpen (true, false);
    }

    void closeSound()
    {
        if (soundIsOpen())
            setOpen (false, true);
    }

    void restorePanel()
    {
        setOpen (false, false);
    }

    void clearSelection()
    {
        for (int i = 0; i < NumParts; ++i)
        {
            if (! rows[i].selected)
                continue;
            rows[i].selected = false;
            rows[i].repaint();
        }
    }

    void markSelection()
    {
        for (int i = 0; i < NumParts; ++i)
        {
            const bool on = i == selected;
            if (rows[i].selected == on)
                continue;
            rows[i].selected = on;
            rows[i].repaint();
        }
    }

    bool motionReduced() const
    {
        return paramRaw (proc.apvts, "reduceMotion") >= 0.5f;
    }

    void select (int part)
    {
        if (soundIsOpen() && part == selected && ! switchRun)
        {
            setOpen (false, true);
            return;
        }
        if (! soundIsOpen())
        {
            selected = part;
            setOpen (true, true);
            return;
        }
        beginSwitch (part);
    }

    void nudgeRow (int dir)
    {
        int cur = -1;
        for (int i = 0; i < NumParts; ++i)
            if (rows[i].hasKeyboardFocus (true))
                cur = i;
        if (cur < 0)
            cur = soundIsOpen() ? selected : 0;
        const int next = jlimit (0, NumParts - 1, cur + dir);
        rows[next].grabKeyboardFocus();
        if (soundIsOpen() && next != selected)
            beginSwitch (next);
    }

    void beginSwitch (int part)
    {
        if (part == selected && ! switchRun)
            return;
        if (motionReduced() || panelRun)
        {
            switchRun = false;
            selected = part;
            markSelection();
            if (! panelRun)
                contentAlpha = 1.f;
            source.setCurrent (samples.mode (selected));
            bindSound();
            repaint();
            return;
        }
        switchPart = part;
        if (! switchRun || switchIn)
        {
            alphaFrom = contentAlpha;
            switchIn = false;
            switchT0 = Time::getMillisecondCounterHiRes();
            switchRun = true;
            armPanelClock();
        }
    }

    void setOpen (bool open, bool animate)
    {
        switchRun = false;
        if (open)
            markSelection();

        const float target = open ? 1.f : 0.f;
        const bool reduced = motionReduced();
        panelTarget = target;
        if (! animate || reduced || std::abs (panelAmt - target) < 0.001f)
        {
            panelAmt = target;
            contentAlpha = target;
            panelRun = false;
            if (! open)
                clearSelection();
            disarmPanelClock();
        }
        else
        {
            panelFrom = panelAmt;
            alphaFrom = contentAlpha;
            panelOpening = open;
            panelT0 = Time::getMillisecondCounterHiRes();
            panelRun = true;
            armPanelClock();
        }
        source.setCurrent (samples.mode (selected));
        if (open)
            bindSound();
        else
            resized();
        repaint();
    }

    void armPanelClock()
    {
        if (getPeer() == nullptr)
        {
            if (switchRun)
            {
                selected = switchPart;
                switchRun = false;
                markSelection();
                bindSound();
            }
            panelAmt = panelTarget;
            contentAlpha = panelTarget;
            panelRun = false;
            panelClockOn = false;
            if (panelTarget < 0.5f)
                clearSelection();
            return;
        }
        if (panelClockOn && ! panelVblank.isEmpty())
            return;
        panelClockOn = true;
        if (panelVblank.isEmpty())
            panelVblank = VBlankAttachment (this, [this] (double) { advancePanel(); });
    }

    void disarmPanelClock()
    {
        panelClockOn = false;
        if (panelVblank.isEmpty())
            return;
        if (auto* mm = MessageManager::getInstanceWithoutCreating())
        {
            Component::SafePointer<Content> alive (this);
            mm->callAsync ([alive]
            {
                if (alive != nullptr && ! alive->panelClockOn)
                    alive->panelVblank = VBlankAttachment();
            });
        }
        else
        {
            panelVblank = VBlankAttachment();
        }
    }

    void advanceSwitch()
    {
        const double elapsed = Time::getMillisecondCounterHiRes() - switchT0;
        if (! switchIn)
        {
            contentAlpha = jmax (0.f, alphaFrom - (float) (elapsed / 40.0));
            if (elapsed >= 40.0)
            {
                selected = switchPart;
                contentAlpha = 0.f;
                switchIn = true;
                switchT0 = Time::getMillisecondCounterHiRes();
                markSelection();
                source.setCurrent (samples.mode (selected));
                bindSound();
            }
        }
        else
        {
            contentAlpha = jmin (1.f, (float) (elapsed / 40.0));
            if (elapsed >= 40.0)
            {
                contentAlpha = 1.f;
                switchRun = false;
            }
        }
        applyContentFade();
        repaint();
    }

    void advancePanel()
    {
        if (! panelRun && ! switchRun)
        {
            disarmPanelClock();
            return;
        }
        if (switchRun)
            advanceSwitch();
        if (! panelRun)
            return;
        const double elapsed = Time::getMillisecondCounterHiRes() - panelT0;
        const double dur = panelOpening ? 180.0 : 140.0;
        const float u = (float) (elapsed / dur);
        if (u >= 1.f)
        {
            panelAmt = panelTarget;
            if (! switchRun)
                contentAlpha = panelTarget;
            panelRun = false;
            if (panelTarget < 0.5f)
                clearSelection();
            if (! switchRun)
                disarmPanelClock();
        }
        else
        {
            // Open: ease-out cubic(0.2, 0, 0, 1). Close: that curve reversed.
            const float eased = panelOpening ? bezierEase (u, 0.2f, 0.f, 0.f, 1.f)
                                             : bezierEase (u, 1.f, 0.f, 0.8f, 1.f);
            panelAmt = panelFrom + (panelTarget - panelFrom) * eased;
            if (! switchRun)
            {
                if (panelOpening)
                {
                    const double fadeDur = (1.0 - (double) alphaFrom) * 100.0;
                    const double fadeStart = 180.0 - fadeDur;
                    contentAlpha = elapsed <= fadeStart
                                     ? alphaFrom
                                     : jlimit (0.f, 1.f, alphaFrom + (float) ((elapsed - fadeStart) / 100.0));
                }
                else
                {
                    contentAlpha = jmax (0.f, alphaFrom - (float) (elapsed / 78.0));
                }
            }
        }
        resized();
        repaint();
    }

    void layoutLeft()
    {
        constexpr float kTop = 64.f;
        const float H = 80.f - 40.f * panelAmt;
        for (int i = 0; i < NumParts; ++i)
        {
            const int y0 = roundToInt (kTop + H * (float) i);
            const int y1 = roundToInt (kTop + H * (float) (i + 1));
            rows[i].setBounds (16, y0, 640, jmax (1, y1 - y0));
        }
        const int panelY = roundToInt (kTop + 7.f * H + 8.f * panelAmt);
        const int panelH = jmax (0, 624 - panelY);
        sound.setBounds (16, panelY, 640, panelH);
        sound.setVisible (panelH > 2);
        applyContentFade();
    }

    void applyContentFade()
    {
        const float a = contentAlpha;
        const bool live = a > 0.99f;
        presetStep.setAlpha (a);
        tone.setAlpha (a);
        punch.setAlpha (a);
        level.setAlpha (a);
        voxSlot.setAlpha (a);
        inStab.setAlpha (a);
        kitStep.setAlpha (a);
        source.setAlpha (a);
        for (auto& col : drums)
            col.setAlpha (a);
        sound.setInterceptsMouseClicks (live, live);
    }

    void placeSound()
    {
        presetStep.setBounds (16, 112, 256, 40);
        // 80 wide so "−12.0 dB" (8 mono glyphs at 12 pt, ~62 px) has room; knob centres stay 344 / 440 / 536.
        tone.setBounds (304, 88, 80, 112);
        punch.setBounds (400, 88, 80, 112);
        level.setBounds (496, 88, 80, 112);
        voxSlot.setBounds (16, 192, 240, 56);
        inStab.setBounds (184, 200, 40, Theme::heightSmall);
        kitStep.setBounds (248, 8, 176, 32);
        source.setBounds (440, 8, 184, 32);
        for (int i = 0; i < 7; ++i)
            drums[i].setBounds (16 + 88 * i, 64, 80, 188);
    }

    void stepPreset (int dir)
    {
        const auto names = presetsForLane (*proc.sharePresets(), selected);
        const auto id = String (partId (selected)) + "Preset";
        if (auto* p = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (id)))
        {
            const int next = p->getIndex() + dir;
            if (next < 0 || next >= names.size())
                return;
            setChoiceIndex (proc, id, next);
            proc.markEdited (selected);
        }
    }

    void browseLanePresets (Stepper& step)
    {
        const int part = selected;
        const auto names = presetsForLane (*proc.sharePresets(), part);
        const auto id = String (partId (part)) + "Preset";
        auto* param = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (id));
        if (param == nullptr || names.isEmpty())
            return;
        PopupMenu menu;
        const int current = jlimit (0, names.size() - 1, param->getIndex());
        for (int i = 0; i < names.size(); ++i)
            menu.addItem (i + 1, names[i], true, i == current);
        menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&step),
                            [this, id, part, count = names.size()] (int result)
                            {
                                if (result <= 0 || result > count)
                                    return;
                                setChoiceIndex (proc, id, result - 1);
                                proc.markEdited (part);
                            });
    }

    void bindSound()
    {
        toneA.reset();
        punchA.reset();
        levelA.reset();
        const auto id = String (partId (selected));
        toneA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id + "Tone", tone.slider);
        punchA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id + "Punch", punch.slider);
        levelA = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, id + "Level", level.slider);
        auto edited = [this] { proc.markEdited (selected); };
        tone.slider.onDragEnd = edited;
        punch.slider.onDragEnd = edited;
        level.slider.onDragEnd = edited;

        const bool drumsOn = selected == Drums;
        presetStep.setVisible (! drumsOn);
        tone.setVisible (! drumsOn);
        punch.setVisible (! drumsOn);
        level.setVisible (! drumsOn);
        voxSlot.setVisible (selected == Vox);
        inStab.setVisible (selected == Hits);
        kitStep.setVisible (drumsOn);
        for (auto& col : drums)
            col.setVisible (drumsOn);
        resized();
    }

    void sync()
    {
        samples.reconcile();
        auto next = proc.copyGroove();
        const bool playing = proc.isHostOrPreviewPlaying();
        const double pos16 = proc.getPlayPpq() * 4.0;
        const float swingNow = paramRaw (proc.apvts, "swing");
        const bool genChanged = ! haveGroove || next.generation != seenGen;
        const int intensityNow = roundToInt (paramRaw (proc.apvts, "intensity"));
        const bool intensityChanged = intensityNow != seenIntensity;
        seenIntensity = intensityNow;
        const int dialectNow = choiceIndex (proc.apvts, "dialect");
        const float stabNow = paramRaw (proc.apvts, "stab");
        const auto plan = planIntensity (dialectNow, intensityNow, stabNow);
        groove = std::move (next);
        haveGroove = true;
        seenGen = groove.generation;
        intensity.word = plan.word;
        for (int i = 0; i < NumParts; ++i)
        {
            rows[i].strip.swing = swingNow;
            rows[i].strip.playing = playing;
            rows[i].strip.pos16 = pos16;
            rows[i].syncHover();
            const bool muted = rows[i].mute.getToggleState();
            const bool kept = paramRaw (proc.apvts, String (partId (i)) + "Keep") >= 0.5f;
            const bool sampled = i < Drums && proc.userSlotMode (i) != 0;
            const bool inLane = intensityLaneIn (plan, i, kept, sampled);
            const bool out = ! inLane;
            const int comes = plan.comesInAt[i] < 0 ? 0 : plan.comesInAt[i];
            if (out != rows[i].laneOut || comes != rows[i].comesIn)
            {
                rows[i].laneOut = out;
                rows[i].comesIn = comes;
                rows[i].repaint();
            }
            rows[i].strip.setVisible (! out);
            rows[i].strip.setAlpha (muted ? Theme::alphaMuted : 1.f);
            rows[i].shuffle.setEnabled (! out);
            rows[i].shuffle.setAlpha (out ? Theme::alphaDisabled : 1.f);
            const auto tip = String (rows[i].selected ? "Close " : "Open ")
                           + laneShortName (i) + " sound";
            if (rows[i].getProperties()["rollaHelp"].toString() != tip)
                bindHelp (rows[i], tip);
            const bool miss = samples.missing (i);
            if (miss != rows[i].sampleMissing)
            {
                rows[i].sampleMissing = miss;
                rows[i].repaint();
            }
            if (genChanged || playing || intensityChanged)
                rows[i].strip.repaint();
            rows[i].keep.repaint();
        }
        if (intensityChanged)
            intensity.repaint();
        source.setCurrent (samples.mode (selected));

        bpm.setBpm (proc.getPlayBpm() > 1.f ? proc.getPlayBpm() : 130.f);
        dialects.setCurrent (choiceIndex (proc.apvts, "dialect"));
        for (int i = 0; i < NumDialects; ++i)
            dialects.buttons[i].repaint();
        {
            const String keyTitle = keyName (choiceIndex (proc.apvts, "key"));
            const String keySub = paramRaw (proc.apvts, "keyLock") >= 0.5f ? "KEY" : "AUTO";
            const int scaleNow = choiceIndex (proc.apvts, "scale");
            // Auto shows what plays: the style's own minor.
            const String scaleTitle = scaleShort (scaleNow == ScaleAuto ? ScaleMinor : scaleNow);
            const String scaleSub = scaleNow == ScaleAuto ? "AUTO" : "SCALE";
            if (keyDrop.title != keyTitle || keyDrop.sub != keySub)
            {
                keyDrop.title = keyTitle;
                keyDrop.sub = keySub;
                keyDrop.repaint();
            }
            if (scaleDrop.title != scaleTitle || scaleDrop.sub != scaleSub)
            {
                scaleDrop.title = scaleTitle;
                scaleDrop.sub = scaleSub;
                scaleDrop.repaint();
            }
        }

        const bool canUndo = proc.undo.canUndo();
        const bool canRedo = proc.undo.canRedo();
        undoBtn.setEnabled (canUndo);
        redoBtn.setEnabled (canRedo);
        undoBtn.setAlpha (canUndo ? 1.f : Theme::alphaDisabled);
        redoBtn.setAlpha (canRedo ? 1.f : Theme::alphaDisabled);

        if (auto* preset = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (String (partId (selected)) + "Preset")))
        {
            const auto names = presetsForLane (*proc.sharePresets(), selected);
            const int index = names.isEmpty() ? 0 : jlimit (0, names.size() - 1, preset->getIndex());
            const auto name = names.isEmpty() ? String() : names[index];
            const auto sub = String (index + 1) + " / " + String (names.size())
                           + utf8 (" · ") + String (dialectName (choiceIndex (proc.apvts, "dialect")));
            presetStep.title = name;
            presetStep.sub = sub;
            presetStep.strike = selected == Vox && proc.hasVoxSample();
            presetStep.setEnds (index <= 0, index >= names.size() - 1);
            presetStep.repaint();
            kitStep.title = name;
            kitStep.sub = {};
            kitStep.setEnds (index <= 0, index >= names.size() - 1);
            kitStep.repaint();
        }

        tone.setReadout (String (tone.slider.getValue() * 10.0, 1));
        punch.setReadout (String (punch.slider.getValue() * 10.0, 1));
        auto db = String (level.slider.getValue(), 1);
        if (db.startsWithChar ('-'))
            db = String::charToString (juce::juce_wchar (0x2212)) + db.substring (1);
        level.setReadout (db + " dB");
        dark.setReadout (String (dark.slider.getValue() * 10.0, 1), darkCaption ((float) dark.slider.getValue()));
        busy.setReadout (String (busy.slider.getValue() * 10.0, 1), busyCaption ((float) busy.slider.getValue()));
        bounce.setReadout (String (bounce.slider.getValue() * 10.0, 1), bounceCaption ((float) bounce.slider.getValue()));
        stab.setReadout (String (stab.slider.getValue() * 10.0, 1), stabCaption ((float) stab.slider.getValue()));
        for (auto& col : drums)
            if (col.isVisible())
                col.sync();

        int rolling = 0;
        StringArray keptNames, mutedNames, sampleNames;
        int tracks = 0;
        for (int i = 0; i < NumParts; ++i)
        {
            const bool k = paramRaw (proc.apvts, String (partId (i)) + "Keep") >= 0.5f;
            const bool m = paramRaw (proc.apvts, String (partId (i)) + "Mute") >= 0.5f;
            const bool sampled = i < Drums && proc.userSlotMode (i) != 0;
            const bool inLane = intensityLaneIn (plan, i, k, sampled);
            if (k)
                keptNames.add (laneShortName (i));
            if (m)
                mutedNames.add (laneShortName (i));
            if (sampled)
                sampleNames.add (laneShortName (i));
            if (! k && ! m && inLane)
                ++rolling;
            if (! m && inLane)
                ++tracks;
        }
        {
            String fixed;
            if (keptNames.isEmpty() && mutedNames.isEmpty() && sampleNames.isEmpty())
                fixed = "kept, muted and samples stay as they are";
            else
            {
                if (! keptNames.isEmpty())
                    fixed << "kept " << keptNames.joinIntoString (" ");
                if (! mutedNames.isEmpty())
                    fixed << (fixed.isEmpty() ? "" : utf8 (" · ")) << "muted " << mutedNames.joinIntoString (" ");
                if (! sampleNames.isEmpty())
                    fixed << (fixed.isEmpty() ? "" : utf8 (" · ")) << "sample " << sampleNames.joinIntoString (" ");
            }
            const auto tip = "INTENSITY " + String (intensityNow) + "\t"
                           + String (plan.word) + utf8 (" · ") + dialectName (dialectNow) + "\n"
                           + "LANES IN\t" + String (plan.lanesIn) + "\n"
                           + "NEXT\t" + String (plan.nextIn) + "\n"
                           + "ARRANGE\t" + String (plan.arrange) + "\n"
                           + "POLY\t" + String (plan.poly) + "\n"
                           + "FIXED\t" + fixed + "\n"
                           + "\tBUSY and STAB still set notes and answers.";
            if (intensity.getProperties()["rollaHelp"].toString() != tip)
                bindHelp (intensity, tip);
        }
        if (allKept())
            face.setCaption ("all kept", {}, {}, true);
        else
        {
            const auto count = String (rolling) + (rolling == 1 ? " lane" : " lanes");
            const auto tail = keptNames.isEmpty() ? String() : utf8 ("  ·  ") + keptNames.joinIntoString (" + ") + " kept";
            const auto tailShort = keptNames.isEmpty() ? String() : utf8 ("  ·  ") + String (keptNames.size()) + " kept";
            face.setCaption (count, tail, tailShort, false);
        }
        {
            const auto serial = proc.getUiNoticeSerial();
            if (serial != seenNoticeSerial)
            {
                seenNoticeSerial = serial;
                status.setNotice (proc.getUiNotice());
            }
        }
        const auto unit = drag.audioMode ? (tracks == 1 ? utf8 (" stem · ") : utf8 (" stems · "))
                                         : (tracks == 1 ? utf8 (" track · ") : utf8 (" tracks · "));
        // Exports follow LENGTH (what the engine loops), not the groove's own length.
        const int exportBars = barsFromIndex ((int) proc.apvts.getRawParameterValue ("length")->load());
        drag.caption = String (tracks) + unit
                     + String (exportBars) + (exportBars == 1 ? " bar" : " bars");
        drag.repaint();
        voxSlot.repaint();
        repaint();
    }

    void paint (Graphics& g) override
    {
        g.fillAll (col (kPaper));
        g.setColour (col (kLine));
        g.fillRect (16, 55, 992, 1);

        auto card = [&] (Rectangle<float> r)
        {
            g.setColour (col (kCard));
            g.fillRoundedRectangle (r, Theme::radiusPanel);
            g.setColour (col (kLine));
            g.drawRoundedRectangle (r.reduced (0.5f), Theme::radiusPanel, 1.f);
        };
        const float laneBottom = rows[NumParts - 1].getBottom() > 64
                                    ? (float) rows[NumParts - 1].getBottom()
                                    : 624.f;
        card ({ 16.f, 64.f, 640.f, laneBottom - 64.f });
        card ({ 672.f, 200.f, 336.f, 272.f });
        card ({ 672.f, 480.f, 336.f, 48.f });

        g.setColour (col (kInk2));
        drawLabel (g, "SWING", Theme::Role::caption, Rectangle<int> (688, 492, 52, 24), Justification::centredLeft);
        g.setColour (col (kInk));
        drawLabel (g, String (roundToInt (paramRaw (proc.apvts, "swing") * 100.f)) + "%", Theme::Role::value,
                   Rectangle<int> (828, 492, 48, 24), Justification::centredLeft);
        g.setColour (col (kLine));
        g.fillRect (888, 492, 1, 24);
        g.setColour (col (kInk2));
        drawLabel (g, "DUCK", Theme::Role::caption, Rectangle<int> (904, 492, 48, 24), Justification::centredLeft);
    }

    void resized() override
    {
        {
            // Header: one 40 px row at y 8, 16 px outer margins, 8 px between controls
            // (16 between undo and redo). Left to right: 16 logo 104 style 496 · 504 intensity 680
            // · 688 KEY 752 · 760 SCALE 824 · 832 BPM 904 · 912 undo 952 · 968 redo 1008.
            static_assert (2 * Theme::dropKey + Theme::space8 == Theme::stepperKey,
                           "KEY + SCALE fill the old key stepper slot");
            const int y = Theme::space8;
            const int h = Theme::heightControl;
            int x = Theme::space16;
            logo.setBounds (x, Theme::space16, Theme::logoWidth, Theme::space24);
            x = logo.getRight() + Theme::space8;
            dialects.setBounds (x, y, Theme::segmentLarge * NumDialects + Theme::space8, h);
            x = dialects.getRight() + Theme::space8;
            intensity.setBounds (x, y, Theme::stepperKit, h);
            x = intensity.getRight() + Theme::space8;
            keyDrop.setBounds (x, y, Theme::dropKey, h);
            x = keyDrop.getRight() + Theme::space8;
            scaleDrop.setBounds (x, y, Theme::dropKey, h);

            int right = getWidth() - Theme::space16;
            redoBtn.setBounds (right - Theme::iconLarge, y, Theme::iconLarge, Theme::iconLarge);
            right = redoBtn.getX() - Theme::space16;
            undoBtn.setBounds (right - Theme::iconLarge, y, Theme::iconLarge, Theme::iconLarge);
            right = undoBtn.getX() - Theme::space8;
            bpm.setBounds (right - Theme::bpmWidth, y + (h - Theme::space16) / 2, Theme::bpmWidth, Theme::space16);
            jassert (bpm.getX() >= scaleDrop.getRight() + Theme::space8);
        }

        layoutLeft();
        placeSound();

        face.setBounds (672, 64, 336, 128);
        // Macro columns are 136 wide (caption and descriptor width); knobs stay centred at x 756 / 924.
        dark.setBounds (688, 204, 136, 128);
        busy.setBounds (856, 204, 136, 128);
        bounce.setBounds (688, 340, 136, 128);
        stab.setBounds (856, 340, 136, 128);
        swing.setBounds (744, 492, 80, 24);
        duck.setBounds (956, 492, 40, Theme::heightSmall);
        drag.setBounds (672, 536, 336, 88);
        status.setBounds (Theme::space16, 624, 992, Theme::space16);
    }

    void paintSound (Graphics& g)
    {
        auto b = sound.getLocalBounds().toFloat();
        g.setColour (col (kCard));
        g.fillRoundedRectangle (b, Theme::radiusPanel);
        g.setColour (col (kLine));
        g.drawRoundedRectangle (b.reduced (0.5f), Theme::radiusPanel, 1.f);

        g.saveState();
        g.setOpacity (contentAlpha);
        g.setColour (col (kLine));
        g.fillRect (0, 47, 640, 1);

        g.setColour (laneColour (selected));
        g.fillEllipse (16.f, 19.f, 10.f, 10.f);
        g.setColour (col (kInk));
        const auto panelName = String (laneShortName (selected));
        drawLabel (g, panelName, Theme::Role::section, Rectangle<int> (32, 12, 120, 24), Justification::centredLeft);
        if (samples.missing (selected))
        {
            float x = 32.f + textWidth (Theme::Role::section, panelName) + 6.f;
            if (selected == Drums && x > 96.f)
                x = 96.f;
            paintMissingIcon (g, x, 18.f, 12.f);
        }
        if (selected != Drums)
        {
            g.setColour (col (kInk2));
            drawLabel (g, channelLine (selected), Theme::Role::meta, Rectangle<int> (152, 12, 160, 24), Justification::centredLeft);
        }
        if (proc.laneEdited (selected))
        {
            g.setColour (col (kMark));
            drawLabel (g, "EDITED", Theme::Role::micro,
                       selected == Drums ? Rectangle<int> (112, 12, 120, 24) : Rectangle<int> (320, 12, 112, 24),
                       Justification::centredLeft);
        }

        if (selected != Drums)
        {
            g.setColour (col (kInk2));
            drawLabel (g, "PRESET", Theme::Role::caption, Rectangle<int> (16, 88, 80, 16), Justification::centredLeft);
            if (selected == Vox)
                drawLabel (g, "SAMPLE", Theme::Role::caption, Rectangle<int> (16, 168, 80, 16), Justification::centredLeft);
            if (selected == Hits)
            {
                drawLabel (g, "PLACEMENT", Theme::Role::caption, Rectangle<int> (16, 168, 100, 16), Justification::centredLeft);
                g.setColour (col (kInk));
                // Left edge on the 16 px panel padding, like the PLACEMENT caption; stops 8 px before the switch.
                drawLabel (g, "In stab", Theme::Role::pill,
                           Rectangle<int> (16, 192, inStab.getX() - Theme::space8 - 16, 40), Justification::centredLeft);
                g.setColour (col (kInk2));
                drawLabel (g, "Hits land on the stab's answer slots.", Theme::Role::meta,
                           Rectangle<int> (16, 240, 280, 16), Justification::centredLeft);
            }
        }
        g.restoreState();
    }
};

void RollaEditor::Content::SoundBody::paint (Graphics& g)
{
    if (host != nullptr)
        host->paintSound (g);
}

struct RollaEditor::SizeConstrainer : juce::ComponentBoundsConstrainer
{
};

namespace
{
constexpr int kEditorBaseW = 1024;
constexpr int kEditorBaseH = 640;

juce::Point<int> fittedEditorSize (int width, int height)
{
    width = juce::jlimit (768, 2048, width);
    height = juce::jlimit (480, 1280, height);
    const double aspect = (double) kEditorBaseW / (double) kEditorBaseH;
    const int h = juce::jlimit (480, 1280, juce::roundToInt ((double) width / aspect));
    const int w = juce::jlimit (768, 2048, juce::roundToInt ((double) h * aspect));
    return { w, juce::jlimit (480, 1280, juce::roundToInt ((double) w / aspect)) };
}
} // namespace

RollaEditor::RollaEditor (RollaProcessor& p)
    : AudioProcessorEditor (p),
      proc (p)
{
    look = std::make_unique<Look>();
    setLookAndFeel (look.get());
    editorSettings = settings::open();
    constrainer = std::make_unique<SizeConstrainer>();
    constrainer->setFixedAspectRatio ((double) kEditorBaseW / (double) kEditorBaseH);
    constrainer->setSizeLimits (768, 480, 2048, 1280);
    setConstrainer (constrainer.get());
    setResizable (true, true);
    content = std::make_unique<Content> (proc, static_cast<Look&> (*look), *this);
    addAndMakeVisible (content.get());
    samplesBanner = std::make_unique<Label>();
    samplesBanner->setColour (Label::backgroundColourId, col (kWarn));
    samplesBanner->setColour (Label::textColourId, col (kPaper));
    samplesBanner->setJustificationType (Justification::centred);
    // A long sample path ends in an ellipsis instead of squashed glyphs.
    samplesBanner->setMinimumHorizontalScale (1.f);
    samplesBanner->setInterceptsMouseClicks (false, false);
    addChildComponent (samplesBanner.get());
    setOpaque (true);
    appliedProjectSerial = proc.getProjectEditorSerial();
    const auto size = initialSize();
    setSize (size.x, size.y);
    rememberSize();
    sizeDirty = false;
    refreshSamplesBanner();
    if (resizableCorner != nullptr)
        resizableCorner->toFront (false);
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);
    struct HelpWindow : TooltipWindow
    {
        using TooltipWindow::TooltipWindow;
        String getTipFor (Component& c) override
        {
            if (ModifierKeys::getCurrentModifiers().isAnyMouseButtonDown())
                return {};
            const auto text = helpUnder (&c);
            return text.isNotEmpty() ? text : TooltipWindow::getTipFor (c);
        }
    };
    tooltip = std::make_unique<HelpWindow> (this, 600);
    tooltip->setLookAndFeel (look.get());
    proc.apvts.addParameterListener ("roll", this);
    startTimerHz (30);
}

RollaEditor::~RollaEditor()
{
    proc.apvts.removeParameterListener ("roll", this);
    removeMouseListener (this);
    if (sizeDirty)
        rememberSize();
    setConstrainer (nullptr);
    resizableCorner.reset();
    content.reset();
    setLookAndFeel (nullptr);
    look.reset();
}

void RollaEditor::paint (Graphics& g)
{
    g.fillAll (col (kPaper));
}

juce::Point<int> RollaEditor::initialSize() const
{
    const int savedW = proc.getEditorWidth();
    const int savedH = proc.getEditorHeight();
    if (savedW >= 768 && savedH >= 480)
        return fittedEditorSize (savedW, savedH);

    if (editorSettings != nullptr)
    {
        const int w = editorSettings->getIntValue ("editorWidth", 0);
        const int h = editorSettings->getIntValue ("editorHeight", 0);
        if (w >= 768 && h >= 480)
            return fittedEditorSize (w, h);
    }

    int userHeight = 1200;
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    if (const auto* display = displays.getPrimaryDisplay())
        userHeight = display->userBounds.getHeight();
    if (userHeight > 0 && userHeight < 1100)
        return { 768, 480 };
    return { kEditorBaseW, kEditorBaseH };
}

void RollaEditor::rememberSize()
{
    const int w = getWidth();
    const int h = getHeight();
    if (w < 768 || h < 480)
        return;
    proc.setEditorSize (w, h);
    if (editorSettings == nullptr)
        return;
    editorSettings->setValue ("editorWidth", w);
    editorSettings->setValue ("editorHeight", h);
    editorSettings->saveIfNeeded();
}

void RollaEditor::resized()
{
    if (content == nullptr)
        return;
    // Design space stays 1024×640. The scale is a graphics transform, so paths and
    // glyph outlines rasterise at the window size instead of a stretched bitmap.
    content->setBounds (0, 0, kEditorBaseW, kEditorBaseH);
    content->setTransform (AffineTransform::scale ((float) getWidth() / (float) kEditorBaseW));
    if (samplesBanner != nullptr)
    {
        const float scale = (float) getWidth() / (float) kEditorBaseW;
        samplesBanner->setBounds (0, 0, getWidth(), roundToInt (22.f * scale));
        // Outside the scaled content, so scale the meta token by hand (10 pt in design space).
        samplesBanner->setFont (Theme::font (Theme::Role::meta).withPointHeight (Theme::pointSize (Theme::Role::meta) * scale));
    }
    if (getWidth() >= 768)
        sizeDirty = true;
}

void RollaEditor::selectLane (int part)
{
    if (content != nullptr)
        content->showLane (part);
}

void RollaEditor::setEditorScale (float scale)
{
    const auto size = fittedEditorSize (roundToInt ((float) kEditorBaseW * scale),
                                        roundToInt ((float) kEditorBaseH * scale));
    setSize (size.x, size.y);
    rememberSize();
    sizeDirty = false;
}

bool RollaEditor::everyLaneKept() const
{
    return content != nullptr && content->allKept();
}

bool RollaEditor::keyPressed (const KeyPress& key)
{
    if (key == KeyPress::escapeKey && hasKeyboardFocus (true)
        && content != nullptr && content->soundIsOpen())
    {
        content->closeSound();
        return true;
    }
    if (key == KeyPress::spaceKey && hasKeyboardFocus (true))
    {
        if (! everyLaneKept())
            proc.triggerRoll();
        return true;
    }
    if ((key == KeyPress::upKey || key == KeyPress::downKey) && hasKeyboardFocus (true) && content != nullptr)
    {
        content->nudgeRow (key == KeyPress::downKey ? 1 : -1);
        return true;
    }
    return false;
}

void RollaEditor::mouseDown (const MouseEvent& e)
{
    grabKeyboardFocus();
    if (e.mods.isPopupMenu() && content != nullptr)
    {
        const auto* hit = e.eventComponent;
        const bool chrome = hit == this || hit == content.get() || hit == &content->logo;
        if (chrome)
        {
            content->showScaleMenu (const_cast<Component*> (hit));
            return;
        }
    }
}

void RollaEditor::mouseMove (const MouseEvent& e)
{
    if (content != nullptr)
        content->showStatus (helpUnder (e.eventComponent));
}

void RollaEditor::parameterChanged (const String& parameterID, float newValue)
{
    if (parameterID == "roll" && newValue >= 0.5f)
        rollPulse.store (true);
}

void RollaEditor::timerCallback()
{
    if (content == nullptr)
        return;

    const auto serial = proc.getProjectEditorSerial();
    if (serial != appliedProjectSerial)
    {
        appliedProjectSerial = serial;
        content->restorePanel();
        const int w = proc.getEditorWidth();
        const int h = proc.getEditorHeight();
        if (w >= 768 && h >= 480 && (w != getWidth() || h != getHeight()))
        {
            const auto size = fittedEditorSize (w, h);
            setSize (size.x, size.y);
        }
    }

    if (sizeDirty && ! juce::ModifierKeys::currentModifiers.isAnyMouseButtonDown())
    {
        sizeDirty = false;
        rememberSize();
    }

    if (rollPulse.exchange (false))
        content->notifyRoll();
    content->sync();

    if (++samplesBannerTick >= 15)
    {
        samplesBannerTick = 0;
        refreshSamplesBanner();
    }
}

void RollaEditor::refreshSamplesBanner()
{
    if (samplesBanner == nullptr)
        return;
    const auto status = proc.samplesStatus();
    if (status.message != samplesBanner->getText())
        samplesBanner->setText (status.message, dontSendNotification);
    const bool show = status.message.isNotEmpty();
    if (show != samplesBanner->isVisible())
    {
        samplesBanner->setVisible (show);
        if (show)
            samplesBanner->toFront (false);
    }
}

} // namespace rolla
