#include "DspUtil.h"
#include "Engine/Engine.h"
#include "Generator/Generator.h"
#include "Generator/Presets.h"
#include "Mix/MixProgram.h"
#include "Mix/MixRuntime.h"

#include <BinaryData.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{

constexpr double kSr = 48000.0;
constexpr double kBpm = 126.0;
constexpr int kBars = 8;
constexpr int kBlock = 256;

const char* kStyle[rolla::NumDialects] = { "UKMIN", "LONG", "POP", "GASKIN" };

struct LaneId
{
    const char* name;
    int part;
    int voice;
};

// MIX_SPEC §5.1 order.
const LaneId kLanes[] = {
    { "KICK", rolla::Drums, rolla::Kick },
    { "CLAP", rolla::Drums, rolla::Clap },
    { "SNARE", rolla::Drums, rolla::Snare },
    { "RIM", rolla::Drums, rolla::Rim },
    { "CH", rolla::Drums, rolla::HatC },
    { "OH", rolla::Drums, rolla::HatO },
    { "RIDE", rolla::Drums, rolla::Ride },
    { "PERC", rolla::Drums, rolla::Perc },
    { "BASS", rolla::Bass, -1 },
    { "STAB", rolla::Stab, -1 },
    { "PLUCK", rolla::Pluck, -1 },
    { "HITS", rolla::Hits, -1 },
    { "PAD", rolla::Pad, -1 },
    { "VOX", rolla::Vox, -1 },
};

int fail (const std::string& msg)
{
    std::cerr << "FAIL " << msg << "\n";
    return 1;
}

bool loadProgram (rolla::MixProgram& program, const juce::File& makeupFile)
{
    juce::String makeupText;
    const char* makeup = BinaryData::mix_makeup_json;
    int makeupBytes = BinaryData::mix_makeup_jsonSize;
    if (makeupFile.existsAsFile())
    {
        makeupText = makeupFile.loadFileAsString();
        makeup = makeupText.toRawUTF8();
        makeupBytes = makeupText.getNumBytesAsUTF8();
    }
    return program.loadFromMemory (BinaryData::mix_profiles_json, BinaryData::mix_profiles_jsonSize,
                                   BinaryData::main_sum_trim_json, BinaryData::main_sum_trim_jsonSize,
                                   makeup, makeupBytes);
}

rolla::EngineParams makeParams (const rolla::PresetBank& bank, const rolla::MixProgram& mix, int dialect, bool bypassSum)
{
    rolla::EngineParams params;
    const auto& row = bank.dialect[dialect];
    for (int i = 0; i < rolla::NumParts; ++i)
    {
        params.tone[i] = 0.5f;
        params.punch[i] = 0.5f;
        params.levelDb[i] = 0.f;
        params.mute[i] = false;
        params.preset[i] = row.presetIndex[i] >= 0 ? row.presetIndex[i] : 0;
    }
    params.duck = true;
    params.drumsToMain = true;
    params.swing = row.swing;
    params.outputDb = 0.f;
    params.drumsAccent = 0.6f;
    params.bars = kBars;
    params.key = 2;
    params.dialect = dialect;
    params.presets = &bank;
    params.mix = &mix;
    // FULL unmutes every MAIN lane, so the frozen mask is all six bits.
    params.sumMask = 63;
    params.bypassSumTrim = bypassSum;
    return params;
}

void applyLane (rolla::EngineParams& params, const LaneId& lane, bool solo)
{
    for (int i = 0; i < rolla::NumParts; ++i)
        params.mute[i] = false;
    for (int v = 0; v < rolla::NumDrumVoices; ++v)
        params.drumVoiceMute[v] = false;
    if (solo)
    {
        for (int i = 0; i < rolla::NumParts; ++i)
            params.mute[i] = true;
        if (lane.part == rolla::Drums)
        {
            params.mute[rolla::Drums] = false;
            for (int v = 0; v < rolla::NumDrumVoices; ++v)
                params.drumVoiceMute[v] = true;
            if (lane.voice >= 0)
                params.drumVoiceMute[lane.voice] = false;
        }
        else
            params.mute[lane.part] = false;
    }
    else if (lane.part == rolla::Drums)
    {
        if (lane.voice >= 0)
            params.drumVoiceMute[lane.voice] = true;
    }
    else
        params.mute[lane.part] = true;
}

struct Take
{
    std::vector<float> l, r;
};

Take renderTake (const rolla::Groove& groove, const rolla::EngineParams& params, int bars)
{
    const double beats = bars * 4.0;
    const int total = (int) std::llround (beats * 60.0 / kBpm * kSr);
    Take take;
    take.l.assign ((size_t) total, 0.f);
    take.r.assign ((size_t) total, 0.f);
    rolla::Engine engine;
    engine.prepare (kSr, kBlock);
    int done = 0;
    double ppq = 0.0;
    while (done < total)
    {
        const int n = std::min (kBlock, total - done);
        rolla::Transport transport;
        transport.playing = true;
        transport.bpm = kBpm;
        transport.ppqStart = ppq;
        transport.sampleRate = kSr;
        juce::MidiBuffer midi;
        engine.process (groove, params, transport, take.l.data() + done, take.r.data() + done, nullptr, nullptr, n, midi, true);
        ppq += (double) n * kBpm / (60.0 * kSr);
        done += n;
    }
    return take;
}

bool writeWav (const juce::File& file, const Take& take)
{
    file.getParentDirectory().createDirectory();
    if (file.existsAsFile())
        file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate (kSr)
                       .withNumChannels (2)
                       .withBitsPerSample (32);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return false;
    juce::AudioBuffer<float> audio (2, (int) take.l.size());
    audio.copyFrom (0, 0, take.l.data(), (int) take.l.size());
    audio.copyFrom (1, 0, take.r.data(), (int) take.r.size());
    writer->writeFromAudioSampleBuffer (audio, 0, (int) take.l.size());
    return true;
}

rolla::Groove makeGroove (const rolla::PresetBank& bank, int dialect)
{
    rolla::Settings settings;
    settings.dark = 0.55f;
    settings.busy = 0.40f;
    settings.bounce = 0.45f;
    settings.stab = 0.55f;
    settings.dialect = dialect;
    settings.key = 2;
    settings.bars = kBars;
    settings.fill = true;
    const uint32_t seeds[rolla::NumParts] = { 0x0B17u, 0x51ABu, 0x19C3u, 0x77E1u, 0x0A11u, 0xBEEFu, 0x0909u };
    rolla::Groove groove {};
    rolla::generate (groove, settings, seeds, 0x7fu);
    juce::ignoreUnused (bank);
    return groove;
}

double rmsDb (const Take& a, const Take& b, const Take& c)
{
    // residual = a - (b - c). a = MUTE, b = FULL, c = SOLO.
    double acc = 0.0;
    const size_t n = std::min (a.l.size(), std::min (b.l.size(), c.l.size()));
    for (size_t i = 0; i < n; ++i)
    {
        const double l = (double) a.l[i] - ((double) b.l[i] - (double) c.l[i]);
        const double r = (double) a.r[i] - ((double) b.r[i] - (double) c.r[i]);
        acc += l * l + r * r;
    }
    const double mean = acc / (double) std::max<size_t> (1, n * 2);
    return 10.0 * std::log10 (std::max (mean, 1.0e-20));
}

double bandRmsDb (const Take& a, const Take& b, const Take& c)
{
    // 250 Hz–4 kHz energy of the residual. Two SVF stages stand in for the
    // checker’s Butterworth; a real leak in this band is tens of dB above -90.
    rolla::Svf hpL, hpR, lpL, lpR;
    double acc = 0.0;
    const size_t n = std::min (a.l.size(), std::min (b.l.size(), c.l.size()));
    const float sr = (float) kSr;
    for (size_t i = 0; i < n; ++i)
    {
        float l = (float) ((double) a.l[i] - ((double) b.l[i] - (double) c.l[i]));
        float r = (float) ((double) a.r[i] - ((double) b.r[i] - (double) c.r[i]));
        l = hpL.highpass (l, 250.f, 0.707f, sr);
        r = hpR.highpass (r, 250.f, 0.707f, sr);
        l = lpL.lowpass (l, 4000.f, 0.707f, sr);
        r = lpR.lowpass (r, 4000.f, 0.707f, sr);
        acc += (double) l * (double) l + (double) r * (double) r;
    }
    const double mean = acc / (double) std::max<size_t> (1, n * 2);
    return 10.0 * std::log10 (std::max (mean, 1.0e-20));
}

int selfTest (const rolla::PresetBank& bank, const rolla::MixProgram& mix)
{
    const int dialect = rolla::UkMin;
    const auto groove = makeGroove (bank, dialect);
    auto fullP = makeParams (bank, mix, dialect, false);
    const auto full = renderTake (groove, fullP, 4);
    const LaneId probe[] = { kLanes[8], kLanes[0], kLanes[1] }; // BASS, KICK, CLAP
    int rc = 0;
    for (const auto& lane : probe)
    {
        auto muteP = fullP;
        auto soloP = fullP;
        applyLane (muteP, lane, false);
        applyLane (soloP, lane, true);
        const auto muted = renderTake (groove, muteP, 4);
        const auto solo = renderTake (groove, soloP, 4);
        const double broad = rmsDb (muted, full, solo);
        const double band = bandRmsDb (muted, full, solo);
        std::cout << "mute-null " << lane.name << " residual " << broad << " dBFS  250-4k " << band << " dBFS\n";
        if (broad > -90.0 || band > -90.0)
        {
            std::cerr << "FAIL mute leak on " << lane.name << "\n";
            rc = 1;
        }
    }
    auto silentP = fullP;
    silentP.mute[rolla::Bass] = true;
    for (int i = 0; i < rolla::NumParts; ++i)
        silentP.mute[i] = true;
    const auto silent = renderTake (groove, silentP, 2);
    float peak = 0.f;
    for (size_t i = 0; i < silent.l.size(); ++i)
        peak = std::max (peak, std::max (std::fabs (silent.l[i]), std::fabs (silent.r[i])));
    std::cout << "all-muted peak " << peak << "\n";
    if (peak > 1.0e-7f)
    {
        std::cerr << "FAIL muted render is not silent\n";
        rc = 1;
    }
    if (rc == 0)
        std::cout << "self-test pass\n";
    return rc;
}

int renderSwitch (const rolla::PresetBank& bank, const rolla::MixProgram& mix, const juce::File& stim,
                  const juce::File& out, int fromDialect, int toDialect, double atSec, int lane)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (stim));
    if (reader == nullptr)
        return fail ("could not read stimulus");
    if (std::abs (reader->sampleRate - kSr) > 1.0)
        return fail ("stimulus must be 48 kHz");
    const int n = (int) reader->lengthInSamples;
    if (n < (int) (1.5 * kSr))
        return fail ("stimulus shorter than 1.5 s");
    juce::AudioBuffer<float> in (2, n);
    reader->read (&in, 0, n, 0, true, true);
    if (reader->numChannels < 2)
        in.copyFrom (1, 0, in, 0, 0, n);

    rolla::MixRuntime rt;
    rt.prepare (kSr);
    auto infoFor = [&] (int dialect)
    {
        rolla::MixBlockInfo info;
        info.dialect = dialect;
        info.sumMask = 63;
        info.bypassSumTrim = true;
        info.activeMask = 63;
        for (int p = 0; p < rolla::kMixMelodic; ++p)
        {
            info.presetIndex[p] = bank.dialect[dialect].presetIndex[p];
            info.presetId[p] = bank.dialect[dialect].presetId[p];
        }
        return info;
    };
    rt.beginBlock (mix, infoFor (fromDialect));
    const int at = std::clamp ((int) std::llround (atSec * kSr), 0, n - 1);
    Take take;
    take.l.resize ((size_t) n);
    take.r.resize ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        if (i == at)
            rt.beginBlock (mix, infoFor (toDialect));
        float l = in.getSample (0, i);
        float r = in.getSample (1, i);
        rt.process (lane, l, r);
        take.l[(size_t) i] = l;
        take.r[(size_t) i] = r;
    }
    if (! writeWav (out, take))
        return fail ("could not write switch render");
    std::cout << "switch " << kStyle[fromDialect] << "->" << kStyle[toDialect]
              << " " << rolla::mixLaneName (lane) << " at " << atSec << "\n";
    return 0;
}

int renderSet (const rolla::PresetBank& bank, const rolla::MixProgram& mix, const juce::File& dir, bool solosOnly, bool bypassSum, int onlyDialect)
{
    for (int d = 0; d < rolla::NumDialects; ++d)
    {
        if (onlyDialect >= 0 && d != onlyDialect)
            continue;
        const auto groove = makeGroove (bank, d);
        auto base = makeParams (bank, mix, d, bypassSum);
        const auto folder = dir.getChildFile (kStyle[d]);
        const auto full = renderTake (groove, base, kBars);
        if (! writeWav (folder.getChildFile ("FULL.wav"), full))
            return fail ("could not write FULL");
        std::cout << kStyle[d] << " FULL\n";
        for (const auto& lane : kLanes)
        {
            auto soloP = base;
            applyLane (soloP, lane, true);
            if (! writeWav (folder.getChildFile (juce::String ("SOLO_") + lane.name + ".wav"), renderTake (groove, soloP, kBars)))
                return fail (std::string ("could not write SOLO_") + lane.name);
            std::cout << kStyle[d] << " SOLO_" << lane.name << "\n";
            if (solosOnly)
                continue;
            auto muteP = base;
            applyLane (muteP, lane, false);
            if (! writeWav (folder.getChildFile (juce::String ("MUTE_") + lane.name + ".wav"), renderTake (groove, muteP, kBars)))
                return fail (std::string ("could not write MUTE_") + lane.name);
            std::cout << kStyle[d] << " MUTE_" << lane.name << "\n";
        }
    }
    return 0;
}

} // namespace

int dialectIndex (const std::string& name)
{
    const char* names[] = { "UKMIN", "LONG", "POP", "GASKIN" };
    const juce::String n (name);
    for (int d = 0; d < 4; ++d)
        if (n.equalsIgnoreCase (names[d]))
            return d;
    return -1;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    bool doSelf = false;
    bool solosOnly = false;
    bool bypassSum = false;
    int onlyDialect = -1;
    bool doSwitch = false;
    int fromDialect = -1, toDialect = -1, switchLane = rolla::mixLaneIndex ("STAB");
    double switchAt = 1.0;
    juce::File outDir, makeup, stim;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--self-test")
            doSelf = true;
        else if (a == "--solos")
            solosOnly = true;
        else if (a == "--bypass-sum")
            bypassSum = true;
        else if (a == "--switch")
            doSwitch = true;
        else if (a == "--stim" && i + 1 < argc)
            stim = juce::File (argv[++i]);
        else if (a == "--from" && i + 1 < argc)
            fromDialect = dialectIndex (argv[++i]);
        else if (a == "--to" && i + 1 < argc)
            toDialect = dialectIndex (argv[++i]);
        else if (a == "--at" && i + 1 < argc)
            switchAt = std::atof (argv[++i]);
        else if (a == "--lane" && i + 1 < argc)
        {
            switchLane = rolla::mixLaneIndex (argv[++i]);
            if (switchLane < 0)
                return fail ("unknown lane");
        }
        else if (a == "--makeup" && i + 1 < argc)
            makeup = juce::File (argv[++i]);
        else if (a == "--dialect" && i + 1 < argc)
        {
            onlyDialect = dialectIndex (argv[++i]);
            if (onlyDialect < 0)
                return fail ("unknown dialect");
        }
        else if (a == "--help")
        {
            std::cout << "rolla-mix-render [--self-test] [--solos] [--bypass-sum] [--makeup file] [--dialect NAME] <outDir>\n"
                      << "rolla-mix-render --switch --stim in.wav --from UKMIN --to LONG --at 1.0 --lane STAB <out.wav>\n";
            return 0;
        }
        else
            outDir = juce::File (a);
    }

    rolla::PresetBank bank;
    if (! rolla::loadPresetsFromJson (bank, BinaryData::presets_json, BinaryData::presets_jsonSize))
        return fail ("presets.json");
    rolla::MixProgram mix;
    if (! loadProgram (mix, makeup))
        return fail ("mix profile");

    if (doSelf)
        return selfTest (bank, mix);
    if (doSwitch)
    {
        if (fromDialect < 0 || toDialect < 0)
            return fail ("switch needs --from and --to");
        if (stim == juce::File() || outDir == juce::File())
            return fail ("switch needs --stim and an output wav");
        return renderSwitch (bank, mix, stim, outDir, fromDialect, toDialect, switchAt, switchLane);
    }
    if (outDir == juce::File())
        return fail ("need an output directory or --self-test");
    return renderSet (bank, mix, outDir, solosOnly, bypassSum, onlyDialect);
}
