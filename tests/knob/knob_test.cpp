#include "PluginProcessor.h"
#include "UiTokens.h"

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

    for (const char* id : { "dark", "busy", "bounce", "stab", "swing", "duck", "key", "dialect",
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
    checkKnob ("Dark.", "dark");
    checkKnob ("Busy.", "busy");
    checkKnob ("Bounce.", "bounce");
    checkKnob ("Stab.", "stab");
    checkKnob ("Tone.", "bassTone");
    checkKnob ("Punch.", "bassPunch");
    checkKnob ("Level,", "bassLevel");
    checkKnob ("Swing,", "swing");

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
        if (auto* button = findButton (*style, "LONG"))
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
    expect (bassFile.getParentDirectory() == cache, "lane midi is in the cache");
    expect (bassFile.getFileExtension() == ".mid", "lane midi is .mid");
    expect (bassFile.getSize() > 32 && drumFile.getSize() > 32, "lane midi has bytes");
    expect (bassFile.getSize() != drumFile.getSize() || bass[0] != drums[0], "lanes are not the same file");

    juce::MemoryBlock bytes;
    bassFile.loadFileAsData (bytes);
    const auto* data = static_cast<const unsigned char*> (bytes.getData());
    expect (bytes.getSize() > 14 && data[0] == 'M' && data[1] == 'T' && data[2] == 'h' && data[3] == 'd', "SMF header");
    const int tracks = ((int) data[10] << 8) | (int) data[11];
    expect (tracks == 1, "a lane file is one track");

    const auto wavs = proc.writeLaneAudio (rolla::Bass);
    expect (wavs.size() == 1, "one wav");
    if (wavs.isEmpty())
        return;
    juce::File wavFile (wavs[0]);
    expect (wavFile.getFileExtension() == ".wav", "wav extension");
    expect (wavFile.getParentDirectory() == cache, "wav is in the cache");
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
        expect (std::sqrt (sum / (double) audio.getNumSamples()) > 1.0e-3, "lane wav is not silent");
    }

    const auto all = proc.writeTempMidi();
    expect (all.size() >= rolla::NumParts + 1, "all-tracks drag still writes every lane");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;
    testBindings();
    testGeneratorControls();
    testState();
    testExport();
    testSoundControls();
    if (fails == 0)
        std::cout << "knob tests passed\n";
    else
        std::cerr << fails << " knob test failure(s)\n";
    return fails == 0 ? 0 : 1;
}
