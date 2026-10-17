#pragma once

#include "../Engine/SampleSpec.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rolla
{

// 14 user slots: six melodic lanes + eight 909 voices.
// PERC is stored and loaded; the pattern generator has no PERC voice yet.
inline constexpr int kUserSlotCount = 14;

enum UserSlotId : int
{
    SlotBass = 0,
    SlotStab,
    SlotHits,
    SlotPluck,
    SlotPad,
    SlotVox,
    SlotKick,
    SlotClap,
    SlotSnare,
    SlotRim,
    SlotCh,
    SlotOh,
    SlotRide,
    SlotPerc
};

inline const char* userSlotName (int slot) noexcept
{
    static const char* names[] = {
        "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX",
        "KICK", "CLAP", "SNARE", "RIM", "CH", "OH", "RIDE", "PERC"
    };
    if (slot < 0 || slot >= kUserSlotCount)
        return "";
    return names[slot];
}

// Relative folder under the samples root, and under Samples/USER/ for copies.
inline const char* userSlotFolder (int slot) noexcept
{
    static const char* folders[] = {
        "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX",
        "DRUMS/KICK", "DRUMS/CLAP", "DRUMS/SNARE", "DRUMS/RIM",
        "DRUMS/CH", "DRUMS/OH", "DRUMS/RIDE", "DRUMS/PERC"
    };
    if (slot < 0 || slot >= kUserSlotCount)
        return "";
    return folders[slot];
}

inline bool userSlotIsDrum (int slot) noexcept
{
    return slot >= SlotKick && slot < kUserSlotCount;
}

inline int userSlotForPart (int part) noexcept
{
    if (part >= SlotBass && part <= SlotVox)
        return part;
    return -1;
}

inline int userSlotForDrum (int voice) noexcept
{
    switch (voice)
    {
        case Kick: return SlotKick;
        case Clap: return SlotClap;
        case Snare: return SlotSnare;
        case Rim: return SlotRim;
        case HatC: return SlotCh;
        case HatO: return SlotOh;
        case Ride: return SlotRide;
        default: return -1;
    }
}

// PRESET_SPEC stores loop points in file-rate frames (often thousands).
// The engine also accepts seconds, which stay well below one loop of audio.
inline bool loopValuesAreFrames (float start, float end, float xfade) noexcept
{
    const float peak = std::max (start, std::max (end, xfade));
    return peak > 32.f;
}

struct LoopRegion
{
    double a = 0.0;
    double b = 0.0;
    double xfade = 0.0;
};

inline LoopRegion makeLoopRegion (const SampleLoop& loop, double rate, int frames, double startFrame) noexcept
{
    LoopRegion region;
    const double fileEnd = frames > 0 ? (double) frames - 1.0 : 0.0;
    const bool asFrames = loopValuesAreFrames (loop.start, loop.end, loop.xfade);
    if (asFrames)
    {
        region.a = loop.start > 0.f ? (double) loop.start : startFrame;
        region.b = loop.end > loop.start ? (double) loop.end : fileEnd;
        region.xfade = std::max (0.f, loop.xfade);
    }
    else
    {
        const double sr = rate > 1000.0 ? rate : 48000.0;
        region.a = loop.start > 0.f ? (double) loop.start * sr : startFrame;
        region.b = loop.end > loop.start ? (double) loop.end * sr : fileEnd;
        region.xfade = std::max (0.f, loop.xfade) * sr;
    }
    if (region.a < 0.0)
        region.a = 0.0;
    if (region.b > fileEnd)
        region.b = fileEnd;
    if (region.b < region.a)
        region.b = fileEnd;
    return region;
}

struct SlotQuery
{
    int mode = 0; // 0 off, 1 sample, 2 layer
    bool ready = false;
    bool missing = false;
    bool loading = false;
    const char* file = "";
    int root = 60;
    float cents = 0.f;
    float start = 0.f;
    bool reverse = false;
    bool loop = false;
    float loopStart = 0.f;
    float loopEnd = 0.f;
    float loopXfade = 0.f;
    float layerMix = 0.5f;
    float gainDb = 0.f;
};

struct PresetQuery
{
    SampleMode mode = SampleMode::Synth;
    const char* file = "";
    int root = 60;
    float start = 0.f;
    bool reverse = false;
    SampleLoop loop {};
    float layerMix = 0.5f;
    bool ready = false;
    bool missing = false;
};

// What the audio thread should do for one note. No allocation.
struct PlayDecision
{
    int kind = 0;             // 0 synth, 1 sample replaces synth, 2 layer
    float synthGain = 1.f;
    float sampleMix = 1.f;
    float gainDb = 0.f;
    const char* file = "";
    int root = 60;
    float cents = 0.f;
    float startSec = 0.f;
    bool reverse = false;
    bool loop = false;
    float loopStart = 0.f;
    float loopEnd = 0.f;
    float loopXfade = 0.f;
    bool fromSlot = false;
    bool suppressPreset = false; // slot owns the voice; do not also play the preset file
    bool warning = false;
};

inline PlayDecision resolvePlay (const PresetQuery& preset, const SlotQuery* slot) noexcept
{
    PlayDecision decision;
    const bool slotOn = slot != nullptr && slot->mode != 0;
    if (slotOn && slot->ready)
    {
        decision.fromSlot = true;
        decision.suppressPreset = true;
        decision.file = slot->file != nullptr ? slot->file : "";
        decision.root = slot->root;
        decision.cents = slot->cents;
        decision.startSec = std::max (0.f, slot->start);
        decision.reverse = slot->reverse;
        decision.loop = slot->loop;
        decision.loopStart = slot->loopStart;
        decision.loopEnd = slot->loopEnd;
        decision.loopXfade = slot->loopXfade;
        decision.gainDb = slot->gainDb;
        decision.kind = slot->mode == (int) SampleMode::Layer ? 2 : 1;
        if (decision.kind == 1)
        {
            decision.sampleMix = 1.f;
            decision.synthGain = 0.f;
        }
        else
        {
            const float mix = std::clamp (slot->layerMix, 0.f, 1.f);
            decision.sampleMix = mix;
            decision.synthGain = 1.f - mix;
        }
        return decision;
    }

    if (slotOn && slot->loading && ! slot->missing)
    {
        // The slot will replace the preset file. Until prep finishes, keep the synth.
        decision.suppressPreset = true;
        decision.kind = 0;
        decision.synthGain = 1.f;
        return decision;
    }

    if (slotOn && (slot->missing || slot->file == nullptr || slot->file[0] == 0))
        decision.warning = true;

    const bool presetNamed = preset.file != nullptr && preset.file[0] != 0;
    const bool presetMissing = preset.mode != SampleMode::Synth
                            && (! presetNamed || preset.missing);
    if (presetMissing)
        decision.warning = true;

    if (preset.mode == SampleMode::Synth || ! presetNamed || preset.missing || ! preset.ready)
    {
        decision.kind = 0;
        decision.synthGain = 1.f;
        return decision;
    }

    decision.kind = preset.mode == SampleMode::Layer ? 2 : 1;
    decision.file = preset.file;
    decision.root = preset.root;
    decision.startSec = std::max (0.f, preset.start);
    decision.reverse = preset.reverse;
    decision.loop = preset.loop.on;
    decision.loopStart = preset.loop.start;
    decision.loopEnd = preset.loop.end;
    decision.loopXfade = preset.loop.xfade;
    if (decision.kind == 1)
    {
        decision.sampleMix = 1.f;
        decision.synthGain = 0.f;
    }
    else
    {
        const float mix = std::clamp (preset.layerMix, 0.f, 1.f);
        decision.sampleMix = mix;
        decision.synthGain = 1.f - mix;
    }
    return decision;
}

inline bool sampleBytesFit (std::uint64_t resident, std::uint64_t incoming, std::uint64_t budget = 128ull * 1024ull * 1024ull) noexcept
{
    return incoming <= budget && resident <= budget - incoming;
}

} // namespace rolla
