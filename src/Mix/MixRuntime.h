#pragma once

#include "MixProgram.h"
#include "../DspUtil.h"

#include <vector>

namespace rolla
{

struct MixBlockInfo
{
    int dialect = 0;
    int presetIndex[6] {};
    const char* presetId[6] {};
    // -1: look up from activeMask. >=0: frozen mask. Bypass is a separate flag.
    int sumMask = -1;
    bool bypassSumTrim = false;
    int activeMask = 63;
    // Steer each lane's short-term loudness toward its profile st_target_lufs.
    bool laneLevel = true;
};

// BS.1770-4 K-weighting for one channel: the pre-filter shelf (+3.9998 dB at
// 1681.97 Hz, Q 0.70718) then the RLB high-pass (38.135 Hz, Q 0.50033), as
// pyloudnorm designs them at any rate. A 997 Hz 0 dBFS sine reads -3.01 LUFS
// in one channel and 0 LUFS in both.
struct KWeight
{
    void prepare (double sampleRate) noexcept;
    void reset() noexcept { s1 = s2 = h1 = h2 = 0.0; }
    double process (double x) noexcept;

    double sb0 = 1, sb1 = 0, sb2 = 0, sa1 = 0, sa2 = 0;
    double hb0 = 1, hb1 = 0, hb2 = 0, ha1 = 0, ha2 = 0;
    double s1 = 0, s2 = 0, h1 = 0, h2 = 0;
};

// Per-lane HPF/LPF, EQ, compressor, saturation, tilt, mono-below, lane level, trim.
// Dialect changes ramp (MIX_SPEC §9.2). The sum itself stays linear.
class MixRuntime
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void beginBlock (const MixProgram& program, const MixBlockInfo& info) noexcept;
    void process (int lane, float& l, float& r) noexcept;
    float sumGain() noexcept;

private:
    struct Biquad
    {
        float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
        float z1 = 0.f, z2 = 0.f;
        void reset() noexcept { z1 = z2 = 0.f; }
        float process (float x) noexcept;
        void setBell (float freq, float q, float gainDb, float sr) noexcept;
        void setShelf (bool high, float freq, float q, float gainDb, float sr) noexcept;
    };

    // BS.1770 K-weighted short-term loudness of one lane, in 100 ms blocks over 3 s.
    static constexpr int kLevelBlocks = 30;
    struct LaneLevel
    {
        KWeight kL, kR;
        double acc = 0.0;
        int count = 0;
        double ring[kLevelBlocks] {};
        double sum = 0.0;
        int w = 0, fill = 0;
        float holdDb = -200.f;
        float wantLin = 1.f;
        float gain = 1.f;
    };

    struct RmsWin
    {
        std::vector<float> ring;
        int n = 1, w = 0;
        double sum = 0.0;
        void assign (int samples) noexcept;
        void reset() noexcept;
        float push (float x) noexcept;
    };

    struct Corr
    {
        std::vector<float> a, b;
        int n = 1, w = 0, fill = 0;
        double ab = 0, aa = 0, bb = 0;
        void assign (int samples) noexcept;
        void reset() noexcept;
        float push (float x, float y) noexcept;
    };

    struct Strip
    {
        Svf hpfL[2], hpfR[2], lpfL[2], lpfR[2];
        Svf hpfLOld[2], hpfROld[2], lpfLOld[2], lpfROld[2];
        Svf monoL, monoR;
        Svf tiltL, tiltR;
        Biquad eqL[2], eqR[2];
        Biquad eqLOld[2], eqROld[2];
        RmsWin rms;
        Corr corr;
        LaneLevel level;
        float compEnv = 1.f;
        MixChain cur {};
        MixChain from {};
        MixChain to {};
        int rampSample = 1000000;
        bool slopeXfade = false;
        bool eqXfade[2] {};
        bool satXfade = false;
        MixSatType satFrom = MixSatType::None;
        MixSatType satTo = MixSatType::None;
        int hpfSlopeFrom = 12, hpfSlopeTo = 12, lpfSlopeFrom = 12, lpfSlopeTo = 12;
        MixEqType eqTypeFrom[2] {}, eqTypeTo[2] {};
        // 4× true-peak clip histories. Every lane runs the same FIR so the mute null keeps one delay.
        float tpUp[2][8] {};
        float tpDown[2][32] {};
    };

    static float raised (float u) noexcept;
    static float shaper (MixSatType type, float x, float drive) noexcept;
    static float tpShape (float x, float ceilLin) noexcept;
    static float clipSample (Strip& s, int ch, float x, float ceilLin) noexcept;
    static void butterworthQ (int slope, float& q0, float& q1, int& stages) noexcept;
    float rampOf (int lane, float ms) const noexcept;
    MixChain blended (const Strip& s) const noexcept;
    float filterBank (Strip& s, float x, bool right, bool hpf, float freq, int slope, bool oldPath) noexcept;
    float eqBank (Strip& s, float x, bool right, int band, const MixEqBand& eq, bool oldPath) noexcept;
    void levelLane (Strip& s, const MixChain& c, float& l, float& r) noexcept;

    double sr = 48000.0;
    int ramp50 = 2400, ramp80 = 3840, ramp120 = 5760, corrN = 2400, rmsN = 240;
    int levelBlockN = 4800;
    float levelCut = 0.f, levelBoost = 0.f;
    bool laneLevel = true;
    Strip strip[kMixLanes];
    const MixProgram* program = nullptr;
    int dialect = 0;
    int sumMask = 63;
    bool bypassSum = false;
    float sumFromDb = 0.f, sumToDb = 0.f;
    int sumRamp = 1000000;
    bool snapped = false;
};

} // namespace rolla
