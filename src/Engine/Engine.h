#pragma once

#include "../Generator/Pattern.h"
#include "../Generator/Presets.h"
#include "SampleLibrary.h"
#include "Sampler.h"

#include <atomic>
#include <memory>
#include <vector>

#include <juce_audio_basics/juce_audio_basics.h>

namespace rolla
{

struct UserSlotView;

struct EngineParams
{
    float tone[NumParts] {};
    float punch[NumParts] {};
    float levelDb[NumParts] {};
    bool mute[NumParts] {};
    int preset[NumParts] {};
    bool duck = true;
    bool drumsToMain = true;
    float swing = 0.52f;
    float outputDb = 0.f;
    float drumsAccent = 0.6f;
    // Per-voice drum panel. Defaults are an identity: tune 0 st, decay 0.5, level 0 dB, accent off.
    float drumTuneSt[NumDrumVoices] {};
    float drumDecay[NumDrumVoices] {};
    float drumLevelDb[NumDrumVoices] {};
    bool drumAccentOn[NumDrumVoices] {};
    float drumAccentAmt[NumDrumVoices] {};
    int bars = 8;
    int key = 2;
    int dialect = UkMin;
    const PresetBank* presets = nullptr;
    const SampleLibrary* library = nullptr;
    // Published by UserSlots. Null keeps the preset sample block only.
    const UserSlotView* userSlots = nullptr;
    int samplePick[NumParts] {};
};

struct Transport
{
    bool playing = false;
    double ppqStart = 0.0;
    double bpm = 130.0;
    double sampleRate = 48000.0;
};

// Realtime groove engine. prepare/reset/loadSample run off the audio thread.
// process() does not allocate or take locks.
class Engine
{
public:
    void prepare (double sampleRate, int maxBlock);
    void reset() noexcept;
    void process (const Groove& groove,
                  const EngineParams& params,
                  const Transport& transport,
                  float* mainL, float* mainR,
                  float* drumL, float* drumR,
                  int numSamples,
                  juce::MidiBuffer& midi,
                  bool patternMidi);

    // Message thread. Copies and resamples into a free buffer, then publishes.
    void loadDrumSample (int voice, const float* mono, int numSamples, double sourceRate);
    void clearDrumSample (int voice);
    bool hasDrumSample (int voice) const noexcept;

private:
    double sr = 48000.0;
    struct SampleSlot
    {
        static constexpr int kBufs = 3;
        std::vector<float> buf[kBufs];
        int length[kBufs] {};
        std::vector<float> source;
        double sourceRate = 0.0;
        std::atomic<int> published { 0 };
        std::atomic<int> reader { -1 };
        std::atomic<bool> loaded { false };
    };

    SampleSlot samples[NumDrumVoices];

    struct Impl;
    std::unique_ptr<Impl> impl;
    Sampler sampler;
    bool prepared = false;
    bool wasPlaying = false;

public:
    Engine();
    ~Engine();
    Engine (const Engine&) = delete;
    Engine& operator= (const Engine&) = delete;
};

} // namespace rolla
