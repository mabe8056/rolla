#pragma once

#include "Pattern.h"
#include "../Engine/SampleSpec.h"

#include <string>
#include <vector>

namespace rolla
{

// Values a factory preset can set. Units are the spec's units.
// Omitted keys keep the defaults below. Unison is capped at 5.
struct Extras
{
    float pw = 0.5f;          // pulse width 0.05–0.95
    float pwmHz = 0.f;        // PWM LFO rate, Hz. 0 = fixed pw
    float pwmDepth = 0.f;     // PWM depth, fraction of the cycle (0–0.45)
    int unison = 1;           // 1–5
    float detuneCents = 0.f;  // 0–50
    float spread = 0.f;       // 0–1
    int wave = -1;            // -1 inherit osc. 0 saw 1 tri 2 sine 3 organ 4 pulse 5 fm
    int engine = 0;           // hits: 0 timbre, 1 organ, 2 fm, 3 pwm
    float fmRatio = 1.f;      // 0.5–16
    float fmIndex = 0.f;      // 0–10
    float fmIndexEnd = 0.f;
    float fmDecayMs = 40.f;
    float noiseDb = -99.f;    // -99 = off
    float noiseHpfHz = 2000.f;
    float noiseBpHz = 0.f;    // 0 = use HPF, else band-pass centre
    float noiseDecayMs = 20.f;
    float pitchEnvSt = 0.f;   // ±24 st, decays to 0
    float pitchEnvTauMs = 40.f;
    float filterAttackMs = 0.f;
    float subLpfHz = 0.f;     // 0 = sub is unfiltered sine. Spec bass uses 120
    float osc2Db = -99.f;
    int osc2Oct = 0;          // ±3
    int osc2Wave = 0;         // 0 saw 1 pulse 2 tri 3 sine
    float chorusMix = 0.f;    // 0–1
    float chorusRateHz = 0.6f;
    float monoBelowHz = 0.f;  // 0 = no crossover. Non-bass spec uses 150–300
    float reverbSeconds = 2.6f; // about 0.8–4.0. 2.6 matches the previous shared plate
    float delaySendDb = -99.f;  // -99 = use the legacy linear delaySend
    float reverbSendDb = -99.f;
    float duckDb = -1.f;      // -1 = part default. 0 = no duck
    float duckRecoverMs = 115.f;
    bool duckGate = false;
    int vowelTo = -1;         // vox morph target. -1 = stay
    float morphMs = 0.f;      // 0 = no glide
    float size = 0.f;         // 0 male formants, 1 female
    float targetsLufs = 0.f;  // harness note, not applied by the engine
    float targetsPeak = -6.f;
};

struct BassPatch
{
    char id[24] = "pluck";
    char name[32] = "Pluck";
    int osc = 0; // 0 saw, 1 tri, 2 sine+2nd, 3 organ drawbars
    float subDb = -4.f;
    float oct2Db = -99.f;
    float cutoffHz = 170.f;
    float resonance = 0.04f;
    float filterEnvOct = 2.f;
    float filterTauMs = 50.f;
    float aMs = 3.f, dMs = 250.f, sDb = -8.f, rMs = 40.f;
    float glideMs = 35.f;
    float driveDb = 0.f;
    float duckDb = 13.f;
    float duckRecoverMs = 115.f;
    float hpfHz = 25.f;
    bool gate = false;
    float trimDb = 0.f;
    int octave = 1;
    Extras ex;
    SampleSpec sample;
};

struct StabPatch
{
    char id[24] = "dub";
    char name[32] = "Dub";
    int mode = 0; // 0 dub, 1 swell, 2 click
    float cutoffHz = 2200.f;
    float envOct = 1.5f;
    float tauMs = 30.f;
    float aMs = 1.f, dMs = 120.f, sDb = -40.f, rMs = 50.f;
    int unison = 2;
    float detuneCents = 5.f;
    float spread = 0.45f;
    float hpfHz = 250.f;
    float delaySend = 0.4f;
    float delayFeedback = 0.4f;
    float delayBeats = 0.75f;
    float reverbSend = 0.28f;
    bool hold = false;
    float trimDb = -8.f;
    bool chordOn = false;
    int chordN = 0;
    int chordIv[4] { 0, 0, 0, 0 };
    Extras ex;
    SampleSpec sample;
};

struct TonePatch
{
    char id[24] = "default";
    char name[32] = "Default";
    float cutoffHz = 2000.f;
    float decayMs = 150.f;
    float attackMs = 8.f;
    float releaseMs = 200.f;
    float brightness = 1.f;
    float detune = 1.f;
    float decayScale = 1.f;
    float detuneCents = 8.f;
    float breath = 0.2f;
    float reverbSend = 0.2f;
    float trimDb = -8.f;
    Extras ex;
    SampleSpec sample;
};

struct DrumPatch
{
    char id[24] = "tight";
    char name[32] = "909 Tight";
    float kickHz = 52.f;
    float kickDecayMs = 280.f;
    float kickClick = 0.45f;
    float kickSweep = 1.7f;
    float snareTone = 0.55f;
    float snareSnap = 0.62f;
    float snareDecayMs = 160.f;
    float clapDecayMs = 180.f;
    float hatDecayMs = 45.f;
    float hatOpenDecayMs = 280.f;
    float hatHpHz = 6500.f;
    float tomDecayMs = 260.f;
    float room = 0.08f;
    float drive = 0.15f;
    float trimDb = 0.f;
    SampleSpec sample;
    // Per-voice round-robin. Empty means voiceFile (or the circuit model).
    std::vector<std::string> voicePool[NumDrumVoices];
};

// One row of the v1.1 dialect → preset table (§1.2) plus E17 overrides.
// presetIndex is -1 when the id is missing. Hats and ride stay synthesized.
struct DialectDefaults
{
    char presetId[NumParts][24] {};
    int presetIndex[NumParts] { -1, -1, -1, -1, -1, -1, -1 };
    bool hitsMute = true;
    float swing = 0.537f; // genre model v1.2 hat band 53-57%
    bool bassDuck = false;
    bool bassGlide = false;
    float bassDuckDb = 13.f;
    float bassDuckMs = 115.f;
    float bassGlideMs = 0.f;
    char bassWhen[24] {};
};

struct PresetBank
{
    // Capacity. count[] is how many JSON presets each lane actually loaded.
    static constexpr int kPer = 40;
    int count[NumParts] { 3, 3, 3, 3, 3, 3, 3 };
    BassPatch bass[kPer];
    StabPatch stab[kPer];
    TonePatch hits[kPer];
    TonePatch pluck[kPer];
    TonePatch pad[kPer];
    TonePatch vox[kPer];
    DrumPatch drums[kPer];
    DialectDefaults dialect[NumDialects];
    bool fromJson = false;
};

inline int presetSlot (const PresetBank& bank, int part, int index) noexcept
{
    int n = 1;
    if (part >= 0 && part < NumParts && bank.count[part] > 0)
        n = bank.count[part];
    if (index < 0)
        return 0;
    if (index >= n)
        return n - 1;
    return index;
}

inline const SampleSpec& specAt (const PresetBank& bank, int part, int index) noexcept
{
    const int i = presetSlot (bank, part, index);
    switch (part)
    {
        case Bass: return bank.bass[i].sample;
        case Stab: return bank.stab[i].sample;
        case Hits: return bank.hits[i].sample;
        case Pluck: return bank.pluck[i].sample;
        case Pad: return bank.pad[i].sample;
        case Vox: return bank.vox[i].sample;
        case Drums: return bank.drums[i].sample;
        default: break;
    }
    return bank.bass[0].sample;
}

// styles 0 fits every dialect. Otherwise bit 0 is UK MIN, then LONG, POP, JACKIN.
inline bool styleFits (std::uint8_t styles, int dialect) noexcept
{
    if (styles == 0 || dialect < 0 || dialect >= NumDialects)
        return true;
    return (styles & (std::uint8_t) (1u << dialect)) != 0;
}

// Pick a non-synth preset for an unkept lane. -1 when the lane has none.
inline int pickPresetForRoll (const PresetBank& bank, int part, int dialect, std::uint32_t seed) noexcept
{
    if (part < 0 || part >= NumParts)
        return -1;
    int fit[PresetBank::kPer];
    int any[PresetBank::kPer];
    int nFit = 0;
    int nAny = 0;
    const int n = bank.count[part] > 0 ? bank.count[part] : 1;
    for (int i = 0; i < n; ++i)
    {
        const auto& spec = specAt (bank, part, i);
        if (spec.mode == SampleMode::Synth)
            continue;
        any[nAny++] = i;
        if (styleFits (spec.styles, dialect))
            fit[nFit++] = i;
    }
    const int* src = nFit > 0 ? fit : any;
    const int m = nFit > 0 ? nFit : nAny;
    if (m <= 0)
        return -1;
    return src[seed % (std::uint32_t) m];
}

// ROLL index into a drum voice pool. Tom low/mid are offset so they are not
// the same perc hit as the PERC voice.
inline const char* drumVoiceChoice (const DrumPatch& drum, int voice, int pick) noexcept
{
    if (voice < 0 || voice >= NumDrumVoices)
        return "";
    const auto& pool = drum.voicePool[voice];
    if (! pool.empty())
    {
        int shift = 0;
        if (voice == TomMid)
            shift = 1;
        else if (voice == TomLow)
            shift = 2;
        if (pick < 0)
            pick = 0;
        const int n = (int) pool.size();
        return pool[(size_t) ((pick + shift) % n)].c_str();
    }
    return drum.sample.voiceFile[voice];
}

int presetIndex (const PresetBank& bank, int part, const char* id) noexcept;
void applyDialectBass (const PresetBank& bank, int dialect, BassPatch& bass) noexcept;

void loadDefaultPresets (PresetBank& bank);
bool loadPresetsFromJson (PresetBank& bank, const char* json, int size);

} // namespace rolla
