// rolla-bundle-test: ROLLA plays the sample pack built into its bundle.
//
//   rolla-bundle-test
//
// Built with the plugin's rules (ROLLA_PLUGIN_BUILD=1: samples only, no synth,
// no circuit kit) plus ROLLA_TEST_HOOKS=1, so ROLLA_BUNDLE_ROOT can stand in for
// <bundle>/Contents/Resources/Samples. It runs on a scratch profile
// (tests/common/ScratchProfile.h) and never opens a real user's settings,
// caches or sample folder. The tone pack it writes lives in that scratch folder.
//
//  (a) bundle first: with a temp root saved in ROLLA.settings, the processor
//      resolves to <scratch>/ROLLA.vst3/Contents/Resources/Samples and indexes it
//  (b) one bar per lane (BASS STAB HITS PLUCK PAD VOX) and per drum voice, through
//      the processor's library, index and presets: not silent, and the file the
//      lane picked is in that lane's folder, distinct across lanes. Then a roll
//      played through processBlock picks every sounding lane from the bundle.
//  (c) empty bundle, no saved root: every lane is silent (< -90 dBFS), nothing
//      falls back to a synth, and samplesStatus() reports the error
//  (d) setSamplesRoot (a temp folder) plays it but leaves ROLLA.settings as it was
//  (e) fidelity: a stereo kick, closed hat and stab at default knobs, rendered
//      solo through the sampler, match their files: every 1/3-octave band from
//      50 Hz to 16 kHz within 1.5 dB (after level match) and the same L/R
//      correlation (within 0.02)

#include "PluginProcessor.h"
#include "Engine/RollaSettings.h"
#include "Engine/Sampler.h"
#include "Drums/DrumSampleSlot.h"
#include "../common/ScratchProfile.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <random>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#if JUCE_LINUX
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
}
#endif

#if ! ROLLA_PLUGIN_BUILD || ! ROLLA_TEST_HOOKS
 #error "rolla-bundle-test needs ROLLA_PLUGIN_BUILD=1 and ROLLA_TEST_HOOKS=1"
#endif

namespace
{
int fails = 0;
constexpr double kRate = 48000.0;
constexpr double kBpm = 130.0;
constexpr float kSilent = 3.1623e-5f; // -90 dBFS
constexpr float kAudible = 1.0e-3f;   // -60 dBFS

void expect (bool ok, const std::string& msg)
{
    std::cout << (ok ? "PASS " : "FAIL ") << msg << "\n";
    if (! ok)
        ++fails;
}

std::string dbfs (float peak)
{
    if (peak <= 0.f)
        return "-inf dBFS";
    return juce::String (20.0 * std::log10 ((double) peak), 1).toStdString() + " dBFS";
}

bool pumpUntil (const std::function<bool()>& done, int ms = 15000)
{
    const auto start = juce::Time::getMillisecondCounter();
    while (! done() && juce::Time::getMillisecondCounter() - start < (juce::uint32) ms)
    {
#if JUCE_LINUX
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (1);
#else
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
#endif
    }
    return done();
}

void writeTone (const juce::File& file, double seconds, float hz, float amplitude, float decay)
{
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    auto fileStream = std::make_unique<juce::FileOutputStream> (file);
    if (fileStream == nullptr || ! fileStream->openedOk())
        return;
    std::unique_ptr<juce::OutputStream> stream (fileStream.release());
    const auto options = juce::AudioFormatWriterOptions().withSampleRate (kRate)
                                                          .withNumChannels (1)
                                                          .withBitsPerSample (16);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return;
    const int n = (int) std::lround (seconds * kRate);
    juce::AudioBuffer<float> buf (1, n);
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / (float) kRate;
        buf.setSample (0, i, amplitude * std::exp (-decay * t) * std::sin (2.f * 3.14159265f * hz * t));
    }
    writer->writeFromAudioSampleBuffer (buf, 0, n);
}

// The same layout CI's tone pack uses (tests/roll/renderer_smoke.py build_pack),
// written here so the test needs nothing from outside. Roots in D.
void buildPack (const juce::File& root)
{
    struct Pitched { const char* folder; const char* stem; const char* note; float hz; };
    const Pitched pitched[] = {
        { "BASS", "bass", "D1", 41.2f }, { "STAB", "stab", "D4", 293.7f }, { "PLUCK", "pluck", "D5", 587.3f },
        { "PAD", "pad", "D4", 293.7f },  { "HITS", "warehouse", "D4", 293.7f },
    };
    for (const auto& p : pitched)
        for (int i = 0; i < 3; ++i)
            writeTone (root.getChildFile (p.folder).getChildFile (juce::String (p.stem) + "_0" + juce::String (i) + "_" + p.note + ".wav"),
                       1.0, p.hz * (1.f + 0.01f * (float) i), 0.3f, 0.5f);
    for (int i = 0; i < 3; ++i)
    {
        writeTone (root.getChildFile ("VOX/chops/chop_0" + juce::String (i) + "_Fs4.wav"), 0.8, 370.f, 0.2f, 0.5f);
        writeTone (root.getChildFile ("VOX/phrases/phrase_0" + juce::String (i) + "_Fs4.wav"), 0.8, 370.f, 0.2f, 0.5f);
        writeTone (root.getChildFile ("PAD/atmospheres/air_0" + juce::String (i) + "_D4.wav"), 1.0, 293.7f, 0.2f, 0.5f);
    }
    struct Drum { const char* folder; float hz; };
    const Drum drums[] = { { "KICK", 220.f }, { "SNARE", 280.f }, { "CLAP", 420.f }, { "RIM", 900.f },
                           { "CH", 1400.f },  { "OH", 700.f },    { "RIDE", 500.f }, { "PERC", 360.f } };
    for (const auto& d : drums)
        for (int i = 0; i < 3; ++i)
            writeTone (root.getChildFile ("DRUMS").getChildFile (d.folder)
                           .getChildFile (juce::String (d.folder).toLowerCase() + "_0" + juce::String (i) + ".wav"),
                       0.3, d.hz * (1.f + 0.02f * (float) i), 0.5f, 8.f);
}

const char* laneFolder (int part)
{
    switch (part)
    {
        case rolla::Bass: return "BASS/";
        case rolla::Stab: return "STAB/";
        case rolla::Hits: return "HITS/";
        case rolla::Pluck: return "PLUCK/";
        case rolla::Pad: return "PAD/";
        case rolla::Vox: return "VOX/";
        default: return "DRUMS/";
    }
}

std::string drumFolder (int voice)
{
    return std::string ("DRUMS/") + rolla::drumSampleFolder (voice) + "/";
}

bool startsWith (const std::string& s, const std::string& prefix)
{
    return s.size() > prefix.size() && s.compare (0, prefix.size(), prefix) == 0;
}

void putEnv (const char* name, const juce::File& f)
{
    rolla::test::putEnv (name, f.getFullPathName().toStdString());
}

// The engine's view of the processor: its library, index, presets and the preset
// each lane has selected. The test's own Sampler plays from exactly that.
rolla::EngineParams paramsFor (rolla::RollaProcessor& proc, const rolla::PresetBank* bank,
                               const rolla::SampleIndex* index)
{
    rolla::EngineParams params;
    params.presets = bank;
    params.library = &proc.sampleLibrary();
    params.sampleIndex = index;
    params.bpm = (float) kBpm;
    auto raw = [&] (const juce::String& id, float fallback)
    {
        auto* v = proc.apvts.getRawParameterValue (id);
        return v != nullptr ? v->load() : fallback;
    };
    for (int p = 0; p < rolla::NumParts; ++p)
    {
        const juce::String id (rolla::partId (p));
        params.preset[p] = (int) raw (id + "Preset", 0.f);
        params.tone[p] = raw (id + "Tone", 0.5f);
        params.punch[p] = raw (id + "Punch", 0.5f);
    }
    for (int v = 0; v < rolla::NumDrumVoices; ++v)
    {
        const juce::String id (v);
        params.drumTuneSt[v] = raw ("dTune" + id, 0.f);
        params.drumDecay[v] = raw ("dDecay" + id, 0.5f);
        params.drumLevelDb[v] = raw ("dLevel" + id, 0.f);
    }
    if (auto* v = proc.apvts.getRawParameterValue ("dialect"))
        params.dialect = (int) v->load();
    return params;
}

// Starts one note (or drum hit), waiting for the file to load, then renders one bar.
// Returns the peak of the lane's bus over that bar.
float renderOneBar (const rolla::PresetBank& bank, const rolla::EngineParams& params,
                    int part, int drumVoice, std::string& picked)
{
    rolla::Sampler sampler;
    sampler.prepare (kRate);
    const int notes[rolla::NumParts] = { 38, 62, 62, 74, 62, 66, 60 };
    const auto start = juce::Time::getMillisecondCounter();
    picked.clear();
    while (picked.empty() && juce::Time::getMillisecondCounter() - start < 8000)
    {
        sampler.reset();
        if (drumVoice >= 0)
            sampler.takeDrum (drumVoice, 110, bank, params);
        else
            sampler.take (part, notes[part], 110, 16.f, bank, params);
        picked = sampler.lastPickedFile (part, drumVoice);
        if (picked.empty())
            juce::Thread::sleep (5);
    }
    const int bar = (int) std::lround (4.0 * 60.0 / kBpm * kRate);
    float peak = 0.f;
    for (int i = 0; i < bar; ++i)
    {
        float laneL[rolla::NumParts] {};
        float laneR[rolla::NumParts] {};
        sampler.render (laneL, laneR, 0.f, 0.f, false);
        peak = std::max ({ peak, std::fabs (laneL[part]), std::fabs (laneR[part]) });
    }
    return peak;
}

// Plays `loops` loops of the current groove through processBlock. Returns the output peak.
float playLoops (rolla::RollaProcessor& proc, int loops)
{
    struct Head : juce::AudioPlayHead
    {
        double ppq = 0.0;
        juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
        {
            juce::AudioPlayHead::PositionInfo info;
            info.setBpm (kBpm);
            info.setIsPlaying (true);
            info.setPpqPosition (ppq);
            return info;
        }
    } head;
    proc.setPlayHead (&head);
    proc.releaseResources();
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::disabled());
    proc.setBusesLayout (layout);
    proc.prepareToPlay (kRate, 256);
    const auto g = proc.copyGroove();
    const int total = (int) std::lround ((double) (g.bars * 4 * loops) * 60.0 / kBpm * kRate) + proc.getLatencySamples();
    juce::AudioBuffer<float> buffer (2, 256);
    float peak = 0.f;
    for (int done = 0; done < total; done += 256)
    {
        buffer.clear();
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
        peak = std::max (peak, buffer.getMagnitude (0, 256));
        head.ppq += 256.0 * kBpm / (60.0 * kRate);
        // Real time for the loader thread, as a host would give it.
        if ((done / 256) % 64 == 0)
            juce::Thread::sleep (1);
    }
    proc.setPlayHead (nullptr);
    return peak;
}

float wavPeak (const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return -1.f;
    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) reader->lengthInSamples);
    reader->read (&buf, 0, (int) reader->lengthInSamples, 0, true, true);
    return buf.getMagnitude (0, buf.getNumSamples());
}

juce::MemoryBlock fileBytes (const juce::File& f)
{
    juce::MemoryBlock mb;
    if (f.existsAsFile())
        f.loadFileAsData (mb);
    return mb;
}

void testResolve (const juce::File& scratch, const juce::File& bundle, const juce::File& savedTemp)
{
    using rolla::SampleLibrary;
    expect (SampleLibrary::bundledRoot() == bundle, "bundledRoot is " + bundle.getFullPathName().toStdString());
    expect (bundle.getFullPathName().endsWith ("ROLLA.vst3/Contents/Resources/Samples")
                || bundle.getFullPathName().endsWith ("ROLLA.vst3\\Contents\\Resources\\Samples"),
            "the stand-in bundle has the plugin's layout");
    expect (SampleLibrary::resolveSampleRoot (savedTemp.getFullPathName()) == bundle,
            "resolve: bundle with audio wins over a saved root");
    expect (SampleLibrary::resolveSampleRoot ({}) == bundle, "resolve: bundle with audio, nothing saved");

    const auto empty = scratch.getChildFile ("empty.vst3/Contents/Resources/Samples");
    empty.createDirectory();
    putEnv ("ROLLA_BUNDLE_ROOT", empty);
    expect (SampleLibrary::resolveSampleRoot (savedTemp.getFullPathName()) == SampleLibrary::defaultRoot(),
            "resolve: a saved root under a temp folder is never used");
    expect (SampleLibrary::resolveSampleRoot (scratch.getChildFile ("gone").getFullPathName()) == SampleLibrary::defaultRoot(),
            "resolve: a saved root that does not exist falls to ~/Music/ROLLA/Samples");
    putEnv ("ROLLA_BUNDLE_ROOT", bundle);

    expect (rolla::settings::isTempLocation (juce::File ("/tmp/rolla-tone-pack"))
                && rolla::settings::isTempLocation (juce::File ("/var/folders/ab/T/x"))
                && rolla::settings::isTempLocation (savedTemp),
            "temp folders are recognised");
    expect (! rolla::settings::isTempLocation (juce::File ("/Users/someone/Music/ROLLA/Samples"))
                && ! rolla::settings::isTempLocation (juce::File ("/home/someone/Music/ROLLA/Samples")),
            "a music folder is not a temp folder");
}

// (a) and (b)
void testBundled (const juce::File& bundle, const juce::File& savedTemp)
{
    const auto settingsFile = rolla::settings::file();
    {
        // As if an earlier test run had saved its tone pack as the user's root.
        auto props = rolla::settings::open();
        props->setValue ("samplesRoot", savedTemp.getFullPathName());
        props->saveIfNeeded();
    }
    const auto before = fileBytes (settingsFile);

    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    pumpUntil ([&] { return proc.samplesFound() > 0; });
    const auto status = proc.samplesStatus();
    std::cout << "active root: " << status.root.getFullPathName() << " (" << status.found << " files)\n";
    expect (proc.samplesRoot() == bundle, "(a) the processor plays from Contents/Resources/Samples");
    expect (proc.samplesFound() > 0, "(a) samplesFound() > 0 (" + std::to_string (proc.samplesFound()) + ")");
    expect (status.bundled && status.message.isEmpty(), "(a) status: bundled pack, no banner");
    expect (fileBytes (settingsFile) == before, "(a) startup leaves ROLLA.settings as it was");
    expect (! bundle.getChildFile ("USER").exists() && ! bundle.getChildFile ("BASS").getChildFile ("USER").exists(),
            "(a) nothing is written into the bundle");

    // (b) one bar per lane and per drum voice, from the bundle.
    const auto bank = proc.sharePresets();
    const auto index = proc.sampleLibrary().index();
    expect (bank != nullptr && index != nullptr, "(b) presets and index are published");
    if (bank == nullptr || index == nullptr)
        return;
    const auto params = paramsFor (proc, bank.get(), index.get());
    std::set<std::string> picks;
    const int lanes[] = { rolla::Bass, rolla::Stab, rolla::Hits, rolla::Pluck, rolla::Pad, rolla::Vox };
    for (int part : lanes)
    {
        std::string picked;
        const float peak = renderOneBar (*bank, params, part, -1, picked);
        const std::string tag = std::string ("(b) ") + rolla::partId (part);
        std::cout << "  " << rolla::partId (part) << ": " << (picked.empty() ? "(nothing)" : picked) << ", peak " << dbfs (peak) << "\n";
        expect (startsWith (picked, laneFolder (part)), tag + " picks a file from " + laneFolder (part));
        expect (bundle.getChildFile (picked).existsAsFile(), tag + " file is inside the bundle");
        expect (peak > kAudible, tag + " one bar is not silent (" + dbfs (peak) + ")");
        picks.insert (picked);
    }
    expect (picks.size() == 6, "(b) six melodic lanes, six different files");

    std::set<std::string> drumPicks;
    for (int voice = 0; voice < rolla::NumDrumVoices; ++voice)
    {
        std::string picked;
        const float peak = renderOneBar (*bank, params, rolla::Drums, voice, picked);
        const std::string tag = std::string ("(b) drum ") + rolla::drumVoiceKey (voice);
        std::cout << "  drum " << rolla::drumVoiceKey (voice) << ": " << (picked.empty() ? "(nothing)" : picked) << ", peak " << dbfs (peak) << "\n";
        expect (startsWith (picked, drumFolder (voice)), tag + " picks a file from " + drumFolder (voice));
        expect (peak > kAudible, tag + " one bar is not silent (" + dbfs (peak) + ")");
        if (voice != rolla::TomLow && voice != rolla::TomMid)
            drumPicks.insert (picked);
    }
    expect (drumPicks.size() == 8, "(b) eight drum folders, eight different files");

    // The live engine: a roll played through processBlock picks every sounding lane from the bundle.
    const auto g = proc.copyGroove();
    const float out = playLoops (proc, 2);
    expect (out > kAudible, "(b) processBlock output is not silent (" + dbfs (out) + ")");
    for (int part : lanes)
    {
        const auto picked = proc.lastPickedFile (part);
        if (g.lane[part].count == 0)
            continue;
        expect (startsWith (picked, laneFolder (part)),
                std::string ("(b) live ") + rolla::partId (part) + " played " + (picked.empty() ? "(nothing)" : picked));
    }
    for (int voice = 0; voice < rolla::NumDrumVoices; ++voice)
    {
        bool hits = false;
        for (int s = 0; s < rolla::kMaxSteps; ++s)
            hits = hits || g.drums.vel[voice][s] > 0;
        if (! hits)
            continue;
        const auto picked = proc.lastPickedFile (rolla::Drums, voice);
        expect (startsWith (picked, drumFolder (voice)),
                std::string ("(b) live drum ") + rolla::drumVoiceKey (voice) + " played " + (picked.empty() ? "(nothing)" : picked));
    }
}

// (c) and (d)
void testEmpty (const juce::File& scratch, const juce::File& tonePack)
{
    const auto empty = scratch.getChildFile ("empty.vst3/Contents/Resources/Samples");
    empty.createDirectory();
    putEnv ("ROLLA_BUNDLE_ROOT", empty);
    const auto settingsFile = rolla::settings::file();
    {
        auto props = rolla::settings::open();
        props->removeValue ("samplesRoot");
        props->saveIfNeeded();
    }

    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    pumpUntil ([&] { return proc.samplesFound() >= 0; });
    const auto status = proc.samplesStatus();
    std::cout << "banner: " << status.message << "\n";
    expect (proc.samplesRoot() == rolla::SampleLibrary::defaultRoot(), "(c) no bundle, nothing saved: ~/Music/ROLLA/Samples");
    expect (status.found == 0, "(c) no files found");
    expect (! status.bundled && status.message.startsWith ("Samples not loaded: ")
                && status.message.contains (status.root.getFullPathName()) && status.message.contains ("(0 files)"),
            "(c) samplesStatus() reports the error");
    expect (! empty.getChildFile ("BASS").exists(), "(c) no lane folders are made inside a bundle");

    // Sampler level: nothing loaded, nothing sounds, nothing hands the note to a synth.
    {
        const auto bank = proc.sharePresets();
        const auto index = proc.sampleLibrary().index();
        auto params = paramsFor (proc, bank.get(), index.get());
        rolla::Sampler sampler;
        sampler.prepare (kRate);
        bool allSilent = true;
        for (int part = 0; part < rolla::Drums; ++part)
        {
            const auto take = sampler.take (part, 60, 110, 16.f, *bank, params);
            allSilent = allSilent && take.kind == 1 && take.synthGain == 0.f;
        }
        for (int voice = 0; voice < rolla::NumDrumVoices; ++voice)
            allSilent = allSilent && sampler.takeDrum (voice, 110, *bank, params) == 0.f;
        expect (allSilent, "(c) take/takeDrum with no samples: no synth gain on any lane or drum voice");
        params.library = nullptr;
        params.sampleIndex = nullptr;
        bool noLibrary = true;
        for (int part = 0; part < rolla::Drums; ++part)
        {
            const auto take = sampler.take (part, 60, 110, 16.f, *bank, params);
            noLibrary = noLibrary && take.kind == 1 && take.synthGain == 0.f;
        }
        for (int voice = 0; voice < rolla::NumDrumVoices; ++voice)
            noLibrary = noLibrary && sampler.takeDrum (voice, 110, *bank, params) == 0.f;
        expect (noLibrary, "(c) take/takeDrum with no library: silent too");
        float peak = 0.f;
        for (int i = 0; i < 48000; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            sampler.render (laneL, laneR, 0.f, 0.f, false);
            for (int p = 0; p < rolla::NumParts; ++p)
                peak = std::max ({ peak, std::fabs (laneL[p]), std::fabs (laneR[p]) });
        }
        expect (peak < kSilent, "(c) sampler output silent (" + dbfs (peak) + ")");
    }

    // Plugin level: the whole mix and every stem.
    const float out = playLoops (proc, 1);
    expect (out < kSilent, "(c) processBlock output silent, no synth (" + dbfs (out) + ")");
    for (int part = 0; part < rolla::NumParts; ++part)
    {
        const auto files = proc.writeLaneAudio (part);
        const float peak = files.isEmpty() ? -1.f : wavPeak (juce::File (files[0]));
        expect (peak >= 0.f && peak < kSilent,
                std::string ("(c) ") + rolla::partId (part) + " stem silent (" + (peak < 0.f ? std::string ("no file") : dbfs (peak)) + ")");
    }

    // (d) a temp folder plays for the session but is never saved.
    const auto before = fileBytes (settingsFile);
    proc.setSamplesRoot (tonePack);
    pumpUntil ([&] { return proc.samplesFound() > 0; });
    expect (proc.samplesRoot() == tonePack && proc.samplesFound() > 0, "(d) setSamplesRoot (temp) plays that folder");
    expect (fileBytes (settingsFile) == before, "(d) ROLLA.settings is unchanged");
    expect (rolla::settings::open()->getValue ("samplesRoot").isEmpty(), "(d) no samplesRoot saved");
}

// (e) ------------------------------------------------------------------------

struct Stereo
{
    std::vector<float> l, r;
};

void writeStereo (const juce::File& file, const Stereo& x)
{
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    auto fileStream = std::make_unique<juce::FileOutputStream> (file);
    if (fileStream == nullptr || ! fileStream->openedOk())
        return;
    std::unique_ptr<juce::OutputStream> stream (fileStream.release());
    const auto options = juce::AudioFormatWriterOptions().withSampleRate (kRate)
                                                          .withNumChannels (2)
                                                          .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return;
    const int n = (int) x.l.size();
    juce::AudioBuffer<float> buf (2, n);
    for (int i = 0; i < n; ++i)
    {
        buf.setSample (0, i, x.l[(size_t) i]);
        buf.setSample (1, i, x.r[(size_t) i]);
    }
    writer->writeFromAudioSampleBuffer (buf, 0, n);
}

// Kick: a 150 -> 45 Hz sweep with a long tail and a short noise click,
// the click different per side.
Stereo makeKick()
{
    std::mt19937 rng (7);
    std::normal_distribution<float> noise (0.f, 1.f);
    const int n = (int) (0.9 * kRate);
    Stereo x { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kRate;
        const double hz = 45.0 + 105.0 * std::exp (-t / 0.04);
        phase += 2.0 * juce::MathConstants<double>::pi * hz / kRate;
        const float body = (float) (0.7 * std::exp (-t / 0.22) * std::sin (phase));
        const float clickEnv = (float) std::exp (-t / 0.004);
        x.l[(size_t) i] = body + 0.25f * clickEnv * noise (rng);
        x.r[(size_t) i] = body + 0.25f * clickEnv * noise (rng);
    }
    return x;
}

// Closed hat: bright noise, half shared and half per side, 70 ms decay.
Stereo makeHat()
{
    std::mt19937 rng (11);
    std::normal_distribution<float> noise (0.f, 1.f);
    const int n = (int) (0.35 * kRate);
    Stereo x { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    float prevC = 0.f, prevL = 0.f, prevR = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float env = (float) std::exp (-(i / kRate) / 0.07);
        const float c = noise (rng), a = noise (rng), b = noise (rng);
        // First differences: a gentle high-pass tilt, as a hat has.
        const float hc = c - prevC, ha = a - prevL, hb = b - prevR;
        prevC = c;
        prevL = a;
        prevR = b;
        x.l[(size_t) i] = 0.12f * env * (hc + 0.8f * ha);
        x.r[(size_t) i] = 0.12f * env * (hc + 0.8f * hb);
    }
    return x;
}

// Stab: a D minor triad (D4 F4 A4), 8 partials each, R detuned 4 cents, 1.2 s.
Stereo makeStab()
{
    const int n = (int) (1.2 * kRate);
    Stereo x { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
    const double roots[] = { 293.66, 349.23, 440.0 };
    const double detune = std::pow (2.0, 4.0 / 1200.0);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kRate;
        const double env = std::exp (-t / 0.35) * std::min (1.0, t / 0.002);
        double l = 0.0, r = 0.0;
        for (double f0 : roots)
            for (int k = 1; k <= 8; ++k)
            {
                const double a = 0.05 / k;
                l += a * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * k * t);
                r += a * std::sin (2.0 * juce::MathConstants<double>::pi * f0 * k * detune * t + 0.3 * k);
            }
        x.l[(size_t) i] = (float) (env * l);
        x.r[(size_t) i] = (float) (env * r);
    }
    return x;
}

constexpr double kBands[] = { 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250,
                              1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000 };

std::vector<double> bandsDb (const std::vector<float>& x)
{
    int order = 10;
    while ((1 << order) < (int) x.size() && order < 20)
        ++order;
    const int n = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) n * 2, 0.f);
    std::copy (x.begin(), x.begin() + std::min ((int) x.size(), n), buf.begin());
    fft.performFrequencyOnlyForwardTransform (buf.data());
    std::vector<double> out;
    for (double fc : kBands)
    {
        const double lo = fc / std::pow (2.0, 1.0 / 6.0);
        const double hi = fc * std::pow (2.0, 1.0 / 6.0);
        double e = 0.0;
        for (int k = (int) std::ceil (lo * n / kRate); k <= (int) std::floor (hi * n / kRate) && k <= n / 2; ++k)
            e += (double) buf[(size_t) k] * (double) buf[(size_t) k];
        out.push_back (10.0 * std::log10 (e + 1.0e-30));
    }
    return out;
}

double energyDb (const std::vector<float>& x)
{
    double e = 0.0;
    for (float v : x)
        e += (double) v * (double) v;
    return 10.0 * std::log10 (e + 1.0e-30);
}

double correlation (const Stereo& x)
{
    double ll = 0.0, rr = 0.0, lr = 0.0;
    for (size_t i = 0; i < x.l.size(); ++i)
    {
        ll += (double) x.l[i] * x.l[i];
        rr += (double) x.r[i] * x.r[i];
        lr += (double) x.l[i] * x.r[i];
    }
    return lr / std::sqrt (ll * rr + 1.0e-30);
}

// Worst band difference (dB) after matching each channel's level, over bands
// within 60 dB of the source's loudest band.
double worstBandDb (const Stereo& src, const Stereo& out, double& atHz, std::string* detail = nullptr)
{
    double worst = 0.0;
    atHz = 0.0;
    for (int ch = 0; ch < 2; ++ch)
    {
        const auto& a = ch == 0 ? src.l : src.r;
        const auto& b = ch == 0 ? out.l : out.r;
        const auto sa = bandsDb (a);
        const auto sb = bandsDb (b);
        const double match = energyDb (b) - energyDb (a);
        const double top = *std::max_element (sa.begin(), sa.end());
        for (size_t k = 0; k < sa.size(); ++k)
        {
            if (sa[k] < top - 60.0)
                continue;
            const double d = std::fabs (sb[k] - sa[k] - match);
            if (detail != nullptr && d > 1.0)
                *detail += (ch == 0 ? " L" : " R") + juce::String (kBands[k], 0).toStdString() + ":"
                           + juce::String (sb[k] - sa[k] - match, 1).toStdString() + "dB@"
                           + juce::String (sa[k] - top, 0).toStdString();
            if (d > worst)
            {
                worst = d;
                atHz = kBands[k];
            }
        }
    }
    return worst;
}

// Plays one note or hit to its end through the sampler and returns that lane's output.
Stereo renderSolo (const rolla::PresetBank& bank, const rolla::EngineParams& params,
                   int part, int drumVoice, int note, int frames, std::string& picked)
{
    rolla::Sampler sampler;
    sampler.prepare (kRate);
    const auto start = juce::Time::getMillisecondCounter();
    picked.clear();
    while (picked.empty() && juce::Time::getMillisecondCounter() - start < 8000)
    {
        sampler.reset();
        if (drumVoice >= 0)
            sampler.takeDrum (drumVoice, 127, bank, params);
        else
            sampler.take (part, note, 127, 1.0e9f, bank, params);
        picked = sampler.lastPickedFile (part, drumVoice);
        if (picked.empty())
            juce::Thread::sleep (5);
    }
    Stereo out { std::vector<float> ((size_t) frames), std::vector<float> ((size_t) frames) };
    for (int i = 0; i < frames; ++i)
    {
        float laneL[rolla::NumParts] {};
        float laneR[rolla::NumParts] {};
        sampler.render (laneL, laneR, 0.f, 0.f, false);
        out.l[(size_t) i] = laneL[part];
        out.r[(size_t) i] = laneR[part];
    }
    return out;
}

void testFidelity (const juce::File& scratch)
{
    const auto root = scratch.getChildFile ("fidelity-pack");
    const auto kick = makeKick();
    const auto hat = makeHat();
    const auto stab = makeStab();
    writeStereo (root.getChildFile ("DRUMS/KICK/kick_fid.wav"), kick);
    writeStereo (root.getChildFile ("DRUMS/CH/ch_fid.wav"), hat);
    writeStereo (root.getChildFile ("STAB/stab_fid_D4.wav"), stab);

    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    const auto bank = proc.sharePresets();
    expect (bank != nullptr, "(e) presets are published");
    if (bank == nullptr)
        return;

    rolla::SampleLibrary library;
    library.setRoot (root);
    const char* manifest = R"JSON({"count": 0, "files": []})JSON";
    library.startIndex (manifest, (int) std::strlen (manifest));
    pumpUntil ([&] { return library.indexedFound() >= 0; });
    const auto index = library.index();
    expect (index != nullptr && library.indexedFound() == 3, "(e) the three source files are indexed");
    if (index == nullptr)
        return;

    auto params = paramsFor (proc, bank.get(), index.get());
    params.library = &library;
    params.sampleIndex = index.get();
    expect (std::fabs (params.tone[rolla::Stab] - 0.5f) < 1.0e-6f && std::fabs (params.tone[rolla::Drums] - 0.5f) < 1.0e-6f
                && std::fabs (params.drumDecay[rolla::Kick] - 0.5f) < 1.0e-6f && std::fabs (params.drumDecay[rolla::HatC] - 0.5f) < 1.0e-6f,
            "(e) knobs are at their defaults (TONE 0.5, DECAY 0.5)");

    struct Case { const char* name; const Stereo* src; int part; int voice; int note; const char* file; };
    const Case cases[] = {
        { "kick", &kick, rolla::Drums, rolla::Kick, 60, "DRUMS/KICK/kick_fid.wav" },
        { "closed hat", &hat, rolla::Drums, rolla::HatC, 60, "DRUMS/CH/ch_fid.wav" },
        { "stab", &stab, rolla::Stab, -1, 62, "STAB/stab_fid_D4.wav" },
    };
    for (const auto& c : cases)
    {
        std::string picked;
        const int frames = (int) c.src->l.size() + (int) (0.1 * kRate);
        auto out = renderSolo (*bank, params, c.part, c.voice, c.note, frames, picked);
        const std::string tag = std::string ("(e) ") + c.name;
        expect (picked == c.file, tag + " plays " + c.file + (picked == c.file ? std::string() : " (got " + picked + ")"));
        // Compare the file's length; what follows it must be silence.
        float after = 0.f;
        for (size_t i = c.src->l.size(); i < out.l.size(); ++i)
            after = std::max ({ after, std::fabs (out.l[i]), std::fabs (out.r[i]) });
        out.l.resize (c.src->l.size());
        out.r.resize (c.src->r.size());
        double atHz = 0.0;
        std::string detail;
        const double worst = worstBandDb (*c.src, out, atHz, &detail);
        if (! detail.empty())
            std::cout << "  " << c.name << " bands off by > 1 dB (diff @ level re loudest band):" << detail << "\n";
        const double cs = correlation (*c.src);
        const double co = correlation (out);
        std::cout << "  " << c.name << ": worst band " << juce::String (worst, 2) << " dB at " << atHz
                  << " Hz, L/R corr source " << juce::String (cs, 3) << " / played " << juce::String (co, 3)
                  << ", tail after the file " << dbfs (after) << "\n";
        expect (worst <= 1.5, tag + " spectrum within 1.5 dB of the file, 50 Hz-16 kHz (worst " + juce::String (worst, 2).toStdString() + " dB)");
        expect (std::fabs (co - cs) <= 0.02, tag + " L/R correlation matches the file (" + juce::String (cs, 3).toStdString()
                                               + " vs " + juce::String (co, 3).toStdString() + ")");
        expect (after < kSilent, tag + " ends with the file");
    }

    // TONE off neutral is the one thing that filters a sample: darker pulls a
    // lowpass down to the kit's cutoff, brighter raises a highpass.
    // How much one band moved against a reference band (a change of tilt).
    auto bandShift = [&] (const Stereo& src, const Stereo& out, double hz, double refHz)
    {
        const auto a = bandsDb (src.l);
        const auto b = bandsDb (out.l);
        auto at = [] (double f)
        {
            size_t k = 0;
            while (kBands[k] < f)
                ++k;
            return k;
        };
        const size_t k = at (hz), r = at (refHz);
        return (b[k] - b[r]) - (a[k] - a[r]);
    };
    {
        auto dark = params;
        dark.tone[rolla::Drums] = 0.f;
        std::string picked;
        const auto out = renderSolo (*bank, dark, rolla::Drums, rolla::HatC, 60, (int) hat.l.size(), picked);
        const double drop = bandShift (hat, out, 12500.0, 1000.0);
        expect (drop < -6.0, "(e) drums TONE 0 darkens the hat (12.5 kHz vs 1 kHz " + juce::String (drop, 1).toStdString() + " dB)");
    }
    {
        auto bright = params;
        bright.tone[rolla::Drums] = 1.f;
        std::string picked;
        const auto out = renderSolo (*bank, bright, rolla::Drums, rolla::Kick, 60, (int) kick.l.size(), picked);
        const double drop = bandShift (kick, out, 50.0, 2000.0);
        expect (drop < -6.0, "(e) drums TONE 1 thins the kick (50 Hz vs 2 kHz " + juce::String (drop, 1).toStdString() + " dB)");
    }
    {
        auto shortHit = params;
        shortHit.drumDecay[rolla::Kick] = 0.f;
        std::string picked;
        const int frames = (int) kick.l.size();
        const auto out = renderSolo (*bank, shortHit, rolla::Drums, rolla::Kick, 60, frames, picked);
        float late = 0.f;
        for (size_t i = (size_t) (frames / 4); i < out.l.size(); ++i)
            late = std::max (late, std::fabs (out.l[i]));
        expect (late < kSilent, "(e) kick DECAY 0 ends within an eighth of the file (" + dbfs (late) + " after a quarter)");
    }
}
} // namespace

int main()
{
    const auto home = rolla::test::useScratchProfile ("bundle");
    if (home.empty())
    {
        std::cerr << "could not set up a scratch profile\n";
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File scratch { juce::String (home) };
    const auto settingsFile = rolla::settings::file();
    std::cout << "scratch profile: " << scratch.getFullPathName() << "\n";
    expect (settingsFile.isAChildOf (scratch), "settings live in the scratch profile (" + settingsFile.getFullPathName().toStdString() + ")");
    expect (rolla::SampleLibrary::defaultRoot().isAChildOf (scratch), "~/Music/ROLLA/Samples is redirected to the scratch profile");
    if (fails != 0)
        return 1;

    const auto bundle = scratch.getChildFile ("ROLLA.vst3/Contents/Resources/Samples");
    buildPack (bundle);
    const auto savedTemp = scratch.getChildFile ("old-test-pack");
    writeTone (savedTemp.getChildFile ("BASS/old_D1.wav"), 0.3, 41.2f, 0.3f, 1.f);
    putEnv ("ROLLA_BUNDLE_ROOT", bundle);

    testResolve (scratch, bundle, savedTemp);
    testBundled (bundle, savedTemp);
    testEmpty (scratch, bundle);
    testFidelity (scratch);

    scratch.deleteRecursively();
    std::cout << (fails == 0 ? "bundle tests passed\n" : std::to_string (fails) + " bundle test failure(s)\n");
    return fails == 0 ? 0 : 1;
}
