#pragma once

// Timbre fingerprint of one sound, and the per-style, per-lane target regions it
// is matched against (Job G2: the kit draw by sound similarity).
// No JUCE: the plugin's index-time analysis and the rolla-fingerprint CLI share
// this code, so a reference hit and a library sample land in the same space.
//
// Analysis: 48 kHz mono, from the onset (first sample over 5% of the peak) to 1.5 s.
// Vector (kFingerprintDims): 13 MFCC means, 13 MFCC spreads, then log2 centroid,
// log2 85% rolloff, flatness, log2(1 + attack ms), log2(1 + decay to -20 / -40 dB ms),
// energy share under 120 Hz, and RMS level (dB). Pitch is kept beside the vector,
// not in it: key fit is a pairing rule, not a timbre distance.

#include "PitchProbe.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace rolla
{

inline constexpr int kMfcc = 13;
inline constexpr int kFingerprintDims = 2 * kMfcc + 8;
inline constexpr double kFingerprintRate = 48000.0;

// Vector order, as written to fp.json "f" and genre_targets.json "features".
inline const std::string& fingerprintName (int dim)
{
    static const std::vector<std::string> names = []
    {
        std::vector<std::string> out;
        for (int i = 0; i < kMfcc; ++i)
            out.push_back ("mfcc_mean_" + std::to_string (i));
        for (int i = 0; i < kMfcc; ++i)
            out.push_back ("mfcc_std_" + std::to_string (i));
        for (const char* t : { "log2_centroid_hz", "log2_rolloff85_hz", "flatness", "log2_attack_ms",
                               "log2_decay20_ms", "log2_decay40_ms", "sub_ratio", "rms_db" })
            out.push_back (t);
        return out;
    }();
    static const std::string none;
    return dim >= 0 && dim < (int) names.size() ? names[(size_t) dim] : none;
}

struct Fingerprint
{
    bool valid = false;
    float mfccMean[kMfcc] {};
    float mfccStd[kMfcc] {};
    float centroidHz = 0.f;
    float rolloffHz = 0.f;
    float flatness = 0.f;
    float attackMs = 0.f;
    float decay20Ms = 0.f;
    float decay40Ms = 0.f;
    float subRatio = 0.f;
    float rmsDb = -120.f;
    PitchInfo pitch;

    std::vector<float> vector() const
    {
        std::vector<float> v ((size_t) kFingerprintDims);
        for (int i = 0; i < kMfcc; ++i)
        {
            v[(size_t) i] = mfccMean[i];
            v[(size_t) (kMfcc + i)] = mfccStd[i];
        }
        float* t = v.data() + 2 * kMfcc;
        t[0] = std::log2 (std::max (20.f, centroidHz));
        t[1] = std::log2 (std::max (20.f, rolloffHz));
        t[2] = flatness;
        t[3] = std::log2 (1.f + attackMs);
        t[4] = std::log2 (1.f + decay20Ms);
        t[5] = std::log2 (1.f + decay40Ms);
        t[6] = subRatio;
        t[7] = rmsDb;
        return v;
    }
};

namespace fingerprint
{

inline double melOf (double hz) noexcept
{
    return 2595.0 * std::log10 (1.0 + hz / 700.0);
}

inline double hzOfMel (double mel) noexcept
{
    return 700.0 * (std::pow (10.0, mel / 2595.0) - 1.0);
}

} // namespace fingerprint

// mono at kFingerprintRate (callers resample). Invalid when silent or under 10 ms.
inline Fingerprint fingerprintOf (const float* mono, int frames, double rate = kFingerprintRate)
{
    using namespace fingerprint;
    Fingerprint fp;
    if (mono == nullptr || frames < (int) (0.01 * rate) || rate < 8000.0)
        return fp;
    float peak = 0.f;
    for (int i = 0; i < frames; ++i)
        peak = std::max (peak, std::fabs (mono[i]));
    if (peak < 1.0e-5f)
        return fp;
    int onset = 0;
    while (onset < frames && std::fabs (mono[onset]) < peak * 0.05f)
        ++onset;
    const int end = std::min (frames, onset + (int) (1.5 * rate));
    const int len = end - onset;
    if (len < (int) (0.01 * rate))
        return fp;
    const float* x = mono + onset;

    // Envelope in 1 ms hops: attack to the peak, decay to -20 / -40 dB after it.
    const int hop = std::max (1, (int) (0.001 * rate));
    std::vector<float> env;
    for (int i = 0; i < len; i += hop)
    {
        float m = 0.f;
        for (int j = i; j < std::min (len, i + hop); ++j)
            m = std::max (m, std::fabs (x[j]));
        env.push_back (m);
    }
    const int peakAt = (int) (std::max_element (env.begin(), env.end()) - env.begin());
    const float envPeak = std::max (1.0e-9f, env[(size_t) peakAt]);
    const float hopMs = 1000.f * (float) hop / (float) rate;
    fp.attackMs = (float) peakAt * hopMs;
    auto decayTo = [&] (float db)
    {
        const float level = envPeak * std::pow (10.f, db / 20.f);
        for (int i = peakAt; i < (int) env.size(); ++i)
            if (env[(size_t) i] < level)
                return (float) (i - peakAt) * hopMs;
        return (float) ((int) env.size() - peakAt) * hopMs;
    };
    fp.decay20Ms = decayTo (-20.f);
    fp.decay40Ms = decayTo (-40.f);
    {
        const int n = std::min (len, (int) (0.3 * rate));
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
            sum += (double) x[i] * (double) x[i];
        fp.rmsDb = (float) (10.0 * std::log10 (std::max (1.0e-12, sum / std::max (1, n))));
    }

    // STFT: 2048 / 512, Hann. 40 mel bands 30 Hz - 16 kHz, MFCC by DCT-II.
    const int N = 2048, step = 512, nb = N / 2 + 1, nMel = 40;
    const double binHz = rate / N;
    std::vector<std::vector<float>> mel ((size_t) nMel, std::vector<float> ((size_t) nb, 0.f));
    {
        const double lo = melOf (30.0), hi = melOf (std::min (16000.0, rate * 0.5 - binHz));
        std::vector<double> edge ((size_t) nMel + 2);
        for (int m = 0; m < nMel + 2; ++m)
            edge[(size_t) m] = hzOfMel (lo + (hi - lo) * m / (nMel + 1));
        for (int m = 0; m < nMel; ++m)
            for (int k = 0; k < nb; ++k)
            {
                const double f = k * binHz, a = edge[(size_t) m], b = edge[(size_t) m + 1], c = edge[(size_t) m + 2];
                double w = 0.0;
                if (f > a && f <= b)
                    w = (f - a) / (b - a);
                else if (f > b && f < c)
                    w = (c - f) / (c - b);
                mel[(size_t) m][(size_t) k] = (float) w;
            }
    }
    std::vector<std::complex<double>> buf ((size_t) N);
    std::vector<double> power ((size_t) nb);
    std::vector<std::vector<double>> ceps;
    std::vector<double> weights;
    double cSum = 0.0, rSum = 0.0, flatSum = 0.0, wSum = 0.0, subE = 0.0, allE = 0.0;
    const int kLo = std::max (1, (int) (30.0 / binHz)), kHi = std::min (nb - 1, (int) (16000.0 / binHz));
    for (int start = 0; start < len; start += step)
    {
        for (int i = 0; i < N; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * pitchprobe::kPi * i / (N - 1));
            buf[(size_t) i] = { start + i < len ? (double) x[start + i] * w : 0.0, 0.0 };
        }
        pitchprobe::fft (buf);
        double e = 0.0;
        for (int k = 0; k < nb; ++k)
        {
            power[(size_t) k] = std::norm (buf[(size_t) k]);
            e += power[(size_t) k];
        }
        if (e < 1.0e-10)
            continue;
        double c = 0.0, band = 0.0, logSum = 0.0;
        for (int k = kLo; k <= kHi; ++k)
        {
            c += k * binHz * power[(size_t) k];
            band += power[(size_t) k];
            logSum += std::log (power[(size_t) k] + 1.0e-12);
            if (k * binHz < 120.0)
                subE += power[(size_t) k];
        }
        allE += band;
        double acc = 0.0, roll = kHi * binHz;
        for (int k = kLo; k <= kHi; ++k)
        {
            acc += power[(size_t) k];
            if (acc >= 0.85 * band)
            {
                roll = k * binHz;
                break;
            }
        }
        const double meanP = band / (kHi - kLo + 1);
        const double flat = meanP > 0.0 ? std::exp (logSum / (kHi - kLo + 1)) / meanP : 0.0;
        if (band > 0.0)
        {
            cSum += (c / band) * e;
            rSum += roll * e;
            flatSum += flat * e;
            wSum += e;
        }
        std::vector<double> logMel ((size_t) nMel);
        for (int m = 0; m < nMel; ++m)
        {
            double s = 0.0;
            for (int k = 0; k < nb; ++k)
                s += mel[(size_t) m][(size_t) k] * power[(size_t) k];
            logMel[(size_t) m] = std::log (s + 1.0e-10);
        }
        std::vector<double> cc ((size_t) kMfcc);
        for (int q = 0; q < kMfcc; ++q)
        {
            double s = 0.0;
            for (int m = 0; m < nMel; ++m)
                s += logMel[(size_t) m] * std::cos (pitchprobe::kPi * q * (m + 0.5) / nMel);
            cc[(size_t) q] = s * std::sqrt (2.0 / nMel);
        }
        ceps.push_back (cc);
        weights.push_back (e);
    }
    if (ceps.empty() || wSum <= 0.0)
        return fp;
    // Frames 40 dB under the loudest do not count toward the MFCC stats.
    const double top = *std::max_element (weights.begin(), weights.end());
    int counted = 0;
    for (int q = 0; q < kMfcc; ++q)
    {
        double s = 0.0, s2 = 0.0;
        counted = 0;
        for (size_t f = 0; f < ceps.size(); ++f)
            if (weights[f] >= top * 1.0e-4)
            {
                s += ceps[f][(size_t) q];
                s2 += ceps[f][(size_t) q] * ceps[f][(size_t) q];
                ++counted;
            }
        const double mean = s / std::max (1, counted);
        fp.mfccMean[q] = (float) mean;
        fp.mfccStd[q] = (float) std::sqrt (std::max (0.0, s2 / std::max (1, counted) - mean * mean));
    }
    fp.centroidHz = (float) (cSum / wSum);
    fp.rolloffHz = (float) (rSum / wSum);
    fp.flatness = (float) (flatSum / wSum);
    fp.subRatio = allE > 0.0 ? (float) (subE / allE) : 0.f;
    fp.pitch = probePitch (x, nullptr, len, rate);
    fp.valid = true;
    return fp;
}

// ---- Target regions ---------------------------------------------------------

struct TargetCluster
{
    std::vector<float> centroid; // normalised units
    std::vector<float> spread;   // per dimension, normalised units
    float weight = 1.f;
};

struct LaneTarget
{
    std::vector<TargetCluster> clusters;
    float threshold = 2.5f;
};

// Feature normalisation shared by targets and library samples.
struct FeatureNorm
{
    std::vector<float> mean, std;

    std::vector<float> apply (const std::vector<float>& v) const
    {
        std::vector<float> out (v.size());
        for (size_t i = 0; i < v.size(); ++i)
        {
            const float m = i < mean.size() ? mean[i] : 0.f;
            const float s = i < std.size() && std[i] > 1.0e-6f ? std[i] : 1.f;
            out[i] = (v[i] - m) / s;
        }
        return out;
    }

    static FeatureNorm of (const std::vector<std::vector<float>>& xs)
    {
        FeatureNorm n;
        if (xs.empty())
            return n;
        const size_t d = xs[0].size();
        n.mean.assign (d, 0.f);
        n.std.assign (d, 1.f);
        for (size_t i = 0; i < d; ++i)
        {
            double s = 0.0, s2 = 0.0;
            for (const auto& x : xs)
            {
                s += x[i];
                s2 += (double) x[i] * x[i];
            }
            const double m = s / xs.size();
            n.mean[i] = (float) m;
            n.std[i] = (float) std::max (1.0e-3, std::sqrt (std::max (0.0, s2 / xs.size() - m * m)));
        }
        return n;
    }
};

// sqrt(mean(((x - c) / spread)^2)).
inline float clusterDistance (const std::vector<float>& x, const TargetCluster& c) noexcept
{
    if (c.centroid.empty() || x.size() != c.centroid.size())
        return 1.0e9f;
    double s = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double sp = i < c.spread.size() ? std::max (0.05f, c.spread[i]) : 1.0;
        const double d = (x[i] - c.centroid[i]) / sp;
        s += d * d;
    }
    return (float) std::sqrt (s / x.size());
}

inline float targetDistance (const std::vector<float>& x, const LaneTarget& t) noexcept
{
    float best = 1.0e9f;
    for (const auto& c : t.clusters)
        best = std::min (best, clusterDistance (x, c));
    return best;
}

// Seeded k-means++ on normalised vectors. k shrinks so every cluster can hold
// about 8 points. Spread floors at 0.25 (a tight cluster still has a region),
// threshold is the 90th percentile of the points' own distances, at least 1.
inline LaneTarget fitTarget (const std::vector<std::vector<float>>& xs, int k, std::uint32_t seed = 0x5eedu)
{
    LaneTarget t;
    const int n = (int) xs.size();
    if (n == 0)
        return t;
    const size_t d = xs[0].size();
    k = std::clamp (std::min (k, n / 8), 1, 8);
    auto rnd = [&seed]
    {
        seed = seed * 1664525u + 1013904223u;
        return (double) (seed >> 8) / 16777216.0;
    };
    auto dist2 = [d] (const std::vector<float>& a, const std::vector<float>& b)
    {
        double s = 0.0;
        for (size_t i = 0; i < d; ++i)
            s += (double) (a[i] - b[i]) * (a[i] - b[i]);
        return s;
    };
    std::vector<std::vector<float>> centres;
    centres.push_back (xs[(size_t) std::min (n - 1, (int) (rnd() * n))]);
    while ((int) centres.size() < k)
    {
        std::vector<double> dd ((size_t) n);
        double total = 0.0;
        for (int i = 0; i < n; ++i)
        {
            double best = 1.0e300;
            for (const auto& c : centres)
                best = std::min (best, dist2 (xs[(size_t) i], c));
            dd[(size_t) i] = best;
            total += best;
        }
        double r = rnd() * total;
        int pick = n - 1;
        for (int i = 0; i < n; ++i)
            if ((r -= dd[(size_t) i]) <= 0.0)
            {
                pick = i;
                break;
            }
        centres.push_back (xs[(size_t) pick]);
    }
    std::vector<int> of ((size_t) n, 0);
    for (int iter = 0; iter < 30; ++iter)
    {
        bool moved = false;
        for (int i = 0; i < n; ++i)
        {
            int best = 0;
            for (int c = 1; c < k; ++c)
                if (dist2 (xs[(size_t) i], centres[(size_t) c]) < dist2 (xs[(size_t) i], centres[(size_t) best]))
                    best = c;
            moved = moved || of[(size_t) i] != best;
            of[(size_t) i] = best;
        }
        for (int c = 0; c < k; ++c)
        {
            std::vector<double> s (d, 0.0);
            int m = 0;
            for (int i = 0; i < n; ++i)
                if (of[(size_t) i] == c)
                {
                    for (size_t j = 0; j < d; ++j)
                        s[j] += xs[(size_t) i][j];
                    ++m;
                }
            if (m > 0)
                for (size_t j = 0; j < d; ++j)
                    centres[(size_t) c][j] = (float) (s[j] / m);
        }
        if (! moved && iter > 0)
            break;
    }
    for (int c = 0; c < k; ++c)
    {
        TargetCluster tc;
        tc.centroid = centres[(size_t) c];
        tc.spread.assign (d, 1.f);
        int m = 0;
        std::vector<double> s2 (d, 0.0);
        for (int i = 0; i < n; ++i)
            if (of[(size_t) i] == c)
            {
                for (size_t j = 0; j < d; ++j)
                    s2[j] += (double) (xs[(size_t) i][j] - tc.centroid[j]) * (xs[(size_t) i][j] - tc.centroid[j]);
                ++m;
            }
        if (m == 0)
            continue;
        if (m >= 3)
            for (size_t j = 0; j < d; ++j)
                tc.spread[j] = (float) std::max (0.25, std::sqrt (s2[j] / m));
        tc.weight = (float) m / (float) n;
        t.clusters.push_back (tc);
    }
    std::vector<float> own;
    for (const auto& x : xs)
        own.push_back (targetDistance (x, t));
    std::sort (own.begin(), own.end());
    t.threshold = std::max (1.f, own[(size_t) std::min (n - 1, (int) std::floor (0.9 * (n - 1) + 0.5))]);
    return t;
}

} // namespace rolla
