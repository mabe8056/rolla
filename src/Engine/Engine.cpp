#include "Engine.h"

#include "../DspUtil.h"
#include "../Drums/DrumMachine.h"
#include "../Generator/Scale.h"
#include "../Mix/MixRuntime.h"
#include "../Mix/LaneFaders.h"
#include "../Mix/TruePeakGuard.h"
#include "../Sampler/UserSlots.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace rolla
{
namespace
{
// Plugin builds play samples only, so no bus adds tanh colour to them.
#if ROLLA_PLUGIN_BUILD
constexpr bool kCleanBus = true;
#else
constexpr bool kCleanBus = false;
#endif


// MIX_SPEC: kick and bass hold corr <120 Hz ≥ 0.99 on the master.
constexpr float kSubMonoHz = 120.f;

float coefFromMs (float ms, float sr) noexcept
{
    return 1.f - std::exp (-1.f / std::max (1.f, ms * 0.001f * sr));
}

float expDecay (float ms, float sr) noexcept
{
    return std::exp (-1.f / std::max (1.f, ms * 0.001f * sr));
}

struct Env
{
    int stage = 0;
    int atkAge = 0, atkN = 1;
    bool cosineAtk = false;
    float amp = 0, sustain = 0, attInc = 0, decC = 0, relC = 0;

    void start (float aMs, float dMs, float sGain, float rMs, float sr) noexcept
    {
        stage = 1;
        amp = 0.f;
        atkAge = 0;
        sustain = std::clamp (sGain, 0.f, 1.f);
        // A 1 ms linear attack opens as one sample step of the whole voice.
        // Stab chords (1 ms) need about 12 ms or the slope in a quiet bar
        // reads as a click. Other short attacks use 6 ms. Longer attacks
        // stay linear, which is already under the click floor.
        cosineAtk = aMs < 6.f;
        const float atkMs = aMs < 2.f ? 12.f : (cosineAtk ? 6.f : aMs);
        atkN = std::max (1, (int) std::lround (atkMs * 0.001f * sr));
        attInc = 1.f / std::max (1.f, atkMs * 0.001f * sr);
        decC = coefFromMs (std::max (5.f, dMs) / 3.f, sr);
        relC = coefFromMs (std::max (5.f, rMs) / 3.f, sr);
    }

    void release() noexcept
    {
        if (stage != 0)
            stage = 4;
    }

    bool tick() noexcept
    {
        if (stage == 1)
        {
            if (cosineAtk)
            {
                ++atkAge;
                if (atkAge >= atkN)
                {
                    amp = 1.f;
                    stage = 2;
                }
                else
                {
                    const float t = (float) atkAge / (float) atkN;
                    amp = 0.5f * (1.f - std::cos (kPi * t));
                }
            }
            else
            {
                amp += attInc;
                if (amp >= 1.f)
                {
                    amp = 1.f;
                    stage = 2;
                }
            }
        }
        else if (stage == 2)
        {
            amp += (sustain - amp) * decC;
            if (std::fabs (amp - sustain) < 1.0e-4f)
            {
                amp = sustain;
                stage = 3;
            }
        }
        else if (stage == 3)
            amp = sustain;
        else if (stage == 4)
        {
            amp += (0.f - amp) * relC;
            if (amp < 1.0e-5f)
            {
                amp = 0.f;
                stage = 0;
                return false;
            }
        }
        else
            return false;
        amp = flush (amp);
        return true;
    }
};

struct BassV
{
    Env env;
    bool on = false;
    int osc = 0;
    float phase = 0, subPhase = 0;
    float freq = 40.f, targetFreq = 40.f, glideC = 1.f;
    bool gliding = false;
    float cut = 170.f, envOct = 2.f, fEnv = 0.f, fCoef = 0.99f, res = 0.05f, drive = 0.f;
    float subGain = 0.5f, oct2Gain = 0.f, trim = 0.3f, velGain = 0.8f, velTarget = 0.8f;
    float subLpfHz = 0.f;
    float pitchSt = 0.f, pitchCoef = 1.f;
    float ring = 0.f, last = 0.f;
    bool fAttack = false;
    float fAtkInc = 1.f;
    int midiNote = 36, midiCh = 0;
    bool midiOn = false;
    bool live = false;
    float end16 = 0.f;
    Ladder ladder, subLp;
    DcBlock dc;

    void kill() noexcept
    {
        on = false;
        midiOn = false;
        env.stage = 0;
        env.amp = 0;
        ladder.reset();
        subLp.reset();
        dc.reset();
        fEnv = 0;
        fAttack = false;
        pitchSt = 0.f;
    }
};

struct PolyV
{
    Env env;
    bool on = false;
    float phase[2] {};
    float freq = 440.f;
    float pan = 0.f;
    float cut = 2000.f, envOct = 1.f, fEnv = 0.f, fCoef = 0.99f;
    float hpfHz = 200.f;
    float detuneCents = 5.f;
    int waveB = 0;
    float trim = 0.15f, velGain = 0.5f;
    float delaySend = 0.f, revSend = 0.f;
    float noiseGain = 0.f, noiseEnv = 0.f, noiseCoef = 1.f, noiseFc = 2000.f;
    bool noiseBp = false;
    int uniN = 2;
    float uniSpread = 0.f;
    float uniPhase[5] {};
    float ringL = 0.f, ringR = 0.f, lastL = 0.f, lastR = 0.f;
    Svf lpR, hpR;
    int midiNote = 60, midiCh = 1;
    bool midiOn = false;
    float end16 = 0.f;
    int partials = 1;
    Svf lp, hp, noiseF;
    Lfsr noise;

    void kill() noexcept
    {
        on = false;
        midiOn = false;
        env.stage = 0;
        lp.reset();
        hp.reset();
        noiseF.reset();
        noiseEnv = 0.f;
    }
};

struct HitV
{
    Env env;
    bool on = false;
    int type = 0;
    float freq[4] {};
    float phase[8] {};
    int engine = 0;
    float pw = 0.5f, pwmPhase = 0.f, pwmHz = 0.f, pwmDepth = 0.f;
    float fmRatio = 1.f, fmIdx = 0.f, fmEnd = 0.f, fmCoef = 1.f;
    float uniCents = 0.f, pitchSt = 0.f, pitchStC = 1.f;
    int uniN = 1;
    float noiseGain = 0.f, noiseFc = 2000.f;
    bool noiseUseBp = false;
    float modPhase = 0, modEnv = 0, modCoef = 0;
    float pitchEnv = 0, pitchCoef = 0;
    float cut = 2000, fEnv = 1, fCoef = 0.9f;
    float noiseEnv = 0, noiseCoef = 0;
    int n = 1;
    float trim = 0.15f, velGain = 0.4f, pan = 0.f;
    float end16 = 0;
    int midiNote[4] {};
    int midiCh = 2;
    int midiCount = 0;
    bool midiOn = false;
    float ring = 0.f, last = 0.f;
    Svf lp, hp, hp2, noiseF;
    Lfsr noise;

    void kill() noexcept
    {
        on = false;
        midiOn = false;
        env.stage = 0;
        lp.reset();
        hp.reset();
    }
};

struct VoxV
{
    Env env;
    bool on = false;
    int vowel = 0;
    float phase = 0, freq = 200;
    float noiseEnv = 0;
    float trim = 0.15f, velGain = 0.4f;
    float revSend = 0.1f;
    float end16 = 0;
    float vib = 0.f, jit = 0.f;
    int jitHold = 0;
    float ring = 0.f, last = 0.f;
    int midiNote = 64, midiCh = 5;
    bool midiOn = false;
    Svf f1, f2, f3, hp;
    Lfsr noise;

    void kill() noexcept
    {
        on = false;
        midiOn = false;
        env.stage = 0;
        f1.reset();
        f2.reset();
        f3.reset();
    }
};

struct Duck
{
    float g = 1, target = 1, c = 0.1f;
    int hold = 0;
    bool arm = false;

    void trigger (float depthDb, float recoverMs, float sr, bool gate) noexcept
    {
        const float floor = dbToGain (-std::clamp (depthDb, 0.f, 48.f));
        target = floor;
        // A 0.1-0.2 ms duck dropped a sustained bass by up to 48 dB in a few samples:
        // an audible click on the kick. 2.5-3 ms is still tight and clean.
        c = coefFromMs (gate ? 2.5f : 3.0f, sr);
        // 20 ms was gone before an off-kick bass note. Hold long enough that
        // the next 16th is still down.
        hold = (int) ((gate ? 0.060f : 0.048f) * sr);
        arm = true;
        recover = std::max (20.f, recoverMs);
        gateMode = gate;
        srRef = sr;
    }

    float next() noexcept
    {
        if (arm)
        {
            g += (target - g) * c;
            if (std::fabs (g - target) < 1.0e-3f)
            {
                g = target;
                arm = false;
            }
        }
        else if (hold > 0)
        {
            --hold;
            if (hold == 0)
            {
                target = 1.f;
                const float relMs = std::max (8.f, recover - 20.f);
                c = coefFromMs (relMs * 0.45f, srRef);
            }
        }
        else
        {
            g += (1.f - g) * c;
            if (g > 0.999f)
                g = 1.f;
        }
        g = flush (std::clamp (g, 0.f, 1.f));
        return g;
    }

    float recover = 115.f, srRef = 48000.f;
    bool gateMode = false;
};

int clampPreset (const PresetBank& bank, int part, int i) noexcept
{
    return presetSlot (bank, part, i);
}

float velGainOf (int vel) noexcept
{
    const float n = std::max (0.f, vel / 112.f);
    return std::pow (n, 2.06f);
}

float midiHz (int note) noexcept
{
    return 440.f * std::pow (2.f, (note - 69) / 12.f);
}

} // namespace

struct Engine::Impl
{
    float sr = 48000.f;
    BassV bass, live;
    PolyV stab[8], pluck[4], pad[6];
    HitV hits[6];
    VoxV vox[4];
    DrumMachine drums;
    Duck duckB, duckP, duckS, duckH, duckPl, duckV;
    Smooth level[NumParts], outGain;
    std::vector<float> dL, dR;
    int dN = 0, dW = 0;
    float dTime = 1000.f;
    Svf dHp, dLp;
    juce::Reverb reverb;
    int reverbIdle = 1000000;
    DcBlock mainDc, mainDcR, drumDc, drumDcR;
    // Master mono below 120 Hz (LR4 on the side, mix path only). It used to be
    // L += LP(M) - LP(L) with a 2-pole LP: the side then only saw 1 - LP, a
    // 6 dB/oct shelf (about -3 dB at 60 Hz), so the plate's side sailed through.
    MonoBelow masterMono;
    // Plate return mono below 120 Hz: the drum room send comes back stereo
    // (width 0.68) after the DRUMS bus cut.
    MonoBelow plateMono;
    // DRUMS bus mono below 120 Hz (LR4 on the side), before the main sum and the stem.
    MonoBelow drumMono;
    MixRuntime mixRt;
    TruePeakGuard tpGuard;
    bool primed = false;
    bool fxHeld = false;
    double lastPos = -1.0;

    struct Play
    {
        bool on = false;
        int buf = 0;
        int pos = 0;
        int len = 0;
        float gain = 0.f;
        float ring = 0.f, last = 0.f;
        int atk = 0, atkN = 0; // short fade-in from pos 0
    };
    Play play[NumDrumVoices];
    int wrapIn = 0, wrapN = 0; // master fade-in after a loop wrap

    struct PendingOff
    {
        int left = 0;
        int ch = 0;
        int note = 0;
        bool on = false;
    };
    PendingOff pending[96];

    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const int n = (int) (sampleRate * 2.0) + 8;
        dL.assign ((size_t) n, 0.f);
        dR.assign ((size_t) n, 0.f);
        dN = n;
        dW = 0;
        reverb.setSampleRate (sampleRate);
        juce::Reverb::Parameters rp;
        rp.roomSize = 0.90f;
        rp.damping = 0.30f;
        rp.wetLevel = 1.f;
        rp.dryLevel = 0.f;
        rp.width = 0.68f;
        rp.freezeMode = 0.f;
        reverb.setParameters (rp);
        for (int i = 0; i < NumParts; ++i)
            level[i].setTime (18.f, sr);
        outGain.setTime (20.f, sr);
        // DC safety only: 20 Hz took about 1 dB off a kick's 40 Hz fundamental.
        mainDc.setCutoff (5.f, sr);
        mainDcR.setCutoff (5.f, sr);
        drumDc.setCutoff (25.f, sr);
        drumDcR.setCutoff (25.f, sr);
        drums.prepare (sr);
        mixRt.prepare (sampleRate);
        tpGuard.prepare (sampleRate);
        reset();
        primed = true;
    }

    void reset() noexcept
    {
        bass.kill();
        live.kill();
        for (auto& v : stab) v.kill();
        for (auto& v : pluck) v.kill();
        for (auto& v : pad) v.kill();
        for (auto& v : hits) v.kill();
        for (auto& v : vox) v.kill();
        drums.reset();
        duckB.g = duckP.g = duckS.g = duckH.g = duckPl.g = duckV.g = 1.f;
        std::fill (dL.begin(), dL.end(), 0.f);
        std::fill (dR.begin(), dR.end(), 0.f);
        dHp.reset();
        dLp.reset();
        reverb.reset();
        reverbIdle = 1000000;
        fxHeld = false;
        bass.ring = bass.last = live.ring = live.last = 0.f;
        wrapIn = 0;
        mainDc.reset();
        mainDcR.reset();
        drumDc.reset();
        drumDcR.reset();
        masterMono.reset();
        plateMono.reset();
        drumMono.reset();
        mixRt.reset();
        tpGuard.reset();
        lastPos = -1.0;
        for (auto& p : play)
            p = {};
        for (auto& p : pending)
            p = {};
    }

    void scheduleOff (int ch, int note, int samplesLeft)
    {
        for (auto& p : pending)
        {
            if (! p.on)
            {
                p.on = true;
                p.left = std::max (1, samplesLeft);
                p.ch = ch;
                p.note = note;
                return;
            }
        }
    }

    void cancelOff (juce::MidiBuffer& midi, int sample, int ch, int note)
    {
        for (auto& p : pending)
        {
            if (p.on && p.ch == ch && p.note == note)
            {
                midi.addEvent (juce::MidiMessage::noteOff (ch, note), sample);
                p.on = false;
            }
        }
    }

    void noteOff (juce::MidiBuffer& midi, int sample, int ch, int note, bool& flag) const
    {
        if (! flag)
            return;
        flag = false;
        midi.addEvent (juce::MidiMessage::noteOff (ch + 1, note), sample);
    }

    template <typename V>
    void releaseVoice (V& v, juce::MidiBuffer& midi, int sample)
    {
        v.env.release();
        noteOff (midi, sample, v.midiCh, v.midiNote, v.midiOn);
    }
};

Engine::Engine() : impl (std::make_unique<Impl>()) {}
Engine::~Engine() = default;

void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    impl->prepare (sr);
    sampler.prepare (sr);
    prepared = true;
    wasPlaying = false;
    for (int v = 0; v < NumDrumVoices; ++v)
        if (! samples[v].source.empty())
            loadDrumSample (v, samples[v].source.data(), (int) samples[v].source.size(), samples[v].sourceRate);
}

void Engine::reset() noexcept
{
    if (impl != nullptr)
        impl->reset();
    sampler.reset();
    wasPlaying = false;
}

void Engine::setDrumSampleSlot (DrumSampleSlot* slot) noexcept
{
    if (impl != nullptr)
        impl->drums.setSampleSlot (slot);
}

bool Engine::hasDrumSample (int voice) const noexcept
{
    return voice >= 0 && voice < NumDrumVoices && samples[voice].loaded.load (std::memory_order_acquire);
}

void Engine::clearDrumSample (int voice)
{
    if (voice < 0 || voice >= NumDrumVoices)
        return;
    samples[voice].loaded.store (false, std::memory_order_release);
    samples[voice].source.clear();
    samples[voice].sourceRate = 0.0;
}

void Engine::loadDrumSample (int voice, const float* mono, int numSamples, double sourceRate)
{
    if (voice < 0 || voice >= NumDrumVoices || mono == nullptr || numSamples <= 8 || sr < 1000.0)
        return;
    auto& s = samples[voice];
    if (s.source.data() != mono)
    {
        s.source.assign (mono, mono + std::min (numSamples, (int) (sourceRate * 2.0 + 8)));
        s.sourceRate = sourceRate;
    }
    const double srcRate = sourceRate > 1000.0 ? sourceRate : sr;
    const double seconds = std::min (2.0, (double) s.source.size() / srcRate);
    const int nDst = std::max (8, (int) (seconds * sr));
    const int pub = s.published.load (std::memory_order_acquire);
    const int rd = s.reader.load (std::memory_order_acquire);
    int w = (pub + 1) % SampleSlot::kBufs;
    for (int i = 0; i < SampleSlot::kBufs; ++i)
        if (i != pub && i != rd)
        {
            w = i;
            break;
        }
    if ((int) s.buf[w].size() < nDst)
        s.buf[w].assign ((size_t) nDst, 0.f);
    const double step = srcRate / sr;
    double p = 0.0;
    const int nSrc = (int) s.source.size();
    for (int i = 0; i < nDst; ++i)
    {
        if (p >= nSrc - 1)
        {
            s.buf[w][(size_t) i] = 0.f;
        }
        else
        {
            const int i0 = (int) p;
            const float f = (float) (p - i0);
            const float a = s.source[(size_t) i0];
            const float b = s.source[(size_t) std::min (nSrc - 1, i0 + 1)];
            s.buf[w][(size_t) i] = a + (b - a) * f;
        }
        p += step;
    }
    s.length[w] = nDst;
    s.published.store (w, std::memory_order_release);
    s.loaded.store (true, std::memory_order_release);
}

void Engine::process (const Groove& groove,
                      const EngineParams& params,
                      const Transport& transport,
                      float* mainL, float* mainR,
                      float* drumL, float* drumR,
                      int numSamples,
                      juce::MidiBuffer& midi,
                      bool patternMidi)
{
    if (! prepared || impl == nullptr || numSamples <= 0 || mainL == nullptr)
    {
        if (mainL != nullptr)
            for (int i = 0; i < numSamples; ++i)
            {
                mainL[i] = 0.f;
                if (mainR != nullptr)
                    mainR[i] = 0.f;
            }
        return;
    }

    auto& E = *impl;
    const float srF = E.sr;
    const PresetBank& bank = params.presets != nullptr ? *params.presets : *[] {
        static PresetBank defaults;
        static bool once = false;
        if (! once)
        {
            loadDefaultPresets (defaults);
            once = true;
        }
        return &defaults;
    }();

    const int bars = std::clamp (params.bars, 2, kMaxBars);
    const double loop16 = (double) bars * 16.0;
    const double bpm = std::clamp (transport.bpm, 20.0, 400.0);
    EngineParams play = params;
    play.bpm = (float) bpm;
    const double d16 = (bpm / 60.0) * 4.0 / sr;
    const bool justStarted = transport.playing && ! wasPlaying;
    double pos = transport.playing ? wrapLoop (transport.ppqStart * 4.0, loop16) : 0.0;
    if (justStarted)
        pos = wrapLoop (pos - d16 * 0.5, loop16);

    struct InEv
    {
        int sample;
        int note;
        int vel;
        int ch;
        bool on;
        bool allOff;
    };
    InEv incoming[128];
    int nIn = 0;
    for (const auto meta : midi)
    {
        if (nIn >= 128)
            break;
        const auto m = meta.getMessage();
        if (m.isNoteOn())
            incoming[nIn++] = { meta.samplePosition, m.getNoteNumber(), m.getVelocity(), m.getChannel(), true, false };
        else if (m.isNoteOff())
            incoming[nIn++] = { meta.samplePosition, m.getNoteNumber(), 0, m.getChannel(), false, false };
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            incoming[nIn++] = { meta.samplePosition, 0, 0, m.getChannel(), false, true };
    }
    midi.clear();

    const float toneB = params.tone[Bass] - 0.5f;
    const float punchB = params.punch[Bass] - 0.5f;
    BassPatch bassPatch = bank.bass[clampPreset (bank, Bass, params.preset[Bass])];
    applyDialectBass (bank, params.dialect, bassPatch);
    const auto& bp = bassPatch;
    const auto& sp = bank.stab[clampPreset (bank, Stab, params.preset[Stab])];
    const auto& hp = bank.hits[clampPreset (bank, Hits, params.preset[Hits])];
    const auto& pp = bank.pluck[clampPreset (bank, Pluck, params.preset[Pluck])];
    const auto& padP = bank.pad[clampPreset (bank, Pad, params.preset[Pad])];
    const auto& vp = bank.vox[clampPreset (bank, Vox, params.preset[Vox])];
    const auto& dp = bank.drums[clampPreset (bank, Drums, params.preset[Drums])];

    for (int i = 0; i < NumParts; ++i)
    {
        // A muted lane is exact silence. -80 dB still leaves a residual, and it
        // still excites the shared delay (300 Hz–2.5 kHz feedback) and the plate.
        if (params.mute[i])
            E.level[i].snap (0.f);
        else
            E.level[i].target = dbToGain (params.levelDb[i]);
    }
    E.outGain.target = dbToGain (params.outputDb);
    // The smoothers rest at silence. A transport start has to open them on this
    // sample, or the downbeat is ramped in over the level time.
    if (justStarted)
    {
        for (int i = 0; i < NumParts; ++i)
            if (! params.mute[i])
                E.level[i].snap (dbToGain (params.levelDb[i]));
        E.outGain.snap (dbToGain (params.outputDb));
    }

    if (wasPlaying && ! transport.playing)
    {
        // Stop kills the voices. Releasing them retriggers the bar-1 attack
        // into the free tail (the loop end is already gated quiet).
        auto offs = [&] (auto& v)
        {
            if (v.midiOn)
                midi.addEvent (juce::MidiMessage::noteOff (v.midiCh + 1, v.midiNote), 0);
            v.kill();
        };
        offs (E.bass);
        offs (E.live);
        for (auto& v : E.stab) offs (v);
        for (auto& v : E.pluck) offs (v);
        for (auto& v : E.pad) offs (v);
        for (auto& v : E.hits)
        {
            if (v.midiOn)
                for (int n = 0; n < v.midiCount; ++n)
                    midi.addEvent (juce::MidiMessage::noteOff (v.midiCh + 1, v.midiNote[n]), 0);
            v.kill();
        }
        for (auto& v : E.vox) offs (v);
        for (auto& p : E.pending)
        {
            if (p.on)
                midi.addEvent (juce::MidiMessage::noteOff (p.ch, p.note), 0);
            p.on = false;
        }
        for (auto& p : E.play)
            p = {};
        sampler.reset();
        E.drums.reset();
    }
    wasPlaying = transport.playing;
    const float ringC = expDecay (12.f, srF);

    const int key = (params.key % 12 + 12) % 12;
    const int bassBase = 12 * (std::clamp (bp.octave, 0, 3) + 1) + key;
    const int stabBase = 60 + key;
    const int hitBase = 60 + key;
    const int pluckBase = 72 + key;
    const int padBase = 60 + key;
    const int voxBase = 64 + key;

    auto swung = [&] (float start) { return swungStart (start, params.swing); };

    auto emitOn = [&] (int sample, int ch, int note, int vel, bool& flag, int& stored)
    {
        note = std::clamp (note, 0, 127);
        vel = std::clamp (vel, 1, 127);
        if (flag && stored == note)
            midi.addEvent (juce::MidiMessage::noteOff (ch + 1, stored), sample);
        stored = note;
        flag = true;
        midi.addEvent (juce::MidiMessage::noteOn (ch + 1, note, (juce::uint8) vel), sample);
    };

    if (params.mix != nullptr && params.mix->loaded)
    {
        auto idOf = [&] (int part) -> const char*
        {
            const int idx = presetSlot (bank, part, params.preset[part]);
            switch (part)
            {
                case Bass: return bank.bass[idx].id;
                case Stab: return bank.stab[idx].id;
                case Hits: return bank.hits[idx].id;
                case Pluck: return bank.pluck[idx].id;
                case Pad: return bank.pad[idx].id;
                case Vox: return bank.vox[idx].id;
                default: return nullptr;
            }
        };
        // MAIN mask bits are BASS, STAB, PLUCK, HITS, PAD, VOX (not the part enum order).
        static constexpr int kMaskBit[6] = { 0, 1, 3, 2, 4, 5 };
        MixBlockInfo info;
        info.dialect = params.dialect;
        info.sumMask = params.sumMask;
        info.bypassSumTrim = params.bypassSumTrim;
        int active = 0;
        for (int p = 0; p < 6; ++p)
        {
            info.presetIndex[p] = params.preset[p];
            info.presetId[p] = idOf (p);
            if (! params.mute[p])
                active |= 1 << kMaskBit[p];
        }
        info.activeMask = active;
        E.mixRt.beginBlock (*params.mix, info);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const double prev = pos;
        if (transport.playing)
        {
            pos = wrapLoop (pos + d16, loop16);
            // Loop wrap or relocate: the loop end faded the output to silence,
            // but kick tails are still sounding. Reopen on a 1 ms raised cosine.
            // Sample-true: voices already fade at the loop end; no master dip, so the
            // downbeat kick keeps its transient on every repeat.
            if (pos < prev && ! (justStarted && i == 0) && ! params.sampleTrue)
            {
                E.wrapN = std::max (8, (int) (0.001 * sr));
                E.wrapIn = E.wrapN;
            }
        }
        else
        {
            // Stop is silence. Killing voices is not enough: an attack that
            // began on the last playing sample keeps rising through the free
            // tail and reads as bar 1 starting again.
            mainL[i] = 0.f;
            if (mainR != nullptr)
                mainR[i] = 0.f;
            if (drumL != nullptr)
                drumL[i] = 0.f;
            if (drumR != nullptr)
                drumR[i] = 0.f;
            continue;
        }

        auto emitHeld = [&] (int part, int note, int vel, float st, float en)
        {
            note = std::clamp (note, 0, 127);
            vel = std::clamp (vel, 1, 127);
            midi.addEvent (juce::MidiMessage::noteOn (part + 1, note, (juce::uint8) vel), i);
            const int dur = std::max (1, (int) ((double) (en - st) * (60.0 / bpm) / 4.0 * sr));
            E.scheduleOff (part + 1, note, dur);
        };

        for (auto& pend : E.pending)
        {
            if (! pend.on)
                continue;
            if (--pend.left <= 0)
            {
                pend.on = false;
                midi.addEvent (juce::MidiMessage::noteOff (pend.ch, pend.note), i);
            }
        }

        for (int e = 0; e < nIn; ++e)
        {
            if (incoming[e].sample != i)
                continue;
            if (incoming[e].allOff)
            {
                E.live.env.release();
                if (E.live.midiOn)
                    midi.addEvent (juce::MidiMessage::noteOff (1, E.live.midiNote), i);
                E.live.midiOn = false;
            }
            else if (incoming[e].ch == 2 && incoming[e].on)
            {
                const int nIv = (sp.chordOn && sp.chordN > 1 && ! params.sampleTrue) ? std::min (4, sp.chordN) : 1;
                const ChordAsk ask = nIv > 1 ? chordAskOf (sp.chordIv, nIv) : ChordAsk {};
                bool chordSounding = false;
                for (int c = 0; c < nIv; ++c)
                {
                    const int note = std::clamp (incoming[e].note + (nIv == 1 ? 0 : sp.chordIv[c]), 0, 127);
                    const auto onMsg = juce::MidiMessage::noteOn (Stab + 1, note, (juce::uint8) std::clamp (incoming[e].vel, 1, 127));
                    if (chordSounding)
                    {
                        midi.addEvent (onMsg, i);
                        continue;
                    }
                    const auto taken = sampler.take (Stab, note, incoming[e].vel, 1.0e9f, bank, play, c == 0 ? ask : ChordAsk {});
                    if (taken.kind == 1)
                    {
                        midi.addEvent (onMsg, i);
                        chordSounding = c == 0 && taken.chordFile;
                        continue;
                    }
                    PolyV* slot = nullptr;
                    for (int p = 0; p < 8; ++p)
                        if (! E.stab[p].on)
                        {
                            slot = &E.stab[p];
                            break;
                        }
                    if (slot == nullptr)
                    {
                        slot = &E.stab[0];
                        for (int p = 1; p < 8; ++p)
                            if (E.stab[p].env.amp < slot->env.amp)
                                slot = &E.stab[p];
                        slot->ringL = slot->lastL;
                        slot->ringR = slot->lastR;
                        if (slot->midiOn)
                            midi.addEvent (juce::MidiMessage::noteOff (slot->midiCh + 1, slot->midiNote), i);
                        slot->midiOn = false;
                    }
                    const float punch = params.punch[Stab] - 0.5f;
                    const float tone = params.tone[Stab] - 0.5f;
                    slot->on = true;
                    slot->freq = std::clamp (midiHz (note), 20.f, srF * 0.45f);
                    slot->end16 = 1.0e9f;
                    slot->env.start (sp.aMs, sp.dMs * std::pow (2.f, -punch * 0.5f), dbToGain (sp.sDb), sp.rMs, srF);
                    slot->cut = sp.cutoffHz * std::pow (2.f, tone);
                    slot->envOct = sp.envOct + punch * 0.8f;
                    slot->fEnv = 1.f;
                    slot->fCoef = expDecay (std::max (4.f, sp.tauMs), srF);
                    slot->hpfHz = sp.hpfHz;
                    slot->detuneCents = sp.detuneCents;
                    slot->waveB = (c == 1) ? 1 : 0;
                    slot->pan = nIv > 1 ? ((c / (float) (nIv - 1)) * 2.f - 1.f) * sp.spread : 0.f;
                    slot->trim = dbToGain (sp.trimDb) * 0.16f * (taken.kind == 2 ? taken.synthGain : 1.f);
                    slot->velGain = velGainOf (incoming[e].vel);
                    slot->uniN = std::clamp (sp.unison, 1, 5);
                    slot->uniSpread = sp.spread;
                    for (float& ph : slot->uniPhase)
                        ph = 0.f;
                    slot->delaySend = sp.delaySend;
                    slot->revSend = sp.reverbSend;
                    slot->noiseGain = sp.ex.noiseDb > -80.f ? dbToGain (sp.ex.noiseDb) : 0.f;
                    slot->noiseEnv = slot->noiseGain > 0.f ? 1.f : 0.f;
                    slot->noiseCoef = expDecay (std::max (1.f, sp.ex.noiseDecayMs), srF);
                    slot->noiseFc = sp.ex.noiseBpHz > 1.f ? sp.ex.noiseBpHz : sp.ex.noiseHpfHz;
                    slot->noiseBp = sp.ex.noiseBpHz > 1.f;
                    slot->midiCh = Stab;
                    slot->lp.reset();
                    slot->hp.reset();
                    slot->lpR.reset();
                    slot->hpR.reset();
                    emitOn (i, Stab, note, incoming[e].vel, slot->midiOn, slot->midiNote);
                }
            }
            else if (incoming[e].ch == 2)
            {
                const int root = incoming[e].note;
                for (auto& v : E.stab)
                {
                    if (! v.midiOn)
                        continue;
                    const int rel = v.midiNote - root;
                    bool mine = rel == 0;
                    if (sp.chordOn)
                        for (int c = 0; c < sp.chordN; ++c)
                            if (sp.chordIv[c] == rel)
                                mine = true;
                    if (mine)
                    {
                        v.env.release();
                        midi.addEvent (juce::MidiMessage::noteOff (v.midiCh + 1, v.midiNote), i);
                        v.midiOn = false;
                        sampler.releaseNote (Stab, v.midiNote);
                    }
                }
            }
            else if (incoming[e].on)
            {
                const int note = incoming[e].note;
                const float hz = std::clamp (midiHz (note), 10.f, srF * 0.45f);
                const auto taken = sampler.take (Bass, note, incoming[e].vel, 1.0e9f, bank, play);
                if (taken.kind == 1)
                {
                    if (E.live.midiOn)
                        midi.addEvent (juce::MidiMessage::noteOff (1, E.live.midiNote), i);
                    E.live.kill();
                    midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) std::clamp (incoming[e].vel, 1, 127)), i);
                    continue;
                }
                const bool legato = E.live.on && E.live.env.stage != 0 && E.live.env.stage != 4 && bp.glideMs > 1.f;
                if (! legato)
                {
                    if (E.live.on)
                        E.live.ring = E.live.last;
                    E.live.env.start (bp.aMs, bp.dMs * std::pow (2.f, -punchB * 0.6f), dbToGain (bp.sDb), bp.rMs, srF);
                    E.live.fEnv = 1.f;
                    E.live.ladder.reset();
                }
                E.live.on = true;
                E.live.live = true;
                E.live.osc = bp.osc;
                E.live.targetFreq = hz;
                if (legato)
                {
                    E.live.gliding = true;
                    E.live.glideC = coefFromMs (std::max (5.f, bp.glideMs), srF);
                }
                else
                {
                    E.live.freq = hz;
                    E.live.gliding = false;
                }
                E.live.cut = bp.cutoffHz * std::pow (2.f, toneB * 1.2f);
                E.live.envOct = std::max (0.1f, bp.filterEnvOct + punchB * 1.4f);
                E.live.fCoef = expDecay (std::max (4.f, bp.filterTauMs * std::pow (2.f, -punchB * 1.2f)), srF);
                E.live.res = bp.resonance;
                E.live.drive = std::max (0.f, bp.driveDb + std::max (0.f, (params.tone[Bass] - 0.66f) / 0.34f) * 6.f);
                E.live.subGain = bp.subDb < -80.f ? 0.f : dbToGain (bp.subDb);
                E.live.subLpfHz = bp.ex.subLpfHz;
                E.live.pitchSt = bp.ex.pitchEnvSt;
                E.live.pitchCoef = expDecay (std::max (5.f, bp.ex.pitchEnvTauMs), srF);
                E.live.fAttack = bp.ex.filterAttackMs > 0.5f;
                E.live.fAtkInc = 1.f / std::max (1.f, bp.ex.filterAttackMs * 0.001f * srF);
                if (E.live.fAttack && ! legato)
                    E.live.fEnv = 0.f;
                E.live.oct2Gain = bp.oct2Db < -80.f ? 0.f : dbToGain (bp.oct2Db);
                E.live.trim = dbToGain (bp.trimDb) * 0.35f * (taken.kind == 2 ? taken.synthGain : 1.f);
                // A glide keeps the envelope open. Stepping velGain there is a one-sample click.
                const float liveVel = velGainOf (incoming[e].vel);
                if (! legato)
                    E.live.velGain = liveVel;
                E.live.velTarget = liveVel;
                E.live.dc.setCutoff (bp.hpfHz, srF);
                E.live.midiCh = 0;
                emitOn (i, 0, note, incoming[e].vel, E.live.midiOn, E.live.midiNote);
            }
            else if (incoming[e].note == E.live.midiNote)
            {
                E.live.env.release();
                if (E.live.midiOn)
                    midi.addEvent (juce::MidiMessage::noteOff (1, E.live.midiNote), i);
                E.live.midiOn = false;
                sampler.releaseNote (Bass, incoming[e].note);
            }
        }

        if (transport.playing)
        {
            auto crossedNote = [&] (float start, float len, float& st, float& en)
            {
                st = swung (start);
                en = st + len;
                if (en > (float) loop16)
                    en = (float) loop16;
                return en > st;
            };

            for (int ni = 0; ni < groove.lane[Bass].count; ++ni)
            {
                const auto& n = groove.lane[Bass].notes[ni];
                float st, en;
                if (! crossedNote (n.start, n.length, st, en))
                    continue;
                if (! crossed (prev, pos, st, loop16))
                    continue;
                const int semi = bassBase + n.chord[0];
                const float hz = std::clamp (midiHz (semi), 10.f, srF * 0.22f);
                // Sample-true: a bass sample rings its own length until the next bass note takes over
                // (Sampler::take releases it), the way a bass one-shot is played. Cut at the MIDI note
                // end, a median note heard 31% of the sample (Oct 20, Melak: "you're not fully using my
                // bass sounds").
                const auto taken = sampler.take (Bass, semi, n.vel, play.sampleTrue ? 1.0e9f : en, bank, play);
                if (taken.kind == 1)
                {
                    if (E.bass.midiOn)
                        midi.addEvent (juce::MidiMessage::noteOff (1, E.bass.midiNote), i);
                    E.bass.kill();
                    emitHeld (Bass, semi, n.vel, st, en);
                    continue;
                }
                const bool legato = E.bass.on && E.bass.env.stage != 0 && E.bass.env.stage != 4
                                    && ((n.flags & NoteGlide) != 0 || bp.glideMs > 1.f);
                if (! legato)
                {
                    if (E.bass.on)
                        E.bass.ring = E.bass.last;
                    E.bass.env.start (bp.aMs, bp.dMs * std::pow (2.f, -punchB * 0.6f), dbToGain (bp.gate ? -60.f : bp.sDb), bp.rMs, srF);
                    E.bass.fEnv = 1.f;
                }
                E.bass.on = true;
                E.bass.osc = bp.osc;
                E.bass.targetFreq = hz;
                E.bass.end16 = en;
                if (legato && bp.glideMs > 0.5f)
                {
                    E.bass.gliding = true;
                    E.bass.glideC = coefFromMs (bp.glideMs, srF);
                }
                else
                {
                    E.bass.freq = hz;
                    E.bass.gliding = false;
                }
                E.bass.cut = bp.cutoffHz * std::pow (2.f, toneB * 1.2f);
                E.bass.envOct = std::max (0.05f, bp.filterEnvOct + punchB * 1.4f);
                E.bass.fCoef = expDecay (std::max (3.f, bp.filterTauMs * std::pow (2.f, -punchB * 1.2f)), srF);
                E.bass.res = bp.resonance;
                E.bass.drive = std::max (0.f, bp.driveDb + std::max (0.f, (params.tone[Bass] - 0.66f) / 0.34f) * 6.f);
                E.bass.subGain = bp.subDb < -80.f ? 0.f : dbToGain (bp.subDb);
                E.bass.subLpfHz = bp.ex.subLpfHz;
                E.bass.pitchSt = bp.ex.pitchEnvSt;
                E.bass.pitchCoef = expDecay (std::max (5.f, bp.ex.pitchEnvTauMs), srF);
                E.bass.fAttack = bp.ex.filterAttackMs > 0.5f;
                E.bass.fAtkInc = 1.f / std::max (1.f, bp.ex.filterAttackMs * 0.001f * srF);
                if (E.bass.fAttack && ! legato)
                    E.bass.fEnv = 0.f;
                E.bass.oct2Gain = 0.f;
                E.bass.trim = dbToGain (bp.trimDb) * 0.38f * (taken.kind == 2 ? taken.synthGain : 1.f);
                // Glide (or an explicit slide) leaves the amp open. Apply the new
                // velocity through velTarget so the sounding note does not step.
                const float bassVel = velGainOf (n.vel);
                if (! legato)
                    E.bass.velGain = bassVel;
                E.bass.velTarget = bassVel;
                E.bass.dc.setCutoff (bp.hpfHz, srF);
                E.bass.midiCh = 0;
                emitOn (i, 0, semi, n.vel, E.bass.midiOn, E.bass.midiNote);
            }

            auto firePoly = [&] (PolyV* pool, int nPool, int part, int base, const auto& patch, bool padLike)
            {
                juce::ignoreUnused (patch);
                for (int ni = 0; ni < groove.lane[part].count; ++ni)
                {
                    const auto& n = groove.lane[part].notes[ni];
                    float st, en;
                    if (! crossedNote (n.start, n.length, st, en) || ! crossed (prev, pos, st, loop16))
                        continue;
                    int count = std::max (1, (int) n.nChord);
                    int iv[4];
                    for (int c = 0; c < count && c < 4; ++c)
                        iv[c] = n.chord[c];
                    // Sample-true: a stab note is the stab, not a parallel chord re-pitched from one file.
                    if (part == Stab && sp.chordOn && count <= 1 && sp.chordN > 1 && ! params.sampleTrue)
                    {
                        const int root = count == 1 ? (int) n.chord[0] : 0;
                        count = std::min (4, sp.chordN);
                        for (int c = 0; c < count; ++c)
                            iv[c] = std::clamp (root + sp.chordIv[c], -48, 48);
                        // Parallel chord memory is written for minor. A locked scale
                        // folds it in, so the stab stays inside the picked scale.
                        if (params.scale != ScaleAuto)
                            for (int c = 0; c < count; ++c)
                                iv[c] = quantizeToScale (params.scale, iv[c]);
                    }
                    const Extras& ex = part == Stab ? sp.ex : (part == Pluck ? pp.ex : padP.ex);
                    const ChordAsk ask = chordAskOf (iv, std::min (count, 4));
                    // Sample-true: the chord folds as one block around its centre (Sampler::take).
                    int anchor = -1;
                    if (params.sampleTrue && count > 1)
                    {
                        int sum = 0;
                        for (int c = 0; c < count && c < 4; ++c)
                            sum += iv[c];
                        anchor = std::clamp (base + (int) std::lround ((double) sum / (double) std::min (count, 4)), 0, 127);
                    }
                    // A recorded chord sounds every tone from its root: the other
                    // notes go to MIDI only, never stacked through the sampler.
                    bool chordSounding = false;
                    for (int c = 0; c < count; ++c)
                    {
                        const int note = std::clamp (base + iv[c], 0, 127);
                        if (chordSounding)
                        {
                            emitHeld (part, note, n.vel, st, en);
                            continue;
                        }
                        const auto taken = sampler.take (part, note, n.vel, en, bank, play, c == 0 ? ask : ChordAsk {}, anchor);
                        if (taken.kind == 1)
                        {
                            emitHeld (part, note, n.vel, st, en);
                            chordSounding = c == 0 && taken.chordFile;
                            continue;
                        }
                        PolyV* slot = nullptr;
                        for (int p = 0; p < nPool; ++p)
                            if (! pool[p].on)
                            {
                                slot = &pool[p];
                                break;
                            }
                        if (slot == nullptr)
                        {
                            slot = &pool[0];
                            for (int p = 1; p < nPool; ++p)
                                if (pool[p].env.amp < slot->env.amp)
                                    slot = &pool[p];
                            slot->ringL = slot->lastL;
                            slot->ringR = slot->lastR;
                            if (slot->midiOn)
                                midi.addEvent (juce::MidiMessage::noteOff (slot->midiCh + 1, slot->midiNote), i);
                            slot->midiOn = false;
                        }
                        const float tone = params.tone[part] - 0.5f;
                        const float punch = params.punch[part] - 0.5f;
                        slot->on = true;
                        slot->freq = std::clamp (midiHz (note), 20.f, srF * 0.45f);
                        slot->end16 = en;
                        slot->partials = 1;
                        const float a = padLike ? padP.attackMs : (part == Pluck ? 2.f : sp.aMs);
                        const float d = (part == Pluck ? pp.decayMs : (padLike ? 80.f : sp.dMs)) * std::pow (2.f, -punch * 0.5f);
                        const float sG = padLike ? 1.f : (part == Pluck ? 0.f : dbToGain (sp.sDb));
                        const float r = padLike ? padP.releaseMs : (part == Pluck ? 40.f : sp.rMs);
                        slot->env.start (a, d, sG, r, srF);
                        if (part == Stab && ! sp.hold && ! padLike)
                            slot->env.sustain = dbToGain (sp.sDb);
                        slot->cut = (part == Pluck ? pp.cutoffHz : (padLike ? padP.cutoffHz : sp.cutoffHz)) * std::pow (2.f, tone * 1.0f);
                        slot->envOct = (part == Stab ? sp.envOct : 0.4f) + punch * 0.8f;
                        slot->fEnv = 1.f;
                        slot->fCoef = expDecay (std::max (4.f, (part == Stab ? sp.tauMs : 40.f) * std::pow (2.f, -punch)), srF);
                        slot->hpfHz = part == Stab ? sp.hpfHz : (padLike ? 250.f : 180.f);
                        slot->detuneCents = part == Stab ? sp.detuneCents : (padLike ? padP.detuneCents : 6.f);
                        slot->waveB = (part == Stab && c == 1) ? 1 : 0;
                        slot->pan = count > 1 ? ((c / (float) (count - 1)) * 2.f - 1.f) * (part == Stab ? sp.spread : 0.7f) : 0.f;
                        slot->trim = dbToGain (part == Stab ? sp.trimDb : (part == Pluck ? pp.trimDb : padP.trimDb))
                                   * (padLike ? 0.22f : 0.16f)
                                   * (taken.kind == 2 ? taken.synthGain : 1.f);
                        slot->velGain = velGainOf (n.vel);
                        slot->uniN = part == Stab ? std::clamp (sp.unison, 1, 5) : 2;
                        slot->uniSpread = part == Stab ? sp.spread : 0.f;
                        for (float& ph : slot->uniPhase)
                            ph = 0.f;
                        const bool lastOfPattern = part == Stab && n.start >= (float) ((bars - 1) * 16 + 15) - 0.05f;
                        slot->delaySend = (part == Stab && ! lastOfPattern) ? sp.delaySend : 0.f;
                        slot->revSend = part == Stab ? sp.reverbSend : (padLike ? padP.reverbSend : 0.05f);
                        slot->noiseGain = ex.noiseDb > -80.f ? dbToGain (ex.noiseDb) : 0.f;
                        slot->noiseEnv = slot->noiseGain > 0.f ? 1.f : 0.f;
                        slot->noiseCoef = expDecay (std::max (1.f, ex.noiseDecayMs), srF);
                        slot->noiseFc = ex.noiseBpHz > 1.f ? ex.noiseBpHz : ex.noiseHpfHz;
                        slot->noiseBp = ex.noiseBpHz > 1.f;
                        slot->midiCh = part;
                        slot->lp.reset();
                        slot->hp.reset();
                        slot->lpR.reset();
                        slot->hpR.reset();
                        emitOn (i, part, note, n.vel, slot->midiOn, slot->midiNote);
                    }
                }
            };
            firePoly (E.stab, 8, Stab, stabBase, sp, false);
            firePoly (E.pluck, 4, Pluck, pluckBase, pp, false);
            firePoly (E.pad, 6, Pad, padBase, padP, true);

            for (int ni = 0; ni < groove.lane[Hits].count; ++ni)
            {
                const auto& n = groove.lane[Hits].notes[ni];
                float st, en;
                if (! crossedNote (n.start, n.length, st, en) || ! crossed (prev, pos, st, loop16))
                    continue;
                const int primary = std::clamp (hitBase + n.chord[0], 0, 127);
                const auto taken = sampler.take (Hits, primary, n.vel, en, bank, play);
                if (taken.kind == 1)
                {
                    const int count = std::clamp ((int) n.nChord, 1, 4);
                    for (int c = 0; c < count; ++c)
                        emitHeld (Hits, std::clamp (hitBase + n.chord[c], 0, 127), n.vel, st, en);
                    continue;
                }
                HitV* slot = nullptr;
                for (auto& h : E.hits)
                    if (! h.on)
                    {
                        slot = &h;
                        break;
                    }
                if (slot == nullptr)
                {
                    slot = &E.hits[0];
                    for (auto& h : E.hits)
                        if (h.env.amp < slot->env.amp)
                            slot = &h;
                    slot->ring = slot->last;
                }
                if (slot->midiOn)
                    for (int c = 0; c < slot->midiCount; ++c)
                        midi.addEvent (juce::MidiMessage::noteOff (slot->midiCh + 1, slot->midiNote[c]), i);
                slot->midiOn = false;
                const float punch = params.punch[Hits] - 0.5f;
                const float tone = params.tone[Hits] - 0.5f;
                slot->on = true;
                slot->type = n.timbre % 5;
                slot->n = std::clamp ((int) n.nChord, 1, 4);
                slot->end16 = en;
                const float hitDec = (slot->type == 2 ? 280.f : (slot->type == 1 ? 220.f : 180.f)) * hp.decayScale;
                slot->env.start (1.f, hitDec * std::pow (2.f, -punch * 0.5f), 0.f, 200.f, srF);
                slot->fEnv = 1.f;
                slot->cut = 1800.f * hp.brightness * std::pow (2.f, tone);
                slot->fCoef = expDecay (slot->type == 1 ? 40.f : 70.f, srF);
                slot->modEnv = 1.f;
                slot->modCoef = expDecay (50.f, srF);
                slot->pitchEnv = slot->type == 2 ? 1.f : 0.f;
                slot->pitchCoef = expDecay (160.f, srF);
                slot->noiseEnv = (slot->type == 3 || slot->type == 1) ? 1.f : 0.f;
                slot->noiseCoef = expDecay (slot->type == 3 ? 45.f : 12.f, srF);
                slot->trim = dbToGain (hp.trimDb) * 0.2f * (taken.kind == 2 ? taken.synthGain : 1.f);
                slot->velGain = velGainOf (n.vel);
                slot->engine = hp.ex.engine;
                slot->pw = hp.ex.pw;
                slot->pwmHz = hp.ex.pwmHz;
                slot->pwmDepth = hp.ex.pwmDepth;
                slot->pwmPhase = 0.f;
                slot->fmRatio = hp.ex.fmRatio;
                slot->fmIdx = std::clamp (hp.ex.fmIndex, 0.f, 10.f) * slot->velGain;
                slot->fmEnd = hp.ex.fmIndexEnd;
                slot->fmCoef = expDecay (std::max (5.f, hp.ex.fmDecayMs), srF);
                slot->uniN = std::clamp (hp.ex.unison, 1, 5);
                slot->uniCents = std::clamp (hp.ex.detuneCents, 0.f, 50.f);
                slot->pitchSt = std::clamp (hp.ex.pitchEnvSt, -24.f, 24.f);
                slot->pitchStC = expDecay (std::max (5.f, hp.ex.pitchEnvTauMs), srF);
                slot->noiseGain = hp.ex.noiseDb > -80.f ? dbToGain (hp.ex.noiseDb) : 0.f;
                slot->noiseFc = hp.ex.noiseBpHz > 1.f ? hp.ex.noiseBpHz : hp.ex.noiseHpfHz;
                slot->noiseUseBp = hp.ex.noiseBpHz > 1.f;
                if (slot->engine != 0 && slot->noiseGain > 0.f)
                {
                    slot->noiseEnv = 1.f;
                    slot->noiseCoef = expDecay (std::max (1.f, hp.ex.noiseDecayMs), srF);
                }
                slot->pan = (slot->type == 2 || slot->type == 4) ? 0.f : 0.15f;
                slot->hp.reset();
                slot->hp2.reset();
                slot->midiCh = Hits;
                slot->midiCount = slot->n;
                slot->lp.reset();
                for (int c = 0; c < slot->n; ++c)
                {
                    const int note = std::clamp (hitBase + n.chord[c], 0, 127);
                    slot->freq[c] = midiHz (note);
                    slot->phase[c] = 0.f;
                    slot->midiNote[c] = note;
                    midi.addEvent (juce::MidiMessage::noteOn (Hits + 1, note, (juce::uint8) n.vel), i);
                }
                slot->midiOn = true;
            }

            for (int ni = 0; ni < groove.lane[Vox].count; ++ni)
            {
                const auto& n = groove.lane[Vox].notes[ni];
                float st, en;
                if (! crossedNote (n.start, n.length, st, en) || ! crossed (prev, pos, st, loop16))
                    continue;
                const int note = std::clamp (voxBase + n.chord[0], 0, 127);
                const auto taken = sampler.take (Vox, note, n.vel, en, bank, play);
                if (taken.kind == 1)
                {
                    emitHeld (Vox, note, n.vel, st, en);
                    continue;
                }
                VoxV* slot = nullptr;
                for (auto& v : E.vox)
                    if (! v.on)
                    {
                        slot = &v;
                        break;
                    }
                if (slot == nullptr)
                {
                    slot = &E.vox[0];
                    for (auto& v : E.vox)
                        if (v.env.amp < slot->env.amp)
                            slot = &v;
                    slot->ring = slot->last;
                }
                if (slot->midiOn)
                    midi.addEvent (juce::MidiMessage::noteOff (slot->midiCh + 1, slot->midiNote), i);
                const float punch = params.punch[Vox] - 0.5f;
                slot->on = true;
                slot->vowel = n.timbre % 4;
                slot->freq = std::clamp (midiHz (note), 40.f, srF * 0.4f);
                slot->phase = 0;
                slot->end16 = en;
                slot->env.start (3.f, vp.decayMs * std::pow (2.f, -punch * 0.4f), 0.f, 30.f, srF);
                slot->noiseEnv = (slot->vowel == 2) ? vp.breath : vp.breath * 0.25f;
                slot->trim = dbToGain (vp.trimDb) * 0.22f * (taken.kind == 2 ? taken.synthGain : 1.f);
                slot->velGain = velGainOf (n.vel);
                slot->revSend = vp.reverbSend;
                slot->midiCh = Vox;
                slot->f1.reset();
                slot->f2.reset();
                slot->f3.reset();
                emitOn (i, Vox, note, n.vel, slot->midiOn, slot->midiNote);
            }

            const int step = (int) std::floor (pos);
            const int prevStep = (int) std::floor (prev);
            auto stepHit = [&] (int t)
            {
                if (t < 0 || t >= bars * 16)
                    return;
                // Swing: odd 16ths (e/a) are late, so they must cross the swung position, not the grid line.
                const float st = swung ((float) t);
                if (! crossed (prev, pos, st, loop16))
                    return;
                // DRUMS muted: DUCK falls back to the host quarter grid. While the lane
                // plays, duck follows this function's kick note-ons instead.
                // Mix path: muting drums must not retarget duck, or SOLO/MUTE/FULL
                // bass envelopes stop nulling. Quarter-grid fallback stays on the legacy path.
                if (params.mix == nullptr && params.mute[Drums] && params.duck && (t % 4) == 0)
                {
                    const float depth = std::max (0.f, bp.duckDb + punchB * 8.f);
                    const float rec = bp.duckRecoverMs;
                    // Bass attacks sit off the kick. Hold through the next 16th.
                    // The preset recover stays as written: stretching it past the
                    // UK MIN bass note drops that lane out of its loudness window.
                    E.duckB.trigger (depth, rec, srF, bp.gate);
                    E.duckP.trigger (padP.ex.duckDb >= 0.f ? padP.ex.duckDb : std::min (depth, 6.f),
                                    padP.ex.duckDb >= 0.f ? padP.ex.duckRecoverMs : rec, srF, false);
                    E.duckS.trigger (sp.ex.duckDb >= 0.f ? sp.ex.duckDb : 3.f, rec, srF, false);
                }
                const float accentAmt = params.drumsAccent;
                const bool accent = groove.drums.accent[t] != 0;
                const float acc = accent ? std::pow (10.f, accentAmt * 8.f / 20.f) : 1.f;
                // Sample kits: the generator writes no step accents, so DRUMS ACCENT
                // sets how hard the kick and clap land on the quarter grid.
                // The 0.6 default is unity; 0 is -4.8 dB, 1 is +3.2 dB.
                const float quarterAcc = params.sampleShaping && (t % 4) == 0
                                             ? std::pow (10.f, (accentAmt - 0.6f) * 8.f / 20.f)
                                             : 1.f;
                const float tone = params.tone[Drums] - 0.5f;
                const float punch = params.punch[Drums] - 0.5f;
                auto tuneOf = [&] (int voice)
                {
                    return std::pow (2.f, params.drumTuneSt[voice] / 12.f);
                };
                auto decayOf = [&] (int voice)
                {
                    return std::pow (2.f, (params.drumDecay[voice] - 0.5f) * 2.f);
                };
                auto levelOf = [&] (int voice)
                {
                    return dbToGain (params.drumLevelDb[voice]);
                };
                auto accentOf = [&] (int voice)
                {
                    return params.drumAccentOn[voice]
                               ? std::pow (10.f, params.drumAccentAmt[voice] * 6.f / 20.f)
                               : 1.f;
                };

                auto startSample = [&] (int voice, int vel, float stepGain) -> bool
                {
                    if (vel <= 0 || voice < 0 || voice >= NumDrumVoices)
                        return false;
                    // An active user slot owns the voice, including while it is
                    // loading or missing. The dropped WAV and the preset sample wait.
                    const int slot = userSlotForDrum (voice);
                    if (slot >= 0 && params.userSlots != nullptr
                        && params.userSlots->slots[slot].mode != 0)
                        return false;
                    auto& s = samples[voice];
                    if (! s.loaded.load (std::memory_order_acquire))
                        return false;
                    const int b = s.published.load (std::memory_order_acquire);
                    const int len = s.length[b];
                    if (len < 8 || (int) s.buf[b].size() < len)
                        return false;
                    s.reader.store (b, std::memory_order_release);
                    auto& pl = E.play[voice];
                    // The old hit's last output (its ring included) decays under
                    // the new one, so a retrigger never steps. When the voice is
                    // already off, last is the decaying ring itself.
                    pl.ring = pl.last;
                    pl.on = true;
                    pl.buf = b;
                    pl.pos = 0;
                    pl.len = len;
                    // A dropped WAV often starts on a non-zero sample (trimmed to the
                    // transient). 0.6 ms raised cosine: no step, transient kept.
                    pl.atkN = std::max (8, (int) (0.0006f * srF));
                    pl.atk = 0;
                    pl.gain = stepGain * (vel / 127.f) * dbToGain (dp.trimDb) * levelOf (voice) * accentOf (voice);
                    return true;
                };

                auto drumOn = [&] (int note, int vel, float seconds)
                {
                    if (! patternMidi || vel <= 0)
                        return;
                    note = std::clamp (note, 0, 127);
                    vel = std::clamp (vel, 1, 127);
                    E.cancelOff (midi, i, 10, note);
                    midi.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) vel), i);
                    E.scheduleOff (10, note, std::max (1, (int) (seconds * srF)));
                };

                auto patchDecay = [&] (int voice) noexcept
                {
                    switch (voice)
                    {
                        case Kick: return dp.kickDecayMs / 280.f;
                        case Snare: return dp.snareDecayMs / 160.f;
                        case Clap: return dp.clapDecayMs / 180.f;
                        case HatC: return dp.hatDecayMs / 45.f;
                        case HatO:
                        case Ride: return dp.hatOpenDecayMs / 280.f;
                        case TomLow:
                        case TomMid: return dp.tomDecayMs / 260.f;
                        default: return 1.f;
                    }
                };
                auto midiHold = [] (int voice) noexcept
                {
                    switch (voice)
                    {
                        case Kick: return 0.18f;
                        case Snare: return 0.14f;
                        case Clap: return 0.16f;
                        case Rim: return 0.04f;
                        case TomLow: return 0.20f;
                        case TomMid: return 0.18f;
                        case HatC: return 0.05f;
                        case HatO: return 0.22f;
                        case Ride: return 0.28f;
                        case Perc: return 0.08f;
                        default: return 0.12f;
                    }
                };

                // Closed hat last so it chokes the open hat. Step accent is kick and clap only.
                const int drumOrder[] = { Kick, Snare, Clap, Rim, TomLow, TomMid, Perc, Ride, HatO, HatC };
                for (int voice : drumOrder)
                {
                    const int vel = groove.drums.vel[voice][t];
                    if (vel <= 0)
                        continue;
                    const float stepAcc = (voice == Kick || voice == Clap) ? acc : 1.f;
                    const float sampleGain = accentOf (voice) * ((voice == Kick || voice == Clap) ? quarterAcc : 1.f);
                    // Mix path: muting drums must not drop the kick duck, or SOLO/MUTE/FULL
                    // bass envelopes stop nulling. The legacy path still skips it.
                    if (voice == Kick && params.duck && (params.mix != nullptr || ! params.mute[Drums]))
                    {
                        const float depth = std::max (0.f, bp.duckDb + punchB * 8.f);
                        const float rec = bp.duckRecoverMs;
                        E.duckB.trigger (depth, rec, srF, bp.gate);
                        E.duckP.trigger (padP.ex.duckDb >= 0.f ? padP.ex.duckDb : std::min (depth, 6.f),
                                        padP.ex.duckDb >= 0.f ? padP.ex.duckRecoverMs : rec, srF, padP.ex.duckGate);
                        E.duckS.trigger (sp.ex.duckDb >= 0.f ? sp.ex.duckDb : 3.f,
                                        sp.ex.duckDb >= 0.f ? sp.ex.duckRecoverMs : rec, srF, sp.ex.duckGate);
                        if (hp.ex.duckDb >= 0.f)
                            E.duckH.trigger (hp.ex.duckDb, hp.ex.duckRecoverMs, srF, hp.ex.duckGate);
                        if (pp.ex.duckDb >= 0.f)
                            E.duckPl.trigger (pp.ex.duckDb, pp.ex.duckRecoverMs, srF, pp.ex.duckGate);
                        if (vp.ex.duckDb >= 0.f)
                            E.duckV.trigger (vp.ex.duckDb, vp.ex.duckRecoverMs, srF, vp.ex.duckGate);
                    }
                    drumOn (drumMidiNote (voice), vel, midiHold (voice));

                    // Sample kits play the file, then the rest of the DRUMS pack.
                    // If that slot is missing or the library is absent, takeDrum
                    // returns the 909 gain so the hit is not silence.
                    // Plugin builds have no circuit kit: the 909 never plays.
#if ROLLA_PLUGIN_BUILD
                    const bool circuitKit = false;
#else
                    const bool circuitKit = dp.sample.mode == SampleMode::Synth;
#endif
                    float keep = 0.f;
                    if (startSample (voice, vel, stepAcc))
                        keep = 0.f;
                    else if (DrumSampleSlot* slot = E.drums.sampleSlot())
                    {
                        DrumSampleRequest req;
                        req.voice = voice;
                        req.velocity = vel;
                        req.tuneRatio = tuneOf (voice);
                        req.decayScale = decayOf (voice);
                        req.levelGain = levelOf (voice) * accentOf (voice) * stepAcc * dbToGain (dp.trimDb);
                        req.folder = drumSampleFolder (voice);
                        req.key = drumVoiceKey (voice);
                        req.midiNote = drumMidiNote (voice);
                        const float slotKeep = slot->trigger (req);
                        if (slotKeep <= 0.001f)
                            keep = 0.f;
                        else if (circuitKit)
                            keep = slotKeep;
                        else
                            keep = sampler.takeDrum (voice, vel, bank, play, sampleGain);
                    }
                    else if (circuitKit)
                        keep = 1.f;
                    else
                        keep = sampler.takeDrum (voice, vel, bank, play, sampleGain);

                    if (keep > 0.001f)
                    {
                        DrumHit hit;
                        hit.voice = voice;
                        hit.velocity = vel;
                        hit.synthKeep = keep;
                        hit.tuneRatio = tuneOf (voice);
                        hit.decayScale = decayOf (voice);
                        hit.patchDecay = patchDecay (voice);
                        hit.levelGain = levelOf (voice) * accentOf (voice) * dbToGain (dp.trimDb);
                        hit.stepAccent = stepAcc;
                        hit.tone = tone;
                        hit.punch = punch;
                        hit.clickScale = dp.kickClick / 0.45f;
                        hit.hatHpScale = dp.hatHpHz / 6500.f;
                        hit.snareTone = dp.snareTone;
                        hit.snareSnap = dp.snareSnap;
                        hit.key = params.key;
                        hit.dialect = params.dialect;
                        E.drums.trigger (hit);
                    }
                }
                juce::ignoreUnused (prevStep, step);
            };

            // A 16th can be crossed even when the integer step doesn't change, because swing
            // moves e/a off the grid. Scan a small window of steps around the playhead.
            const int centre = (int) std::floor (pos);
            for (int t = centre - 2; t <= centre + 2; ++t)
            {
                int wrapped = t;
                if (wrapped < 0)
                    wrapped += bars * 16;
                if (wrapped >= bars * 16)
                    wrapped -= bars * 16;
                stepHit (wrapped);
            }
        }

        auto releaseIfEnded = [&] (auto& v)
        {
            if (! v.on || ! transport.playing)
                return;
            if (crossed (prev, pos, v.end16, loop16))
            {
                v.env.release();
                if (v.midiOn)
                {
                    midi.addEvent (juce::MidiMessage::noteOff (v.midiCh + 1, v.midiNote), i);
                    v.midiOn = false;
                }
            }
        };
        releaseIfEnded (E.bass);
        for (auto& v : E.stab) releaseIfEnded (v);
        for (auto& v : E.pluck) releaseIfEnded (v);
        for (auto& v : E.pad) releaseIfEnded (v);
        for (auto& v : E.hits)
        {
            if (v.on && transport.playing && crossed (prev, pos, v.end16, loop16))
            {
                v.env.release();
                if (v.midiOn)
                    for (int c = 0; c < v.midiCount; ++c)
                        midi.addEvent (juce::MidiMessage::noteOff (v.midiCh + 1, v.midiNote[c]), i);
                v.midiOn = false;
            }
        }
        for (auto& v : E.vox) releaseIfEnded (v);

        auto renderBass = [&] (BassV& v) -> float
        {
            if (! v.on)
            {
                if (std::fabs (v.ring) < 1.0e-6f)
                    return 0.f;
                v.ring = flush (v.ring * ringC);
                v.last = v.ring;
                return v.ring;
            }
            if (v.gliding)
            {
                v.freq += (v.targetFreq - v.freq) * v.glideC;
                if (std::fabs (v.freq - v.targetFreq) / std::max (1.f, v.targetFreq) < 0.001f)
                {
                    v.freq = v.targetFreq;
                    v.gliding = false;
                }
            }
            const float sr2 = srF * 2.f;
            float playF = v.freq;
            if (std::fabs (v.pitchSt) > 1.0e-4f)
            {
                playF *= std::pow (2.f, v.pitchSt / 12.f);
                v.pitchSt *= v.pitchCoef;
            }
            float acc = 0.f;
            for (int k = 0; k < 2; ++k)
            {
                const float dt = std::min (playF / sr2, 0.45f);
                float x = 0.f;
                if (v.osc == 1)
                    x = triPhase (v.phase);
                else if (v.osc == 2)
                    x = sinePhase (v.phase) + dbToGain (-11.f) * sinePhase (std::fmod (v.phase * 2.f, 1.f));
                else if (v.osc == 3)
                    x = sinePhase (v.phase) + dbToGain (-3.f) * sinePhase (std::fmod (v.phase * 2.f, 1.f))
                        + dbToGain (-17.f) * sinePhase (std::fmod (v.phase * 4.f, 1.f));
                else
                    x = blepSaw (v.phase, dt);
                if (v.drive > 0.2f)
                    x = fastTanh (x * (1.f + v.drive * 0.25f));
                const float fc = std::clamp (v.cut * std::pow (2.f, v.envOct * v.fEnv), 30.f, sr2 * 0.4f);
                x = v.ladder.process (x, fc, v.res, sr2);
                acc += std::clamp (x, -3.f, 3.f);
                advancePhase (v.phase, dt);
            }
            if (v.fAttack)
            {
                v.fEnv += v.fAtkInc;
                if (v.fEnv >= 1.f)
                {
                    v.fEnv = 1.f;
                    v.fAttack = false;
                }
            }
            else
            {
                v.fEnv *= v.fCoef;
                if (v.fEnv < 1.0e-5f)
                    v.fEnv = 0.f;
            }
            float y = acc * 0.5f;
            if (v.subGain > 0.001f)
            {
                const float dt = std::min (playF / srF, 0.45f);
                float sub = sinePhase (v.subPhase);
                advancePhase (v.subPhase, dt);
                if (v.subLpfHz > 1.f)
                    sub = v.subLp.process (sub, v.subLpfHz, 0.f, srF);
                y += v.subGain * sub;
            }
            if (std::fabs (v.velGain - v.velTarget) > 1.0e-6f)
            {
                // 5 ms covers a ghost (80) against an accent (112), about a 6 dB move.
                const float step = std::max (v.velGain, v.velTarget) / std::max (1.f, 0.005f * srF);
                if (v.velGain + step < v.velTarget)
                    v.velGain += step;
                else if (v.velGain - step > v.velTarget)
                    v.velGain -= step;
                else
                    v.velGain = v.velTarget;
            }
            y = v.dc.process (y) * v.env.amp * v.velGain * v.trim;
            y += v.ring;
            v.ring = flush (v.ring * ringC);
            if (! v.env.tick())
                v.kill();
            v.last = y;
            return flush (y);
        };

        auto renderPoly = [&] (PolyV& v) -> std::pair<float, float>
        {
            auto decayRing = [&]()
            {
                v.ringL = flush (v.ringL * ringC);
                v.ringR = flush (v.ringR * ringC);
            };
            if (! v.on)
            {
                if (std::fabs (v.ringL) < 1.0e-6f && std::fabs (v.ringR) < 1.0e-6f)
                    return std::pair<float, float> { 0.f, 0.f };
                decayRing();
                v.lastL = v.ringL;
                v.lastR = v.ringR;
                return { v.ringL, v.ringR };
            }
            float outL = 0.f, outR = 0.f;
            const float fc = std::clamp (v.cut * std::pow (2.f, std::max (0.f, v.envOct) * v.fEnv), 40.f, srF * 0.45f);
            if (v.uniN >= 3)
            {
                const int nU = std::clamp (v.uniN, 3, 5);
                float accL = 0.f, accR = 0.f;
                for (int u = 0; u < nU; ++u)
                {
                    const float t = (u / (float) (nU - 1)) * 2.f - 1.f;
                    const float cents = t * v.detuneCents;
                    const float f = v.freq * std::pow (2.f, cents / 1200.f);
                    const float dt = std::min (f / srF, 0.45f);
                    const float s = blepSaw (v.uniPhase[u], dt);
                    advancePhase (v.uniPhase[u], dt);
                    const float pan = std::clamp (v.pan * 0.35f + t * v.uniSpread, -1.f, 1.f);
                    const float angle = (pan + 1.f) * 0.25f * kPi;
                    accL += s * std::cos (angle);
                    accR += s * std::sin (angle);
                }
                const float norm = 0.55f / std::sqrt ((float) nU);
                accL *= norm;
                accR *= norm;
                if (v.noiseGain > 1.0e-6f && v.noiseEnv > 1.0e-6f)
                {
                    float nz = v.noise.next();
                    nz = v.noiseBp ? v.noiseF.bandpass (nz, v.noiseFc, 0.7f, srF)
                                   : v.noiseF.highpass (nz, v.noiseFc, 0.7f, srF);
                    nz *= v.noiseGain * v.noiseEnv;
                    accL += nz;
                    accR += nz;
                    v.noiseEnv *= v.noiseCoef;
                }
                float lpL, bpL, hpL, lpR, bpR, hpR;
                v.lp.process (accL, fc, 0.7f, srF, lpL, bpL, hpL);
                v.lpR.process (accR, fc, 0.7f, srF, lpR, bpR, hpR);
                outL = v.hp.highpass (lpL, v.hpfHz, 0.7f, srF);
                outR = v.hpR.highpass (lpR, v.hpfHz, 0.7f, srF);
            }
            else
            {
                const float dt = std::min (v.freq / srF, 0.45f);
                const float dt2 = std::min (v.freq * std::pow (2.f, v.detuneCents / 1200.f) / srF, 0.45f);
                float x = blepSaw (v.phase[0], dt);
                float y2 = v.waveB == 1 ? blepSquare (v.phase[1], dt2) : blepSaw (v.phase[1], dt2);
                x = (x + y2 * 0.7f) * 0.6f;
                if (v.noiseGain > 1.0e-6f && v.noiseEnv > 1.0e-6f)
                {
                    float nz = v.noise.next();
                    nz = v.noiseBp ? v.noiseF.bandpass (nz, v.noiseFc, 0.7f, srF)
                                   : v.noiseF.highpass (nz, v.noiseFc, 0.7f, srF);
                    x += nz * v.noiseGain * v.noiseEnv;
                    v.noiseEnv *= v.noiseCoef;
                }
                advancePhase (v.phase[0], dt);
                advancePhase (v.phase[1], dt2);
                float lp, bp, hp;
                v.lp.process (x, fc, 0.7f, srF, lp, bp, hp);
                const float out = v.hp.highpass (lp, v.hpfHz, 0.7f, srF);
                outL = out * (1.f - std::max (0.f, v.pan));
                outR = out * (1.f + std::min (0.f, v.pan));
            }
            v.fEnv *= v.fCoef;
            const float g = v.env.amp * v.velGain * v.trim;
            outL = outL * g + v.ringL;
            outR = outR * g + v.ringR;
            decayRing();
            if (! v.env.tick())
                v.kill();
            outL = flush (outL);
            outR = flush (outR);
            v.lastL = outL;
            v.lastR = outR;
            return { outL, outR };
        };

        float playL[NumParts] {}, playR[NumParts] {};
        sampler.retryHeld (bank, play, (float) prev, (float) pos, transport.playing);
        sampler.render (playL, playR, (float) prev, (float) pos, transport.playing, (float) loop16);

        const float bFader = E.level[Bass].next();
        const float bDuck = params.duck ? E.duckB.next() : 1.f;
        // Sample-true: a stereo bass file sums to mono (it was its left channel only).
        const float bDry = renderBass (E.bass) + renderBass (E.live)
                           + (params.sampleTrue ? 0.5f * (playL[Bass] + playR[Bass]) : playL[Bass]);
        float bSamp = bDry * bFader * bDuck;

        float sL = 0, sR = 0, sDelay = 0, sRev = 0;
        for (auto& v : E.stab)
        {
            auto [l, r] = renderPoly (v);
            sL += l;
            sR += r;
            sDelay += (l + r) * 0.5f * v.delaySend;
            sRev += (l + r) * 0.5f * v.revSend;
        }
        const float stabDuck = params.duck ? E.duckS.next() : 1.f;
        const float stabFader = E.level[Stab].next();
        const float sDryL = sL + playL[Stab];
        const float sDryR = sR + playR[Stab];
        sL = sDryL * stabFader * stabDuck;
        sR = sDryR * stabFader * stabDuck;
        sDelay *= stabFader;
        sRev *= stabFader;

        float pL = 0, pR = 0;
        for (auto& v : E.pluck)
        {
            auto [l, r] = renderPoly (v);
            pL += l;
            pR += r;
        }
        const float pluckFader = E.level[Pluck].next();
        const float pluckDuck = params.duck ? E.duckPl.next() : 1.f;
        const float pDryL = pL + playL[Pluck];
        const float pDryR = pR + playR[Pluck];
        pL = pDryL * pluckFader * pluckDuck;
        pR = pDryR * pluckFader * pluckDuck;

        float aL = 0, aR = 0, aRev = 0;
        for (auto& v : E.pad)
        {
            auto [l, r] = renderPoly (v);
            aL += l;
            aR += r;
            aRev += (l + r) * 0.5f * v.revSend;
        }
        const float padDuck = params.duck ? E.duckP.next() : 1.f;
        const float padFader = E.level[Pad].next();
        const float aDryL = aL + playL[Pad];
        const float aDryR = aR + playR[Pad];
        aL = aDryL * padFader * padDuck;
        aR = aDryR * padFader * padDuck;
        aRev *= padFader;

        float hL = 0, hR = 0;
        for (auto& v : E.hits)
        {
            if (! v.on)
            {
                if (std::fabs (v.ring) < 1.0e-6f)
                    continue;
                v.ring = flush (v.ring * ringC);
                v.last = v.ring;
                hL += v.ring * (1.f - v.pan);
                hR += v.ring * (1.f + v.pan * 0.5f);
                continue;
            }
            float x = 0.f;
            if (v.engine != 0)
            {
                const float semi = v.pitchSt;
                v.pitchSt *= v.pitchStC;
                const int notes = (v.engine == 3 && v.uniN > 1) ? 1 : v.n;
                const int copies = v.engine == 3 ? std::clamp (v.uniN, 1, 5) : 1;
                float pw = v.pw;
                if (v.engine == 3 && v.pwmHz > 0.f)
                {
                    pw = std::clamp (v.pw + v.pwmDepth * std::sin (2.f * kPi * v.pwmPhase), 0.05f, 0.95f);
                    advancePhase (v.pwmPhase, v.pwmHz / srF);
                }
                for (int c = 0; c < notes; ++c)
                {
                    for (int u = 0; u < copies; ++u)
                    {
                        const float spread = copies <= 1 ? 0.f : ((u / (float) (copies - 1)) - 0.5f) * 2.f;
                        const float f = v.freq[c] * std::pow (2.f, (semi + spread * v.uniCents) / 12.f);
                        const float dt = std::min (f / srF, 0.45f);
                        const int ph = std::min (7, c * copies + u);
                        if (v.engine == 1)
                        {
                            x += sinePhase (v.phase[ph]);
                            x += dbToGain (-3.f) * sinePhase (std::fmod (v.phase[ph] * 2.f, 1.f));
                            x += dbToGain (-9.f) * sinePhase (std::fmod (v.phase[ph] * 3.f, 1.f));
                            advancePhase (v.phase[ph], dt);
                        }
                        else if (v.engine == 2)
                        {
                            const float mod = sinePhase (v.modPhase) * v.fmIdx;
                            x += std::sin (2.f * kPi * v.phase[ph] + mod);
                            advancePhase (v.phase[ph], dt);
                        }
                        else
                        {
                            x += blepPulse (v.phase[ph], dt, pw);
                            if (u == 0)
                                x += dbToGain (-3.f) * blepSaw (std::fmod (v.phase[ph] + 0.37f, 1.f), dt * 0.5f);
                            advancePhase (v.phase[ph], dt);
                        }
                    }
                }
                if (v.noiseGain > 1.0e-6f)
                {
                    float nz = v.noise.next();
                    nz = v.noiseUseBp ? v.noiseF.bandpass (nz, v.noiseFc, 1.f, srF)
                                      : v.noiseF.highpass (nz, v.noiseFc, 0.7f, srF);
                    x += nz * v.noiseGain * v.noiseEnv;
                }
                if (v.engine == 2)
                {
                    const float dt0 = std::min (v.freq[0] / srF, 0.45f);
                    advancePhase (v.modPhase, dt0 * std::clamp (v.fmRatio, 0.5f, 16.f));
                }
                v.fmIdx = v.fmEnd + (v.fmIdx - v.fmEnd) * v.fmCoef;
                x *= copies > 1 ? 1.f / std::sqrt ((float) copies) : 1.f;
            }
            else if (v.type == 0)
            {
                for (int c = 0; c < v.n; ++c)
                {
                    const float dt = std::min (v.freq[c] / srF, 0.45f);
                    x += sinePhase (v.phase[c]);
                    x += 0.7f * sinePhase (std::fmod (v.phase[c] * 2.f, 1.f)) * v.modEnv;
                    x += 0.35f * sinePhase (std::fmod (v.phase[c] * 3.f, 1.f));
                    advancePhase (v.phase[c], dt);
                }
            }
            else if (v.type == 1)
            {
                for (int c = 0; c < v.n; ++c)
                {
                    const float dt = std::min (v.freq[c] / srF, 0.45f);
                    const float mod = sinePhase (v.modPhase) * v.modEnv * 3.5f;
                    x += std::sin (2.f * kPi * v.phase[c] + mod);
                    advancePhase (v.phase[c], dt);
                    advancePhase (v.modPhase, dt);
                }
                x += v.noise.next() * v.noiseEnv * 0.15f;
            }
            else if (v.type == 2)
            {
                const float semi = 12.f * v.pitchEnv;
                for (int c = 0; c < v.n; ++c)
                {
                    const float f = v.freq[c] * std::pow (2.f, semi / 12.f);
                    const float dt = std::min (f / srF, 0.45f);
                    x += blepSaw (v.phase[c], dt);
                    advancePhase (v.phase[c], dt * (1.f + 0.01f * c));
                }
            }
            else if (v.type == 3)
            {
                for (int c = 0; c < v.n; ++c)
                {
                    const float dt = std::min (v.freq[c] / srF, 0.45f);
                    x += blepSaw (v.phase[c], dt) * 0.6f;
                    advancePhase (v.phase[c], dt);
                }
                x += v.noise.next() * v.noiseEnv;
            }
            else
            {
                for (int c = 0; c < v.n; ++c)
                {
                    const float dt = std::min (v.freq[c] / srF, 0.45f);
                    x += blepSaw (v.phase[c], dt);
                    x += 0.6f * blepSaw (std::fmod (v.phase[c] + 0.5f, 1.f), dt);
                    advancePhase (v.phase[c], dt * (1.f + 0.008f * c));
                }
            }
            v.modEnv *= v.modCoef;
            v.pitchEnv *= v.pitchCoef;
            v.noiseEnv *= v.noiseCoef;
            v.fEnv *= v.fCoef;
            float lp, bp, hpS;
            v.lp.process (x * 0.25f, std::clamp (v.cut * (0.5f + v.fEnv), 80.f, srF * 0.45f), 0.8f, srF, lp, bp, hpS);
            float y = lp;
            if ((v.engine == 0 && (v.type == 2 || v.type == 4)) || v.engine == 3)
            {
                y = v.hp.highpass (y, 220.f, 0.707f, srF);
                y = v.hp2.highpass (y, 220.f, 0.707f, srF);
            }
            y = y * v.env.amp * v.velGain * v.trim + v.ring;
            v.ring = flush (v.ring * ringC);
            if (! v.env.tick())
                v.kill();
            y = flush (y);
            v.last = y;
            hL += y * (1.f - v.pan);
            hR += y * (1.f + v.pan * 0.5f);
        }
        const float hitsFader = E.level[Hits].next();
        const float hitsDuck = params.duck ? E.duckH.next() : 1.f;
        const float hDryL = hL + playL[Hits];
        const float hDryR = hR + playR[Hits];
        hL = hDryL * hitsFader * hitsDuck;
        hR = hDryR * hitsFader * hitsDuck;

        float voxL = 0, voxR = 0, voxRev = 0;
        static const float form[4][3] = {
            { 520.f, 900.f, 2500.f },
            { 780.f, 1180.f, 2550.f },
            { 480.f, 1800.f, 2650.f },
            { 450.f, 1100.f, 2350.f },
        };
        for (auto& v : E.vox)
        {
            if (! v.on)
            {
                if (std::fabs (v.ring) < 1.0e-6f)
                    continue;
                v.ring = flush (v.ring * ringC);
                v.last = v.ring;
                voxL += v.ring;
                voxR += v.ring * 0.92f;
                continue;
            }
            const float bright = bank.vox[clampPreset (bank, Vox, params.preset[Vox])].brightness * std::pow (2.f, (params.tone[Vox] - 0.5f) * 0.6f);
            v.vib += 5.f / srF;
            if (v.vib >= 1.f)
                v.vib -= 1.f;
            if (--v.jitHold <= 0)
            {
                v.jit = v.noise.next() * 7.f;
                v.jitHold = std::max (1, (int) (0.018f * srF));
            }
            const float cents = 15.f * std::sin (2.f * kPi * v.vib) + v.jit;
            const float playF = v.freq * std::pow (2.f, cents / 1200.f);
            const float dt = std::min (playF / srF, 0.45f);
            float src = blepSaw (v.phase, dt);
            advancePhase (v.phase, dt);
            {
                const float nz = v.hp.highpass (v.noise.next(), 1800.f, 0.7f, srF);
                src += nz * (0.12f + 0.55f * v.noiseEnv);
                v.noiseEnv *= expDecay (70.f, srF);
            }
            const float* fo = form[v.vowel];
            const float o1 = v.f1.bandpass (src, fo[0] * bright, 6.f, srF);
            const float o2 = v.f2.bandpass (src, fo[1] * bright, 8.f, srF);
            const float o3 = v.f3.bandpass (src, fo[2] * bright, 6.f, srF);
            float y = (o1 * 1.0f + o2 * 0.90f + o3 * 0.55f) * v.env.amp * v.velGain * v.trim;
            y += v.ring;
            v.ring = flush (v.ring * ringC);
            if (! v.env.tick())
                v.kill();
            y = flush (y);
            v.last = y;
            voxL += y;
            voxR += y * 0.92f;
            voxRev += y * v.revSend;
        }
        const float voxFader = E.level[Vox].next();
        const float voxDuck = params.duck ? E.duckV.next() : 1.f;
        const float vDryL = voxL + playL[Vox];
        const float vDryR = voxR + playR[Vox];
        voxL = vDryL * voxFader * voxDuck;
        voxR = vDryR * voxFader * voxDuck;
        voxRev *= voxFader;

        const bool mixSum = params.mix != nullptr && params.mix->loaded;
        float voiceL[NumDrumVoices] {};
        float voiceR[NumDrumVoices] {};
        float slotL = 0.f, slotR = 0.f;
        float synthL = 0.f, synthR = 0.f;
        if (mixSum)
            E.drums.process (slotL, slotR, voiceL, voiceR);
        else
            E.drums.process (synthL, synthR);

        float vSamp[NumDrumVoices] {};
        float samp = 0.f;
        for (int v = 0; v < NumDrumVoices; ++v)
        {
            auto& pl = E.play[v];
            if (! pl.on)
            {
                if (std::fabs (pl.ring) > 1.0e-6f)
                {
                    vSamp[v] += pl.ring;
                    samp += pl.ring;
                    pl.ring = flush (pl.ring * ringC);
                    pl.last = pl.ring;
                }
                continue;
            }
            auto& slot = samples[v];
            if (pl.buf < 0 || pl.buf >= SampleSlot::kBufs || pl.pos < 0 || pl.pos >= pl.len
                || pl.pos >= (int) slot.buf[pl.buf].size())
            {
                pl.ring = pl.last;
                pl.on = false;
                continue;
            }
            const int fadeN = std::max (8, (int) (0.012f * srF));
            float g = 1.f;
            const int left = pl.len - pl.pos;
            if (left < fadeN)
            {
                const float t = left / (float) fadeN;
                g = 0.5f * (1.f - std::cos (kPi * t));
            }
            if (pl.atk < pl.atkN)
            {
                ++pl.atk;
                const float t = pl.atk / (float) (pl.atkN + 1);
                g *= 0.5f * (1.f - std::cos (kPi * t));
            }
            float s = slot.buf[pl.buf][(size_t) pl.pos++] * pl.gain * g + pl.ring;
            pl.ring = flush (pl.ring * ringC);
            pl.last = s;
            vSamp[v] += s;
            samp += s;
            if (pl.pos >= pl.len)
                pl.on = false;
        }
        samp = flush (samp);

        const float dLvl = E.level[Drums].next();
        float drumLft = 0.f, drumRgt = 0.f;
        if (mixSum)
        {
            // Duck, then the fixed strip, then the lane fader. A mute is exact silence
            // after the strip so the shared delay and plate get nothing from that lane.
            // The strip still sees the dry voice, so FULL / MUTE / SOLO share one state.
            auto shaped = [&] (int lane, float dryL, float dryR, float duck, float fader, bool muted)
            {
                float l = dryL * duck;
                float r = dryR * duck;
                if (params.sampleTrue)
                {
                    // The sample as recorded at its lane fader. No strip.
                    const float g = dbToGain (laneFaderDb (params.dialect, lane));
                    l *= g;
                    r *= g;
                }
                else
                    E.mixRt.process (lane, l, r);
                if (muted)
                    l = r = 0.f;
                else
                {
                    l *= fader;
                    r *= fader;
                }
                return std::pair<float, float> (l, r);
            };
            const auto b = shaped (0, bDry, bDry, bDuck, bFader, params.mute[Bass]);
            bSamp = 0.5f * (b.first + b.second);
            const auto st = shaped (1, sDryL, sDryR, stabDuck, stabFader, params.mute[Stab]);
            sL = st.first;
            sR = st.second;
            const auto hi = shaped (2, hDryL, hDryR, hitsDuck, hitsFader, params.mute[Hits]);
            hL = hi.first;
            hR = hi.second;
            const auto plk = shaped (3, pDryL, pDryR, pluckDuck, pluckFader, params.mute[Pluck]);
            pL = plk.first;
            pR = plk.second;
            const auto pd = shaped (4, aDryL, aDryR, padDuck, padFader, params.mute[Pad]);
            aL = pd.first;
            aR = pd.second;
            const auto vx = shaped (5, vDryL, vDryR, voxDuck, voxFader, params.mute[Vox]);
            voxL = vx.first;
            voxR = vx.second;
            sDelay = 0.5f * (sL + sR) * sp.delaySend;
            sRev = 0.5f * (sL + sR) * sp.reverbSend;
            aRev = 0.5f * (aL + aR) * padP.reverbSend;
            voxRev = 0.5f * (voxL + voxR) * vp.reverbSend;
            if (params.sampleTrue)
                sDelay = sRev = aRev = voxRev = 0.f; // dry: no delay or plate added to the samples

            // Kick Snare Clap Rim — toms stay linear — HatC HatO Ride Perc.
            // Toms are not a solo lane. A shared nonlinear perc strip would
            // stop MUTE_PERC − (FULL − SOLO_PERC) from nulling.
            static constexpr int kLane[NumDrumVoices] = { 6, 7, 8, 9, -1, -1, 10, 11, 12, 13 };
            for (int v = 0; v < NumDrumVoices; ++v)
            {
                const float dryL = voiceL[v] + vSamp[v] + sampler.drumOutL[v];
                const float dryR = voiceR[v] + vSamp[v] + sampler.drumOutR[v];
                if (kLane[v] < 0)
                {
                    if (! params.mute[Drums] && ! params.drumVoiceMute[v])
                    {
                        const float tom = params.sampleTrue ? dbToGain (laneFaderDb (params.dialect, kFaderPerc)) : 1.f;
                        drumLft += dryL * dLvl * tom;
                        drumRgt += dryR * dLvl * tom;
                    }
                    continue;
                }
                const bool muted = params.mute[Drums] || params.drumVoiceMute[v];
                const auto o = shaped (kLane[v], dryL, dryR, 1.f, dLvl, muted);
                drumLft += o.first;
                drumRgt += o.second;
            }
            if (! params.mute[Drums])
            {
                drumLft += slotL * dLvl;
                drumRgt += slotR * dLvl;
            }
        }
        else
        {
            drumLft = synthL + samp + playL[Drums];
            drumRgt = synthR + samp + playR[Drums];
#if ROLLA_PLUGIN_BUILD
            // Samples only: the bus is a clean gain. (Unused while the mix program loads.)
            drumLft *= dLvl;
            drumRgt *= dLvl;
#else
            const float dDrive = 1.f + dp.drive * 2.f;
            drumLft = fastTanh (drumLft * dDrive) * dLvl;
            drumRgt = fastTanh (drumRgt * dDrive) * dLvl;
#endif
            drumLft = E.drumDc.process (drumLft);
            drumRgt = E.drumDcR.process (drumRgt);
        }
        // Stereo drum samples, voice pans and slot kicks put side energy under 120 Hz.
        // Mono it here, so the main sum and the DRUMS stem get the same bus.
        // Linear, so MUTE - (FULL - SOLO) still nulls.
        if (! params.sampleTrue)
            E.drumMono.process (drumLft, drumRgt, kSubMonoHz, srF);

        float fxGate = 1.f;
        if (transport.playing)
        {
            float barPos = (float) std::fmod (pos, 16.0);
            if (barPos < 0.f)
                barPos += 16.f;
            constexpr float fadeStart = 15.f;
            constexpr float closedAt = 15.913f;
            if (barPos >= closedAt)
                fxGate = 0.f;
            else if (barPos > fadeStart)
            {
                const float t = (barPos - fadeStart) / (closedAt - fadeStart);
                const float u = 1.f - t;
                fxGate = u * u;
            }
            if (fxGate <= 0.f)
            {
                if (! E.fxHeld)
                {
                    std::fill (E.dL.begin(), E.dL.end(), 0.f);
                    std::fill (E.dR.begin(), E.dR.end(), 0.f);
                    E.dHp.reset();
                    E.dLp.reset();
                    E.reverb.reset();
                    E.reverbIdle = 1000000;
                    E.fxHeld = true;
                }
            }
            else
                E.fxHeld = false;
        }
        else
        {
            // A stop must not reopen the bar-end gate. Delay and reverb that
            // were cleared on the last sixteenth stay clear, so the free tail
            // after a loop is silence rather than bar 1 played again.
            fxGate = 0.f;
        }
        sDelay *= fxGate;
        sRev *= fxGate;
        aRev *= fxGate;
        voxRev *= fxGate;

        float delL = 0, delR = 0;
        if (E.dN > 4)
        {
            const float beats = std::max (0.05f, sp.delayBeats);
            const float delaySamps = std::clamp ((float) (beats * (60.0 / bpm) * sr), 2.f, (float) E.dN - 3.f);
            E.dTime += (delaySamps - E.dTime) * 0.002f;
            auto readAt = [&] (const std::vector<float>& b, float t)
            {
                float rp = (float) E.dW - t;
                while (rp < 0.f)
                    rp += (float) E.dN;
                const int i0 = (int) rp % E.dN;
                const int i1 = (i0 + 1) % E.dN;
                const float f = rp - std::floor (rp);
                return b[(size_t) i0] + (b[(size_t) i1] - b[(size_t) i0]) * f;
            };
            const float dl = readAt (E.dL, E.dTime);
            const float dr = readAt (E.dR, E.dTime);
            float fb = E.dLp.lowpass (E.dHp.highpass (dr, 300.f, 0.7f, srF), 2500.f, 0.7f, srF);
            E.dL[(size_t) E.dW] = flush ((sDelay + fb * sp.delayFeedback) * fxGate);
            E.dR[(size_t) E.dW] = flush (dl * fxGate);
            const float mid = 0.5f * (dl + dr);
            delL = (dl * 0.62f + mid * 0.38f) * fxGate;
            delR = (dr * 0.62f + mid * 0.38f) * fxGate;
            if (++E.dW >= E.dN)
                E.dW = 0;
        }

        float revL = 0.f, revR = 0.f;
        const float room = params.sampleTrue ? 0.f : dp.room;
        const float send = (sRev + aRev + voxRev + (drumLft + drumRgt) * 0.5f * room) * fxGate;
        // The idle skip is a level gate. On the mix path it must stay closed, or a
        // sparse solo drops the plate while the full mix keeps it running and the
        // mute null fails.
        if (mixSum || std::fabs (send) > 1.0e-6f || E.reverbIdle < (int) srF)
        {
            revL = send;
            revR = send;
            E.reverb.processStereo (&revL, &revR, 1);
            revL = flush (revL * fxGate);
            revR = flush (revR * fxGate);
            // The plate widens the drum room send after the DRUMS bus cut; mono its low end.
            if (mixSum)
                E.plateMono.process (revL, revR, kSubMonoHz, srF);
            if (! mixSum && std::fabs (revL) < 1.0e-7f && std::fabs (revR) < 1.0e-7f && std::fabs (send) < 1.0e-6f)
                ++E.reverbIdle;
            else
                E.reverbIdle = 0;
        }

        const float og = E.outGain.next();
        float outL = bSamp + sL + pL + aL + hL + voxL + delL * dbToGain (-2.f) + revL * dbToGain (-8.f);
        float outR = bSamp + sR + pR + aR + hR + voxR + delR * dbToGain (-2.f) + revR * dbToGain (-8.f);
        if (params.drumsToMain || drumL == nullptr)
        {
            outL += drumLft;
            outR += drumRgt;
        }
        if (params.mix != nullptr && params.mix->loaded)
        {
            // Linear sum. The shared fastTanh was the mute leak: MUTE − (FULL − SOLO)
            // left a common 250 Hz–4 kHz residual from the cubic term of the sum.
            const float sg = params.sampleTrue ? dbToGain (kSampleTrueMasterDb[std::clamp (params.dialect, 0, 3)])
                                               : E.mixRt.sumGain();
            // Mono below 120 Hz on the master: the plate, the delay and the unstripped
            // toms / slot kicks put side energy under the lanes' own mono-below.
            // LR4 on the side, mid untouched. Linear, so MUTE − (FULL − SOLO) still nulls.
            if (! params.sampleTrue)
                E.masterMono.process (outL, outR, kSubMonoHz, srF);
            outL = flush (E.mainDc.process (outL) * sg * og);
            outR = flush (E.mainDcR.process (outR) * sg * og);
        }
        else
        {
#if ROLLA_PLUGIN_BUILD
            // Samples only: a clean sum. The true-peak guard below is the safety.
            outL = flush (E.mainDc.process (outL) * og);
            outR = flush (E.mainDcR.process (outR) * og);
#else
            outL = flush (fastTanh (E.mainDc.process (outL) * 1.05f) * og);
            outR = flush (fastTanh (E.mainDcR.process (outR) * 1.05f) * og);
#endif
        }
        // The free tail after the loop is hard silence. Reach that silence
        // on a raised cosine, or the last playing sample steps into the zero.
        if (transport.playing && ! params.sampleTrue)
        {
            const double untilEnd = loop16 - pos;
            const double window = d16 * 0.010 * sr;
            if (untilEnd > 0.0 && untilEnd < window)
            {
                const float t = (float) (untilEnd / window);
                const float g = 0.5f * (1.f - std::cos (kPi * t));
                outL *= g;
                outR *= g;
                drumLft *= g;
                drumRgt *= g;
            }
        }
        if (E.wrapIn > 0)
        {
            const float t = (float) (E.wrapN - E.wrapIn + 1) / (float) (E.wrapN + 1);
            const float g = 0.5f * (1.f - std::cos (kPi * t));
            outL *= g;
            outR *= g;
            drumLft *= g;
            drumRgt *= g;
            --E.wrapIn;
        }
        mainL[i] = outL;
        if (mainR != nullptr)
            mainR[i] = outR;
        if (drumL != nullptr)
            drumL[i] = (params.mix != nullptr && params.mix->loaded) || kCleanBus ? flush (drumLft * og) : flush (fastTanh (drumLft) * og);
        if (drumR != nullptr)
            drumR[i] = (params.mix != nullptr && params.mix->loaded) || kCleanBus ? flush (drumRgt * og) : flush (fastTanh (drumRgt) * og);
    }

    // Master true-peak safety (-6 dBTP). The DRUMS bus rides the same delay.
    E.tpGuard.setCeilingDbtp (params.sampleTrue ? kSampleTrueCeilingDbtp : TruePeakGuard::kDefaultCeilingDbtp);
    E.tpGuard.setEnabled (params.tpGuard);
    E.tpGuard.process (mainL, mainR, drumL, drumR, numSamples);
}

int Engine::latencySamples() const noexcept
{
    return impl != nullptr ? impl->tpGuard.latencySamples() : 0;
}

float Engine::takeTpGuardGrDb() noexcept
{
    return impl != nullptr ? impl->tpGuard.takeMaxGrDb() : 0.f;
}

} // namespace rolla
