#pragma once

#include "DrumSampleSlot.h"

#include <memory>

namespace rolla
{

// Metallic hat bank. Measured is the partial set in PRESET_SPEC §9.5.
// Model808 is the six low squares (205.3 Hz and the published ratios).
enum class HatBank : int
{
    Measured = 0,
    Model808 = 1
};

// One drum hit. The engine fills this from the lane knobs and the active preset.
// synthKeep is the model gain after the sample slot (0 = sample replaced it).
struct DrumHit
{
    int voice = Kick;
    int velocity = 100;
    float synthKeep = 1.f;
    float tuneRatio = 1.f;
    float decayScale = 1.f;
    float patchDecay = 1.f;
    float levelGain = 1.f;
    float stepAccent = 1.f; // kick and clap only; hats stay at 1
    float tone = 0.f;       // lane tone, already centred, about −0.5..0.5
    float punch = 0.f;      // lane punch, centred
    float clickScale = 1.f;
    float hatHpScale = 1.f;
    float snareTone = 0.55f;
    float snareSnap = 0.62f;
    int key = 2;
    int dialect = UkMin;
};

// Circuit-modelled 909 kit: bridged-T kick / snare / rim / toms, multi-burst
// clap, six-square hats and ride. process() adds into left/right (it does not
// clear them) and then asks the sample slot to add its own audio.
class DrumMachine
{
public:
    DrumMachine();
    ~DrumMachine();
    DrumMachine (const DrumMachine&) = delete;
    DrumMachine& operator= (const DrumMachine&) = delete;

    void prepare (float sampleRate) noexcept;
    void reset() noexcept;
    void setHatBank (HatBank bank) noexcept;
    void setSampleSlot (DrumSampleSlot* slot) noexcept;
    DrumSampleSlot* sampleSlot() const noexcept;

    void trigger (const DrumHit& hit) noexcept;
    void process (float& left, float& right) noexcept;

    // Key follow: root / 4th / 5th nearest 52.8 Hz inside 49–56 Hz. Tie → 5th.
    static float kickHzForKey (int key) noexcept;

private:
    struct Core;
    std::unique_ptr<Core> core;
};

} // namespace rolla
