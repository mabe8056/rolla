#include "Sampler.h"
#include "Engine.h"

#include <algorithm>
#include <cmath>

namespace rolla
{
namespace
{

constexpr float kSincPi = 3.14159265358979323846f;

float midiHz (int note) noexcept
{
    return 440.f * std::pow (2.f, (note - 69) / 12.f);
}

float ampDb (float db) noexcept
{
    return std::pow (10.f, db / 20.f);
}

float coefFromMs (float ms, float sr) noexcept
{
    return 1.f - std::exp (-1.f / std::max (1.f, ms * 0.001f * sr));
}

float expDecay (float ms, float sr) noexcept
{
    return std::exp (-1.f / std::max (1.f, ms * 0.001f * sr));
}

int presetOf (const EngineParams& params, int part) noexcept
{
    if (part < 0 || part >= NumParts)
        return 0;
    const int i = params.preset[part];
    if (i < 0)
        return 0;
    if (i >= PresetBank::kPer)
        return PresetBank::kPer - 1;
    return i;
}

} // namespace

const SampleSpec& sampleSpecFor (const PresetBank& bank, int part, int preset) noexcept
{
    const int i = std::clamp (preset, 0, PresetBank::kPer - 1);
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

void Sampler::Voice::Env::start (float aMs, float dMs, float sGain, float rMs, float sr) noexcept
{
    stage = 1;
    amp = 0.f;
    sustain = std::clamp (sGain, 0.f, 1.f);
    attInc = 1.f / std::max (1.f, aMs * 0.001f * sr);
    decC = coefFromMs (std::max (5.f, dMs) / 3.f, sr);
    relC = coefFromMs (std::max (5.f, rMs) / 3.f, sr);
}

void Sampler::Voice::Env::release() noexcept
{
    if (stage != 0)
        stage = 4;
}

bool Sampler::Voice::Env::tick() noexcept
{
    if (stage == 1)
    {
        amp += attInc;
        if (amp >= 1.f)
        {
            amp = 1.f;
            stage = 2;
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
    return true;
}

void Sampler::prepare (double sampleRate) noexcept
{
    sr = sampleRate > 1000.0 ? sampleRate : 48000.0;
    reset();
}

void Sampler::reset() noexcept
{
    for (auto& v : voices)
        v = {};
    for (int& c : sliceCursor)
        c = 0;
}

void Sampler::releaseNote (int part, int midiNote) noexcept
{
    for (auto& v : voices)
        if (v.on && v.part == part && v.midiNote == midiNote)
            v.env.release();
}

void Sampler::releaseAll() noexcept
{
    for (auto& v : voices)
        if (v.on)
            v.env.release();
}

Sampler::Voice* Sampler::allocate (int) noexcept
{
    Voice* free = nullptr;
    Voice* quiet = nullptr;
    for (auto& v : voices)
    {
        if (! v.on)
        {
            free = &v;
            break;
        }
        if (quiet == nullptr || v.env.amp < quiet->env.amp)
            quiet = &v;
    }
    Voice* slot = free != nullptr ? free : quiet;
    if (slot == nullptr)
        return nullptr;
    if (slot->on)
    {
        slot->stopping = true;
        slot->fadeInc = -1.f / 64.f;
    }
    return slot;
}

float Sampler::readSinc (const std::vector<float>& src, double index) const noexcept
{
    const int n = (int) src.size();
    if (n <= 0)
        return 0.f;
    const int i = (int) std::floor (index);
    const float frac = (float) (index - i);
    constexpr int k = 8;
    float acc = 0.f;
    for (int t = -k + 1; t <= k; ++t)
    {
        const int idx = i + t;
        const float x = (idx >= 0 && idx < n) ? src[(size_t) idx] : 0.f;
        const float p = frac - (float) t;
        float w = 1.f;
        if (std::fabs (p) > 1.0e-5f)
        {
            const float sinc = std::sin (kSincPi * p) / (kSincPi * p);
            const float window = 0.5f + 0.5f * std::cos (kSincPi * p / (float) k);
            w = sinc * window;
        }
        acc += x * w;
    }
    return acc;
}

void Sampler::readVoice (Voice& v, float& l, float& r) noexcept
{
    l = r = 0.f;
    if (! v.on || v.audio == nullptr || v.audio->frames <= 0)
    {
        v.on = false;
        return;
    }

    const double n = (double) v.audio->frames;
    double index = v.reverse ? (n - 1.0 - v.pos) : v.pos;
    if (v.loop && v.loopB > v.loopA + 8.0)
    {
        const double span = v.loopB - v.loopA;
        if (v.pos >= v.loopB)
            v.pos = v.loopA + std::fmod (v.pos - v.loopA, span);
        index = v.reverse ? (n - 1.0 - v.pos) : v.pos;
    }

    float ls = readSinc (v.audio->left, index);
    float rs = readSinc (v.audio->right, index);

    if (v.loop && v.xfade > 4.0 && v.pos > v.loopB - v.xfade && v.pos < v.loopB)
    {
        const float t = (float) ((v.pos - (v.loopB - v.xfade)) / v.xfade);
        const double head = v.loopA + (v.pos - (v.loopB - v.xfade));
        const double hi = v.reverse ? (n - 1.0 - head) : head;
        const float w = std::sin (t * 0.5f * kSincPi);
        const float u = std::cos (t * 0.5f * kSincPi);
        ls = ls * u + readSinc (v.audio->left, hi) * w;
        rs = rs * u + readSinc (v.audio->right, hi) * w;
    }

    v.pos += v.ratio;
    if (! v.loop && v.pos >= n - 1.0)
        v.env.release();

    const float srF = (float) sr;
    if (v.fEnv > 0.f)
        v.fEnv *= v.fCoef;
    const float fc = std::clamp (v.cut * std::pow (2.f, v.envOct * v.fEnv), 40.f, srF * 0.45f);
    ls = v.ladder.process (ls, fc, v.res, srF);
    if (v.hpfHz > 25.f)
    {
        const float c = std::exp (-2.f * kSincPi * v.hpfHz / srF);
        v.hpState = c * v.hpState + (1.f - c) * ls;
        ls -= v.hpState;
    }
    rs = ls + (rs - ls) * 0.85f;

    const float env = v.env.amp;
    v.fade = std::clamp (v.fade + v.fadeInc, 0.f, 1.f);
    const float g = env * v.gain * v.fade;
    l = ls * g * (1.f - v.pan * 0.5f);
    r = rs * g * (1.f + v.pan * 0.5f);
    if (! v.env.tick() || (v.stopping && v.fade <= 0.f))
        v.on = false;
}

namespace
{

struct Amp
{
    float a = 3.f, d = 200.f, s = 0.2f, r = 40.f;
    float cut = 2000.f, res = 0.05f, envOct = 0.5f, tau = 40.f, hpf = 30.f, trim = 0.2f;
};

Amp ampFor (const PresetBank& bank, int part, int preset)
{
    const int i = std::clamp (preset, 0, PresetBank::kPer - 1);
    Amp a;
    if (part == Bass)
    {
        const auto& p = bank.bass[i];
        a = { p.aMs, p.dMs, ampDb (p.sDb), p.rMs, p.cutoffHz, p.resonance, p.filterEnvOct, p.filterTauMs, p.hpfHz, ampDb (p.trimDb) * 0.4f };
    }
    else if (part == Stab)
    {
        const auto& p = bank.stab[i];
        a = { p.aMs, p.dMs, ampDb (p.sDb), p.rMs, p.cutoffHz, 0.08f, p.envOct, p.tauMs, p.hpfHz, ampDb (p.trimDb) * 0.25f };
    }
    else if (part == Hits)
    {
        const auto& p = bank.hits[i];
        a = { 1.f, p.decayMs, 0.f, 40.f, p.cutoffHz * p.brightness, 0.1f, 0.4f, 50.f, 180.f, ampDb (p.trimDb) * 0.25f };
    }
    else if (part == Pluck)
    {
        const auto& p = bank.pluck[i];
        a = { 2.f, p.decayMs, 0.f, 40.f, p.cutoffHz, 0.08f, 0.4f, 30.f, 180.f, ampDb (p.trimDb) * 0.22f };
    }
    else if (part == Pad)
    {
        const auto& p = bank.pad[i];
        a = { p.attackMs, 400.f, 0.85f, p.releaseMs, p.cutoffHz, 0.06f, 0.3f, 80.f, 200.f, ampDb (p.trimDb) * 0.28f };
    }
    else if (part == Vox)
    {
        const auto& p = bank.vox[i];
        a = { 3.f, p.decayMs, 0.f, 30.f, 4000.f * p.brightness, 0.05f, 0.2f, 40.f, 200.f, ampDb (p.trimDb) * 0.25f };
    }
    else
    {
        const auto& p = bank.drums[i];
        a = { 1.f, 180.f, 0.f, 20.f, 8000.f, 0.02f, 0.f, 20.f, 30.f, ampDb (p.trimDb) * 0.5f };
    }
    return a;
}

} // namespace

Sampler::Take Sampler::take (int part, int midiNote, int velocity, float end16,
                             const PresetBank& bank, const EngineParams& params) noexcept
{
    Take result;
    if (part < 0 || part >= Drums || params.library == nullptr)
        return result;
    const auto& spec = sampleSpecFor (bank, part, presetOf (params, part));
    if (spec.mode == SampleMode::Synth)
        return result;

    const int sliceIndex = sliceCursor[part]++;
    const auto choice = chooseSample (spec, midiNote, params.samplePick[part], sliceIndex);
    const SampleAudio* audio = (choice.file != nullptr && choice.file[0] != 0)
                                  ? params.library->tryGet (choice.file)
                                  : nullptr;
    if (audio == nullptr || audio->frames < 8)
        return result;

    Voice* slot = allocate (part);
    if (slot == nullptr)
        return result;

    const auto amp = ampFor (bank, part, presetOf (params, part));
    const float srF = (float) sr;
    Voice fresh;
    fresh.on = true;
    fresh.part = part;
    fresh.midiNote = midiNote;
    fresh.age = serial++;
    fresh.audio = audio;
    fresh.reverse = spec.reverse;
    fresh.ratio = std::clamp ((midiHz (midiNote) / std::max (20.f, midiHz (choice.root))) * (audio->rate / sr), 0.05, 16.0);
    fresh.end16 = end16;
    fresh.fade = 0.f;
    fresh.fadeInc = 1.f / 64.f;
    fresh.stopping = false;
    fresh.gain = amp.trim * std::clamp (velocity / 127.f, 0.05f, 1.f);
    if (spec.mode == SampleMode::Layer)
        fresh.gain *= std::clamp (spec.layerMix, 0.f, 1.f);
    fresh.pan = (part == Stab || part == Pad) ? 0.15f : 0.f;
    fresh.cut = amp.cut;
    fresh.res = amp.res;
    fresh.envOct = amp.envOct;
    fresh.fEnv = 1.f;
    fresh.fCoef = expDecay (std::max (4.f, amp.tau), srF);
    fresh.hpfHz = amp.hpf;
    fresh.hpState = 0.f;
    fresh.ladder.reset();
    fresh.env.start (amp.a, amp.d, amp.s, amp.r, srF);

    double startFrame = std::clamp ((double) spec.start, 0.0, 16.0) * audio->rate;
    if (choice.slice && audio->sliceCount > 1)
    {
        const int s = (choice.sliceIndex % audio->sliceCount + audio->sliceCount) % audio->sliceCount;
        startFrame = (double) audio->slices[s];
    }
    fresh.pos = startFrame;
    fresh.loop = spec.loop.on && part != Hits && part != Vox;
    fresh.loopA = spec.loop.start > 0.f ? spec.loop.start * audio->rate : startFrame;
    fresh.loopB = spec.loop.end > spec.loop.start ? spec.loop.end * audio->rate : (double) audio->frames - 1.0;
    fresh.xfade = std::max (0.f, spec.loop.xfade) * (float) audio->rate;
    if (fresh.loopB > audio->frames - 1)
        fresh.loopB = (double) audio->frames - 1.0;

    *slot = fresh;
    if (spec.mode == SampleMode::Sample)
    {
        result.kind = 1;
        result.synthGain = 0.f;
    }
    else
    {
        result.kind = 2;
        result.synthGain = 1.f - std::clamp (spec.layerMix, 0.f, 1.f);
    }
    return result;
}

float Sampler::takeDrum (int voice, int velocity, const PresetBank& bank, const EngineParams& params) noexcept
{
    if (voice < 0 || voice >= NumDrumVoices || params.library == nullptr || velocity <= 0)
        return 1.f;
    const auto& spec = sampleSpecFor (bank, Drums, presetOf (params, Drums));
    if (spec.mode == SampleMode::Synth)
        return 1.f;

    const char* file = spec.voiceFile[voice];
    int root = spec.root;
    if (file[0] == 0 && spec.poolCount > 0)
    {
        const int i = (params.samplePick[Drums] % spec.poolCount + spec.poolCount) % spec.poolCount;
        file = spec.pool[i];
        if (spec.poolRoot[i] > 0)
            root = spec.poolRoot[i];
    }
    if (file[0] == 0)
        file = spec.file;
    const SampleAudio* audio = file[0] != 0 ? params.library->tryGet (file) : nullptr;
    if (audio == nullptr || audio->frames < 8)
        return 1.f;

    Voice* slot = allocate (Drums);
    if (slot == nullptr)
        return 1.f;

    const auto amp = ampFor (bank, Drums, presetOf (params, Drums));
    const float tune = std::pow (2.f, params.drumTuneSt[voice] / 12.f);
    Voice fresh;
    fresh.on = true;
    fresh.part = Drums;
    fresh.age = serial++;
    fresh.audio = audio;
    fresh.reverse = spec.reverse;
    fresh.ratio = std::clamp (tune * (audio->rate / sr), 0.25, 4.0);
    juce::ignoreUnused (root);
    fresh.end16 = 1.0e9f;
    fresh.fade = 0.f;
    fresh.fadeInc = 1.f / 32.f;
    fresh.gain = amp.trim * std::clamp (velocity / 127.f, 0.05f, 1.f) * ampDb (params.drumLevelDb[voice]);
    if (spec.mode == SampleMode::Layer)
        fresh.gain *= std::clamp (spec.layerMix, 0.f, 1.f);
    fresh.cut = amp.cut;
    fresh.res = amp.res;
    fresh.envOct = 0.f;
    fresh.fEnv = 1.f;
    fresh.fCoef = expDecay (30.f, (float) sr);
    fresh.hpfHz = 30.f;
    fresh.ladder.reset();
    const float decay = 80.f * std::pow (2.f, (params.drumDecay[voice] - 0.5f) * 2.f);
    fresh.env.start (0.5f, decay, 0.f, 12.f, (float) sr);
    fresh.pos = std::clamp ((double) spec.start, 0.0, 4.0) * audio->rate;
    fresh.loop = spec.loop.on;
    fresh.loopA = spec.loop.start * audio->rate;
    fresh.loopB = spec.loop.end > spec.loop.start ? spec.loop.end * audio->rate : (double) audio->frames - 1.0;
    fresh.xfade = spec.loop.xfade * (float) audio->rate;
    *slot = fresh;

    if (spec.mode == SampleMode::Sample)
        return 0.f;
    return 1.f - std::clamp (spec.layerMix, 0.f, 1.f);
}

void Sampler::render (float* laneL, float* laneR, float prev16, float pos16, bool playing) noexcept
{
    if (laneL == nullptr || laneR == nullptr)
        return;
    for (int i = 0; i < NumParts; ++i)
        laneL[i] = laneR[i] = 0.f;

    for (auto& v : voices)
    {
        if (! v.on)
            continue;
        if (playing && v.end16 < 1.0e8f)
        {
            const bool hit = (prev16 < v.end16 && pos16 >= v.end16)
                          || (pos16 < prev16 && v.end16 >= prev16);
            if (hit)
                v.env.release();
        }
        float l = 0.f, r = 0.f;
        readVoice (v, l, r);
        if (v.part >= 0 && v.part < NumParts)
        {
            laneL[v.part] += l;
            laneR[v.part] += r;
        }
    }
}

} // namespace rolla
