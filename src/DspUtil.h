#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace rolla
{

inline constexpr float kPi = 3.14159265358979323846f;

inline float flush (float x) noexcept
{
    return (std::fabs (x) < 1.0e-12f || ! std::isfinite (x)) ? 0.f : x;
}

inline float fastTanh (float x) noexcept
{
    x = std::clamp (x, -3.f, 3.f);
    const float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

inline float dbToGain (float db) noexcept
{
    return std::pow (10.f, db / 20.f);
}

inline float polyBlep (float t, float dt) noexcept
{
    if (dt <= 1.0e-8f)
        return 0.f;
    if (t < dt)
    {
        t /= dt;
        return t + t - t * t - 1.f;
    }
    if (t > 1.f - dt)
    {
        t = (t - 1.f) / dt;
        return t * t + t + t + 1.f;
    }
    return 0.f;
}

inline float blepSaw (float t, float dt) noexcept
{
    return (2.f * t - 1.f) - polyBlep (t, dt);
}

inline float blepSquare (float t, float dt) noexcept
{
    float y = t < 0.5f ? 1.f : -1.f;
    y += polyBlep (t, dt);
    float t2 = t + 0.5f;
    if (t2 >= 1.f)
        t2 -= 1.f;
    y -= polyBlep (t2, dt);
    return y;
}

// Band-limited pulse. pw is the high portion of the cycle, clamped to 5–95%.
inline float blepPulse (float t, float dt, float pw) noexcept
{
    pw = std::clamp (pw, 0.05f, 0.95f);
    float y = t < pw ? 1.f : -1.f;
    y += polyBlep (t, dt);
    float t2 = t - pw;
    if (t2 < 0.f)
        t2 += 1.f;
    y -= polyBlep (t2, dt);
    return y;
}

inline float sinePhase (float t) noexcept
{
    return std::sin (2.f * kPi * t);
}

inline float triPhase (float t) noexcept
{
    const float x = t < 0.5f ? t : 1.f - t;
    return x * 4.f - 1.f;
}

inline void advancePhase (float& phase, float dt) noexcept
{
    phase += dt;
    if (phase >= 1.f)
        phase -= std::floor (phase);
    if (phase < 0.f)
        phase = 0.f;
}

// Cytomic / Andrew Simper TPT state-variable filter.
struct Svf
{
    float ic1 = 0.f, ic2 = 0.f;

    void reset() noexcept { ic1 = ic2 = 0.f; }

    void process (float x, float fc, float q, float sr, float& lp, float& bp, float& hp) noexcept
    {
        fc = std::clamp (fc, 15.f, sr * 0.45f);
        q = std::max (0.15f, q);
        const float g = std::tan (kPi * fc / sr);
        const float k = 1.f / q;
        const float a1 = 1.f / (1.f + g * (g + k));
        const float a2 = g * a1;
        const float a3 = g * a2;
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = flush (2.f * v1 - ic1);
        ic2 = flush (2.f * v2 - ic2);
        lp = v2;
        bp = v1;
        hp = x - k * v1 - v2;
    }

    float lowpass (float x, float fc, float q, float sr) noexcept
    {
        float lp, bp, hp;
        process (x, fc, q, sr, lp, bp, hp);
        return lp;
    }

    float bandpass (float x, float fc, float q, float sr) noexcept
    {
        float lp, bp, hp;
        process (x, fc, q, sr, lp, bp, hp);
        return bp;
    }

    float highpass (float x, float fc, float q, float sr) noexcept
    {
        float lp, bp, hp;
        process (x, fc, q, sr, lp, bp, hp);
        return hp;
    }
};

// Mono below fc on a stereo bus. The side (L-R)/2 goes through a Linkwitz-Riley
// 24 dB/oct high-pass (two Butterworth Q 0.7071 stages); the mid (L+R)/2 is not
// filtered. Below fc both channels carry (L+R)/2, above fc the image is kept,
// and L+R is the input sum exactly at every frequency (flat, linear). No allocation.
struct MonoBelow
{
    Svf hp1, hp2;

    void reset() noexcept { hp1.reset(); hp2.reset(); }

    void process (float& l, float& r, float fc, float sr) noexcept
    {
        const float mid = 0.5f * (l + r);
        float side = 0.5f * (l - r);
        side = hp1.highpass (side, fc, 0.70710678f, sr);
        side = hp2.highpass (side, fc, 0.70710678f, sr);
        l = flush (mid + side);
        r = flush (mid - side);
    }
};

// One-pole highpass / DC blocker (R is the pole, ~0.995 at 48 kHz for ~30 Hz).
struct DcBlock
{
    float x1 = 0.f, y1 = 0.f, r = 0.995f;

    void setCutoff (float hz, float sr) noexcept
    {
        r = std::clamp (1.f - (2.f * kPi * hz / sr), 0.9f, 0.9999f);
    }

    void reset() noexcept { x1 = y1 = 0.f; }

    float process (float x) noexcept
    {
        const float y = x - x1 + r * y1;
        x1 = x;
        y1 = flush (y);
        return y;
    }
};

// 4-pole ladder. Resonance stays low (the bass patches sit at 0–10%).
// Run this inside 2x oversampling when the input is a bright oscillator plus drive.
struct Ladder
{
    float s[4] { 0, 0, 0, 0 };

    void reset() noexcept { s[0] = s[1] = s[2] = s[3] = 0.f; }

    float process (float x, float fc, float res, float sr) noexcept
    {
        fc = std::clamp (fc, 20.f, sr * 0.45f);
        res = std::clamp (res, 0.f, 0.85f);
        const float g = std::tan (kPi * fc / sr);
        const float G = g / (1.f + g);
        const float k = res * 3.2f;
        x = fastTanh (x - k * s[3]);

        auto pole = [&] (float in, float& st) noexcept
        {
            const float v = (in - st) * G;
            const float y = flush (v + st);
            st = flush (y + v);
            return y;
        };

        float y = pole (x, s[0]);
        y = pole (y, s[1]);
        y = pole (y, s[2]);
        y = pole (y, s[3]);
        return y;
    }
};

struct Lfsr
{
    uint32_t s = 0xACE1u;

    float next() noexcept
    {
        const uint32_t bit = ((s >> 0) ^ (s >> 2) ^ (s >> 3) ^ (s >> 5)) & 1u;
        s = (s >> 1) | (bit << 15);
        if (s == 0)
            s = 0xACE1u;
        return (s & 1u) ? 1.f : -1.f;
    }
};

struct Smooth
{
    float y = 0.f, target = 0.f, c = 0.01f;

    void setTime (float ms, float sr) noexcept
    {
        c = 1.f - std::exp (-1.f / std::max (1.f, ms * 0.001f * sr));
    }

    void snap (float v) noexcept { y = target = v; }

    float next() noexcept
    {
        y += (target - y) * c;
        y = flush (y);
        return y;
    }
};

// 16th-note swing. 0.50 is straight. 0.57 delays the e and a
// (the second 16th of each 8th) to 57% of that 8th.
inline float swungStart (float start16, float swing) noexcept
{
    swing = std::clamp (swing, 0.5f, 0.75f);
    const int i = (int) std::floor (start16 + 1.0e-4f);
    const float frac = start16 - (float) i;
    if ((i & 1) == 0)
        return start16;
    const float pair = (float) (i - 1);
    return pair + swing * 2.f + frac;
}

inline double wrapLoop (double x, double m) noexcept
{
    if (m <= 1.0e-9)
        return 0.0;
    double r = std::fmod (x, m);
    if (r < 0.0)
        r += m;
    return r;
}

inline bool crossed (double prev, double now, double t, double loop) noexcept
{
    if (t < 0.0 || t > loop + 1.0e-6)
        return false;
    if (now >= prev)
        return t > prev && t <= now;
    if (t >= loop - 1.0e-6)
        return true;
    return t > prev || t <= now;
}

} // namespace rolla
