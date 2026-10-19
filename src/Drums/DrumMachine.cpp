#include "DrumMachine.h"

#include "BridgedT.h"
#include "../DspUtil.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace rolla
{
namespace
{

constexpr float kKickOut = 0.188f;
constexpr float kClapOut = 0.214f;
constexpr float kSnareOut = 0.680f;
constexpr float kHatOut = 0.310f;
constexpr float kRideRatio = 0.1259f; // −18 dB under the open hat
constexpr float kRimOut = 0.214f;
constexpr float kTomOut = 0.037f;
constexpr float kPercOut = 0.164f;
constexpr float kClickAtMinus15 = 0.30f;

struct Noise
{
    std::uint32_t s = 1;

    Noise() noexcept = default;
    explicit Noise (std::uint32_t seed) noexcept : s (seed == 0 ? 1u : seed) {}

    float next() noexcept
    {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        if (s == 0)
            s = 1u;
        return (float) (s >> 8) * (1.f / 16777215.f) * 2.f - 1.f;
    }
};

struct Env3
{
    float g = 0.f;
    int age = 0;
    int n1 = 1, n2 = 1, n3 = 1;
    float c0 = 1.f, c1 = 1.f, c2 = 1.f, c3 = 1.f;
    bool on = false;

    static float coef (float from, float to, int n) noexcept
    {
        if (n < 1)
            return 1.f;
        from = std::max (from, 1.0e-8f);
        to = std::max (to, 1.0e-8f);
        return std::exp (std::log (to / from) / (float) n);
    }

    void start (float sr, float ms1, float db1, float ms2, float db2, float ms3, float db3, float scale) noexcept
    {
        scale = std::clamp (scale, 0.08f, 6.f);
        auto nOf = [&] (float ms) noexcept
        {
            return std::max (1, (int) (ms * scale * 0.001f * sr));
        };
        n1 = nOf (ms1);
        n2 = std::max (n1 + 1, nOf (ms2));
        n3 = std::max (n2 + 1, nOf (ms3));
        const float g1 = std::pow (10.f, db1 / 20.f);
        const float g2 = std::pow (10.f, db2 / 20.f);
        const float g3 = std::pow (10.f, db3 / 20.f);
        g = 1.f;
        c0 = coef (1.f, g1, n1);
        c1 = coef (g1, g2, n2 - n1);
        c2 = coef (g2, g3, n3 - n2);
        c3 = coef (g3, g3 * 0.001f, std::max (1, (int) (0.18f * scale * sr)));
        age = 0;
        on = true;
    }

    float tick() noexcept
    {
        if (! on)
            return 0.f;
        const float y = g;
        if (age < n1)
            g *= c0;
        else if (age < n2)
            g *= c1;
        else if (age < n3)
            g *= c2;
        else
            g *= c3;
        ++age;
        g = flush (g);
        if (g < 1.0e-5f)
        {
            g = 0.f;
            on = false;
        }
        return y;
    }
};

// Raised-cosine 0→1. A hard step into a reset highpass is a one-sample click.
struct Atk
{
    int age = 0;
    int n = 1;

    void trigger (float sr) noexcept
    {
        age = 0;
        n = std::max (8, (int) (0.002f * sr));
    }

    float next() noexcept
    {
        if (age >= n)
            return 1.f;
        const float t = (float) age / (float) n;
        ++age;
        return 0.5f * (1.f - std::cos (kPi * t));
    }
};

struct Click
{
    float env = 0.f;
    float coef = 1.f;
    int age = 0;
    int len = 1;
    Svf hp;
    Noise noise { 0xC11Cu };

    void trigger (float sr, float amp) noexcept
    {
        env = amp;
        coef = std::exp (-1.f / std::max (1.f, 0.005f * sr));
        age = 0;
        // 2 ms beater. The old burst was a unit step on sample 0; the highpass
        // turned that into a one-sample impulse on every kick.
        len = std::max (8, (int) (0.002f * sr));
        hp.reset();
    }

    float tick (float sr) noexcept
    {
        if (env < 1.0e-6f)
            return 0.f;
        float w = 0.f;
        if (age < len)
        {
            const float t = (float) age / (float) len;
            w = 0.5f * (1.f - std::cos (2.f * kPi * t));
            ++age;
        }
        // A noisy highpass on this window is a one-sample step of a few
        // percent. The hann itself is the beater.
        (void) noise;
        (void) sr;
        const float y = w * env;
        env *= coef;
        return y;
    }
};

struct KickV
{
    BridgedT body;
    Env3 env;
    Click click;
    float f0 = 55.f;
    float semis = 21.f;
    float pitch = 0.f;
    float pitchC = 1.f;
    int hold = 0;
    float fade = 1.f;
    float fadeInc = 1.f;
    float gain = 0.f;
    float last = 0.f;
    float ring = 0.f;
    bool on = false;
};

struct Tone2
{
    BridgedT a, b;
    float f1 = 180.f, f2 = 330.f;
    float env = 0.f, envC = 1.f;
    float nEnv = 0.f, nC = 1.f;
    float tone = 0.55f;
    float snap = 0.6f;
    float gain = 0.f;
    float last = 0.f;
    float ring = 0.f;
    bool on = false;
    Atk atk;
    Svf hp;
    Noise noise { 0x51Au };
};

struct ClapV
{
    bool on = false;
    int age = 0;
    int nBurst = 4;
    int burstAt[4] {};
    int burstW = 8;
    float burstAmp[4] {};
    float tail = 0.f;
    float tailC = 1.f;
    float tailAmp = 0.3f;
    int tailAt = 0;
    float gain = 0.f;
    float tune = 1.f;
    float last = 0.f;
    float ring = 0.f;
    Svf bp;
    Noise noise { 0xC1ABu };
};

struct Metal
{
    bool on = false;
    bool choking = false;
    float phase[6] {};
    float freq[6] {};
    float hpHz = 5000.f;
    float lpHz = 12000.f;
    Env3 env;
    float choke = 1.f;
    float gain = 0.f;
    float last = 0.f;
    float ring = 0.f;
    Atk atk;
    Svf hp1, hp2, lp;
    Noise noise { 0xA47u };
};

// Shaker: white noise through a 4 kHz highpass and a 14 kHz lowpass.
struct PercV
{
    Env3 env;
    Noise noise { 0x5EEu };
    Svf hp, lp;
    float hpHz = 4000.f;
    float lpHz = 14000.f;
    float gain = 0.f;
    float pan = -0.20f;
    float last = 0.f;
    float ring = 0.f;
    bool on = false;
    Atk atk;
};

struct RimTom
{
    BridgedT body;
    float hz = 1760.f;
    float semis = 0.f;
    float pitch = 0.f;
    float pitchC = 1.f;
    float env = 0.f;
    float envC = 1.f;
    float click = 0.f;
    float clickC = 1.f;
    float gain = 0.f;
    float pan = 0.f;
    float last = 0.f;
    float ring = 0.f;
    bool on = false;
    Atk atk;
    Noise noise { 0xB1Fu };
    Svf hp;
};

float dbAmp (float db) noexcept
{
    return std::pow (10.f, db / 20.f);
}

float velGain (int velocity) noexcept
{
    return std::clamp (velocity / 127.f, 0.f, 1.f);
}

void addPan (float x, float pan, float& left, float& right) noexcept
{
    const float p = std::clamp (pan, -1.f, 1.f);
    left += x * (1.f - std::max (0.f, p));
    right += x * (1.f + std::min (0.f, p));
}

float smoothed (float raw, float& last, float& ring, float ringC) noexcept
{
    const float y = flush (raw + ring);
    ring = flush (ring * ringC);
    last = y;
    return y;
}

const float kUkHat[6] = { 3743.f, 4750.f, 5974.f, 6966.f, 8493.f, 9672.f };
const float kGasHat[6] = { 3346.f, 4938.f, 5886.f, 6868.f, 9214.f, 10379.f };
const float kRideHz[6] = { 3695.f, 4915.f, 5963.f, 7178.f, 9599.f, 11462.f };
const float k808Ratio[6] = { 1.f, 1.483f, 1.800f, 2.546f, 2.630f, 3.897f };

} // namespace

struct DrumMachine::Core
{
    float sr = 48000.f;
    float ringC = 0.5f;
    float chokeC = 0.5f;
    HatBank bank = HatBank::Measured;
    std::atomic<DrumSampleSlot*> slot { nullptr };
    KickV kick;
    Tone2 snare;
    ClapV clap;
    Metal hatC, hatO, ride;
    PercV perc;
    RimTom rim, tomL, tomM;

    void prepare (float sampleRate) noexcept
    {
        sr = std::max (8000.f, sampleRate);
        ringC = std::exp (std::log (1.0e-3f) / std::max (1.f, 0.008f * sr));
        chokeC = std::exp (std::log (1.0e-3f) / std::max (1.f, 0.018f * sr));
        reset();
    }

    void reset() noexcept
    {
        kick = {};
        snare = {};
        clap = {};
        hatC = {};
        hatO = {};
        ride = {};
        perc = {};
        rim = {};
        tomL = {};
        tomM = {};
        kick.click.noise = Noise { 0xC11Cu };
        snare.noise = Noise { 0x51Au };
        clap.noise = Noise { 0xC1ABu };
        hatC.noise = Noise { 0xA47u };
        hatO.noise = Noise { 0xB58u };
        ride.noise = Noise { 0xD19u };
        perc.noise = Noise { 0x5EEu };
        rim.noise = Noise { 0xB1Fu };
        tomL.noise = Noise { 0x70Eu };
        tomM.noise = Noise { 0x71Eu };
        if (auto* s = slot.load (std::memory_order_acquire))
            s->reset();
    }

    float decayOf (const DrumHit& hit) const noexcept
    {
        const float punch = std::pow (2.f, -hit.punch * 0.7f);
        return std::clamp (hit.decayScale * hit.patchDecay * punch, 0.08f, 6.f);
    }

    // Retrigger restarts the voice, so the old waveform stops here. Hold its
    // last output and let it decay (~8 ms) under the new hit. This must happen
    // on every retrigger: the old 0.015 gate skipped quiet tails, and a long
    // kick tail under 0.015 stepped straight to zero on the next kick.
    void armRing (float& last, float& ring) const noexcept
    {
        ring = last;
    }

    void triggerKick (const DrumHit& hit) noexcept
    {
        const bool jackin = hit.dialect == Jackin;
        float hz = DrumMachine::kickHzForKey (hit.key) * hit.tuneRatio;
        hz *= std::pow (2.f, hit.tone * 0.12f);
        // The key follow sits at 46-62 Hz (near 52.8 Hz). Tune is ±12 st, so the body has to
        // be allowed about an octave either side or the knob barely moves.
        hz = std::clamp (hz, 30.f, 120.f);
        armRing (kick.last, kick.ring);
        kick.on = true;
        kick.f0 = hz;
        kick.semis = jackin ? 25.f : 21.f;
        const float tauMs = jackin ? 30.f : 25.f;
        kick.pitch = 1.f;
        kick.pitchC = std::exp (-1.f / std::max (1.f, tauMs * 0.001f * sr));
        kick.hold = (int) (0.006f * sr);
        kick.fade = 0.f;
        kick.fadeInc = 1.f / std::max (1.f, 0.0011f * sr);
        kick.body.strike (0.92f);
        const float scale = decayOf (hit);
        if (jackin)
            kick.env.start (sr, 60.f, -6.f, 110.f, -20.f, 150.f, -40.f, scale);
        else
            kick.env.start (sr, 68.f, -6.f, 242.f, -20.f, 455.f, -40.f, scale);
        float clickDb = (jackin ? -8.f : -15.f) + hit.punch * 6.f;
        if (hit.clickScale > 1.0e-4f)
            clickDb += 20.f * std::log10 (hit.clickScale);
        const float click = kClickAtMinus15 * dbAmp (clickDb - (-15.f));
        kick.click.trigger (sr, click);
        const float accent = (hit.stepAccent > 0.f) ? hit.stepAccent : 1.f;
        kick.gain = kKickOut * velGain (hit.velocity) * hit.synthKeep * hit.levelGain * accent;
    }

    void triggerSnare (const DrumHit& hit) noexcept
    {
        const float tune = hit.tuneRatio * std::pow (2.f, hit.tone * 0.25f);
        armRing (snare.last, snare.ring);
        snare.on = true;
        snare.f1 = std::clamp (180.f * tune, 80.f, 400.f);
        snare.f2 = std::clamp (330.f * tune, 120.f, 700.f);
        snare.a.strike (0.85f);
        snare.b.strike (0.72f);
        snare.tone = std::clamp (hit.snareTone + hit.tone * 0.25f, 0.f, 1.f);
        snare.snap = std::clamp (hit.snareSnap, 0.f, 1.5f);
        const float ms = 160.f * decayOf (hit);
        snare.env = 1.f;
        snare.envC = std::exp (std::log (1.0e-4f) / std::max (1.f, ms * 0.001f * sr));
        snare.nEnv = 1.f;
        snare.nC = std::exp (std::log (1.0e-4f) / std::max (1.f, ms * 0.42f * 0.001f * sr));
        snare.hp.reset();
        snare.atk.trigger (sr);
        snare.gain = kSnareOut * velGain (hit.velocity) * hit.synthKeep * hit.levelGain;
    }

    void triggerClap (const DrumHit& hit) noexcept
    {
        const bool jackin = hit.dialect == Jackin;
        if (clap.on || std::fabs (clap.last) > 1.0e-4f)
            clap.ring = clap.last;
        clap.on = true;
        clap.age = 0;
        const float ms = sr / 1000.f;
        const float space = jackin ? 10.5f : 10.f;
        clap.nBurst = jackin ? 3 : 4;
        clap.burstW = std::max (4, (int) ((jackin ? 5.5f : 6.2f) * ms));
        const float ampsK1[4] = { 1.f, 0.66f, 0.36f, 0.22f };
        const float ampsK2[4] = { 1.f, 0.78f, 0.58f, 0.f };
        for (int i = 0; i < 4; ++i)
        {
            clap.burstAt[i] = (int) (space * (float) i * ms);
            clap.burstAmp[i] = jackin ? ampsK2[i] : ampsK1[i];
        }
        clap.tailAt = clap.burstAt[clap.nBurst - 1];
        clap.tail = 0.f;
        clap.tailAmp = jackin ? 0.62f : 0.30f;
        const float tailMs = (jackin ? 90.f : 36.f) * decayOf (hit);
        clap.tailC = std::exp (-1.f / std::max (1.f, tailMs * 0.001f * sr));
        clap.bp.reset();
        clap.tune = std::clamp (hit.tuneRatio, 0.5f, 2.f);
        const float accent = (hit.stepAccent > 0.f) ? hit.stepAccent : 1.f;
        clap.gain = kClapOut * velGain (hit.velocity) * hit.synthKeep * hit.levelGain * accent;
    }

    void loadBank (Metal& m, const float* measured, float hp, float lp, float tune) noexcept
    {
        const float t = std::max (0.25f, tune);
        if (bank == HatBank::Model808)
        {
            const float base = 205.3f * t;
            for (int i = 0; i < 6; ++i)
                m.freq[i] = std::clamp (base * k808Ratio[i], 30.f, sr * 0.45f);
        }
        else
        {
            for (int i = 0; i < 6; ++i)
                m.freq[i] = std::clamp (measured[i] * t, 30.f, sr * 0.45f);
        }
        m.hpHz = std::clamp (hp, 200.f, sr * 0.42f);
        m.lpHz = std::clamp (lp, m.hpHz + 200.f, sr * 0.45f);
        for (int i = 0; i < 6; ++i)
            m.phase[i] = 0.137f * (float) i;
    }

    void triggerMetal (Metal& m, const DrumHit& hit, bool open, bool rideVoice) noexcept
    {
        const bool jackin = hit.dialect == Jackin;
        const float toneOct = (bank == HatBank::Model808) ? (hit.tone * 2.f) : (hit.tone * 0.35f);
        const float tune = hit.tuneRatio * std::pow (2.f, toneOct);
        const float hpScale = std::clamp (hit.hatHpScale, 0.5f, 2.f);
        armRing (m.last, m.ring);
        m.on = true;
        m.choking = false;
        m.choke = 1.f;
        const float* partials = rideVoice ? kRideHz : (jackin ? kGasHat : kUkHat);
        float hp = 5000.f;
        float lp = 10500.f;
        if (rideVoice)
        {
            hp = 4000.f;
            lp = 14000.f;
        }
        else if (jackin)
        {
            hp = 2000.f;
            lp = 7000.f;
        }
        loadBank (m, partials, hp * hpScale, lp, tune);
        const float scale = decayOf (hit);
        if (rideVoice)
            m.env.start (sr, 40.f, -6.f, 440.f, -20.f, 760.f, -40.f, scale);
        else if (open && jackin)
            m.env.start (sr, 50.f, -6.f, 230.f, -20.f, 420.f, -40.f, scale);
        else if (open)
            m.env.start (sr, 15.f, -6.f, 135.f, -20.f, 180.f, -30.f, scale);
        else if (jackin)
            m.env.start (sr, 25.f, -6.f, 60.f, -20.f, 110.f, -40.f, scale);
        else
            m.env.start (sr, 13.f, -6.f, 45.f, -20.f, 85.f, -40.f, scale);
        const float out = (rideVoice ? kHatOut * kRideRatio : kHatOut);
        m.atk.trigger (sr);
        m.gain = out * velGain (hit.velocity) * hit.synthKeep * hit.levelGain;
    }

    void triggerPerc (const DrumHit& hit) noexcept
    {
        armRing (perc.last, perc.ring);
        perc.on = true;
        const float tone = std::pow (2.f, hit.tone * 0.35f);
        const float hpScale = std::clamp (hit.hatHpScale, 0.5f, 2.f);
        perc.hpHz = std::clamp (4000.f * hpScale * tone, 800.f, sr * 0.35f);
        perc.lpHz = std::clamp (14000.f * tone, perc.hpHz + 500.f, sr * 0.47f);
        perc.env.start (sr, 20.f, -6.f, 39.f, -12.f, 70.f, -20.f, decayOf (hit));
        perc.hp.reset();
        perc.lp.reset();
        perc.atk.trigger (sr);
        perc.pan = -0.20f;
        perc.gain = kPercOut * velGain (hit.velocity) * hit.synthKeep * hit.levelGain;
    }

    void triggerRimTom (RimTom& v, const DrumHit& hit, float hz, float semis, float t20, float pan, float out) noexcept
    {
        armRing (v.last, v.ring);
        v.on = true;
        v.hz = std::clamp (hz * hit.tuneRatio * std::pow (2.f, hit.tone * 0.2f), 40.f, sr * 0.4f);
        v.semis = semis;
        v.pitch = semis > 0.f ? 1.f : 0.f;
        v.pitchC = std::exp (-1.f / std::max (1.f, 0.040f * sr));
        v.body.strike (0.8f);
        const float ms = t20 * decayOf (hit);
        v.env = 1.f;
        v.envC = std::exp (std::log (0.1f) / std::max (1.f, ms * 0.001f * sr));
        v.click = 0.22f;
        v.clickC = std::exp (-1.f / std::max (1.f, 0.004f * sr));
        v.hp.reset();
        v.atk.trigger (sr);
        v.pan = pan;
        v.gain = out * velGain (hit.velocity) * hit.synthKeep * hit.levelGain;
    }

    float renderKick() noexcept
    {
        if (! kick.on && std::fabs (kick.ring) < 1.0e-7f)
            return 0.f;
        float raw = 0.f;
        if (kick.on)
        {
            const float st = kick.semis * kick.pitch;
            const float hz = kick.f0 * std::pow (2.f, st / 12.f);
            const float body = kick.body.tick (hz, sr) * kick.env.tick();
            if (kick.hold > 0)
                --kick.hold;
            else
                kick.pitch *= kick.pitchC;
            kick.fade = std::min (1.f, kick.fade + kick.fadeInc);
            // Raised cosine, 1.1 ms. The strike leaves the body at its peak, so
            // a linear ramp still opened on a visible first-sample step.
            const float fadeG = kick.fade >= 1.f ? 1.f : 0.5f * (1.f - std::cos (kPi * kick.fade));
            raw = (body * fadeG + kick.click.tick (sr)) * kick.gain;
            if (! kick.env.on && kick.click.env < 1.0e-5f)
                kick.on = false;
        }
        return smoothed (raw, kick.last, kick.ring, ringC);
    }

    float renderSnare() noexcept
    {
        if (! snare.on && std::fabs (snare.ring) < 1.0e-7f)
            return 0.f;
        float raw = 0.f;
        if (snare.on)
        {
            const float a = snare.a.tick (snare.f1, sr);
            const float b = snare.b.tick (snare.f2, sr);
            const float tone = a * (1.f - snare.tone) + b * snare.tone;
            const float nz = snare.hp.highpass (snare.noise.next(), 1800.f, 0.75f, sr);
            raw = (tone * snare.env + nz * snare.nEnv * snare.snap) * snare.gain * snare.atk.next();
            snare.env *= snare.envC;
            snare.nEnv *= snare.nC;
            if (snare.env < 1.0e-5f && snare.nEnv < 1.0e-5f)
                snare.on = false;
        }
        return smoothed (raw, snare.last, snare.ring, ringC);
    }

    float renderClap() noexcept
    {
        if (! clap.on && std::fabs (clap.ring) < 1.0e-7f)
            return 0.f;
        float raw = 0.f;
        if (clap.on)
        {
            auto hann = [] (int age, int at, int w) noexcept
            {
                if (w < 2 || age < at || age >= at + w)
                    return 0.f;
                const float t = (float) (age - at) / (float) w;
                return 0.5f * (1.f - std::cos (2.f * kPi * t));
            };
            float env = 0.f;
            for (int i = 0; i < clap.nBurst; ++i)
                env += hann (clap.age, clap.burstAt[i], clap.burstW) * clap.burstAmp[i];
            if (clap.age >= clap.tailAt)
            {
                if (clap.tail <= 0.f)
                    clap.tail = clap.tailAmp;
                float fade = 1.f;
                const int w = std::max (1, clap.burstW);
                if (clap.age < clap.tailAt + w)
                {
                    const float t = (float) (clap.age - clap.tailAt) / (float) w;
                    fade = 0.5f * (1.f - std::cos (kPi * t));
                }
                env += clap.tail * fade;
                clap.tail *= clap.tailC;
            }
            const bool jackin = clap.nBurst == 3;
            const float fc = (jackin ? 1500.f : 1300.f) * std::max (0.5f, clap.tune);
            const float nz = clap.bp.bandpass (clap.noise.next() * 4.28f, fc, 1.7f, sr);
            raw = nz * env * clap.gain;
            ++clap.age;
            const int end = clap.burstAt[clap.nBurst - 1] + clap.burstW;
            if (clap.age > end && clap.tail < 0.001f)
                clap.on = false;
        }
        const float y = flush (raw + clap.ring);
        clap.ring = flush (clap.ring * ringC);
        clap.last = y;
        return y;
    }

    float renderMetal (Metal& m) noexcept
    {
        if (! m.on && std::fabs (m.ring) < 1.0e-7f)
            return 0.f;
        float raw = 0.f;
        if (m.on)
        {
            float x = 0.f;
            for (int i = 0; i < 6; ++i)
            {
                const float dt = std::min (m.freq[i] / sr, 0.45f);
                x += blepSquare (m.phase[i], dt);
                advancePhase (m.phase[i], dt);
            }
            x = x * (1.f / 6.f) + m.noise.next() * 0.251189f;
            float y = m.hp1.highpass (x, m.hpHz, 1.30656f, sr);
            y = m.hp2.highpass (y, m.hpHz, 0.541196f, sr);
            y = m.lp.lowpass (y, m.lpHz, 0.7071f, sr);
            if (m.choking)
            {
                m.choke *= chokeC;
                if (m.choke < 1.0e-4f)
                    m.on = false;
            }
            raw = y * m.env.tick() * m.choke * m.gain * m.atk.next();
            if (! m.env.on)
                m.on = false;
        }
        return smoothed (raw, m.last, m.ring, ringC);
    }

    float renderPerc() noexcept
    {
        if (! perc.on && std::fabs (perc.ring) < 1.0e-7f)
            return 0.f;
        float raw = 0.f;
        if (perc.on)
        {
            const float nz = perc.lp.lowpass (perc.hp.highpass (perc.noise.next(), perc.hpHz, 0.707f, sr),
                                              perc.lpHz, 0.707f, sr);
            raw = nz * perc.env.tick() * perc.gain * perc.atk.next();
            if (! perc.env.on)
                perc.on = false;
        }
        return smoothed (raw, perc.last, perc.ring, ringC);
    }

    float renderRimTom (RimTom& v) noexcept
    {
        if (! v.on && std::fabs (v.ring) < 1.0e-7f)
            return 0.f;
        float raw = 0.f;
        if (v.on)
        {
            const float hz = v.hz * std::pow (2.f, (v.semis * v.pitch) / 12.f);
            const float body = v.body.tick (hz, sr) * v.env;
            const float clk = v.hp.highpass (v.noise.next(), 2500.f, 0.7f, sr) * v.click;
            raw = (body + clk * 0.25f) * v.gain * v.atk.next();
            v.pitch *= v.pitchC;
            v.env *= v.envC;
            v.click *= v.clickC;
            if (v.env < 1.0e-5f)
                v.on = false;
        }
        return smoothed (raw, v.last, v.ring, ringC);
    }
};

DrumMachine::DrumMachine() : core (std::make_unique<Core>()) {}
DrumMachine::~DrumMachine() = default;

void DrumMachine::prepare (float sampleRate) noexcept
{
    core->prepare (sampleRate);
    if (auto* s = core->slot.load (std::memory_order_acquire))
        s->prepare (sampleRate);
}

void DrumMachine::reset() noexcept { core->reset(); }

void DrumMachine::setHatBank (HatBank bank) noexcept { core->bank = bank; }

void DrumMachine::setSampleSlot (DrumSampleSlot* slot) noexcept
{
    core->slot.store (slot, std::memory_order_release);
    if (slot != nullptr)
        slot->prepare (core->sr);
}

DrumSampleSlot* DrumMachine::sampleSlot() const noexcept
{
    return core->slot.load (std::memory_order_acquire);
}

float DrumMachine::kickHzForKey (int key) noexcept
{
    key = (key % 12 + 12) % 12;
    const int degree[3] = { 0, 5, 7 };
    float bestHz = 55.f;
    float bestDist = 1.0e9f;
    int bestDeg = -1;
    // Pass 0: the preferred 49-56 Hz window. Keys F, F#, A# and B have no
    // root / 4th / 5th there, so pass 1 widens to 40-70 Hz (still nearest
    // 52.8 Hz) instead of dropping to a 55 Hz that is out of key.
    const float lo[2] = { 48.95f, 40.f };
    const float hi[2] = { 56.05f, 70.f };
    for (int pass = 0; pass < 2 && bestDeg < 0; ++pass)
    {
        for (int midi = 24; midi <= 48; ++midi)
        {
            const int pc = midi % 12;
            int deg = -1;
            for (int i = 0; i < 3; ++i)
                if (pc == (key + degree[i]) % 12)
                    deg = i;
            if (deg < 0)
                continue;
            const float hz = 440.f * std::pow (2.f, (midi - 69) / 12.f);
            if (hz < lo[pass] || hz > hi[pass])
                continue;
            const float dist = std::fabs (hz - 52.8f);
            const bool closer = dist + 0.03f < bestDist;
            const bool tieToFifth = std::fabs (dist - bestDist) <= 0.05f && deg == 2 && bestDeg != 2;
            if (bestDeg < 0 || closer || tieToFifth)
            {
                bestHz = hz;
                bestDist = dist;
                bestDeg = deg;
            }
        }
    }
    return bestHz;
}

void DrumMachine::trigger (const DrumHit& hit) noexcept
{
    if (hit.voice < 0 || hit.voice >= NumDrumVoices || hit.velocity <= 0 || hit.synthKeep <= 0.001f)
        return;
    switch (hit.voice)
    {
        case Kick: core->triggerKick (hit); break;
        case Snare: core->triggerSnare (hit); break;
        case Clap: core->triggerClap (hit); break;
        case Rim:
            core->triggerRimTom (core->rim, hit, 1760.f, 0.f, 150.f, 0.10f, kRimOut);
            break;
        case TomLow:
        {
            const bool jackin = hit.dialect == Jackin;
            core->triggerRimTom (core->tomL, hit, jackin ? 196.f : 146.83f, 5.f,
                                 jackin ? 160.f : 200.f, -0.30f, kTomOut);
            break;
        }
        case TomMid:
        {
            const bool jackin = hit.dialect == Jackin;
            core->triggerRimTom (core->tomM, hit, jackin ? 246.94f : 196.f, 5.f,
                                 jackin ? 140.f : 170.f, -0.18f, kTomOut);
            break;
        }
        case HatC:
            core->hatO.choking = true;
            core->triggerMetal (core->hatC, hit, false, false);
            break;
        case HatO:
            core->triggerMetal (core->hatO, hit, true, false);
            break;
        case Ride:
            core->triggerMetal (core->ride, hit, true, true);
            break;
        case Perc:
            core->triggerPerc (hit);
            break;
        default: break;
    }
}

void DrumMachine::process (float& left, float& right) noexcept
{
    process (left, right, nullptr, nullptr);
}

void DrumMachine::process (float& left, float& right, float* voiceL, float* voiceR) noexcept
{
    const bool split = voiceL != nullptr && voiceR != nullptr;
    auto take = [&] (int voice, float x, float pan)
    {
        float l = 0.f, r = 0.f;
        addPan (x, pan, l, r);
        if (split)
        {
            voiceL[voice] = l;
            voiceR[voice] = r;
        }
        else
        {
            left += l;
            right += r;
        }
    };
    take (Kick, core->renderKick(), 0.f);
    take (Snare, core->renderSnare(), 0.f);
    take (Clap, core->renderClap(), 0.f);
    take (Rim, core->renderRimTom (core->rim), core->rim.pan);
    take (TomLow, core->renderRimTom (core->tomL), core->tomL.pan);
    take (TomMid, core->renderRimTom (core->tomM), core->tomM.pan);
    take (Perc, core->renderPerc(), core->perc.pan);
    take (HatC, core->renderMetal (core->hatC), 0.f);
    take (HatO, core->renderMetal (core->hatO), 0.f);
    take (Ride, core->renderMetal (core->ride), 0.20f);
    if (auto* s = core->slot.load (std::memory_order_acquire))
        s->render (left, right);
}

} // namespace rolla
