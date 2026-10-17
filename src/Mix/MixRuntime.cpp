#include "MixRuntime.h"

#include <algorithm>
#include <cmath>

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
    for (auto& s : strip)
    {
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

    const float trim = dbToLin (c.outputTrimDb);
    l = flush (yL * trim);
    r = flush (yR * trim);

    if (s.rampSample < 1000000)
        ++s.rampSample;
    if (s.rampSample >= ramp80)
    {
        s.slopeXfade = false;
        s.satXfade = false;
        s.eqXfade[0] = s.eqXfade[1] = false;
    }
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
