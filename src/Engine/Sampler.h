#pragma once

#include "SampleLibrary.h"
#include "../DspUtil.h"
#include "../Generator/Presets.h"

namespace rolla
{

struct EngineParams;

// Realtime sample voices. take() / takeDrum() run on the audio thread and only
// read a published SampleAudio. Rendering uses a 16-tap windowed sinc.
class Sampler
{
public:
    struct Take
    {
        int kind = 0;       // 0 synth only, 1 sample replaces synth, 2 layer
        float synthGain = 1.f;
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    Take take (int part, int midiNote, int velocity, float end16,
               const PresetBank& bank, const EngineParams& params) noexcept;

    // Returns the gain the synthesized drum voice should keep (0 = sample replaced it).
    float takeDrum (int voice, int velocity, const PresetBank& bank, const EngineParams& params) noexcept;
    void releaseNote (int part, int midiNote) noexcept;

    void releaseAll() noexcept;
    void render (float* laneL, float* laneR, float prev16, float pos16, bool playing) noexcept;

private:
    struct Voice
    {
        bool on = false;
        int part = 0;
        int midiNote = 60;
        int age = 0;
        const SampleAudio* audio = nullptr;
        bool reverse = false;
        double pos = 0.0;
        double ratio = 1.0;
        double loopA = 0.0;
        double loopB = 0.0;
        double xfade = 0.0;
        bool loop = false;
        float fade = 0.f;
        float fadeInc = 1.f;
        bool stopping = false;
        float gain = 0.2f;
        float pan = 0.f;
        float end16 = 1.0e9f;
        float cut = 8000.f;
        float res = 0.05f;
        float envOct = 0.f;
        float fEnv = 1.f;
        float fCoef = 0.99f;
        float hpfHz = 20.f;
        float hpState = 0.f;
        Ladder ladder;
        struct Env
        {
            int stage = 0;
            float amp = 0, sustain = 0, attInc = 0, decC = 0, relC = 0;
            void start (float aMs, float dMs, float sGain, float rMs, float sr) noexcept;
            void release() noexcept;
            bool tick() noexcept;
        } env;
    };

    Voice* allocate (int part) noexcept;
    float readSinc (const std::vector<float>& src, double index) const noexcept;
    void readVoice (Voice& v, float& l, float& r) noexcept;
    // True when a user slot owns this note (playing, or still loading).
    bool playUserSlot (int part, int drumVoice, int midiNote, int velocity, float end16,
                       const PresetBank& bank, const EngineParams& params,
                       Take& take, float& drumKeep) noexcept;

    double sr = 48000.0;
    int serial = 1;
    int sliceCursor[NumParts] {};
    Voice voices[40];
};

const SampleSpec& sampleSpecFor (const PresetBank& bank, int part, int preset) noexcept;

} // namespace rolla
