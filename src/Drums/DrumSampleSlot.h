#pragma once

#include "../Generator/Pattern.h"

namespace rolla
{

// Per-voice sample hook for the sampler engineer.
//
// Files live under the samples root, which the plugin already creates:
//   macOS / Linux: ~/Music/ROLLA/Samples/DRUMS/<folder>/
//   Windows:       %USERPROFILE%\Music\ROLLA\Samples\DRUMS/<folder>/
// <folder> is KICK, SNARE, CLAP, RIM, CH, OH, RIDE, or PERC (both toms).
// `key` matches the presets.json voices.* field (tomLow / tomMid share PERC).
//
// The audio thread calls trigger() when a hit is not already replaced by a
// dropped WAV, then render() once per sample from the drum machine.
// trigger() returns the circuit-model gain to keep:
//   1 = this hook did not play a sample, 0 = the sample replaced the model.
// Factory kits do not use that 1. A missing drum file is filled from the
// DRUMS pack (PERC, OH, RIDE, then the other folders), not from the model.
// trigger() and render() must not allocate or lock.

struct DrumSampleRequest
{
    int voice = Kick;
    int velocity = 100;
    float tuneRatio = 1.f;
    float decayScale = 1.f;
    float levelGain = 1.f;
    const char* folder = "KICK";
    const char* key = "kick";
    int midiNote = 36;
};

class DrumSampleSlot
{
public:
    virtual ~DrumSampleSlot() = default;
    virtual void prepare (double sampleRate) { (void) sampleRate; }
    virtual void reset() noexcept {}
    virtual float trigger (const DrumSampleRequest&) noexcept { return 1.f; }
    virtual void render (float& left, float& right) noexcept
    {
        (void) left;
        (void) right;
    }
};

inline const char* drumSampleFolder (int voice) noexcept
{
    switch (voice)
    {
        case Kick: return "KICK";
        case Snare: return "SNARE";
        case Clap: return "CLAP";
        case Rim: return "RIM";
        case HatC: return "CH";
        case HatO: return "OH";
        case Ride: return "RIDE";
        case Perc:
        case TomLow:
        case TomMid: return "PERC";
        default: return "PERC";
    }
}

inline const char* drumVoiceKey (int voice) noexcept
{
    switch (voice)
    {
        case Kick: return "kick";
        case Snare: return "snare";
        case Clap: return "clap";
        case Rim: return "rim";
        case TomLow: return "tomLow";
        case TomMid: return "tomMid";
        case HatC: return "hatC";
        case HatO: return "hatO";
        case Ride: return "ride";
        case Perc: return "perc";
        default: return "perc";
    }
}

} // namespace rolla
