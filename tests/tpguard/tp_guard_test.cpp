// rolla-tp-test: master true-peak safety stage (issue #13, task 2).
//
// Part A drives TruePeakGuard directly: exact passthrough under the ceiling,
// the fs/4 inter-sample worst case, +6 dBFS noise / square / impulses, a
// transient out of silence, release back to unity, and bypass.
// Part B renders the engine for every style x INTENSITY 0/25/50/75/100 with
// the mix program loaded (guard on and off), plus a +12 dB hot pass.
//
// The gate meter reproduces tests/mix/mix_check.py: scipy.signal.resample_poly
// (x, 4, 1), i.e. firwin(81, 1/4, kaiser 5.0) * 4. A stricter 64-tap/phase
// Kaiser-9 meter is printed alongside for information. Neither is the guard's
// own 24-tap detector. Factory presets are sample-only, so part B forces every
// lane to SYNTH; no sample pack is needed.

#include "Engine/Engine.h"
#include "Generator/Generator.h"
#include "Generator/Intensity.h"
#include "Generator/Presets.h"
#include "Mix/MixProgram.h"
#include "Mix/TruePeakGuard.h"
#include "../common/ScratchProfile.h"

#include <BinaryData.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kSr = 48000.0;
constexpr double kBpm = 126.0;
constexpr int kBlock = 256;
constexpr float kCeiling = -6.f;

int failures = 0;

void check (bool ok, const std::string& what, const std::string& detail = {})
{
    std::printf ("%s %s%s%s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.empty() ? "" : "  ", detail.c_str());
    if (! ok)
        ++failures;
}

double besselI0 (double x)
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 60; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
    }
    return sum;
}

// Peak of the signal upsampled `factor` times (Kaiser windowed sinc, 64 taps per phase).
double truePeak (const std::vector<float>& x, int factor = 4)
{
    constexpr int half = 32;
    constexpr double beta = 9.0;
    const double i0b = besselI0 (beta);
    std::vector<std::vector<double>> h ((size_t) factor, std::vector<double> (2 * half));
    for (int j = 0; j < factor; ++j)
    {
        double sum = 0.0;
        for (int k = 0; k < 2 * half; ++k)
        {
            const double t = (double) j / factor - (double) (k - (half - 1));
            const double s = std::fabs (t) < 1e-12 ? 1.0 : std::sin (kPi * t) / (kPi * t);
            const double u = t / (half + 0.5);
            const double w = std::fabs (u) >= 1.0 ? 0.0 : besselI0 (beta * std::sqrt (1.0 - u * u)) / i0b;
            h[(size_t) j][(size_t) k] = s * w;
            sum += s * w;
        }
        for (auto& v : h[(size_t) j])
            v /= sum;
    }
    double peak = 0.0;
    const int n = (int) x.size();
    for (int c = 0; c < n; ++c)
    {
        peak = std::max (peak, (double) std::fabs (x[(size_t) c]));
        for (int j = 1; j < factor; ++j)
        {
            double y = 0.0;
            for (int k = 0; k < 2 * half; ++k)
            {
                const int idx = c + k - (half - 1);
                if (idx >= 0 && idx < n)
                    y += h[(size_t) j][(size_t) k] * x[(size_t) idx];
            }
            peak = std::max (peak, std::fabs (y));
        }
    }
    return peak;
}

// scipy.signal.resample_poly (x, 4, 1) peak, as tests/mix/mix_check.py measures dBTP.
double scipyPeak (const std::vector<float>& x)
{
    constexpr int L = 81, up = 4, half = 40;
    double h[L];
    double sum = 0.0;
    for (int n = 0; n < L; ++n)
    {
        const double m = n - half;
        const double s = m == 0.0 ? 1.0 : std::sin (kPi * 0.25 * m) / (kPi * 0.25 * m);
        const double a = 2.0 * n / (L - 1) - 1.0;
        h[n] = 0.25 * s * besselI0 (5.0 * std::sqrt (std::max (0.0, 1.0 - a * a))) / besselI0 (5.0);
        sum += h[n];
    }
    for (double& v : h)
        v = v / sum * up;
    double peak = 0.0;
    const long n = (long) x.size();
    for (long m = 0; m < n * up; ++m)
    {
        double y = 0.0;
        for (int k = (int) ((m + half) % up); k < L; k += up)
        {
            const long i = (m + half - k) / up;
            if (i >= 0 && i < n)
                y += h[k] * x[(size_t) i];
        }
        peak = std::max (peak, std::fabs (y));
    }
    return peak;
}

// Gate meter (scipy resample_poly 4x), dBTP.
double gateDbtp (const std::vector<float>& l, const std::vector<float>& r)
{
    return 20.0 * std::log10 (std::max (scipyPeak (l), scipyPeak (r)) + 1e-12);
}

double dbtp (const std::vector<float>& l, const std::vector<float>& r, int factor = 4)
{
    const double p = std::max (truePeak (l, factor), truePeak (r, factor));
    return 20.0 * std::log10 (p + 1e-12);
}

std::string fmt (const char* f, double a, double b = 0.0, double c = 0.0)
{
    char buf[200];
    std::snprintf (buf, sizeof (buf), f, a, b, c);
    return buf;
}

struct Stereo
{
    std::vector<float> l, r;
};

Stereo runGuard (const Stereo& in, bool enabled, float* grOut = nullptr)
{
    rolla::TruePeakGuard g;
    g.prepare (kSr);
    g.setEnabled (enabled);
    Stereo out = in;
    for (size_t done = 0; done < out.l.size(); done += kBlock)
    {
        const int n = (int) std::min ((size_t) kBlock, out.l.size() - done);
        g.process (out.l.data() + done, out.r.data() + done, nullptr, nullptr, n);
    }
    if (grOut != nullptr)
        *grOut = g.takeMaxGrDb();
    return out;
}

int latencyOf()
{
    rolla::TruePeakGuard g;
    g.prepare (kSr);
    return g.latencySamples();
}

bool delayedCopy (const Stereo& in, const Stereo& out, int lat)
{
    for (size_t i = 0; i < in.l.size(); ++i)
    {
        const float wantL = i >= (size_t) lat ? in.l[i - (size_t) lat] : 0.f;
        const float wantR = i >= (size_t) lat ? in.r[i - (size_t) lat] : 0.f;
        if (out.l[i] != wantL || out.r[i] != wantR)
            return false;
    }
    return true;
}

// 10 ms raised-cosine fades, so the meters measure the signal, not the cut at the buffer edge.
void fadeEdges (Stereo& s)
{
    const int n = (int) s.l.size();
    const int f = (int) (0.01 * kSr);
    for (int i = 0; i < std::min (f, n / 2); ++i)
    {
        const float g = 0.5f - 0.5f * (float) std::cos (kPi * i / f);
        s.l[(size_t) i] *= g;
        s.r[(size_t) i] *= g;
        s.l[(size_t) (n - 1 - i)] *= g;
        s.r[(size_t) (n - 1 - i)] *= g;
    }
}

// Pass/fail on the gate meter; the strict meter is printed with it.
void expectUnder (const Stereo& o, const std::string& what, const std::string& extra)
{
    const double gate = gateDbtp (o.l, o.r);
    const double strict = dbtp (o.l, o.r);
    char d[200];
    std::snprintf (d, sizeof (d), "out %.2f dBTP (strict meter %.2f)%s", gate, strict, extra.c_str());
    check (gate <= kCeiling, what, d);
}

void partA()
{
    const int lat = latencyOf();
    const int n = (int) kSr; // 1 s

    {
        std::mt19937 rng (7);
        std::normal_distribution<float> gauss (0.f, 1.f);
        Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        for (int i = 0; i < n; ++i)
        {
            const float v = 0.12f * (float) std::sin (2.0 * kPi * 220.0 * i / kSr) + 0.02f * gauss (rng);
            s.l[(size_t) i] = v;
            s.r[(size_t) i] = 0.8f * v;
        }
        float gr = -1.f;
        const Stereo o = runGuard (s, true, &gr);
        check (delayedCopy (s, o, lat) && gr == 0.f, "under the ceiling: output is the input delayed, bit for bit",
               fmt ("latency %.0f samples (%.2f ms at 48 kHz), input %.2f dBTP", lat, lat * 1000.0 / kSr, gateDbtp (s.l, s.r)));
    }
    {
        // fs/4 at 45 degrees: every sample sits 3 dB under the true peak.
        Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        for (int i = 0; i < n; ++i)
            s.l[(size_t) i] = s.r[(size_t) i] = (float) std::sin (kPi / 2.0 * i + kPi / 4.0);
        fadeEdges (s);
        expectUnder (runGuard (s, true), "fs/4 inter-sample peak is caught",
                     fmt (", in %.2f dBTP with sample peak -3.01 dBFS", gateDbtp (s.l, s.r)));
    }
    for (int seed = 1; seed <= 6; ++seed)
    {
        std::mt19937 rng ((unsigned) seed);
        std::normal_distribution<float> gauss (0.f, 1.f);
        Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        for (int i = 0; i < n; ++i)
        {
            s.l[(size_t) i] = std::clamp (0.7f * gauss (rng), -2.f, 2.f);
            s.r[(size_t) i] = std::clamp (0.7f * gauss (rng), -2.f, 2.f);
        }
        fadeEdges (s);
        float gr = 0.f;
        const Stereo o = runGuard (s, true, &gr);
        expectUnder (o, "+6 dBFS white noise stays under -6 dBTP (seed " + std::to_string (seed) + ")",
                     fmt (", in %.2f dBTP, max GR %.1f dB", gateDbtp (s.l, s.r), gr));
    }
    {
        Stereo square { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        Stereo clicks = square;
        for (int i = 0; i < n; ++i)
        {
            square.l[(size_t) i] = square.r[(size_t) i] = ((i / 40) % 2 ? 2.f : -2.f);
            clicks.l[(size_t) i] = clicks.r[(size_t) i] = (i % 4800 == 2400) ? 2.f : 0.f;
        }
        fadeEdges (square);
        float gr = 0.f;
        Stereo o = runGuard (square, true, &gr);
        expectUnder (o, "+6 dBFS square stays under -6 dBTP", fmt (", max GR %.1f dB", gr));
        o = runGuard (clicks, true, &gr);
        expectUnder (o, "+6 dBFS single-sample impulses stay under -6 dBTP", fmt (", max GR %.1f dB", gr));
    }
    {
        // A burst out of silence, then quiet: the lookahead catches the first
        // sample and the gain returns to exactly 1.
        Stereo s { std::vector<float> ((size_t) (3 * n)), std::vector<float> ((size_t) (3 * n)) };
        for (int i = n; i < n + 2400; ++i)
            s.l[(size_t) i] = s.r[(size_t) i] = 0.95f * (float) std::sin (2.0 * kPi * 3000.0 * i / kSr);
        for (int i = 2 * n; i < 3 * n; ++i)
            s.l[(size_t) i] = s.r[(size_t) i] = 0.1f * (float) std::sin (2.0 * kPi * 110.0 * i / kSr);
        const Stereo o = runGuard (s, true);
        bool recovered = true;
        for (int i = 2 * n + lat; i < 3 * n; ++i)
            if (o.l[(size_t) i] != s.l[(size_t) (i - lat)])
                recovered = false;
        Stereo burst;
        burst.l.assign (o.l.begin() + n - 2400, o.l.begin() + 2 * n);
        burst.r.assign (o.r.begin() + n - 2400, o.r.begin() + 2 * n);
        expectUnder (burst, "a burst out of silence is caught", "");
        check (recovered, "after the burst the gain returns to exactly 1");
    }
    {
        Stereo s { std::vector<float> ((size_t) n), std::vector<float> ((size_t) n) };
        for (int i = 0; i < n; ++i)
            s.l[(size_t) i] = s.r[(size_t) i] = 0.9f * (float) std::sin (2.0 * kPi * 1000.0 * i / kSr);
        const Stereo o = runGuard (s, false);
        check (delayedCopy (s, o, lat), "guard off: same latency, signal untouched");
    }
}

// Factory presets are sample-only. Without a pack they would render silence,
// so every lane plays its synth voice here.
void forceSynth (rolla::PresetBank& bank)
{
    for (int i = 0; i < rolla::PresetBank::kPer; ++i)
    {
        bank.bass[i].sample.mode = rolla::SampleMode::Synth;
        bank.stab[i].sample.mode = rolla::SampleMode::Synth;
        bank.hits[i].sample.mode = rolla::SampleMode::Synth;
        bank.pluck[i].sample.mode = rolla::SampleMode::Synth;
        bank.pad[i].sample.mode = rolla::SampleMode::Synth;
        bank.vox[i].sample.mode = rolla::SampleMode::Synth;
        bank.drums[i].sample.mode = rolla::SampleMode::Synth;
    }
}

bool loadProgram (rolla::MixProgram& program)
{
    return program.loadFromMemory (BinaryData::mix_profiles_json, BinaryData::mix_profiles_jsonSize,
                                   BinaryData::main_sum_trim_json, BinaryData::main_sum_trim_jsonSize,
                                   BinaryData::mix_makeup_json, BinaryData::mix_makeup_jsonSize);
}

rolla::EngineParams makeParams (const rolla::PresetBank& bank, const rolla::MixProgram& mix, int dialect)
{
    rolla::EngineParams params;
    const auto& row = bank.dialect[dialect];
    for (int i = 0; i < rolla::NumParts; ++i)
    {
        params.tone[i] = 0.5f;
        params.punch[i] = 0.5f;
        params.preset[i] = row.presetIndex[i] >= 0 ? row.presetIndex[i] : 0;
    }
    params.swing = row.swing;
    params.bars = 8;
    params.key = 2;
    params.dialect = dialect;
    params.presets = &bank;
    params.bpm = (float) kBpm;
    params.mix = &mix;
    return params;
}

Stereo render (const rolla::Groove& groove, const rolla::EngineParams& params, float* gr)
{
    const int total = (int) std::llround (8 * 4.0 * 60.0 / kBpm * kSr);
    Stereo s { std::vector<float> ((size_t) total), std::vector<float> ((size_t) total) };
    rolla::Engine engine;
    engine.prepare (kSr, kBlock);
    double ppq = 0.0;
    float maxGr = 0.f;
    for (int done = 0; done < total; done += kBlock)
    {
        const int n = std::min (kBlock, total - done);
        rolla::Transport t;
        t.playing = true;
        t.bpm = kBpm;
        t.ppqStart = ppq;
        t.sampleRate = kSr;
        juce::MidiBuffer midi;
        engine.process (groove, params, t, s.l.data() + done, s.r.data() + done, nullptr, nullptr, n, midi, true);
        maxGr = std::max (maxGr, engine.takeTpGuardGrDb());
        ppq += (double) n * kBpm / (60.0 * kSr);
    }
    if (gr != nullptr)
        *gr = maxGr;
    return s;
}

void partB()
{
    rolla::PresetBank bank;
    rolla::MixProgram mix;
    const bool ok = rolla::loadPresetsFromJson (bank, BinaryData::presets_json, BinaryData::presets_jsonSize) && loadProgram (mix);
    check (ok, "presets and mix program load");
    if (! ok)
        return;
    forceSynth (bank);

    double worstOn = -100.0, worstOff = -100.0;
    for (int d = 0; d < rolla::NumDialects; ++d)
    {
        rolla::Settings st;
        st.dialect = d;
        st.bars = 8;
        st.fill = true;
        const std::uint32_t seeds[rolla::NumParts] = { 0x0B17u, 0x51ABu, 0x19C3u, 0x77E1u, 0x0A11u, 0xBEEFu, 0x0909u };
        rolla::Groove full {};
        rolla::generate (full, st, seeds, 0x7fu);
        for (int intensity : { 0, 25, 50, 75, 100 })
        {
            const auto plan = rolla::planIntensity (d, intensity, st.stab);
            rolla::Groove g {};
            rolla::applyIntensity (full, g, plan, 0u, 0u);
            auto on = makeParams (bank, mix, d);
            auto off = on;
            off.tpGuard = false;
            float gr = 0.f;
            const Stereo a = render (g, on, &gr);
            const Stereo b = render (g, off, nullptr);
            const double tpOn = gateDbtp (a.l, a.r), tpOff = gateDbtp (b.l, b.r);
            worstOn = std::max (worstOn, tpOn);
            worstOff = std::max (worstOff, tpOff);
            char label[96];
            std::snprintf (label, sizeof (label), "%s INTENSITY %d at or under -6 dBTP", rolla::dialectName (d), intensity);
            check (tpOn <= kCeiling && tpOff > -80.0, label,
                   fmt ("guard on %.2f dBTP (GR %.2f dB), off %.2f dBTP", tpOn, gr, tpOff));
        }
        // Hot pass: +12 dB output at full intensity makes the guard work hard.
        auto hot = makeParams (bank, mix, d);
        hot.outputDb = 12.f;
        float gr = 0.f;
        const Stereo h = render (full, hot, &gr);
        const double tp = gateDbtp (h.l, h.r);
        check (tp <= kCeiling, std::string (rolla::dialectName (d)) + " +12 dB output still at or under -6 dBTP",
               fmt ("%.2f dBTP, GR %.1f dB, strict meter %.2f dBTP", tp, gr, dbtp (h.l, h.r)));
    }
    std::printf ("INFO worst over styles x intensity: guard on %.2f dBTP, guard off %.2f dBTP (synth voices, no sample pack)\n",
                 worstOn, worstOff);
}
} // namespace

int main()
{
    // Links the sample library and user slots: run on a scratch profile like every test.
    if (rolla::test::useScratchProfile ("tp").empty())
    {
        std::printf ("could not set up a scratch profile\n");
        return 1;
    }
    partA();
    partB();
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
