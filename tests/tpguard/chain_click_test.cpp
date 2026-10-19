// rolla-tp-test part D: the KICK, RIM and PERC mix strips must not add clicks.
//
// Click score = A&R d2_clicks (tools/score_rolls.py), on the mono mean:
//   d[n] = x[n] - x[n-1]; loc = RMS of d over the centred +-10 ms window;
//   a click is |d| > 10 * max(loc, 1e-6) and |d| > 0.02; events within 5 ms merge;
//   the first 5 ms are ignored.
// "Score" here is the largest |d| / max(loc, 1e-6) after the first 5 ms (steps under
// 1e-4 are numerical dust and skipped), plus the event count.
//
// Each hit is levelled like the sample library (loudnessGainDb: -24 LUFS, -6 dBTP cap),
// scaled by the drum amp trim (0.5) and velocity 110, laid out at 126 BPM and rendered
// through one MixRuntime strip with the shipped mix_profiles.json + mix_makeup.json, in
// every dialect, lane level on (as in the engine). Kicks: four on the floor through KICK.
// Rim / perc: a 16th pattern through RIM and through PERC (a pack perc can play as rim).
// The raw score is the same dry pattern at the gain that matches the render's RMS.
//
// Synthetic hits always run (soft attack, clipped flat-top attack, hard edge, ...).
// Set ROLLA_CLICK_PACK=<folder> to add every pack file whose path has "kick" (kicks)
// or "rim" / "perc" (rim and perc). The folder is only read.
//
// Pass: rendered events <= raw events, and rendered score <= max(1.10 x raw, raw + 1).
// The slack covers the linear EQ/HPF and the 15 ms compressor attack nudging the local
// RMS by a few percent; the A&R failure was 2.6 -> over 10 and 12 -> 16..19.
// The kick also has to land on its st_target_lufs (+-1 LU), so loudness holds.

#include "Mix/MixProgram.h"
#include "Mix/MixRuntime.h"
#include "Sampler/SamplePrep.h"

#include <BinaryData.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace chainclick
{
namespace
{
constexpr double kPiD = 3.141592653589793;
constexpr double kTwoPi = 2.0 * kPiD;
constexpr double kBpm = 126.0;
constexpr int kBlock = 256;
constexpr int kKickBars = 6; // 11.4 s: lane level settles, ST read on the last 3 s
constexpr int kPercBars = 4;
constexpr int kKickLane = 6, kRimLane = 9, kPercLane = 13;
constexpr float kDrumAmp = 0.5f;          // Sampler ampFor(Drums): ampDb (trimDb 0) * 0.5
constexpr float kVel = 110.f / 127.f;
const char* const kDialectNames[rolla::kMixDialects] = { "UKMIN", "LONG", "POP", "JACKIN" };

int failures = 0;
double worstGrowth = 0.0;
std::string worstWhat;

void check (bool ok, const std::string& what, const std::string& detail)
{
    std::printf ("%s %s  %s\n", ok ? "PASS" : "FAIL", what.c_str(), detail.c_str());
    if (! ok)
        ++failures;
}

struct Hit
{
    std::string name;
    std::vector<float> l, r; // r empty = mono
    double rate = 48000.0;
    bool pack = false;
};

struct Pattern
{
    std::vector<float> l, r;
};

struct Score
{
    double maxRatio = 0.0;
    double maxStep = 0.0;
    int events = 0;
};

// tools/score_rolls.py d2_clicks, plus the max ratio. x is scaled by gain first.
Score d2Clicks (const std::vector<float>& x, double sr, float gain)
{
    Score s;
    const int n = (int) x.size();
    if (n < 2)
        return s;
    std::vector<double> d ((size_t) n, 0.0);
    for (int i = 1; i < n; ++i)
        d[(size_t) i] = (double) (x[(size_t) i] * gain) - (double) (x[(size_t) (i - 1)] * gain);
    std::vector<double> c ((size_t) n + 1, 0.0);
    for (int i = 0; i < n; ++i)
        c[(size_t) i + 1] = c[(size_t) i] + d[(size_t) i] * d[(size_t) i];
    const int w = (int) (0.01 * sr);
    const int merge = (int) (0.005 * sr);
    int last = -1;
    for (int i = 0; i < n; ++i)
    {
        const int lo = std::clamp (i - w, 0, n);
        const int hi = std::clamp (i + w, 0, n);
        const double loc = std::sqrt (std::max (0.0, c[(size_t) hi] - c[(size_t) lo]) / (double) std::max (hi - lo, 1));
        const double den = std::max (loc, 1.0e-6);
        const double ad = std::fabs (d[(size_t) i]);
        if (ad > 10.0 * den && ad > 0.02)
        {
            if (last < 0 || i - last > merge)
            {
                last = i;
                if (i > merge)
                    ++s.events;
            }
        }
        if (i > merge && ad > 1.0e-4)
        {
            s.maxRatio = std::max (s.maxRatio, ad / den);
            s.maxStep = std::max (s.maxStep, ad);
        }
    }
    return s;
}

std::vector<float> monoOf (const Pattern& p)
{
    std::vector<float> m (p.l.size());
    for (size_t i = 0; i < m.size(); ++i)
        m[i] = 0.5f * (p.l[i] + p.r[i]);
    return m;
}

double rmsOf (const std::vector<float>& x)
{
    double acc = 0.0;
    for (float v : x)
        acc += (double) v * (double) v;
    return std::sqrt (acc / (double) std::max<size_t> (1, x.size()));
}

// BS.1770 over [from, end): both channels, K-weighted, as the lane level stage reads it.
double stLufs (const Pattern& p, double sr, int from)
{
    rolla::KWeight kl, kr;
    kl.prepare (sr);
    kr.prepare (sr);
    double sum = 0.0;
    int count = 0;
    for (int i = 0; i < (int) p.l.size(); ++i)
    {
        const double a = kl.process ((double) p.l[(size_t) i]);
        const double b = kr.process ((double) p.r[(size_t) i]);
        if (i >= from)
        {
            sum += a * a + b * b;
            ++count;
        }
    }
    return -0.691 + 10.0 * std::log10 (sum / (double) std::max (1, count) + 1.0e-30);
}

// 12 ms raised-cosine fade at the END only (sample prep does the same). The attack is never touched.
void tailFade (std::vector<float>& x, double sr)
{
    const int n = (int) x.size();
    const int f = std::min (n, (int) (0.012 * sr));
    for (int i = 0; i < f; ++i)
        x[(size_t) (n - 1 - i)] *= (float) (0.5 - 0.5 * std::cos (kPiD * (double) i / (double) f));
}

double noiseOf (std::uint32_t& seed)
{
    seed = seed * 1664525u + 1013904223u;
    return (double) (seed >> 8) / 8388608.0 - 1.0;
}

// Pitch-swept kick. attackS > 0 is the sample's own soft attack; startAtPeak makes a
// hard edge (first sample = full level); clipAt > 0 flat-tops the attack like a clipped file.
Hit makeKick (const char* name, double decayS, double fEnd, double fStart, double sweepS, double attackS,
              bool startAtPeak, double drive, double clipAt, double clickAmp)
{
    constexpr double sr = 48000.0;
    Hit h;
    h.name = name;
    h.rate = sr;
    const int n = (int) (std::min (6.0 * decayS + 0.05, 3.0) * sr);
    h.l.assign ((size_t) n, 0.f);
    double phase = startAtPeak ? 0.25 * kTwoPi : 0.0;
    std::uint32_t seed = 12345u;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        double env = std::exp (-t / decayS);
        if (attackS > 0.0 && t < attackS)
            env *= 0.5 - 0.5 * std::cos (kPiD * t / attackS);
        double x = drive * env * std::sin (phase);
        if (clipAt > 0.0)
            x = std::clamp (x, -clipAt, clipAt);
        if (clickAmp > 0.0 && t < 0.0015)
            x += clickAmp * noiseOf (seed) * std::exp (-t / 0.0004);
        h.l[(size_t) i] = (float) x;
        const double hz = fEnd + (fStart - fEnd) * std::exp (-t / sweepS);
        phase += kTwoPi * hz / sr;
    }
    tailFade (h.l, sr);
    return h;
}

// Two partials (the first can sweep down) plus optional noise, one decay.
Hit makePerc (const char* name, double decayS, double f1, double a1, double sweepHz, double sweepS, double f2, double a2,
              double noiseAmp, double attackS, bool startAtPeak, double drive, double clipAt)
{
    constexpr double sr = 48000.0;
    Hit h;
    h.name = name;
    h.rate = sr;
    const int n = (int) ((6.0 * decayS + 0.05) * sr);
    h.l.assign ((size_t) n, 0.f);
    double p1 = startAtPeak ? 0.25 * kTwoPi : 0.0;
    double p2 = p1;
    std::uint32_t seed = 777u;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / sr;
        double env = std::exp (-t / decayS);
        if (attackS > 0.0 && t < attackS)
            env *= 0.5 - 0.5 * std::cos (kPiD * t / attackS);
        double x = drive * env * (a1 * std::sin (p1) + a2 * std::sin (p2));
        if (noiseAmp > 0.0)
            x += noiseAmp * env * noiseOf (seed);
        if (clipAt > 0.0)
            x = std::clamp (x, -clipAt, clipAt);
        h.l[(size_t) i] = (float) x;
        const double hz1 = f1 + (sweepS > 0.0 ? sweepHz * std::exp (-t / sweepS) : 0.0);
        p1 += kTwoPi * hz1 / sr;
        p2 += kTwoPi * f2 / sr;
    }
    tailFade (h.l, sr);
    return h;
}

// The library's load gain: -24 LUFS integrated, capped at -6 dBTP.
void normalise (Hit& h)
{
    const float db = rolla::loudnessGainDb (h.l, h.r, h.rate);
    const float g = std::pow (10.f, db / 20.f);
    for (auto& v : h.l)
        v *= g;
    for (auto& v : h.r)
        v *= g;
}

std::vector<Hit> loadPack (const juce::File& root, bool kicks)
{
    std::vector<Hit> out;
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    for (const auto& entry : juce::RangedDirectoryIterator (root, true, "*.wav;*.aif;*.aiff;*.flac", juce::File::findFiles))
    {
        const auto file = entry.getFile();
        const auto path = file.getFullPathName().toLowerCase();
        const bool isKick = path.contains ("kick");
        const bool isPerc = ! isKick && (path.contains ("rim") || path.contains ("perc"));
        if (kicks ? ! isKick : ! isPerc)
            continue;
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
        if (rd == nullptr || rd->lengthInSamples < 16 || rd->sampleRate < 8000.0 || rd->numChannels < 1)
            continue;
        const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 4.0));
        juce::AudioBuffer<float> buf ((int) rd->numChannels, len);
        rd->read (&buf, 0, len, 0, true, true);
        Hit h;
        h.name = file.getFileName().toStdString();
        h.rate = rd->sampleRate;
        h.pack = true;
        h.l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
        if (buf.getNumChannels() > 1)
            h.r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len);
        out.push_back (std::move (h));
    }
    return out;
}

Pattern layHits (const Hit& h, float gain, const std::vector<int>& onsets, int total)
{
    Pattern p { std::vector<float> ((size_t) total, 0.f), std::vector<float> ((size_t) total, 0.f) };
    const auto& hr = h.r.empty() ? h.l : h.r;
    for (int on : onsets)
    {
        for (size_t k = 0; k < h.l.size(); ++k)
        {
            const size_t at = (size_t) on + k;
            if (at >= (size_t) total)
                break;
            p.l[at] += gain * h.l[k];
            p.r[at] += gain * hr[k];
        }
    }
    return p;
}

// One strip, as the engine drives it: beginBlock every block, process per sample.
Pattern renderStrip (const rolla::MixProgram& mix, int dialect, int lane, const Pattern& in, double sr)
{
    auto rt = std::make_unique<rolla::MixRuntime>();
    rt->prepare (sr);
    rolla::MixBlockInfo info;
    info.dialect = dialect;
    info.sumMask = 63;
    info.bypassSumTrim = true;
    info.laneLevel = true;
    Pattern out = in;
    const int n = (int) in.l.size();
    for (int i = 0; i < n; ++i)
    {
        if (i % kBlock == 0)
            rt->beginBlock (mix, info);
        rt->process (lane, out.l[(size_t) i], out.r[(size_t) i]);
    }
    return out;
}

void runHit (const rolla::MixProgram& mix, const Hit& hit, int lane, const char* laneName,
             const std::vector<int>& steps16, int bars, bool kickLoudness)
{
    struct Level
    {
        const char* name;
        float db;
        bool asserted;
    };
    // Hot is printed only: the lane level needs ~1 s to pull a hot start down, and those
    // first hits can touch the knee. Nominal and quiet are the gate. Every dialect runs
    // nominal; quiet and hot run in UKMIN for synthetic hits (keeps the test time down).
    const Level levels[] = { { "nominal", 0.f, true }, { "quiet -8 dB", -8.f, true }, { "hot +4 dB", 4.f, false } };
    const double sr = hit.rate;
    const double s16 = 60.0 / kBpm / 4.0 * sr;
    const int total = (int) std::llround (bars * 16 * s16);
    std::vector<int> onsets;
    for (int b = 0; b < bars; ++b)
        for (int st : steps16)
            onsets.push_back ((int) std::llround ((double) (b * 16 + st) * s16));

    for (int d = 0; d < rolla::kMixDialects; ++d)
    {
        if (! mix.chain[d][lane].valid)
        {
            check (false, std::string (laneName) + " " + kDialectNames[d] + ": strip present in the mix program", "");
            continue;
        }
        const int nLevels = (hit.pack || d != 0) ? 1 : 3;
        for (int li = 0; li < nLevels; ++li)
        {
            const float g = kDrumAmp * kVel * std::pow (10.f, levels[li].db / 20.f);
            const Pattern dry = layHits (hit, g, onsets, total);
            const Pattern out = renderStrip (mix, d, lane, dry, sr);
            const auto dm = monoOf (dry);
            const auto om = monoOf (out);
            const double rd = rmsOf (dm), ro = rmsOf (om);
            const float match = rd > 1.0e-12 ? (float) (ro / rd) : 1.f;
            const Score raw = d2Clicks (dm, sr, match);
            const Score got = d2Clicks (om, sr, 1.f);
            const double allowed = std::max (1.10 * raw.maxRatio, raw.maxRatio + 1.0);
            const bool ok = got.events <= raw.events && got.maxRatio <= allowed;
            const std::string what = std::string (laneName) + " " + kDialectNames[d] + " " + levels[li].name + " "
                                   + hit.name + ": strip adds no click";
            char detail[320];
            std::snprintf (detail, sizeof (detail),
                           "raw %.2f (%d ev, max step %.4f) -> out %.2f (%d ev, max step %.4f), allowed %.2f",
                           raw.maxRatio, raw.events, raw.maxStep, got.maxRatio, got.events, got.maxStep, allowed);
            if (levels[li].asserted)
            {
                check (ok, what, detail);
                if (raw.maxRatio > 0.0 && got.maxRatio / raw.maxRatio > worstGrowth)
                {
                    worstGrowth = got.maxRatio / raw.maxRatio;
                    worstWhat = what;
                }
            }
            else
                std::printf ("INFO %s (%s)  %s\n", what.c_str(), ok ? "within" : "OVER", detail);

            if (kickLoudness && li == 0)
            {
                const double st = stLufs (out, sr, std::max (0, total - (int) (3.0 * sr)));
                const double target = mix.chain[d][lane].stTargetLufs;
                char ld[160];
                std::snprintf (ld, sizeof (ld), "last 3 s %.2f LUFS-S, target %.2f", st, target);
                check (std::fabs (st - target) <= 1.0,
                       std::string (laneName) + " " + kDialectNames[d] + " " + hit.name + ": lane ST on target (+-1 LU)", ld);
            }
        }
    }
}

int run()
{
    auto mix = std::make_unique<rolla::MixProgram>();
    const bool loaded = mix->loadFromMemory (BinaryData::mix_profiles_json, BinaryData::mix_profiles_jsonSize,
                                             BinaryData::main_sum_trim_json, BinaryData::main_sum_trim_jsonSize,
                                             BinaryData::mix_makeup_json, BinaryData::mix_makeup_jsonSize);
    check (loaded, "part D: mix program loads", "");
    if (! loaded)
        return failures;

    std::vector<Hit> kicks;
    kicks.push_back (makeKick ("synth soft attack (4 ms)", 0.25, 50.0, 150.0, 0.03, 0.004, false, 0.9, 0.0, 0.0));
    kicks.push_back (makeKick ("synth clean phase-0", 0.30, 50.0, 150.0, 0.03, 0.0, false, 0.8, 0.0, 0.0));
    kicks.push_back (makeKick ("synth hard edge + click", 0.20, 52.0, 180.0, 0.02, 0.0, true, 0.8, 0.0, 0.35));
    kicks.push_back (makeKick ("synth clipped flat-top attack", 0.18, 50.0, 160.0, 0.025, 0.0, false, 2.5, 0.95, 0.0));
    kicks.push_back (makeKick ("synth long 909 tail", 0.50, 55.0, 115.0, 0.015, 0.0, false, 0.85, 0.0, 0.15));
    kicks.push_back (makeKick ("synth short tight", 0.06, 60.0, 240.0, 0.01, 0.0, true, 0.9, 0.0, 0.0));

    std::vector<Hit> percs;
    percs.push_back (makePerc ("synth rim hard edge", 0.012, 1760.0, 0.7, 0.0, 0.0, 520.0, 0.3, 0.0, 0.0, true, 0.9, 0.0));
    percs.push_back (makePerc ("synth rim clipped", 0.012, 1760.0, 0.7, 0.0, 0.0, 520.0, 0.3, 0.0, 0.0, true, 3.6, 0.9));
    percs.push_back (makePerc ("synth conga soft attack", 0.09, 320.0, 1.0, 80.0, 0.01, 0.0, 0.0, 0.0, 0.002, false, 0.8, 0.0));
    percs.push_back (makePerc ("synth shaker noise", 0.035, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.6, 0.003, false, 1.0, 0.0));
    percs.push_back (makePerc ("synth tick", 0.0006, 6000.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, true, 0.8, 0.0));

    int packKicks = 0, packPercs = 0;
    if (const char* env = std::getenv ("ROLLA_CLICK_PACK"); env != nullptr && *env != 0)
    {
        const juce::File root (juce::String::fromUTF8 (env));
        if (root.isDirectory())
        {
            auto pk = loadPack (root, true);
            auto pp = loadPack (root, false);
            packKicks = (int) pk.size();
            packPercs = (int) pp.size();
            for (auto& h : pk)
                kicks.push_back (std::move (h));
            for (auto& h : pp)
                percs.push_back (std::move (h));
        }
        else
            std::printf ("INFO ROLLA_CLICK_PACK is not a folder; synthetic hits only\n");
    }
    std::printf ("INFO part D: %d pack kicks, %d pack rim/perc (synthetic hits always run)\n", packKicks, packPercs);

    for (auto& h : kicks)
        normalise (h);
    for (auto& h : percs)
        normalise (h);

    const std::vector<int> fourFloor { 0, 4, 8, 12 };
    const std::vector<int> percSteps { 2, 4, 7, 10, 12, 14 };
    for (const auto& h : kicks)
        runHit (*mix, h, kKickLane, "KICK", fourFloor, kKickBars, true);
    for (const auto& h : percs)
    {
        runHit (*mix, h, kRimLane, "RIM", percSteps, kPercBars, false);
        runHit (*mix, h, kPercLane, "PERC", percSteps, kPercBars, false);
    }
    std::printf ("INFO part D worst score growth (render / raw): %.3f  %s\n", worstGrowth, worstWhat.c_str());
    return failures;
}

} // namespace
} // namespace chainclick

int runChainClickTests()
{
    return chainclick::run();
}
