#pragma once

// Retune guard (issue #13 amendment): retuning must never wreck a sound.
// No JUCE: the processor, the tests and the tools share this code.
//
// Before a lane plays a library file pitched by `semis` (tonalShift in
// PitchProbe.h), judgeRetune renders that shift offline with the sampler's own
// interpolator and compares it to the original:
//  - shift size: about +-3 st for pads, chords and vox, +-5 for stabs and
//    plucks, +-4 for bass;
//  - bass low end: the fundamental stays inside the sub band, the sub+low share
//    holds, low mids do not pile up, the file stays mono-safe, and (when the kick
//    is known) the fundamental does not sit just off the kick's;
//  - artifacts: energy folding over Nyquist (the sampler's sinc does not band-limit
//    an upward shift), and DC;
//  - spectrum: centroid, tilt and band balance against an ideal shift of the
//    original (so only damage counts, not the move itself), plus how far the
//    band balance moved from the original (a pad turning thin or honky), and the
//    lane's low-end slot from the mix spec;
//  - waveform character: attack time, crest factor and a harmonic-to-noise proxy.
// Any failed check rejects the file for that shift. The caller re-picks from the
// pool (pickRetuneSafe) and caches verdicts per file and rounded shift (RetuneCache).

#include "PitchProbe.h"
#include "../Generator/Pattern.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace rolla
{

enum class RetuneKind : std::uint8_t
{
    Tonal = 0, // pads, chords, vox: sustained, about +-3 st
    Short,     // stabs, plucks, hits: about +-5 st
    Bass       // bass one-shots: about +-4 st, fundamental inside the sub band
};

// Defaults come from docs/mix/MIX_SPEC.md and the #13 amendment.
// resources/retune_tolerances.json overrides them in the plugin.
struct RetuneTolerances
{
    double capTonal = 3.0;
    double capShort = 5.0;
    double capBass = 4.0;
    // "About": a file 30 cents off its note may go that far past the cap.
    double capSlack = 0.5;

    // BASS HPF 30 Hz (below root D1, 36.7 Hz).
    double subFloorHz = 30.0;
    // STAB / PAD HPF 120 Hz "keeps <120 Hz for kick/bass".
    double subCeilHz = 120.0;
    // Bass sub+low (<200 Hz) reference minimum, 87.7 %.
    double subLowMin = 0.877;
    // Bass 200-800 Hz share may grow by this much at most (low-mid pile-up).
    double lowMidRise = 0.10;
    // Bass corr <120 Hz >= 0.99.
    double monoCorrMin = 0.99;
    // A bass fundamental within this of the kick's, but not in unison, beats against it.
    double kickClashSt = 1.0;
    double kickUnisonSt = 0.25;

    // PAD: <120 Hz <= -20 dB re total. STAB / HITS: <= -15 dB.
    double tonalLowMaxDb = -20.0;
    double shortLowMaxDb = -15.0;

    // Against the ideal shift of the original.
    double centroidExcessOct = 0.30;
    double tiltExcessDbOct = 3.0; // the stab tilt window is 3 dB/oct wide
    double bandExcess = 0.15;
    double flatnessExcessDb = 8.0;
    // Against the original: the largest share any band gains or loses. A coarse
    // backstop (a narrow sound crossing one band edge moves a lot); the caps and
    // the slot checks do the fine work.
    double bandMove = 0.5;

    // Attack time may scale by 2^attackOct either way. Faster than attackFloorMs is a click.
    double attackOct = 0.5;
    double attackFloorMs = 2.0;
    double crestDb = 3.0;

    // Share of the energy that folds over Nyquist (-23 dB), and extra energy in the top 10 %.
    double foldShare = 0.005;
    double nyquistExcess = 0.01;
    // DC <= -60 dBFS.
    double dcMax = 0.001;

    // Seconds of source audio analysed.
    double maxSeconds = 2.0;

    double capFor (RetuneKind kind) const noexcept
    {
        return kind == RetuneKind::Bass ? capBass : (kind == RetuneKind::Short ? capShort : capTonal);
    }
};

struct RetuneRequest
{
    const float* left = nullptr;
    const float* right = nullptr; // null: mono
    int frames = 0;
    double rate = 48000.0;    // the file's rate
    double outRate = 0.0;     // playback rate; 0 is the file's rate
    double semis = 0.0;       // what tonalShift asks for
    double rootMidi = -1.0;   // the root tonalShift used; < 0 probes the file
    RetuneKind kind = RetuneKind::Tonal;
    double kickF0Hz = 0.0;    // 0: unknown, no clash check
};

inline constexpr int kRetuneBands = 6;
// The mix spec's band edges (Hz).
inline constexpr double kRetuneBandEdge[kRetuneBands + 1] = { 20.0, 60.0, 250.0, 1000.0, 4000.0, 12000.0, 20000.0 };

// [0] is the original (or the ideal shift where noted), [1] the rendered shift.
struct RetuneMetrics
{
    double shift = 0.0;
    double cap = 0.0;
    double fundamentalHz[2] {};
    double centroidHz[2] {};     // ideal, rendered
    double centroidExcessOct = 0.0;
    double tiltDbOct[2] {};      // ideal, rendered
    double bandShare[2][kRetuneBands] {};
    double bandExcess = 0.0;     // rendered vs ideal
    double bandMove = 0.0;       // rendered vs original
    double subLowShare[2] {};
    double lowMidShare[2] {};
    double lowDb[2] {};          // <120 Hz re total
    double attackMs[2] {};
    double crestDb[2] {};
    double flatnessDb[2] {};     // ideal, rendered
    double foldShare = 0.0;
    double nyquistExcess = 0.0;
    double dc[2] {};
    double lowCorr[2] { 1.0, 1.0 };
};

struct RetuneVerdict
{
    bool ok = true;
    // False when the audio was not there to judge (still loading). Not a pass.
    bool known = true;
    std::string reason;
    RetuneMetrics metrics;
};

namespace retune
{

constexpr double kPi = 3.14159265358979323846;

// Same kernel as Sampler::readSinc: 16-tap Hann-windowed sinc with no cutoff
// scaling, so an upward shift aliases here exactly as it does in playback.
inline float readSinc (const float* src, int n, double index) noexcept
{
    const int i = (int) std::floor (index);
    const float frac = (float) (index - (double) i);
    constexpr int k = 8;
    constexpr float pi = 3.14159265358979323846f;
    float acc = 0.f;
    for (int t = -k + 1; t <= k; ++t)
    {
        const int idx = i + t;
        const float x = (idx >= 0 && idx < n) ? src[idx] : 0.f;
        const float p = frac - (float) t;
        float w = 1.f;
        if (std::fabs (p) > 1.0e-5f)
        {
            const float sinc = std::sin (pi * p) / (pi * p);
            const float window = 0.5f + 0.5f * std::cos (pi * p / (float) k);
            w = sinc * window;
        }
        acc += x * w;
    }
    return acc;
}

// src[0..n) at `rate`, pitched by semis and written at outRate, start to end
// like a voice without a loop (Sampler::take's ratio).
inline std::vector<float> renderShift (const float* src, int n, double rate, double semis, double outRate)
{
    std::vector<float> out;
    if (src == nullptr || n <= 1 || rate <= 0.0 || outRate <= 0.0)
        return out;
    const double step = std::pow (2.0, semis / 12.0) * (rate / outRate);
    if (step <= 1.0e-6)
        return out;
    out.reserve ((size_t) ((double) n / step) + 2);
    for (double pos = 0.0; pos < (double) (n - 1); pos += step)
        out.push_back (readSinc (src, n, pos));
    return out;
}

struct Spectrum
{
    std::vector<double> power; // averaged |X|^2, bins 0..N/2
    double binHz = 1.0;
};

// Averaged Hann-windowed power spectrum. A file shorter than the FFT is windowed
// over its own length, as in pitchprobe::spectrumPeaks.
inline Spectrum powerSpectrum (const std::vector<float>& x, double rate)
{
    constexpr size_t N = 8192;
    Spectrum s;
    s.binHz = rate / (double) N;
    s.power.assign (N / 2 + 1, 0.0);
    if (x.size() < 2)
        return s;
    const size_t L = std::max<size_t> (2, std::min (N, x.size()));
    const size_t hop = N / 2;
    std::vector<std::complex<double>> buf (N);
    int frames = 0;
    for (size_t start = 0; frames < 64; start += hop)
    {
        if (frames > 0 && start + L > x.size())
            break;
        for (size_t i = 0; i < N; ++i)
        {
            const double w = i < L ? 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (L - 1)) : 0.0;
            const double v = start + i < x.size() ? (double) x[start + i] : 0.0;
            buf[i] = std::complex<double> (v * w, 0.0);
        }
        pitchprobe::fft (buf);
        for (size_t k = 0; k <= N / 2; ++k)
            s.power[k] += std::norm (buf[k]);
        ++frames;
    }
    for (double& p : s.power)
        p /= (double) std::max (1, frames);
    return s;
}

// Energy in [lo, hi) Hz, read on a spectrum whose frequencies are scaled by
// `scale` (1 = as measured; r = the ideal shift of the original by ratio r).
inline double bandEnergy (const Spectrum& s, double lo, double hi, double scale = 1.0) noexcept
{
    double e = 0.0;
    for (size_t k = 1; k < s.power.size(); ++k)
    {
        const double f = (double) k * s.binHz * scale;
        if (f >= lo && f < hi)
            e += s.power[k];
    }
    return e;
}

inline double centroid (const Spectrum& s, double lo, double hi, double scale = 1.0) noexcept
{
    double num = 0.0, den = 0.0;
    for (size_t k = 1; k < s.power.size(); ++k)
    {
        const double f = (double) k * s.binHz * scale;
        if (f < lo || f >= hi)
            continue;
        num += f * s.power[k];
        den += s.power[k];
    }
    return den > 1.0e-30 ? num / den : 0.0;
}

// Least-squares slope (dB/oct) of the 1/3-octave levels, 200 Hz-8 kHz (the
// mix spec's tilt). Bands are floored 50 dB under the loudest, so an empty band
// counts the same in both spectra. hi caps the range (Nyquist).
inline double tilt (const Spectrum& s, double hi, double scale = 1.0) noexcept
{
    double level[17] {};
    double x[17] {};
    double top = -1.0e9;
    int n = 0;
    for (int b = -7; b <= 9; ++b)
    {
        const double fc = 1000.0 * std::pow (2.0, (double) b / 3.0);
        const double lo = fc * std::pow (2.0, -1.0 / 6.0);
        const double up = fc * std::pow (2.0, 1.0 / 6.0);
        if (up > hi)
            break;
        level[n] = 10.0 * std::log10 (bandEnergy (s, lo, up, scale) + 1.0e-30);
        x[n] = std::log2 (fc);
        top = std::max (top, level[n]);
        ++n;
    }
    if (n < 3)
        return 0.0;
    double mx = 0.0, my = 0.0;
    for (int i = 0; i < n; ++i)
    {
        level[i] = std::max (level[i], top - 50.0);
        mx += x[i];
        my += level[i];
    }
    mx /= n;
    my /= n;
    double sxy = 0.0, sxx = 0.0;
    for (int i = 0; i < n; ++i)
    {
        sxy += (x[i] - mx) * (level[i] - my);
        sxx += (x[i] - mx) * (x[i] - mx);
    }
    return sxx > 1.0e-12 ? sxy / sxx : 0.0;
}

// Spectral flatness (dB) over [lo, hi): near 0 is noise, very negative is tonal.
// Bins are floored 50 dB under the loudest so interpolation dust does not count.
inline double flatness (const Spectrum& s, double lo, double hi, double scale = 1.0) noexcept
{
    double top = 0.0;
    for (size_t k = 1; k < s.power.size(); ++k)
    {
        const double f = (double) k * s.binHz * scale;
        if (f >= lo && f < hi)
            top = std::max (top, s.power[k]);
    }
    if (top <= 1.0e-30)
        return 0.0;
    const double floorP = top * 1.0e-5;
    double logSum = 0.0, sum = 0.0;
    int n = 0;
    for (size_t k = 1; k < s.power.size(); ++k)
    {
        const double f = (double) k * s.binHz * scale;
        if (f < lo || f >= hi)
            continue;
        const double p = std::max (s.power[k], floorP);
        logSum += std::log (p);
        sum += p;
        ++n;
    }
    if (n == 0 || sum <= 0.0)
        return 0.0;
    const double geo = std::exp (logSum / n);
    return 10.0 * std::log10 (geo / (sum / n));
}

// 10 % -> 90 % rise of a peak-hold envelope (10 ms release), in ms.
inline double attackMs (const std::vector<float>& x, double rate) noexcept
{
    if (x.empty() || rate <= 0.0)
        return 0.0;
    const double rel = std::exp (-1.0 / (0.010 * rate));
    std::vector<double> env (x.size());
    double e = 0.0, peak = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        e = std::max ((double) std::fabs (x[i]), e * rel);
        env[i] = e;
        peak = std::max (peak, e);
    }
    if (peak <= 1.0e-9)
        return 0.0;
    size_t rise10 = 0, rise90 = 0;
    while (rise10 < env.size() && env[rise10] < 0.1 * peak)
        ++rise10;
    rise90 = rise10;
    while (rise90 < env.size() && env[rise90] < 0.9 * peak)
        ++rise90;
    return 1000.0 * (double) (rise90 - rise10) / rate;
}

inline double crestDb (const std::vector<float>& x) noexcept
{
    double peak = 0.0, sum = 0.0;
    for (float v : x)
    {
        peak = std::max (peak, (double) std::fabs (v));
        sum += (double) v * (double) v;
    }
    if (x.empty() || sum <= 1.0e-20)
        return 0.0;
    const double rms = std::sqrt (sum / (double) x.size());
    return 20.0 * std::log10 (peak / rms);
}

inline double meanOf (const std::vector<float>& x) noexcept
{
    double sum = 0.0;
    for (float v : x)
        sum += v;
    return x.empty() ? 0.0 : sum / (double) x.size();
}

// L/R correlation under 120 Hz (one-pole low-pass), the mix spec's mono test.
inline double lowCorr (const std::vector<float>& l, const std::vector<float>& r, double rate) noexcept
{
    if (l.empty() || r.size() != l.size() || rate <= 0.0)
        return 1.0;
    const double a = 1.0 - std::exp (-2.0 * kPi * 120.0 / rate);
    double yl = 0.0, yr = 0.0, slr = 0.0, sll = 0.0, srr = 0.0;
    for (size_t i = 0; i < l.size(); ++i)
    {
        yl += a * ((double) l[i] - yl);
        yr += a * ((double) r[i] - yr);
        slr += yl * yr;
        sll += yl * yl;
        srr += yr * yr;
    }
    if (sll <= 1.0e-20 || srr <= 1.0e-20)
        return 1.0;
    return slr / std::sqrt (sll * srr);
}

inline double hzOfMidi (double midi) noexcept
{
    return 440.0 * std::pow (2.0, (midi - 69.0) / 12.0);
}

} // namespace retune

// The root tonalShift would retune from, or -1 when it plays the file as recorded
// (unpitched, or under kPitchConfMin with no agreeing filename note). In a locked
// key such a file is not retuned: the lane picks a different one.
inline double retuneRoot (const PitchInfo& m, int namedRoot) noexcept
{
    if (! m.pitched())
        return -1.0;
    if (m.conf >= kPitchConfMin)
        return (double) m.rootPitch;
    if (namedRoot >= 0 && pitchprobe::pcDistance ((double) namedRoot, (double) m.rootPitch) <= 1.0)
        return (double) namedRoot + 12.0 * std::round (((double) m.rootPitch - (double) namedRoot) / 12.0);
    return -1.0;
}

// Lane -> shift class. Stabs, plucks and hits over 1.5 s (or chord files over
// 1 s) sustain like a pad, so they take the tonal cap.
inline RetuneKind retuneKindFor (int part, double seconds, bool chordFile) noexcept
{
    if (part == Bass)
        return RetuneKind::Bass;
    if (part == Pad || part == Vox)
        return RetuneKind::Tonal;
    if (seconds > 1.5 || (chordFile && seconds > 1.0))
        return RetuneKind::Tonal;
    return RetuneKind::Short;
}

inline RetuneVerdict judgeRetune (const RetuneRequest& req, const RetuneTolerances& tol = {})
{
    using namespace retune;
    RetuneVerdict v;
    RetuneMetrics& m = v.metrics;
    m.shift = req.semis;
    m.cap = tol.capFor (req.kind);
    if (req.left == nullptr || req.frames < 8 || req.rate < 8000.0)
    {
        v.ok = false;
        v.known = false;
        v.reason = "no audio to judge";
        return v;
    }
    // As recorded is not a retune.
    if (std::fabs (req.semis) < 0.05)
        return v;

    auto fail = [&v] (const char* fmt, double a, double b)
    {
        if (! v.ok)
            return;
        char text[128];
        std::snprintf (text, sizeof text, fmt, a, b);
        v.ok = false;
        v.reason = text;
    };

    const double outRate = req.outRate > 0.0 ? req.outRate : req.rate;
    const double ratio = std::pow (2.0, req.semis / 12.0);
    const double outNyq = 0.5 * outRate;
    const int n = std::min (req.frames, (int) std::max (256.0, tol.maxSeconds * req.rate));

    std::vector<float> l (req.left, req.left + n);
    std::vector<float> r;
    if (req.right != nullptr)
        r.assign (req.right, req.right + n);
    std::vector<float> mono (l);
    if (! r.empty())
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] = 0.5f * (l[(size_t) i] + r[(size_t) i]);

    const std::vector<float> shiftedL = renderShift (l.data(), n, req.rate, req.semis, outRate);
    std::vector<float> shiftedR;
    if (! r.empty())
        shiftedR = renderShift (r.data(), n, req.rate, req.semis, outRate);
    std::vector<float> shifted (shiftedL);
    if (! shiftedR.empty() && shiftedR.size() == shiftedL.size())
        for (size_t i = 0; i < shifted.size(); ++i)
            shifted[i] = 0.5f * (shiftedL[i] + shiftedR[i]);
    if (shifted.size() < 8)
    {
        fail ("shift %.1f st leaves %.0f frames", req.semis, (double) shifted.size());
        return v;
    }

    const Spectrum so = powerSpectrum (mono, req.rate);
    const Spectrum ss = powerSpectrum (shifted, outRate);
    const double topHz = std::min (kRetuneBandEdge[kRetuneBands], outNyq);

    // Shift size.
    m.shift = req.semis;
    if (std::fabs (req.semis) > m.cap + tol.capSlack)
        fail ("shift %+.1f st is past the %.0f st cap", req.semis, m.cap);

    // Fundamental (bass).
    double root = req.rootMidi;
    if (root < 0.0)
    {
        const PitchInfo probed = probePitch (req.left, req.right, n, req.rate,
                                             req.kind == RetuneKind::Bass ? kPitchFloorHz : kPitchFloorTonalHz);
        root = probed.pitched() ? (double) probed.rootPitch : -1.0;
    }
    if (root >= 0.0)
    {
        m.fundamentalHz[0] = hzOfMidi (root);
        m.fundamentalHz[1] = hzOfMidi (root + req.semis);
    }
    if (req.kind == RetuneKind::Bass && root >= 0.0)
    {
        if (m.fundamentalHz[1] < tol.subFloorHz)
            fail ("bass fundamental %.1f Hz is under the %.0f Hz sub floor", m.fundamentalHz[1], tol.subFloorHz);
        if (m.fundamentalHz[1] > tol.subCeilHz)
            fail ("bass fundamental %.1f Hz is over the %.0f Hz sub band", m.fundamentalHz[1], tol.subCeilHz);
        if (req.kickF0Hz > 0.0)
        {
            const double d = std::fabs (12.0 * std::log2 (m.fundamentalHz[1] / req.kickF0Hz));
            if (d > tol.kickUnisonSt && d < tol.kickClashSt)
                fail ("bass fundamental %.1f Hz beats against the %.1f Hz kick", m.fundamentalHz[1], req.kickF0Hz);
        }
    }

    // Artifacts. What lands past the output Nyquist folds back into the band.
    const double totalO = bandEnergy (so, 1.0, 1.0e9);
    m.foldShare = totalO > 1.0e-30 ? bandEnergy (so, outNyq, 1.0e9, ratio) / totalO : 0.0;
    {
        const double totalS = bandEnergy (ss, 1.0, outNyq + 1.0);
        const double totalP = bandEnergy (so, 1.0, outNyq, ratio);
        const double topS = totalS > 1.0e-30 ? bandEnergy (ss, 0.9 * outNyq, outNyq + 1.0) / totalS : 0.0;
        const double topP = totalP > 1.0e-30 ? bandEnergy (so, 0.9 * outNyq, outNyq, ratio) / totalP : 0.0;
        m.nyquistExcess = std::max (0.0, topS - topP);
    }
    if (m.foldShare > tol.foldShare)
        fail ("aliasing: %.1f%% of the energy folds over Nyquist (limit %.1f%%)", 100.0 * m.foldShare, 100.0 * tol.foldShare);
    if (m.nyquistExcess > tol.nyquistExcess)
        fail ("aliasing: %.1f%% extra energy near Nyquist (limit %.1f%%)", 100.0 * m.nyquistExcess, 100.0 * tol.nyquistExcess);
    m.dc[0] = meanOf (mono);
    m.dc[1] = meanOf (shifted);
    if (std::fabs (m.dc[1]) > tol.dcMax && std::fabs (m.dc[1]) > 2.0 * std::fabs (m.dc[0]))
        fail ("DC offset %.4f (limit %.4f)", m.dc[1], tol.dcMax);

    // Spectrum: the render against an ideal shift of the original.
    const double lo = kRetuneBandEdge[0];
    m.centroidHz[0] = centroid (so, lo, topHz, ratio);
    m.centroidHz[1] = centroid (ss, lo, topHz);
    m.centroidExcessOct = (m.centroidHz[0] > 0.0 && m.centroidHz[1] > 0.0)
                            ? std::fabs (std::log2 (m.centroidHz[1] / m.centroidHz[0])) : 0.0;
    m.tiltDbOct[0] = tilt (so, outNyq, ratio);
    m.tiltDbOct[1] = tilt (ss, outNyq);
    {
        const double totIdeal = bandEnergy (so, lo, topHz, ratio);
        const double totShift = bandEnergy (ss, lo, topHz);
        const double totOrig = bandEnergy (so, lo, std::min (kRetuneBandEdge[kRetuneBands], 0.5 * req.rate));
        for (int b = 0; b < kRetuneBands; ++b)
        {
            const double a = kRetuneBandEdge[b];
            const double z = kRetuneBandEdge[b + 1];
            const double ideal = totIdeal > 1.0e-30 ? bandEnergy (so, a, std::min (z, topHz), ratio) / totIdeal : 0.0;
            const double orig = totOrig > 1.0e-30 ? bandEnergy (so, a, z) / totOrig : 0.0;
            m.bandShare[0][b] = orig;
            m.bandShare[1][b] = totShift > 1.0e-30 ? bandEnergy (ss, a, std::min (z, topHz)) / totShift : 0.0;
            m.bandExcess = std::max (m.bandExcess, std::fabs (m.bandShare[1][b] - ideal));
            m.bandMove = std::max (m.bandMove, std::fabs (m.bandShare[1][b] - orig));
        }
        auto share = [] (const Spectrum& s, double a, double z, double tot)
        {
            return tot > 1.0e-30 ? bandEnergy (s, a, z) / tot : 0.0;
        };
        m.subLowShare[0] = share (so, lo, 200.0, totOrig);
        m.subLowShare[1] = share (ss, lo, 200.0, totShift);
        m.lowMidShare[0] = share (so, 200.0, 800.0, totOrig);
        m.lowMidShare[1] = share (ss, 200.0, 800.0, totShift);
        m.lowDb[0] = 10.0 * std::log10 (share (so, lo, 120.0, totOrig) + 1.0e-12);
        m.lowDb[1] = 10.0 * std::log10 (share (ss, lo, 120.0, totShift) + 1.0e-12);
    }
    if (m.centroidExcessOct > tol.centroidExcessOct)
        fail ("centroid %.0f Hz vs %.0f Hz for a clean shift", m.centroidHz[1], m.centroidHz[0]);
    if (std::fabs (m.tiltDbOct[1] - m.tiltDbOct[0]) > tol.tiltExcessDbOct)
        fail ("tilt %.1f dB/oct vs %.1f for a clean shift", m.tiltDbOct[1], m.tiltDbOct[0]);
    if (m.bandExcess > tol.bandExcess)
        fail ("band balance off a clean shift by %.0f%% (limit %.0f%%)", 100.0 * m.bandExcess, 100.0 * tol.bandExcess);
    if (m.bandMove > tol.bandMove)
        fail ("band balance moved %.0f%% from the original (limit %.0f%%): thin or honky", 100.0 * m.bandMove, 100.0 * tol.bandMove);

    // The lane's slot. Only a slot the original fitted can be lost.
    if (req.kind == RetuneKind::Bass)
    {
        if (m.subLowShare[0] >= tol.subLowMin && m.subLowShare[1] < tol.subLowMin)
            fail ("bass loses its sub: %.0f%% under 200 Hz (min %.0f%%)", 100.0 * m.subLowShare[1], 100.0 * tol.subLowMin);
        if (m.lowMidShare[1] > m.lowMidShare[0] + tol.lowMidRise)
            fail ("bass piles up low mids: %.0f%% in 200-800 Hz (was %.0f%%)", 100.0 * m.lowMidShare[1], 100.0 * m.lowMidShare[0]);
    }
    else
    {
        const double cap = req.kind == RetuneKind::Tonal ? tol.tonalLowMaxDb : tol.shortLowMaxDb;
        if (m.lowDb[0] <= cap && m.lowDb[1] > cap)
            fail ("low end %.1f dB under 120 Hz crowds the kick and bass (max %.0f dB)", m.lowDb[1], cap);
    }

    // Waveform character.
    m.attackMs[0] = attackMs (mono, req.rate);
    m.attackMs[1] = attackMs (shifted, outRate);
    if (std::max (m.attackMs[0], m.attackMs[1]) >= tol.attackFloorMs)
    {
        const double a0 = std::max (m.attackMs[0], 0.25);
        const double a1 = std::max (m.attackMs[1], 0.25);
        if (std::fabs (std::log2 (a1 / a0)) > tol.attackOct)
            fail ("attack %.1f ms vs %.1f ms: smeared or slowed", m.attackMs[1], m.attackMs[0]);
    }
    m.crestDb[0] = crestDb (mono);
    m.crestDb[1] = crestDb (shifted);
    if (std::fabs (m.crestDb[1] - m.crestDb[0]) > tol.crestDb)
        fail ("crest %.1f dB vs %.1f dB", m.crestDb[1], m.crestDb[0]);
    const double flatHi = std::min (10000.0, 0.9 * outNyq);
    m.flatnessDb[0] = flatness (so, 50.0, flatHi, ratio);
    m.flatnessDb[1] = flatness (ss, 50.0, flatHi);
    if (std::fabs (m.flatnessDb[1] - m.flatnessDb[0]) > tol.flatnessExcessDb)
        fail ("harmonic-to-noise moved: flatness %.1f dB vs %.1f dB (grainy)", m.flatnessDb[1], m.flatnessDb[0]);

    // Mono safety (bass). Resampling should not change it; a render that does is broken.
    if (req.kind == RetuneKind::Bass && ! r.empty())
    {
        m.lowCorr[0] = lowCorr (l, r, req.rate);
        m.lowCorr[1] = lowCorr (shiftedL, shiftedR, outRate);
        if (m.lowCorr[0] >= tol.monoCorrMin && m.lowCorr[1] < tol.monoCorrMin)
            fail ("bass is no longer mono-safe: corr <120 Hz %.3f (min %.2f)", m.lowCorr[1], tol.monoCorrMin);
    }
    return v;
}

// Verdicts per (file, lane class, shift rounded to a tenth of a semitone). One
// file's shifts differ by whole steps plus its fixed detune, so a roll fills a
// handful of rows. Any thread; never the audio thread (judging renders audio).
class RetuneCache
{
public:
    template <typename Judge>
    RetuneVerdict get (const std::string& file, RetuneKind kind, double semis, Judge&& judge)
    {
        const std::string key = keyOf (file, kind, semis);
        {
            std::lock_guard<std::mutex> lock (mu);
            const auto it = rows.find (key);
            if (it != rows.end())
                return it->second;
        }
        RetuneVerdict v = judge();
        if (! v.known)
            return v;
        std::lock_guard<std::mutex> lock (mu);
        // A whole library times a dozen shifts is a few MB; past that, start over.
        if (rows.size() > 20000)
            rows.clear();
        rows[key] = v;
        return v;
    }

    std::size_t size() const
    {
        std::lock_guard<std::mutex> lock (mu);
        return rows.size();
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock (mu);
        rows.clear();
    }

    static long long tenths (double semis) noexcept
    {
        return std::llround (semis * 10.0);
    }

private:
    static std::string keyOf (const std::string& file, RetuneKind kind, double semis)
    {
        return file + "|" + std::to_string ((int) kind) + "|" + std::to_string (tenths (semis));
    }

    mutable std::mutex mu;
    std::map<std::string, RetuneVerdict> rows;
};

struct RetunePick
{
    int index = -1;       // candidate to play; -1 when there are none
    bool safe = false;    // false: nothing passed, index is the nearest workable one
    bool pending = false; // a candidate before any pass was still loading: judge again later
    int judged = 0;
};

// Walks candidates start, start+1, ... (wrapping, at most `window`) and takes the
// first whose verdict passes. judge (index, worst) returns the verdict and writes
// the largest shift that candidate needs (huge when it cannot be retuned). With no
// pass, the candidate with the smallest worst shift is the nearest workable one.
template <typename Judge>
RetunePick pickRetuneSafe (int count, int start, int window, Judge&& judge)
{
    RetunePick pick;
    if (count <= 0)
        return pick;
    start = ((start % count) + count) % count;
    const int n = std::min (count, std::max (1, window));
    double bestWorst = 1.0e30;
    for (int i = 0; i < n; ++i)
    {
        const int at = (start + i) % count;
        double worst = 1.0e9;
        const RetuneVerdict v = judge (at, worst);
        ++pick.judged;
        if (! v.known)
        {
            // Never skip past a file only because it has not loaded yet.
            pick.pending = true;
            pick.index = start;
            pick.safe = false;
            return pick;
        }
        if (v.ok)
        {
            pick.index = at;
            pick.safe = true;
            return pick;
        }
        if (worst < bestWorst)
        {
            bestWorst = worst;
            pick.index = at;
        }
    }
    if (pick.index < 0)
        pick.index = start;
    return pick;
}

// Melak's rule (Maestro 354): with the key locked, a lane whose whole pool fails
// the guard is muted for the pattern, never played mangled. In Auto the key moves
// instead (pickAutoKey), and a lane with no pass anywhere keeps its least-shifted file.
inline bool retuneMuteFor (bool keyLocked, const RetunePick& pick) noexcept
{
    return keyLocked && ! pick.pending && ! pick.safe && pick.index >= 0;
}

// Header notice for the muted lanes: "NO SAFE FILE: BASS, PAD in F# DORIAN".
inline std::string retuneNoticeText (const std::vector<std::string>& lanes, const char* key, const char* scale)
{
    if (lanes.empty())
        return {};
    std::string text = "NO SAFE FILE: ";
    for (std::size_t i = 0; i < lanes.size(); ++i)
        text += (i > 0 ? ", " : "") + lanes[i];
    text += std::string (" in ") + (key != nullptr ? key : "") + " " + (scale != nullptr ? scale : "");
    return text;
}

struct AutoKeyPick
{
    int key = 0;          // the key to play
    bool found = false;   // some key lets every pitched lane pass
    bool pending = false; // a file was still loading: decide on a later tick
    int tried = 0;
};

// Auto key. passes (key) is 1 when every pitched lane has at least one file that
// survives the guard in that key, 0 when some lane has none, -1 when a file is still
// loading. Today's key wins if it passes (so a roll with nothing failing is
// untouched); else the nearest key that does, +1 before -1 at equal distance, so
// the choice is deterministic. Nothing passes: today's key stays.
template <typename Passes>
AutoKeyPick pickAutoKey (int today, Passes&& passes)
{
    AutoKeyPick pick;
    today = ((today % 12) + 12) % 12;
    pick.key = today;
    for (int d = 0; d <= 6; ++d)
    {
        for (int sign : { 1, -1 })
        {
            if ((d == 0 || d == 6) && sign < 0)
                continue;
            const int k = ((today + sign * d) % 12 + 12) % 12;
            ++pick.tried;
            const int got = passes (k);
            if (got < 0)
            {
                pick.pending = true;
                pick.key = today;
                return pick;
            }
            if (got > 0)
            {
                pick.key = k;
                pick.found = true;
                return pick;
            }
        }
    }
    return pick;
}

} // namespace rolla
