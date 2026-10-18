#include "MixRuntime.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace rolla
{
namespace
{

float clampFreq (float hz, float sr) noexcept
{
    return std::clamp (hz, 15.f, sr * 0.45f);
}

float dbToLin (float db) noexcept
{
    return std::pow (10.f, db / 20.f);
}

// 4× windowed-sinc, cutoff at base Nyquist, Hamming, 32 taps. Phase sums are ~1
// so a constant passes at unity. Downsample coefficients are the same kernel at gain 1/4.
constexpr float kTpUp[4][8] = {
    { -2.521876595e-03f, 9.478050676e-03f, -3.669804799e-02f, 1.241118636e-01f, 9.749208155e-01f, -8.920659055e-02f, 2.703718712e-02f, -6.349975978e-03f },
    { -7.274272497e-03f, 3.332998791e-02f, -1.189236275e-01f, 4.445740020e-01f, 7.698216084e-01f, -1.592658091e-01f, 4.721445142e-02f, -1.024776645e-02f },
    { -1.024776645e-02f, 4.721445142e-02f, -1.592658091e-01f, 7.698216084e-01f, 4.445740020e-01f, -1.189236275e-01f, 3.332998791e-02f, -7.274272497e-03f },
    { -6.349975978e-03f, 2.703718712e-02f, -8.920659055e-02f, 9.749208155e-01f, 1.241118636e-01f, -3.669804799e-02f, 9.478050676e-03f, -2.521876595e-03f },
};
constexpr float kTpDown[32] = {
    -6.304691486e-04f, -1.818568124e-03f, -2.561941612e-03f, -1.587493994e-03f, 2.369512669e-03f, 8.332496978e-03f, 1.180361286e-02f, 6.759296779e-03f,
    -9.174511998e-03f, -2.973090689e-02f, -3.981645227e-02f, -2.230164764e-02f, 3.102796591e-02f, 1.111435005e-01f, 1.924554021e-01f, 2.437302039e-01f,
    2.437302039e-01f, 1.924554021e-01f, 1.111435005e-01f, 3.102796591e-02f, -2.230164764e-02f, -3.981645227e-02f, -2.973090689e-02f, -9.174511998e-03f,
    6.759296779e-03f, 1.180361286e-02f, 8.332496978e-03f, 2.369512669e-03f, -1.587493994e-03f, -2.561941612e-03f, -1.818568124e-03f, -6.304691486e-04f,
};

} // namespace

float MixRuntime::Biquad::process (float x) noexcept
{
    const float y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return flush (y);
}

void MixRuntime::Biquad::setBell (float freq, float q, float gainDb, float sr) noexcept
{
    if (std::fabs (gainDb) < 0.001f || freq < 10.f)
    {
        b0 = 1.f; b1 = b2 = a1 = a2 = 0.f;
        return;
    }
    const float A = std::pow (10.f, gainDb / 40.f);
    const float w0 = 2.f * kPi * clampFreq (freq, sr) / sr;
    const float cw = std::cos (w0);
    const float alpha = std::sin (w0) / (2.f * std::max (0.05f, q));
    const float a0 = 1.f + alpha / A;
    b0 = (1.f + alpha * A) / a0;
    b1 = (-2.f * cw) / a0;
    b2 = (1.f - alpha * A) / a0;
    a1 = (-2.f * cw) / a0;
    a2 = (1.f - alpha / A) / a0;
}

void MixRuntime::Biquad::setShelf (bool high, float freq, float q, float gainDb, float sr) noexcept
{
    if (std::fabs (gainDb) < 0.001f || freq < 10.f)
    {
        b0 = 1.f; b1 = b2 = a1 = a2 = 0.f;
        return;
    }
    const float A = std::pow (10.f, gainDb / 40.f);
    const float w0 = 2.f * kPi * clampFreq (freq, sr) / sr;
    const float cw = std::cos (w0);
    const float sw = std::sin (w0);
    const float S = std::max (0.2f, q);
    const float alpha = sw / 2.f * std::sqrt ((A + 1.f / A) * (1.f / S - 1.f) + 2.f);
    const float two = 2.f * std::sqrt (A) * alpha;
    float a0, bb0, bb1, bb2, aa1, aa2;
    if (high)
    {
        a0 = (A + 1.f) - (A - 1.f) * cw + two;
        bb0 = A * ((A + 1.f) + (A - 1.f) * cw + two);
        bb1 = -2.f * A * ((A - 1.f) + (A + 1.f) * cw);
        bb2 = A * ((A + 1.f) + (A - 1.f) * cw - two);
        aa1 = 2.f * ((A - 1.f) - (A + 1.f) * cw);
        aa2 = (A + 1.f) - (A - 1.f) * cw - two;
    }
    else
    {
        a0 = (A + 1.f) + (A - 1.f) * cw + two;
        bb0 = A * ((A + 1.f) - (A - 1.f) * cw + two);
        bb1 = 2.f * A * ((A - 1.f) - (A + 1.f) * cw);
        bb2 = A * ((A + 1.f) - (A - 1.f) * cw - two);
        aa1 = -2.f * ((A - 1.f) + (A + 1.f) * cw);
        aa2 = (A + 1.f) + (A - 1.f) * cw - two;
    }
    b0 = bb0 / a0;
    b1 = bb1 / a0;
    b2 = bb2 / a0;
    a1 = aa1 / a0;
    a2 = aa2 / a0;
}

void KWeight::prepare (double sampleRate) noexcept
{
    // libebur128's bilinear design: at 48 kHz it is the BS.1770 table exactly
    // (shelf b 1.53512486 -2.69169619 1.19839281, a -1.69065929 0.73248077;
    // RLB b 1 -2 1, a -1.99004745 0.99007225), and it holds at other rates.
    const double fs = sampleRate > 1000.0 ? sampleRate : 48000.0;
    constexpr double pi = 3.14159265358979323846;
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        sb0 = (Vh + Vb * K / Q + K * K) / a0;
        sb1 = 2.0 * (K * K - Vh) / a0;
        sb2 = (Vh - Vb * K / Q + K * K) / a0;
        sa1 = 2.0 * (K * K - 1.0) / a0;
        sa2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        hb0 = 1.0;
        hb1 = -2.0;
        hb2 = 1.0;
        ha1 = 2.0 * (K * K - 1.0) / a0;
        ha2 = (1.0 - K / Q + K * K) / a0;
    }
    reset();
}

double KWeight::process (double x) noexcept
{
    // Transposed direct form II, both stages.
    const double y = sb0 * x + s1;
    s1 = sb1 * x - sa1 * y + s2;
    s2 = sb2 * x - sa2 * y;
    const double z = hb0 * y + h1;
    h1 = hb1 * y - ha1 * z + h2;
    h2 = hb2 * y - ha2 * z;
    if (std::fabs (s1) < 1.0e-30) s1 = 0.0;
    if (std::fabs (s2) < 1.0e-30) s2 = 0.0;
    if (std::fabs (h1) < 1.0e-30) h1 = 0.0;
    if (std::fabs (h2) < 1.0e-30) h2 = 0.0;
    return z;
}

void MixRuntime::RmsWin::assign (int samples) noexcept
{
    n = std::max (1, samples);
    ring.assign ((size_t) n, 0.f);
    w = 0;
    sum = 0.0;
}

void MixRuntime::RmsWin::reset() noexcept
{
    std::fill (ring.begin(), ring.end(), 0.f);
    w = 0;
    sum = 0.0;
}

float MixRuntime::RmsWin::push (float x) noexcept
{
    const float s = x * x;
    sum += (double) s - (double) ring[(size_t) w];
    if (sum < 0.0)
        sum = 0.0;
    ring[(size_t) w] = s;
    w = (w + 1) % n;
    return std::sqrt ((float) (sum / (double) n));
}

void MixRuntime::Corr::assign (int samples) noexcept
{
    n = std::max (8, samples);
    a.assign ((size_t) n, 0.f);
    b.assign ((size_t) n, 0.f);
    w = 0;
    fill = 0;
    ab = aa = bb = 0.0;
}

void MixRuntime::Corr::reset() noexcept
{
    std::fill (a.begin(), a.end(), 0.f);
    std::fill (b.begin(), b.end(), 0.f);
    w = 0;
    fill = 0;
    ab = aa = bb = 0.0;
}

float MixRuntime::Corr::push (float x, float y) noexcept
{
    if (fill >= n)
    {
        const float ox = a[(size_t) w];
        const float oy = b[(size_t) w];
        ab -= (double) ox * (double) oy;
        aa -= (double) ox * (double) ox;
        bb -= (double) oy * (double) oy;
    }
    else
        ++fill;
    a[(size_t) w] = x;
    b[(size_t) w] = y;
    ab += (double) x * (double) y;
    aa += (double) x * (double) x;
    bb += (double) y * (double) y;
    w = (w + 1) % n;
    const double den = std::sqrt (std::max (0.0, aa) * std::max (0.0, bb));
    if (den < 1.0e-12)
        return 1.f;
    return (float) std::clamp (ab / den, -1.0, 1.0);
}

float MixRuntime::raised (float u) noexcept
{
    u = std::clamp (u, 0.f, 1.f);
    return 0.5f * (1.f - std::cos (kPi * u));
}

float MixRuntime::shaper (MixSatType type, float x, float drive) noexcept
{
    drive = std::max (0.05f, drive);
    const float u = x * drive;
    float y = u;
    if (type == MixSatType::Softclip)
        y = std::tanh (u);
    else if (type == MixSatType::Tape)
        y = u / (1.f + std::fabs (u));
    else if (type == MixSatType::Tube)
        y = std::tanh (u + 0.25f * u * std::fabs (u));
    else
        return x;
    // Unity small-signal gain so drive changes harmonics, not the lane's LUFS.
    const float g0 = (type == MixSatType::Tube) ? (drive * 1.f) : drive;
    return y / std::max (0.05f, g0);
}

void MixRuntime::butterworthQ (int slope, float& q0, float& q1, int& stages) noexcept
{
    if (slope >= 24)
    {
        stages = 2;
        q0 = 0.54119610f;
        q1 = 1.30656296f;
    }
    else
    {
        stages = 1;
        q0 = 0.70710678f;
        q1 = 0.70710678f;
    }
}

void MixRuntime::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    ramp50 = std::max (1, (int) std::lround (0.050 * sr));
    ramp80 = std::max (1, (int) std::lround (0.080 * sr));
    ramp120 = std::max (1, (int) std::lround (0.120 * sr));
    corrN = std::max (8, (int) std::lround (0.050 * sr));
    rmsN = std::max (4, (int) std::lround (0.005 * sr));
    levelBlockN = std::max (16, (int) std::lround (0.100 * sr));
    // One-pole toward the wanted gain: cuts in ~0.3 s, boosts in ~1.5 s.
    levelCut = 1.f - std::exp (-1.f / (float) (0.3 * sr));
    levelBoost = 1.f - std::exp (-1.f / (float) (1.5 * sr));
    for (auto& s : strip)
    {
        s.level.kL.prepare (sr);
        s.level.kR.prepare (sr);
        s.rms.assign (rmsN);
        s.corr.assign (corrN);
        s.compEnv = 1.f;
        s.rampSample = 1000000;
    }
    reset();
}

void MixRuntime::reset() noexcept
{
    for (auto& s : strip)
    {
        for (int i = 0; i < 2; ++i)
        {
            s.hpfL[i].reset(); s.hpfR[i].reset();
            s.lpfL[i].reset(); s.lpfR[i].reset();
            s.hpfLOld[i].reset(); s.hpfROld[i].reset();
            s.lpfLOld[i].reset(); s.lpfROld[i].reset();
            s.eqL[i].reset(); s.eqR[i].reset();
            s.eqLOld[i].reset(); s.eqROld[i].reset();
        }
        s.monoL.reset(); s.monoR.reset();
        s.tiltL.reset(); s.tiltR.reset();
        s.rms.reset();
        s.corr.reset();
        s.compEnv = 1.f;
        auto& lv = s.level;
        lv.kL.reset();
        lv.kR.reset();
        lv.acc = lv.sum = 0.0;
        lv.count = lv.w = lv.fill = 0;
        std::fill (std::begin (lv.ring), std::end (lv.ring), 0.0);
        lv.holdDb = -200.f;
        lv.wantLin = lv.gain = 1.f;
        std::memset (s.tpUp, 0, sizeof s.tpUp);
        std::memset (s.tpDown, 0, sizeof s.tpDown);
        s.rampSample = 1000000;
        s.slopeXfade = s.satXfade = false;
        s.eqXfade[0] = s.eqXfade[1] = false;
    }
    sumRamp = 1000000;
    snapped = false;
}

float MixRuntime::rampOf (int /*lane*/, float ms) const noexcept
{
    if (ms >= 100.f)
        return (float) ramp120;
    if (ms >= 70.f)
        return (float) ramp80;
    return (float) ramp50;
}

MixChain MixRuntime::blended (const Strip& s) const noexcept
{
    const float uGain = raised ((float) s.rampSample / (float) ramp50);
    const float uFreq = uGain;
    const float uComp = raised ((float) s.rampSample / (float) ramp120);
    const float uSat = raised ((float) s.rampSample / (float) ramp80);
    MixChain c = s.to;
    auto lerp = [] (float a, float b, float u) { return a + (b - a) * u; };
    auto lerpLog = [] (float a, float b, float u)
    {
        a = std::max (1.f, a);
        b = std::max (1.f, b);
        const float la = std::log2 (a);
        const float lb = std::log2 (b);
        return std::exp2 (la + (lb - la) * u);
    };
    c.hpfHz = lerpLog (s.from.hpfHz, s.to.hpfHz, uFreq);
    c.lpfHz = lerpLog (std::max (20.f, s.from.lpfHz), std::max (20.f, s.to.lpfHz), uFreq);
    c.inputTrimDb = lerp (s.from.inputTrimDb, s.to.inputTrimDb, uGain);
    c.outputTrimDb = lerp (s.from.outputTrimDb, s.to.outputTrimDb, uGain);
    c.tpCeilDb = lerp (s.from.tpCeilDb, s.to.tpCeilDb, uGain);
    c.tiltDbPerOct = lerp (s.from.tiltDbPerOct, s.to.tiltDbPerOct, uGain);
    c.tiltPivotHz = lerpLog (s.from.tiltPivotHz, s.to.tiltPivotHz, uFreq);
    c.monoBelowHz = lerpLog (std::max (1.f, s.from.monoBelowHz), std::max (1.f, s.to.monoBelowHz), uFreq);
    c.compThresholdDb = lerp (s.from.compThresholdDb, s.to.compThresholdDb, uComp);
    c.compAttackMs = lerp (s.from.compAttackMs, s.to.compAttackMs, uComp);
    c.compReleaseMs = lerp (s.from.compReleaseMs, s.to.compReleaseMs, uComp);
    c.compKneeDb = lerp (s.from.compKneeDb, s.to.compKneeDb, uComp);
    const float invA = 1.f / std::max (1.f, s.from.compRatio);
    const float invB = 1.f / std::max (1.f, s.to.compRatio);
    c.compRatio = 1.f / std::max (0.05f, lerp (invA, invB, uComp));
    c.satDriveDb = lerp (s.from.satDriveDb, s.to.satDriveDb, uSat);
    c.satMix = lerp (s.from.satMix, s.to.satMix, uSat);
    for (int i = 0; i < 2; ++i)
    {
        c.eq[i].freqHz = lerpLog (std::max (20.f, s.from.eq[i].freqHz), std::max (20.f, s.to.eq[i].freqHz), uFreq);
        c.eq[i].gainDb = lerp (s.from.eq[i].gainDb, s.to.eq[i].gainDb, uGain);
        const float qa = std::max (0.05f, s.from.eq[i].q);
        const float qb = std::max (0.05f, s.to.eq[i].q);
        c.eq[i].q = std::exp (lerp (std::log (qa), std::log (qb), uFreq));
        c.eq[i].type = s.to.eq[i].type;
    }
    c.hpfSlope = s.to.hpfSlope;
    c.lpfSlope = s.to.lpfSlope;
    c.lpfOn = s.to.lpfOn;
    c.sat = s.to.sat;
    c.forceMono = s.to.forceMono;
    c.nEq = s.to.nEq;
    return c;
}

float MixRuntime::filterBank (Strip& s, float x, bool right, bool hpf, float freq, int slope, bool oldPath) noexcept
{
    float q0, q1;
    int stages = 1;
    butterworthQ (slope, q0, q1, stages);
    const float srF = (float) sr;
    auto& a = hpf ? (oldPath ? (right ? s.hpfROld[0] : s.hpfLOld[0]) : (right ? s.hpfR[0] : s.hpfL[0]))
                  : (oldPath ? (right ? s.lpfROld[0] : s.lpfLOld[0]) : (right ? s.lpfR[0] : s.lpfL[0]));
    auto& b = hpf ? (oldPath ? (right ? s.hpfROld[1] : s.hpfLOld[1]) : (right ? s.hpfR[1] : s.hpfL[1]))
                  : (oldPath ? (right ? s.lpfROld[1] : s.lpfLOld[1]) : (right ? s.lpfR[1] : s.lpfL[1]));
    float y = hpf ? a.highpass (x, freq, q0, srF) : a.lowpass (x, freq, q0, srF);
    if (stages > 1)
        y = hpf ? b.highpass (y, freq, q1, srF) : b.lowpass (y, freq, q1, srF);
    return y;
}

float MixRuntime::eqBank (Strip& s, float x, bool right, int band, const MixEqBand& eq, bool oldPath) noexcept
{
    auto& bq = oldPath ? (right ? s.eqROld[band] : s.eqLOld[band])
                       : (right ? s.eqR[band] : s.eqL[band]);
    const float srF = (float) sr;
    if (eq.type == MixEqType::Bell)
        bq.setBell (eq.freqHz, eq.q, eq.gainDb, srF);
    else if (eq.type == MixEqType::LowShelf)
        bq.setShelf (false, eq.freqHz, eq.q, eq.gainDb, srF);
    else if (eq.type == MixEqType::HighShelf)
        bq.setShelf (true, eq.freqHz, eq.q, eq.gainDb, srF);
    else
        return x;
    return bq.process (x);
}

void MixRuntime::beginBlock (const MixProgram& prog, const MixBlockInfo& info) noexcept
{
    program = &prog;
    dialect = std::clamp (info.dialect, 0, kMixDialects - 1);
    bypassSum = info.bypassSumTrim;
    laneLevel = info.laneLevel;
    sumMask = info.sumMask >= 0 ? std::clamp (info.sumMask, 0, 63) : std::clamp (info.activeMask, 0, 63);

    float targetSum = prog.dialectTrimDb[dialect] + prog.sumAlignDb[dialect];
    if (! bypassSum)
        targetSum += prog.sumTrimDb[dialect][sumMask];
    if (! snapped)
    {
        sumFromDb = sumToDb = targetSum;
        sumRamp = ramp50;
    }
    else if (std::fabs (targetSum - sumToDb) > 0.001f)
    {
        const float u = raised ((float) sumRamp / (float) ramp50);
        sumFromDb = sumFromDb + (sumToDb - sumFromDb) * u;
        sumToDb = targetSum;
        sumRamp = 0;
    }

    for (int lane = 0; lane < kMixLanes; ++lane)
    {
        MixChain target = prog.chain[dialect][lane];
        if (! target.valid)
            continue;
        target.outputTrimDb += prog.makeupDb[dialect][lane];
        target.tpCeilDb = prog.tpCeilDb[dialect][lane];
        if (lane < kMixMelodic && info.presetId[lane] != nullptr)
            prog.applyOverride (target, lane, info.presetId[lane]);
        auto& s = strip[lane];
        if (! snapped || ! s.cur.valid)
        {
            s.from = s.to = s.cur = target;
            s.rampSample = 1000000;
            s.slopeXfade = s.satXfade = false;
            s.eqXfade[0] = s.eqXfade[1] = false;
            s.hpfSlopeFrom = s.hpfSlopeTo = target.hpfSlope;
            s.lpfSlopeFrom = s.lpfSlopeTo = target.lpfSlope;
            s.satFrom = s.satTo = target.sat;
            for (int i = 0; i < 2; ++i)
                s.eqTypeFrom[i] = s.eqTypeTo[i] = target.eq[i].type;
            continue;
        }
        const bool slopeChanged = target.hpfSlope != s.to.hpfSlope || target.lpfSlope != s.to.lpfSlope
                               || target.lpfOn != s.to.lpfOn;
        const bool satChanged = target.sat != s.to.sat;
        bool eqChanged = false;
        for (int i = 0; i < 2; ++i)
            if (target.eq[i].type != s.to.eq[i].type)
                eqChanged = true;
        if (slopeChanged || satChanged || eqChanged
            || std::fabs (target.hpfHz - s.to.hpfHz) > 0.5f
            || std::fabs (target.inputTrimDb - s.to.inputTrimDb) > 0.01f
            || std::fabs (target.outputTrimDb - s.to.outputTrimDb) > 0.01f
            || std::fabs (target.tpCeilDb - s.to.tpCeilDb) > 0.05f
            || std::fabs (target.compReleaseMs - s.to.compReleaseMs) > 0.5f
            || std::fabs (target.satDriveDb - s.to.satDriveDb) > 0.05f)
        {
            s.from = blended (s);
            s.to = target;
            s.rampSample = 0;
            if (slopeChanged)
            {
                s.slopeXfade = true;
                s.hpfSlopeFrom = s.from.hpfSlope;
                s.hpfSlopeTo = target.hpfSlope;
                s.lpfSlopeFrom = s.from.lpfSlope;
                s.lpfSlopeTo = target.lpfSlope;
                for (int i = 0; i < 2; ++i)
                {
                    s.hpfLOld[i] = s.hpfL[i];
                    s.hpfROld[i] = s.hpfR[i];
                    s.lpfLOld[i] = s.lpfL[i];
                    s.lpfROld[i] = s.lpfR[i];
                }
            }
            if (satChanged)
            {
                s.satXfade = true;
                s.satFrom = s.from.sat;
                s.satTo = target.sat;
                s.corr.reset();
            }
            for (int i = 0; i < 2; ++i)
            {
                if (target.eq[i].type != s.from.eq[i].type)
                {
                    s.eqXfade[i] = true;
                    s.eqTypeFrom[i] = s.from.eq[i].type;
                    s.eqTypeTo[i] = target.eq[i].type;
                    s.eqLOld[i] = s.eqL[i];
                    s.eqROld[i] = s.eqR[i];
                }
            }
        }
    }
    snapped = true;
}

float MixRuntime::tpShape (float x, float ceilLin) noexcept
{
    // +6 dB and above is "no ceiling": the lane still runs the FIR so every lane shares one delay.
    if (ceilLin > 2.f)
        return x;
    const float ax = std::fabs (x);
    const float knee = 0.7f * ceilLin;
    if (ax <= knee)
        return x;
    const float span = std::max (1.0e-8f, ceilLin - knee);
    float y = knee + span * std::tanh ((ax - knee) / span);
    if (y > ceilLin)
        y = ceilLin;
    return std::copysign (y, x);
}

float MixRuntime::clipSample (Strip& s, int ch, float x, float ceilLin) noexcept
{
    float* up = s.tpUp[ch];
    std::memmove (up + 1, up, sizeof (float) * 7);
    up[0] = x;
    float hi[4];
    for (int p = 0; p < 4; ++p)
    {
        const float* c = kTpUp[p];
        float acc = c[0] * up[0] + c[1] * up[1] + c[2] * up[2] + c[3] * up[3]
                  + c[4] * up[4] + c[5] * up[5] + c[6] * up[6] + c[7] * up[7];
        hi[p] = tpShape (acc, ceilLin);
    }
    float* down = s.tpDown[ch];
    for (int p = 0; p < 4; ++p)
    {
        std::memmove (down + 1, down, sizeof (float) * 31);
        down[0] = hi[p];
    }
    float y = 0.f;
    for (int k = 0; k < 32; ++k)
        y += kTpDown[k] * down[k];
    return y;
}

void MixRuntime::process (int lane, float& l, float& r) noexcept
{
    if (program == nullptr || lane < 0 || lane >= kMixLanes)
        return;
    auto& s = strip[lane];
    if (! s.to.valid && ! s.cur.valid)
        return;
    const MixChain c = snapped ? blended (s) : s.to;
    s.cur = c;
    const float srF = (float) sr;
    const float u80 = raised ((float) s.rampSample / (float) ramp80);

    auto one = [&] (float x, bool right) -> float
    {
        float y = x * dbToLin (c.inputTrimDb);
        if (c.hpfHz > 16.f)
        {
            const float neu = filterBank (s, y, right, true, c.hpfHz, c.hpfSlope, false);
            if (s.slopeXfade && s.hpfSlopeFrom != s.hpfSlopeTo)
            {
                const float old = filterBank (s, y, right, true, c.hpfHz, s.hpfSlopeFrom, true);
                const float theta = u80 * (kPi * 0.5f);
                const float rho = 1.f;
                const float g0 = std::cos (theta), g1 = std::sin (theta);
                const float den = std::sqrt (std::max (1.0e-8f, 1.f + 2.f * rho * g0 * g1));
                y = (g0 * old + g1 * neu) / den;
            }
            else
                y = neu;
        }
        if (c.lpfOn && c.lpfHz < srF * 0.45f)
        {
            const float neu = filterBank (s, y, right, false, c.lpfHz, c.lpfSlope, false);
            if (s.slopeXfade && s.lpfSlopeFrom != s.lpfSlopeTo)
            {
                const float old = filterBank (s, y, right, false, c.lpfHz, s.lpfSlopeFrom, true);
                const float theta = u80 * (kPi * 0.5f);
                const float g0 = std::cos (theta), g1 = std::sin (theta);
                const float den = std::sqrt (std::max (1.0e-8f, 1.f + 2.f * 1.f * g0 * g1));
                y = (g0 * old + g1 * neu) / den;
            }
            else
                y = neu;
        }
        for (int band = 0; band < c.nEq && band < 2; ++band)
        {
            MixEqBand eq = c.eq[band];
            const float neu = eqBank (s, y, right, band, eq, false);
            if (s.eqXfade[band])
            {
                MixEqBand oldEq = eq;
                oldEq.type = s.eqTypeFrom[band];
                const float old = eqBank (s, y, right, band, oldEq, true);
                const float theta = u80 * (kPi * 0.5f);
                const float g0 = std::cos (theta), g1 = std::sin (theta);
                const float den = std::sqrt (std::max (1.0e-8f, 1.f + 2.f * g0 * g1));
                y = (g0 * old + g1 * neu) / den;
            }
            else
                y = neu;
        }
        return y;
    };

    float yL = one (l, false);
    float yR = one (r, true);

    const float midIn = 0.5f * (yL + yR);
    const float rms = s.rms.push (midIn);
    const float detDb = 20.f * std::log10 (rms + 1.0e-12f);
    const float over = detDb - c.compThresholdDb;
    const float knee = std::max (0.f, c.compKneeDb);
    const float slope = 1.f - 1.f / std::max (1.f, c.compRatio);
    float gr = 0.f;
    if (knee < 0.05f)
        gr = over > 0.f ? over * slope : 0.f;
    else if (over <= -0.5f * knee)
        gr = 0.f;
    else if (over >= 0.5f * knee)
        gr = over * slope;
    else
    {
        const float x = over + 0.5f * knee;
        gr = (x * x) / (2.f * knee) * slope;
    }
    const float targetG = dbToLin (-gr);
    const float atk = 1.f - std::exp (-1.f / std::max (1.f, c.compAttackMs * 0.001f * srF));
    const float rel = 1.f - std::exp (-1.f / std::max (1.f, c.compReleaseMs * 0.001f * srF));
    const float coef = targetG < s.compEnv ? atk : rel;
    s.compEnv += (targetG - s.compEnv) * coef;
    s.compEnv = flush (s.compEnv);
    yL *= s.compEnv;
    yR *= s.compEnv;

    if (c.sat != MixSatType::None || s.satXfade)
    {
        const float drive = dbToLin (c.satDriveDb);
        const float mix = std::clamp (c.satMix, 0.f, 1.f);
        auto wet = [&] (MixSatType type, float x)
        {
            return x + (shaper (type, x, drive) - x) * mix;
        };
        const float nL = wet (c.sat, yL);
        const float nR = wet (c.sat, yR);
        if (s.satXfade)
        {
            const float oL = wet (s.satFrom, yL);
            const float oR = wet (s.satFrom, yR);
            const float rho = s.corr.push (0.5f * (oL + oR), 0.5f * (nL + nR));
            const float theta = u80 * (kPi * 0.5f);
            const float g0 = std::cos (theta), g1 = std::sin (theta);
            const float den = std::sqrt (std::max (1.0e-8f, 1.f + 2.f * rho * g0 * g1));
            yL = (g0 * oL + g1 * nL) / den;
            yR = (g0 * oR + g1 * nR) / den;
        }
        else
        {
            yL = nL;
            yR = nR;
        }
    }

    if (std::fabs (c.tiltDbPerOct) > 0.05f)
    {
        const float pivot = clampFreq (c.tiltPivotHz, srF);
        float lp, bp, hp;
        s.tiltL.process (yL, pivot, 0.707f, srF, lp, bp, hp);
        const float lowG = dbToLin (-c.tiltDbPerOct);
        const float highG = dbToLin (c.tiltDbPerOct);
        yL = lp * lowG + (yL - lp) * highG;
        s.tiltR.process (yR, pivot, 0.707f, srF, lp, bp, hp);
        yR = lp * lowG + (yR - lp) * highG;
    }

    if (c.forceMono)
    {
        const float m = 0.5f * (yL + yR);
        yL = yR = m;
    }
    else if (c.monoBelowHz > 20.f)
    {
        const float fc = clampFreq (c.monoBelowHz, srF);
        const float lpL = s.monoL.lowpass (yL, fc, 0.707f, srF);
        const float lpR = s.monoR.lowpass (yR, fc, 0.707f, srF);
        const float mono = 0.5f * (lpL + lpR);
        yL = yL - lpL + mono;
        yR = yR - lpR + mono;
    }

    if (laneLevel)
        levelLane (s, c, yL, yR);

    const float trim = dbToLin (c.outputTrimDb);
    const float ceilLin = dbToLin (c.tpCeilDb);
    // Soft knee into a hard clamp at 4×, after the trim, so makeup can put the body
    // back on the short-term target while the peak stays on the ceiling. No sum limiter.
    l = flush (clipSample (s, 0, yL * trim, ceilLin));
    r = flush (clipSample (s, 1, yR * trim, ceilLin));

    if (s.rampSample < 1000000)
        ++s.rampSample;
    if (s.rampSample >= ramp80)
    {
        s.slopeXfade = false;
        s.satXfade = false;
        s.eqXfade[0] = s.eqXfade[1] = false;
    }
}

// Lane level: the lane's own K-weighted short-term loudness, held at its recent
// max, steers a slow gain so the ST max sits on the profile's st_target_lufs.
// It reads only this lane (before the mute), so FULL / MUTE / SOLO share one gain.
// Blocks well under the hold (rests, silence) neither move the hold nor the gain,
// so a lane coming back from a rest is not boosted.
void MixRuntime::levelLane (Strip& s, const MixChain& c, float& l, float& r) noexcept
{
    auto& lv = s.level;
    const double kl = lv.kL.process ((double) l);
    const double kr = lv.kR.process ((double) r);
    lv.acc += kl * kl + kr * kr;
    if (++lv.count >= levelBlockN)
    {
        const double block = lv.acc / (double) lv.count;
        lv.acc = 0.0;
        lv.count = 0;
        lv.sum += block - lv.ring[lv.w];
        if (lv.sum < 0.0)
            lv.sum = 0.0;
        lv.ring[lv.w] = block;
        lv.w = (lv.w + 1) % kLevelBlocks;
        lv.fill = std::min (lv.fill + 1, kLevelBlocks);
        // Output trim (makeup) comes after this stage: count it in.
        const float blockDb = -0.691f + 10.f * std::log10 ((float) block + 1.0e-20f) + c.outputTrimDb;
        const float stDb = -0.691f + 10.f * std::log10 ((float) (lv.sum / (double) lv.fill) + 1.0e-20f) + c.outputTrimDb;
        const bool active = blockDb > -70.f && blockDb > lv.holdDb - 20.f;
        if (active && lv.fill >= 4)
        {
            // The hold falls 0.5 dB/s while the lane plays, so a quieter section is found.
            lv.holdDb = std::max (stDb, lv.holdDb - 0.05f);
            float err = c.stTargetLufs - lv.holdDb;
            err = err > 0.f ? std::max (0.f, err - 0.5f) : std::min (0.f, err + 0.5f);
            lv.wantLin = dbToLin (std::clamp (err, -9.f, 12.f));
        }
    }
    lv.gain += (lv.wantLin - lv.gain) * (lv.wantLin < lv.gain ? levelCut : levelBoost);
    lv.gain = flush (lv.gain);
    l *= lv.gain;
    r *= lv.gain;
}

float MixRuntime::sumGain() noexcept
{
    const float u = raised ((float) sumRamp / (float) ramp50);
    const float db = sumFromDb + (sumToDb - sumFromDb) * u;
    if (sumRamp < 1000000)
        ++sumRamp;
    return dbToLin (db);
}

} // namespace rolla
