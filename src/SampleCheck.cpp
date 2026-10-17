#include "Engine/Engine.h"
#include "Engine/Sampler.h"
#include "Generator/Presets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <chrono>
#include <cmath>
#include <functional>
#include <iostream>
#include <thread>

namespace
{

int fail (const std::string& msg)
{
    std::cerr << "FAIL " << msg << "\n";
    return 1;
}

bool waitUntil (const std::function<bool()>& ready)
{
    for (int i = 0; i < 40; ++i)
    {
        if (ready())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    return false;
}

void writeSine (const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.release(), 48000.0, 1, 16, {}, 0));
    if (writer == nullptr)
        return;
    juce::AudioBuffer<float> buf (1, 24000);
    for (int i = 0; i < buf.getNumSamples(); ++i)
        buf.setSample (0, i, 0.4f * std::sin (2.f * 3.14159265f * 440.f * (float) i / 48000.f));
    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    if (rolla::midiFromNoteName ("D1", -1) != 26)
        return fail ("D1");
    if (rolla::midiFromNoteName ("C4", -1) != 60)
        return fail ("C4");
    if (rolla::midiFromNoteName ("D#1", -1) != 27)
        return fail ("D#1");
    if (rolla::midiFromNoteName ("Db1", -1) != 25)
        return fail ("Db1");

    const auto root = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rolla-sample-check");
    root.deleteRecursively();
    writeSine (root.getChildFile ("BASS/tone.wav"));

    rolla::PresetBank bank;
    rolla::loadDefaultPresets (bank);
    auto& spec = bank.bass[0].sample;
    spec.mode = rolla::SampleMode::Sample;
    spec.root = 69;
    rolla::copySamplePath (spec.file, 96, "BASS/tone.wav");

    rolla::SampleLibrary library;
    library.setRoot (root);
    library.watch (bank);
    if (! waitUntil ([&] { return library.ready ("BASS/tone.wav"); }))
        return fail ("load");

    rolla::Sampler sampler;
    sampler.prepare (48000.0);
    rolla::EngineParams params;
    params.presets = &bank;
    params.library = &library;
    const auto taken = sampler.take (rolla::Bass, 69, 110, 1.0e9f, bank, params);
    if (taken.kind != 1)
        return fail ("take");

    float peak = 0.f;
    float jump = 0.f;
    float prev = 0.f;
    for (int i = 0; i < 4000; ++i)
    {
        float laneL[rolla::NumParts] {};
        float laneR[rolla::NumParts] {};
        sampler.render (laneL, laneR, 0.f, 0.f, false);
        peak = std::max (peak, std::fabs (laneL[rolla::Bass]));
        if (i > 96)
            jump = std::max (jump, std::fabs (laneL[rolla::Bass] - prev));
        prev = laneL[rolla::Bass];
    }
    std::cout << "peak " << peak << " jump " << jump << "\n";
    if (peak < 0.02f)
        return fail ("silent sample");
    if (jump > 0.2f)
        return fail ("click");

    rolla::copySamplePath (spec.file, 96, "BASS/missing.wav");
    library.watch (bank);
    if (! waitUntil ([&] { return library.missing ("BASS/missing.wav"); }))
        return fail ("missing flag");
    sampler.reset();
    const auto fallback = sampler.take (rolla::Bass, 69, 110, 1.0e9f, bank, params);
    if (fallback.kind != 1 || std::fabs (fallback.synthGain) > 1.0e-6f)
        return fail ("missing sample invented a synth");

    rolla::Engine engine;
    engine.prepare (48000.0, 128);
    rolla::Groove groove;
    groove.bars = 2;
    groove.lane[rolla::Bass].count = 1;
    groove.lane[rolla::Bass].notes[0].start = 0.f;
    groove.lane[rolla::Bass].notes[0].length = 8.f;
    groove.lane[rolla::Bass].notes[0].nChord = 1;
    groove.lane[rolla::Bass].notes[0].vel = 100;
    params.bars = 2;
    for (int i = 0; i < rolla::NumParts; ++i)
        params.levelDb[i] = 0.f;
    rolla::Transport transport;
    transport.playing = true;
    transport.bpm = 130.0;
    transport.sampleRate = 48000.0;
    double energy = 0.0;
    for (int block = 0; block < 40; ++block)
    {
        float left[128] {}, right[128] {};
        juce::MidiBuffer midi;
        engine.process (groove, params, transport, left, right, nullptr, nullptr, 128, midi, false);
        for (float s : left)
            energy += (double) s * (double) s;
        transport.ppqStart += 128.0 * 130.0 / 60.0 / 48000.0;
    }
    std::cout << "fallback energy " << energy << "\n";
    if (energy > 1.0e-4)
        return fail ("missing file invented a sound");

    std::cout << "sample check ok\n";
    return 0;
}
