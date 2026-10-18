#pragma once

#include "SampleLibrary.h"
#include <atomic>
#include <string>
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
        // The sample is a recorded chord: it already sounds the other chord
        // tones, so the caller plays it once per chord, not once per note.
        bool chordFile = false;
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // ask: the chord this note opens (empty for a single note). A chord file plays
    // from that chord's root, and moves to a root where its quality fits.
    Take take (int part, int midiNote, int velocity, float end16,
               const PresetBank& bank, const EngineParams& params,
               ChordAsk ask = {}) noexcept;

    // Returns the gain the synthesized drum voice should keep (0 = sample replaced it).
    // gainScale: per-hit accent (DRUMS ACCENT on the downbeat, the voice's ACCENT).
    float takeDrum (int voice, int velocity, const PresetBank& bank, const EngineParams& params,
                    float gainScale = 1.f) noexcept;
    void releaseNote (int part, int midiNote) noexcept;

    void releaseAll() noexcept;
    // loopEnd16 > 0 fades every voice out at the phrase end so nothing runs into the next bar.
    void render (float* laneL, float* laneR, float prev16, float pos16, bool playing, float loopEnd16 = 0.f) noexcept;

    // The file the last note on a lane (or drum voice) started, library-relative;
    // empty if nothing played. Read from the message thread by tests and status.
    std::string lastPickedFile (int part, int drumVoice = -1) const;

    // Filled by render(). Melodic playback stays on the lane buses; drum hits are also split per voice.
    float drumOutL[NumDrumVoices] {};
    float drumOutR[NumDrumVoices] {};

private:
    std::atomic<const SampleAudio*> lastAudio[NumParts] {};
    std::atomic<const SampleAudio*> lastDrumAudio[NumDrumVoices] {};
    void notePicked (int part, int drumVoice, const SampleAudio* audio) noexcept
    {
        if (audio == nullptr)
            return;
        if (drumVoice >= 0 && drumVoice < NumDrumVoices)
            lastDrumAudio[drumVoice].store (audio, std::memory_order_relaxed);
        else if (part >= 0 && part < NumParts)
            lastAudio[part].store (audio, std::memory_order_relaxed);
    }

    struct Voice
    {
        bool on = false;
        // Set for the sample that starts the voice. The transport rewind that
        // opens a downbeat looks like a loop wrap, and that wrap must not kill
        // a voice that has not sounded yet.
        bool born = false;
        int part = 0;
        int drumVoice = -1;
        int midiNote = 60;
        int age = 0;
        const SampleAudio* audio = nullptr;
        bool reverse = false;
        double pos = 0.0;
        double ratio = 1.0;
        double loopA = 0.0;
        double loopB = 0.0;
        double xfade = 0.0;
        // In-memory slice of a longer file. chopEnd 0 plays the whole buffer.
        // The published SampleAudio is never edited.
        double chopStart = 0.0;
        double chopEnd = 0.0;
        int chopFade = 0;
        bool loop = false;
        // Phrase loops keep their pitch and follow the host tempo.
        bool tempoSync = false;
        float stretch = 1.f;
        double anchor = 0.0;
        double splicePos = 0.0;
        int spliceLeft = 0;
        float fade = 1.f;
        float fadeInc = 0.f;
        bool stopping = false;
        // Raised-cosine edges. A linear step at a sample boundary is a click.
        int attackLeft = 0;
        int attackN = 0;
        int releaseLeft = 0;
        int releaseN = 0;
        float noteEdge = 1.f;
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
    void park (const Voice& dying) noexcept;
    void armAttack (Voice& voice) noexcept;
    float readSinc (const std::vector<float>& src, double index) const noexcept;
    void readVoice (Voice& v, float& l, float& r) noexcept;
    // True when a user slot owns this note (playing, or still loading).
    bool playUserSlot (int part, int drumVoice, int midiNote, int velocity, float end16,
                       const PresetBank& bank, const EngineParams& params,
                       Take& take, float& drumKeep) noexcept;

    double sr = 48000.0;
    // takeDrum's gainScale, seen by playUserSlot for a drum voice. Audio thread only.
    float drumGain = 1.f;
    int serial = 1;
    int sliceCursor[NumParts] {};
    Voice voices[40];
    Voice tails[16];
};

const SampleSpec& sampleSpecFor (const PresetBank& bank, int part, int preset) noexcept;

} // namespace rolla
