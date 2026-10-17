#pragma once

#include "../DspUtil.h"

namespace rolla
{

// Small-signal model of the 808/909 bridged-T drum oscillator.
//
// The analog core is an op-amp with a bridged-T in the feedback and a diode
// pair that limits the swing. Linearised, that network is a complex pole pair:
// two capacitor states rotated at the tune frequency. The diodes conduct when
// a trigger slams the state; after that the body is a clean sine, which is
// what the 909 kick sounds like once the pitch envelope has settled.
// The tune input is the instantaneous frequency, so a pitch envelope is just
// the tuning resistor being modulated by the envelope capacitor.
struct BridgedT
{
    float c1 = 0.f;
    float c2 = 0.f;

    void reset() noexcept { c1 = c2 = 0.f; }

    // Discharge the bridge into a known state. `amount` is about 0.2–1.
    void strike (float amount) noexcept
    {
        const float slammed = fastTanh (amount * 1.35f);
        c1 = slammed;
        c2 = 0.f;
    }

    float tick (float hz, float sr) noexcept
    {
        hz = std::clamp (hz, 12.f, sr * 0.45f);
        const float w = 2.f * kPi * hz / sr;
        const float co = std::cos (w);
        const float sn = std::sin (w);
        // Slight loss so a voice that is not gated cannot grow without bound.
        constexpr float pole = 0.99992f;
        const float n1 = pole * (c1 * co - c2 * sn);
        const float n2 = pole * (c1 * sn + c2 * co);
        const float y = c1;
        // Diode pair. The strike lands near the knee; the steady body stays linear.
        const float mag = std::sqrt (n1 * n1 + n2 * n2);
        constexpr float knee = 1.05f;
        if (mag > knee)
        {
            const float k = knee / mag;
            c1 = flush (n1 * k);
            c2 = flush (n2 * k);
        }
        else
        {
            c1 = flush (n1);
            c2 = flush (n2);
        }
        return y;
    }
};

} // namespace rolla
