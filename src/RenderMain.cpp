#include "DspUtil.h"
#include "Engine/Engine.h"
#include "Generator/Generator.h"
#include "Generator/Presets.h"

#include <BinaryData.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{

int fail (const std::string& msg)
{
    std::cerr << "FAIL " << msg << "\n";
    return 1;
}

bool nearly (double a, double b)
{
    return std::fabs (a - b) < 1.0e-9;
}

int checkRng()
{
    const double expect[5] = {
        0.5256123431026936,
        0.7495628057513386,
        0.5401806251611561,
        0.4998694087844342,
        0.32606114144437015,
    };
    rolla::Rng rng (0x0B17u);
    for (int i = 0; i < 5; ++i)
    {
        const double v = rng.next();
        std::cout << "rng[" << i << "] " << v << "\n";
        if (! nearly (v, expect[i]))
            return fail ("mulberry32 diverged at index " + std::to_string (i));
    }
    return 0;
}

float offKickRatio (const rolla::Groove& g)
{
    int off = 0, on = 0;
    const auto& lane = g.lane[rolla::Bass];
    for (int i = 0; i < lane.count; ++i)
    {
        const int step = (int) std::floor (lane.notes[i].start + 1.0e-4f);
        if (step < 0 || step >= g.bars * 16)
            continue;
        if (g.drums.vel[rolla::Kick][step] > 0)
            ++on;
        else
            ++off;
    }
    const int n = on + off;
    return n == 0 ? 0.f : (float) off / (float) n;
}

float stabOnBeat (const rolla::Groove& g)
{
    int on = 0, n = 0;
    const auto& lane = g.lane[rolla::Stab];
    for (int i = 0; i < lane.count; ++i)
    {
        const int step = (int) std::floor (lane.notes[i].start + 1.0e-4f);
        ++n;
        if ((step % 4) == 0)
            ++on;
    }
    return n == 0 ? 0.f : (float) on / (float) n;
}

bool sameGroove (const rolla::Groove& a, const rolla::Groove& b)
{
    if (a.bars != b.bars || a.lane[rolla::Bass].count != b.lane[rolla::Bass].count)
        return false;
    for (int p = 0; p < rolla::NumParts; ++p)
    {
        if (a.lane[p].count != b.lane[p].count)
            return false;
        for (int i = 0; i < a.lane[p].count; ++i)
        {
            if (std::memcmp (&a.lane[p].notes[i].start, &b.lane[p].notes[i].start, sizeof (float)) != 0)
                return false;
            if (a.lane[p].notes[i].chord[0] != b.lane[p].notes[i].chord[0])
                return false;
        }
    }
    return std::memcmp (a.drums.vel, b.drums.vel, sizeof (a.drums.vel)) == 0;
}

struct RenderStats
{
    float peak = 0.f;
    double sum = 0.0;
    int samples = 0;
    int subnormals = 0;
    int nonFinite = 0;
    int bassOns = 0;
    double seconds = 0.0;
};

RenderStats renderDialect (const rolla::PresetBank& bank, int dialect, const juce::File& out,
                           rolla::SampleLibrary* library, const rolla::SampleIndex* index)
{
    constexpr double sr = 48000.0;
    constexpr double bpm = 130.0;
    constexpr int bars = 8;
    const double beats = bars * 4.0;
    const int total = (int) std::llround (beats * 60.0 / bpm * sr);

    rolla::Settings settings;
    settings.dark = 0.55f;
    settings.busy = 0.40f;
    settings.bounce = 0.45f;
    settings.stab = 0.55f;
    settings.dialect = dialect;
    settings.key = 2;
    settings.bars = bars;
    settings.fill = true;

    const uint32_t seeds[rolla::NumParts] = { 0x0B17u, 0x51ABu, 0x19C3u, 0x77E1u, 0x0A11u, 0xBEEFu, 0x0909u };
    rolla::Groove groove {};
    rolla::generate (groove, settings, seeds, 0x7fu);
    rolla::Groove again {};
    rolla::generate (again, settings, seeds, 0x7fu);

    std::cout << rolla::dialectId (dialect)
              << " bassNotes=" << groove.lane[rolla::Bass].count
              << " stabNotes=" << groove.lane[rolla::Stab].count
              << " offKick=" << offKickRatio (groove)
              << " stabOnBeat=" << stabOnBeat (groove)
              << " guard=" << groove.guardRef
              << " deterministic=" << (sameGroove (groove, again) ? "yes" : "NO")
              << "\n";

    rolla::Engine engine;
    engine.prepare (sr, 256);
    rolla::EngineParams params;
    for (int i = 0; i < rolla::NumParts; ++i)
    {
        params.tone[i] = 0.5f;
        params.punch[i] = 0.5f;
        params.levelDb[i] = 0.f;
        params.mute[i] = false;
        params.preset[i] = 0;
    }
    params.duck = true;
    params.drumsToMain = true;
    params.swing = 0.52f;
    params.outputDb = 0.f;
    params.drumsAccent = 0.6f;
    params.bars = bars;
    params.key = 2;
    params.dialect = dialect;
    params.presets = &bank;
    params.library = library;
    params.sampleIndex = index;
    params.bpm = (float) bpm;
    // Every lane's chosen file in memory before the first block (bounded, logged).
    if (library != nullptr && index != nullptr)
        rolla::prefetchPlayFiles (*library, bank, *index, params.preset, params.samplePick, dialect, 20000, "render");

    juce::AudioBuffer<float> audio (2, total);
    audio.clear();
    RenderStats stats;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    int done = 0;
    double ppq = 0.0;
    const int block = 256;
    int midiOn[16] {};
    while (done < total)
    {
        const int n = std::min (block, total - done);
        rolla::Transport transport;
        transport.playing = true;
        transport.bpm = bpm;
        transport.ppqStart = ppq;
        transport.sampleRate = sr;
        juce::MidiBuffer midi;
        engine.process (groove, params, transport,
                        audio.getWritePointer (0, done),
                        audio.getWritePointer (1, done),
                        nullptr, nullptr, n, midi, true);
        for (const auto meta : midi)
            if (meta.getMessage().isNoteOn())
            {
                const int ch = meta.getMessage().getChannel();
                if (ch >= 1 && ch <= 16)
                    ++midiOn[ch - 1];
            }
        ppq += (double) n * bpm / (60.0 * sr);
        done += n;
    }
    stats.seconds = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    std::cout << "midiOn";
    for (int ch = 0; ch < 16; ++ch)
        if (midiOn[ch] != 0)
            std::cout << " ch" << (ch + 1) << "=" << midiOn[ch];
    std::cout << "\n";
    stats.bassOns = midiOn[0];

    for (int ch = 0; ch < 2; ++ch)
    {
        const float* data = audio.getReadPointer (ch);
        for (int i = 0; i < total; ++i)
        {
            const float x = data[i];
            if (! std::isfinite (x))
                ++stats.nonFinite;
            else if (std::fpclassify (x) == FP_SUBNORMAL)
                ++stats.subnormals;
            stats.peak = std::max (stats.peak, std::fabs (x));
            stats.sum += (double) x * (double) x;
            ++stats.samples;
        }
    }

    out.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (out.createOutputStream());
    if (stream != nullptr)
    {
        juce::AudioFormatWriterOptions options;
        options = options.withSampleRate (sr)
                      .withNumChannels (2)
                      .withBitsPerSample (24);
        if (auto writer = wav.createWriterFor (stream, options))
            writer->writeFromAudioSampleBuffer (audio, 0, total);
    }
    return stats;
}

struct Take
{
    std::vector<float> l, r;
};

Take renderTake (const rolla::PresetBank&, const rolla::Groove& groove, const rolla::EngineParams& params,
                 double seconds, juce::MidiBuffer midi, bool playing)
{
    constexpr double sr = 48000.0;
    constexpr double bpm = 130.0;
    const int total = std::max (1, (int) std::llround (seconds * sr));
    Take take;
    take.l.assign ((size_t) total, 0.f);
    take.r.assign ((size_t) total, 0.f);
    rolla::Engine engine;
    engine.prepare (sr, 256);
    int done = 0;
    double ppq = 0.0;
    bool first = true;
    while (done < total)
    {
        const int n = std::min (256, total - done);
        rolla::Transport transport;
        transport.playing = playing;
        transport.bpm = bpm;
        transport.ppqStart = ppq;
        transport.sampleRate = sr;
        juce::MidiBuffer block;
        if (first)
        {
            block.swapWith (midi);
            first = false;
        }
        engine.process (groove, params, transport,
                        take.l.data() + done, take.r.data() + done,
                        nullptr, nullptr, n, block, false);
        if (playing)
            ppq += (double) n * bpm / (60.0 * sr);
        done += n;
    }
    return take;
}

rolla::EngineParams baseParams (const rolla::PresetBank& bank, int bars)
{
    rolla::EngineParams params;
    for (int i = 0; i < rolla::NumParts; ++i)
    {
        params.tone[i] = 0.5f;
        params.punch[i] = 0.5f;
        params.levelDb[i] = 0.f;
        params.mute[i] = true;
        params.preset[i] = 0;
    }
    params.duck = true;
    params.drumsToMain = true;
    params.swing = 0.52f;
    params.outputDb = 0.f;
    params.drumsAccent = 0.6f;
    params.bars = bars;
    params.key = 2;
    params.dialect = rolla::UkMin;
    params.presets = &bank;
    return params;
}

float peakOf (const Take& t)
{
    float peak = 0.f;
    const int n = (int) t.l.size();
    for (int i = 0; i < n; ++i)
        peak = std::max (peak, std::max (std::fabs (t.l[(size_t) i]), std::fabs (t.r[(size_t) i])));
    return peak;
}

double rmsRange (const Take& t, int a, int b)
{
    a = std::max (0, a);
    b = std::min (b, (int) t.l.size());
    if (b <= a)
        return 0.0;
    double s = 0.0;
    for (int i = a; i < b; ++i)
    {
        const double l = t.l[(size_t) i];
        const double r = t.r[(size_t) i];
        s += l * l + r * r;
    }
    return std::sqrt (s / (double) (b - a) / 2.0);
}

// A hard cut is a short-window RMS that falls more than ~18 dB inside a fifth of a millisecond
// while the signal is still above −60 dB. Noise and filter sweeps do not do that; a stopped voice does.
int hardCuts (const Take& t)
{
    constexpr double sr = 48000.0;
    const int n = (int) t.l.size();
    const int win = std::max (8, (int) (0.001 * sr));
    const int hop = std::max (4, win / 6);
    int cuts = 0;
    double prev = 0.0;
    bool have = false;
    for (int i = 0; i + win < n; i += hop)
    {
        double s = 0.0;
        for (int k = 0; k < win; ++k)
        {
            const double l = t.l[(size_t) (i + k)];
            const double r = t.r[(size_t) (i + k)];
            s += l * l + r * r;
        }
        const double rms = std::sqrt (s / (double) win);
        if (have && prev > 0.001 && rms < prev * 0.12)
            ++cuts;
        prev = rms;
        have = true;
    }
    return cuts;
}

double correlation (const Take& t)
{
    double ll = 0, rr = 0, lr = 0;
    for (size_t i = 0; i < t.l.size(); ++i)
    {
        const double l = t.l[i];
        const double r = t.r[i];
        ll += l * l;
        rr += r * r;
        lr += l * r;
    }
    if (ll < 1.0e-12 || rr < 1.0e-12)
        return 1.0;
    return lr / std::sqrt (ll * rr);
}

double centroidHz (const Take& t)
{
    constexpr double sr = 48000.0;
    constexpr int order = 12;
    constexpr int N = 1 << order;
    if ((int) t.l.size() < N)
        return 0.0;
    juce::dsp::FFT fft (order);
    std::vector<float> block ((size_t) N * 2, 0.f);
    std::vector<double> mag ((size_t) (N / 2), 0.0);
    const int hop = N / 2;
    for (int pos = 0; pos + N <= (int) t.l.size(); pos += hop)
    {
        double energy = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f * (1.f - std::cos (2.f * 3.14159265358979323846f * (float) i / (float) (N - 1)));
            const float m = 0.5f * (t.l[(size_t) (pos + i)] + t.r[(size_t) (pos + i)]);
            block[(size_t) i] = m * w;
            energy += (double) m * (double) m;
        }
        if (energy < 1.0e-8)
            continue;
        std::fill (block.begin() + N, block.end(), 0.f);
        fft.performFrequencyOnlyForwardTransform (block.data());
        for (int k = 1; k < N / 2; ++k)
            mag[(size_t) k] += (double) block[(size_t) k];
    }
    double num = 0.0, den = 0.0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = (double) k * sr / (double) N;
        num += f * mag[(size_t) k];
        den += mag[(size_t) k];
    }
    return den > 0.0 ? num / den : 0.0;
}

double bandShare (const Take& t, double hzLo, double hzHi)
{
    constexpr double sr = 48000.0;
    constexpr int order = 12;
    constexpr int N = 1 << order;
    if ((int) t.l.size() < N)
        return 0.0;
    juce::dsp::FFT fft (order);
    std::vector<float> block ((size_t) N * 2, 0.f);
    double in = 0.0, all = 0.0;
    const int hop = N / 2;
    for (int pos = 0; pos + N <= (int) t.l.size(); pos += hop)
    {
        double energy = 0.0;
        for (int i = 0; i < N; ++i)
        {
            const float w = 0.5f * (1.f - std::cos (2.f * 3.14159265358979323846f * (float) i / (float) (N - 1)));
            const float m = 0.5f * (t.l[(size_t) (pos + i)] + t.r[(size_t) (pos + i)]);
            block[(size_t) i] = m * w;
            energy += (double) m * (double) m;
        }
        if (energy < 1.0e-8)
            continue;
        std::fill (block.begin() + N, block.end(), 0.f);
        fft.performFrequencyOnlyForwardTransform (block.data());
        for (int k = 1; k < N / 2; ++k)
        {
            const double f = (double) k * sr / (double) N;
            const double m = (double) block[(size_t) k];
            all += m;
            if (f >= hzLo && f < hzHi)
                in += m;
        }
    }
    return all > 0.0 ? in / all : 0.0;
}

// Milliseconds from the peak of the 20 ms RMS envelope until it first sits 10 dB down.
double tailToMinus10Ms (const Take& t)
{
    constexpr double sr = 48000.0;
    const int n = (int) t.l.size();
    const int win = std::max (8, (int) (0.020 * sr));
    const int hop = std::max (1, (int) (0.001 * sr));
    std::vector<double> env;
    env.reserve ((size_t) (n / hop + 2));
    for (int i = 0; i + win < n; i += hop)
    {
        double s = 0.0;
        for (int k = 0; k < win; ++k)
        {
            const double l = t.l[(size_t) (i + k)];
            const double r = t.r[(size_t) (i + k)];
            s += l * l + r * r;
        }
        env.push_back (std::sqrt (s / (double) win));
    }
    if (env.size() < 8)
        return 0.0;
    const int look = std::min ((int) env.size(), (int) (0.30 * sr / hop));
    size_t peakAt = 0;
    double peak = 0.0;
    for (int i = 0; i < look; ++i)
        if (env[(size_t) i] > peak)
        {
            peak = env[(size_t) i];
            peakAt = (size_t) i;
        }
    if (peak < 1.0e-6)
        return 0.0;
    const double line = peak * std::pow (10.0, -10.0 / 20.0);
    size_t last = peakAt;
    for (size_t i = peakAt; i < env.size(); ++i)
        if (env[i] >= line)
            last = i;
    return (double) (last - peakAt) * (double) hop / sr * 1000.0;
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;
    double process (double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// Integrated LUFS at 48 kHz (BS.1770 K-weight, absolute then relative gate).
double integratedLufs (const Take& t)
{
    constexpr double sr = 48000.0;
    Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585, 0, 0 };
    Biquad hp { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621, 0, 0 };
    Biquad shelfR = shelf, hpR = hp;
    const int n = (int) t.l.size();
    std::vector<double> ms;
    ms.reserve ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double l = hp.process (shelf.process (t.l[(size_t) i]));
        const double r = hpR.process (shelfR.process (t.r[(size_t) i]));
        ms.push_back (0.5 * (l * l + r * r));
    }
    const int block = (int) (0.4 * sr);
    const int hop = (int) (0.1 * sr);
    if (n < block)
        return -70.0;
    std::vector<double> blocks;
    for (int i = 0; i + block <= n; i += hop)
    {
        double s = 0.0;
        for (int k = 0; k < block; ++k)
            s += ms[(size_t) (i + k)];
        blocks.push_back (s / (double) block);
    }
    auto meanOf = [] (const std::vector<double>& v, double gate)
    {
        double s = 0.0;
        int c = 0;
        for (double x : v)
            if (x >= gate)
            {
                s += x;
                ++c;
            }
        return c == 0 ? 0.0 : s / (double) c;
    };
    const double absGate = std::pow (10.0, (-70.0 + 0.691) / 10.0);
    const double ungated = meanOf (blocks, absGate);
    if (ungated <= 0.0)
        return -70.0;
    const double relGate = ungated * std::pow (10.0, -10.0 / 10.0);
    const double gated = meanOf (blocks, std::max (absGate, relGate));
    if (gated <= 0.0)
        return -70.0;
    return -0.691 + 10.0 * std::log10 (gated);
}

std::vector<double> kWeightedPower (const Take& t)
{
    Biquad shelf { 1.53512485958697, -2.69169618940638, 1.19839281085285, -1.69065929318241, 0.73248077421585, 0, 0 };
    Biquad hp { 1.0, -2.0, 1.0, -1.99004745483398, 0.99007225036621, 0, 0 };
    Biquad shelfR = shelf, hpR = hp;
    std::vector<double> ms (t.l.size());
    for (size_t i = 0; i < t.l.size(); ++i)
    {
        const double l = hp.process (shelf.process (t.l[i]));
        const double r = hpR.process (shelfR.process (t.r[i]));
        ms[i] = 0.5 * (l * l + r * r);
    }
    return ms;
}

// Loudest 400 ms K-weighted block. Sparse hits are judged on the hit, not the gaps.
double momentaryMaxLufs (const std::vector<double>& ms)
{
    constexpr double sr = 48000.0;
    const int n = (int) ms.size();
    const int block = (int) (0.4 * sr);
    const int hop = (int) (0.1 * sr);
    if (n < block)
        return -70.0;
    double best = 0.0;
    for (int i = 0; i + block <= n; i += hop)
    {
        double s = 0.0;
        for (int k = 0; k < block; ++k)
            s += ms[(size_t) (i + k)];
        best = std::max (best, s / (double) block);
    }
    if (best <= 1.0e-20)
        return -70.0;
    return -0.691 + 10.0 * std::log10 (best);
}

int analyseSound (const rolla::PresetBank& bank)
{
    constexpr double sr = 48000.0;
    int failures = 0;
    auto failAr = [&] (const std::string& msg)
    {
        std::cerr << "FAIL " << msg << "\n";
        ++failures;
    };

    rolla::Settings settings;
    settings.dark = 0.55f;
    settings.busy = 0.40f;
    settings.bounce = 0.45f;
    settings.stab = 0.55f;
    settings.dialect = rolla::UkMin;
    settings.key = 2;
    settings.bars = 8;
    settings.fill = true;
    const uint32_t seeds[rolla::NumParts] = { 0x0B17u, 0x51ABu, 0x19C3u, 0x77E1u, 0x0A11u, 0xBEEFu, 0x0909u };
    rolla::Groove groove {};
    rolla::generate (groove, settings, seeds, 0x7fu);

    const double bars8 = 8.0 * 4.0 * 60.0 / 130.0;
    auto solo = [&] (int part)
    {
        auto params = baseParams (bank, 8);
        params.mute[part] = false;
        return renderTake (bank, groove, params, bars8, {}, true);
    };

    const Take bass = solo (rolla::Bass);
    const Take stab = solo (rolla::Stab);
    const Take hits = solo (rolla::Hits);
    const Take vox = solo (rolla::Vox);
    const double bassC = centroidHz (bass);
    const double stabC = centroidHz (stab);
    const double hitsC = centroidHz (hits);
    const double voxC = centroidHz (vox);
    const double bassLufs = integratedLufs (bass);
    const double hitsMom = momentaryMaxLufs (kWeightedPower (hits));
    const double under = bassLufs - hitsMom;
    const double bassUnder60 = bandShare (bass, 0.0, 60.0);
    std::cout << "lane bass centroid=" << bassC << " Hz under60=" << bassUnder60
              << " peak=" << peakOf (bass)
              << " lufs=" << bassLufs << " cuts=" << hardCuts (bass) << "\n";
    std::cout << "lane stab centroid=" << stabC << " Hz peak=" << peakOf (stab)
              << " corr=" << correlation (stab) << " tailMs=" << tailToMinus10Ms (stab)
              << " cuts=" << hardCuts (stab) << "\n";
    std::cout << "lane hits centroid=" << hitsC << " Hz peak=" << peakOf (hits)
              << " momentary=" << hitsMom << " underBassLU=" << under
              << " cuts=" << hardCuts (hits) << "\n";
    std::cout << "lane vox centroid=" << voxC << " Hz peak=" << peakOf (vox)
              << " cuts=" << hardCuts (vox) << "\n";

    if (bassC < 73.0 || bassC > 200.0)
        failAr ("bass centroid " + std::to_string (bassC) + " Hz is outside 73–200");
    if (bassUnder60 > 0.70)
        failAr ("bass energy under 60 Hz is " + std::to_string (bassUnder60) + ", above the 0.70 reference cap");
    if (hardCuts (bass) != 0)
        failAr ("bass hard cut");
    if (hardCuts (stab) != 0)
        failAr ("stab lane hard cut");
    if (hardCuts (hits) != 0)
        failAr ("hits hard cut");
    if (hardCuts (vox) != 0)
        failAr ("vox hard cut");
    if (under < 2.0 || under > 4.5)
        failAr ("hits loudness " + std::to_string (under) + " LU under bass, want about 3");

    rolla::Groove empty {};
    empty.bars = 2;
    auto stabParams = baseParams (bank, 2);
    stabParams.mute[rolla::Stab] = false;
    juce::MidiBuffer chord;
    chord.addEvent (juce::MidiMessage::noteOn (2, 57, (juce::uint8) 110), 0);
    const Take stabNote = renderTake (bank, empty, stabParams, 1.70, chord, true);
    const double noteC = centroidHz (stabNote);
    const double noteCorr = correlation (stabNote);
    const double noteTail = tailToMinus10Ms (stabNote);
    const float notePeak = peakOf (stabNote);
    std::cout << "stab chord centroid=" << noteC << " Hz tailMs=" << noteTail
              << " corr=" << noteCorr << " peak=" << notePeak
              << " cuts=" << hardCuts (stabNote) << "\n";
    if (noteC < 900.0 || noteC > 2500.0)
        failAr ("stab centroid " + std::to_string (noteC) + " Hz is outside 900–2500");
    if (noteTail < 500.0 || noteTail > 1600.0)
        failAr ("stab tail " + std::to_string (noteTail) + " ms is outside 500–1600");
    if (noteCorr < 0.40 || noteCorr > 0.70)
        failAr ("stab correlation " + std::to_string (noteCorr) + " is outside 0.40–0.70");
    if (hardCuts (stabNote) != 0)
        failAr ("stab chord hard cut");

    const int barSamples = (int) std::llround (4.0 * 60.0 / 130.0 * sr);
    const Take stabBar = renderTake (bank, empty, stabParams, (double) barSamples / sr, chord, true);
    const int tailN = (int) (0.010 * sr);
    const double gateRms = rmsRange (stabBar, barSamples - tailN, barSamples);
    const double gateDb = 20.0 * std::log10 (std::max (1.0e-12, gateRms / std::max (1.0e-12, (double) peakOf (stabBar))));
    std::cout << "stab bar-gate floor=" << gateDb << " dB re peak\n";
    if (gateDb > -40.0)
        failAr ("bar-line tail gate left the stab at " + std::to_string (gateDb) + " dB");

    auto muted = baseParams (bank, 8);
    const Take leaked = renderTake (bank, groove, muted, 2.0, {}, true);
    std::cout << "mutedPeak=" << peakOf (leaked) << "\n";
    if (peakOf (leaked) > 1.0e-3f)
        failAr ("muted lanes still feeding the output");

    rolla::Groove clap {};
    clap.bars = 2;
    clap.drums.vel[rolla::Clap][0] = 120;
    auto drumParams = baseParams (bank, 2);
    drumParams.mute[rolla::Drums] = false;
    const Take clapTake = renderTake (bank, clap, drumParams, 1.2, {}, true);
    const int a = (int) (0.08 * sr);
    const int b = (int) (0.28 * sr);
    const double clapTail = rmsRange (clapTake, a, b);
    const double clapDb = 20.0 * std::log10 (std::max (1.0e-12, clapTail / std::max (1.0e-12, (double) peakOf (clapTake))));
    std::cout << "clap peak=" << peakOf (clapTake) << " tailDb=" << clapDb
              << " cuts=" << hardCuts (clapTake) << "\n";
    if (hardCuts (clapTake) != 0)
        failAr ("clap hard cut");
    if (clapDb < -45.0)
        failAr ("clap tail is gone by 80 ms (" + std::to_string (clapDb) + " dB)");

    return failures;
}

void silenceCheck (const rolla::PresetBank& bank)
{
    rolla::Engine engine;
    engine.prepare (48000.0, 128);
    rolla::Groove groove {};
    rolla::EngineParams params;
    params.presets = &bank;
    params.bars = 8;
    params.swing = 0.52f;
    float l[128], r[128];
    std::fill (l, l + 128, 0.25f);
    std::fill (r, r + 128, -0.25f);
    juce::MidiBuffer midi;
    rolla::Transport t;
    t.playing = false;
    t.bpm = 130;
    t.sampleRate = 48000;
    engine.process (groove, params, t, l, r, nullptr, nullptr, 128, midi, false);
    float peak = 0.f;
    for (int i = 0; i < 128; ++i)
        peak = std::max (peak, std::max (std::fabs (l[i]), std::fabs (r[i])));
    std::cout << "idlePeak " << peak << "\n";
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
    engine.process (groove, params, t, l, r, nullptr, nullptr, 128, midi, false);
    peak = 0.f;
    int bad = 0;
    for (int i = 0; i < 128; ++i)
    {
        if (! std::isfinite (l[i]) || ! std::isfinite (r[i]))
            ++bad;
        peak = std::max (peak, std::max (std::fabs (l[i]), std::fabs (r[i])));
    }
    std::cout << "notePeak " << peak << " nonFinite " << bad << "\n";
}

void forceSynth (rolla::PresetBank& bank)
{
    auto clear = [] (rolla::SampleSpec& spec)
    {
        spec.mode = rolla::SampleMode::Synth;
        spec.filter.on = false;
    };
    for (int i = 0; i < rolla::PresetBank::kPer; ++i)
    {
        clear (bank.bass[i].sample);
        clear (bank.stab[i].sample);
        clear (bank.hits[i].sample);
        clear (bank.pluck[i].sample);
        clear (bank.pad[i].sample);
        clear (bank.vox[i].sample);
        clear (bank.drums[i].sample);
    }
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;
    const int rng = checkRng();
    if (rng != 0)
        return rng;

    rolla::PresetBank bank;
    rolla::loadPresetsFromJson (bank, BinaryData::presets_json, BinaryData::presets_jsonSize);

    rolla::PresetBank synth = bank;
    forceSynth (synth);
    silenceCheck (synth);
    const int arFailures = analyseSound (synth);
    const auto pack = rolla::openInstalledPack (BinaryData::SAMPLE_MANIFEST_json, BinaryData::SAMPLE_MANIFEST_jsonSize);
    const bool havePack = pack.library != nullptr && pack.index != nullptr;
    if (! havePack)
        std::cout << "no sample pack; sample renders stay silent\n";

    juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile ("renders");
    if (argc > 1)
        dir = juce::File (argv[1]);
    dir.createDirectory();

    int failures = 0;
    const double audioSeconds = 8.0 * 4.0 * 60.0 / 130.0;
    for (int d = 0; d < rolla::NumDialects; ++d)
    {
        const auto file = dir.getChildFile (juce::String ("ROLLA-") + rolla::dialectId (d) + ".wav");
        const auto stats = renderDialect (bank, d, file, pack.library.get(), pack.index.get());
        const double rms = stats.samples > 0 ? std::sqrt (stats.sum / (double) stats.samples) : 0.0;
        const double cpu = stats.seconds / audioSeconds;
        std::cout << rolla::dialectId (d)
                  << " peak=" << stats.peak
                  << " rms=" << rms
                  << " cpu=" << cpu
                  << " file=" << file.getFullPathName()
                  << "\n";
        if (stats.nonFinite != 0)
        {
            std::cerr << "FAIL non-finite samples in " << rolla::dialectId (d) << "\n";
            ++failures;
        }
        if (stats.subnormals != 0)
        {
            std::cerr << "FAIL subnormals in " << rolla::dialectId (d) << "\n";
            ++failures;
        }
        if (stats.peak < 0.01f)
        {
            if (havePack)
            {
                std::cerr << "FAIL silence in " << rolla::dialectId (d) << "\n";
                ++failures;
            }
            else
                std::cout << "no sample pack; render is silent\n";
        }
        if (stats.peak > 1.05f)
        {
            std::cerr << "FAIL overs in " << rolla::dialectId (d) << "\n";
            ++failures;
        }
        if (stats.bassOns < 8)
        {
            std::cerr << "FAIL bass did not play in " << rolla::dialectId (d) << "\n";
            ++failures;
        }
        if (cpu > 0.10)
        {
            std::cerr << "FAIL cpu " << cpu << " exceeds 0.10 of one core in " << rolla::dialectId (d) << "\n";
            ++failures;
        }
    }

    juce::File artifacts ("/opt/cursor/artifacts/renders");
    if (artifacts.getParentDirectory().isDirectory())
    {
        artifacts.createDirectory();
        for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.wav"))
            f.copyFileTo (artifacts.getChildFile (f.getFileName()));
    }

    return (failures + arFailures) == 0 ? 0 : 1;
}
