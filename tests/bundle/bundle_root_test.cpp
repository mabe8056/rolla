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

#include "PluginProcessor.h"
#include "Engine/RollaSettings.h"
#include "Engine/Sampler.h"
#include "Drums/DrumSampleSlot.h"
#include "../common/ScratchProfile.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <functional>
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
    for (int p = 0; p < rolla::NumParts; ++p)
        if (auto* v = proc.apvts.getRawParameterValue (juce::String (rolla::partId (p)) + "Preset"))
            params.preset[p] = (int) v->load();
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

    scratch.deleteRecursively();
    std::cout << (fails == 0 ? "bundle tests passed\n" : std::to_string (fails) + " bundle test failure(s)\n");
    return fails == 0 ? 0 : 1;
}
