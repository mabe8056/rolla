#include "SamplePrep.h"
#include "PlayDecision.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace rolla
{
namespace
{

constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

    void setHighpass (double freq, double q, double rate) noexcept
    {
        const double w0 = 2.0 * kPi * freq / rate;
        const double cosw = std::cos (w0);
        const double sinw = std::sin (w0);
        const double alpha = sinw / (2.0 * q);
        const double inv = 1.0 / (1.0 + alpha);
        b0 = (1.0 + cosw) * 0.5 * inv;
        b1 = -(1.0 + cosw) * inv;
        b2 = b0;
        a1 = -2.0 * cosw * inv;
        a2 = (1.0 - alpha) * inv;
    }

    void setKShelf (double rate) noexcept
    {
        const double f0 = 1681.974450955533;
        const double G = 3.999843853973347;
        const double Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / rate);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        b0 = (Vh + Vb * K / Q + K * K) / a0;
        b1 = 2.0 * (K * K - Vh) / a0;
        b2 = (Vh - Vb * K / Q + K * K) / a0;
        a1 = 2.0 * (K * K - 1.0) / a0;
        a2 = (1.0 - K / Q + K * K) / a0;
    }

    void setKHighpass (double rate) noexcept
    {
        const double f0 = 38.13547087602444;
        const double Q = 0.5003270373238773;
        const double K = std::tan (kPi * f0 / rate);
        const double a0 = 1.0 + K / Q + K * K;
        b0 = 1.0 / a0;
        b1 = -2.0 / a0;
        b2 = 1.0 / a0;
        a1 = 2.0 * (K * K - 1.0) / a0;
        a2 = (1.0 - K / Q + K * K) / a0;
    }

    void run (float* x, int n) const noexcept
    {
        double s1 = 0.0, s2 = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double in = x[i];
            const double out = b0 * in + s1;
            s1 = b1 * in - a1 * out + s2;
            s2 = b2 * in - a2 * out;
            x[i] = (float) out;
        }
    }
};

void zeroPhase (const Biquad& filter, float* x, int n)
{
    filter.run (x, n);
    std::reverse (x, x + n);
    filter.run (x, n);
    std::reverse (x, x + n);
}

void removeDc (std::vector<float>& x)
{
    if (x.empty())
        return;
    double sum = 0.0;
    for (float s : x)
        sum += s;
    const float mean = (float) (sum / (double) x.size());
    for (float& s : x)
        s -= mean;
}

float peakOf (const std::vector<float>& a, const std::vector<float>& b)
{
    float peak = 0.f;
    const int n = (int) a.size();
    for (int i = 0; i < n; ++i)
    {
        peak = std::max (peak, std::fabs (a[(size_t) i]));
        if ((int) b.size() == n)
            peak = std::max (peak, std::fabs (b[(size_t) i]));
    }
    return peak;
}

float catmull (float y0, float y1, float y2, float y3, float t) noexcept
{
    const float a = -0.5f * y0 + 1.5f * y1 - 1.5f * y2 + 0.5f * y3;
    const float b = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
    const float c = -0.5f * y0 + 0.5f * y2;
    return ((a * t + b) * t + c) * t + y1;
}

float truePeak (const std::vector<float>& x)
{
    const int n = (int) x.size();
    if (n <= 0)
        return 0.f;
    float peak = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float y0 = x[(size_t) std::max (0, i - 1)];
        const float y1 = x[(size_t) i];
        const float y2 = x[(size_t) std::min (n - 1, i + 1)];
        const float y3 = x[(size_t) std::min (n - 1, i + 2)];
        peak = std::max (peak, std::fabs (y1));
        for (int k = 1; k < 4; ++k)
            peak = std::max (peak, std::fabs (catmull (y0, y1, y2, y3, k / 4.f)));
    }
    return peak;
}

void kWeight (std::vector<float>& x, double rate)
{
    if (x.empty() || rate < 1000.0)
        return;
    Biquad shelf;
    shelf.setKShelf (rate);
    shelf.run (x.data(), (int) x.size());
    Biquad hp;
    hp.setKHighpass (rate);
    hp.run (x.data(), (int) x.size());
}

float integratedLufs (const std::vector<float>& left, const std::vector<float>& right, double rate, bool mono)
{
    const int n = (int) left.size();
    if (n <= 0 || rate < 1000.0)
        return -70.f;
    std::vector<float> wl = left;
    std::vector<float> wr;
    kWeight (wl, rate);
    if (! mono && right.size() == (size_t) n)
    {
        wr = right;
        kWeight (wr, rate);
    }
    const int block = std::max (1, (int) std::lround (0.4 * rate));
    const int hop = std::max (1, (int) std::lround (0.1 * rate));
    std::vector<double> powers;
    if (n < block)
    {
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double l = wl[(size_t) i];
            const double r = mono ? 0.0 : wr[(size_t) i];
            sum += mono ? l * l : (l * l + r * r);
        }
        powers.push_back (sum / (double) n);
    }
    else
    {
        for (int start = 0; start + block <= n; start += hop)
        {
            double sum = 0.0;
            for (int i = 0; i < block; ++i)
            {
                const double l = wl[(size_t) (start + i)];
                const double r = mono ? 0.0 : wr[(size_t) (start + i)];
                sum += mono ? l * l : (l * l + r * r);
            }
            powers.push_back (sum / (double) block);
        }
    }
    if (powers.empty())
        return -70.f;

    auto loudness = [] (double power) noexcept
    {
        if (power <= 1.0e-20)
            return -200.0;
        return -0.691 + 10.0 * std::log10 (power);
    };

    double gatedPower = 0.0;
    int gated = 0;
    for (double p : powers)
    {
        if (loudness (p) > -70.0)
        {
            gatedPower += p;
            ++gated;
        }
    }
    if (gated == 0)
        return (float) loudness (powers[0]);
    const double absMean = gatedPower / (double) gated;
    const double relative = loudness (absMean) - 10.0;
    double relPower = 0.0;
    int relCount = 0;
    for (double p : powers)
    {
        if (loudness (p) > relative)
        {
            relPower += p;
            ++relCount;
        }
    }
    if (relCount == 0)
        return (float) loudness (absMean);
    return (float) loudness (relPower / (double) relCount);
}

void fadeIn (std::vector<float>& x, int n)
{
    n = std::min (n, (int) x.size());
    if (n <= 1)
        return;
    for (int i = 0; i < n; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos ((float) kPi * (float) i / (float) n);
        x[(size_t) i] *= w;
    }
}

void fadeOut (std::vector<float>& x, int n)
{
    n = std::min (n, (int) x.size());
    if (n <= 1)
        return;
    const int start = (int) x.size() - n;
    for (int i = 0; i < n; ++i)
    {
        const float w = 0.5f + 0.5f * std::cos ((float) kPi * (float) i / (float) n);
        x[(size_t) (start + i)] *= w;
    }
}

void trimFront (std::vector<float>& left, std::vector<float>& right, int cut)
{
    if (cut <= 0)
        return;
    if (cut >= (int) left.size())
        cut = std::max (0, (int) left.size() - 32);
    left.erase (left.begin(), left.begin() + cut);
    if ((int) right.size() >= cut)
        right.erase (right.begin(), right.begin() + cut);
}

int pitchClass (char c) noexcept
{
    switch (c)
    {
        case 'C': case 'c': return 0;
        case 'D': case 'd': return 2;
        case 'E': case 'e': return 4;
        case 'F': case 'f': return 5;
        case 'G': case 'g': return 7;
        case 'A': case 'a': return 9;
        case 'B': case 'b': return 11;
        default: return -1;
    }
}

float median (std::vector<float> values)
{
    if (values.empty())
        return 0.f;
    const size_t mid = values.size() / 2;
    std::nth_element (values.begin(), values.begin() + (std::ptrdiff_t) mid, values.end());
    return values[mid];
}

} // namespace

PreparedBuffer prepareSample (const float* const* channels, int numChannels, int frames, double rate)
{
    PreparedBuffer out;
    out.rate = rate > 1000.0 ? rate : 48000.0;
    if (channels == nullptr || numChannels <= 0 || frames <= 0)
    {
        out.report.silent = true;
        return out;
    }

    out.left.assign ((size_t) frames, 0.f);
    out.right.assign ((size_t) frames, 0.f);
    if (numChannels == 1)
    {
        const float* src = channels[0] != nullptr ? channels[0] : out.left.data();
        for (int i = 0; i < frames; ++i)
            out.left[(size_t) i] = out.right[(size_t) i] = src[i];
    }
    else
    {
        for (int i = 0; i < frames; ++i)
        {
            float l = 0.f, r = 0.f;
            int ln = 0, rn = 0;
            for (int c = 0; c < numChannels; ++c)
            {
                const float* src = channels[c];
                const float s = src != nullptr ? src[i] : 0.f;
                if ((c % 2) == 0)
                {
                    l += s;
                    ++ln;
                }
                else
                {
                    r += s;
                    ++rn;
                }
            }
            out.left[(size_t) i] = ln > 0 ? l / (float) ln : 0.f;
            out.right[(size_t) i] = rn > 0 ? r / (float) rn : out.left[(size_t) i];
        }
    }

    removeDc (out.left);
    removeDc (out.right);
    Biquad hp;
    hp.setHighpass (10.0, 0.70710678118, out.rate);
    zeroPhase (hp, out.left.data(), frames);
    zeroPhase (hp, out.right.data(), frames);

    const float peakBefore = peakOf (out.left, out.right);
    if (peakBefore < 1.0e-8f)
    {
        out.frames = frames;
        out.report.silent = true;
        out.report.frames = frames;
        out.report.lufs = -70.f;
        return out;
    }

    const float leadThresh = peakBefore * std::pow (10.f, -54.f / 20.f);
    int onset = 0;
    for (; onset < frames; ++onset)
    {
        if (std::fabs (out.left[(size_t) onset]) >= leadThresh
            || std::fabs (out.right[(size_t) onset]) >= leadThresh)
            break;
    }
    const int preroll = std::max (1, (int) std::lround (0.001 * out.rate));
    const int cut = std::max (0, onset - preroll);
    trimFront (out.left, out.right, cut);
    out.report.leadTrimFrames = cut;
    const int fadeInN = std::max (1, (int) std::lround (0.0005 * out.rate));
    if (cut > 0)
    {
        fadeIn (out.left, fadeInN);
        fadeIn (out.right, fadeInN);
    }

    const float lufs = integratedLufs (out.left, out.right, out.rate, numChannels == 1);
    const float samplePeak = std::max (peakOf (out.left, out.right), truePeak (out.left));
    const float tpRight = truePeak (out.right);
    const float peak = std::max (samplePeak, tpRight);
    const float tpCeil = std::pow (10.f, -6.05f / 20.f);
    float gain = std::pow (10.f, (-24.f - lufs) / 20.f);
    bool capped = false;
    if (peak > 1.0e-8f && peak * gain > tpCeil)
    {
        gain = tpCeil / peak;
        capped = true;
    }
    if (! std::isfinite (gain) || gain <= 0.f)
        gain = 1.f;
    for (float& s : out.left)
        s *= gain;
    for (float& s : out.right)
        s *= gain;
    out.report.gainDb = 20.f * std::log10 (gain);
    out.report.lufs = lufs;
    out.report.capped = capped;

    const float tailThresh = std::pow (10.f, -60.f / 20.f);
    int last = (int) out.left.size() - 1;
    while (last > 32
           && std::fabs (out.left[(size_t) last]) < tailThresh
           && std::fabs (out.right[(size_t) last]) < tailThresh)
        --last;
    const int keep = last + 1;
    if (keep < (int) out.left.size())
    {
        out.left.resize ((size_t) keep);
        out.right.resize ((size_t) keep);
    }
    const int fadeOutN = std::max (1, (int) std::lround (0.012 * out.rate));
    fadeOut (out.left, fadeOutN);
    fadeOut (out.right, fadeOutN);

    out.frames = (int) out.left.size();
    out.report.frames = out.frames;
    return out;
}

int detectRootMidi (const float* mono, int frames, double rate, bool tail)
{
    if (mono == nullptr || frames < 64 || rate < 1000.0)
        return -1;
    int begin = 0;
    int end = frames;
    const int threeSec = (int) std::min<double> (frames, rate * 3.0);
    if (tail)
        begin = std::max (0, (int) (frames * 0.6));
    else
        end = std::max (begin + 64, threeSec);
    if (end - begin < 64)
        return -1;

    const int minPeriod = std::max (2, (int) (rate / 2000.0));
    const int maxPeriod = std::min ((end - begin) / 2, (int) (rate / 40.0));
    if (maxPeriod <= minPeriod + 2)
        return -1;
    const int window = std::min (end - begin, 2048);
    const int hop = window;
    std::vector<float> voiced;
    std::vector<float> norms ((size_t) maxPeriod + 1);
    int hops = 0;
    for (int start = begin; start + window <= end; start += hop)
    {
        ++hops;
        double energy = 0.0;
        for (int i = 0; i < window; i += 2)
            energy += (double) mono[start + i] * (double) mono[start + i];
        if (energy < 1.0e-6)
            continue;
        int bestTau = minPeriod;
        float best = -1.f;
        for (int tau = minPeriod; tau <= maxPeriod; ++tau)
        {
            double corr = 0.0, e0 = 0.0, e1 = 0.0;
            const int span = window - tau;
            for (int i = 0; i < span; i += 2)
            {
                const float a = mono[start + i];
                const float b = mono[start + i + tau];
                corr += (double) a * (double) b;
                e0 += (double) a * (double) a;
                e1 += (double) b * (double) b;
            }
            const float norm = (float) (corr / std::sqrt (std::max (1.0e-18, e0 * e1)));
            norms[(size_t) tau] = norm;
            if (norm > best)
            {
                best = norm;
                bestTau = tau;
            }
        }
        if (best < 0.8f)
            continue;
        int chosen = bestTau;
        for (int tau = minPeriod + 1; tau < bestTau; ++tau)
        {
            const float n = norms[(size_t) tau];
            if (n >= 0.9f && n >= norms[(size_t) tau - 1] && n >= norms[(size_t) tau + 1] && n > best * 0.85f)
            {
                chosen = tau;
                break;
            }
        }
        voiced.push_back ((float) (rate / (double) chosen));
    }
    if (hops <= 0 || (int) voiced.size() * 4 < hops)
        return -1;
    const float f0 = median (std::move (voiced));
    if (f0 < 30.f || f0 > 4000.f)
        return -1;
    const int midi = (int) std::lround (69.0 + 12.0 * std::log2 ((double) f0 / 440.0));
    if (midi < 0 || midi > 127)
        return -1;
    return midi;
}

int midiFromFilename (const char* path) noexcept
{
    if (path == nullptr || path[0] == 0)
        return -1;
    int found = -1;
    for (int i = 0; path[i] != 0; ++i)
    {
        const bool boundary = i == 0 || path[i - 1] == '_' || path[i - 1] == '-' || path[i - 1] == ' ' || path[i - 1] == '/';
        if (! boundary)
            continue;
        const int pc = pitchClass (path[i]);
        if (pc < 0)
            continue;
        int semi = pc;
        int j = i + 1;
        if (path[j] == '#' || path[j] == 'b')
        {
            semi += path[j] == '#' ? 1 : -1;
            ++j;
        }
        else if (path[j] == 's' || path[j] == 'S')
        {
            const char after = path[j + 1];
            const bool sharp = after == 0 || after == '_' || after == '-' || after == '.' || after == ' '
                            || after == 'm' || after == 'M' || (after >= '0' && after <= '9');
            if (! sharp)
                continue;
            ++semi;
            ++j;
        }
        if (path[j] == 'm' || path[j] == 'M')
        {
            ++j;
            if ((path[j] == 'i' || path[j] == 'I') && (path[j + 1] == 'n' || path[j + 1] == 'N'))
                j += 2;
        }
        int oct = 3;
        if (path[j] >= '0' && path[j] <= '9')
        {
            oct = path[j] - '0';
            ++j;
            if (path[j] >= '0' && path[j] <= '9')
                continue;
        }
        const char tail = path[j];
        if (tail != 0 && tail != '_' && tail != '-' && tail != '.' && tail != ' ' && tail != '/')
            continue;
        int midi = (oct + 1) * 12 + semi;
        if (midi >= 0 && midi <= 127)
            found = midi;
    }
    return found;
}

float loudnessGainDb (const std::vector<float>& left, const std::vector<float>& right, double rate)
{
    if (left.empty() || rate < 1000.0)
        return 0.f;
    const bool mono = right.size() != left.size();
    const float lufs = integratedLufs (left, right, rate, mono);
    const float peak = std::max (peakOf (left, right), std::max (truePeak (left), right.empty() ? 0.f : truePeak (right)));
    const float ceiling = std::pow (10.f, kLoadTruePeakCapDb / 20.f);
    float gain = std::pow (10.f, (kLoadLufsTarget - lufs) / 20.f);
    if (peak > 1.0e-8f && peak * gain > ceiling)
        gain = ceiling / peak;
    if (! std::isfinite (gain) || gain <= 0.f)
        return 0.f;
    return 20.f * std::log10 (std::max (gain, 1.0e-6f));
}

int chooseSlotRoot (int slot, const float* mono, int frames, double rate, const char* path)
{
    if (userSlotIsDrum (slot) && slot != SlotKick)
        return 60;
    const bool kick = slot == SlotKick;
    const int detected = detectRootMidi (mono, frames, rate, kick);
    if (detected >= 0)
        return detected;
    const int named = midiFromFilename (path);
    if (named >= 0)
        return named;
    return 60;
}

} // namespace rolla
