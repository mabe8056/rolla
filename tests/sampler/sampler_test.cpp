#include "Engine/Engine.h"
#include "Engine/Sampler.h"
#include "Sampler/PlayDecision.h"
#include "Sampler/SamplePrep.h"
#include "Sampler/UserSlots.h"
#include "Generator/Presets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <cstdio>
#include <cstring>
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

void writeBurst (const juce::File& file, int frames, int burst)
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
    juce::AudioBuffer<float> buf (1, frames);
    buf.clear();
    const int n = std::min (frames, burst);
    for (int i = 0; i < n; ++i)
        buf.setSample (0, i, 0.5f * std::sin (2.f * 3.14159265f * 440.f * (float) i / 48000.f));
    writer->writeFromAudioSampleBuffer (buf, 0, frames);
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
        expect (factory.bass[0].sample.mode == rolla::SampleMode::Sample, "pluck is his one-shot");
        expect (std::string (factory.bass[0].sample.file) == "BASS/THMAG_Bass_One_Shot_4_Fm.wav",
                "pluck uses the manifest file");
        expect (factory.bass[1].sample.loop.on && factory.bass[1].sample.loop.start > 32.f,
                "round loop is stored in frames");
        expect (factory.stab[0].sample.mode == rolla::SampleMode::Sample, "dub stab is a sample");
        expect (factory.drums[0].sample.mode == rolla::SampleMode::Sample, "tight kit is sampled");
        expect (std::string (factory.drums[0].sample.voiceFile[rolla::Kick]) == "DRUMS/KICK/CD_Kick_06.wav",
                "tight kick file");
        expect (factory.drums[1].sample.voiceFile[rolla::Ride][0] != 0, "room kit has a ride");
        expect (factory.count[rolla::Drums] == 2, "factory drums are the two sample kits");
        for (int i = 0; i < factory.count[rolla::Drums]; ++i)
            expect (factory.drums[i].sample.mode == rolla::SampleMode::Sample, "no synth drum kit");
        expect (factory.drums[0].voicePool[rolla::Kick].empty(), "kick pool is the scanned folder");
        expect (factory.drums[0].voicePool[rolla::Perc].empty(), "perc pool is the scanned folder");
        expect (factory.drums[0].sample.filter.on && factory.drums[0].sample.filter.lane == rolla::Drums,
                "tight kit filters the drum pack");
        expect (std::string (rolla::drumVoiceChoice (factory.drums[0], rolla::Kick, 0))
                    == "DRUMS/KICK/CD_Kick_06.wav",
                "the hero kick is only a hint until the index is up");
        expect (factory.bass[0].sample.styles != 0, "pluck is tagged to a dialect");
        expect (factory.bass[0].sample.filter.on && factory.bass[0].sample.filter.lane == rolla::Bass,
                "pluck draws from the bass folder");
        int bare = 0;
        for (int part = rolla::Bass; part <= rolla::Vox; ++part)
            for (int i = 0; i < factory.count[part]; ++i)
            {
                const auto& spec = rolla::specAt (factory, part, i);
                if (spec.mode != rolla::SampleMode::Sample || ! spec.filter.on || spec.filter.lane != part)
                    ++bare;
            }
        expect (factory.count[rolla::Bass] == 3, "bass stays three lane recipes");
        expect (factory.count[rolla::Bass] <= 16, "bass did not grow one preset per file");
        expect (bare == 0, "every default melodic preset is a sample recipe");
        bool phrases = false;
        bool chops = false;
        bool loops = false;
        bool atmospheres = false;
        for (int i = 0; i < factory.count[rolla::Vox]; ++i)
        {
            if (std::string (factory.vox[i].id) == "phrases"
                && std::string (factory.vox[i].sample.filter.category) == "phrase"
                && factory.vox[i].sample.filter.loop < 0)
                phrases = true;
            if (std::string (factory.vox[i].id) == "chops"
                && std::string (factory.vox[i].sample.filter.category) == "chop")
                chops = true;
            if (std::string (factory.vox[i].id) == "loops"
                && std::string (factory.vox[i].sample.filter.category) == "phrase"
                && factory.vox[i].sample.filter.loop == 1)
                loops = true;
        }
        for (int i = 0; i < factory.count[rolla::Pad]; ++i)
            if (std::string (factory.pad[i].id) == "atmospheres"
                && std::string (factory.pad[i].sample.filter.category) == "atmosphere")
                atmospheres = true;
        expect (phrases && chops && loops, "vox recipes include phrases, chops, and tempo-locked loops");
        expect (atmospheres, "pad recipes include atmospheres");
        const int first = rolla::pickPresetForRoll (factory, rolla::Bass, rolla::UkMin, 1);
        expect (first == 0 && factory.bass[first].sample.filter.on,
                "uk min roll stays on the pluck recipe");
        bool sameRecipe = true;
        for (std::uint32_t seed = 2; seed < 24; ++seed)
            if (rolla::pickPresetForRoll (factory, rolla::Bass, rolla::UkMin, seed) != first)
                sameRecipe = false;
        expect (sameRecipe, "the dialect picks the recipe; the folder scan picks the file");
        expect (rolla::pickPresetForRoll (factory, rolla::Drums, rolla::Gaskin, 3) == 1,
                "gaskin roll stays on the sample kit");
        for (int dialect = 0; dialect < rolla::NumDialects; ++dialect)
            for (std::uint32_t seed = 1; seed < 32; ++seed)
            {
                const int picked = rolla::pickPresetForRoll (factory, rolla::Drums, dialect, seed);
                expect (picked >= 0 && factory.drums[picked].sample.mode == rolla::SampleMode::Sample,
                        "roll stays on a sample kit");
            }

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
        expect (kept.kind == 1 && std::fabs (kept.synthGain) < 1.0e-4f,
                "a missing factory file stays silent when the pool is empty");
        expect (missingSampler.takeDrum (rolla::Kick, 100, factory, missingParams) < 0.001f,
                "a missing kick stays silent when the pool is empty");
        rolla::EngineParams noLib = missingParams;
        noLib.library = nullptr;
        const auto absentTake = missingSampler.take (rolla::Bass, 41, 100, 4.f, factory, noLib);
        expect (absentTake.kind == 1 && std::fabs (absentTake.synthGain) < 1.0e-4f,
                "an absent library stays silent");
        expect (missingSampler.takeDrum (rolla::Kick, 100, factory, noLib) < 0.001f,
                "an absent library does not play the 909");

        rolla::PresetBank swap;
        rolla::loadDefaultPresets (swap);
        swap.count[rolla::Bass] = 2;
        swap.bass[0].sample.mode = rolla::SampleMode::Sample;
        std::snprintf (swap.bass[0].sample.file, sizeof swap.bass[0].sample.file, "BASS/gone.wav");
        std::snprintf (swap.bass[0].id, sizeof swap.bass[0].id, "gone");
        swap.bass[1].sample.mode = rolla::SampleMode::Sample;
        std::snprintf (swap.bass[1].sample.file, sizeof swap.bass[1].sample.file, "BASS/alt.wav");
        std::snprintf (swap.bass[1].id, sizeof swap.bass[1].id, "alt");
        swap.bass[1].sample.root = 69;
        swap.count[rolla::Drums] = 1;
        swap.drums[0].sample.mode = rolla::SampleMode::Sample;
        swap.drums[0].voicePool[rolla::Kick].push_back ("DRUMS/KICK/gone.wav");
        swap.drums[0].voicePool[rolla::Kick].push_back ("DRUMS/KICK/alt.wav");
        std::snprintf (swap.drums[0].sample.voiceFile[rolla::Kick], 96, "DRUMS/KICK/gone.wav");
        const auto altBass = rootDir.getChildFile ("BASS/alt.wav");
        const auto altKick = rootDir.getChildFile ("DRUMS/KICK/alt.wav");
        writeTone (altBass, 0.25, 0.4f, 110.f, false);
        writeTone (altKick, 0.2, 0.5f, 80.f, false);
        rolla::SampleLibrary altLib;
        altLib.setRoot (rootDir);
        altLib.watch (swap);
        expect (waitUntil ([&] { return altLib.ready ("BASS/alt.wav") && altLib.missing ("BASS/gone.wav"); }),
                "alternate bass is ready and the missing one is flagged");
        expect (waitUntil ([&] { return altLib.ready ("DRUMS/KICK/alt.wav") && altLib.missing ("DRUMS/KICK/gone.wav"); }),
                "alternate kick is ready");
        rolla::Sampler altSampler;
        altSampler.prepare (48000.0);
        rolla::EngineParams altParams;
        altParams.presets = &swap;
        altParams.library = &altLib;
        const auto swapped = altSampler.take (rolla::Bass, 69, 110, 4.f, swap, altParams);
        expect (swapped.kind == 1 && std::fabs (swapped.synthGain) < 1.0e-6f,
                "a missing bass preset plays another bass sample");
        float altPeak = 0.f;
        for (int i = 0; i < 2000; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            altSampler.render (laneL, laneR, 0.f, 0.f, false);
            altPeak = std::max (altPeak, std::fabs (laneL[rolla::Bass]));
        }
        expect (altPeak > 0.01f, "the substitute bass sample is audible");
        expect (altSampler.takeDrum (rolla::Kick, 110, swap, altParams) < 0.001f,
                "a missing kick plays another kick, not the 909");
        float kickAlt = 0.f;
        for (int i = 0; i < 2000; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            altSampler.render (laneL, laneR, 0.f, 0.f, false);
            kickAlt = std::max (kickAlt, std::fabs (laneL[rolla::Drums]));
        }
        expect (kickAlt > 0.01f, "the substitute kick is audible");

        swap.drums[0].voicePool[rolla::Kick].clear();
        swap.drums[0].voicePool[rolla::Kick].push_back ("DRUMS/KICK/gone.wav");
        swap.drums[0].voicePool[rolla::Perc].clear();
        swap.drums[0].voicePool[rolla::Perc].push_back ("DRUMS/KICK/alt.wav");
        altSampler.reset();
        expect (altSampler.takeDrum (rolla::Kick, 110, swap, altParams) < 0.001f,
                "a missing kick folder does not play the circuit model");
        float packPeak = 0.f;
        for (int i = 0; i < 2000; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            altSampler.render (laneL, laneR, 0.f, 0.f, false);
            packPeak = std::max (packPeak, std::fabs (laneL[rolla::Drums]));
        }
        expect (packPeak > 0.01f, "a missing kick plays a perc sample from the pack");

        rolla::PresetBank packBank;
        rolla::loadDefaultPresets (packBank);
        packBank.count[rolla::Bass] = 1;
        packBank.bass[0].sample.mode = rolla::SampleMode::Sample;
        std::snprintf (packBank.bass[0].sample.file, sizeof packBank.bass[0].sample.file, "BASS/gone.wav");
        packBank.count[rolla::Drums] = 1;
        packBank.drums[0].sample.mode = rolla::SampleMode::Sample;
        packBank.drums[0].voicePool[rolla::Perc].push_back ("DRUMS/KICK/alt.wav");
        rolla::Sampler packSampler;
        packSampler.prepare (48000.0);
        rolla::EngineParams packParams;
        packParams.presets = &packBank;
        packParams.library = &altLib;
        const auto packed = packSampler.take (rolla::Bass, 48, 110, 4.f, packBank, packParams);
        expect (packed.kind == 1 && std::fabs (packed.synthGain) < 1.0e-6f,
                "a lane with no sample plays the drum pack, not the synth");
        float lanePack = 0.f;
        for (int i = 0; i < 2000; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            packSampler.render (laneL, laneR, 0.f, 0.f, false);
            lanePack = std::max (lanePack, std::fabs (laneL[rolla::Bass]));
        }
        expect (lanePack > 0.01f, "the drum-pack substitute is audible on the lane");
    }

    {
        const auto scanRoot = rootDir.getChildFile ("scan");
        scanRoot.deleteRecursively();
        writeTone (scanRoot.getChildFile ("BASS/a_low.wav"), 0.2, 0.85f, 110.f, false);
        writeTone (scanRoot.getChildFile ("BASS/b_high.wav"), 0.2, 0.2f, 220.f, false);
        writeTone (scanRoot.getChildFile ("BASS/extra.wav"), 0.15, 0.4f, 130.f, false);
        writeTone (scanRoot.getChildFile ("VOX/phrases/ah.wav"), 0.2, 0.5f, 440.f, false);
        writeTone (scanRoot.getChildFile ("VOX/chops/uh.wav"), 0.2, 0.5f, 523.f, false);
        writeTone (scanRoot.getChildFile ("PAD/atmospheres/bed.wav"), 0.3, 0.3f, 196.f, false);
        writeTone (scanRoot.getChildFile ("DRUMS/KICK/k1.wav"), 0.12, 0.8f, 70.f, false);
        writeTone (scanRoot.getChildFile ("DRUMS/KICK/k2.wav"), 0.12, 0.25f, 90.f, false);
        writeTone (scanRoot.getChildFile ("DRUMS/PERC/shaker.wav"), 0.12, 0.45f, 800.f, false);
        writeTone (scanRoot.getChildFile ("DRUMS/SHAKER/noise.wav"), 0.1, 0.3f, 1000.f, false);
        const char* manifest = R"JSON({
          "count": 4,
          "files": [
            {"file":"BASS/a_low.wav","lane":"BASS","root":"F1","source_pack":"Phantom","artist":"Julian Fijma","category":"one-shot","source":"Phantom/Julian Fijma/a.wav","presets":["bass.ukmin.pluck"]},
            {"file":"BASS/b_high.wav","lane":"BASS","root":"C2","source_pack":"Phantom","artist":"Julian Fijma","category":"one-shot"},
            {"file":"BASS/gone.wav","lane":"BASS","root":"D1","source_pack":"UNDRGRND","category":"one-shot"},
            {"file":"VOX/phrases/ah.wav","lane":"VOX","category":"phrase","source_pack":"Minimal Tech Vocals","artist":"Sine"},
            {"file":"VOX/chops/uh.wav","lane":"VOX","category":"chop","source_pack":"Minimal Tech Vocals"},
            {"file":"PAD/atmospheres/bed.wav","lane":"PAD","category":"atmosphere","source_pack":"Sine Artist Series"}
          ]
        })JSON";

        rolla::SampleLibrary scan;
        scan.setRoot (scanRoot);
        scan.startIndex (manifest, (int) std::strlen (manifest));
        expect (waitUntil ([&] { return scan.indexedFound() >= 0; }), "index publishes");
        expect (! scan.ready ("BASS/a_low.wav") && ! scan.missing ("BASS/a_low.wav"),
                "the scan does not preload audio");
        const auto snap = scan.index();
        expect (snap != nullptr && snap->found >= 9, "disk files are indexed");
        expect (snap->missing >= 1, "a manifest file that is not on disk counts as missing");
        rolla::PoolFilter bass;
        bass.on = true;
        bass.lane = rolla::Bass;
        const int bassCount = rolla::sampleIndexCount (*snap, rolla::Bass, bass);
        expect (bassCount >= 3, "the bass recipe sees the whole folder");
        const auto* firstBass = rolla::sampleIndexAt (*snap, rolla::Bass, bass, 0);
        const auto* secondBass = rolla::sampleIndexAt (*snap, rolla::Bass, bass, 1);
        expect (firstBass != nullptr && secondBass != nullptr
                    && std::strcmp (firstBass->path, secondBass->path) != 0,
                "roll ordinals are different files");
        expect (std::string (firstBass->pack) == "Phantom", "v2 pack metadata is kept");
        expect (std::string (firstBass->artist) == "Julian Fijma", "v2 artist metadata is kept");
        expect (firstBass->rootPc == 5, "manifest root wins");
        bool extra = false;
        bool shaker = false;
        for (const auto& entry : snap->entries)
        {
            if (std::string (entry.path) == "BASS/extra.wav" && entry.onDisk)
                extra = true;
            if (std::string (entry.path) == "DRUMS/SHAKER/noise.wav" && entry.voice == rolla::Perc && entry.onDisk)
                shaker = true;
        }
        expect (extra, "a file that is not in the manifest is still scanned");
        expect (shaker, "an unknown drum folder stays in the perc pool");
        rolla::PoolFilter phrase;
        phrase.on = true;
        phrase.lane = rolla::Vox;
        std::snprintf (phrase.category, sizeof phrase.category, "phrase");
        expect (rolla::sampleIndexCount (*snap, rolla::Vox, phrase) == 1, "phrase filter");
        rolla::PoolFilter chop;
        chop.on = true;
        chop.lane = rolla::Vox;
        std::snprintf (chop.category, sizeof chop.category, "chop");
        expect (rolla::sampleIndexCount (*snap, rolla::Vox, chop) == 1, "chop filter");
        rolla::PoolFilter atmosphere;
        atmosphere.on = true;
        atmosphere.lane = rolla::Pad;
        std::snprintf (atmosphere.category, sizeof atmosphere.category, "atmosphere");
        expect (rolla::sampleIndexCount (*snap, rolla::Pad, atmosphere) == 1, "atmosphere filter");

        rolla::PresetBank recipes;
        rolla::loadDefaultPresets (recipes);
        recipes.count[rolla::Bass] = 1;
        recipes.bass[0].sample.mode = rolla::SampleMode::Sample;
        recipes.bass[0].sample.filter.on = true;
        recipes.bass[0].sample.filter.lane = rolla::Bass;
        recipes.bass[0].sample.file[0] = 0;
        recipes.count[rolla::Drums] = 1;
        recipes.drums[0].sample.mode = rolla::SampleMode::Sample;
        recipes.drums[0].sample.filter.on = true;
        recipes.drums[0].sample.filter.lane = rolla::Drums;
        rolla::Sampler recipeSampler;
        recipeSampler.prepare (48000.0);
        rolla::EngineParams recipeParams;
        recipeParams.presets = &recipes;
        recipeParams.library = &scan;
        recipeParams.sampleIndex = snap.get();
        recipes.bass[0].sample.filter.rootPc = firstBass->rootPc;
        recipeParams.samplePick[rolla::Bass] = 0;
        const auto low = recipeSampler.take (rolla::Bass, 41, 110, 4.f, recipes, recipeParams);
        expect (low.kind == 1 && std::fabs (low.synthGain) < 1.0e-6f, "a recipe plays a scanned bass");
        expect (waitUntil ([&] { return scan.ready (firstBass->path); }), "the chosen bass loads on demand");
        expect (! scan.ready (secondBass->path), "the other bass stays unloaded");
        recipeSampler.take (rolla::Bass, 41, 110, 4.f, recipes, recipeParams);
        float lowPeak = 0.f;
        for (int i = 0; i < 1500; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            recipeSampler.render (laneL, laneR, 0.f, 0.f, false);
            lowPeak = std::max (lowPeak, std::fabs (laneL[rolla::Bass]));
        }
        recipeSampler.reset();
        recipes.bass[0].sample.filter.rootPc = secondBass->rootPc;
        recipeSampler.take (rolla::Bass, 41, 110, 4.f, recipes, recipeParams);
        expect (waitUntil ([&] { return scan.ready (secondBass->path); }), "the next roll loads the other bass");
        recipeSampler.take (rolla::Bass, 41, 110, 4.f, recipes, recipeParams);
        float highPeak = 0.f;
        for (int i = 0; i < 1500; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            recipeSampler.render (laneL, laneR, 0.f, 0.f, false);
            highPeak = std::max (highPeak, std::fabs (laneL[rolla::Bass]));
        }
        expect (lowPeak > 0.005f && highPeak > 0.005f,
                "two rolls draw audible files from the lane");

        recipeSampler.reset();
        recipeParams.samplePick[rolla::Drums] = 0;
        expect (recipeSampler.takeDrum (rolla::Kick, 100, recipes, recipeParams) < 0.001f,
                "a scanned kick does not play the circuit model");
        expect (waitUntil ([&] { return scan.ready ("DRUMS/KICK/k1.wav"); }), "kick loads on demand");
        expect (! scan.ready ("DRUMS/KICK/k2.wav"), "the other kick stays unloaded");
        recipeSampler.takeDrum (rolla::Kick, 100, recipes, recipeParams);
        float scannedKick = 0.f;
        for (int i = 0; i < 1200; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            recipeSampler.render (laneL, laneR, 0.f, 0.f, false);
            scannedKick = std::max (scannedKick, std::fabs (laneL[rolla::Drums]));
        }
        expect (scannedKick > 0.01f, "the scanned kick is audible");

        const auto cache = scanRoot.getChildFile (".rolla-index.json");
        expect (cache.existsAsFile(), "the index is cached beside the samples");
        auto cachedText = cache.loadFileAsString();
        expect (cachedText.contains ("Phantom"), "cache keeps pack metadata");
        cachedText = cachedText.replace ("Phantom", "CachedPack");
        cache.replaceWithText (cachedText);
        rolla::SampleLibrary again;
        again.setRoot (scanRoot);
        again.startIndex (manifest, (int) std::strlen (manifest));
        expect (waitUntil ([&] { return again.indexedFound() >= 0; }), "cached index publishes");
        const auto cachedSnap = again.index();
        bool cachedPack = false;
        if (cachedSnap != nullptr)
            for (const auto& entry : cachedSnap->entries)
                if (std::string (entry.pack) == "CachedPack")
                    cachedPack = true;
        expect (cachedPack, "a matching signature reuses the cache");
        expect (again.indexedFound() == scan.indexedFound(), "the cached scan counts the same files");

        scanRoot.getChildFile ("SAMPLE_MANIFEST_v2.json").replaceWithText (
            R"JSON({"files":[{"file":"BASS/a_low.wav","lane":"BASS","root":"F1","source_pack":"Override","artist":"Disk","category":"phrase"}]})JSON");
        rolla::SampleLibrary disk;
        disk.setRoot (scanRoot);
        disk.startIndex (manifest, (int) std::strlen (manifest));
        expect (waitUntil ([&] { return disk.indexedFound() >= 0; }), "disk manifest is indexed");
        const auto diskSnap = disk.index();
        bool overridden = false;
        if (diskSnap != nullptr)
            for (const auto& entry : diskSnap->entries)
                if (std::string (entry.path) == "BASS/a_low.wav"
                    && std::string (entry.pack) == "Override"
                    && std::string (entry.category) == "phrase")
                    overridden = true;
        expect (overridden, "SAMPLE_MANIFEST_v2.json overrides the embedded manifest");
    }

    expect (std::fabs (rolla::phraseTempoStretch (120, 60.f) - 0.5f) < 1.0e-4f, "half tempo stretches to 0.5");
    expect (std::fabs (rolla::phraseTempoStretch (100, 500.f) - 4.f) < 1.0e-4f, "stretch clamps at 4");
    expect (std::fabs (rolla::phraseTempoStretch (10, 120.f) - 1.f) < 1.0e-4f, "a file without a tempo stays at unity");

    {
        const auto dualRoot = rootDir.getChildFile ("dual");
        writeTone (dualRoot.getChildFile ("PLUCK/hit.wav"), 0.2, 0.4f, 880.f, false);
        writeTone (dualRoot.getChildFile ("DRUMS/RIM/tick.wav"), 0.1, 0.5f, 1200.f, false);
        writeBurst (dualRoot.getChildFile ("VOX/phrase_120_ah.wav"), 48000, 4800);
        writeTone (dualRoot.getChildFile ("VOX/quiet.wav"), 0.4, 0.01f, 220.f, false);
        const char* manifest = R"JSON({
          "files": [
            {"file":"PLUCK/hit.wav","lane":"PLUCK","category":"synth_hit_short","alt_lanes":["STAB","PLUCK"]},
            {"file":"DRUMS/RIM/tick.wav","lane":"DRUMS","voice":"RIM","category":"rim_alt_short","alt_lanes":["DRUMS/RIM","DRUMS/PERC"]},
            {"file":"VOX/phrase_120_ah.wav","lane":"VOX","category":"vox_phrase_loop"},
            {"file":"VOX/quiet.wav","lane":"VOX","category":"vox_shot"}
          ]
        })JSON";
        rolla::SampleLibrary dual;
        dual.setRoot (dualRoot);
        dual.startIndex (manifest, (int) std::strlen (manifest));
        expect (waitUntil ([&] { return dual.indexedFound() >= 0; }), "dual-lane index publishes");
        const auto snap = dual.index();
        expect (snap != nullptr, "dual-lane snapshot");
        rolla::PoolFilter stab;
        stab.on = true;
        stab.lane = rolla::Stab;
        rolla::PoolFilter pluck;
        pluck.on = true;
        pluck.lane = rolla::Pluck;
        rolla::PoolFilter rim;
        rim.on = true;
        rim.lane = rolla::Drums;
        rim.voice = rolla::Rim;
        rolla::PoolFilter perc;
        perc.on = true;
        perc.lane = rolla::Drums;
        perc.voice = rolla::Perc;
        expect (snap != nullptr && rolla::sampleIndexCount (*snap, rolla::Stab, stab) == 1, "a pluck flagged for either lane is in the stab pool");
        expect (snap != nullptr && rolla::sampleIndexCount (*snap, rolla::Pluck, pluck) == 1, "the same pluck stays in the pluck pool");
        expect (snap != nullptr && rolla::sampleIndexCount (*snap, rolla::Drums, rim) == 1, "the rim stays in the rim pool");
        expect (snap != nullptr && rolla::sampleIndexCount (*snap, rolla::Drums, perc) == 1, "a rim flagged for either lane is in the perc pool");
        rolla::PoolFilter loops;
        loops.on = true;
        loops.lane = rolla::Vox;
        loops.loop = 1;
        std::snprintf (loops.category, sizeof loops.category, "phrase");
        const auto* loop = snap != nullptr ? rolla::sampleIndexAt (*snap, rolla::Vox, loops, 0) : nullptr;
        expect (loop != nullptr && loop->phraseLoop && loop->fileBpm == 120, "a vox_phrase_loop is a 120 bpm phrase");
        rolla::PoolFilter shots;
        shots.on = true;
        shots.lane = rolla::Vox;
        shots.loop = 0;
        std::snprintf (shots.category, sizeof shots.category, "chop");
        expect (snap != nullptr && rolla::sampleIndexCount (*snap, rolla::Vox, shots) == 1, "a vox shot is a chop, not a phrase loop");

        rolla::PresetBank phraseBank;
        rolla::loadDefaultPresets (phraseBank);
        phraseBank.count[rolla::Vox] = 1;
        phraseBank.vox[0].decayMs = 8000.f;
        phraseBank.vox[0].brightness = 4.f;
        phraseBank.vox[0].sample.mode = rolla::SampleMode::Sample;
        phraseBank.vox[0].sample.filter = loops;
        rolla::Sampler phraseSampler;
        phraseSampler.prepare (48000.0);
        rolla::EngineParams phraseParams;
        phraseParams.presets = &phraseBank;
        phraseParams.library = &dual;
        phraseParams.sampleIndex = snap.get();
        phraseParams.bpm = 60.f;
        phraseSampler.take (rolla::Vox, 60, 110, 16.f, phraseBank, phraseParams);
        expect (waitUntil ([&] { return dual.ready ("VOX/phrase_120_ah.wav"); }), "the phrase loop loads");
        phraseSampler.take (rolla::Vox, 60, 110, 16.f, phraseBank, phraseParams);
        int lastLoud = 0;
        for (int i = 0; i < 24000; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            phraseSampler.render (laneL, laneR, 0.f, 0.f, false);
            if (std::fabs (laneL[rolla::Vox]) > 0.004f)
                lastLoud = i;
        }
        expect (lastLoud > 7000 && lastLoud < 14000,
                "half tempo holds a 100 ms phrase burst for about 200 ms");

        const auto quietFile = dualRoot.getChildFile ("VOX/quiet.wav");
        const auto quietSum = checksum (quietFile);
        dual.request ("VOX/quiet.wav");
        expect (waitUntil ([&] { return dual.ready ("VOX/quiet.wav"); }), "the quiet file loads");
        float boosted = 0.f;
        if (const auto* audio = dual.tryGet ("VOX/quiet.wav"))
            for (float s : audio->left)
                boosted = std::max (boosted, std::fabs (s));
        expect (boosted > 0.04f, "load-time gain lifts a quiet raw copy");
        expect (checksum (quietFile) == quietSum, "loudness normalisation does not write the wav");
        auto cacheText = dualRoot.getChildFile (".rolla-loudness.json").loadFileAsString();
        expect (cacheText.contains ("VOX/quiet.wav"), "the gain is cached");
        cacheText = cacheText.replace ("\"gainDb\":", "\"gainDb\": 0, \"was\":");
        dualRoot.getChildFile (".rolla-loudness.json").replaceWithText (cacheText);
        rolla::SampleLibrary cached;
        cached.setRoot (dualRoot);
        cached.request ("VOX/quiet.wav");
        expect (waitUntil ([&] { return cached.ready ("VOX/quiet.wav"); }), "the cached gain loads");
        float cachedPeak = 0.f;
        if (const auto* audio = cached.tryGet ("VOX/quiet.wav"))
            for (float s : audio->left)
                cachedPeak = std::max (cachedPeak, std::fabs (s));
        expect (cachedPeak < 0.03f, "an edited cache gain is applied instead of remeasuring");
        expect (checksum (quietFile) == quietSum, "the cached gain still leaves the wav untouched");
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
