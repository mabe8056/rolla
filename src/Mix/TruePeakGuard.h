#pragma once

// Master true-peak safety stage (issue #13, task 2).
//
// 4x-oversampled peak detection (24-tap Kaiser windowed sinc per phase), instant
// attack / exponential release, a lookahead hold over L samples and an
// L-sample box average. The box average of a held minimum reaches the needed
// gain by the time the peak comes out of the delay line, so the output stays
// at or under the ceiling without clipping. While nothing comes near the
// ceiling the gain is exactly 1 and the output is the input, delayed.
//
// Latency is fixed per sample rate (lookahead + interpolator delay) and is
// the same with the guard off, so toggling it never moves the audio.
// prepare() allocates; process() does not. No JUCE. Header-only, so every
// target that builds Engine.cpp picks it up without a CMake change.

#include <algorithm>
#include <cmath>
#include <vector>

namespace rolla
{

// Settings mirror mix_profiles.json "tp_guard"; tests/mix/validate_profiles.py
// checks the two agree.
class TruePeakGuard
{
public:
    static constexpr float kDefaultCeilingDbtp = -6.f;
    // Internal target under the ceiling, covering what the 4x estimate misses
    // between its own sub-samples.
    static constexpr float kMarginDb = 0.2f;

    void prepare (double sampleRate, float lookaheadMs = 1.5f, float releaseMs = 80.f);
    void reset() noexcept;

    void setCeilingDbtp (float db) noexcept;
    void setEnabled (bool on) noexcept { enabled = on; }
    bool isEnabled() const noexcept { return enabled; }

    // In place. `delayL` / `delayR` (may be null) ride the same delay without
    // gain, for an aux bus that has to stay aligned with the main bus.
    void process (float* left, float* right, float* delayL, float* delayR, int numSamples) noexcept;

    int latencySamples() const noexcept { return latency; }

    // Largest gain reduction since the last call, in dB (>= 0).
    float takeMaxGrDb() noexcept;

private:
    static constexpr int kPhases = 4;
    static constexpr int kTaps = 24; // per phase; interpolator delay kTaps / 2
    static constexpr int kHalf = kTaps / 2;

    float detect (float l, float r) noexcept;

    struct Ring
    {
        std::vector<float> d;
        int w = 0;
        void assign (int n) { d.assign ((size_t) n, 0.f); w = 0; }
        void clear() noexcept { std::fill (d.begin(), d.end(), 0.f); w = 0; }
        // Push x, return the value pushed `delay` samples ago (delay < size).
        float push (float x, int delay) noexcept
        {
            const int n = (int) d.size();
            d[(size_t) w] = x;
            int r = w - delay;
            if (r < 0)
                r += n;
            w = (w + 1 == n) ? 0 : w + 1;
            return d[(size_t) r];
        }
    };

    float coeff[kPhases][kTaps] {};
    float histL[kTaps] {};
    float histR[kTaps] {};
    int histW = 0;

    // Sliding minimum over L samples (monotonic deque in fixed arrays).
    std::vector<float> dqVal;
    std::vector<long long> dqIdx;
    int dqHead = 0, dqTail = 0, dqCap = 0;
    long long sampleIndex = 0;

    // Box average over L samples of the held gain.
    std::vector<float> boxRing;
    int boxW = 0;
    double boxSum = 0.0;
    int boxBelowOne = 0;

    Ring audioL, audioR, auxL, auxR;

    double sr = 48000.0;
    int look = 72;
    int latency = 72 + kHalf;
    float releaseCoef = 0.9997f;
    float deficit = 0.f;
    float ceilingLin = 0.5f;
    float targetLin = 0.5f;
    float maxGr = 0.f;
    bool enabled = true;
};

namespace tpg_detail
{
inline constexpr double kPi = 3.14159265358979323846;

inline double besselI0 (double x) noexcept
{
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 40; ++k)
    {
        term *= (x / (2.0 * k)) * (x / (2.0 * k));
        sum += term;
        if (term < 1e-12 * sum)
            break;
    }
    return sum;
}

inline float dbToLin (float db) noexcept
{
    return std::pow (10.f, db / 20.f);
}
} // namespace tpg_detail

inline void TruePeakGuard::prepare (double sampleRate, float lookaheadMs, float releaseMs)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    look = std::max (8, (int) std::lround (lookaheadMs * 0.001 * sr));
    // Detector delay + lookahead + kHalf more, so every sample within the
    // interpolator's reach of a peak (+-kHalf) already carries the reduced gain.
    latency = kHalf + look - 1 + kHalf;
    releaseCoef = (float) std::exp (-1.0 / (std::max (1.f, releaseMs) * 0.001 * sr));

    // Sub-sample j/4 between x[c] and x[c+1] from x[c-7 .. c+8].
    constexpr double beta = 6.0;
    const double i0b = tpg_detail::besselI0 (beta);
    for (int j = 0; j < kPhases; ++j)
    {
        double sum = 0.0;
        for (int k = 0; k < kTaps; ++k)
        {
            const double t = (double) j / kPhases - (double) (k - (kHalf - 1));
            const double sinc = std::fabs (t) < 1e-12 ? 1.0 : std::sin (tpg_detail::kPi * t) / (tpg_detail::kPi * t);
            const double x = t / (kHalf + 0.5);
            const double w = std::fabs (x) >= 1.0 ? 0.0 : tpg_detail::besselI0 (beta * std::sqrt (1.0 - x * x)) / i0b;
            coeff[j][k] = (float) (sinc * w);
            sum += sinc * w;
        }
        for (int k = 0; k < kTaps; ++k)
            coeff[j][k] = (float) (coeff[j][k] / sum);
    }

    dqCap = look + 2 * kHalf + 4;
    dqVal.assign ((size_t) dqCap, 1.f);
    dqIdx.assign ((size_t) dqCap, 0);
    boxRing.assign ((size_t) look, 1.f);
    audioL.assign (latency + 1);
    audioR.assign (latency + 1);
    auxL.assign (latency + 1);
    auxR.assign (latency + 1);
    setCeilingDbtp (kDefaultCeilingDbtp);
    reset();
}

inline void TruePeakGuard::reset() noexcept
{
    std::fill (std::begin (histL), std::end (histL), 0.f);
    std::fill (std::begin (histR), std::end (histR), 0.f);
    histW = 0;
    dqHead = dqTail = 0;
    sampleIndex = 0;
    std::fill (boxRing.begin(), boxRing.end(), 1.f);
    boxW = 0;
    boxSum = (double) look;
    boxBelowOne = 0;
    audioL.clear();
    audioR.clear();
    auxL.clear();
    auxR.clear();
    deficit = 0.f;
    maxGr = 0.f;
}

inline void TruePeakGuard::setCeilingDbtp (float db) noexcept
{
    ceilingLin = tpg_detail::dbToLin (db);
    targetLin = tpg_detail::dbToLin (db - kMarginDb);
}

inline float TruePeakGuard::takeMaxGrDb() noexcept
{
    const float g = maxGr;
    maxGr = 0.f;
    return g;
}

inline float TruePeakGuard::detect (float l, float r) noexcept
{
    // Shift histories: index kTaps-1 is the newest sample.
    for (int k = 0; k < kTaps - 1; ++k)
    {
        histL[k] = histL[k + 1];
        histR[k] = histR[k + 1];
    }
    histL[kTaps - 1] = l;
    histR[kTaps - 1] = r;

    float peak = std::max (std::fabs (histL[kHalf - 1]), std::fabs (histR[kHalf - 1]));
    for (int j = 1; j < kPhases; ++j)
    {
        float yl = 0.f, yr = 0.f;
        for (int k = 0; k < kTaps; ++k)
        {
            yl += coeff[j][k] * histL[k];
            yr += coeff[j][k] * histR[k];
        }
        peak = std::max (peak, std::max (std::fabs (yl), std::fabs (yr)));
    }
    return peak;
}

inline void TruePeakGuard::process (float* left, float* right, float* delayL, float* delayR, int numSamples) noexcept
{
    // Hold covers the peak sample and kHalf samples either side of it, through the box.
    const int hold = look + 2 * kHalf;
    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i];
        const float inR = right != nullptr ? right[i] : inL;

        // Gain needed for the sub-samples centred kHalf-1 samples back.
        const float tp = detect (inL, inR);
        float need = 1.f;
        if (enabled && tp > targetLin)
            need = targetLin / tp;

        // Instant attack, exponential release toward exactly 1. The release runs on
        // the deficit (1 - gain): near unity, 1 - d * coef would round back to itself.
        deficit *= releaseCoef;
        if (deficit < 1e-4f) // within 0.001 dB: exactly 1, so quiet passages are bit-exact again
            deficit = 0.f;
        if (1.f - need > deficit)
            deficit = 1.f - need;
        const float env = 1.f - deficit;

        // Sliding minimum of env over `hold` samples.
        const long long n = sampleIndex++;
        while (dqTail != dqHead)
        {
            const int last = (dqTail - 1 + dqCap) % dqCap;
            if (dqVal[(size_t) last] < env)
                break;
            dqTail = last;
        }
        dqVal[(size_t) dqTail] = env;
        dqIdx[(size_t) dqTail] = n;
        dqTail = (dqTail + 1) % dqCap;
        while (dqIdx[(size_t) dqHead] <= n - hold)
            dqHead = (dqHead + 1) % dqCap;
        const float held = dqVal[(size_t) dqHead];

        // Box average of the held gain over `look` samples.
        const float old = boxRing[(size_t) boxW];
        boxRing[(size_t) boxW] = held;
        boxW = (boxW + 1 == look) ? 0 : boxW + 1;
        boxSum += (double) held - (double) old;
        boxBelowOne += (held < 1.f ? 1 : 0) - (old < 1.f ? 1 : 0);
        const float g = boxBelowOne == 0 ? 1.f : (float) std::min (1.0, boxSum / (double) look);
        if (boxBelowOne == 0)
            boxSum = (double) look; // keep the running sum from drifting

        const float outL = audioL.push (inL, latency);
        const float outR = audioR.push (inR, latency);
        if (g < 1.f)
        {
            const float gr = -20.f * std::log10 (g);
            maxGr = std::max (maxGr, gr);
            left[i] = outL * g;
            if (right != nullptr)
                right[i] = outR * g;
        }
        else
        {
            left[i] = outL;
            if (right != nullptr)
                right[i] = outR;
        }
        if (delayL != nullptr)
            delayL[i] = auxL.push (delayL[i], latency);
        if (delayR != nullptr)
            delayR[i] = auxR.push (delayR[i], latency);
    }
}

} // namespace rolla
