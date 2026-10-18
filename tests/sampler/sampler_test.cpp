#include "Engine/Engine.h"
#include "Engine/SampleChop.h"
#include "Engine/Sampler.h"
#include "Drums/DrumMachine.h"
#include "GenreBrain/GenreTargetsJson.h"
#include "GenreBrain/KitMeshJson.h"
#include "GenreBrain/LanePools.h"
#include "GenreBrain/SampleFeaturesJson.h"
#include "GenreBrain/SoundSelector.h"
#include "Mix/TruePeakGuard.h"
#include "Sampler/Fingerprint.h"
#include "Sampler/PitchProbe.h"
#include "Tools/FingerprintTool.h"
#include "Sampler/PlayDecision.h"
#include "Sampler/SamplePrep.h"
#include "Sampler/UserSlots.h"
#include "Generator/Presets.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <iostream>
#include <iterator>
#include <string>
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

void writeKick (const juce::File& file, double seconds)
{
    // A realistic kick: pitch drops 150 -> 50 Hz, slow decay so the next quarter
    // retriggers while the last one is still loud. Starts at phase 0 (no click of its own).
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    auto fileStream = std::make_unique<juce::FileOutputStream> (file);
    if (fileStream == nullptr || ! fileStream->openedOk())
        return;
    std::unique_ptr<juce::OutputStream> stream (fileStream.release());
    const auto options = juce::AudioFormatWriterOptions().withSampleRate (48000.0)
                                                          .withNumChannels (1)
                                                          .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return;
    const int n = (int) std::lround (seconds * 48000.0);
    juce::AudioBuffer<float> buf (1, n);
    double phase = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / 48000.0;
        const double hz = 50.0 + 100.0 * std::exp (-t / 0.03);
        phase += 2.0 * 3.14159265358979 * hz / 48000.0;
        buf.setSample (0, i, (float) (0.8 * std::exp (-t / 0.35) * std::sin (phase)));
    }
    writer->writeFromAudioSampleBuffer (buf, 0, n);
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

// Sawtooth-like notes (harmonics at 1/h), summed, 1 s at 48 kHz after 20 ms of silence.
std::vector<float> sawNotes (std::initializer_list<double> midis)
{
    const int lead = 960, n = 48000;
    std::vector<float> out ((size_t) (lead + n), 0.f);
    for (double m : midis)
    {
        const double f = 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
        for (int h = 1; h <= 10 && f * h < 20000.0; ++h)
            for (int i = 0; i < n; ++i)
                out[(size_t) (lead + i)] += (float) (0.15 / h * std::sin (2.0 * 3.14159265358979 * f * h * i / 48000.0));
    }
    return out;
}

void writeWave (const juce::File& file, const std::vector<float>& samples)
{
    file.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    auto fileStream = std::make_unique<juce::FileOutputStream> (file);
    if (fileStream == nullptr || ! fileStream->openedOk())
        return;
    std::unique_ptr<juce::OutputStream> stream (fileStream.release());
    const auto options = juce::AudioFormatWriterOptions().withSampleRate (48000.0)
                                                          .withNumChannels (1)
                                                          .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return;
    juce::AudioBuffer<float> buf (1, (int) samples.size());
    for (int i = 0; i < (int) samples.size(); ++i)
        buf.setSample (0, i, samples[(size_t) i]);
    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

void testPitchProbe()
{
    using rolla::ChordQuality;
    {
        const auto a3 = sawNotes ({ 57.0 });
        const auto p = rolla::probePitch (a3.data(), nullptr, (int) a3.size(), 48000.0);
        expect (p.pitched() && ! p.isChord && std::fabs (p.rootPitch - 57.f) < 0.2f && p.conf >= rolla::kPitchConfMin,
                "probe: a saw at A3 is one note at 57 (got " + std::to_string (p.rootPitch) + ", conf " + std::to_string (p.conf) + ")");
    }
    {
        const auto sharp = sawNotes ({ 60.3 });
        const auto p = rolla::probePitch (sharp.data(), nullptr, (int) sharp.size(), 48000.0);
        expect (std::fabs (p.rootPitch - 60.3f) < 0.1f, "probe: 30 cents sharp is measured (got " + std::to_string (p.rootPitch) + ")");
    }
    {
        const auto d1 = sawNotes ({ 26.0 });
        const auto p = rolla::probePitch (d1.data(), nullptr, (int) d1.size(), 48000.0);
        expect (p.pitched() && ! p.isChord && std::fabs (p.rootPitch - 26.f) < 0.3f,
                "probe: a D1 bass is one note at 26 (got " + std::to_string (p.rootPitch) + ")");
    }
    {
        const auto cm = sawNotes ({ 60.0, 63.0, 67.0 });
        const auto p = rolla::probePitch (cm.data(), nullptr, (int) cm.size(), 48000.0);
        expect (p.isChord && p.quality == ChordQuality::Minor && ((int) std::lround (p.rootPitch) % 12) == 0,
                "probe: C-Eb-G is a C minor chord (root " + std::to_string (p.rootPitch) + ")");
    }
    {
        const auto inv = sawNotes ({ 63.0, 67.0, 72.0 });
        const auto p = rolla::probePitch (inv.data(), nullptr, (int) inv.size(), 48000.0);
        expect (p.isChord && p.quality == ChordQuality::Minor && ((int) std::lround (p.rootPitch) % 12) == 0,
                "probe: Eb-G-C (first inversion) is still C minor (root " + std::to_string (p.rootPitch) + ")");
    }
    {
        const auto fmaj = sawNotes ({ 53.0, 57.0, 60.0 });
        const auto p = rolla::probePitch (fmaj.data(), nullptr, (int) fmaj.size(), 48000.0);
        expect (p.isChord && p.quality == ChordQuality::Major && ((int) std::lround (p.rootPitch) % 12) == 5,
                "probe: F-A-C is an F major chord");
    }
    {
        const auto power = sawNotes ({ 48.0, 55.0 });
        const auto p = rolla::probePitch (power.data(), nullptr, (int) power.size(), 48000.0);
        expect (p.isChord && p.quality == ChordQuality::Neutral && ((int) std::lround (p.rootPitch) % 12) == 0,
                "probe: C-G is a neutral (fifth) chord on C");
    }
    {
        // A 120 ms stab (5 ms attack, exponential decay) must measure with enough
        // confidence to be retuned, at 48k and 44.1k.
        for (const double rate : { 48000.0, 44100.0 })
            for (const double m : { 48.0, 57.0, 63.0, 70.0 })
            {
                const int lead = (int) (0.01 * rate), n = (int) (0.12 * rate);
                std::vector<float> stab ((size_t) (lead + n), 0.f);
                const double f = 440.0 * std::pow (2.0, (m - 69.0) / 12.0);
                for (int i = 0; i < n; ++i)
                {
                    const double t = (double) i / rate;
                    const double env = std::min (1.0, t / 0.005) * std::exp (-t / 0.05);
                    double x = 0.0;
                    for (int h = 1; h <= 10 && f * h < 18000.0; ++h)
                        x += 0.3 / h * std::sin (2.0 * 3.14159265358979 * f * h * t);
                    stab[(size_t) (lead + i)] = (float) (env * x);
                }
                const auto p = rolla::probePitch (stab.data(), nullptr, (int) stab.size(), rate, rolla::kPitchFloorTonalHz);
                expect (p.pitched() && ! p.isChord && std::fabs (p.rootPitch - (float) m) < 0.3f && p.conf >= rolla::kPitchConfMin,
                        "probe: a 120 ms stab at " + std::to_string ((int) m) + " (" + std::to_string ((int) rate)
                            + " Hz) is retunable (got " + std::to_string (p.rootPitch) + ", conf " + std::to_string (p.conf) + ")");
            }
    }
    {
        // Sub rumble at 40 Hz under a C4 stab: the tonal-lane floor keeps C4 the root.
        auto c4 = sawNotes ({ 60.0 });
        for (size_t i = 960; i < c4.size(); ++i)
            c4[i] += (float) (0.25 * std::sin (2.0 * 3.14159265358979 * 40.0 * (double) (i - 960) / 48000.0));
        const auto p = rolla::probePitch (c4.data(), nullptr, (int) c4.size(), 48000.0, rolla::kPitchFloorTonalHz);
        expect (p.pitched() && ((int) std::lround (p.rootPitch) % 12) == 0 && p.rootPitch > 50.f,
                "probe: 40 Hz rumble under a C4 stab is not the root (got " + std::to_string (p.rootPitch) + ")");
    }
    {
        std::vector<float> noise (48000);
        std::uint32_t s = 12345u;
        for (auto& x : noise)
        {
            s = s * 1664525u + 1013904223u;
            x = 0.3f * ((float) (s >> 8) / 8388608.f - 1.f);
        }
        const auto p = rolla::probePitch (noise.data(), nullptr, (int) noise.size(), 48000.0);
        expect (! p.pitched(), "probe: white noise is unpitched");
    }

    rolla::PitchInfo c4;
    c4.rootPitch = 60.f;
    c4.conf = 0.9f;
    rolla::ShiftRule lane;
    expect (std::fabs (rolla::tonalShift (c4, -1, 62.0, lane) - 2.0) < 1.0e-9, "shift: C4 file plays D4 at +2");
    expect (std::fabs (rolla::tonalShift (c4, -1, 74.0, lane) - 2.0) < 1.0e-9, "shift: +14 folds to +2 (7 st cap)");
    expect (std::fabs (rolla::tonalShift (c4, -1, 52.0, lane) - 4.0) < 1.0e-9, "shift: -8 folds to +4");
    rolla::ShiftRule bass;
    bass.cap = 12.0;
    rolla::PitchInfo d1 = c4;
    d1.rootPitch = 26.f;
    expect (std::fabs (rolla::tonalShift (d1, -1, 38.0, bass) - 12.0) < 1.0e-9, "shift: bass keeps a full octave up");
    rolla::ShiftRule vox;
    vox.retune = false;
    expect (rolla::tonalShift (c4, -1, 66.0, vox) == 0.0, "shift: vox is never retuned");
    rolla::PitchInfo weak = c4;
    weak.conf = 0.3f;
    expect (rolla::tonalShift (weak, 67, 62.0, lane) == 0.0, "shift: a weak measurement the label contradicts is not retuned");
    expect (std::fabs (rolla::tonalShift (weak, 49, 62.0, lane) - 1.0) < 1.0e-9,
            "shift: a weak measurement the label agrees with uses the label's note (C#) in the measured octave");
    expect (rolla::tonalShift (rolla::PitchInfo {}, 60, 62.0, lane) == 0.0, "shift: unpitched files play as recorded");

    const int maj[] = { 0, 4, 7 }, mnr[] = { 0, 3, 7 }, fifth[] = { 0, 7 }, inv[] = { 4, 7, 12 };
    expect (rolla::chordAskOf (maj, 3).quality == ChordQuality::Major, "intervals 0-4-7 are major");
    expect (rolla::chordAskOf (mnr, 3).quality == ChordQuality::Minor, "intervals 0-3-7 are minor");
    expect (rolla::chordAskOf (fifth, 2).quality == ChordQuality::Neutral, "intervals 0-7 are neutral");
    const auto first = rolla::chordAskOf (inv, 3);
    expect (first.quality == ChordQuality::Major && first.rootDelta == 8,
            "E-G-C (first inversion) asks for C major, root 8 st above the E");
    expect (rolla::chordFileOffset (ChordQuality::Minor, ChordQuality::Major) == 4, "minor file on a major chord plays from the third");
    expect (rolla::chordFileOffset (ChordQuality::Major, ChordQuality::Minor) == 3, "major file on a minor chord plays from the minor third");
    expect (rolla::chordFileOffset (ChordQuality::Minor, ChordQuality::Minor) == 0, "matching quality stays put");
    expect (rolla::chordFileOffset (ChordQuality::Neutral, ChordQuality::Major) == 0, "neutral fits either");
}

std::vector<float> kickHit (double seconds = 0.4)
{
    std::vector<float> x ((size_t) (seconds * 48000.0));
    double phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / 48000.0;
        phase += 2.0 * 3.14159265358979 * (50.0 + 70.0 * std::exp (-t / 0.03)) / 48000.0;
        x[i] = (float) (0.8 * std::exp (-t / 0.06) * std::sin (phase));
    }
    return x;
}

std::vector<float> hatHit (std::uint32_t seed, double seconds = 0.15)
{
    std::vector<float> x ((size_t) (seconds * 48000.0));
    float prev = 0.f, prev2 = 0.f;
    for (size_t i = 0; i < x.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const float w = (float) (seed >> 8) / 8388608.f - 1.f;
        const float d1 = w - prev, d2 = d1 - prev2; // two differentiators: energy sits high
        prev = w;
        prev2 = d1;
        x[i] = (float) (0.3 * std::exp (-(double) i / 48000.0 / 0.012)) * d2;
    }
    return x;
}

// A synthetic kick whose body settles on `bodyHz`: a 10 ms pitch drop, then a long tail.
std::vector<float> tunedKick (double bodyHz, double seconds = 0.6)
{
    std::vector<float> x ((size_t) (seconds * 48000.0));
    double phase = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / 48000.0;
        phase += 2.0 * 3.14159265358979 * (bodyHz + 60.0 * std::exp (-t / 0.01)) / 48000.0;
        x[i] = (float) (0.8 * std::exp (-t / 0.25) * std::sin (phase));
    }
    return x;
}

// A kick with no fundamental: a decaying white-noise thump.
std::vector<float> noiseKick (double seconds = 0.6)
{
    std::vector<float> x ((size_t) (seconds * 48000.0));
    std::uint32_t s = 12345u;
    for (size_t i = 0; i < x.size(); ++i)
    {
        s = s * 1664525u + 1013904223u;
        x[i] = (float) (0.6 * std::exp (-(double) i / 48000.0 / 0.25)) * ((float) (s >> 8) / 8388608.f - 1.f);
    }
    return x;
}

double hzOfMidi (double midi)
{
    return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
}

// Upward zero crossings of x[from..to): the mean frequency in Hz, 0 with too few.
double zeroCrossHz (const std::vector<float>& x, size_t from, size_t to, double rate)
{
    double first = -1.0, last = -1.0;
    int count = 0;
    for (size_t i = std::max<size_t> (from, 1); i < to && i < x.size(); ++i)
        if (x[i - 1] < 0.f && x[i] >= 0.f)
        {
            const double at = (double) (i - 1) + (double) (-x[i - 1]) / (double) (x[i] - x[i - 1]);
            if (first < 0.0)
                first = at;
            last = at;
            ++count;
        }
    return count >= 3 && last > first ? (double) (count - 1) * rate / (last - first) : 0.0;
}

void testKickKeyFollow()
{
    using rolla::DrumMachine;
    // Key C: root, fourth and fifth in the kick window give G1, about 49 Hz. Key D gives A1, 55 Hz.
    expect (std::fabs (DrumMachine::kickHzForKey (0) - 49.0f) < 0.1f, "key follow: key C targets ~49 Hz");

    const auto body = tunedKick (55.0);
    const auto p = rolla::probePitch (body.data(), nullptr, (int) body.size(), 48000.0);
    const auto got = " (root " + std::to_string (p.rootPitch) + ", conf " + std::to_string (p.conf) + ")";
    expect (p.pitched() && ! p.isChord && p.conf >= rolla::kKickFollowConfMin
                && std::fabs (hzOfMidi (p.rootPitch) - 55.0) < 1.5,
            "key follow: the synthetic kick reads ~55 Hz with confidence" + got);

    const double toC = rolla::kickKeyFollowRatio (p, 0);
    const double semisC = 12.0 * std::log2 (toC);
    expect (semisC < -1.6 && semisC > -2.4 && std::fabs (semisC) <= rolla::kKickFollowMaxSt,
            "key follow: a 55 Hz kick in C is tuned down about 2 st (got " + std::to_string (semisC) + ")" + got);
    expect (std::fabs (hzOfMidi (p.rootPitch) * toC - (double) DrumMachine::kickHzForKey (0)) < 1.0,
            "key follow: the retuned kick lands on the key's kick note");
    expect (std::fabs (12.0 * std::log2 (rolla::kickKeyFollowRatio (p, 2))) < 0.4,
            "key follow: a 55 Hz kick in D stays about where it is");

    rolla::PitchInfo high;
    high.rootPitch = (float) (69.0 + 12.0 * std::log2 (70.0 / 440.0));
    high.conf = 0.9f;
    expect (std::fabs (12.0 * std::log2 (rolla::kickKeyFollowRatio (high, 0)) + 3.0) < 1.0e-9,
            "key follow: a 70 Hz kick in C clamps at -3 st, keeping its body");
    rolla::PitchInfo octave = high;
    octave.rootPitch = (float) (69.0 + 12.0 * std::log2 (110.0 / 440.0));
    expect (std::fabs (12.0 * std::log2 (rolla::kickKeyFollowRatio (octave, 0)) + 2.0) < 0.05,
            "key follow: a 110 Hz reading folds to 55 Hz before tuning");

    const auto thump = noiseKick();
    const auto unpitched = rolla::probePitch (thump.data(), nullptr, (int) thump.size(), 48000.0);
    expect (rolla::kickKeyFollowRatio (unpitched, 0) == 1.0, "key follow: an unpitched kick is not retuned");
    expect (rolla::kickKeyFollowRatio (rolla::PitchInfo {}, 0) == 1.0, "key follow: no measurement, no retune");
    rolla::PitchInfo weak = p;
    weak.conf = 0.29f;
    expect (rolla::kickKeyFollowRatio (weak, 0) == 1.0, "key follow: confidence under 0.3 is not retuned");
}

void testFingerprint()
{
    const auto kick = kickHit();
    const auto hat = hatHit (7u);
    auto stab = sawNotes ({ 60.0 });
    for (size_t i = 0; i < stab.size(); ++i)
        stab[i] *= (float) std::exp (-(double) i / 48000.0 / 0.25);
    const auto k = rolla::fingerprintOf (kick.data(), (int) kick.size());
    const auto h = rolla::fingerprintOf (hat.data(), (int) hat.size());
    const auto s = rolla::fingerprintOf (stab.data(), (int) stab.size());
    expect (k.valid && k.subRatio > 0.4f && k.centroidHz < 400.f,
            "fingerprint: a kick owns the sub (sub " + std::to_string (k.subRatio) + ", centroid " + std::to_string (k.centroidHz) + ")");
    expect (h.valid && h.centroidHz > 4000.f && h.decay40Ms < 200.f && h.subRatio < 0.05f,
            "fingerprint: a hat is bright and short (centroid " + std::to_string (h.centroidHz) + ", decay40 " + std::to_string (h.decay40Ms) + ")");
    expect (s.valid && std::fabs (s.pitch.rootPitch - 60.f) < 0.3f, "fingerprint: a C4 stab keeps its pitch beside the vector");
    expect ((int) k.vector().size() == rolla::kFingerprintDims && rolla::fingerprintName (rolla::kFingerprintDims - 1) == "sub_ratio"
                && rolla::fingerprintName (rolla::kDecay40Dim) == "log2_decay40_ms",
            "fingerprint: vector and names line up (level and pitch stay out of the distance)");
    expect (! k.truncated && ! std::isnan (k.vector()[(size_t) rolla::kDecay40Dim]), "fingerprint: a kick is not truncated");
    {
        std::vector<float> drone ((size_t) (48000 * 3));
        for (size_t i = 0; i < drone.size(); ++i)
            drone[i] = 0.4f * (float) std::sin (2.0 * 3.14159265358979 * 220.0 * (double) i / 48000.0);
        const auto d = rolla::fingerprintOf (drone.data(), (int) drone.size());
        expect (d.truncated && std::isnan (d.vector()[(size_t) rolla::kDecay40Dim]),
                "fingerprint: a sound ringing past 1.5 s is truncated and its decay40 is skipped");
    }

    // Target regions: two blobs give two clusters; a point in one is a hit, a far point is not.
    std::vector<std::vector<float>> xs;
    std::uint32_t seed = 3u;
    auto jitter = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return ((float) (seed >> 8) / 16777216.f - 0.5f) * 0.6f;
    };
    for (int i = 0; i < 40; ++i)
    {
        const float c = i % 2 == 0 ? 0.f : 5.f;
        xs.push_back ({ c + jitter(), c + jitter(), c + jitter() });
    }
    xs[3][2] = std::numeric_limits<float>::quiet_NaN(); // a truncated sound's missing dimension
    const auto t = rolla::fitTarget (xs, 3);
    expect (t.clusters.size() >= 2, "targets: two blobs give at least two clusters");
    expect (rolla::targetDistance ({ 0.1f, 0.f, -0.1f }, t) <= t.threshold, "targets: a point in a blob is a hit");
    expect (rolla::targetDistance ({ 20.f, -20.f, 20.f }, t) > t.threshold, "targets: a far point is a miss");
    expect (rolla::targetDistance ({ 0.1f, 0.f, std::numeric_limits<float>::quiet_NaN() }, t) <= t.threshold,
            "targets: a missing dimension is skipped, not counted as far");
    std::vector<std::vector<float>> few (xs.begin(), xs.begin() + 12);
    expect (rolla::fitTarget (few, 3).clusters.size() == 1, "targets: under 20 hits is one region");
}

int runTool (std::initializer_list<juce::String> words)
{
    juce::StringArray args;
    for (const auto& w : words)
        args.add (w);
    return rolla::fptool::run (args);
}

void testFingerprintTool()
{
    namespace fp = rolla::fptool;
    expect (fp::laneFromPath ("DRUMS/CH/x.wav") == "HAT_C" && fp::laneFromPath ("VOX/chops/y.wav") == "VOX"
                && fp::laneFromPath ("STAB/z.wav") == "STAB" && fp::laneFromPath ("misc/z.wav").isEmpty(),
            "fingerprint tool: lanes come from folders");

    const auto base = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rolla-fp-test");
    base.deleteRecursively();
    for (int i = 0; i < 3; ++i)
        writeWave (base.getChildFile ("lib/DRUMS/KICK/k" + juce::String (i) + ".wav"), kickHit());
    writeWave (base.getChildFile ("lib/DRUMS/CH/h0.wav"), hatHit (11u));
    const auto libJson = base.getChildFile ("lib.json");
    expect (runTool ({ "scan", base.getChildFile ("lib").getFullPathName(), "--out", libJson.getFullPathName() }) == 0,
            "fingerprint tool: scan runs");
    const auto lib = fp::entriesFromJson (juce::JSON::parse (libJson));
    int kicks = 0, hats = 0;
    for (const auto& e : lib)
    {
        kicks += e.lane == "KICK" ? 1 : 0;
        hats += e.lane == "HAT_C" ? 1 : 0;
    }
    expect (lib.size() == 4 && kicks == 3 && hats == 1, "fingerprint tool: scan finds 3 kicks and a hat");

    // A Demucs-style track: kicks on the beat, hats between, a four-note bass.
    std::vector<float> drums ((size_t) (48000 * 6), 0.f), bass ((size_t) (48000 * 6), 0.f);
    for (int b = 0; b < 12; ++b)
    {
        const auto kk = kickHit (0.3);
        const auto hh = hatHit (100u + (std::uint32_t) b, 0.1);
        const size_t at = (size_t) (b * 24000);
        for (size_t i = 0; i < kk.size() && at + i < drums.size(); ++i)
            drums[at + i] += kk[i];
        for (size_t i = 0; i < hh.size() && at + 12000 + i < drums.size(); ++i)
            drums[at + 12000 + i] += hh[i];
    }
    const auto note = sawNotes ({ 36.0 });
    for (int b = 0; b < 4; ++b)
        for (size_t i = 0; i < 30000 && b * 60000 + i < bass.size(); ++i)
            bass[(size_t) (b * 60000) + i] = note[960 + i] * (float) std::exp (-(double) i / 48000.0 / 0.3);
    writeWave (base.getChildFile ("stems/trackA/sep/drums.wav"), drums);
    writeWave (base.getChildFile ("stems/trackA/sep/bass.wav"), bass);
    // Not tracks: a tools folder and a track missing from the CSV are skipped.
    writeWave (base.getChildFile ("stems/_tools/sep/drums.wav"), drums);
    writeWave (base.getChildFile ("stems/trackB/sep/drums.wav"), drums);
    // The box CSV's header: track,style,mode_or_role.
    base.getChildFile ("styles.csv").replaceWithText ("track,style,mode_or_role\ntrackA,ukmin,support\n");
    {
        juce::StringArray skipped;
        const auto got = fp::sliceStems (base.getChildFile ("stems"), fp::readStyles (base.getChildFile ("styles.csv")), 0.f, &skipped);
        bool onlyA = ! got.empty();
        for (const auto& e : got)
            onlyA = onlyA && e.track == "trackA";
        expect (onlyA && skipped.contains ("_tools") && skipped.contains ("trackB"),
                "fingerprint tool: stems skips '_' folders and tracks missing from the CSV, and lists them");
    }
    const auto stemJson = base.getChildFile ("stems.json");
    expect (runTool ({ "stems", base.getChildFile ("stems").getFullPathName(), "--styles",
                       base.getChildFile ("styles.csv").getFullPathName(), "--out", stemJson.getFullPathName() }) == 0,
            "fingerprint tool: stems runs");
    const auto sliced = fp::entriesFromJson (juce::JSON::parse (stemJson));
    int sk = 0, sh = 0, sb = 0;
    for (const auto& e : sliced)
    {
        sk += e.lane == "KICK" ? 1 : 0;
        sh += e.lane.startsWith ("HAT") ? 1 : 0;
        sb += e.lane == "BASS" ? 1 : 0;
        expect (e.style == "ukmin" && e.track == "trackA", "fingerprint tool: slices carry track and style");
    }
    expect (sk >= 10 && sh >= 10 && sb >= 3,
            "fingerprint tool: stems slice into kicks, hats and bass (" + std::to_string (sk) + "/" + std::to_string (sh)
                + "/" + std::to_string (sb) + ")");
    const auto stemDoc = juce::JSON::parse (stemJson);
    expect (fp::nonIdString (stemDoc).isEmpty(), "privacy: a stem fp.json holds only ids, labels and styles as strings");
    auto* leaky = new juce::DynamicObject();
    leaky->setProperty ("path", "Some Artist - Some Track/sep/drums.wav");
    expect (fp::nonIdString (juce::var (leaky)) == "path", "privacy: a path string is caught");

    const auto oneJson = base.getChildFile ("one.json");
    expect (runTool ({ "scan-stems", base.getChildFile ("stems/trackA").getFullPathName(), "--track-id", "t01", "--style", "pop",
                       "--min-conf", "0.55", "--out", oneJson.getFullPathName() }) == 0,
            "fingerprint tool: scan-stems runs on one track");
    const auto one = fp::entriesFromJson (juce::JSON::parse (oneJson));
    bool idsOk = ! one.empty();
    for (const auto& e : one)
        idsOk = idsOk && e.track == "t01" && e.style == "pop";
    expect (idsOk, "fingerprint tool: scan-stems keys slices by the given track id and style");
    expect (one.size() <= sliced.size(), "fingerprint tool: --min-conf only drops slices");

    base.getChildFile ("ids.csv").replaceWithText ("folder,track_id,style\ntrackA,t07,long\n");
    const auto idMap = fp::readStyles (base.getChildFile ("ids.csv"));
    expect (idMap.count ("trackA") == 1 && idMap.at ("trackA").id == "t07" && idMap.at ("trackA").style == "long",
            "fingerprint tool: a folder,track_id,style CSV writes the id, not the folder");

    const auto targetsJson = base.getChildFile ("targets.json");
    expect (runTool ({ "targets", stemJson.getFullPathName(), "--out", targetsJson.getFullPathName() }) == 0,
            "fingerprint tool: targets runs");
    const auto targets = juce::JSON::parse (targetsJson);
    const auto* kickClusters = targets["styles"]["ukmin"]["KICK"]["clusters"].getArray();
    expect (kickClusters != nullptr && ! kickClusters->isEmpty(), "fingerprint tool: targets hold ukmin KICK clusters");
    const auto* names = targets["features"].getArray();
    expect (names != nullptr && names->size() == rolla::kFingerprintDims, "fingerprint tool: targets list every feature");
    expect (! (bool) targets["styles"]["ukmin"]["KICK"]["pooled"] && (bool) targets["styles"]["ukmin"]["BASS"]["pooled"],
            "fingerprint tool: a lane with under 8 hits takes the pooled region");
    base.deleteRecursively();
}

void testGenreTargets()
{
    using namespace rolla::genre;
    const auto mock = GenreTargets::mock();
    expect (! mock.fromData && mock.lane (Style::Deep, Lane::Stab).centroidHz.hi < mock.lane (Style::Bouncy, Lane::Stab).centroidHz.hi
                && mock.lane (Style::Deep, Lane::Pad).decayMs.hi > mock.lane (Style::Bouncy, Lane::Pad).decayMs.hi,
            "genre mock: DEEP is darker and longer than BOUNCY");
    expect (std::string (laneName (Lane::HatC)) == "HAT_C" && laneFromName ("VOX") == Lane::Vox && styleFromId ("gaskin") == Style::Jackin,
            "genre names round-trip");

    SampleFeatures kick;
    kick.lane = Lane::Kick;
    kick.centroidHz = 150.f;
    kick.tiltDbPerOct = -6.f;
    kick.attackMs = 2.f;
    kick.decay20Ms = 200.f;
    kick.subRatio = 0.6f;
    expect (mock.bandMiss (Style::Roller, kick) == 0.f, "genre mock: a typical kick sits inside the kick bands");
    kick.centroidHz = 3000.f;
    expect (mock.bandMiss (Style::Roller, kick) > 1.f, "genre mock: a bright 'kick' misses the bands");
    expect (mock.timbreDistance (Style::Roller, kick) == 0.f, "genre mock: no timbre regions, distance 0 (bands decide)");

    // Partial real data: one style, one lane, overrides what it has and keeps the rest.
    const char* text = R"JSON({"version":1,"norm":{"mean":[0,0,0],"std":[1,1,1]},
      "styles":{"long":{"groove":{"swing":[0.53,0.58]},
        "STAB":{"clusters":[{"centroid":[0,0,null],"spread":[1,1,1],"weight":1}],"threshold":1.5,"count":40,
                "bands":{"centroid_hz":[300,900]},"pitch":{"min_conf":0.6},"loudness_db":-9}}}})JSON";
    const auto g = parseGenreTargets (juce::JSON::parse (text));
    const auto& stab = g.lane (Style::Deep, Lane::Stab);
    expect (g.fromData && stab.present && stab.count == 40 && stab.timbre.clusters.size() == 1
                && std::isnan (stab.timbre.clusters[0].centroid[2]) && stab.centroidHz.hi == 900.f && stab.minPitchConf == 0.6f
                && g.style (Style::Deep).groove.swing.lo == 0.53f,
            "genre json: given fields override (null centroid = NaN)");
    expect (stab.attackMs.set && stab.attackMs.hi == mock.lane (Style::Deep, Lane::Stab).attackMs.hi
                && g.lane (Style::Roller, Lane::Kick).centroidHz.hi == mock.lane (Style::Roller, Lane::Kick).centroidHz.hi,
            "genre json: missing fields and styles keep the neutral values");
    SampleFeatures s;
    s.lane = Lane::Stab;
    s.vector = { 0.2f, -0.1f, 5.f };
    expect (g.timbreDistance (Style::Deep, s) < 1.f, "genre json: timbre distance skips the null dimension");
    expect (! loadGenreTargets (juce::File ("/nonexistent/genre_targets.json")).fromData, "genre json: no file means the mock");

    // The box's shape: style names (roller...), and the ext block's p10..p90 bands
    // (ALL, then per style) and swing in percent.
    const char* boxText = R"JSON({"version":1,"styles":{"roller":{"KICK":{"clusters":[{"centroid":[1],"spread":[1]}],"threshold":1.2,"pooled":false}}},
      "ext":{"schema":"rolla.genre_targets.ext/1",
        "bands":{"ALL":{"stab_chord":{"features":{"centroid_hz":{"p10":400,"p50":700,"p90":1200}}}},
                 "DEEP":{"kick":{"n_hits":10,"features":{"centroid_hz":{"p10":200,"p50":261,"p90":330},"decay20_ms":{"p10":60,"p50":90,"p90":140}}}}},
        "groove":{"JACKIN":{"hat_16th_swing_pct":{"p10":51.1,"p50":56.5,"p90":64.6}}}}})JSON";
    const auto box = parseGenreTargets (juce::JSON::parse (boxText));
    expect (box.lane (Style::Roller, Lane::Kick).timbre.clusters.size() == 1 && box.lane (Style::Roller, Lane::Kick).timbre.threshold == 1.2f,
            "genre json: 'roller' maps to ROLLER");
    expect (box.lane (Style::Deep, Lane::Kick).centroidHz.lo == 200.f && box.lane (Style::Deep, Lane::Kick).decayMs.hi == 140.f
                && box.lane (Style::Bouncy, Lane::Pluck).centroidHz.hi == 1200.f,
            "genre json: ext bands (per style over ALL) become p10..p90 bands");
    expect (box.lane (Style::Bouncy, Lane::Hits).centroidHz.hi == mock.lane (Style::Bouncy, Lane::Hits).centroidHz.hi,
            "genre json: stab_chord covers STAB and PLUCK only; HITS keeps its own values");
    expect (std::fabs (box.style (Style::Jackin).groove.swing.hi - 0.646f) < 1.0e-4f, "genre json: ext swing percent becomes a ratio band");
}

// Made-up samples with numbers typical of the mock bands; never real files or a profile.
rolla::genre::SampleFeatures mockSample (rolla::genre::Lane lane, const std::string& id, const std::string& pack, bool onTarget)
{
    using namespace rolla::genre;
    SampleFeatures s;
    s.id = id;
    s.lane = lane;
    s.pack = pack;
    s.tiltDbPerOct = -5.f;
    s.attackMs = 2.f;
    if (lane == Lane::Kick)
    {
        // Off target: too bright and too long for a ROLLER kick, but only mildly.
        s.centroidHz = onTarget ? 150.f : 800.f;
        s.decay20Ms = onTarget ? 200.f : 600.f;
        s.subRatio = 0.6f;
    }
    else if (lane == Lane::Bass)
    {
        s.centroidHz = 200.f;
        s.decay20Ms = 300.f;
        s.subRatio = 0.5f;
        s.pitch.rootPitch = 36.f;
        s.pitch.conf = onTarget ? 0.9f : 0.1f;
    }
    else
    {
        s.centroidHz = 7000.f;
        s.decay20Ms = 60.f;
        s.subRatio = 0.01f;
    }
    return s;
}

std::string mockFeatureBlock (float centroid, float decay, float sub, float root, float conf)
{
    const std::string zeros = "[0,0,0,0,0,0,0,0,0,0,0,0,0]", ones = "[1,1,1,1,1,1,1,1,1,1,1,1,1]";
    return "{\"mfcc_mean\":" + zeros + ",\"mfcc_std\":" + ones + ",\"centroid_hz\":" + std::to_string (centroid)
         + ",\"rolloff85_hz\":4000,\"flatness\":0.1,\"attack_ms\":2,\"decay20_ms\":" + std::to_string (decay)
         + ",\"decay40_ms\":null,\"truncated\":false,\"sub_ratio\":" + std::to_string (sub)
         + ",\"rms_db\":-12,\"tilt_db_oct\":-5,\"root_pitch\":" + std::to_string (root) + ",\"root_conf\":" + std::to_string (conf)
         + ",\"is_chord\":false,\"quality\":\"\"}";
}

void testSoundSelector()
{
    using namespace rolla::genre;
    using Pools = std::array<std::vector<SampleFeatures>, kLanes>;
    const auto targets = GenreTargets::mock();
    auto at = [] (Pools& pools, Lane lane) -> std::vector<SampleFeatures>& { return pools[(size_t) lane]; };
    auto tag = [] (const char* prefix, int i) { return std::string (prefix) + std::to_string (i); };

    // On target is drawn far more often; off target still sometimes (no hard filter).
    {
        Pools pools;
        for (int i = 0; i < 4; ++i)
            at (pools, Lane::Kick).push_back (mockSample (Lane::Kick, tag ("on", i), "", true));
        for (int i = 0; i < 4; ++i)
            at (pools, Lane::Kick).push_back (mockSample (Lane::Kick, tag ("off", i), "", false));
        // A soft temperature shows the weighting; the default (0.35) is sharper but
        // still never filters an off-target file out.
        SelectorOptions soft;
        soft.temperature = 1.f;
        int on = 0, off = 0;
        for (std::uint64_t seed = 1; seed <= 400; ++seed)
            (drawKit (pools, Style::Roller, targets, seed, nullptr, soft).at (Lane::Kick) < 4 ? on : off) += 1;
        expect (on > 3 * off, "selector: on-target kicks win most draws (" + std::to_string (on) + " vs " + std::to_string (off) + ")");
        expect (off > 10, "selector: off-target kicks are still drawn sometimes (" + std::to_string (off) + ")");
        int sharpOn = 0, sharpOff = 0;
        for (std::uint64_t seed = 1; seed <= 4000; ++seed)
            (drawKit (pools, Style::Roller, targets, seed).at (Lane::Kick) < 4 ? sharpOn : sharpOff) += 1;
        expect (sharpOff >= 1 && sharpOn > 20 * sharpOff,
                "selector: the default temperature is sharp but never a hard filter (" + std::to_string (sharpOn) + " vs "
                    + std::to_string (sharpOff) + ")");
    }

    Pools full;
    for (int i = 0; i < 8; ++i)
    {
        at (full, Lane::Kick).push_back (mockSample (Lane::Kick, tag ("k", i), i % 2 == 0 ? "PA" : "PB", i < 5));
        at (full, Lane::HatC).push_back (mockSample (Lane::HatC, tag ("h", i), i % 2 == 0 ? "PA" : "PB", true));
    }
    for (int i = 0; i < 6; ++i)
        at (full, Lane::Bass).push_back (mockSample (Lane::Bass, tag ("b", i), "PC", i != 2));
    at (full, Lane::Pad).push_back (mockSample (Lane::Pad, "only-pad", "PC", true));

    // Same seed, same kit; different seeds spread over the pool.
    {
        const Kit a = drawKit (full, Style::Deep, targets, 42u);
        const Kit b = drawKit (full, Style::Deep, targets, 42u);
        expect (a == b, "selector: the same seed draws the same kit");
        std::vector<int> seen;
        for (std::uint64_t seed = 1; seed <= 100; ++seed)
        {
            const int k = drawKit (full, Style::Deep, targets, seed).at (Lane::Kick);
            if (std::find (seen.begin(), seen.end(), k) == seen.end())
                seen.push_back (k);
        }
        expect (seen.size() >= 4, "selector: seeds spread over the pool, not a fixed best pick");
    }

    // A re-roll never repeats the last kit; each lane with alternatives moves.
    {
        bool moved = true, kept = true;
        for (std::uint64_t seed = 1; seed <= 200; ++seed)
        {
            const Kit last = drawKit (full, Style::Roller, targets, seed);
            const Kit next = drawKit (full, Style::Roller, targets, seed + 7919u, &last);
            moved = moved && next != last && next.at (Lane::Kick) != last.at (Lane::Kick)
                 && next.at (Lane::HatC) != last.at (Lane::HatC) && next.at (Lane::Bass) != last.at (Lane::Bass);
            kept = kept && next.at (Lane::Pad) == 0;
        }
        expect (moved, "selector: a re-roll changes every lane that has another file");
        expect (kept, "selector: a one-file pool still plays its file");
    }

    // Pitched lanes: a confident pitch scores better than a doubtful one.
    expect (fitScore (targets, Style::Roller, Lane::Bass, mockSample (Lane::Bass, "a", "", true))
                < fitScore (targets, Style::Roller, Lane::Bass, mockSample (Lane::Bass, "b", "", false)),
            "selector: low pitch confidence costs on a pitched lane");
    {
        auto mono = mockSample (Lane::Bass, "m", "", true), wide = mono;
        wide.lowCorr = 0.6f;
        expect (fitScore (targets, Style::Roller, Lane::Bass, wide) > fitScore (targets, Style::Roller, Lane::Bass, mono) + 2.5f,
                "selector: a stereo bass (low L/R correlation under 150 Hz) costs, since BASS is summed to mono");
    }

    // Pack tendency: the hat leans to the kick's pack.
    {
        SelectorOptions neutral;
        neutral.packBoost = neutral.drumPackBoost = neutral.familyBoost = 1.f;
        int same = 0, sameNeutral = 0;
        const int runs = 400;
        for (std::uint64_t seed = 1; seed <= (std::uint64_t) runs; ++seed)
        {
            const Kit a = drawKit (full, Style::Roller, targets, seed);
            const Kit b = drawKit (full, Style::Roller, targets, seed, nullptr, neutral);
            same += full[(size_t) Lane::HatC][(size_t) a.at (Lane::HatC)].pack == full[(size_t) Lane::Kick][(size_t) a.at (Lane::Kick)].pack ? 1 : 0;
            sameNeutral += full[(size_t) Lane::HatC][(size_t) b.at (Lane::HatC)].pack == full[(size_t) Lane::Kick][(size_t) b.at (Lane::Kick)].pack ? 1 : 0;
        }
        expect (same > sameNeutral + runs / 5 && same < runs,
                "selector: same-pack share rises with the pack tendency, and stays a tendency (" + std::to_string (same) + " vs "
                    + std::to_string (sameNeutral) + " of " + std::to_string (runs) + ")");
    }

    // Stream B's hook: a clashing draw is re-drawn; with no clean option the least-bad stays.
    {
        Pools pools;
        for (int i = 0; i < 4; ++i)
            at (pools, Lane::Kick).push_back (mockSample (Lane::Kick, tag ("k", i), "K", true));
        for (int i = 0; i < 8; ++i)
            at (pools, Lane::HatC).push_back (mockSample (Lane::HatC, tag ("h", i), i < 4 ? "BAD" : "OK", true));
        int calls = 0;
        SelectorOptions mesh;
        mesh.pairPenalty = [&calls] (const SampleFeatures& candidate, const SampleFeatures&)
        {
            ++calls;
            return candidate.pack == "BAD" ? 5.f : 0.f;
        };
        mesh.maxPenalty = 1.f;
        mesh.maxTries = 8;
        int badFree = 0, badMeshed = 0;
        for (std::uint64_t seed = 1; seed <= 200; ++seed)
        {
            badFree += drawKit (pools, Style::Roller, targets, seed).at (Lane::HatC) < 4 ? 1 : 0;
            badMeshed += drawKit (pools, Style::Roller, targets, seed, nullptr, mesh).at (Lane::HatC) < 4 ? 1 : 0;
        }
        expect (badFree > 0 && badMeshed == 0 && calls > 0, "selector: the pair penalty re-draws a clashing hat");
        at (pools, Lane::HatC).resize (4); // every hat clashes now
        expect (drawKit (pools, Style::Roller, targets, 5u, nullptr, mesh).at (Lane::HatC) >= 0,
                "selector: with no clean hat the least-bad one stays (no empty lane)");
    }

    // Empty pool: no pick, and the other lanes are unaffected.
    {
        const Kit kit = drawKit (full, Style::Bouncy, targets, 9u);
        expect (kit.at (Lane::Stab) == -1 && kit.at (Lane::Kick) >= 0, "selector: an empty pool is no pick");
        expect (drawKit (Pools {}, Style::Bouncy, targets, 9u) == Kit(), "selector: no pools, no picks");
    }

    // sample_features.json: object keyed by path, and the fp.json array form.
    {
        const std::string text = "{\"version\":1,\"files\":{"
                                 "\"BASS/b1.wav\":{\"lane\":\"BASS\",\"pack\":\"PX\",\"family\":\"fx\",\"tier\":2,\"f\":"
                               + mockFeatureBlock (200.f, 300.f, 0.5f, 36.f, 0.8f) + "},"
                                 "\"DRUMS/KICK/k1.wav\":{\"lane\":\"KICK\",\"f\":" + mockFeatureBlock (150.f, 200.f, 0.6f, -1.f, 0.f) + "},"
                                 "\"MISC/x.wav\":{\"lane\":\"NOPE\",\"f\":" + mockFeatureBlock (150.f, 200.f, 0.6f, -1.f, 0.f) + "},"
                                 "\"BASS/short.wav\":{\"lane\":\"BASS\",\"f\":{\"mfcc_mean\":[1,2]}}}}";
        const auto index = parseSampleFeatures (juce::JSON::parse (juce::String (text)));
        const auto* b1 = index.find ("BASS/b1.wav");
        expect (index.fromData && index.size() == 2 && index.pools[(size_t) Lane::Bass].size() == 1
                    && index.pools[(size_t) Lane::Kick].size() == 1,
                "features json: known lanes with MFCCs load, the rest is skipped");
        expect (b1 != nullptr && b1->pack == "PX" && b1->family == "fx" && b1->tier == 2 && b1->lane == Lane::Bass
                    && std::fabs (b1->pitch.conf - 0.8f) < 1.0e-4f && b1->truncated
                    && b1->vector.size() == (size_t) rolla::kFingerprintDims && std::isnan (b1->vector[(size_t) rolla::kDecay40Dim]),
                "features json: pack, family, tier and pitch read; a null decay40 is unknown");
        const std::string arrayText = "{\"version\":1,\"files\":[{\"path\":\"BASS/b2.wav\",\"lane\":\"BASS\",\"f\":"
                                    + mockFeatureBlock (200.f, 300.f, 0.5f, 36.f, 0.8f) + "}]}";
        expect (parseSampleFeatures (juce::JSON::parse (juce::String (arrayText))).find ("BASS/b2.wav") != nullptr,
                "features json: the fp.json array form loads too");
        const auto none = loadSampleFeatures (juce::File ("/nonexistent/sample_features.json"));
        expect (! none.fromData && none.size() == 0, "features json: no file, no index");
    }

    // The seam: a fully covered pool maps the selector's choice back to the index ordinal.
    {
        rolla::SampleIndex idx;
        auto addEntry = [&idx] (const char* path, int lane)
        {
            rolla::SampleEntry e;
            std::strncpy (e.path, path, sizeof (e.path) - 1);
            e.lane = lane;
            e.onDisk = true;
            idx.entries.push_back (e);
        };
        addEntry ("BASS/a.wav", rolla::Bass);
        addEntry ("STAB/c.wav", rolla::Stab);
        addEntry ("BASS/b.wav", rolla::Bass);
        rolla::PoolFilter filter;
        filter.on = true;
        filter.lane = rolla::Bass;
        const std::string text = "{\"version\":1,\"files\":{"
                                 "\"BASS/a.wav\":{\"lane\":\"BASS\",\"f\":" + mockFeatureBlock (200.f, 300.f, 0.5f, 36.f, 0.9f) + "},"
                                 "\"BASS/b.wav\":{\"lane\":\"BASS\",\"f\":" + mockFeatureBlock (200.f, 300.f, 0.5f, 38.f, 0.9f) + "}}}";
        const auto features = parseSampleFeatures (juce::JSON::parse (juce::String (text)));
        expect (laneForPart (rolla::Bass, -1) == Lane::Bass && laneForPart (rolla::Drums, rolla::Rim) == Lane::Perc
                    && laneForPart (rolla::Drums, rolla::HatO) == Lane::HatO,
                "seam: parts and drum voices map to genre lanes");
        Pools pools;
        at (pools, Lane::Bass) = featurePool (idx, rolla::Bass, filter, features, Lane::Bass);
        expect (at (pools, Lane::Bass).size() == 2 && at (pools, Lane::Bass)[0].id == "BASS/a.wav"
                    && at (pools, Lane::Bass)[1].id == "BASS/b.wav",
                "seam: the feature pool follows the index pool order");
        bool mapped = true;
        for (std::uint64_t seed = 1; seed <= 20; ++seed)
        {
            const int j = drawKit (pools, Style::Roller, targets, seed).at (Lane::Bass);
            const auto* entry = rolla::sampleIndexAt (idx, rolla::Bass, filter, j);
            mapped = mapped && entry != nullptr && at (pools, Lane::Bass)[(size_t) j].id == entry->path;
        }
        expect (mapped, "seam: the selector's pick is the index ordinal of the same file");
        addEntry ("BASS/new.wav", rolla::Bass);
        at (pools, Lane::Bass) = featurePool (idx, rolla::Bass, filter, features, Lane::Bass);
        const auto& withMissing = at (pools, Lane::Bass);
        expect (withMissing.size() == 3 && withMissing[2].excluded && ! withMissing[0].excluded,
                "seam: a file the index lacks keeps its place in the pool, excluded");
        bool neverMissing = true;
        for (std::uint64_t seed = 1; seed <= 200; ++seed)
            neverMissing = neverMissing && drawKit (pools, Style::Roller, GenreTargets::mock(), seed, nullptr).pick[(size_t) Lane::Bass] != 2;
        expect (neverMissing, "seam: an excluded file is never drawn");
    }
}

// Kit meshing rules (KitMesh.h) on mock features: no audio, no library.
void testKitMesh()
{
    using namespace rolla::genre;
    auto make = [] (Lane lane, float sub, float centroid, float root, float conf)
    {
        SampleFeatures s;
        s.lane = lane;
        s.subRatio = sub;
        s.centroidHz = centroid;
        s.attackMs = 5.f;
        s.decay20Ms = 150.f;
        s.pitch.rootPitch = root;
        s.pitch.conf = conf;
        s.pitch.chordPcs = (std::uint16_t) (root >= 0.f ? 1 : 0);
        return s;
    };
    MeshContext ctx;
    ctx.keyRootPc = 0; // C minor
    const float at = ctx.rules.conflictAt;

    // Kick vs bass. The bass is retuned to the line (the key root, C); the kick is not.
    const auto bass = make (Lane::Bass, 0.8f, 90.f, 36.f, 0.9f);
    const auto kickClash = make (Lane::Kick, 0.8f, 80.f, 35.f, 0.8f); // B1, a semitone under C
    const auto r1 = pairPenalty (kickClash, bass, ctx);
    expect (r1.penalty >= at && r1.reason == MeshReason::FundamentalClash,
            "mesh: sub-heavy kick a semitone off a sub-heavy bass conflicts");
    expect (pairPenalty (bass, kickClash, ctx).penalty == r1.penalty, "mesh: pair order does not matter");
    const auto cleanBass = make (Lane::Bass, 0.6f, 90.f, 36.f, 0.9f);
    const auto cleanKick = make (Lane::Kick, 0.6f, 80.f, 36.f, 0.8f); // tuned to the key root
    expect (pairPenalty (cleanKick, cleanBass, ctx).penalty == 0.f, "mesh: a kick on the key root with a normal bass is clean");
    auto longKick = cleanKick;
    longKick.decay20Ms = 400.f; // still over -20 dB when the offbeat bass is up (238 ms at 126 BPM)
    const auto r2 = pairPenalty (longKick, cleanBass, ctx);
    expect (r2.penalty >= at && r2.reason == MeshReason::KickTailOverlap, "mesh: a long kick tail over the bass conflicts");
    const auto proxyKick = make (Lane::Kick, 0.8f, 93.5f, -1.f, 0.f); // no tail pitch: f0 from the centroid (~62 Hz, B1)
    const float proxy = pairPenalty (proxyKick, bass, ctx).penalty;
    expect (proxy > 0.f && proxy < at, "mesh: a centroid-proxy fundamental clash counts, but not alone");

    // Bass vs stab: low-mid masking and register.
    const auto muddyStab = make (Lane::Stab, 0.35f, 250.f, 40.f, 0.8f);
    const auto r3 = pairPenalty (bass, muddyStab, ctx);
    expect (r3.penalty >= at && r3.reason == MeshReason::LowMidMasking, "mesh: a dark, sub-heavy low stab over the bass conflicts");
    const auto stab = make (Lane::Stab, 0.05f, 900.f, 60.f, 0.85f);
    expect (pairPenalty (bass, stab, ctx).penalty == 0.f, "mesh: a bright stab two octaves up is clean against the bass");

    // Stab vs pad register spacing.
    const auto padClose = make (Lane::Pad, 0.02f, 800.f, 62.f, 0.8f);
    const auto r4 = pairPenalty (stab, padClose, ctx);
    expect (r4.penalty >= at && r4.reason == MeshReason::RegisterCrowding, "mesh: stab and pad roots a tone apart conflict");
    const auto padUnder = make (Lane::Pad, 0.02f, 500.f, 48.f, 0.8f);
    expect (pairPenalty (stab, padUnder, ctx).penalty == 0.f, "mesh: a pad an octave under the stab is clean");

    // Key: a confident major-triad file on one-finger lines in C minor sounds E natural.
    auto majorStab = stab;
    majorStab.pitch.isChord = true;
    majorStab.pitch.quality = rolla::ChordQuality::Major;
    majorStab.pitch.chordPcs = (std::uint16_t) ((1u << 0) | (1u << 4) | (1u << 7));
    const auto k1 = keyPenalty (majorStab, ctx);
    expect (k1.penalty >= at && k1.reason == MeshReason::OutOfKey, "mesh: a major chord file conflicts in minor");
    auto inMajor = ctx;
    inMajor.scaleMask = kScaleMajor;
    expect (keyPenalty (majorStab, inMajor).penalty == 0.f, "mesh: the same chord file fits a major key");
    auto asked = ctx;
    asked.chordAsk = true; // written minor triads: the sampler plays it from the minor third (Eb G Bb)
    expect (keyPenalty (majorStab, asked).penalty == 0.f, "mesh: chordFileOffset rescues a major file on asked minor triads");
    expect (keyPenalty (stab, ctx).penalty == 0.f, "mesh: a confident single note always lands in key");
    const auto vox = make (Lane::Vox, 0.0f, 1600.f, 61.f, 0.9f); // C#, not retuned
    expect (keyPenalty (vox, ctx).penalty >= at, "mesh: an unretuned vox on a wrong pitch class conflicts");
    const auto weakStab = make (Lane::Stab, 0.05f, 900.f, 61.f, 0.4f); // plays as recorded
    const float weak = keyPenalty (weakStab, ctx).penalty;
    expect (weak > 0.f && weak < at, "mesh: a low-confidence wrong pitch counts, scaled by its confidence");
    auto noKey = ctx;
    noKey.keyRootPc = -1;
    expect (keyPenalty (majorStab, noKey).penalty == 0.f, "mesh: no key, no key rule");

    // Top band.
    const auto hat = make (Lane::HatC, 0.f, 8700.f, -1.f, 0.f);
    const auto ride = make (Lane::Ride, 0.f, 8900.f, -1.f, 0.f);
    const auto hatO = make (Lane::HatO, 0.f, 8800.f, -1.f, 0.f);
    expect (pairPenalty (hat, ride, ctx).reason == MeshReason::TopBandClash && pairPenalty (hat, ride, ctx).penalty >= at,
            "mesh: a ride on the closed hat's band conflicts");
    expect (pairPenalty (hat, hatO, ctx).penalty == 0.f, "mesh: closed and open hat are a pair by design");

    // Whole kit, and the selector hook.
    const std::vector<SampleFeatures> kit { kickClash, bass, majorStab };
    const auto conflicts = kitConflicts (kit, ctx);
    expect (conflicts.size() == 2 && conflicts[0].a == 0 && conflicts[0].b == 1 && conflicts[1].a == 2 && conflicts[1].b == -1
                && conflicts[1].reason == MeshReason::OutOfKey,
            "mesh: kitConflicts lists the kick/bass clash and the out-of-key chord");
    const auto hook = pairPenaltyHook (ctx);
    expect (hook (cleanKick, majorStab) >= at && hook (cleanKick, stab) == 0.f, "mesh: the hook adds each member's key penalty");
    expect (pairPenaltyHook (ctx, false) (cleanKick, majorStab) == 0.f, "mesh: the hook without key is pairs only");

    // Config: a file overrides what it has and keeps the rest.
    const auto rules = parseMeshRules (juce::JSON::parse (R"JSON({"crowding":{"full_clear_st":[1.0,1.5]},"conflict_at":1.5})JSON"));
    expect (rules.crowdFullSt == 1.f && rules.crowdClearSt == 1.5f && rules.conflictAt == 1.5f
                && rules.subHeavy == MeshRules::fromMixSpec().subHeavy,
            "mesh json: given numbers override, missing keep the mix-spec defaults");
    auto tuned = ctx;
    tuned.rules = rules;
    expect (pairPenalty (stab, padClose, tuned).penalty == 0.f, "mesh json: a tighter crowding tolerance clears a tone apart");
    expect (loadMeshRules (juce::File ("/nonexistent/mesh_rules.json")).crowdClearSt == MeshRules::fromMixSpec().crowdClearSt,
            "mesh json: no file means the defaults");
}

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juce;

    testPitchProbe();
    testFingerprint();
    testFingerprintTool();
    testGenreTargets();
    testSoundSelector();
    testKitMesh();
    testKickKeyFollow();

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
    // Caches live outside the sample roots now, keyed by root path: clear this test's.
    for (const char* sub : { "scan", "dual" })
        for (const char* kind : { "index", "loudness" })
            rolla::sampleCacheFor (rootDir.getChildFile (sub), kind).deleteFile();
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
        // Neutral CI versions of the A&R pitch checks: play library files through
        // the sampler and measure what comes out.
        const auto pitchRoot = rootDir.getChildFile ("pitch");
        pitchRoot.deleteRecursively();
        writeWave (pitchRoot.getChildFile ("STAB/saw_C4.wav"), sawNotes ({ 60.0 }));
        // A factory-style wrong label: the voice is A4, the filename says B0.
        writeWave (pitchRoot.getChildFile ("VOX/chops/yeah_B0.wav"), sawNotes ({ 69.0 }));
        writeWave (pitchRoot.getChildFile ("PAD/chord_Cmin.wav"), sawNotes ({ 60.0, 63.0, 67.0 }));

        rolla::PresetBank bank;
        rolla::loadDefaultPresets (bank);
        auto useFile = [] (rolla::SampleSpec& spec, const char* file)
        {
            spec.mode = rolla::SampleMode::Sample;
            spec.filter.on = false;
            std::snprintf (spec.file, sizeof spec.file, "%s", file);
        };
        bank.count[rolla::Stab] = 1;
        useFile (bank.stab[0].sample, "STAB/saw_C4.wav");
        bank.count[rolla::Vox] = 1;
        useFile (bank.vox[0].sample, "VOX/chops/yeah_B0.wav");
        bank.count[rolla::Pad] = 1;
        useFile (bank.pad[0].sample, "PAD/chord_Cmin.wav");

        rolla::SampleLibrary lib;
        lib.setRoot (pitchRoot);
        lib.watch (bank);
        expect (waitUntil ([&] { return lib.ready ("STAB/saw_C4.wav") && lib.ready ("VOX/chops/yeah_B0.wav")
                                        && lib.ready ("PAD/chord_Cmin.wav"); }),
                "pitch-check files load");
        rolla::EngineParams params;
        params.presets = &bank;
        params.library = &lib;

        float outPeak = 0.f;
        auto played = [&] (int part, int note, rolla::ChordAsk ask, bool& chordFile)
        {
            rolla::Sampler s;
            s.prepare (48000.0);
            chordFile = s.take (part, note, 110, 1.0e9f, bank, params, ask).chordFile;
            std::vector<float> out (36000);
            outPeak = 0.f;
            for (auto& x : out)
            {
                float laneL[rolla::NumParts] {};
                float laneR[rolla::NumParts] {};
                s.render (laneL, laneR, 0.f, 0.f, false);
                x = laneL[part];
                outPeak = std::max (outPeak, std::fabs (x));
            }
            return rolla::probePitch (out.data(), nullptr, (int) out.size(), 48000.0);
        };
        auto got = [&] (const rolla::PitchInfo& p)
        {
            return " (got root " + std::to_string (p.rootPitch) + ", conf " + std::to_string (p.conf) + ", chord "
                 + std::to_string ((int) p.isChord) + ", output peak " + std::to_string (outPeak) + ")";
        };

        bool chord = false;
        const auto stab = played (rolla::Stab, 62, {}, chord);
        expect (stab.pitched() && rolla::pitchprobe::pcDistance (stab.rootPitch, 62.0) < 0.3,
                "SAMPLE_OUT_OF_KEY: a C4 stab asked for D sounds D" + got (stab));
        expect (! chord, "a single-note file is not a chord file");

        const auto vox = played (rolla::Vox, 64, {}, chord);
        expect (vox.pitched() && std::fabs (vox.rootPitch - 69.f) < 1.f,
                "VOX_PITCH_SHIFTED: a vox chop keeps its voice, whatever the label" + got (vox));

        rolla::ChordAsk cMajor;
        cMajor.quality = rolla::ChordQuality::Major;
        const auto pad = played (rolla::Pad, 60, cMajor, chord);
        expect (chord, "CHORD_STACK: a recorded chord reports itself, so the engine plays it once per chord");
        expect (pad.isChord && rolla::pitchprobe::pcDistance (pad.rootPitch, 64.0) < 0.3 && pad.quality == rolla::ChordQuality::Minor,
                "a C minor file on a C major chord plays from the third (E minor)" + got (pad));
        pitchRoot.deleteRecursively();
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
        // A recipe whose category the library lacks still draws from its own lane:
        // the vox "loops" recipe (tempo-locked phrase loops) with none on disk.
        {
            rolla::SampleSpec loops;
            loops.mode = rolla::SampleMode::Sample;
            loops.filter.on = true;
            loops.filter.lane = rolla::Vox;
            loops.filter.loop = 1;
            std::snprintf (loops.filter.category, sizeof loops.filter.category, "phrase");
            const auto exact = rolla::poolFilterFor (loops, rolla::Vox, -1, rolla::Pop);
            const auto widened = rolla::poolFilterWithFiles (*snap, loops, rolla::Vox, -1, rolla::Pop);
            const int widenedCount = rolla::sampleIndexCount (*snap, rolla::Vox, widened);
            bool allVox = widenedCount > 0;
            for (int i = 0; i < widenedCount; ++i)
                if (const auto* e = rolla::sampleIndexAt (*snap, rolla::Vox, widened, i))
                    allVox = allVox && juce::String (e->path).startsWith ("VOX/");
            expect (rolla::sampleIndexCount (*snap, rolla::Vox, exact) == 0 && widenedCount > 0 && allVox,
                    "a vox recipe with no matching files on disk widens within VOX instead of going silent ("
                        + std::to_string (widenedCount) + " files)");
            rolla::PoolFilter chopOnly;
            chopOnly.on = true;
            chopOnly.lane = rolla::Vox;
            std::snprintf (chopOnly.category, sizeof chopOnly.category, "chop");
            rolla::SampleSpec chops;
            chops.filter = chopOnly;
            const auto same = rolla::poolFilterWithFiles (*snap, chops, rolla::Vox, -1, rolla::Pop);
            expect (std::string (same.category) == "chop" && rolla::sampleIndexCount (*snap, rolla::Vox, same) == 1,
                    "a recipe that has files keeps its exact filter");
        }
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

        // Per-voice drum picks. Unset (-1) keeps the shared samplePick[Drums] file; a
        // set pick selects that ordinal of the voice's own scanned pool.
        {
            const auto kickFilter = rolla::poolFilterFor (recipes.drums[0].sample, rolla::Drums, rolla::Kick, recipeParams.dialect);
            const int kicks = rolla::sampleIndexCount (*snap, rolla::Drums, kickFilter);
            const auto* kick0 = rolla::sampleIndexAt (*snap, rolla::Drums, kickFilter, 0);
            const auto* kick1 = rolla::sampleIndexAt (*snap, rolla::Drums, kickFilter, 1);
            expect (kicks == 2 && kick0 != nullptr && kick1 != nullptr, "drum pick: two scanned kicks");
            expect (recipeParams.drumPick[rolla::Kick] == -1 && recipeParams.drumPick[rolla::TomLow] == -1,
                    "drum pick: EngineParams defaults every voice to unset");
            if (kicks == 2 && kick0 != nullptr && kick1 != nullptr)
            {
                expect (recipeSampler.lastPickedFile (rolla::Drums, rolla::Kick) == kick0->path,
                        "drum pick: unset plays today's shared-pick file");

                recipeSampler.reset();
                recipeParams.drumPick[rolla::Kick] = 1;
                recipeSampler.takeDrum (rolla::Kick, 100, recipes, recipeParams);
                expect (waitUntil ([&] { return scan.ready (kick1->path); }), "drum pick: the voice's own pick loads");
                recipeSampler.takeDrum (rolla::Kick, 100, recipes, recipeParams);
                expect (recipeSampler.lastPickedFile (rolla::Drums, rolla::Kick) == kick1->path,
                        "drum pick: a set pick plays that voice's file, not the shared pick's");

                // The prefetch list follows the same rule.
                int presetOf[rolla::NumParts] {};
                int picks[rolla::NumParts] {};
                int voicePicks[rolla::NumDrumVoices];
                std::fill (std::begin (voicePicks), std::end (voicePicks), -1);
                std::vector<std::string> shared, unset, voiced;
                rolla::collectPlayFiles (recipes, *snap, presetOf, picks, recipeParams.dialect, 1, shared);
                rolla::collectPlayFiles (recipes, *snap, presetOf, picks, recipeParams.dialect, 1, unset, voicePicks);
                voicePicks[rolla::Kick] = 1;
                rolla::collectPlayFiles (recipes, *snap, presetOf, picks, recipeParams.dialect, 1, voiced, voicePicks);
                expect (unset == shared, "drum pick: unset picks prefetch exactly today's files");
                expect (std::find (voiced.begin(), voiced.end(), std::string (kick1->path)) != voiced.end(),
                        "drum pick: the prefetch loads the voice's own pick");

                recipeSampler.reset();
                recipeParams.drumPick[rolla::Kick] = -1;
                recipeSampler.takeDrum (rolla::Kick, 100, recipes, recipeParams);
                expect (recipeSampler.lastPickedFile (rolla::Drums, rolla::Kick) == kick0->path,
                        "drum pick: clearing the pick goes back to the shared file");
            }
        }

        const auto cache = rolla::sampleCacheFor (scanRoot, "index");
        expect (! scanRoot.getChildFile (".rolla-index.json").exists(), "nothing is written into the sample folder");
        expect (cache.existsAsFile() && ! cache.isAChildOf (scanRoot), "the index is cached outside the samples");
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

    {
        // Kick key follow through takeDrum: a scanned 55 Hz kick in key C plays near
        // 49 Hz with the gate on, and exactly as recorded with it off (the default).
        const auto followRoot = rootDir.getChildFile ("keyfollow");
        followRoot.deleteRecursively();
        for (const char* kind : { "index", "loudness" })
            rolla::sampleCacheFor (followRoot, kind).deleteFile();
        writeWave (followRoot.getChildFile ("DRUMS/KICK/k55.wav"), tunedKick (55.0));
        const char* empty = R"JSON({"count":0,"files":[]})JSON";
        rolla::SampleLibrary follow;
        follow.setRoot (followRoot);
        follow.startIndex (empty, (int) std::strlen (empty));
        expect (waitUntil ([&] { return follow.indexedFound() >= 0; }), "key follow: index publishes");
        const auto followSnap = follow.index();

        rolla::PresetBank kit;
        rolla::loadDefaultPresets (kit);
        kit.count[rolla::Drums] = 1;
        kit.drums[0].sample.mode = rolla::SampleMode::Sample;
        kit.drums[0].sample.filter.on = true;
        kit.drums[0].sample.filter.lane = rolla::Drums;
        for (auto& pool : kit.drums[0].voicePool)
            pool.clear();
        rolla::EngineParams kp;
        kp.presets = &kit;
        kp.library = &follow;
        kp.sampleIndex = followSnap.get();
        kp.key = 0;
        kp.drumDecay[rolla::Kick] = 1.f;

        auto playedHz = [&] (bool on)
        {
            kp.kickKeyFollow = on;
            rolla::Sampler s;
            s.prepare (48000.0);
            s.takeDrum (rolla::Kick, 110, kit, kp);
            if (! waitUntil ([&] { return follow.ready ("DRUMS/KICK/k55.wav"); }))
                return 0.0;
            s.reset();
            s.takeDrum (rolla::Kick, 110, kit, kp);
            std::vector<float> out (9600);
            for (auto& x : out)
            {
                float laneL[rolla::NumParts] {};
                float laneR[rolla::NumParts] {};
                s.render (laneL, laneR, 0.f, 0.f, false);
                x = laneL[rolla::Drums];
            }
            // 30-150 ms: past the pitch drop, before the envelope runs out.
            return zeroCrossHz (out, 1440, 7200, 48000.0);
        };
        const double off = playedHz (false);
        const double on = playedHz (true);
        const auto* loaded = follow.tryGet ("DRUMS/KICK/k55.wav");
        expect (loaded != nullptr && loaded->pitch.pitched(), "key follow: the scanned kick has a measured pitch");
        expect (std::fabs (off - 55.0) < 2.0, "key follow: off by default, the kick plays as recorded (got "
                                                  + std::to_string (off) + " Hz)");
        expect (std::fabs (on - 49.0) < 2.0, "key follow: on, a 55 Hz kick in C plays near 49 Hz (got "
                                                 + std::to_string (on) + " Hz)");
        followRoot.deleteRecursively();
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
        const auto cacheFile = rolla::loudnessCacheFor (dualRoot);
        expect (! dualRoot.getChildFile (".rolla-loudness.json").exists(), "nothing is written into the sample folder");
        expect (! cacheFile.isAChildOf (dualRoot), "the loudness cache lives outside the sample folder");
        auto cacheText = cacheFile.loadFileAsString();
        expect (cacheText.contains ("VOX/quiet.wav"), "the gain is cached");
        cacheText = cacheText.replace ("\"gainDb\":", "\"gainDb\": 0, \"was\":");
        cacheFile.replaceWithText (cacheText);
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

    {
        rolla::SampleEntry vinyl;
        std::snprintf (vinyl.path, sizeof vinyl.path, "PAD/vinyl_crackle.wav");
        vinyl.lane = rolla::Pad;
        vinyl.onDisk = true;
        std::snprintf (vinyl.category, sizeof vinyl.category, "atmosphere");
        rolla::SampleEntry air;
        std::snprintf (air.path, sizeof air.path, "PAD/air_pad.wav");
        air.lane = rolla::Pad;
        air.onDisk = true;
        std::snprintf (air.category, sizeof air.category, "pad");
        rolla::SampleEntry acid;
        std::snprintf (acid.path, sizeof acid.path, "HITS/TAT_Synth_acid.wav");
        acid.lane = rolla::Hits;
        acid.onDisk = true;
        std::snprintf (acid.category, sizeof acid.category, "acid_hit");
        rolla::SampleEntry three;
        std::snprintf (three.path, sizeof three.path, "HITS/303saw.wav");
        three.lane = rolla::Hits;
        three.onDisk = true;
        std::snprintf (three.category, sizeof three.category, "fx");
        rolla::SampleEntry house;
        std::snprintf (house.path, sizeof house.path, "HITS/warehouse_hit.wav");
        house.lane = rolla::Hits;
        house.onDisk = true;
        std::snprintf (house.category, sizeof house.category, "fx");
        rolla::SampleEntry riser;
        std::snprintf (riser.path, sizeof riser.path, "HITS/noise_riser.wav");
        riser.lane = rolla::Hits;
        riser.onDisk = true;
        std::snprintf (riser.category, sizeof riser.category, "fx");
        rolla::SampleEntry tagged;
        std::snprintf (tagged.path, sizeof tagged.path, "BASS/only_uk.wav");
        tagged.lane = rolla::Bass;
        tagged.onDisk = true;
        tagged.styles = 1;
        rolla::SampleEntry open;
        std::snprintf (open.path, sizeof open.path, "BASS/any.wav");
        open.lane = rolla::Bass;
        open.onDisk = true;
        rolla::SampleIndex pools;
        pools.entries = { vinyl, air, acid, three, house, riser, tagged, open };

        rolla::PoolFilter pad;
        pad.on = true;
        pad.lane = rolla::Pad;
        pad.dialect = rolla::UkMin;
        expect (rolla::sampleIndexCount (pools, rolla::Pad, pad) == 1, "a vinyl crackle is not a pad");
        expect (std::string (rolla::sampleIndexAt (pools, rolla::Pad, pad, 0)->path) == "PAD/air_pad.wav",
                "the pad pool keeps the pad");
        pad.dialect = -1;
        expect (rolla::sampleIndexCount (pools, rolla::Pad, pad) == 1, "vinyl stays out when dialect is unset");

        rolla::PoolFilter hits;
        hits.on = true;
        hits.lane = rolla::Hits;
        hits.dialect = rolla::UkMin;
        expect (rolla::sampleIndexCount (pools, rolla::Hits, hits) == 0, "UK MIN hits drop acid, fx and risers");
        hits.dialect = rolla::Long;
        expect (rolla::sampleIndexCount (pools, rolla::Hits, hits) == 0, "LONG hits drop acid");
        hits.dialect = rolla::Pop;
        expect (rolla::sampleIndexCount (pools, rolla::Hits, hits) == 0, "BOUNCY hits drop acid");
        hits.dialect = rolla::Gaskin;
        expect (rolla::sampleIndexCount (pools, rolla::Hits, hits) == 0, "JACKIN hits drop acid");

        rolla::PoolFilter bass;
        bass.on = true;
        bass.lane = rolla::Bass;
        bass.dialect = rolla::Pop;
        expect (rolla::sampleIndexCount (pools, rolla::Bass, bass) == 1, "a ukmin-only file stays out of BOUNCY");
        bass.dialect = rolla::UkMin;
        expect (rolla::sampleIndexCount (pools, rolla::Bass, bass) == 2, "styles 0 still fits UK MIN");
        expect (rolla::sampleHasToken ("HITS/303fx_hat.wav", "303"), "303 matches 303fx");
        expect (! rolla::sampleHasToken ("HITS/1303_hat.wav", "303"), "303 does not match 1303");
    }

    {
        auto add = [] (rolla::SampleIndex& index, int lane, const char* path, const char* category)
        {
            rolla::SampleEntry entry;
            std::snprintf (entry.path, sizeof entry.path, "%s", path);
            entry.lane = lane;
            entry.onDisk = true;
            if (category != nullptr)
                std::snprintf (entry.category, sizeof entry.category, "%s", category);
            index.entries.push_back (entry);
        };
        rolla::SampleIndex fake;
        add (fake, rolla::Pad, "PAD/MTN_FX_Noise_Bed.wav", "pad");
        add (fake, rolla::Pad, "PAD/VER_134_VinylNoise_FX.wav", "pad");
        add (fake, rolla::Pad, "PAD/CD_SFX_Vinyl_Texture.wav", "pad");
        add (fake, rolla::Pad, "PAD/JF_128_Best_Vinyl_Background.wav", "pad");
        add (fake, rolla::Pad, "PAD/MTN_FX_Drone.wav", "pad");
        add (fake, rolla::Pad, "PAD/JustNoize_Background.wav", "pad");
        add (fake, rolla::Pad, "PAD/Effects_Wash.wav", "pad");
        add (fake, rolla::Pad, "PAD/Vinyl/pretty_pad_Fm.wav", "pad");
        add (fake, rolla::Pad, "PAD/prefix_warm_Fm.wav", "pad");
        add (fake, rolla::Pad, "PAD/Air_Pad_Fm.wav", "pad");
        add (fake, rolla::Pluck, "PLUCK/Telephone_Bell_C.wav", "pluck");
        add (fake, rolla::Pluck, "PLUCK/Whoosh_Downlifter.wav", "pluck");
        add (fake, rolla::Pluck, "PLUCK/Pluck_Chord_D5.wav", "pluck");
        add (fake, rolla::Stab, "STAB/Uplifter_Sweep.wav", "stab");
        add (fake, rolla::Stab, "STAB/Stab_Chord_Fm.wav", "stab");
        add (fake, rolla::Hits, "HITS/MA_128_Lazer_Synth_FX.wav", "fx");
        add (fake, rolla::Hits, "HITS/Cat_Synth_FX.wav", "fx");
        add (fake, rolla::Hits, "HITS/CD_SFX_Razor.wav", "fx");
        add (fake, rolla::Hits, "HITS/Telephone_Synth_FX.wav", "fx");
        add (fake, rolla::Hits, "HITS/Acid_Hit_C.wav", "acid_hit");
        add (fake, rolla::Hits, "HITS/303_Saw_D4.wav", "fx");
        add (fake, rolla::Hits, "HITS/Noise_Riser_C.wav", "fx");
        add (fake, rolla::Hits, "HITS/FX/Chord_Stab_C.wav", "chord");
        add (fake, rolla::Hits, "HITS/hitch_ride_C.wav", "hit");
        add (fake, rolla::Hits, "HITS/Chord_Stab_Cmin.wav", "chord");
        add (fake, rolla::Hits, "HITS/Rave_Stabber_Dmin.wav", "stab");
        add (fake, rolla::Hits, "HITS/Organ_Hit_Fm.wav", "organ");
        add (fake, rolla::Hits, "HITS/Piano_Keys_A_Minor.wav", "keys");
        add (fake, rolla::Hits, "HITS/VER_Synth_Hit_C.wav", "hit");
        add (fake, rolla::Hits, "HITS/TAT_Synth_C.wav", "synth");
        add (fake, rolla::Hits, "HITS/One_Shot_Hit_Cs.wav", "hit");

        expect (rolla::sampleHasToken ("Effects_Wash", "effects"), "Effects is the fx word");
        expect (rolla::sampleHasToken ("Cat_Synth_FX", "fx"), "a separated _FX matches");
        expect (! rolla::sampleHasToken ("prefix_warm_Fm", "fx"), "prefix does not contain an fx token");
        expect (! rolla::sampleHasToken ("hitch_ride_C", "hit"), "hitch is not the word hit");
        expect (rolla::sampleRoleRejected ("PAD/Effects_Wash.wav", rolla::Pad), "effects is rejected");
        expect (! rolla::sampleRoleRejected ("PAD/prefix_warm_Fm.wav", rolla::Pad), "prefix stays");

        auto paths = [] (const rolla::SampleIndex& index, int lane, int dialect)
        {
            rolla::PoolFilter filter;
            filter.on = true;
            filter.lane = lane;
            filter.dialect = dialect;
            std::vector<std::string> out;
            const int n = rolla::sampleIndexCount (index, lane, filter);
            for (int i = 0; i < n; ++i)
            {
                const auto* entry = rolla::sampleIndexAt (index, lane, filter, i);
                if (entry != nullptr)
                    out.emplace_back (entry->path);
            }
            return out;
        };
        auto has = [] (const std::vector<std::string>& names, const char* path)
        {
            for (const auto& name : names)
                if (name == path)
                    return true;
            return false;
        };

        const auto pads = paths (fake, rolla::Pad, rolla::UkMin);
        expect (pads.size() == 2, "pad keeps the tonal names and drops fx noise vinyl");
        expect (has (pads, "PAD/prefix_warm_Fm.wav") && has (pads, "PAD/Air_Pad_Fm.wav"), "pad tonal names stay");
        expect (! has (pads, "PAD/MTN_FX_Noise_Bed.wav"), "pad drops noise fx");
        expect (! has (pads, "PAD/VER_134_VinylNoise_FX.wav"), "pad drops vinyl noise");
        expect (! has (pads, "PAD/CD_SFX_Vinyl_Texture.wav"), "pad drops sfx texture");
        expect (! has (pads, "PAD/JF_128_Best_Vinyl_Background.wav"), "pad drops vinyl background");
        expect (! has (pads, "PAD/MTN_FX_Drone.wav"), "pad drops drone");
        expect (! has (pads, "PAD/JustNoize_Background.wav"), "pad drops background");
        expect (! has (pads, "PAD/Effects_Wash.wav"), "pad drops Effects");
        expect (! has (pads, "PAD/Vinyl/pretty_pad_Fm.wav"), "pad drops a vinyl folder");

        const auto plucks = paths (fake, rolla::Pluck, rolla::Pop);
        expect (plucks.size() == 1 && plucks[0] == "PLUCK/Pluck_Chord_D5.wav", "pluck drops telephone and whoosh");
        const auto stabs = paths (fake, rolla::Stab, rolla::Gaskin);
        expect (stabs.size() == 1 && stabs[0] == "STAB/Stab_Chord_Fm.wav", "stab drops the uplifter sweep");

        for (int dialect : { rolla::UkMin, rolla::Long, rolla::Pop, rolla::Gaskin })
        {
            const auto hitPaths = paths (fake, rolla::Hits, dialect);
            expect (has (hitPaths, "HITS/Chord_Stab_Cmin.wav"), "tonal chord stab stays");
            expect (has (hitPaths, "HITS/Rave_Stabber_Dmin.wav"), "rave stabber stays");
            expect (has (hitPaths, "HITS/Organ_Hit_Fm.wav"), "organ hit stays");
            expect (has (hitPaths, "HITS/Piano_Keys_A_Minor.wav"), "piano keys stay");
            expect (has (hitPaths, "HITS/VER_Synth_Hit_C.wav"), "VER synth hit stays");
            expect (has (hitPaths, "HITS/TAT_Synth_C.wav"), "TAT synth stays");
            expect (has (hitPaths, "HITS/One_Shot_Hit_Cs.wav"), "a hit with a key stays");
            expect (! has (hitPaths, "HITS/MA_128_Lazer_Synth_FX.wav"), "lazer fx stays out");
            expect (! has (hitPaths, "HITS/Cat_Synth_FX.wav"), "cat synth fx stays out");
            expect (! has (hitPaths, "HITS/CD_SFX_Razor.wav"), "razor sfx stays out");
            expect (! has (hitPaths, "HITS/Telephone_Synth_FX.wav"), "telephone fx stays out");
            expect (! has (hitPaths, "HITS/Acid_Hit_C.wav"), "acid stays out of hits");
            expect (! has (hitPaths, "HITS/303_Saw_D4.wav"), "303 stays out of hits");
            expect (! has (hitPaths, "HITS/Noise_Riser_C.wav"), "noise riser stays out");
            expect (! has (hitPaths, "HITS/FX/Chord_Stab_C.wav"), "an FX folder stays out");
            expect (! has (hitPaths, "HITS/hitch_ride_C.wav"), "hitch is not a tonal hit");
            expect (hitPaths.size() == 7, "hits keep the seven tonal names");
        }

        rolla::SampleIndex pitchedOnly;
        add (pitchedOnly, rolla::Hits, "HITS/MA_128_Lazer_Synth_FX.wav", "fx");
        add (pitchedOnly, rolla::Hits, "HITS/Synth_One_Shot_C.wav", "synth");
        rolla::PoolFilter loose;
        loose.on = true;
        loose.lane = rolla::Hits;
        loose.dialect = rolla::Gaskin;
        expect (rolla::sampleIndexCount (pitchedOnly, rolla::Hits, loose) == 1, "a pitched same-lane file is the fallback");
        expect (std::string (rolla::sampleIndexAt (pitchedOnly, rolla::Hits, loose, 0)->path) == "HITS/Synth_One_Shot_C.wav",
                "the fallback stays in the hits lane");

        rolla::SampleIndex emptyLane;
        add (emptyLane, rolla::Hits, "HITS/Acid_Hit_C.wav", "acid_hit");
        add (emptyLane, rolla::Hits, "HITS/CD_SFX_Razor.wav", "fx");
        add (emptyLane, rolla::Hits, "HITS/Vinyl_Crackle.wav", "vinyl");
        add (emptyLane, rolla::Pad, "PAD/MTN_FX_Noise_Bed.wav", "pad");
        const auto emptyHits = rolla::pickLaneSample (emptyLane, rolla::Hits, loose, 0);
        expect (emptyHits.mute && emptyHits.entry == nullptr, "an empty hits pool mutes");
        rolla::PoolFilter emptyPad;
        emptyPad.on = true;
        emptyPad.lane = rolla::Pad;
        emptyPad.dialect = rolla::UkMin;
        const auto emptyPads = rolla::pickLaneSample (emptyLane, rolla::Pad, emptyPad, 3);
        expect (emptyPads.mute && emptyPads.entry == nullptr, "an empty pad pool mutes");

        rolla::SampleLibrary muteLib;
        rolla::PresetBank muteBank;
        muteBank.count[rolla::Hits] = 1;
        muteBank.hits[0].sample.mode = rolla::SampleMode::Sample;
        muteBank.hits[0].sample.filter.on = true;
        muteBank.hits[0].sample.filter.lane = rolla::Hits;
        rolla::Sampler muteSampler;
        muteSampler.prepare (48000.0);
        rolla::EngineParams muteParams;
        muteParams.presets = &muteBank;
        muteParams.library = &muteLib;
        muteParams.sampleIndex = &emptyLane;
        muteParams.dialect = rolla::Gaskin;
        const auto muted = muteSampler.take (rolla::Hits, 64, 110, 4.f, muteBank, muteParams);
        expect (muted.kind == 1 && std::fabs (muted.synthGain) < 1.0e-6f,
                "an empty pool mutes instead of synthesising");
    }

    {
        const auto wrapRoot = rootDir.getChildFile ("wrap");
        writeTone (wrapRoot.getChildFile ("DRUMS/KICK/down.wav"), 0.25, 0.9f, 70.f, false);
        const char* manifest = R"JSON({"files":[{"file":"DRUMS/KICK/down.wav","lane":"DRUMS","voice":"KICK","category":"kick"}]})JSON";
        rolla::SampleLibrary wrapLib;
        wrapLib.setRoot (wrapRoot);
        wrapLib.startIndex (manifest, (int) std::strlen (manifest));
        expect (waitUntil ([&] { return wrapLib.indexedFound() >= 0; }), "downbeat index publishes");
        const auto wrapSnap = wrapLib.index();
        rolla::PresetBank wrapBank;
        wrapBank.count[rolla::Drums] = 1;
        wrapBank.drums[0].sample.mode = rolla::SampleMode::Sample;
        wrapBank.drums[0].sample.filter.on = true;
        wrapBank.drums[0].sample.filter.lane = rolla::Drums;
        rolla::Sampler wrapSampler;
        wrapSampler.prepare (48000.0);
        rolla::EngineParams wrapParams;
        wrapParams.presets = &wrapBank;
        wrapParams.library = &wrapLib;
        wrapParams.sampleIndex = wrapSnap.get();
        wrapParams.dialect = rolla::UkMin;
        for (int i = 0; i < rolla::NumDrumVoices; ++i)
            wrapParams.drumDecay[i] = 0.5f;
        expect (wrapSampler.takeDrum (rolla::Kick, 120, wrapBank, wrapParams) < 0.001f, "downbeat kick requests the file");
        expect (waitUntil ([&] { return wrapLib.ready ("DRUMS/KICK/down.wav"); }), "downbeat kick loads");
        wrapSampler.takeDrum (rolla::Kick, 120, wrapBank, wrapParams);
        float bornPeak = 0.f;
        float prev = 31.99f;
        float pos = 0.01f;
        for (int i = 0; i < 800; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            wrapSampler.render (laneL, laneR, prev, pos, true, 32.f);
            bornPeak = std::max (bornPeak, std::fabs (laneL[rolla::Drums]));
            prev = pos;
            pos += 0.02f;
        }
        expect (bornPeak > 0.01f, "a kick born on the downbeat wrap still sounds");

        wrapSampler.reset();
        wrapSampler.takeDrum (rolla::Kick, 120, wrapBank, wrapParams);
        float laneL[rolla::NumParts] {};
        float laneR[rolla::NumParts] {};
        wrapSampler.render (laneL, laneR, 10.f, 10.02f, true, 32.f);
        wrapSampler.render (laneL, laneR, 31.99f, 0.01f, true, 32.f);
        float after = 0.f;
        prev = 0.01f;
        pos = 0.03f;
        // The cut is a 4 ms fade now (no click), then silence.
        for (int i = 0; i < 200; ++i)
        {
            wrapSampler.render (laneL, laneR, prev, pos, true, 32.f);
            prev = pos;
            pos += 0.02f;
        }
        for (int i = 0; i < 40; ++i)
        {
            wrapSampler.render (laneL, laneR, prev, pos, true, 32.f);
            after = std::max (after, std::fabs (laneL[rolla::Drums]));
            prev = pos;
            pos += 0.02f;
        }
        expect (after < 1.0e-4f, "a kick already sounding fades out within 4 ms of the next loop wrap");

        rolla::Engine downbeat;
        downbeat.prepare (48000.0, 256);
        rolla::Groove groove;
        groove.bars = 2;
        groove.key = 2;
        groove.drums.vel[rolla::Kick][0] = 120;
        rolla::EngineParams play = wrapParams;
        play.bars = 2;
        play.bpm = 130.f;
        play.duck = false;
        rolla::Transport transport;
        transport.playing = true;
        transport.bpm = 130.0;
        transport.ppqStart = 0.0;
        transport.sampleRate = 48000.0;
        float left[512] {};
        float right[512] {};
        for (int block = 0; block < 2; ++block)
        {
            juce::MidiBuffer midi;
            downbeat.process (groove, play, transport, left + block * 256, right + block * 256, nullptr, nullptr, 256, midi, true);
            int kickMidi = -1;
            for (const auto meta : midi)
            {
                const auto msg = meta.getMessage();
                if (msg.isNoteOn() && msg.getChannel() == 10 && msg.getNoteNumber() == rolla::drumMidiNote (rolla::Kick))
                    kickMidi = block * 256 + meta.samplePosition;
            }
            if (block == 0)
                expect (kickMidi == 0, "the first kick note is sample 0");
            transport.ppqStart += 256.0 * 130.0 / 60.0 / 48000.0;
        }
        const int lat = downbeat.latencySamples();
        int onset = -1;
        for (int i = 0; i < 512; ++i)
            if (std::fabs (left[i]) > 1.0e-3f)
            {
                onset = i;
                break;
            }
        expect (onset >= 0, "the downbeat kick is audible in the first block");
        expect (onset >= lat && onset - lat < 400, "after the true-peak delay the kick is the downbeat, not the next 16th");
    }

    {
        // The same kick sample retriggered 8x in a row (every quarter, each one cutting
        // the last while it is still loud) must click no more than the first kick or the
        // raw file: no steal / retrigger cut without a fade, no alternating voices.
        const auto kRoot = rootDir.getChildFile ("retrig");
        writeKick (kRoot.getChildFile ("DRUMS/KICK/long.wav"), 0.9);
        const char* manifest = R"JSON({"files":[{"file":"DRUMS/KICK/long.wav","lane":"DRUMS","voice":"KICK","category":"kick"}]})JSON";
        rolla::SampleLibrary kLib;
        kLib.setRoot (kRoot);
        kLib.startIndex (manifest, (int) std::strlen (manifest));
        expect (waitUntil ([&] { return kLib.indexedFound() >= 0; }), "retrigger index publishes");
        const auto kSnap = kLib.index();
        rolla::PresetBank kBank;
        kBank.count[rolla::Drums] = 1;
        kBank.drums[0].sample.mode = rolla::SampleMode::Sample;
        kBank.drums[0].sample.filter.on = true;
        kBank.drums[0].sample.filter.lane = rolla::Drums;
        kLib.preload ({ std::string ("DRUMS/KICK/long.wav") });
        expect (waitUntil ([&] { return kLib.ready ("DRUMS/KICK/long.wav"); }), "retrigger kick preloads");

        for (const float decay : { 0.5f, 1.0f })
        {
            rolla::EngineParams play;
            play.presets = &kBank;
            play.library = &kLib;
            play.sampleIndex = kSnap.get();
            play.dialect = rolla::UkMin;
            for (int i = 0; i < rolla::NumDrumVoices; ++i)
                play.drumDecay[i] = decay;
            play.bars = 2;
            play.bpm = 130.f;
            play.duck = false;
            rolla::Engine eng;
            eng.prepare (48000.0, 256);
            rolla::Groove groove;
            groove.bars = 2;
            groove.key = 2;
            for (int st = 0; st < 32; st += 4)
                groove.drums.vel[rolla::Kick][st] = 120;
            rolla::Transport tr;
            tr.playing = true;
            tr.bpm = 130.0;
            tr.ppqStart = 0.0;
            tr.sampleRate = 48000.0;
            const int loopN = (int) std::lround (8.0 * 60.0 / 130.0 * 48000.0);
            const int blocks = loopN / 256;
            std::vector<float> outL ((size_t) blocks * 256, 0.f), outR ((size_t) blocks * 256, 0.f);
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                eng.process (groove, play, tr, outL.data() + b * 256, outR.data() + b * 256, nullptr, nullptr, 256, midi, true);
                tr.ppqStart += 256.0 * 130.0 / 60.0 / 48000.0;
            }
            const int lat = eng.latencySamples();
            const double quarterN = 60.0 / 130.0 * 48000.0;
            // Click score: worst |second difference| from 5 ms before to 10 ms after the onset,
            // relative to that kick's own body peak.
            auto score = [&] (const std::vector<float>& x, int on) {
                float d2 = 0.f, pk = 1.0e-9f;
                for (int i = std::max (2, on - 240); i < std::min ((int) x.size(), on + 480); ++i)
                    d2 = std::max (d2, std::fabs (x[(size_t) i] - 2.f * x[(size_t) i - 1] + x[(size_t) i - 2]));
                for (int i = on; i < std::min ((int) x.size(), on + 2400); ++i)
                    pk = std::max (pk, std::fabs (x[(size_t) i]));
                return d2 / pk;
            };
            std::vector<float> raw (48000, 0.f);
            {
                double phase = 0.0;
                for (int i = 0; i < 48000; ++i)
                {
                    const double t = i / 48000.0;
                    phase += 2.0 * 3.14159265358979 * (50.0 + 100.0 * std::exp (-t / 0.03)) / 48000.0;
                    raw[(size_t) i + 0] = (float) (0.8 * std::exp (-t / 0.35) * std::sin (phase));
                }
            }
            std::vector<float> rawPad (480, 0.f);
            rawPad.insert (rawPad.end(), raw.begin(), raw.end());
            const float rawScore = score (rawPad, 480);
            std::vector<float> s;
            for (int k = 0; k < 8; ++k)
                s.push_back (score (outL, lat + (int) std::lround (k * quarterN)));
            float lo = 1.0e9f, hi = 0.f;
            for (int k = 1; k < 8; ++k)
            {
                lo = std::min (lo, s[(size_t) k]);
                hi = std::max (hi, s[(size_t) k]);
            }
            std::string detail = " (raw " + std::to_string (rawScore) + ", first " + std::to_string (s[0])
                               + ", retriggers " + std::to_string (lo) + ".." + std::to_string (hi) + ", decay "
                               + std::to_string (decay) + ")";
            std::cout << "INFO kick retrigger click scores" << detail << "\n";
            expect (s[0] <= rawScore * 2.0f + 1.0e-3f, "the first kick's onset is no sharper than the raw file" + detail);
            expect (hi <= std::max (s[0], rawScore) * 1.5f + 1.0e-3f,
                    "a retriggered kick (cutting the last one) clicks no more than a fresh kick" + detail);
            expect (hi <= lo * 1.5f + 1.0e-3f, "all 8 retriggered kicks are alike: no alternating-voice clicks" + detail);
        }
    }

    {
        std::vector<float> pureSine (4096);
        std::vector<float> harmonic (8192);
        std::vector<float> noise (8192);
        std::uint32_t rng = 1u;
        for (int i = 0; i < 8192; ++i)
        {
            const float t = (float) i / 48000.f;
            if (i < 4096)
                pureSine[(size_t) i] = std::sin (2.f * 3.14159265f * 440.f * t);
            float body = 0.62f * std::cos (2.f * 3.14159265f * 293.7f * t);
            const float wide = 0.14f * std::cos (2.f * 3.14159265f * 293.7f * 1.07f * t);
            const float harms[] = { 0.22f, 0.16f, 0.1f, 0.06f };
            for (int k = 0; k < 4; ++k)
                body += harms[k] * std::cos (2.f * 3.14159265f * 293.7f * (float) (k + 2) * t);
            harmonic[(size_t) i] = body + wide;
            rng = rng * 1664525u + 1013904223u;
            noise[(size_t) i] = ((rng >> 8) / 16777216.f) * 2.f - 1.f;
        }
        const float sineFlat = rolla::spectralFlatness (pureSine.data(), (int) pureSine.size());
        const float harmFlat = rolla::spectralFlatness (harmonic.data(), (int) harmonic.size());
        const float noiseFlat = rolla::spectralFlatness (noise.data(), (int) noise.size());
        expect (sineFlat >= 0.f && sineFlat < 0.15f, "a sine is tonal (" + std::to_string (sineFlat) + ")");
        expect (harmFlat >= 0.f && harmFlat < rolla::kTonalFlatness,
                "a harmonic tone stays under the chop cutoff (" + std::to_string (harmFlat) + ")");
        expect (noiseFlat > 0.55f, "noise is broadband (" + std::to_string (noiseFlat) + ")");

        auto addFlat = [] (rolla::SampleIndex& index, const char* path, float flatness)
        {
            rolla::SampleEntry entry;
            std::snprintf (entry.path, sizeof entry.path, "%s", path);
            entry.lane = rolla::Pad;
            entry.onDisk = true;
            entry.flatness = flatness;
            index.entries.push_back (entry);
        };
        rolla::PoolFilter padFilter;
        padFilter.on = true;
        padFilter.lane = rolla::Pad;
        padFilter.dialect = rolla::UkMin;
        rolla::SampleIndex unmeasured;
        addFlat (unmeasured, "PAD/Warm_Texture_Fm.wav", -1.f);
        expect (rolla::sampleIndexCount (unmeasured, rolla::Pad, padFilter) == 0,
                "an unmeasured texture stays out");
        rolla::SampleIndex tonalBed;
        addFlat (tonalBed, "PAD/Warm_Texture_Fm.wav", 0.1f);
        expect (rolla::sampleIndexCount (tonalBed, rolla::Pad, padFilter) == 1,
                "a measured tonal texture can be a pad");
        rolla::SampleIndex noisyBed;
        addFlat (noisyBed, "PAD/Warm_Texture_Fm.wav", 0.9f);
        expect (rolla::sampleIndexCount (noisyBed, rolla::Pad, padFilter) == 0,
                "a measured broadband texture stays out");
        rolla::SampleIndex vinylBed;
        addFlat (vinylBed, "PAD/Vinyl_Texture_Fm.wav", 0.05f);
        expect (rolla::sampleIndexCount (vinylBed, rolla::Pad, padFilter) == 0,
                "vinyl stays out even when the read looks tonal");
        rolla::SampleIndex fxDrone;
        addFlat (fxDrone, "PAD/MTN_FX_Drone.wav", 0.05f);
        expect (rolla::sampleIndexCount (fxDrone, rolla::Pad, padFilter) == 0,
                "an fx drone stays out even when the read looks tonal");

        constexpr int rate = 48000;
        const int chopFrames = (int) (rate * (rolla::kChopMinSeconds + 1.5));
        std::vector<float> src ((size_t) chopFrames);
        for (int i = 0; i < chopFrames; ++i)
            src[(size_t) i] = 0.9f * std::cos (2.f * 3.14159265f * 440.f * (float) i / (float) rate);
        const std::vector<float> untouched = src;
        const int onset = rate / 2;
        const int onsets[] = { onset };
        const auto plan = rolla::planSampleChop (rolla::Hits, chopFrames, (double) rate, 130.f, 0u,
                                                  onsets, 1, 0, harmFlat, false, false);
        expect (rolla::chopPlanOn (plan), "a long tonal hit is sliced");
        expect (plan.start == onset, "the slice starts on the hashed onset");
        const int quarter = (int) ((double) rate * 60.0 / 130.0);
        expect (plan.end > plan.start && plan.end - plan.start <= quarter + 8, "the slice is at most a quarter note");
        expect (plan.fade >= 8 && plan.fade <= (int) (0.006 * rate), "the edge fade is a few milliseconds");
        expect (std::fabs (src[(size_t) plan.start]) > 0.5f, "the onset sample itself is not a zero crossing");
        std::vector<float> sliced ((size_t) chopFrames, 0.f);
        for (int i = plan.start; i < plan.end; ++i)
            sliced[(size_t) i] = src[(size_t) i] * rolla::chopEdge (i, plan.start, plan.end, plan.fade);
        expect (src == untouched, "slicing does not write the source buffer");
        expect (std::fabs (sliced[(size_t) plan.start]) < 1.0e-5f, "the slice fades in from silence");
        expect (std::fabs (sliced[(size_t) (plan.end - 1)]) < 1.0e-5f, "the slice fades out to silence");
        const float rawStep = std::fabs (src[(size_t) plan.start]);
        float edgeStep = 0.f;
        for (int i = plan.start; i < plan.start + plan.fade; ++i)
            edgeStep = std::max (edgeStep, std::fabs (sliced[(size_t) i] - sliced[(size_t) (i - 1)]));
        for (int i = plan.end - plan.fade; i < plan.end; ++i)
            edgeStep = std::max (edgeStep, std::fabs (sliced[(size_t) i] - sliced[(size_t) (i - 1)]));
        expect (edgeStep < rawStep * 0.25f, "neither slice edge is a step");
        const int body = plan.start + (plan.end - plan.start) / 2;
        expect (std::fabs (sliced[(size_t) body]) > std::pow (10.f, -6.f / 20.f),
                "the middle of the slice is hotter than -6 dBTP");

        const auto shortPlan = rolla::planSampleChop (rolla::Hits, rate / 2, (double) rate, 130.f, 0u,
                                                       onsets, 1, 0, 0.05f, false, false);
        expect (! rolla::chopPlanOn (shortPlan) && ! shortPlan.blocked, "a short one-shot stays whole");
        const auto blocked = rolla::planSampleChop (rolla::Hits, chopFrames, (double) rate, 130.f, 1u,
                                                     onsets, 1, 0, noiseFlat, false, false);
        expect (blocked.blocked, "broadband audio is not a hit chop");
        const auto named = rolla::planSampleChop (rolla::Stab, chopFrames, (double) rate, 130.f, 2u,
                                                   onsets, 1, 0, 0.05f, true, false);
        expect (named.blocked, "a noise name is not chopped");
        const auto held = rolla::planSampleChop (rolla::Pad, chopFrames, (double) rate, 130.f, 3u,
                                                  nullptr, 0, onset, 0.1f, false, true);
        expect (held.hold && rolla::chopPlanOn (held), "a tonal pad bed holds its loud region");

        rolla::TruePeakGuard guard;
        guard.prepare ((double) rate);
        const float ceiling = std::pow (10.f, -6.f / 20.f);
        std::vector<float> left = sliced;
        std::vector<float> right = sliced;
        const int flush = guard.latencySamples() + 64;
        left.resize (left.size() + (size_t) flush, 0.f);
        right.resize (right.size() + (size_t) flush, 0.f);
        float guarded = 0.f;
        for (int i = 0; i < (int) left.size();)
        {
            const int n = std::min (256, (int) left.size() - i);
            guard.process (left.data() + i, right.data() + i, nullptr, nullptr, n);
            for (int k = 0; k < n; ++k)
                guarded = std::max (guarded, std::max (std::fabs (left[(size_t) (i + k)]),
                                                       std::fabs (right[(size_t) (i + k)])));
            i += n;
        }
        expect (guard.takeMaxGrDb() > 0.1f, "the -6 dBTP guard engages on the hot slice");
        expect (guarded <= ceiling + 1.0e-3f, "the guarded slice stays under -6 dBTP");

        const auto chopRoot = rootDir.getChildFile ("chop");
        const auto hitFile = chopRoot.getChildFile ("HITS/Chord_Stab_C4.wav");
        const auto tonalPad = chopRoot.getChildFile ("PAD/Warm_Texture_C4.wav");
        const auto noisyPad = chopRoot.getChildFile ("PAD/Room_Texture_C4.wav");
        const auto vinylPad = chopRoot.getChildFile ("PAD/Vinyl_Texture_C4.wav");
        const double longSeconds = rolla::kChopMinSeconds + 1.5;
        writeTone (tonalPad, 0.6, 0.4f, 220.f, false);
        writeTone (vinylPad, 0.6, 0.4f, 220.f, false);
        {
            const int n = (int) std::lround (0.6 * 48000.0);
            std::vector<float> hiss ((size_t) n);
            std::uint32_t s = 99u;
            for (float& x : hiss)
            {
                s = s * 1664525u + 1013904223u;
                x = (((s >> 8) & 0xffff) / 32768.f) - 1.f;
            }
            tonalPad.getParentDirectory().createDirectory();
            juce::WavAudioFormat wav;
            auto stream = std::make_unique<juce::FileOutputStream> (noisyPad);
            std::unique_ptr<juce::OutputStream> owned (stream.release());
            const auto options = juce::AudioFormatWriterOptions().withSampleRate (48000.0)
                                                                  .withNumChannels (1)
                                                                  .withBitsPerSample (16);
            auto writer = wav.createWriterFor (owned, options);
            expect (writer != nullptr, "noise texture writes");
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> buf (1, n);
                for (int i = 0; i < n; ++i)
                    buf.setSample (0, i, hiss[(size_t) i]);
                writer->writeFromAudioSampleBuffer (buf, 0, n);
            }
        }
        {
            const int n = (int) std::lround (longSeconds * 48000.0);
            juce::WavAudioFormat wav;
            hitFile.getParentDirectory().createDirectory();
            auto stream = std::make_unique<juce::FileOutputStream> (hitFile);
            std::unique_ptr<juce::OutputStream> owned (stream.release());
            const auto options = juce::AudioFormatWriterOptions().withSampleRate (48000.0)
                                                                  .withNumChannels (1)
                                                                  .withBitsPerSample (16);
            auto writer = wav.createWriterFor (owned, options);
            expect (writer != nullptr, "hit slice writes");
            if (writer != nullptr)
            {
                juce::AudioBuffer<float> buf (1, n);
                for (int i = 0; i < n; ++i)
                    buf.setSample (0, i, 0.9f * std::cos (2.f * 3.14159265f * 440.f * (float) i / 48000.f));
                writer->writeFromAudioSampleBuffer (buf, 0, n);
            }
        }
        juce::MemoryBlock before;
        hitFile.loadFileAsData (before);
        rolla::SampleLibrary chopLib;
        chopLib.setRoot (chopRoot);
        chopLib.startIndex ("{\"files\":[]}", 12);
        expect (waitUntil ([&] { return chopLib.indexedFound() >= 4; }), "chop index publishes");
        const auto chopSnap = chopLib.index();
        expect (chopSnap != nullptr && (int) chopSnap->entries.size() >= 4, "chop index has the four files");
        if (chopSnap != nullptr)
        {
            rolla::PoolFilter pads;
            pads.on = true;
            pads.lane = rolla::Pad;
            pads.dialect = rolla::UkMin;
            const auto padNames = [&] ()
            {
                std::vector<std::string> names;
                const int n = rolla::sampleIndexCount (*chopSnap, rolla::Pad, pads);
                for (int i = 0; i < n; ++i)
                    if (const auto* entry = rolla::sampleIndexAt (*chopSnap, rolla::Pad, pads, i))
                        names.emplace_back (entry->path);
                return names;
            }();
            auto listed = [] (const std::vector<std::string>& names, const char* path)
            {
                for (const auto& name : names)
                    if (name == path)
                        return true;
                return false;
            };
            expect (listed (padNames, "PAD/Warm_Texture_C4.wav"), "index keeps a tonal texture");
            expect (! listed (padNames, "PAD/Room_Texture_C4.wav"), "index drops a broadband texture");
            expect (! listed (padNames, "PAD/Vinyl_Texture_C4.wav"), "index drops vinyl even if tonal");
        }

        rolla::PresetBank chopBank;
        chopBank.count[rolla::Hits] = 1;
        chopBank.hits[0].sample.mode = rolla::SampleMode::Sample;
        chopBank.hits[0].sample.filter.on = true;
        chopBank.hits[0].sample.filter.lane = rolla::Hits;
        chopBank.hits[0].decayMs = 5000.f;
        rolla::Sampler chopSampler;
        chopSampler.prepare (48000.0);
        rolla::EngineParams chopParams;
        chopParams.presets = &chopBank;
        chopParams.library = &chopLib;
        chopParams.sampleIndex = chopSnap.get();
        chopParams.dialect = rolla::Gaskin;
        chopParams.bpm = 130.f;
        chopParams.samplePick[rolla::Hits] = 0;
        chopSampler.take (rolla::Hits, 60, 110, 1.0e9f, chopBank, chopParams);
        expect (waitUntil ([&] { return chopLib.ready ("HITS/Chord_Stab_C4.wav"); }), "sliced hit loads");
        const auto played = chopSampler.take (rolla::Hits, 60, 110, 1.0e9f, chopBank, chopParams);
        expect (played.kind == 1 && std::fabs (played.synthGain) < 1.0e-6f, "the sliced hit replaces the synth");
        std::vector<float> rendered (12000);
        float prev16 = 0.f;
        float pos16 = 0.01f;
        for (int i = 0; i < (int) rendered.size(); ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            chopSampler.render (laneL, laneR, prev16, pos16, true, 0.f);
            rendered[(size_t) i] = laneL[rolla::Hits];
            prev16 = pos16;
            pos16 += 0.0001f;
        }
        float slicePeak = 0.f;
        float jump = 0.f;
        float tailPeak = 0.f;
        for (int i = 0; i < (int) rendered.size(); ++i)
        {
            slicePeak = std::max (slicePeak, std::fabs (rendered[(size_t) i]));
            if (i > 0)
                jump = std::max (jump, std::fabs (rendered[(size_t) i] - rendered[(size_t) (i - 1)]));
            if (i >= 9000)
                tailPeak = std::max (tailPeak, std::fabs (rendered[(size_t) i]));
        }
        expect (std::fabs (rendered[0]) < 1.0e-4f, "playback starts at silence");
        expect (slicePeak > 0.001f, "the sliced hit is audible");
        expect (jump < slicePeak * 0.5f, "playback has no click at either end of the slice");
        expect (tailPeak < slicePeak * 0.05f, "the voice stops when the slice ends");
        juce::MemoryBlock after;
        hitFile.loadFileAsData (after);
        expect (before.getSize() == after.getSize()
                    && std::memcmp (before.getData(), after.getData(), before.getSize()) == 0,
                "the source wav is unchanged");
    }

    // ---------------------------------------------------------------- vox starts on the voice
    {
        constexpr int rate = 48000;
        // 0.5 s near-silence, 0.4 s voice, 0.6 s near-silence, 0.4 s voice, 0.3 s near-silence.
        std::vector<float> vox;
        std::mt19937 noise (5);
        std::uniform_real_distribution<float> u (-1.f, 1.f);
        auto quiet = [&] (double sec) { for (int i = 0; i < (int) (sec * rate); ++i) vox.push_back (0.001f * u (noise)); };
        auto voice = [&] (double sec, float hz)
        {
            for (int i = 0; i < (int) (sec * rate); ++i)
                vox.push_back (0.5f * std::sin (2.f * 3.14159265f * hz * (float) i / (float) rate));
        };
        quiet (0.5);
        const int v1 = (int) vox.size();
        voice (0.4, 330.f);
        quiet (0.6);
        const int v2 = (int) vox.size();
        voice (0.4, 392.f);
        quiet (0.3);
        const int frames = (int) vox.size();
        const int cand[] = { 0, (int) (0.45 * rate), (int) (0.95 * rate), v2, frames - 100 };
        int voiced[24];
        float floorRms = 0.f;
        const int loud = rolla::loudestRegionStart (vox.data(), frames, rate);
        const int n = rolla::voicedOnsets (vox.data(), nullptr, frames, rate, cand, 5, loud, voiced, 24, floorRms);
        const int near = (int) (0.04 * rate);
        bool onVoice = n >= 2;
        bool hitV1 = false, hitV2 = false;
        for (int i = 0; i < n; ++i)
        {
            const bool inV1 = voiced[i] >= v1 - near && voiced[i] < v1 + (int) (0.4 * rate);
            const bool inV2 = voiced[i] >= v2 - near && voiced[i] < v2 + (int) (0.4 * rate);
            onVoice = onVoice && (inV1 || inV2);
            hitV1 = hitV1 || std::abs (voiced[i] - v1) <= near;
            hitV2 = hitV2 || std::abs (voiced[i] - v2) <= near;
        }
        expect (onVoice && hitV1 && hitV2, "vox starts sit on the voice, not on the lead-in, the gap or the tail ("
                                               + std::to_string (n) + " starts)");
        const int fromZero = rolla::voxStart (0, false, 0, voiced, n, floorRms, vox.data(), nullptr, frames, rate);
        expect (std::abs (fromZero - v1) <= near, "an unsliced vox note that opens on silence moves to the voice");
        const int inVoice = v1 + rate / 10;
        expect (rolla::voxStart (inVoice, false, 0, voiced, n, floorRms, vox.data(), nullptr, frames, rate) == inVoice,
                "a vox note that already starts on the voice stays put");
        bool rotates = true;
        for (int k = 0; k < 2 * n; ++k)
            rotates = rotates && rolla::voxStart (0, true, k, voiced, n, floorRms, vox.data(), nullptr, frames, rate) == voiced[k % n];
        expect (rotates, "sliced vox rotates through the voiced starts only");

        // End to end: a dropped vox file with a half-second lead-in sounds at once.
        const auto voxFile = rootDir.getChildFile ("VOX/lead_in.wav");
        voxFile.getParentDirectory().createDirectory();
        {
            juce::WavAudioFormat wav;
            auto fileStream = std::make_unique<juce::FileOutputStream> (voxFile);
            std::unique_ptr<juce::OutputStream> stream (fileStream.release());
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions().withSampleRate (rate).withNumChannels (1).withBitsPerSample (16));
            juce::AudioBuffer<float> buf (1, frames);
            for (int i = 0; i < frames; ++i)
                buf.setSample (0, i, vox[(size_t) i]);
            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (buf, 0, frames);
        }
        const auto voxSum = checksum (voxFile);
        rolla::UserSlots voxSlots;
        voxSlots.setRoot (rootDir);
        juce::String voxError;
        expect (voxSlots.assign (rolla::SlotVox, voxFile, voxError), "assign vox: " + voxError.toStdString());
        expect (waitUntil ([&] { return voxSlots.status (rolla::SlotVox) == 2; }), "vox slot loads");
        rolla::PresetBank voxBank;
        rolla::loadDefaultPresets (voxBank);
        rolla::Sampler voxSampler;
        voxSampler.prepare ((double) rate);
        rolla::EngineParams voxParams;
        voxParams.presets = &voxBank;
        voxParams.userSlots = voxSlots.published();
        for (int i = 0; i < rolla::NumDrumVoices; ++i)
            voxParams.drumDecay[i] = 0.5f;
        const auto voxTaken = voxSampler.take (rolla::Vox, 64, 110, 1.0e9f, voxBank, voxParams);
        std::vector<float> heard ((size_t) rate);
        float voxPeak = 0.f;
        for (int i = 0; i < rate; ++i)
        {
            float laneL[rolla::NumParts] {};
            float laneR[rolla::NumParts] {};
            voxSampler.render (laneL, laneR, 0.f, 0.f, false);
            heard[(size_t) i] = laneL[rolla::Vox];
            voxPeak = std::max (voxPeak, std::fabs (laneL[rolla::Vox]));
        }
        int first = -1;
        for (int i = 0; i < rate && first < 0 && voxPeak > 1.0e-4f; ++i)
            if (std::fabs (heard[(size_t) i]) > 0.1f * voxPeak)
                first = i;
        expect (voxTaken.kind != 0 && first >= 0 && first < (int) (0.05 * rate),
                "a vox chop with a lead-in sounds within 50 ms (first sound at sample " + std::to_string (first) + ")");
        expect (checksum (voxFile) == voxSum, "the vox source file is unchanged");
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
