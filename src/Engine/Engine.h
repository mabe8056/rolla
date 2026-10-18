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
class DrumSampleSlot;
struct MixProgram;

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
    // Published sample-folder index. Null until the background scan finishes.
    const SampleIndex* sampleIndex = nullptr;
    // Published by UserSlots. Null keeps the preset sample block only.
    const UserSlotView* userSlots = nullptr;
    int samplePick[NumParts] {};
    float bpm = 130.f;
    // Baked mix strip (MIX_SPEC). Null keeps the legacy sum.
    const MixProgram* mix = nullptr;
    // Per-909-voice mute used by the offline render set. The lane mute still silences every voice.
    bool drumVoiceMute[NumDrumVoices] {};
    // -1: E21 mask from the active lanes. 0..63: frozen mask (FULL / MUTE / SOLO share one trim).
    int sumMask = -1;
    bool bypassSumTrim = false;
    // Master true-peak safety stage, ceiling -6 dBTP (src/Mix/TruePeakGuard.h).
    // Off keeps the same latency and passes the sum through unchanged.
    bool tpGuard = true;
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
    // Fixed output delay of the true-peak stage at the prepared rate (on or off).
    int latencySamples() const noexcept;
    // Audio thread or a test harness: largest true-peak gain reduction since the last call, dB.
    float takeTpGuardGrDb() noexcept;
    bool hasDrumSample (int voice) const noexcept;

    // Library-relative file the last note on a lane (or drum voice) started. Tests and status only.
    std::string lastPickedFile (int part, int drumVoice = -1) const { return sampler.lastPickedFile (part, drumVoice); }

    // Per-voice sample hook (src/Drums/DrumSampleSlot.h). Null keeps the
    // preset sampler. A dropped WAV still wins over this slot.
    void setDrumSampleSlot (DrumSampleSlot* slot) noexcept;

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
