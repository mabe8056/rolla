#pragma once

// Export helpers shared by the plugin's drag-out / export buttons and
// rolla-export-test. Header-only so every target that builds Engine.cpp can
// use it without a CMake change.
//
//  - File names carry style, key, tempo, lane and seed:
//      ROLLA_BOUNCY_Dmin_130bpm_bass_1a2b3c4d.mid
//  - renderLoop() bounces exactly one loop (LENGTH) through the plugin path
//    (mix program, E21 trim, master tp_guard). It starts a few seconds before
//    the loop end, so the kept loop opens with its own reverb / delay tails and
//    repeats seamlessly, and it drops the guard's latency so the downbeat is
//    sample 0. If the bounce had to ask the sample library for a file (lazy
//    load right after a roll), it waits for the loader and bounces again.

#include "../Engine/Engine.h"
#include "../Engine/SampleLibrary.h"
#include "../Generator/Pattern.h"
#include "../Generator/Presets.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstdint>

namespace rolla::rollexport
{

inline const char* styleTag (int dialect) noexcept
{
    return kStyleLabels[std::clamp (dialect, 0, NumDialects - 1)];
}

// One id per roll: every lane seed, mixed. Lane files use their own lane seed.
inline std::uint32_t rollSeed (const Groove& g) noexcept
{
    std::uint32_t h = 2166136261u;
    auto mixIn = [&h] (std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
        {
            h ^= (v >> (i * 8)) & 0xffu;
            h *= 16777619u;
        }
    };
    for (int p = 0; p < NumParts; ++p)
        mixIn (p == Drums ? g.drums.seed : g.lane[p].seed);
    mixIn ((std::uint32_t) g.dialect);
    return h;
}

inline std::uint32_t laneSeed (const Groove& g, int part) noexcept
{
    if (part == Drums)
        return g.drums.seed;
    return (part >= 0 && part < NumParts) ? g.lane[part].seed : rollSeed (g);
}

inline juce::String keyTag (int key)
{
    // "C#" -> "Cs": '#' is legal on macOS but trips some DAW browsers.
    return juce::String (keyName (key)).replace ("#", "s") + "min";
}

// part >= 0: one lane. part == kFull: every lane (MIDI) / the full mix (audio).
inline constexpr int kFull = -1;

inline juce::String baseName (const Groove& g, int key, double bpm, int part)
{
    const juce::String lane = part == kFull ? juce::String ("full") : juce::String (partId (std::clamp (part, 0, NumParts - 1)));
    const std::uint32_t seed = part == kFull ? rollSeed (g) : laneSeed (g, part);
    return "ROLLA_" + juce::String (styleTag (g.dialect)) + "_" + keyTag (key) + "_"
           + juce::String ((int) std::lround (bpm)) + "bpm_" + lane + "_"
           + juce::String::toHexString ((juce::int64) seed).paddedLeft ('0', 8);
}

// The engine plays the bass from the active preset's octave (after the style's
// bass override). MidiExport derives it from the style instead (BOUNCY 2, the
// rest 1), so a preset that disagrees exported the bass an octave off. Shift
// the bass offsets so the file holds the notes that play.
// TODO: once src/Generator/ is free, pass the octave into MidiExport instead.
inline Groove midiGroove (const Groove& g, const PresetBank* bank, int bassPreset, int dialect)
{
    Groove out = g;
    if (bank == nullptr)
        return out;
    BassPatch bp = bank->bass[presetSlot (*bank, Bass, bassPreset)];
    applyDialectBass (*bank, dialect, bp);
    const int playedOctave = std::clamp (bp.octave, 0, 3);
    const int fileOctave = g.dialect == Pop ? 2 : 1;
    const int shift = 12 * (playedOctave - fileOctave);
    if (shift == 0)
        return out;
    auto& lane = out.lane[Bass];
    for (int i = 0; i < lane.count; ++i)
        for (int c = 0; c < std::max (1, (int) lane.notes[i].nChord) && c < 4; ++c)
            lane.notes[i].chord[c] = (std::int8_t) std::clamp ((int) lane.notes[i].chord[c] + shift, -128, 127);
    return out;
}

// The engine loops params.bars (LENGTH), which can differ from the groove's own
// length until the next roll. Cut or extend the groove to what plays.
inline Groove atLength (const Groove& g, int bars)
{
    Groove out = g;
    out.bars = std::clamp (bars, 2, kMaxBars);
    const float end = (float) (out.bars * 16);
    for (int p = 0; p < Drums; ++p)
    {
        auto& lane = out.lane[p];
        int w = 0;
        for (int i = 0; i < lane.count; ++i)
        {
            Note n = lane.notes[i];
            if (n.start >= end)
                continue;
            n.length = std::min (n.length, end - n.start);
            lane.notes[w++] = n;
        }
        lane.count = w;
    }
    for (int v = 0; v < NumDrumVoices; ++v)
        for (int t = out.bars * 16; t < kMaxSteps; ++t)
            out.drums.vel[v][t] = 0;
    for (int t = out.bars * 16; t < kMaxSteps; ++t)
        out.drums.accent[t] = 0;
    return out;
}

inline int loopSamples (int bars, double bpm, double rate) noexcept
{
    return (int) std::llround ((double) bars * 4.0 * 60.0 / std::max (1.0, bpm) * rate);
}

struct LoopRender
{
    juce::AudioBuffer<float> audio;
    int kickSample = -1;      // first kick note-on of the kept loop, in file samples
    bool samplesSettled = true; // false if the loader was still busy at the timeout
};

namespace detail
{
// Plays `total` samples from `ppqStart`; keeps [keepFrom, keepFrom + keep length).
inline void pass (Engine& engine, const Groove& g, const EngineParams& params, double rate, double bpm,
                  double ppqStart, int total, juce::AudioBuffer<float>& keep, int keepFrom, int midiFrom, int& kickAt)
{
    engine.reset();
    constexpr int kBlock = 1024;
    juce::AudioBuffer<float> block (2, kBlock);
    double ppq = ppqStart;
    kickAt = -1;
    for (int done = 0; done < total;)
    {
        const int n = std::min (kBlock, total - done);
        block.clear();
        Transport t;
        t.playing = true;
        t.ppqStart = ppq;
        t.bpm = bpm;
        t.sampleRate = rate;
        juce::MidiBuffer midi;
        engine.process (g, params, t, block.getWritePointer (0), block.getWritePointer (1), nullptr, nullptr, n, midi, true);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int at = done + meta.samplePosition - midiFrom;
            if (at >= 0 && kickAt < 0 && msg.isNoteOn() && msg.getChannel() == 10
                && msg.getNoteNumber() == drumMidiNote (Kick))
                kickAt = at;
        }
        const int a = std::max (done, keepFrom);
        const int b = std::min (done + n, keepFrom + keep.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            if (b > a)
                keep.copyFrom (ch, a - keepFrom, block, ch, a - done, b - a);
        ppq += (double) n * bpm / (60.0 * rate);
        done += n;
    }
}
} // namespace detail

// Pre-roll before the kept loop: enough for reverb / delay tails to wrap in.
inline constexpr double kPrerollSeconds = 4.0;

// `params` are the plugin's params for this bounce (mutes, presets, picks,
// library, index, mix, bars = LENGTH). The engine must already be prepared at `rate`.
// Samples load lazily: if any engine had to ask the library for a file during
// the bounce, the loader is given time to finish and the loop is bounced again.
inline LoopRender renderLoop (Engine& engine, const Groove& g, const EngineParams& params, double rate, double bpm,
                              const SampleLibrary* library, int timeoutMs = 5000)
{
    LoopRender out;
    const int bars = std::clamp (params.bars, 2, kMaxBars);
    const int loopN = loopSamples (bars, bpm, rate);
    if (loopN < 64)
        return out;
    const int lat = std::max (0, engine.latencySamples());
    const int preN = std::min (loopN, (int) std::llround (kPrerollSeconds * rate));
    const double loopBeats = (double) bars * 4.0;
    const double preBeats = (double) preN * bpm / (60.0 * rate);
    const double ppqStart = loopBeats - preBeats;
    const int total = preN + loopN + lat;

    out.audio.setSize (2, loopN);
    // The timeout bounds each wait for the loader, never the bounce itself: 7 stems
    // bounce in parallel and one pass can take longer than the timeout on a busy CPU.
    // A shared deadline let a stem that lazy-loaded a file (vox) skip its redo: silent stem.
    auto waitIdle = [&]
    {
        if (library == nullptr)
            return true;
        const auto deadline = juce::Time::getMillisecondCounterHiRes() + timeoutMs;
        while (! library->idle() && juce::Time::getMillisecondCounterHiRes() < deadline)
            juce::Thread::sleep (2);
        return library->idle();
    };
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        waitIdle();
        const auto before = library != nullptr ? library->notReadyRequests() : 0;
        out.audio.clear();
        detail::pass (engine, g, params, rate, bpm, ppqStart, total, out.audio, preN + lat, preN, out.kickSample);
        // Any engine (this one, a parallel stem, live playback) hitting an
        // unloaded file means this pass may be missing notes: wait and redo it.
        if (library == nullptr || library->notReadyRequests() == before)
            break;
        out.samplesSettled = waitIdle();
        if (! out.samplesSettled)
            break;
    }
    return out;
}

} // namespace rolla::rollexport
