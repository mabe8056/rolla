#include "Engine/Engine.h"
#include "Engine/Sampler.h"
#include "Sampler/PlayDecision.h"
#include "Sampler/SamplePrep.h"
#include "Sampler/UserSlots.h"
#include "Generator/Presets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

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

bool waitUntil (const std::function<bool()>& ready)
{
    for (int i = 0; i < 80; ++i)
    {
        if (ready())
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
    }
    return false;
}

std::uint64_t checksum (const juce::File& file)
{
    juce::MemoryBlock block;
    file.loadFileAsData (block);
    std::uint64_t hash = 1469598103934665603ull;
    const auto* bytes = static_cast<const unsigned char*> (block.getData());
    for (size_t i = 0; i < block.getSize(); ++i)
        hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

void writeTone (const juce::File& file, double seconds, float amplitude, float hz, bool leadSilence)
{
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    auto fileStream = std::make_unique<juce::FileOutputStream> (file);
    if (fileStream == nullptr || ! fileStream->openedOk())
        return;
    std::unique_ptr<juce::OutputStream> stream (fileStream.release());
    const auto options = juce::AudioFormatWriterOptions().withSampleRate (48000.0)
                                                          .withNumChannels (1)
                                                          .withBitsPerSample (16);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return;
    const int n = (int) std::lround (seconds * 48000.0);
    const int lead = leadSilence ? 4800 : 0;
    juce::AudioBuffer<float> buf (1, n + lead);
    buf.clear();
    for (int i = 0; i < n; ++i)
        buf.setSample (0, lead + i, amplitude * std::sin (2.f * 3.14159265f * hz * (float) i / 48000.f));
    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

rolla::SlotQuery slotQuery (int mode, bool ready, bool missing, bool loading)
{
    rolla::SlotQuery slot;
    slot.mode = mode;
    slot.ready = ready;
    slot.missing = missing;
    slot.loading = loading;
    slot.file = "USER/BASS/tone.wav";
    slot.root = 69;
    slot.layerMix = 0.25f;
    slot.loop = true;
    slot.loopStart = 23097.f;
    slot.loopEnd = 45367.f;
    slot.loopXfade = 4410.f;
    return slot;
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    static_assert (rolla::kUserSlotCount == 14, "14 user slots");
    expect (rolla::userSlotForPart (rolla::Bass) == rolla::SlotBass, "bass slot");
    expect (rolla::userSlotForPart (rolla::Drums) < 0, "drums lane is not a melodic slot");
    expect (rolla::userSlotForDrum (rolla::Kick) == rolla::SlotKick, "kick slot");
    expect (rolla::userSlotForDrum (rolla::HatC) == rolla::SlotCh, "hat slot");
    expect (rolla::userSlotForDrum (rolla::Perc) == rolla::SlotPerc, "perc slot");
    expect (rolla::userSlotForDrum (rolla::TomLow) < 0, "toms stay on the dropped-wav path");
    expect (std::string (rolla::userSlotFolder (rolla::SlotPerc)) == "DRUMS/PERC", "perc folder");

    expect (rolla::midiFromFilename ("BASS/THMAG_Bass_One_Shot_4_Fm.wav") == 53, "Fm -> F3");
    expect (rolla::midiFromFilename ("STAB/thing_C#.wav") == 49, "C# -> C#3");
    expect (rolla::midiFromFilename ("BASS/pluck_D1.wav") == 26, "D1");
    expect (rolla::midiFromFilename ("VOX/shot_Ds.wav") == 51, "Ds -> D#3");
    expect (rolla::midiFromFilename ("no-note.wav") < 0, "no filename note");

    expect (rolla::sampleBytesFit (100, 20, 128), "budget fits");
    expect (! rolla::sampleBytesFit (100, 50, 120), "budget blocks");

    rolla::SampleLoop frames;
    frames.start = 23097.f;
    frames.end = 45367.f;
    frames.xfade = 4410.f;
    const auto frameRegion = rolla::makeLoopRegion (frames, 44100.0, 50000, 0.0);
    expect (std::fabs (frameRegion.a - 23097.0) < 0.1, "loop frames are not scaled by rate");
    expect (std::fabs (frameRegion.b - 45367.0) < 0.1, "loop end frames");
    expect (std::fabs (frameRegion.xfade - 4410.0) < 0.1, "loop xfade frames");

    rolla::SampleLoop seconds;
    seconds.start = 0.5f;
    seconds.end = 1.0f;
    seconds.xfade = 0.01f;
    const auto secondRegion = rolla::makeLoopRegion (seconds, 44100.0, 88200, 10.0);
    expect (std::fabs (secondRegion.a - 22050.0) < 1.0, "loop seconds still scale");
    expect (std::fabs (secondRegion.xfade - 441.0) < 1.0, "xfade seconds");

    rolla::PresetQuery preset;
    preset.mode = rolla::SampleMode::Sample;
    preset.file = "BASS/tone.wav";
    preset.root = 38;
    preset.ready = true;
    const auto samplePlay = rolla::resolvePlay (preset, nullptr);
    expect (samplePlay.kind == 1 && std::fabs (samplePlay.synthGain) < 1.0e-6f && ! samplePlay.warning, "preset sample");

    preset.ready = false;
    preset.missing = true;
    const auto missingPreset = rolla::resolvePlay (preset, nullptr);
    expect (missingPreset.kind == 0 && missingPreset.warning, "missing preset falls back to synth");

    preset.mode = rolla::SampleMode::Synth;
    preset.missing = false;
    preset.file = "";
    const auto synth = rolla::resolvePlay (preset, nullptr);
    expect (synth.kind == 0 && ! synth.warning, "synth mode is silent about files");

    preset.mode = rolla::SampleMode::Layer;
    preset.file = "BASS/tone.wav";
    preset.ready = true;
    preset.missing = false;
    preset.layerMix = 0.35f;
    const auto layer = rolla::resolvePlay (preset, nullptr);
    expect (layer.kind == 2 && std::fabs (layer.synthGain - 0.65f) < 1.0e-4f, "layer mix is sample share");

    auto readySlot = slotQuery (1, true, false, false);
    const auto slotPlay = rolla::resolvePlay (preset, &readySlot);
    expect (slotPlay.fromSlot && slotPlay.kind == 1 && slotPlay.suppressPreset, "slot sample replaces preset");
    expect (std::fabs (slotPlay.loopStart - 23097.f) < 0.1f, "slot keeps frame loop");

    readySlot.mode = 2;
    const auto slotLayer = rolla::resolvePlay (preset, &readySlot);
    expect (slotLayer.kind == 2 && std::fabs (slotLayer.synthGain - 0.75f) < 1.0e-4f, "slot layer mix");

    auto loading = slotQuery (1, false, false, true);
    const auto whileLoading = rolla::resolvePlay (preset, &loading);
    expect (whileLoading.suppressPreset && whileLoading.kind == 0 && ! whileLoading.warning, "loading does not warn");

    auto gone = slotQuery (1, false, true, false);
    preset.ready = true;
    const auto slotMissing = rolla::resolvePlay (preset, &gone);
    expect (slotMissing.warning && ! slotMissing.fromSlot && slotMissing.kind == 2, "missing slot falls back to preset sample");

    preset.mode = rolla::SampleMode::Synth;
    preset.ready = false;
    preset.file = "";
    const auto bothMissing = rolla::resolvePlay (preset, &gone);
    expect (bothMissing.warning && bothMissing.kind == 0, "missing slot and synth preset stay on the synth");

    std::vector<float> sine (24000);
    for (int i = 0; i < (int) sine.size(); ++i)
        sine[(size_t) i] = 0.2f + 0.25f * std::sin (2.f * 3.14159265f * 440.f * (float) i / 48000.f);
    const float* chans[] = { sine.data() };
    const auto prepped = rolla::prepareSample (chans, 1, (int) sine.size(), 48000.0);
    double mean = 0.0;
    float peak = 0.f;
    for (float s : prepped.left)
    {
        mean += s;
        peak = std::max (peak, std::fabs (s));
    }
    mean /= (double) std::max (1, prepped.frames);
    expect (std::fabs (mean) < 1.0e-3, "prep removes DC");
    expect (peak <= std::pow (10.f, -5.9f / 20.f), "prep caps true peak");
    expect (prepped.report.gainDb < 0.f, "a hot file is turned down");

    std::vector<float> late (48000);
    for (int i = 8000; i < (int) late.size(); ++i)
        late[(size_t) i] = 0.2f * std::sin (2.f * 3.14159265f * 220.f * (float) (i - 8000) / 48000.f);
    const float* lateCh[] = { late.data() };
    const auto trimmed = rolla::prepareSample (lateCh, 1, (int) late.size(), 48000.0);
    expect (trimmed.report.leadTrimFrames > 4000, "leading silence is trimmed");
    expect (trimmed.frames < (int) late.size() - 4000, "trimmed buffer is shorter");

    std::vector<float> pure (24000);
    for (int i = 0; i < (int) pure.size(); ++i)
        pure[(size_t) i] = 0.25f * std::sin (2.f * 3.14159265f * 440.f * (float) i / 48000.f);
    const int root = rolla::detectRootMidi (pure.data(), (int) pure.size(), 48000.0, false);
    expect (root == 69, "440 Hz is A4");
    expect (rolla::chooseSlotRoot (rolla::SlotSnare, sine.data(), (int) sine.size(), 48000.0, "x_C2.wav") == 60,
            "non-kick drums stay on C3");

    const auto rootDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rolla-sampler-test");
    rootDir.deleteRecursively();
    const auto inside = rootDir.getChildFile ("BASS/tone.wav");
    writeTone (inside, 0.5, 0.4f, 440.f, false);
    const auto outside = rootDir.getParentDirectory().getChildFile ("rolla-outside-tone.wav");
    writeTone (outside, 0.5, 0.3f, 330.f, false);
    const auto beforeInside = checksum (inside);
    const auto beforeOutside = checksum (outside);

    rolla::UserSlots slots;
    slots.setRoot (rootDir);
    juce::String error;
    expect (slots.assign (rolla::SlotBass, inside, error), "assign in-root: " + error.toStdString());
    expect (slots.file (rolla::SlotBass).toStdString() == "BASS/tone.wav", "in-root path is not copied");
    expect (! rootDir.getChildFile ("USER/BASS/tone.wav").existsAsFile(), "in-root file is not duplicated");
    expect (checksum (inside) == beforeInside, "in-root file bytes stay put");

    expect (waitUntil ([&] { return slots.status (rolla::SlotBass) == 2; }),
            "load in-root: " + slots.lastError (rolla::SlotBass).toStdString());
    expect (! slots.warning (rolla::SlotBass), "ready slot is not a warning");
    expect (slots.rootNote (rolla::SlotBass) == 69, "detected root is A4");
    expect (checksum (inside) == beforeInside, "prep does not write the file");

    expect (slots.assign (rolla::SlotKick, inside, error), "assign kick");
    expect (waitUntil ([&] { return slots.status (rolla::SlotKick) == 2; }),
            "load kick: " + slots.lastError (rolla::SlotKick).toStdString());
    expect (slots.rootNote (rolla::SlotKick) == 69, "kick tail f0 is A4");

    expect (slots.assign (rolla::SlotStab, outside, error), "assign outside: " + error.toStdString());
    expect (slots.file (rolla::SlotStab).startsWith ("USER/STAB/"), "outside file is copied under USER");
    expect (checksum (outside) == beforeOutside, "the source file is not modified");
    expect (waitUntil ([&] { return slots.status (rolla::SlotStab) == 2; }),
            "load copy: " + slots.lastError (rolla::SlotStab).toStdString());

    const auto tooLong = rootDir.getChildFile ("BASS/long.wav");
    writeTone (tooLong, 61.0, 0.1f, 110.f, false);
    const auto longSum = checksum (tooLong);
    expect (! slots.assign (rolla::SlotPluck, tooLong, error), "files over 60 s are refused");
    expect (error.contains ("60"), "refusal names the limit");
    expect (checksum (tooLong) == longSum, "refused file is not modified");
    expect (slots.mode (rolla::SlotPluck) == 0, "refused assign leaves the slot");

    rolla::PresetBank bank;
    rolla::loadDefaultPresets (bank);
    rolla::Sampler sampler;
    sampler.prepare (48000.0);
    rolla::EngineParams params;
    params.presets = &bank;
    params.userSlots = slots.published();
    for (int i = 0; i < rolla::NumDrumVoices; ++i)
        params.drumDecay[i] = 0.5f;
    const auto taken = sampler.take (rolla::Bass, 69, 110, 1.0e9f, bank, params);
    expect (taken.kind == 1 && std::fabs (taken.synthGain) < 1.0e-6f, "sample slot replaces the synth");
    float peakOut = 0.f;
    for (int i = 0; i < 4000; ++i)
    {
        float laneL[rolla::NumParts] {};
        float laneR[rolla::NumParts] {};
        sampler.render (laneL, laneR, 0.f, 0.f, false);
        peakOut = std::max (peakOut, std::fabs (laneL[rolla::Bass]));
    }
    expect (peakOut > 0.01f, "slot sample is audible");
    const float kickKeep = sampler.takeDrum (rolla::Kick, 110, bank, params);
    expect (kickKeep < 0.001f, "kick slot replaces the synth drum");
    float kickPeak = 0.f;
    for (int i = 0; i < 2000; ++i)
    {
        float laneL[rolla::NumParts] {};
        float laneR[rolla::NumParts] {};
        sampler.render (laneL, laneR, 0.f, 0.f, false);
        kickPeak = std::max (kickPeak, std::fabs (laneL[rolla::Drums]));
    }
    expect (kickPeak > 0.01f, "kick slot is audible");

    slots.setLayerMix (rolla::SlotBass, 0.25f);
    slots.setMode (rolla::SlotBass, 2);
    params.userSlots = slots.published();
    sampler.reset();
    const auto layered = sampler.take (rolla::Bass, 69, 110, 1.0e9f, bank, params);
    expect (layered.kind == 2 && std::fabs (layered.synthGain - 0.75f) < 1.0e-3f, "runtime layer mix");

    juce::ValueTree state ("ROLLA");
    slots.writeState (state);
    auto xml = state.createXml();
    expect (xml != nullptr && xml->toString().contains ("BASS/tone.wav"), "state stores the relative path");
    expect (xml->toString().length() < 8000, "state does not embed audio");
    expect (! xml->toString().contains ("RIFF"), "state is not a wav");

    rolla::UserSlots restored;
    restored.setRoot (rootDir);
    restored.readState (state);
    expect (waitUntil ([&] { return restored.status (rolla::SlotBass) == 2; }), "restored slot reloads");
    expect (restored.mode (rolla::SlotBass) == 2, "mode survives the session");
    expect (restored.file (rolla::SlotStab).startsWith ("USER/STAB/"), "copied path survives");
    expect (checksum (inside) == beforeInside && checksum (outside) == beforeOutside, "reload does not touch files");

    juce::ValueTree missingTree ("ROLLA");
    auto missingSlots = juce::ValueTree ("userSlots");
    auto missingSlot = juce::ValueTree ("slot");
    missingSlot.setProperty ("id", rolla::SlotHits, nullptr);
    missingSlot.setProperty ("mode", 1, nullptr);
    missingSlot.setProperty ("file", "HITS/missing.wav", nullptr);
    missingSlot.setProperty ("root", 60, nullptr);
    missingSlots.appendChild (missingSlot, nullptr);
    missingTree.appendChild (missingSlots, nullptr);
    restored.readState (missingTree);
    expect (waitUntil ([&] { return restored.warning (rolla::SlotHits); }), "missing file raises a warning");
    params.userSlots = restored.published();
    sampler.reset();
    const auto fallback = sampler.take (rolla::Hits, 60, 100, 1.0e9f, bank, params);
    expect (fallback.kind == 0, "missing slot keeps the synth");

    rolla::Engine engine;
    engine.prepare (48000.0, 128);
    rolla::Groove groove;
    groove.bars = 2;
    groove.lane[rolla::Hits].count = 1;
    groove.lane[rolla::Hits].notes[0].start = 0.f;
    groove.lane[rolla::Hits].notes[0].length = 8.f;
    groove.lane[rolla::Hits].notes[0].nChord = 1;
    groove.lane[rolla::Hits].notes[0].vel = 110;
    params.bars = 2;
    for (int i = 0; i < rolla::NumParts; ++i)
        params.levelDb[i] = 0.f;
    rolla::Transport transport;
    transport.playing = true;
    transport.bpm = 130.0;
    transport.sampleRate = 48000.0;
    double energy = 0.0;
    for (int block = 0; block < 30; ++block)
    {
        float left[128] {}, right[128] {};
        juce::MidiBuffer midi;
        engine.process (groove, params, transport, left, right, nullptr, nullptr, 128, midi, false);
        for (float s : left)
            energy += (double) s * (double) s;
        transport.ppqStart += 128.0 * 130.0 / 60.0 / 48000.0;
    }
    expect (energy > 1.0e-4, "missing slot does not silence the lane");

    const auto zeros = rootDir.getChildFile ("HITS/zeros.wav");
    writeTone (zeros, 0.3, 0.f, 440.f, false);
    expect (restored.assign (rolla::SlotHits, zeros, error), "assign silence");
    expect (waitUntil ([&] { return restored.status (rolla::SlotHits) == 2; }),
            "silence loads: " + restored.lastError (rolla::SlotHits).toStdString());
    params.userSlots = restored.published();
    rolla::Engine quietEngine;
    quietEngine.prepare (48000.0, 128);
    transport.ppqStart = 0.0;
    double quiet = 0.0;
    for (int block = 0; block < 30; ++block)
    {
        float left[128] {}, right[128] {};
        juce::MidiBuffer midi;
        quietEngine.process (groove, params, transport, left, right, nullptr, nullptr, 128, midi, false);
        for (float s : left)
            quiet += (double) s * (double) s;
        transport.ppqStart += 128.0 * 130.0 / 60.0 / 48000.0;
    }
    expect (quiet < energy * 0.05, "SAMPLE mode replaces the synth instead of stacking it");

    {
        const auto jsonFile = juce::File (__FILE__)
                                  .getParentDirectory().getParentDirectory().getParentDirectory()
                                  .getChildFile ("resources/presets.json");
        const auto text = jsonFile.loadFileAsString();
        rolla::PresetBank factory;
        expect (rolla::loadPresetsFromJson (factory, text.toRawUTF8(), (int) text.getNumBytesAsUTF8()),
                "factory presets parse");
        expect (std::string (factory.bass[0].id) == "pluck", "bass order");
        expect (factory.bass[0].sample.mode == rolla::SampleMode::Layer, "pluck is layered");
        expect (std::string (factory.bass[0].sample.file) == "BASS/THMAG_Bass_One_Shot_4_Fm.wav",
                "pluck uses the manifest file");
        expect (factory.bass[1].sample.loop.on && factory.bass[1].sample.loop.start > 32.f,
                "round loop is stored in frames");
        expect (factory.stab[0].sample.mode == rolla::SampleMode::Sample, "dub stab is a sample");
        expect (factory.drums[0].sample.mode == rolla::SampleMode::Sample, "tight kit is sampled");
        expect (std::string (factory.drums[0].sample.voiceFile[rolla::Kick]) == "DRUMS/KICK/CD_Kick_06.wav",
                "tight kick file");
        expect (factory.drums[1].sample.voiceFile[rolla::Ride][0] != 0, "room kit has a ride");
        expect (factory.drums[2].sample.mode == rolla::SampleMode::Synth, "909 synth kit keeps the circuit model");
        expect (std::string (factory.drums[2].name) == "909 SYNTH", "circuit kit is named 909 SYNTH");
        expect (factory.drums[0].voicePool[rolla::Kick].size() >= 20, "kick pool");
        expect (factory.drums[0].voicePool[rolla::Snare].size() >= 10, "snare pool");
        expect (factory.drums[0].voicePool[rolla::Clap].size() >= 16, "clap pool");
        expect (factory.drums[0].voicePool[rolla::Rim].size() >= 8, "rim pool");
        expect (factory.drums[0].voicePool[rolla::HatC].size() >= 14, "closed hat pool");
        expect (factory.drums[0].voicePool[rolla::HatO].size() >= 14, "open hat pool");
        expect (factory.drums[0].voicePool[rolla::Ride].size() >= 6, "ride pool");
        expect (factory.drums[0].voicePool[rolla::Perc].size() >= 12, "perc pool");
        expect (std::string (rolla::drumVoiceChoice (factory.drums[0], rolla::Kick, 0))
                    == "DRUMS/KICK/CD_Kick_06.wav",
                "pick 0 is the hero kick");
        expect (std::string (rolla::drumVoiceChoice (factory.drums[0], rolla::Kick, 1))
                    != "DRUMS/KICK/CD_Kick_06.wav",
                "the next roll picks another kick");
        expect (factory.bass[0].sample.styles != 0, "pluck is tagged to a dialect");
        int synths = 0;
        int sampled = 0;
        for (int i = 0; i < factory.count[rolla::Bass]; ++i)
        {
            if (factory.bass[i].sample.mode == rolla::SampleMode::Synth)
                ++synths;
            else if (factory.bass[i].sample.file[0] != 0)
                ++sampled;
        }
        expect (factory.count[rolla::Bass] > 3, "bass bank grew past the three heroes");
        expect (synths == 1 && std::string (factory.bass[factory.count[rolla::Bass] - 1].name) == "SYNTH",
                "one clearly named synth bass preset");
        expect (sampled + synths == factory.count[rolla::Bass], "every bass preset is a sample or SYNTH");
        const int first = rolla::pickPresetForRoll (factory, rolla::Bass, rolla::UkMin, 1);
        expect (first >= 0 && factory.bass[first].sample.mode != rolla::SampleMode::Synth,
                "roll does not pick the synth bass");
        bool varied = false;
        for (std::uint32_t seed = 2; seed < 24; ++seed)
            if (rolla::pickPresetForRoll (factory, rolla::Bass, rolla::UkMin, seed) != first)
                varied = true;
        expect (varied, "roll preset pick changes with the seed");
        expect (rolla::pickPresetForRoll (factory, rolla::Drums, rolla::Gaskin, 3) == 1,
                "gaskin roll stays on the sample kit");

        rolla::SampleLibrary missingLib;
        missingLib.setRoot (rootDir.getChildFile ("empty-root"));
        missingLib.watch (factory);
        expect (waitUntil ([&] { return missingLib.missing (factory.bass[0].sample.file); }),
                "absent factory file is flagged");
        rolla::Sampler missingSampler;
        missingSampler.prepare (48000.0);
        rolla::EngineParams missingParams;
        missingParams.presets = &factory;
        missingParams.library = &missingLib;
        const auto kept = missingSampler.take (rolla::Bass, 41, 100, 4.f, factory, missingParams);
        expect (kept.kind == 0 && std::fabs (kept.synthGain - 1.f) < 1.0e-6f,
                "missing factory file keeps the synth");
    }

    {
        std::vector<float> silence (256, 0.f);
        rolla::Engine drumEngine;
        drumEngine.prepare (48000.0, 128);
        drumEngine.loadDrumSample (rolla::Kick, silence.data(), (int) silence.size(), 48000.0);
        rolla::Groove kick;
        kick.bars = 2;
        kick.drums.vel[rolla::Kick][0] = 110;
        rolla::EngineParams drumParams;
        drumParams.presets = &bank;
        drumParams.bars = 2;
        for (int i = 0; i < rolla::NumDrumVoices; ++i)
            drumParams.drumDecay[i] = 0.5f;
        auto measure = [&] (rolla::Engine& eng)
        {
            rolla::Transport play;
            play.playing = true;
            play.bpm = 130.0;
            play.sampleRate = 48000.0;
            double sum = 0.0;
            for (int block = 0; block < 20; ++block)
            {
                float left[128] {}, right[128] {};
                juce::MidiBuffer midi;
                eng.process (kick, drumParams, play, left, right, nullptr, nullptr, 128, midi, false);
                for (float s : left)
                    sum += (double) s * (double) s;
                play.ppqStart += 128.0 * 130.0 / 60.0 / 48000.0;
            }
            return sum;
        };
        const double dropped = measure (drumEngine);
        expect (dropped < 1.0e-4, "a dropped silent kick replaces the synth");
        drumEngine.reset();
        drumParams.userSlots = slots.published();
        const double slotted = measure (drumEngine);
        expect (slotted > 1.0e-4, "user kick slot wins over the dropped wav");
    }

    outside.deleteFile();
    tooLong.deleteFile();
    rootDir.deleteRecursively();

    if (fails > 0)
    {
        std::cerr << fails << " failed\n";
        return 1;
    }
    std::cout << "sampler tests ok\n";
    return 0;
}
