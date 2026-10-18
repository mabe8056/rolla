#include "DspUtil.h"
#include "Engine/Engine.h"
#include "Generator/Generator.h"
#include "Generator/Intensity.h"
#include "Generator/Presets.h"
#include "Mix/MixProgram.h"
#include "Mix/MixRuntime.h"
#include "../tests/common/ScratchProfile.h"

#include <juce_dsp/juce_dsp.h>

#include <BinaryData.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
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

rolla::EngineParams makeParams (const rolla::PresetBank& bank, const rolla::MixProgram& mix, int dialect, bool bypassSum,
                              rolla::SampleLibrary* library, const rolla::SampleIndex* index)
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
    params.library = library;
    params.sampleIndex = index;
    params.bpm = (float) kBpm;
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

Take renderTake (const rolla::Groove& groove, const rolla::EngineParams& params, int bars, float* tpGuardGrDb = nullptr)
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
    float gr = 0.f;
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
        // takeTpGuardGrDb clears the block peak, so the render peak is the max of the blocks.
        gr = std::max (gr, engine.takeTpGuardGrDb());
        ppq += (double) n * kBpm / (60.0 * kSr);
        done += n;
    }
    if (tpGuardGrDb != nullptr)
        *tpGuardGrDb = gr;
    return take;
}

// MIX_SPEC §9.4b. mix_check.py gates tp_guard_gr_db_max at <= 1 dB when the SUM sidecar carries it.
bool writeSumGrSidecar (const juce::File& wav, float grDb)
{
    char buf[80];
    std::snprintf (buf, sizeof buf, "{\"tp_guard_gr_db_max\":%.4f}\n", (double) grDb);
    return wav.getSiblingFile (wav.getFileName() + ".gr.json").replaceWithText (buf);
}

// 4× true-peak estimate of the linear sum. The mix checker uses scipy's
// resample_poly, so the ceiling target sits under -6 dBTP with margin.
float truePeakLinear (const Take& take)
{
    const int n = (int) take.l.size();
    if (n <= 0)
        return 0.f;
    juce::dsp::Oversampling<float> os (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
    os.initProcessing ((size_t) n);
    juce::AudioBuffer<float> buf (2, n);
    buf.copyFrom (0, 0, take.l.data(), n);
    buf.copyFrom (1, 0, take.r.data(), n);
    juce::dsp::AudioBlock<float> block (buf);
    const auto up = os.processSamplesUp (block);
    float peak = 0.f;
    for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
    {
        const float* p = up.getChannelPointer (ch);
        for (size_t i = 0; i < up.getNumSamples(); ++i)
            peak = std::max (peak, std::fabs (p[i]));
    }
    return peak;
}

// One linear gain for FULL, SOLO, and MUTE. Already-legal sums stay at unity
// so a calibrated sample mix keeps its short-term level. The gain is sized
// from the hottest INTENSITY of this groove: the slider only removes or
// rearranges lanes, and the raw phrase is the all-lanes case.
float sumCeilingGain (float truePeakLin)
{
    const float limit = std::pow (10.f, -6.0f / 20.f);
    const float target = std::pow (10.f, -6.6f / 20.f);
    if (truePeakLin <= limit)
        return 1.f;
    return target / truePeakLin;
}

void scaleTake (Take& take, float gain)
{
    if (gain >= 0.9999f)
        return;
    for (size_t i = 0; i < take.l.size(); ++i)
    {
        take.l[i] *= gain;
        take.r[i] *= gain;
    }
}

float hottestIntensityPeak (const rolla::Groove& groove, const rolla::EngineParams& params, int dialect, float fullPeak)
{
    float worst = fullPeak;
    if (fullPeak < 1.0e-4f)
        return worst;
    const int levels[] = { 0, 25, 50, 75, 100 };
    for (int level : levels)
    {
        const auto plan = rolla::planIntensity (dialect, level, 0.55f);
        rolla::Groove arranged;
        rolla::applyIntensity (groove, arranged, plan, 0, 0);
        worst = std::max (worst, truePeakLinear (renderTake (arranged, params, kBars)));
    }
    return worst;
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

int selfTest (const rolla::PresetBank& bank, const rolla::MixProgram& mix,
              rolla::SampleLibrary* library, const rolla::SampleIndex* index)
{
    const int dialect = rolla::UkMin;
    const auto groove = makeGroove (bank, dialect);
    auto fullP = makeParams (bank, mix, dialect, false, library, index);
    // The guard is nonlinear. Mute-null compares FULL, SOLO, and MUTE, so it stays off here.
    fullP.tpGuard = false;
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
    Take hot;
    hot.l.resize (4800);
    hot.r.resize (4800);
    for (int i = 0; i < 4800; ++i)
    {
        const float s = 0.9f * std::sin (2.f * 3.14159265f * 1000.f * (float) i / (float) kSr);
        hot.l[(size_t) i] = s;
        hot.r[(size_t) i] = s * 0.5f;
    }
    Take solo = hot;
    for (auto& s : solo.r)
        s = 0.f;
    Take mute;
    mute.l.resize (hot.l.size());
    mute.r.resize (hot.r.size());
    for (size_t i = 0; i < hot.l.size(); ++i)
    {
        mute.l[i] = hot.l[i] - solo.l[i];
        mute.r[i] = hot.r[i] - solo.r[i];
    }
    const float gain = sumCeilingGain (truePeakLinear (hot));
    scaleTake (hot, gain);
    scaleTake (solo, gain);
    scaleTake (mute, gain);
    const float capped = truePeakLinear (hot);
    const float limit = std::pow (10.f, -6.0f / 20.f);
    std::cout << "sum ceiling gain " << gain << " peak " << capped << "\n";
    if (gain >= 0.9999f || capped > limit)
    {
        std::cerr << "FAIL summed true peak stayed above -6 dBTP\n";
        rc = 1;
    }
    const double residual = rmsDb (mute, hot, solo);
    if (residual > -90.0)
    {
        std::cerr << "FAIL sum ceiling broke the mute null\n";
        rc = 1;
    }
    Take quiet;
    quiet.l.assign (1024, 0.05f);
    quiet.r.assign (1024, 0.05f);
    if (sumCeilingGain (truePeakLinear (quiet)) < 0.9999f)
    {
        std::cerr << "FAIL a quiet sum was turned down\n";
        rc = 1;
    }

    // K-weighting calibration (BS.1770): a 997 Hz 0 dBFS sine reads 0 LUFS in
    // both channels and -3.01 LUFS in one, so lane level agrees with the scorer.
    auto kLufs = [] (float gainL, float gainR, float hz)
    {
        rolla::KWeight kl, kr;
        kl.prepare (kSr);
        kr.prepare (kSr);
        double sum = 0.0;
        int n = 0;
        for (int i = 0; i < (int) (3.0 * kSr); ++i)
        {
            const double x = std::sin (2.0 * 3.14159265358979323846 * hz * (double) i / kSr);
            const double a = kl.process (x * gainL);
            const double b = kr.process (x * gainR);
            if (i >= (int) (0.5 * kSr))
            {
                sum += a * a + b * b;
                ++n;
            }
        }
        return -0.691 + 10.0 * std::log10 (sum / (double) std::max (1, n) + 1.0e-30);
    };
    const double both = kLufs (1.f, 1.f, 997.f);
    const double one = kLufs (1.f, 0.f, 997.f);
    std::cout << "K-weight 997 Hz 0 dBFS: both " << both << " LUFS, one channel " << one << " LUFS\n";
    if (std::fabs (both) > 0.05 || std::fabs (one + 3.01) > 0.05)
    {
        std::cerr << "FAIL K-weighting is off BS.1770\n";
        rc = 1;
    }

    // Lane level: a steady tone about 4.5 dB under a lane's target ends on the
    // target (±1 dB); with lane level off it stays at least 2 dB under.
    {
        constexpr int lane = 1; // STAB
        const auto& chain = mix.chain[dialect][lane];
        auto settle = [&] (bool on, float amp)
        {
            rolla::MixRuntime rt;
            rt.prepare (kSr);
            rolla::MixBlockInfo info;
            info.dialect = dialect;
            info.sumMask = 63;
            info.bypassSumTrim = true;
            info.laneLevel = on;
            for (int p = 0; p < rolla::kMixMelodic; ++p)
            {
                info.presetIndex[p] = bank.dialect[dialect].presetIndex[p];
                info.presetId[p] = bank.dialect[dialect].presetId[p];
            }
            rt.beginBlock (mix, info);
            rolla::KWeight kl, kr;
            kl.prepare (kSr);
            kr.prepare (kSr);
            const int n = (int) (12.0 * kSr);
            double sum = 0.0;
            int count = 0;
            for (int i = 0; i < n; ++i)
            {
                float l = amp * (float) std::sin (2.0 * 3.14159265358979323846 * 440.0 * (double) i / kSr);
                float r = l;
                rt.process (lane, l, r);
                const double a = kl.process (l);
                const double b = kr.process (r);
                if (i >= n - (int) (3.0 * kSr))
                {
                    sum += a * a + b * b;
                    ++count;
                }
            }
            return -0.691 + 10.0 * std::log10 (sum / (double) std::max (1, count) + 1.0e-30);
        };
        if (chain.valid)
        {
            // Find an input level that lands 4.5 dB under the target with lane level off.
            // The strip's compressor is not linear, so step toward it a few times.
            const double goal = chain.stTargetLufs - 4.5;
            float amp = 0.05f;
            double before = settle (false, amp);
            for (int step = 0; step < 6 && std::fabs (before - goal) > 0.25; ++step)
            {
                amp *= (float) std::pow (10.0, (goal - before) / 20.0);
                before = settle (false, amp);
            }
            const double after = settle (true, amp);
            std::cout << "lane level STAB: target " << chain.stTargetLufs << " LUFS, off " << before << ", on " << after << "\n";
            if (std::fabs (after - chain.stTargetLufs) > 1.0 || before > chain.stTargetLufs - 2.0)
            {
                std::cerr << "FAIL lane level does not land a quiet lane on its target\n";
                rc = 1;
            }
        }
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

int renderSet (const rolla::PresetBank& bank, const rolla::MixProgram& mix, const juce::File& dir, bool solosOnly, bool bypassSum, int onlyDialect,
              rolla::SampleLibrary* library, const rolla::SampleIndex* index)
{
    // Loudness windows are sample-pack levels. A missing pack stays silent
    // (no synth fill) and the checker skips those windows. The -6 dBTP ceiling
    // is still applied, and still measured, with or without the pack.
    if (library == nullptr || index == nullptr)
    {
        dir.createDirectory();
        dir.getChildFile ("no_sample_pack").replaceWithText ("1\n");
    }
    for (int d = 0; d < rolla::NumDialects; ++d)
    {
        if (onlyDialect >= 0 && d != onlyDialect)
            continue;
        const auto groove = makeGroove (bank, d);
        auto base = makeParams (bank, mix, d, bypassSum, library, index);
        const auto folder = dir.getChildFile (kStyle[d]);
        float tpGuardGrDb = 0.f;
        auto full = renderTake (groove, base, kBars, &tpGuardGrDb);
        const float fullPeak = truePeakLinear (full);
        const float worst = hottestIntensityPeak (groove, base, d, fullPeak);
        const float gain = sumCeilingGain (worst);
        scaleTake (full, gain);
        const auto fullWav = folder.getChildFile ("FULL.wav");
        if (! writeWav (fullWav, full))
            return fail ("could not write FULL");
        if (! writeSumGrSidecar (fullWav, tpGuardGrDb))
            return fail ("could not write FULL.wav.gr.json");
        std::cout << kStyle[d] << " FULL peak " << fullPeak << " ceiling " << gain << "\n";
        for (const auto& lane : kLanes)
        {
            auto soloP = base;
            applyLane (soloP, lane, true);
            auto solo = renderTake (groove, soloP, kBars);
            scaleTake (solo, gain);
            if (! writeWav (folder.getChildFile (juce::String ("SOLO_") + lane.name + ".wav"), solo))
                return fail (std::string ("could not write SOLO_") + lane.name);
            std::cout << kStyle[d] << " SOLO_" << lane.name << "\n";
            if (solosOnly)
                continue;
            auto muteP = base;
            applyLane (muteP, lane, false);
            auto muted = renderTake (groove, muteP, kBars);
            scaleTake (muted, gain);
            if (! writeWav (folder.getChildFile (juce::String ("MUTE_") + lane.name + ".wav"), muted))
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
    // Never the real user profile: settings, caches and ~/Music/ROLLA/Samples go
    // to a scratch folder. `--samples DIR` renders with that pack, read only.
    if (rolla::test::useScratchProfile ("mix", true).empty() || ! rolla::test::useSamplesArg (argc, argv))
        return fail ("could not set up a scratch profile");
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
        else if (a == "--samples" && i + 1 < argc)
            ++i; // read by useSamplesArg above
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
            std::cout << "rolla-mix-render [--self-test] [--solos] [--bypass-sum] [--makeup file] [--dialect NAME] [--samples DIR] <outDir>\n"
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

    const auto pack = rolla::openInstalledPack (BinaryData::SAMPLE_MANIFEST_json, BinaryData::SAMPLE_MANIFEST_jsonSize);
    if (! (pack.library != nullptr && pack.index != nullptr))
        std::cout << "no sample pack; mix renders stay silent\n";
    if (doSelf)
        return selfTest (bank, mix, pack.library.get(), pack.index.get());
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
    return renderSet (bank, mix, outDir, solosOnly, bypassSum, onlyDialect, pack.library.get(), pack.index.get());
}
