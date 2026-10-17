// Offline check for the 909 models. Not part of the plugin build.
//   g++ -std=c++17 -O2 -Wall -Wextra -Isrc src/Drums/DrumMachine.cpp src/Drums/DrumPattern.cpp src/Drums/level_check.cpp -o /tmp/drumcheck
#include "Drums/DrumMachine.h"
#include "Drums/DrumPattern.h"

#include "DspUtil.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

namespace
{

constexpr float kSr = 48000.f;
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double process (double x) noexcept
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

Biquad kShelf()
{
    Biquad b;
    b.b0 = 1.53512485958697;
    b.b1 = -2.69169618940638;
    b.b2 = 1.19839281085285;
    b.a1 = -1.69065929318241;
    b.a2 = 0.73248077421585;
    return b;
}

Biquad kHp()
{
    Biquad b;
    b.b0 = 1.0;
    b.b1 = -2.0;
    b.b2 = 1.0;
    b.a1 = -1.99004745483398;
    b.a2 = 0.99007225036621;
    return b;
}

struct Bus
{
    rolla::DcBlock drumDc, mainDc;
    Bus()
    {
        drumDc.setCutoff (25.f, kSr);
        mainDc.setCutoff (20.f, kSr);
    }
    void process (float inL, float inR, float& oL, float& oR) noexcept
    {
        constexpr float drive = 1.f + 0.15f * 2.f;
        float l = rolla::fastTanh (inL * drive);
        float r = rolla::fastTanh (inR * drive);
        l = drumDc.process (l);
        r = rolla::flush (r);
        oL = rolla::flush (rolla::fastTanh (mainDc.process (l) * 1.05f));
        oR = rolla::flush (rolla::fastTanh (r * 1.05f));
    }
};

float stLufsMax (const std::vector<float>& l, const std::vector<float>& r)
{
    Biquad sl = kShelf(), sr = kShelf(), hl = kHp(), hr = kHp();
    std::vector<double> kl (l.size()), kr (r.size());
    for (size_t i = 0; i < l.size(); ++i)
    {
        kl[i] = hl.process (sl.process (l[i]));
        kr[i] = hr.process (sr.process (r[i]));
    }
    const int win = (int) (3.0 * kSr);
    const int hop = (int) (0.1 * kSr);
    double best = -200.0;
    if ((int) l.size() < win)
        return -200.f;
    for (int i = 0; i + win <= (int) l.size(); i += hop)
    {
        double sl2 = 0, sr2 = 0;
        for (int n = 0; n < win; ++n)
        {
            sl2 += kl[(size_t) (i + n)] * kl[(size_t) (i + n)];
            sr2 += kr[(size_t) (i + n)] * kr[(size_t) (i + n)];
        }
        const double z = (sl2 + sr2) / (double) win;
        if (z > 1.0e-20)
            best = std::max (best, -0.691 + 10.0 * std::log10 (z));
    }
    return (float) best;
}

rolla::DrumHit hitOf (int voice, int vel, float accent, int dialect = rolla::UkMin)
{
    rolla::DrumHit h;
    h.voice = voice;
    h.velocity = vel;
    h.synthKeep = 1.f;
    h.stepAccent = accent;
    h.key = 2;
    h.dialect = dialect;
    h.snareTone = 0.55f;
    h.snareSnap = 0.62f;
    h.clickScale = 1.f;
    h.hatHpScale = 1.f;
    return h;
}

void renderVoice (rolla::DrumMachine& m, int voice, int vel, float accent, bool beats2and4,
                  std::vector<float>& dry, std::vector<float>& busL, std::vector<float>& busR,
                  int dialect = rolla::UkMin)
{
    constexpr int N = 48000 * 8;
    dry.assign ((size_t) N, 0.f);
    busL.assign ((size_t) N, 0.f);
    busR.assign ((size_t) N, 0.f);
    Bus bus;
    const double sp16 = (double) kSr * (60.0 / 130.0) / 4.0;
    int next = 0;
    for (int i = 0; i < N; ++i)
    {
        const double pos = (double) i / sp16;
        while ((double) next <= pos)
        {
            const int s16 = next % 16;
            const bool kickStep = s16 == 0 || s16 == 4 || s16 == 8 || s16 == 12;
            const bool clapStep = s16 == 4 || s16 == 12;
            const bool on = beats2and4 ? clapStep : kickStep;
            if (on)
                m.trigger (hitOf (voice, vel, accent, dialect));
            ++next;
        }
        float l = 0, r = 0;
        m.process (l, r);
        dry[(size_t) i] = 0.5f * (l + r);
        bus.process (l, r, busL[(size_t) i], busR[(size_t) i]);
    }
}

float lastAbove (const std::vector<float>& x, int from, int to, float threshDb)
{
    const int w = (int) (0.005f * kSr);
    const int hop = (int) (0.0005f * kSr);
    to = std::min (to, (int) x.size() - w);
    double peak = 1.0e-12;
    for (int i = from; i < to; i += hop)
    {
        double s = 0;
        for (int n = 0; n < w; ++n)
            s += (double) x[(size_t) (i + n)] * x[(size_t) (i + n)];
        peak = std::max (peak, std::sqrt (s / w));
    }
    const double thr = peak * std::pow (10.0, threshDb / 20.0);
    float last = 0.f;
    for (int i = from; i < to; i += hop)
    {
        double s = 0;
        for (int n = 0; n < w; ++n)
            s += (double) x[(size_t) (i + n)] * x[(size_t) (i + n)];
        if (std::sqrt (s / w) >= thr)
            last = (float) i / kSr * 1000.f;
    }
    return last;
}

float freqNear (const std::vector<float>& x, float centreMs)
{
    const int a = std::max (1, (int) ((centreMs - 4.f) * 0.001f * kSr));
    const int b = std::min ((int) x.size() - 1, (int) ((centreMs + 12.f) * 0.001f * kSr));
    int prev = -1;
    float bestF = 0;
    float bestD = 1.0e9f;
    for (int i = a; i < b; ++i)
    {
        if (x[(size_t) (i - 1)] <= 0.f && x[(size_t) i] > 0.f)
        {
            if (prev > 0)
            {
                const float t = 0.5f * (float) (prev + i) / kSr * 1000.f;
                const float d = std::fabs (t - centreMs);
                if (d < bestD)
                {
                    bestD = d;
                    bestF = kSr / (float) (i - prev);
                }
            }
            prev = i;
        }
    }
    return bestF;
}

float tailF0 (const std::vector<float>& x)
{
    const int a = (int) (0.180f * kSr);
    const int b = (int) (0.320f * kSr);
    int prev = -1;
    double sum = 0;
    int n = 0;
    for (int i = a; i < b && i < (int) x.size(); ++i)
    {
        if (x[(size_t) (i - 1)] <= 0.f && x[(size_t) i] > 0.f)
        {
            if (prev > 0)
            {
                sum += (double) kSr / (double) (i - prev);
                ++n;
            }
            prev = i;
        }
    }
    return n > 0 ? (float) (sum / n) : 0.f;
}

float clickDb (const std::vector<float>& x)
{
    rolla::Svf hp;
    float full = 1.0e-9f;
    float hi = 1.0e-9f;
    const int n = std::min ((int) x.size(), (int) (0.40f * kSr));
    const int atk = (int) (0.010f * kSr);
    for (int i = 0; i < n; ++i)
    {
        full = std::max (full, std::fabs (x[(size_t) i]));
        if (i < atk)
        {
            const float y = hp.highpass (x[(size_t) i], 2000.f, 0.707f, kSr);
            hi = std::max (hi, std::fabs (y));
        }
    }
    return 20.f * std::log10 (hi / full);
}

float goertzelPeak (const std::vector<float>& x, int n, float f0, float f1)
{
    float bestF = f0, bestM = 0;
    for (float f = f0; f <= f1; f += 15.f)
    {
        const double w = 2.0 * kPi * (double) f / kSr;
        const double coeff = 2.0 * std::cos (w);
        double s0 = 0, s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i)
        {
            s0 = (double) x[(size_t) i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double m = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        if (m > bestM)
        {
            bestM = (float) m;
            bestF = f;
        }
    }
    return bestF;
}

float centroid (const std::vector<float>& x, int n)
{
    double num = 0, den = 0;
    for (float f = 800.f; f <= 16000.f; f += 40.f)
    {
        const double w = 2.0 * kPi * (double) f / kSr;
        const double coeff = 2.0 * std::cos (w);
        double s0 = 0, s1 = 0, s2 = 0;
        for (int i = 0; i < n; ++i)
        {
            s0 = (double) x[(size_t) i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        const double m = std::sqrt (std::max (0.0, s1 * s1 + s2 * s2 - coeff * s1 * s2));
        num += (double) f * m;
        den += m;
    }
    return den > 0 ? (float) (num / den) : 0.f;
}

int burstCount (const std::vector<float>& x)
{
    rolla::Svf bp;
    const int n = (int) (0.045f * kSr);
    const int w = (int) (0.0015f * kSr);
    std::vector<float> env ((size_t) n, 0.f);
    for (int i = 0; i < n; ++i)
    {
        const float y = bp.bandpass (x[(size_t) i], 1500.f, 1.2f, kSr);
        double s = 0;
        const int a = std::max (0, i - w);
        for (int k = a; k <= i; ++k)
            s += (double) y * 0; // placeholder to keep the filter warm
        (void) s;
        env[(size_t) i] = std::fabs (y);
    }
    // 1 ms RMS of the stored band energy is heavy; use a short smooth of |bp|.
    std::vector<float> sm ((size_t) n, 0.f);
    float acc = 0;
    for (int i = 0; i < n; ++i)
    {
        acc += env[(size_t) i];
        if (i >= w)
            acc -= env[(size_t) (i - w)];
        sm[(size_t) i] = acc / (float) w;
    }
    int peaks = 0;
    for (int i = 2; i < n - 2; ++i)
    {
        if (sm[(size_t) i] > sm[(size_t) (i - 1)] && sm[(size_t) i] >= sm[(size_t) (i + 1)]
            && sm[(size_t) i] > 0.15f * sm[0 + (size_t) (w + 2)])
        {
            bool tall = true;
            const int guard = (int) (0.006f * kSr);
            for (int k = std::max (0, i - guard); k < i; ++k)
                if (sm[(size_t) i] < sm[(size_t) k] * 1.4f)
                    tall = false;
            if (tall)
                ++peaks;
        }
    }
    return peaks;
}

int fails = 0;

void expect (bool ok, const char* what)
{
    if (! ok)
    {
        std::cout << "FAIL " << what << "\n";
        ++fails;
    }
}

} // namespace

int main()
{
    const float accent = std::pow (10.f, 0.6f * 8.f / 20.f);
    rolla::DrumMachine m;
    m.prepare (kSr);

    std::vector<float> dry, bl, br;
    renderVoice (m, rolla::Kick, 100, accent, false, dry, bl, br);
    const float kickL = stLufsMax (bl, br);
    std::vector<float> one ((size_t) (0.7f * kSr), 0.f);
    {
        rolla::DrumMachine s;
        s.prepare (kSr);
        s.trigger (hitOf (rolla::Kick, 100, accent));
        for (size_t i = 0; i < one.size(); ++i)
        {
            float l = 0, r = 0;
            s.process (l, r);
            one[i] = 0.5f * (l + r);
        }
    }
    const float f5 = freqNear (one, 5.f);
    const float f0 = tailF0 (one);
    const float k6 = lastAbove (one, 0, (int) one.size(), -6.f);
    const float k20 = lastAbove (one, 0, (int) one.size(), -20.f);
    const float k40 = lastAbove (one, 0, (int) one.size(), -40.f);
    const float clk = clickDb (one);
    float kPeak = 0;
    for (float x : one)
        kPeak = std::max (kPeak, std::fabs (x));
    std::cout << "kick peak " << kPeak << "\n";
    std::cout << "kick lufs " << kickL << " f5 " << f5 << " f0 " << f0
              << " dB " << k6 << " " << k20 << " " << k40 << " click " << clk << "\n";

    m.reset();
    renderVoice (m, rolla::Clap, 100, accent, true, dry, bl, br);
    const float clapL = stLufsMax (bl, br);
    std::vector<float> cone ((size_t) (0.4f * kSr), 0.f);
    {
        rolla::DrumMachine s;
        s.prepare (kSr);
        s.trigger (hitOf (rolla::Clap, 100, accent));
        for (size_t i = 0; i < cone.size(); ++i)
        {
            float l = 0, r = 0;
            s.process (l, r);
            cone[i] = 0.5f * (l + r);
        }
    }
    const float c6 = lastAbove (cone, 0, (int) cone.size(), -6.f);
    const float c12 = lastAbove (cone, 0, (int) cone.size(), -12.f);
    const float c20 = lastAbove (cone, 0, (int) cone.size(), -20.f);
    const float peakHz = goertzelPeak (cone, (int) (0.08f * kSr), 700.f, 2200.f);
    const int bursts = burstCount (cone);
    float cPeak = 0;
    for (float x : cone)
        cPeak = std::max (cPeak, std::fabs (x));
    std::cout << "clap peak " << cPeak << "\n";
    std::cout << "clap lufs " << clapL << " dB " << c6 << " " << c12 << " " << c20
              << " peak " << peakHz << " bursts " << bursts << "\n";

    // Tail still present 80–280 ms after one clap (the render harness gate).
    {
        rolla::DrumMachine one;
        one.prepare (kSr);
        one.trigger (hitOf (rolla::Clap, 120, 1.f));
        double e = 0;
        float peak = 1.0e-9f;
        const int a = (int) (0.08f * kSr);
        const int b = (int) (0.28f * kSr);
        for (int i = 0; i < b; ++i)
        {
            float l = 0, r = 0;
            one.process (l, r);
            peak = std::max (peak, std::fabs (l));
            if (i >= a)
                e += (double) l * l;
        }
        const double rms = std::sqrt (e / (double) (b - a));
        const double db = 20.0 * std::log10 (rms / peak);
        std::cout << "clap tail window " << db << " dB\n";
        expect (db >= -45.0, "clap tail");
    }

    m.reset();
    renderVoice (m, rolla::Snare, 100, accent, true, dry, bl, br);
    std::cout << "snare lufs " << stLufsMax (bl, br) << "\n";

    m.reset();
    renderVoice (m, rolla::HatO, 100, 1.f, false, dry, bl, br);
    std::vector<float> hone ((size_t) (0.35f * kSr), 0.f);
    {
        rolla::DrumMachine s;
        s.prepare (kSr);
        s.trigger (hitOf (rolla::HatO, 100, 1.f));
        for (size_t i = 0; i < hone.size(); ++i)
        {
            float l = 0, r = 0;
            s.process (l, r);
            hone[i] = l;
        }
    }
    const float oh6 = lastAbove (hone, 0, (int) hone.size(), -6.f);
    const float oh20 = lastAbove (hone, 0, (int) hone.size(), -20.f);
    const float ohC = centroid (hone, (int) (0.060f * kSr));
    std::cout << "oh lufs " << stLufsMax (bl, br) << " dB " << oh6 << " " << oh20
              << " cent " << ohC << "\n";

    m.reset();
    {
        rolla::DrumMachine h;
        h.prepare (kSr);
        h.trigger (hitOf (rolla::HatC, 100, 1.f));
        std::vector<float> x ((size_t) (0.2f * kSr), 0.f);
        for (size_t i = 0; i < x.size(); ++i)
        {
            float l = 0, r = 0;
            h.process (l, r);
            x[i] = l;
        }
        std::cout << "ch dB " << lastAbove (x, 0, (int) x.size(), -20.f)
                  << " cent " << centroid (x, (int) (0.040f * kSr)) << "\n";
    }

    std::cout << "key D " << rolla::DrumMachine::kickHzForKey (2)
              << " C " << rolla::DrumMachine::kickHzForKey (0)
              << " A " << rolla::DrumMachine::kickHzForKey (9) << "\n";

    rolla::Settings settings;
    settings.dialect = rolla::UkMin;
    settings.bars = 8;
    settings.busy = 0.40f;
    settings.stab = 0.55f;
    rolla::Groove g {};
    g.lane[rolla::Stab].count = 1;
    g.lane[rolla::Stab].notes[0].start = 7.f;
    g.lane[rolla::Stab].notes[0].length = 2.f;
    rolla::Rng rng (0x0909u);
    rolla::writeDrumPattern (g, settings, rng);
    int kicks = 0, claps = 0, opens = 0, ghostOnStab = 0, straightKick = 0;
    for (int t = 0; t < 16; ++t)
    {
        if (g.drums.vel[rolla::Kick][t])
            ++kicks;
        if (g.drums.vel[rolla::Clap][t])
            ++claps;
        if (g.drums.vel[rolla::HatO][t])
            ++opens;
    }
    ghostOnStab = g.drums.vel[rolla::Rim][7];
    for (int t = 0; t < 128; ++t)
        if (g.drums.vel[rolla::Kick][t] && (t % 2) == 0)
            ++straightKick;
    int percs = 0, percOdd = 0, gaskinPerc = 0;
    for (int t = 0; t < 16; ++t)
    {
        if (g.drums.vel[rolla::Perc][t])
        {
            ++percs;
            if ((t % 2) == 1)
                ++percOdd;
        }
    }
    {
        rolla::Settings gas = settings;
        gas.dialect = rolla::Gaskin;
        rolla::Groove gg {};
        rolla::Rng gr (0x0909u);
        rolla::writeDrumPattern (gg, gas, gr);
        for (int t = 0; t < 16; ++t)
            if (gg.drums.vel[rolla::Perc][t])
                ++gaskinPerc;
    }
    std::cout << "bar1 kick " << kicks << " clap " << claps << " oh " << opens
              << " rimOnStab " << ghostOnStab << " evenKicks " << straightKick
              << " perc " << percs << " percOdd " << percOdd << " gaskinPerc " << gaskinPerc << "\n";
    expect (kicks == 4 && claps == 2 && opens == 4, "uk grid");
    expect (percs == 16 && percOdd == 8, "perc grid");
    expect (gaskinPerc == 0, "gaskin perc off");
    expect (ghostOnStab == 0, "ghost dropped under stab");
    expect (std::fabs (rolla::DrumMachine::kickHzForKey (2) - 55.f) < 0.1f, "key follow D");

    expect (std::fabs (kickL - (-23.7f)) <= 0.5f, "kick lufs");
    expect (std::fabs (clapL - (-26.2f)) <= 1.0f, "clap lufs");
    expect (std::fabs (f0 - 55.f) < 1.5f, "kick f0");
    expect (f5 > 160.f && f5 < 210.f, "kick f@5ms");
    expect (k6 > 51.f && k6 < 69.f, "kick -6");
    expect (k20 > 165.f && k20 < 225.f, "kick -20");
    expect (k40 > 306.f && k40 < 414.f, "kick -40");
    expect (clk > -18.f && clk < -12.f, "click");
    expect (c20 > 60.f && c20 < 110.f, "clap -20");
    expect (peakHz > 1100.f && peakHz < 1500.f, "clap peak");

    {
        rolla::DrumMachine p;
        p.prepare (kSr);
        std::vector<float> one ((size_t) (0.25f * kSr), 0.f);
        p.trigger (hitOf (rolla::Perc, 100, 1.f));
        for (size_t i = 0; i < one.size(); ++i)
        {
            float l = 0, r = 0;
            p.process (l, r);
            one[i] = 0.5f * (l + r);
        }
        const float p20 = lastAbove (one, 0, (int) one.size(), -20.f);
        p.reset();
        constexpr int N = 48000 * 8;
        std::vector<float> pl (N), pr (N);
        Bus bus;
        const double sp16 = (double) kSr * (60.0 / 130.0) / 4.0;
        const float cycleDb[4] = { -5.f, -17.f, 0.f, -7.f };
        int next = 0;
        for (int i = 0; i < N; ++i)
        {
            const double pos = (double) i / sp16;
            while ((double) next <= pos)
            {
                const float db = cycleDb[next & 3];
                const int vel = std::clamp ((int) std::lround (100.f * std::pow (10.f, db / 20.f)), 1, 127);
                p.trigger (hitOf (rolla::Perc, vel, 1.f));
                ++next;
            }
            float l = 0, r = 0;
            p.process (l, r);
            bus.process (l, r, pl[(size_t) i], pr[(size_t) i]);
        }
        const float pL = stLufsMax (pl, pr);
        std::cout << "perc lufs " << pL << " dB20 " << p20 << "\n";
        expect (std::fabs (pL - (-31.3f)) <= 1.5f, "perc lufs");
        expect (p20 > 50.f && p20 < 95.f, "perc -20");
    }

    std::cout << (fails == 0 ? "PASS\n" : "FAILED\n");
    return fails == 0 ? 0 : 1;
}
