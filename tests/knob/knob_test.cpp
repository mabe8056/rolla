#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "../common/ScratchProfile.h"
#include "UiTokens.h"
#include "ui/TextFit.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
}

namespace
{

int fails = 0;

void expect (bool ok, const std::string& msg)
{
    if (ok)
        return;
    std::cerr << "FAIL " << msg << "\n";
    ++fails;
}

bool pumpUntil (const std::function<bool()>& done, int ms = 4000)
{
    const auto start = juce::Time::getMillisecondCounter();
    while (! done() && juce::Time::getMillisecondCounter() - start < (juce::uint32) ms)
    {
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (1);
    }
    return done();
}

void setDenorm (rolla::RollaProcessor& proc, const char* id, float denorm)
{
    auto* param = proc.apvts.getParameter (id);
    expect (param != nullptr, std::string ("missing ") + id);
    if (param == nullptr)
        return;
    param->setValueNotifyingHost (param->convertTo0to1 (denorm));
}

float rawOf (rolla::RollaProcessor& proc, const char* id)
{
    if (auto* v = proc.apvts.getRawParameterValue (id))
        return v->load();
    return 0.f;
}

bool flagOf (rolla::RollaProcessor& proc, const char* id)
{
    return rawOf (proc, id) >= 0.5f;
}

void solo (rolla::RollaProcessor& proc, int part)
{
    for (int i = 0; i < rolla::NumParts; ++i)
        setDenorm (proc, (std::string (rolla::partId (i)) + "Mute").c_str(), i == part ? 0.f : 1.f);
}

struct Head : juce::AudioPlayHead
{
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm (130.0);
        info.setIsPlaying (true);
        info.setPpqPosition (ppq);
        return info;
    }
    double ppq = 0.0;
};

struct Ear
{
    double rms = 0.0;
    double bright = 0.0;
    std::vector<float> mono;
};

Ear render (rolla::RollaProcessor& proc, Head& head, int bars)
{
    proc.releaseResources();
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::disabled());
    proc.setBusesLayout (layout);
    proc.prepareToPlay (48000.0, 256);

    const double bpm = 130.0;
    const int total = (int) std::llround ((double) bars * 4.0 * 60.0 / bpm * 48000.0);
    Ear ear;
    ear.mono.assign ((size_t) total, 0.f);
    juce::AudioBuffer<float> buffer (2, 256);
    head.ppq = 0.0;
    double sum = 0.0, hp = 0.0, lp = 0.0;
    float state = 0.f;
    const float c = 1.f - std::exp (-2.f * 3.14159265f * 1400.f / 48000.f);
    int done = 0;
    while (done < total)
    {
        const int n = std::min (256, total - done);
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
        const float* L = buffer.getReadPointer (0);
        const float* R = buffer.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float x = 0.5f * (L[i] + (R != nullptr ? R[i] : L[i]));
            ear.mono[(size_t) done + (size_t) i] = x;
            sum += (double) x * (double) x;
            state += (x - state) * c;
            const float high = x - state;
            hp += (double) high * (double) high;
            lp += (double) state * (double) state;
        }
        head.ppq += (double) n * bpm / (60.0 * 48000.0);
        done += n;
    }
    ear.rms = std::sqrt (sum / (double) std::max (1, total));
    ear.bright = hp / (hp + lp + 1.0e-12);
    return ear;
}

double diffRms (const Ear& a, const Ear& b)
{
    const size_t n = std::min (a.mono.size(), b.mono.size());
    if (n == 0)
        return 0.0;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double d = (double) a.mono[i] - (double) b.mono[i];
        sum += d * d;
    }
    return std::sqrt (sum / (double) n);
}

bool heard (const Ear& a, const Ear& b, std::string& detail)
{
    const double diff = diffRms (a, b);
    const double rel = std::abs (a.rms - b.rms) / std::max (1.0e-9, std::max (a.rms, b.rms));
    const double bright = std::abs (a.bright - b.bright);
    detail = "diff " + std::to_string (diff) + " rel " + std::to_string (rel)
           + " bright " + std::to_string (bright)
           + " rms " + std::to_string (a.rms) + " / " + std::to_string (b.rms);
    if (a.rms < 1.0e-5 && b.rms < 1.0e-5)
    {
        detail += " (silent)";
        return false;
    }
    return diff > 2.5e-4 || rel > 0.04 || bright > 0.02;
}

void expectHeard (rolla::RollaProcessor& proc, Head& head, const std::string& name,
                  const std::function<void (float)>& apply, float lo, float hi)
{
    apply (lo);
    auto a = render (proc, head, 2);
    apply (hi);
    auto b = render (proc, head, 2);
    std::string detail;
    if (! heard (a, b, detail) && a.rms < 1.0e-4 && b.rms < 1.0e-4)
    {
        apply (lo);
        a = render (proc, head, 8);
        apply (hi);
        b = render (proc, head, 8);
        heard (a, b, detail);
    }
    else if (! heard (a, b, detail))
    {
        apply (lo);
        a = render (proc, head, 4);
        apply (hi);
        b = render (proc, head, 4);
        heard (a, b, detail);
    }
    expect (heard (a, b, detail), name + " " + detail);
}

std::uint64_t mix (std::uint64_t h, std::uint64_t v)
{
    h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t hashPart (const rolla::Groove& g, int part)
{
    std::uint64_t h = (std::uint64_t) part + 1;
    if (part == rolla::Drums)
    {
        const int steps = g.bars * 16;
        for (int v = 0; v < rolla::NumDrumVoices; ++v)
            for (int t = 0; t < steps; ++t)
                h = mix (h, g.drums.vel[v][t]);
        return h;
    }
    const auto& lane = g.lane[part];
    h = mix (h, (std::uint64_t) lane.count);
    for (int i = 0; i < lane.count; ++i)
    {
        const auto& n = lane.notes[i];
        h = mix (h, (std::uint64_t) std::llround (n.start * 1000.0));
        h = mix (h, (std::uint64_t) std::llround (n.length * 1000.0));
        h = mix (h, n.vel);
        h = mix (h, n.flags);
        h = mix (h, n.nChord);
        for (int c = 0; c < n.nChord; ++c)
            h = mix (h, (std::uint64_t) (int) n.chord[c] + 64);
    }
    return h;
}

std::uint64_t hashAll (const rolla::Groove& g)
{
    std::uint64_t h = 0;
    for (int i = 0; i < rolla::NumParts; ++i)
        h = mix (h, hashPart (g, i));
    return h;
}

bool packOnDisk (rolla::RollaProcessor& proc)
{
    pumpUntil ([&] { return proc.samplesFound() >= 0; }, 8000);
    return proc.samplesFound() > 0;
}

bool reshapeTo (rolla::RollaProcessor& proc, const char* id, float value)
{
    const auto before = proc.copyGroove().generation;
    setDenorm (proc, id, value);
    return pumpUntil ([&] { return proc.copyGroove().generation != before; });
}

juce::Component* findHelp (juce::Component& c, const char* prefix)
{
    if (c.getProperties()["rollaHelp"].toString().startsWith (prefix))
        return &c;
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* found = findHelp (*c.getChildComponent (i), prefix))
            return found;
    return nullptr;
}

int countHelp (juce::Component& c, const char* prefix)
{
    int n = c.getProperties()["rollaHelp"].toString().startsWith (prefix) ? 1 : 0;
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        n += countHelp (*c.getChildComponent (i), prefix);
    return n;
}

juce::Slider* findSlider (juce::Component& c)
{
    if (auto* slider = dynamic_cast<juce::Slider*> (&c))
        return slider;
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* found = findSlider (*c.getChildComponent (i)))
            return found;
    return nullptr;
}

juce::Button* findButton (juce::Component& c, const char* text)
{
    if (auto* button = dynamic_cast<juce::Button*> (&c))
        if (button->getButtonText() == text)
            return button;
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        if (auto* found = findButton (*c.getChildComponent (i), text))
            return found;
    return nullptr;
}

void collectSliders (juce::Component& c, std::vector<juce::Slider*>& out)
{
    if (auto* slider = dynamic_cast<juce::Slider*> (&c))
        out.push_back (slider);
    for (int i = 0; i < c.getNumChildComponents(); ++i)
        collectSliders (*c.getChildComponent (i), out);
}

void testSoundControls()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    Head head;
    proc.setPlayHead (&head);
    // Pluck is a DEEP lane. ROLLER stays empty; DEEP brings it in with the arrangement.
    expect (proc.copyGroove().lane[rolla::Pluck].count == 0, "roller leaves pluck out");
    expect (reshapeTo (proc, "dialect", (float) rolla::Long), "deep for pluck");
    expect (pumpUntil ([&] { return proc.copyGroove().lane[rolla::Pluck].count > 0; }), "deep brings pluck in");

    // Factory presets play the sample pack and do not fall back to the synth.
    // A runner without ~/Music/ROLLA/Samples stays silent, same as the loudness gate.
    if (! packOnDisk (proc))
    {
        std::cout << "sound knobs skipped: sample pack is not on this machine\n";
        proc.setPlayHead (nullptr);
        return;
    }

    const int lanes[] = { rolla::Bass, rolla::Stab, rolla::Hits, rolla::Pluck, rolla::Pad, rolla::Vox };
    const char* names[] = { "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX" };
    for (int i = 0; i < 6; ++i)
    {
        solo (proc, lanes[i]);
        const auto id = std::string (rolla::partId (lanes[i]));
        setDenorm (proc, (id + "Punch").c_str(), 0.5f);
        setDenorm (proc, (id + "Level").c_str(), 0.f);
        expectHeard (proc, head, std::string (names[i]) + " TONE",
                     [&] (float v) { setDenorm (proc, (id + "Tone").c_str(), v); }, 0.f, 1.f);
        setDenorm (proc, (id + "Tone").c_str(), 0.5f);
        expectHeard (proc, head, std::string (names[i]) + " PUNCH",
                     [&] (float v) { setDenorm (proc, (id + "Punch").c_str(), v); }, 0.f, 1.f);
        setDenorm (proc, (id + "Punch").c_str(), 0.5f);
        expectHeard (proc, head, std::string (names[i]) + " LEVEL",
                     [&] (float v) { setDenorm (proc, (id + "Level").c_str(), v); }, -24.f, 6.f);
    }

    solo (proc, rolla::Drums);
    expectHeard (proc, head, "SWING",
                 [&] (float v) { setDenorm (proc, "swing", v); }, 0.50f, 0.57f);

    for (int i = 0; i < rolla::NumParts; ++i)
        setDenorm (proc, (std::string (rolla::partId (i)) + "Mute").c_str(), i == rolla::Bass || i == rolla::Drums ? 0.f : 1.f);
    expectHeard (proc, head, "DUCK",
                 [&] (float v) { setDenorm (proc, "duck", v); }, 0.f, 1.f);

    solo (proc, rolla::Bass);
    expectHeard (proc, head, "KEY",
                 [&] (float v) { setDenorm (proc, "key", v); }, 0.f, 11.f);

    solo (proc, rolla::Drums);
    const int voices[] = { rolla::Kick, rolla::Snare, rolla::Clap, rolla::Rim, rolla::HatC, rolla::HatO, rolla::Ride };
    for (int voice : voices)
    {
        const auto id = std::to_string (voice);
        for (int other : voices)
            if (other != voice)
                setDenorm (proc, ("dLevel" + std::to_string (other)).c_str(), -24.f);
        setDenorm (proc, ("dLevel" + id).c_str(), 0.f);
        setDenorm (proc, ("dDecay" + id).c_str(), 0.85f);
        expectHeard (proc, head, std::string (rolla::panelVoiceLabel (voice)) + " TUNE",
                     [&] (float v) { setDenorm (proc, ("dTune" + id).c_str(), v); }, -12.f, 12.f);
        setDenorm (proc, ("dTune" + id).c_str(), 0.f);
        setDenorm (proc, ("dLevel" + id).c_str(), 0.f);
        expectHeard (proc, head, std::string (rolla::panelVoiceLabel (voice)) + " DECAY",
                     [&] (float v) { setDenorm (proc, ("dDecay" + id).c_str(), v); }, 0.f, 1.f);
        setDenorm (proc, ("dDecay" + id).c_str(), 0.7f);
        expectHeard (proc, head, std::string (rolla::panelVoiceLabel (voice)) + " LEVEL",
                     [&] (float v) { setDenorm (proc, ("dLevel" + id).c_str(), v); }, -24.f, 6.f);
        setDenorm (proc, ("dLevel" + id).c_str(), 0.f);
    }

    proc.setPlayHead (nullptr);
}

float notesPerBar (const rolla::Groove& g)
{
    return g.bars > 0 ? (float) g.lane[rolla::Bass].count / (float) g.bars : 0.f;
}

void testBusyDensity()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        if ((int) rawOf (proc, "dialect") != style)
            expect (reshapeTo (proc, "dialect", (float) style), std::string (names[style]) + " dialect");
        auto stabPerBar = [] (const rolla::Groove& g)
        {
            return g.bars > 0 ? (float) g.lane[rolla::Stab].count / (float) g.bars : 0.f;
        };
        expect (reshapeTo (proc, "busy", 0.f), std::string (names[style]) + " BUSY min");
        const auto atLo = proc.copyGroove();
        const float lo = notesPerBar (atLo);
        const float stabLo = stabPerBar (atLo);
        expect (reshapeTo (proc, "busy", 0.5f), std::string (names[style]) + " BUSY mid");
        const auto atMid = proc.copyGroove();
        const float mid = notesPerBar (atMid);
        const float stabMid = stabPerBar (atMid);
        expect (reshapeTo (proc, "busy", 1.f), std::string (names[style]) + " BUSY max");
        const auto atHi = proc.copyGroove();
        const float hi = notesPerBar (atHi);
        const float stabHi = stabPerBar (atHi);
        std::cout << names[style] << " BUSY bass/bar " << lo << " -> " << mid << " -> " << hi
                  << "  stabs/bar " << stabLo << " -> " << stabMid << " -> " << stabHi << "\n";
        const std::string detail = std::string (names[style]) + " BUSY bass/bar "
                                    + std::to_string (lo) + " -> " + std::to_string (mid) + " -> " + std::to_string (hi);
        expect (lo <= mid + 0.02f && mid <= hi + 0.02f, detail + " monotonic");
        expect (lo > 0.5f && hi + 1.0e-3f >= lo * 1.30f, detail + " rises at least 30%");
        const std::string stabDetail = std::string (names[style]) + " BUSY stabs/bar "
                                        + std::to_string (stabLo) + " -> " + std::to_string (stabMid) + " -> " + std::to_string (stabHi);
        expect (stabLo <= stabMid + 0.02f && stabMid <= stabHi + 0.02f && stabHi > stabLo + 0.2f,
                stabDetail + " monotonic");
    }
}

void testGeneratorControls()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();

    int snare = 0, ride = 0, rim = 0;
    {
        const auto g = proc.copyGroove();
        const int steps = g.bars * 16;
        for (int t = 0; t < steps; ++t)
        {
            snare += g.drums.vel[rolla::Snare][t] > 0;
            ride += g.drums.vel[rolla::Ride][t] > 0;
            rim += g.drums.vel[rolla::Rim][t] > 0;
        }
    }
    expect (snare > 0, "snare is in the pattern");
    expect (ride > 0, "ride is in the pattern");
    expect (rim > 0, "rim is in the pattern");

    auto compareMacro = [&] (const char* id, const char* name)
    {
        expect (reshapeTo (proc, id, 0.f), std::string (name) + " reshape at min");
        const auto lo = hashAll (proc.copyGroove());
        expect (reshapeTo (proc, id, 1.f), std::string (name) + " reshape at max");
        const auto hi = hashAll (proc.copyGroove());
        expect (lo != hi, std::string (name) + " pattern changes from min to max");
    };
    compareMacro ("dark", "DARK");
    compareMacro ("busy", "BUSY");
    compareMacro ("bounce", "BOUNCE");
    compareMacro ("stab", "STAB");
    compareMacro ("styleIntensity", "STYLE INTENSITY");

    expect (reshapeTo (proc, "busy", 0.f), "busy back to min");
    setDenorm (proc, "bassKeep", 1.f);
    const auto keptBass = hashPart (proc.copyGroove(), rolla::Bass);
    const auto keptDrums = hashPart (proc.copyGroove(), rolla::Drums);
    expect (reshapeTo (proc, "busy", 1.f), "busy with bass kept");
    expect (hashPart (proc.copyGroove(), rolla::Bass) == keptBass, "BUSY leaves a kept lane");
    expect (hashPart (proc.copyGroove(), rolla::Drums) != keptDrums, "BUSY still rewrites unkept drums");

    setDenorm (proc, "bassKeep", 0.f);
    const auto uk = hashAll (proc.copyGroove());
    expect (reshapeTo (proc, "dialect", (float) rolla::Gaskin), "style reshape");
    expect (hashAll (proc.copyGroove()) != uk, "STYLE pattern changes");
    expect ((int) rawOf (proc, "dialect") == rolla::Gaskin, "style parameter landed");

    for (const char* id : { "dark", "busy", "bounce", "stab", "styleIntensity", "swing", "duck", "key", "dialect",
                            "bassTone", "bassPunch", "bassLevel", "dTune0", "dDecay0", "dLevel0" })
    {
        auto* param = proc.apvts.getParameter (id);
        expect (param != nullptr && param->isAutomatable(), std::string (id) + " automatable");
    }
}

void testState()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    setDenorm (proc, "dark", 0.2f);
    setDenorm (proc, "bassTone", 0.81f);
    setDenorm (proc, "dTune0", -4.5f);
    setDenorm (proc, "duck", 0.f);
    setDenorm (proc, "key", 7.f);
    expect (reshapeTo (proc, "busy", 0.9f), "state setup reshape");
    const auto groove = proc.copyGroove();

    juce::MemoryBlock block;
    proc.getStateInformation (block);
    rolla::RollaProcessor restored;
    restored.waitUntilReady();
    restored.setStateInformation (block.getData(), (int) block.getSize());
    expect (std::abs (rawOf (restored, "dark") - 0.2f) < 0.01f, "restore dark");
    expect (std::abs (rawOf (restored, "bassTone") - 0.81f) < 0.01f, "restore tone");
    expect (std::abs (rawOf (restored, "dTune0") + 4.5f) < 0.05f, "restore kick tune");
    expect (! flagOf (restored, "duck"), "restore duck");
    expect ((int) rawOf (restored, "key") == 7, "restore key");
    expect (std::abs (rawOf (restored, "busy") - 0.9f) < 0.01f, "restore busy");
    const auto back = restored.copyGroove();
    expect (hashPart (back, rolla::Bass) == hashPart (groove, rolla::Bass), "restore bass pattern");
    expect (back.generation == groove.generation, "restore does not reshuffle");
}

void testBindings()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    setDenorm (proc, "bassTone", 0.62f);
    const int voices[] = { rolla::Kick, rolla::Snare, rolla::Clap, rolla::Rim, rolla::HatC, rolla::HatO, rolla::Ride };
    float tunes[7], decays[7], levels[7];
    for (int i = 0; i < 7; ++i)
    {
        static const float decayValues[] = { 0.04f, 0.28f, 0.40f, 0.68f, 0.78f, 0.90f, 0.96f };
        tunes[i] = -11.f + (float) i * 1.7f;
        decays[i] = decayValues[i];
        levels[i] = -21.f + (float) i * 3.f;
        const auto id = std::to_string (voices[i]);
        setDenorm (proc, ("dTune" + id).c_str(), tunes[i]);
        setDenorm (proc, ("dDecay" + id).c_str(), decays[i]);
        setDenorm (proc, ("dLevel" + id).c_str(), levels[i]);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    expect (editor != nullptr, "editor");
    if (editor == nullptr)
        return;
    expect (countHelp (*editor, "Drag this lane") == rolla::NumParts, "a drag handle on every lane");
    expect (countHelp (*editor, "Drag all.") == 1, "full drag tile");

    auto checkKnob = [&] (const char* help, const char* id)
    {
        auto* host = findHelp (*editor, help);
        expect (host != nullptr, std::string (help) + " on screen");
        if (host == nullptr)
            return;
        auto* slider = findSlider (*host);
        expect (slider != nullptr, std::string (help) + " slider");
        if (slider == nullptr)
            return;
        expect (std::abs (slider->getValue() - (double) rawOf (proc, id)) < 0.03, std::string (id) + " slider shows the parameter");
        const float next = std::abs (rawOf (proc, id) - 0.25f) < 0.05f ? 0.7f : 0.25f;
        const float denorm = id == std::string ("swing") ? 0.56f : next;
        slider->setValue (denorm, juce::sendNotificationSync);
        expect (std::abs (rawOf (proc, id) - denorm) < 0.03, std::string (id) + " slider writes the parameter");
        setDenorm (proc, id, (float) (denorm > 0.5f ? 0.15f : 0.85f));
        if (std::string (id) == "swing")
            setDenorm (proc, id, 0.51f);
        expect (std::abs (slider->getValue() - (double) rawOf (proc, id)) < 0.03, std::string (id) + " follows the host");
    };
    checkKnob ("Sets how dark", "dark");
    checkKnob ("Sets how many bass", "busy");
    checkKnob ("Sets how early", "bounce");
    checkKnob ("Sets how often the stab", "stab");
    checkKnob ("Brightness", "bassTone");
    checkKnob ("From round", "bassPunch");
    checkKnob ("This lane's volume", "bassLevel");
    checkKnob ("Swing, from 50", "swing");

    auto* duck = findHelp (*editor, "Duck.");
    expect (duck != nullptr, "duck on screen");
    if (auto* button = dynamic_cast<juce::Button*> (duck))
    {
        const bool on = button->getToggleState();
        button->triggerClick();
        expect (pumpUntil ([&] { return flagOf (proc, "duck") != on; }), "duck toggle writes the parameter");
    }

    auto* key = findHelp (*editor, "Key.");
    expect (key != nullptr, "key on screen");
    if (key != nullptr)
    {
        const int before = (int) rawOf (proc, "key");
        if (auto* next = findButton (*key, "Next"))
        {
            next->triggerClick();
            expect (pumpUntil ([&] { return (int) rawOf (proc, "key") == (before + 1) % 12; }),
                    "key stepper writes the parameter");
        }
    }

    auto* style = findHelp (*editor, "Style.");
    expect (style != nullptr, "style on screen");
    if (style != nullptr)
        if (auto* button = findButton (*style, "DEEP"))
        {
            const auto generation = proc.copyGroove().generation;
            button->triggerClick();
            expect (pumpUntil ([&] { return (int) rawOf (proc, "dialect") == rolla::Long; }), "style click");
            expect ((int) rawOf (proc, "dialect") == rolla::Long, "style writes dialect");
            expect (pumpUntil ([&] { return proc.copyGroove().generation != generation; }), "style regenerates");
        }

    std::vector<juce::Slider*> sliders;
    collectSliders (*editor, sliders);
    for (int i = 0; i < 7; ++i)
    {
        const auto id = std::to_string (voices[i]);
        auto findAt = [&] (float value) -> juce::Slider*
        {
            juce::Slider* hit = nullptr;
            int matches = 0;
            for (auto* slider : sliders)
                if (std::abs (slider->getValue() - (double) value) < 0.008)
                {
                    hit = slider;
                    ++matches;
                }
            expect (matches == 1, std::string (rolla::panelVoiceLabel (voices[i])) + " knob bound once at " + std::to_string (value));
            return hit;
        };
        if (auto* tune = findAt (tunes[i]))
        {
            tune->setValue (3.5, juce::sendNotificationSync);
            expect (std::abs (rawOf (proc, ("dTune" + id).c_str()) - 3.5f) < 0.08f,
                    std::string (rolla::panelVoiceLabel (voices[i])) + " tune attachment");
        }
        if (auto* decay = findAt (decays[i]))
        {
            decay->setValue (0.12, juce::sendNotificationSync);
            expect (std::abs (rawOf (proc, ("dDecay" + id).c_str()) - 0.12f) < 0.03f,
                    std::string (rolla::panelVoiceLabel (voices[i])) + " decay attachment");
        }
        if (auto* level = findAt (levels[i]))
        {
            level->setValue (2.5, juce::sendNotificationSync);
            expect (std::abs (rawOf (proc, ("dLevel" + id).c_str()) - 2.5f) < 0.08f,
                    std::string (rolla::panelVoiceLabel (voices[i])) + " level attachment");
        }
    }

    editor.reset();
    pumpUntil ([] { return false; }, 50);
}

bool trackHasChannel (const unsigned char* data, size_t size, int trackIndex, int channel);

void testExport()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    const auto cache = proc.exportCache();
    expect (cache.getFullPathName().contains ("ROLLA"), "cache folder is ROLLA");
    expect (cache.isDirectory(), "cache exists");

    const auto bass = proc.writeLaneMidi (rolla::Bass);
    const auto drums = proc.writeLaneMidi (rolla::Drums);
    expect (bass.size() == 1 && drums.size() == 1, "one file per lane");
    if (bass.isEmpty() || drums.isEmpty())
        return;
    juce::File bassFile (bass[0]);
    juce::File drumFile (drums[0]);
    expect (bassFile.isAChildOf (cache), "lane midi is in the cache");
    expect (bassFile.getFileExtension() == ".mid", "lane midi is .mid");
    expect (bassFile.getSize() > 32 && drumFile.getSize() > 32, "lane midi has bytes");
    expect (bassFile.getSize() != drumFile.getSize() || bass[0] != drums[0], "lanes are not the same file");

    juce::MemoryBlock bytes;
    bassFile.loadFileAsData (bytes);
    const auto* data = static_cast<const unsigned char*> (bytes.getData());
    expect (bytes.getSize() > 14 && data[0] == 'M' && data[1] == 'T' && data[2] == 'h' && data[3] == 'd', "SMF header");
    const int tracks = ((int) data[10] << 8) | (int) data[11];
    expect (tracks == 1, "a lane file is one track");

    const bool pack = packOnDisk (proc);
    const auto wavs = proc.writeLaneAudio (rolla::Bass);
    expect (wavs.size() == 1, "one wav");
    if (wavs.isEmpty())
        return;
    juce::File wavFile (wavs[0]);
    expect (wavFile.getFileExtension() == ".wav", "wav extension");
    expect (wavFile.isAChildOf (cache), "wav is in the cache");
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (wavFile));
    expect (reader != nullptr, "wav reads");
    if (reader != nullptr)
    {
        const double expected = 8.0 * 4.0 * 60.0 / 130.0 * reader->sampleRate;
        expect (reader->sampleRate == 48000.0, "wav rate");
        expect (reader->numChannels == 2, "wav stereo");
        expect (std::abs ((double) reader->lengthInSamples - expected) < 64.0, "wav is 8 bars");
        juce::AudioBuffer<float> audio (2, (int) std::min (reader->lengthInSamples, (juce::int64) 48000));
        reader->read (&audio, 0, audio.getNumSamples(), 0, true, true);
        double sum = 0.0;
        for (int i = 0; i < audio.getNumSamples(); ++i)
            sum += (double) audio.getSample (0, i) * (double) audio.getSample (0, i);
        if (pack)
            expect (std::sqrt (sum / (double) audio.getNumSamples()) > 1.0e-3, "lane wav is not silent");
        else
            std::cout << "lane wav energy skipped: sample pack is not on this machine\n";
    }

    const auto all = proc.writeTempMidi();
    expect (all.size() == 1, "full drag is one multitrack file");
    if (! all.isEmpty())
    {
        juce::File allFile (all[0]);
        expect (allFile.getFileExtension() == ".mid", "full drag is .mid");
        expect (allFile.isAChildOf (cache), "full midi is in the cache");
        juce::MemoryBlock allBytes;
        allFile.loadFileAsData (allBytes);
        const auto* allData = static_cast<const unsigned char*> (allBytes.getData());
        expect (allBytes.getSize() > 14 && allData[0] == 'M' && allData[1] == 'T' && allData[2] == 'h' && allData[3] == 'd',
                "full SMF header");
        const int format = ((int) allData[8] << 8) | (int) allData[9];
        const int allTracks = ((int) allData[10] << 8) | (int) allData[11];
        expect (format == 1, "full export is a type-1 file");
        expect (allTracks == rolla::NumParts + 1, "tempo track plus one track per lane");
        expect (trackHasChannel (allData, allBytes.getSize(), 1, rolla::Bass), "bass is its own track");
        expect (trackHasChannel (allData, allBytes.getSize(), 1 + rolla::Drums, 9), "drums are their own track");
    }

    const auto stems = proc.writeStemAudio();
    expect (stems.size() == rolla::NumParts, "one stem per lane");
    juce::AudioFormatManager stemFormats;
    stemFormats.registerBasicFormats();
    for (int i = 0; i < stems.size(); ++i)
    {
        juce::File stem (stems[i]);
        expect (stem.getFileExtension() == ".wav", std::string (rolla::partId (i)) + " stem is wav");
        expect (stem.isAChildOf (cache), std::string (rolla::partId (i)) + " stem is in the cache");
        std::unique_ptr<juce::AudioFormatReader> stemReader (stemFormats.createReaderFor (stem));
        expect (stemReader != nullptr, std::string (rolla::partId (i)) + " stem reads");
        if (stemReader == nullptr)
            continue;
        const double expected = 8.0 * 4.0 * 60.0 / 130.0 * stemReader->sampleRate;
        expect (stemReader->numChannels == 2, std::string (rolla::partId (i)) + " stem stereo");
        expect (std::abs ((double) stemReader->lengthInSamples - expected) < 64.0, std::string (rolla::partId (i)) + " stem is 8 bars");
        if (pack && (i == rolla::Bass || i == rolla::Stab || i == rolla::Drums))
        {
            // The stab answers on the odd bars, so the first second of an 8-bar stem
            // can be silence even when the lane rendered. Measure the whole file.
            const int n = (int) std::min (stemReader->lengthInSamples, (juce::int64) 800000);
            juce::AudioBuffer<float> audio (2, std::max (1, n));
            stemReader->read (&audio, 0, n, 0, true, true);
            double sum = 0.0;
            for (int s = 0; s < n; ++s)
                sum += (double) audio.getSample (0, s) * (double) audio.getSample (0, s);
            expect (std::sqrt (sum / (double) std::max (1, n)) > 1.0e-3,
                    std::string (rolla::partId (i)) + " stem is not silent");
        }
    }
}

bool trackHasChannel (const unsigned char* data, size_t size, int trackIndex, int channel)
{
    if (data == nullptr || size < 14)
        return false;
    size_t pos = 14;
    for (int index = 0; pos + 8 <= size; ++index)
    {
        if (data[pos] != 'M' || data[pos + 1] != 'T' || data[pos + 2] != 'r' || data[pos + 3] != 'k')
            return false;
        const size_t len = ((size_t) data[pos + 4] << 24) | ((size_t) data[pos + 5] << 16)
                         | ((size_t) data[pos + 6] << 8) | (size_t) data[pos + 7];
        pos += 8;
        if (pos + len > size)
            return false;
        if (index == trackIndex)
        {
            size_t i = pos;
            const size_t end = pos + len;
            int status = 0;
            while (i < end)
            {
                int guard = 0;
                while (i < end && (data[i] & 0x80) != 0 && guard++ < 4)
                    ++i;
                if (i >= end)
                    break;
                ++i;
                if (i >= end)
                    break;
                if ((data[i] & 0x80) != 0)
                    status = data[i++];
                if (i >= end)
                    break;
                const int cmd = status & 0xf0;
                if (cmd == 0x90 || cmd == 0x80)
                {
                    const int vel = i + 1 < end ? data[i + 1] : 0;
                    if (cmd == 0x90 && (status & 0x0f) == channel && vel > 0)
                        return true;
                    i += 2;
                }
                else if (cmd == 0xc0 || cmd == 0xd0)
                    ++i;
                else if (status == 0xff)
                {
                    if (++i >= end)
                        break;
                    const int n = data[i++];
                    i += (size_t) n;
                }
                else
                    i += 2;
            }
            return false;
        }
        pos += len;
    }
    return false;
}

void testStabDice()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    std::uint64_t frozen[rolla::NumParts];
    for (int i = 0; i < rolla::NumParts; ++i)
        frozen[i] = hashPart (proc.copyGroove(), i);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    expect (editor != nullptr, "dice editor");
    if (editor == nullptr)
        return;
    auto* row = findHelp (*editor, "Open STAB sound");
    expect (row != nullptr, "stab row");
    auto* dice = row != nullptr ? findHelp (*row, "Shuffle this lane") : nullptr;
    expect (dice != nullptr, "stab dice");

    std::uint64_t stab = frozen[rolla::Stab];
    for (int n = 0; n < 3; ++n)
    {
        if (auto* button = dynamic_cast<juce::Button*> (dice))
            button->triggerClick();
        else
            proc.triggerShuffle (rolla::Stab);
        expect (pumpUntil ([&] { return hashPart (proc.copyGroove(), rolla::Stab) != stab; }),
                "stab dice changes only the stab pattern");
        const auto groove = proc.copyGroove();
        stab = hashPart (groove, rolla::Stab);
        for (int i = 0; i < rolla::NumParts; ++i)
            if (i != rolla::Stab)
                expect (hashPart (groove, i) == frozen[i],
                        std::string ("stab dice leaves ") + rolla::partId (i) + " on roll " + std::to_string (n + 1));
    }

    const auto before = proc.writeLaneMidi (rolla::Stab);
    expect (before.size() == 1, "drag the re-rolled stab");
    editor.reset();
}

// Every string the editor draws goes through rolla::drawLabel. Paint the editor in
// several states at the default (1024x640) and minimum (768x480) sizes and check:
//  - point size >= Theme::minTextPoint (design space; the content is one scale transform),
//  - the measured string fits its box, unless the call allows an ellipsis,
//  - the drawn text (box narrowed to the string, height = point size) lies inside the clip,
//    i.e. inside the painting component's bounds,
//  - the processor UI notice is drawn when set, and the KEY / SCALE pills don't overlap.
void testTextFit()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    std::unique_ptr<juce::AudioProcessorEditor> base (proc.createEditor());
    auto* editor = dynamic_cast<rolla::RollaEditor*> (base.get());
    expect (editor != nullptr, "text fit: editor");
    if (editor == nullptr)
        return;

    for (int r = 0; r <= (int) rolla::Theme::Role::keycap; ++r)
        expect (rolla::Theme::pointSize ((rolla::Theme::Role) r) >= rolla::Theme::minTextPoint,
                "role " + std::to_string (r) + " is at least the minimum point size");

    int records = 0;
    std::vector<std::string> seen;
    std::string where;
    rolla::textAudit() = [&] (const rolla::TextRecord& t)
    {
        ++records;
        seen.push_back (t.text.toStdString());
        const auto label = where + " '" + t.text.toStdString() + "' box "
                         + t.box.toString().toStdString() + " width " + std::to_string (t.width);
        expect (t.point >= rolla::Theme::minTextPoint - 0.01f, label + " below the minimum point size");
        const float drawnW = t.ellipsis ? std::min (t.width, t.box.getWidth()) : t.width;
        if (! t.ellipsis)
            expect (t.width <= t.box.getWidth() + 0.5f, label + " is clipped (wider than its box)");
        const auto drawn = t.justification.appliedToRectangle (
            juce::Rectangle<float> (0.f, 0.f, std::min (drawnW, t.box.getWidth()), t.point), t.box);
        expect (t.clip.expanded (1.f).contains (drawn),
                label + " runs outside its component (clip " + t.clip.toString().toStdString() + ")");
    };

    auto paintAll = [&] (const std::string& state)
    {
        pumpUntil ([] { return false; }, 120); // editor timer: sync() picks up parameter changes
        for (auto size : { juce::Point<int> (1024, 640), juce::Point<int> (768, 480) })
        {
            editor->setSize (size.x, size.y);
            where = state + " @" + std::to_string (size.x) + "x" + std::to_string (size.y);
            juce::Image image (juce::Image::ARGB, size.x, size.y, true);
            juce::Graphics g (image);
            editor->paintEntireComponent (g, true);

            const float scale = (float) size.x / 1024.f;
            for (int i = 0; i < editor->getNumChildComponents(); ++i)
                if (auto* text = dynamic_cast<juce::Label*> (editor->getChildComponent (i)))
                    if (text->isVisible())
                        expect (text->getFont().getHeightInPoints() / scale >= rolla::Theme::minTextPoint - 0.05f,
                                where + " banner below the minimum point size");
        }
    };

    paintAll ("closed");
    expect (records > 40, "text fit: the editor drew its labels through drawLabel (" + std::to_string (records) + ")");

    for (int lane : { rolla::Bass, rolla::Hits, rolla::Vox, rolla::Drums })
    {
        editor->selectLane (lane);
        paintAll (std::string ("open ") + rolla::laneShortName (lane));
    }

    // Long captions: six lanes kept, low intensity (lanes out), key and scale locked.
    for (int i = 0; i < rolla::Drums; ++i)
        setDenorm (proc, (std::string (rolla::partId (i)) + "Keep").c_str(), 1.f);
    setDenorm (proc, "intensity", 0.f);
    setDenorm (proc, "keyLock", 1.f);
    setDenorm (proc, "key", 3.f);
    setDenorm (proc, "scale", 5.f);
    editor->selectLane (rolla::Drums);
    paintAll ("kept + locked");

    // Guard notice in the status strip.
    const juce::String notice ("NO SAFE FILE: STAB muted in D# Phrygian");
    proc.setUiNotice (notice);
    expect (proc.getUiNotice() == notice, "ui notice round trip");
    seen.clear();
    paintAll ("notice");
    expect (std::find (seen.begin(), seen.end(), notice.toStdString()) != seen.end(), "ui notice is drawn");
    proc.clearUiNotice();
    seen.clear();
    paintAll ("notice cleared");
    expect (std::find (seen.begin(), seen.end(), notice.toStdString()) == seen.end(), "cleared ui notice is gone");

    // Header controls sit on the token grid without overlapping.
    // The scaled content is the editor's 1024x640 child; header controls end above the y 55 rule.
    std::vector<juce::Component*> header;
    for (int i = 0; i < editor->getNumChildComponents(); ++i)
    {
        auto* content = editor->getChildComponent (i);
        if (dynamic_cast<juce::Label*> (content) != nullptr || content->getWidth() != 1024)
            continue;
        for (int k = 0; k < content->getNumChildComponents(); ++k)
        {
            auto* child = content->getChildComponent (k);
            if (child->isVisible() && child->getBottom() <= 56)
                header.push_back (child);
        }
    }
    expect (header.size() >= 7, "header controls found (" + std::to_string (header.size()) + ")");
    for (size_t a = 0; a < header.size(); ++a)
    {
        expect (header[a]->getX() >= 16 && header[a]->getRight() <= 1008,
                "header control inside the 16 px margins: " + header[a]->getBounds().toString().toStdString());
        for (size_t b = a + 1; b < header.size(); ++b)
            expect (! header[a]->getBounds().intersects (header[b]->getBounds()),
                    "header controls overlap: " + header[a]->getBounds().toString().toStdString()
                        + " / " + header[b]->getBounds().toString().toStdString());
    }

    rolla::textAudit() = nullptr;
    base.reset();
    pumpUntil ([] { return false; }, 50);
}

} // namespace

int main()
{
    // Never the real user profile: settings, caches and sample folders go to a scratch folder.
    if (rolla::test::useScratchProfile ("knob").empty())
    {
        std::cerr << "could not set up a scratch profile\n";
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI juce;
    testTextFit();
    testBindings();
    testBusyDensity();
    testGeneratorControls();
    testState();
    testExport();
    testStabDice();
    testSoundControls();
    if (fails == 0)
        std::cout << "knob tests passed\n";
    else
        std::cerr << fails << " knob test failure(s)\n";
    return fails == 0 ? 0 : 1;
}
