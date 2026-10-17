#include "PluginEditor.h"

#include "DspUtil.h"
#include "LaneSampleUi.h"
#include "RollFace.h"
#include "UiTokens.h"

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

Font tracked (Font font, float em)
{
    font.setExtraKerningFactor (em);
    return font;
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
    static const char* names[] = { "b7", "2nd", "b3", "3rd", "b2" };
    const int i = jlimit (0, 4, roundToInt (dark * 4.f));
    return String (names[i]) + " lift";
}

String busyCaption (float busy)
{
    return String (roundToInt (2.f + 5.f * busy)) + " hits / bar";
}

String bounceCaption (float bounce)
{
    return "flip slot " + String (roundToInt (4.f + 10.f * bounce));
}

String stabCaption (float stab)
{
    if (stab > 0.66f)
        return "every 1 bar";
    if (stab > 0.33f)
        return "every 2 bars";
    return "every 4 bars";
}

void focusRing (Graphics& g, Component& c)
{
    if (! c.hasKeyboardFocus (false))
        return;
    // 2 px stroke whose inner edge sits 2 px outside the control.
    g.setColour (col (kInk));
    g.drawRoundedRectangle (c.getLocalBounds().toFloat().expanded (3.f), Theme::radiusFocus, 2.f);
}

void paintMissingIcon (Graphics& g, float x, float y, float s)
{
    Path tri;
    tri.addTriangle (x, y + s, x + s * 0.5f, y, x + s, y + s);
    g.setColour (col (kMark));
    g.fillPath (tri);
    g.setColour (col (kPaper));
    const float cx = x + s * 0.5f;
    g.fillRect (cx - s * 0.055f, y + s * 0.36f, s * 0.11f, s * 0.30f);
    g.fillEllipse (cx - s * 0.065f, y + s * 0.72f, s * 0.13f, s * 0.13f);
}

struct Look : LookAndFeel_V4
{
    Font instSemi, instBold, mono, monoMed, monoSemi;

    Look()
        : instSemi (Theme::sansSemi (Theme::typeControl)),
          instBold (Theme::sansBold (Theme::typeControl)),
          mono (Theme::mono (Theme::typeBody)),
          monoMed (Theme::monoMed (Theme::typeBody)),
          monoSemi (Theme::monoSemi (Theme::typeValue))
    {
        setColour (PopupMenu::backgroundColourId, col (kCard));
        setColour (PopupMenu::textColourId, col (kInk));
        setColour (PopupMenu::highlightedBackgroundColourId, col (kHov));
        setColour (PopupMenu::highlightedTextColourId, col (kInk));
        setColour (ResizableWindow::backgroundColourId, col (kPaper));
    }

    Font getPopupMenuFont() override { return instSemi.withPointHeight (Theme::typeControl); }
    Font getLabelFont (Label&) override { return instSemi; }
    Font getTextButtonFont (TextButton&, int) override { return instSemi; }

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
        g.setFont (instSemi.withPointHeight (Theme::typeControl));
        auto textArea = area.reduced (28, 0);
        g.drawFittedText (text, textArea, Justification::centredLeft, 1);
        if (isTicked)
        {
            g.setColour (col (kInk));
            Path tick;
            tick.startNewSubPath (10.f, (float) area.getCentreY());
            tick.lineTo (13.f, (float) area.getCentreY() + 3.f);
            tick.lineTo (18.f, (float) area.getCentreY() - 4.f);
            g.strokePath (tick, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
        }
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

float textWidth (const Font& font, const String& text)
{
    return GlyphArrangement::getStringWidth (font, text);
}

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
        const float stroke = size >= 48.f ? 4.f : 3.f;
        const float r = size * 0.5f;
        const float arcR = r - stroke * 0.5f;
        const float capR = r * (size >= 48.f ? 0.64f : 0.60f);
        const float ptrW = size >= 48.f ? 2.5f : 2.f;
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
        g.setColour (col (isMouseOver() ? kInk2 : kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), rad, 1.f);

        auto lf = dynamic_cast<Look*> (&getLookAndFeel());
        g.setColour (col (kInk2));
        if (lf != nullptr)
            g.setFont (tracked (lf->instSemi.withPointHeight (Theme::typeMicro), 0.08f));
        g.drawText ("LVL", b.reduced (8.f, 0.f), Justification::centredLeft, false);
        g.setColour (col (kInk));
        if (lf != nullptr)
            g.setFont (lf->mono.withPointHeight (Theme::typeCaption));
        g.drawText (String (getValue(), 1), b.reduced (8.f, 0.f), Justification::centredRight, false);
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
        g.fillRoundedRectangle (x - 2.f, y - 8.f, 4.f, 16.f, 2.f);
        focusRing (g, *this);
    }
};

struct LabeledKnob : Component
{
    KnobSlider slider;
    String caption, readout, detail;
    bool compact = false;
    bool macro = false;

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
        if (compact)
            slider.setBounds (0, 14, 32, 32);
        else if (macro)
            slider.setBounds (0, 24, 64, 64);
        else
            slider.setBounds (0, 24, 64, 64);
    }

    void paint (Graphics& g) override
    {
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        if (lf == nullptr)
            return;
        auto top = getLocalBounds().toFloat().removeFromTop (compact ? 12.f : 16.f);
        g.setColour (col (kInk2));
        g.setFont (tracked (lf->instSemi.withPointHeight (compact ? Theme::typeMicro
                                                                  : (macro ? Theme::typeControl : Theme::typeCaption)),
                            macro ? 0.10f : 0.10f));
        g.drawText (caption, top, Justification::centred, false);

        if (macro)
        {
            g.setColour (col (kInk));
            g.setFont (lf->monoSemi.withPointHeight (Theme::typeValue));
            g.drawText (readout, Rectangle<float> (0.f, 96.f, (float) getWidth(), 16.f), Justification::centred, false);
            g.setColour (col (kInk2));
            g.setFont (lf->mono.withPointHeight (Theme::typeCaption));
            g.drawText (detail, Rectangle<float> (0.f, 112.f, (float) getWidth(), 14.f), Justification::centred, false);
        }
        else
        {
            g.setColour (col (kInk));
            g.setFont (lf->mono.withPointHeight (compact ? Theme::typeCaption : Theme::typeControl));
            auto bottom = getLocalBounds().toFloat().removeFromBottom (compact ? 12.f : 16.f);
            g.drawText (readout, bottom, Justification::centred, false);
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
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void paintButton (Graphics& g, bool over, bool down) override
    {
        auto b = getLocalBounds().toFloat();
        const bool on = getToggleState();
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
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
            if (lf != nullptr)
                g.setFont (lf->instBold.withPointHeight (Theme::typeBody));
            g.drawText ("M", getLocalBounds(), Justification::centred, false);
        }
        else if (kind == dice)
        {
            auto die = b.reduced (8.f);
            g.drawRoundedRectangle (die, 2.2f, 1.4f);
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
                g.drawRoundedRectangle (body, 1.6f, 1.4f);
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

struct LaneRow : Component, FileDragAndDropTarget
{
    RollaProcessor* proc = nullptr;
    int part = 0;
    bool selected = false;
    bool hovered = false;
    bool hot = false;
    bool sampleMissing = false;
    std::function<void (int)> onSelect;
    IconButton mute { IconButton::mute, "Mute" };
    IconButton shuffle { IconButton::dice, "Shuffle" };
    IconButton keep { IconButton::lock, "Keep" };
    PatternStrip strip;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> muteA, keepA;

    LaneRow()
    {
        addAndMakeVisible (strip);
        addAndMakeVisible (mute);
        addAndMakeVisible (shuffle);
        addAndMakeVisible (keep);
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

    void paint (Graphics& g) override
    {
        Path clip;
        clip.addRoundedRectangle (0.f, (float) (-40 * part), 640.f, 280.f, 8.f);
        g.reduceClipRegion (clip);

        auto b = getLocalBounds().toFloat();
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
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        g.setColour (laneColour (part).withAlpha (muted ? 0.4f : 1.f));
        g.fillEllipse (16.f, 16.f, 8.f, 8.f);
        const auto name = String (laneShortName (part));
        g.setColour ((selected || hovered ? col (kInk) : col (kInk2)).withAlpha (muted ? 0.4f : 1.f));
        if (lf != nullptr)
        {
            auto nameFont = tracked (lf->instBold.withPointHeight (Theme::typeControl), 0.06f);
            g.setFont (nameFont);
            g.drawText (name, 32, 12, 72, 16, Justification::centredLeft, false);
            if (sampleMissing)
            {
                float x = 32.f + textWidth (nameFont, name) + 4.f;
                if (x > 92.f)
                    x = 92.f;
                paintMissingIcon (g, x, 14.f, 12.f);
            }
        }
        else
        {
            g.drawText (name, 32, 12, 72, 16, Justification::centredLeft, false);
        }

        if (hot)
        {
            g.setColour (col (kInk));
            g.drawRect (b.reduced (0.5f), 1.f);
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
        strip.setBounds (104, 8, 408, 24);
        mute.setBounds (520, 4, 32, 32);
        shuffle.setBounds (560, 4, 32, 32);
        keep.setBounds (600, 4, 32, 32);
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
    g.strokePath (p, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));
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
        prev.setAlpha (atStart ? 0.35f : 1.f);
        next.setAlpha (atEnd ? 0.35f : 1.f);
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
        g.setColour (col (kCard));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (col (kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f - 0.5f, 1.f);

        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        if (lf == nullptr)
            return;
        auto mid = getLocalBounds().reduced (40, 0).toFloat();
        g.setColour (strike ? col (kInk3) : col (kInk));
        g.setFont (tracked (lf->instBold.withPointHeight (getHeight() < 36 ? Theme::typeControl : Theme::typePreset), 0.02f));
        if (sub.isEmpty())
            g.drawFittedText (title, mid.toNearestInt(), Justification::centred, 1);
        else
        {
            g.drawText (title, mid.removeFromTop (mid.getHeight() * 0.58f), Justification::centred, false);
            if (strike)
            {
                const float tw = textWidth (lf->instBold.withPointHeight (Theme::typePreset), title);
                g.drawLine (mid.getCentreX() - tw * 0.5f, mid.getY() - 1.f, mid.getCentreX() + tw * 0.5f, mid.getY() - 1.f, 1.f);
            }
            g.setColour (col (kInk3));
            g.setFont (lf->mono.withPointHeight (Theme::typeCaption));
            g.drawText (sub, mid, Justification::centred, false);
        }
    }

    void paintOverChildren (Graphics& g) override
    {
        auto arrow = [&] (QuietButton& btn, bool right)
        {
            auto b = btn.getBounds().toFloat().reduced (6.f, 4.f);
            if (btn.isEnabled() && btn.isDown())
            {
                g.setColour (col (kLine));
                g.fillRoundedRectangle (b, 8.f);
            }
            else if (btn.isEnabled() && btn.isOver())
            {
                g.setColour (col (kHov));
                g.fillRoundedRectangle (b, 8.f);
            }
            g.setColour (btn.isEnabled() ? (btn.isOver() ? col (kInk) : col (kInk2)) : col (kInk2).withAlpha (0.35f));
            chevron (g, btn.getBounds().toFloat(), right);
        };
        arrow (prev, false);
        arrow (next, true);
    }
};

struct DialectBar : Component
{
    QuietButton buttons[NumDialects];
    int current = 0;
    std::function<void (int)> onPick;

    DialectBar()
    {
        const char* names[] = { "UK MIN", "LONG", "POP", "GASKIN 90s" };
        for (int i = 0; i < NumDialects; ++i)
        {
            buttons[i].setButtonText (names[i]);
            buttons[i].setWantsKeyboardFocus (true);
            buttons[i].setMouseCursor (MouseCursor::PointingHandCursor);
            addAndMakeVisible (buttons[i]);
            buttons[i].onClick = [this, i] { if (onPick) onPick (i); };
            buttons[i].onStateChange = [this] { repaint(); };
        }
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
        auto r = getLocalBounds().reduced (4, 4);
        for (int i = 0; i < NumDialects; ++i)
        {
            const int w = (i == NumDialects - 1) ? r.getWidth() : r.getWidth() / (NumDialects - i);
            buttons[i].setBounds (r.removeFromLeft (w));
        }
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col (kCard));
        g.fillRoundedRectangle (b, 20.f);
        g.setColour (col (kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), 19.5f, 1.f);
    }

    void paintOverChildren (Graphics& g) override
    {
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        for (int i = 0; i < NumDialects; ++i)
        {
            auto b = buttons[i].getBounds().toFloat().reduced (2.f, 4.f);
            const bool on = i == current;
            const bool down = buttons[i].isDown();
            const bool over = buttons[i].isOver();
            if (on)
            {
                g.setColour (col (kInk));
                g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            }
            else if (down)
            {
                g.setColour (col (kLine));
                g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            }
            else if (over)
            {
                g.setColour (col (kHov));
                g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            }
            if (lf != nullptr)
                g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeBody), 0.04f));
            g.setColour (on ? col (kPaper) : (over ? col (kInk) : col (kInk2)));
            g.drawFittedText (buttons[i].getButtonText(), buttons[i].getBounds(), Justification::centred, 1);
        }
    }
};

struct ModeBar : Component
{
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
        auto r = getLocalBounds().reduced (3, 3);
        for (int i = 0; i < 3; ++i)
        {
            const int w = (i == 2) ? r.getWidth() : r.getWidth() / (3 - i);
            buttons[i].setBounds (r.removeFromLeft (w));
        }
    }

    void paint (Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour (col (kCard));
        g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
        g.setColour (col (kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), b.getHeight() * 0.5f - 0.5f, 1.f);
    }

    void paintOverChildren (Graphics& g) override
    {
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        for (int i = 0; i < 3; ++i)
        {
            auto b = buttons[i].getBounds().toFloat().reduced (1.f, 1.f);
            const bool on = i == current;
            const bool down = buttons[i].isDown();
            const bool over = buttons[i].isOver();
            if (on)
            {
                g.setColour (col (kInk));
                g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            }
            else if (down)
            {
                g.setColour (col (kLine));
                g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            }
            else if (over)
            {
                g.setColour (col (kHov));
                g.fillRoundedRectangle (b, b.getHeight() * 0.5f);
            }
            if (lf != nullptr)
                g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeCaption), 0.06f));
            g.setColour (on ? col (kPaper) : (over ? col (kInk) : col (kInk2)));
            g.drawFittedText (buttons[i].getButtonText(), buttons[i].getBounds(), Justification::centred, 1);
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
        g.setColour (down ? col (kLine) : (over ? col (kHov) : col (kCard)));
        g.fillRoundedRectangle (b.reduced (0.5f), 20.f);
        g.setColour (col (kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), 19.5f, 1.f);

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
        placed.applyTransform (AffineTransform::translation (12.f, 12.f));
        if (redo)
            placed.applyTransform (AffineTransform::scale (-1.f, 1.f, 20.f, 20.f));
        g.setColour (isEnabled() ? (over ? col (kInk) : col (kInk2)) : col (kInk2));
        g.strokePath (placed, PathStrokeType (1.5f, PathStrokeType::curved, PathStrokeType::rounded));
        focusRing (g, *this);
    }
};

struct FaceRollButton : QuietButton, private Timer
{
    RollaProcessor* proc = nullptr;
    String countText { "7 lanes" };
    String keptText;
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

    void setCaption (const String& count, const String& kept, bool disable)
    {
        if (count == countText && kept == keptText && disable == blocked)
            return;
        countText = count;
        keptText = kept;
        blocked = disable;
        setEnabled (! disable);
        setAlpha (disable ? 0.35f : 1.f);
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
        g.fillRoundedRectangle (b, 8.f);
        g.setColour (col (hot ? kLine2 : kLine));
        g.drawRoundedRectangle (b.reduced (0.5f), 8.f, 1.f);

        FacePose pose;
        pose.hover = hover.v;
        pose.press = press.v;
        pose.snap = snap && rolling;
        pose.roll = (! pose.snap && rolling) ? (float) ((Time::getMillisecondCounterHiRes() - rollStart) / 340.0) : -1.f;
        pose.roll = std::clamp (pose.roll, -1.f, 1.f);
        if (proc != nullptr)
            pose.dark = paramRaw (proc->apvts, "dark");
        paintRollFace (g, { 16.f, 16.f, 96.f, 96.f }, pose);

        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        if (lf != nullptr)
        {
            g.setColour (col (kInk));
            g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeHero), -0.01f));
            g.drawText ("Roll", 128, 32, 150, 40, Justification::centredLeft, false);

            g.setColour (col (kInk2));
            g.setFont (lf->mono.withPointHeight (Theme::typeCaption));
            g.drawRoundedRectangle (272.f, 40.f, 48.f, 20.f, 4.f, 1.f);
            g.drawText ("SPACE", 272, 40, 48, 20, Justification::centred, false);

            g.setFont (lf->mono.withPointHeight (Theme::typeBody));
            const auto left = textWidth (lf->monoMed.withPointHeight (Theme::typeBody), countText);
            g.setColour (col (kInk));
            g.setFont (lf->monoMed.withPointHeight (Theme::typeBody));
            g.drawText (countText, 128, 80, (int) std::ceil (left), 16, Justification::centredLeft, false);
            g.setColour (col (kInk2));
            g.setFont (lf->mono.withPointHeight (Theme::typeBody));
            g.drawText (keptText, 128 + (int) std::ceil (left), 80, 190, 16, Justification::centredLeft, false);
        }
        focusRing (g, *this);
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
        const float rad = vox ? 8.f : 6.f;
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

        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        if (lf == nullptr || proc == nullptr)
            return;

        if (vox)
        {
            auto text = b.reduced (12.f, 0.f);
            if (loaded)
                text.removeFromRight (32.f);
            g.setColour (col (kInk));
            g.setFont (lf->monoMed.withPointHeight (Theme::typeControl));
            g.drawFittedText (fromSlot ? slotLabel() : (loaded ? proc->getVoxSampleName() : "drop"),
                              text.removeFromTop (b.getHeight() * 0.55f).toNearestInt(),
                              Justification::centredLeft, 1);
            g.setColour (col (kInk2));
            g.setFont (lf->instSemi.withPointHeight (Theme::typeCaption));
            g.drawFittedText (loaded ? "overrides preset" : utf8 ("wav · aif · aiff · flac"),
                              text.toNearestInt(), Justification::centredLeft, 1);
            if (loaded)
            {
                g.setColour (col (kInk2));
                g.drawLine (b.getRight() - 28.f, b.getCentreY() - 4.f, b.getRight() - 20.f, b.getCentreY() + 4.f, 1.4f);
                g.drawLine (b.getRight() - 20.f, b.getCentreY() - 4.f, b.getRight() - 28.f, b.getCentreY() + 4.f, 1.4f);
            }
        }
        else
        {
            g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeCaption), 0.04f));
            g.setColour (col (kInk));
            auto top = b.reduced (6.f, 1.f).removeFromTop (b.getHeight() * 0.5f);
            g.drawText (loaded ? "YOUR WAV" : "909", top, Justification::centredLeft, false);
            g.setFont (lf->mono.withPointHeight (Theme::typeCaption));
            g.setColour (col (kInk2));
            auto name = fromSlot ? slotLabel()
                                 : (loaded ? proc->getDrumSampleName (voice) : String (panelVoiceCode (voice)));
            if (name.length() > 12)
                name = name.substring (0, 11) + utf8 ("…");
            g.drawText (name, b.reduced (6.f, 1.f).removeFromBottom (b.getHeight() * 0.5f),
                        Justification::centredLeft, false);
        }
        focusRing (g, *this);
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
        g.setColour (on ? col (kInk) : col (kPaper));
        g.fillRoundedRectangle (b, 12.f);
        g.setColour (on ? col (kInk) : (over ? col (kInk2) : col (kLine2)));
        g.drawRoundedRectangle (b.reduced (0.5f), 12.f, 1.f);
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        g.setColour (on ? col (kPaper) : (over ? col (kInk) : col (kInk2)));
        if (lf != nullptr)
            g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeCaption), 0.06f));
        g.drawText ("ACCENT", getLocalBounds(), Justification::centred, false);
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
    }

    void sync()
    {
        tune.setReadout (signed1 ((float) tune.slider.getValue()));
        decay.setReadout (String (decay.slider.getValue() * 10.0, 1));
        slot.repaint();
    }

    void resized() override
    {
        slot.setBounds (0, 24, 80, 32);
        tune.setBounds (4, 66, 32, 60);
        decay.setBounds (44, 66, 32, 60);
        level.setBounds (0, 132, 80, 24);
        accent.setBounds (0, 164, 80, 24);
    }

    void paint (Graphics& g) override
    {
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        if (lf == nullptr)
            return;
        g.setColour (col (kInk));
        g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeBody), 0.06f));
        g.drawText (panelVoiceLabel (voice), 0, 0, 80, 16, Justification::centredLeft, false);
    }
};

struct DragTile : Component
{
    RollaProcessor* proc = nullptr;
    QuietButton exportBtn { "EXPORT" };
    String caption { utf8 ("7 tracks · 8 bars") };
    std::unique_ptr<FileChooser> chooser;
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
        outline.addRoundedRectangle (b.reduced (0.5f), 8.f);
        Path dashed;
        const float dash[] = { 3.f, 3.f };
        PathStrokeType (1.f).createDashedStroke (dashed, outline, dash, 2);
        g.setColour (isMouseOver (true) ? col (kInk) : col (kInk3));
        g.fillPath (dashed);

        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        if (lf == nullptr)
            return;
        g.setColour (col (kInk2));
        Path arrow;
        arrow.startNewSubPath (28.f, 30.f);
        arrow.lineTo (28.f, 48.f);
        arrow.startNewSubPath (22.f, 42.f);
        arrow.lineTo (28.f, 48.f);
        arrow.lineTo (34.f, 42.f);
        arrow.startNewSubPath (18.f, 56.f);
        arrow.lineTo (38.f, 56.f);
        g.strokePath (arrow, PathStrokeType (1.6f, PathStrokeType::curved, PathStrokeType::rounded));

        g.setColour (col (kInk));
        g.setFont (tracked (lf->instBold.withPointHeight (Theme::typePreset), 0.08f));
        g.drawText ("DRAG MIDI", 48, 28, 160, 18, Justification::centredLeft, false);
        g.setColour (col (kInk2));
        g.setFont (lf->mono.withPointHeight (Theme::typeBody));
        g.drawText (caption, 48, 48, 180, 16, Justification::centredLeft, false);
    }

    void paintOverChildren (Graphics& g) override
    {
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        auto b = exportBtn.getBounds().toFloat();
        const bool over = exportBtn.isOver();
        g.setColour (over ? col (kInk) : col (kPaper));
        g.fillRoundedRectangle (b, 16.f);
        g.setColour (over ? col (kInk) : col (kLine2));
        g.drawRoundedRectangle (b.reduced (0.5f), 16.f, 1.f);
        g.setColour (over ? col (kPaper) : col (kInk));
        if (lf != nullptr)
            g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeCaption), 0.06f));
        g.drawText ("EXPORT", exportBtn.getBounds(), Justification::centred, false);
    }

    void resized() override
    {
        exportBtn.setBounds (248, 28, 72, 32);
    }

    void mouseDown (const MouseEvent&) override { armed = false; }

    void mouseDrag (const MouseEvent& e) override
    {
        if (e.eventComponent != this || proc == nullptr || armed)
            return;
        if (e.getDistanceFromDragStart() <= 4.f)
            return;
        armed = true;
        auto files = proc->writeTempMidi();
        if (auto* dnd = DragAndDropContainer::findParentDragContainerFor (this))
            if (files.size() > 0)
                dnd->performExternalDragDropOfFiles (files, false, this);
    }

    void choose()
    {
        if (proc == nullptr)
            return;
        const auto start = File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("ROLLA");
        chooser = std::make_unique<FileChooser> ("Export MIDI", start, String(), true);
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                              [this] (const FileChooser& fc)
                              {
                                  const auto dir = fc.getResult();
                                  if (dir != File() && proc != nullptr)
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
        g.setColour (on ? col (kInk) : col (kLine2));
        g.fillRoundedRectangle (b, 14.f);
        const float d = 20.f;
        const float x = on ? b.getRight() - 4.f - d : b.getX() + 4.f;
        g.setColour (on ? col (kPaper) : col (kInk2));
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
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        if (lf == nullptr)
            return;
        g.setFont (lf->monoMed.withPointHeight (Theme::typeBody));
        g.setColour (col (kInk));
        const float w = textWidth (lf->monoMed.withPointHeight (Theme::typeBody), bpm);
        g.drawText (bpm, 0, 0, (int) std::ceil (w), getHeight(), Justification::centredLeft, false);
        g.setColour (col (kInk2));
        g.setFont (lf->mono.withPointHeight (Theme::typeBody));
        g.drawText (" BPM", (int) std::ceil (w), 0, getWidth(), getHeight(), Justification::centredLeft, false);
    }
};

struct LogoButton : QuietButton
{
    LogoButton()
        : QuietButton ("ROLLA")
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void paintButton (Graphics& g, bool over, bool) override
    {
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        if (lf == nullptr)
            return;
        g.setColour (over ? col (kInk) : col (kInk));
        g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeLogo), -0.01f));
        g.drawText ("ROLLA", getLocalBounds(), Justification::centredLeft, false);
    }
};

struct StatusStrip : Component
{
    String text { "Hover a control." };

    void setHelp (const String& next)
    {
        const auto shown = next.isEmpty() ? String ("Hover a control.") : next;
        if (shown == text)
            return;
        text = shown;
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        g.setColour (col (kInk2));
        if (lf != nullptr)
            g.setFont (lf->mono.withPointHeight (Theme::typeCaption));
        g.drawFittedText (text, getLocalBounds(), Justification::centredLeft, 1);
    }
};

} // namespace

struct RollaEditor::Content : Component
{
    RollaProcessor& proc;
    RollaEditor& editor;
    LogoButton logo;
    DialectBar dialects;
    Stepper keyStep;
    BpmReadout bpm;
    HistoryButton undoBtn { false }, redoBtn { true };
    LaneRow rows[NumParts];
    LabeledKnob tone, punch, level;
    LabeledKnob dark, busy, bounce, stab;
    Stepper presetStep, kitStep;
    ModeBar source;
    LaneSampleProbe samples;
    SampleSlot voxSlot;
    SwitchButton inStab, duck;
    DrumColumn drums[7];
    FaceRollButton face;
    SwingSlider swing;
    DragTile drag;
    StatusStrip status;
    std::unique_ptr<FileChooser> sampleChooser;
    int selected = Bass;
    Groove groove;
    uint32_t seenGen = 0;
    bool haveGroove = false;

    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> toneA, punchA, levelA;
    std::unique_ptr<AudioProcessorValueTreeState::SliderAttachment> darkA, busyA, bounceA, stabA, swingA;
    std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment> duckA, inStabA;

    Content (RollaProcessor& p, Look& lf, RollaEditor& ed)
        : proc (p), editor (ed), samples (p)
    {
        setLookAndFeel (&lf);
        setOpaque (true);

        addAndMakeVisible (logo);
        addAndMakeVisible (dialects);
        addAndMakeVisible (keyStep);
        addAndMakeVisible (bpm);
        addAndMakeVisible (undoBtn);
        addAndMakeVisible (redoBtn);
        logo.onClick = [this] { showScaleMenu (&logo); };
        dialects.onPick = [this] (int i) { setChoiceIndex (proc, "dialect", i); };
        keyStep.onStep = [this] (int dir)
        {
            const int n = (choiceIndex (proc.apvts, "key") + dir + 12) % 12;
            setChoiceIndex (proc, "key", n);
        };
        undoBtn.onClick = [this] { proc.undo.undo(); };
        redoBtn.onClick = [this] { proc.undo.redo(); };

        for (int i = 0; i < NumParts; ++i)
        {
            rows[i].configure (proc, i);
            rows[i].onSelect = [this] (int part) { select (part); };
            rows[i].strip.groove = &groove;
            addAndMakeVisible (rows[i]);
        }

        tone.caption = "TONE";
        punch.caption = "PUNCH";
        level.caption = "LEVEL";
        tone.slider.getProperties().set ("bipolar", true);
        for (auto* k : { &tone, &punch, &level })
            addAndMakeVisible (k);
        presetStep.onStep = [this] (int dir) { stepPreset (dir); };
        presetStep.onBrowse = [this] { browseLanePresets (presetStep); };
        addAndMakeVisible (presetStep);
        kitStep.onStep = [this] (int dir) { stepPreset (dir); };
        kitStep.onBrowse = [this] { browseLanePresets (kitStep); };
        addAndMakeVisible (kitStep);
        source.onPick = [this] (int mode)
        {
            if (samples.mode (selected) == mode)
                return;
            samples.setMode (selected, mode);
            proc.markEdited (selected);
            sync();
        };
        addAndMakeVisible (source);

        voxSlot.proc = &proc;
        voxSlot.voice = -1;
        voxSlot.onLoaded = [this] { repaint(); };
        addAndMakeVisible (voxSlot);
        inStab.onClick = [this] { proc.markEdited (Hits); };
        addAndMakeVisible (inStab);
        inStabA = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "hitsInStab", inStab);

        const int voices[] = { Kick, Snare, Clap, Rim, HatC, HatO, Ride };
        for (int i = 0; i < 7; ++i)
        {
            drums[i].configure (proc, voices[i]);
            addAndMakeVisible (drums[i]);
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
        bindHelp (dialects, "Style. Picks the preset and swing for every lane.");
        bindHelp (keyStep, "Key. Every lane follows this minor key.");
        bindHelp (bpm, "Host tempo, in beats per minute.");
        bindHelp (undoBtn, "Undo the last edit.");
        bindHelp (redoBtn, "Redo the edit you just undid.");
        bindHelp (presetStep, "Preset for this lane. Click the name to see only this lane's presets.");
        bindHelp (kitStep, "Drum kit. Click the name to choose another kit.");
        bindHelp (source, "Source. Synth, a sample, or both layered.");
        bindHelp (tone, "Tone. Brighter towards the right.");
        bindHelp (punch, "Punch. Shorter and snappier towards the right.");
        bindHelp (level, "Level, in decibels.");
        bindHelp (dark, "Dark. How minor the harmony sits.");
        bindHelp (busy, "Busy. How many hits land in a bar.");
        bindHelp (bounce, "Bounce. Where the offbeat flip sits.");
        bindHelp (stab, "Stab. How often the stab answers.");
        bindHelp (swing, "Swing, from 50 to 57 percent.");
        bindHelp (duck, "Duck. The bass dips under the stab.");
        bindHelp (face, "Roll. Re-rolls every lane that is not kept.");
        bindHelp (drag, "Drag MIDI. Pulls a clip of the current pattern.");
        bindHelp (voxSlot, "Vox sample. Drop a file to override the preset.");
        bindHelp (inStab, "In stab. Hits land on the stab's answer slots.");
        for (int i = 0; i < NumParts; ++i)
            bindHelp (rows[i], String (laneShortName (i)) + ". Click to edit. Mute, shuffle, or keep it from here.");

        groove = proc.copyGroove();
        haveGroove = true;
        seenGen = groove.generation;
        select (Bass);
        sync();
    }

    ~Content() override
    {
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
        menu.addItem (8, proc.samplesRoot().getFullPathName(), false, false);
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

    void select (int part)
    {
        selected = part;
        for (int i = 0; i < NumParts; ++i)
        {
            rows[i].selected = i == part;
            rows[i].repaint();
        }
        bindSound();
        source.setCurrent (samples.mode (selected));
        repaint();
    }

    void stepPreset (int dir)
    {
        const auto names = presetsForLane (proc.copyPresets(), selected);
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
        const auto names = presetsForLane (proc.copyPresets(), part);
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
        groove = std::move (next);
        haveGroove = true;
        seenGen = groove.generation;
        for (int i = 0; i < NumParts; ++i)
        {
            rows[i].strip.swing = swingNow;
            rows[i].strip.playing = playing;
            rows[i].strip.pos16 = pos16;
            rows[i].syncHover();
            const bool muted = rows[i].mute.getToggleState();
            rows[i].strip.setAlpha (muted ? 0.4f : 1.f);
            rows[i].shuffle.setEnabled (true);
            rows[i].shuffle.setAlpha (1.f);
            const bool miss = samples.missing (i);
            if (miss != rows[i].sampleMissing)
            {
                rows[i].sampleMissing = miss;
                rows[i].repaint();
            }
            if (genChanged || playing)
                rows[i].strip.repaint();
            rows[i].keep.repaint();
        }
        source.setCurrent (samples.mode (selected));

        bpm.setBpm (proc.getPlayBpm() > 1.f ? proc.getPlayBpm() : 130.f);
        dialects.setCurrent (choiceIndex (proc.apvts, "dialect"));
        for (int i = 0; i < NumDialects; ++i)
            dialects.buttons[i].repaint();
        keyStep.title = String (keyName (choiceIndex (proc.apvts, "key"))) + " MIN";
        keyStep.repaint();

        const bool canUndo = proc.undo.canUndo();
        const bool canRedo = proc.undo.canRedo();
        undoBtn.setEnabled (canUndo);
        redoBtn.setEnabled (canRedo);
        undoBtn.setAlpha (canUndo ? 1.f : 0.35f);
        redoBtn.setAlpha (canRedo ? 1.f : 0.35f);

        if (auto* preset = dynamic_cast<AudioParameterChoice*> (proc.apvts.getParameter (String (partId (selected)) + "Preset")))
        {
            const auto names = presetsForLane (proc.copyPresets(), selected);
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
        level.setReadout (String (level.slider.getValue(), 1) + " dB");
        dark.setReadout (String (dark.slider.getValue() * 10.0, 1), darkCaption ((float) dark.slider.getValue()));
        busy.setReadout (String (busy.slider.getValue() * 10.0, 1), busyCaption ((float) busy.slider.getValue()));
        bounce.setReadout (String (bounce.slider.getValue() * 10.0, 1), bounceCaption ((float) bounce.slider.getValue()));
        stab.setReadout (String (stab.slider.getValue() * 10.0, 1), stabCaption ((float) stab.slider.getValue()));
        for (auto& col : drums)
            if (col.isVisible())
                col.sync();

        int rolling = 0;
        StringArray kept;
        int tracks = 0;
        for (int i = 0; i < NumParts; ++i)
        {
            const bool k = paramRaw (proc.apvts, String (partId (i)) + "Keep") >= 0.5f;
            const bool m = paramRaw (proc.apvts, String (partId (i)) + "Mute") >= 0.5f;
            if (k)
                kept.add (laneShortName (i));
            if (! k && ! m)
                ++rolling;
            if (! m)
                ++tracks;
        }
        if (allKept())
            face.setCaption ("all kept", {}, true);
        else
        {
            const auto count = String (rolling) + (rolling == 1 ? " lane" : " lanes");
            const auto tail = kept.isEmpty() ? String() : utf8 ("  ·  ") + kept.joinIntoString (" + ") + " kept";
            face.setCaption (count, tail, false);
        }
        drag.caption = String (tracks) + (tracks == 1 ? utf8 (" track · ") : utf8 (" tracks · "))
                     + String (groove.bars) + (groove.bars == 1 ? " bar" : " bars");
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
        card ({ 16.f, 64.f, 640.f, 280.f });
        card ({ 16.f, 352.f, 640.f, 272.f });
        card ({ 672.f, 200.f, 336.f, 272.f });
        card ({ 672.f, 480.f, 336.f, 48.f });

        auto* lf = dynamic_cast<Look*> (&getLookAndFeel());
        if (lf == nullptr)
            return;

        g.setColour (proc.samplesMissing() > 0 ? col (kMark) : col (kInk2));
        g.setFont (lf->mono.withHeight (11.f));
        g.drawText (proc.sampleInventoryText(), 568, 16, 250, 18, Justification::centredLeft, true);

        g.setColour (col (kLine));
        g.fillRect (16, 399, 640, 1);

        g.setColour (laneColour (selected));
        g.fillEllipse (32.f, 371.f, 10.f, 10.f);
        auto nameFont = tracked (lf->instBold.withPointHeight (Theme::typeTitle), 0.02f);
        g.setColour (col (kInk));
        g.setFont (nameFont);
        const auto panelName = String (laneShortName (selected));
        g.drawText (panelName, 48, 364, 120, 24, Justification::centredLeft, false);
        if (samples.missing (selected))
        {
            float x = 48.f + textWidth (nameFont, panelName) + 6.f;
            if (selected == Drums && x > 112.f)
                x = 112.f;
            paintMissingIcon (g, x, 370.f, 12.f);
        }
        if (selected != Drums)
        {
            g.setColour (col (kInk2));
            g.setFont (lf->mono.withPointHeight (Theme::typeBody));
            g.drawText (channelLine (selected), 168, 364, 160, 24, Justification::centredLeft, false);
        }
        if (proc.laneEdited (selected))
        {
            g.setColour (col (kMark));
            g.setFont (lf->mono.withPointHeight (Theme::typeCaption));
            g.drawFittedText ("EDITED",
                              selected == Drums ? Rectangle<int> (128, 364, 120, 24)
                                                : Rectangle<int> (336, 364, 112, 24),
                              Justification::centredLeft, 1);
        }

        if (selected != Drums)
        {
            g.setColour (col (kInk2));
            g.setFont (tracked (lf->instSemi.withPointHeight (Theme::typeCaption), 0.10f));
            g.drawText ("PRESET", 32, 440, 80, 16, Justification::centredLeft, false);
            if (selected == Vox)
                g.drawText ("SAMPLE", 32, 520, 80, 16, Justification::centredLeft, false);
            if (selected == Hits)
            {
                g.drawText ("PLACEMENT", 32, 520, 100, 16, Justification::centredLeft, false);
                g.setFont (lf->instSemi.withPointHeight (Theme::typeControl));
                g.setColour (col (kInk));
                g.drawText ("In stab", 48, 544, 120, 40, Justification::centredLeft, false);
                g.setColour (col (kInk2));
                g.setFont (lf->instSemi.withPointHeight (Theme::typeBody));
                g.drawText ("Hits land on the stab's answer slots.", 32, 592, 280, 16, Justification::centredLeft, true);
            }
        }

        g.setColour (col (kInk2));
        g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeCaption), 0.10f));
        g.drawText ("SWING", 688, 492, 52, 24, Justification::centredLeft, false);
        g.setColour (col (kInk));
        g.setFont (lf->mono.withPointHeight (Theme::typeControl));
        g.drawText (String (roundToInt (paramRaw (proc.apvts, "swing") * 100.f)) + "%",
                    828, 492, 48, 24, Justification::centredLeft, false);
        g.setColour (col (kLine));
        g.fillRect (888, 492, 1, 24);
        g.setColour (col (kInk2));
        g.setFont (tracked (lf->instBold.withPointHeight (Theme::typeCaption), 0.10f));
        g.drawText ("DUCK", 904, 492, 48, 24, Justification::centredLeft, false);
    }

    void resized() override
    {
        logo.setBounds (16, 16, 76, 24);
        dialects.setBounds (100, 8, 312, 40);
        keyStep.setBounds (428, 8, 136, 40);
        bpm.setBounds (832, 20, 72, 16);
        undoBtn.setBounds (912, 8, 40, 40);
        redoBtn.setBounds (968, 8, 40, 40);

        for (int i = 0; i < NumParts; ++i)
            rows[i].setBounds (16, 64 + 40 * i, 640, 40);

        presetStep.setBounds (32, 464, 256, 40);
        tone.setBounds (328, 440, 64, 112);
        punch.setBounds (424, 440, 64, 112);
        level.setBounds (520, 440, 64, 112);
        voxSlot.setBounds (32, 544, 240, 56);
        inStab.setBounds (200, 550, 44, 28);
        kitStep.setBounds (264, 360, 176, 32);
        source.setBounds (456, 360, 184, 32);
        for (int i = 0; i < 7; ++i)
            drums[i].setBounds (32 + 88 * i, 416, 80, 188);

        face.setBounds (672, 64, 336, 128);
        dark.setBounds (724, 204, 64, 128);
        busy.setBounds (892, 204, 64, 128);
        bounce.setBounds (724, 340, 64, 128);
        stab.setBounds (892, 340, 64, 128);
        swing.setBounds (744, 492, 80, 24);
        duck.setBounds (956, 490, 44, 28);
        drag.setBounds (672, 536, 336, 88);
        status.setBounds (Theme::space16, 624, 992, Theme::space16);
    }
};

struct RollaEditor::SizeConstrainer : juce::ComponentBoundsConstrainer
{
};

namespace
{
constexpr int kEditorBaseW = 1024;
constexpr int kEditorBaseH = 640;

juce::PropertiesFile::Options editorSettingsOptions()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "ROLLA";
    options.filenameSuffix = ".settings";
    options.osxLibrarySubFolder = "Application Support";
    options.folderName = "ROLLA";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    return options;
}

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
    editorSettings = std::make_unique<juce::PropertiesFile> (editorSettingsOptions());
    constrainer = std::make_unique<SizeConstrainer>();
    constrainer->setFixedAspectRatio ((double) kEditorBaseW / (double) kEditorBaseH);
    constrainer->setSizeLimits (768, 480, 2048, 1280);
    setConstrainer (constrainer.get());
    setResizable (true, true);
    content = std::make_unique<Content> (proc, static_cast<Look&> (*look), *this);
    addAndMakeVisible (content.get());
    setOpaque (true);
    appliedProjectSerial = proc.getProjectEditorSerial();
    const auto size = initialSize();
    setSize (size.x, size.y);
    rememberSize();
    sizeDirty = false;
    if (resizableCorner != nullptr)
        resizableCorner->toFront (false);
    setWantsKeyboardFocus (true);
    addMouseListener (this, true);
    struct HelpWindow : TooltipWindow
    {
        using TooltipWindow::TooltipWindow;
        String getTipFor (Component& c) override
        {
            const auto text = helpUnder (&c);
            return text.isNotEmpty() ? text : TooltipWindow::getTipFor (c);
        }
    };
    tooltip = std::make_unique<HelpWindow> (this, 600);
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
    if (getWidth() >= 768)
        sizeDirty = true;
}

void RollaEditor::selectLane (int part)
{
    if (content != nullptr)
        content->select (juce::jlimit (0, NumParts - 1, part));
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
    if (key == KeyPress::spaceKey && hasKeyboardFocus (true))
    {
        if (! everyLaneKept())
            proc.triggerRoll();
        return true;
    }
    return false;
}

void RollaEditor::mouseDown (const MouseEvent& e)
{
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
    if (e.eventComponent == this)
        grabKeyboardFocus();
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
}

} // namespace rolla
