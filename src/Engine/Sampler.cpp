#include "Sampler.h"
#include "Engine.h"
#include "../Sampler/PlayDecision.h"
#include "../Sampler/UserSlots.h"

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
    if (params.presets != nullptr)
        return presetSlot (*params.presets, part, params.preset[part]);
    const int i = params.preset[part];
    if (i < 0)
        return 0;
    if (i >= PresetBank::kPer)
        return PresetBank::kPer - 1;
    return i;
}

enum class FileState { Ready, Pending, Absent };

FileState probeFile (const SampleLibrary* library, const char* file, const SampleAudio*& audio) noexcept
{
    audio = nullptr;
    if (library == nullptr || file == nullptr || file[0] == 0)
        return FileState::Absent;
    audio = library->tryGet (file);
    if (audio != nullptr && audio->frames >= 8)
        return FileState::Ready;
    audio = nullptr;
    if (library->missing (file))
        return FileState::Absent;
    return FileState::Pending;
}

struct LaneHit
{
    const SampleSpec* spec = nullptr;
    SampleChoice choice {};
    const SampleAudio* audio = nullptr;
    bool pending = false;
};

bool readyChoice (const SampleLibrary* library, const SampleSpec& spec, int midiNote,
                  int pick, int sliceIndex, LaneHit& hit) noexcept
{
    if (spec.mode == SampleMode::Synth)
        return false;
    const SampleAudio* audio = nullptr;
    const auto choice = chooseSample (spec, midiNote, pick, sliceIndex);
    const auto state = probeFile (library, choice.file, audio);
    if (state == FileState::Ready)
    {
        hit.spec = &spec;
        hit.choice = choice;
        hit.audio = audio;
        return true;
    }
    if (state == FileState::Pending)
        hit.pending = true;

    const char* extras[SampleSpec::kPool + SampleSpec::kZones + 1] {};
    int n = 0;
    if (spec.file[0] != 0)
        extras[n++] = spec.file;
    for (int i = 0; i < spec.poolCount && i < SampleSpec::kPool; ++i)
        if (spec.pool[i][0] != 0)
            extras[n++] = spec.pool[i];
    for (int i = 0; i < spec.zoneCount && i < SampleSpec::kZones && n < SampleSpec::kPool + SampleSpec::kZones + 1; ++i)
        if (spec.zones[i].file[0] != 0)
            extras[n++] = spec.zones[i].file;
    for (int i = 0; i < n; ++i)
    {
        if (choice.file != nullptr && std::strcmp (extras[i], choice.file) == 0)
            continue;
        const auto extra = probeFile (library, extras[i], audio);
        if (extra == FileState::Ready)
        {
            hit.spec = &spec;
            hit.choice = choice;
            hit.choice.file = extras[i];
            hit.audio = audio;
            return true;
        }
        if (extra == FileState::Pending)
            hit.pending = true;
    }
    return false;
}

// The chosen preset wins. A confirmed-missing file tries the rest of the lane.
// A file that is still loading does not get replaced, and nothing here selects the synth.
LaneHit resolveLane (const PresetBank& bank, const EngineParams& params, int part,
                     int midiNote, int sliceIndex) noexcept
{
    LaneHit hit;
    const int current = presetOf (params, part);
    const auto& currentSpec = specAt (bank, part, current);
    if (currentSpec.mode == SampleMode::Synth)
        return hit;
    const int pick = params.samplePick[part];
    if (readyChoice (params.library, currentSpec, midiNote, pick, sliceIndex, hit))
        return hit;
    if (hit.pending)
        return hit;

    const int n = bank.count[part] > 0 ? bank.count[part] : 1;
    for (int step = 1; step < n; ++step)
    {
        const int index = (current + (pick < 0 ? 0 : pick) + step) % n;
        if (index == current)
            continue;
        LaneHit alt;
        if (readyChoice (params.library, specAt (bank, part, index), midiNote, 0, 0, alt))
            return alt;
    }
    hit.spec = &currentSpec;
    hit.audio = nullptr;
    return hit;
}

} // namespace

const SampleSpec& sampleSpecFor (const PresetBank& bank, int part, int preset) noexcept
{
    return specAt (bank, part, preset);
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
    const int i = presetSlot (bank, part, preset);
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

bool Sampler::playUserSlot (int part, int drumVoice, int midiNote, int velocity, float end16,
                             const PresetBank& bank, const EngineParams& params,
                             Take& take, float& drumKeep) noexcept
{
    if (params.userSlots == nullptr || velocity <= 0)
        return false;
    const int slotIndex = drumVoice >= 0 ? userSlotForDrum (drumVoice) : userSlotForPart (part);
    if (slotIndex < 0)
        return false;
    const auto& pod = params.userSlots->slots[slotIndex];
    if (pod.mode == 0)
        return false;

    SlotQuery slot;
    slot.mode = pod.mode;
    slot.ready = pod.status == 2 && pod.audio != nullptr && pod.audio->frames >= 8;
    slot.missing = pod.status == 3 || pod.file[0] == 0;
    slot.loading = pod.status == 1 && ! slot.missing;
    slot.file = pod.file;
    slot.root = pod.root;
    slot.cents = pod.cents;
    slot.start = pod.start;
    slot.reverse = pod.reverse != 0;
    slot.loop = pod.loopOn != 0;
    slot.loopStart = pod.loopStart;
    slot.loopEnd = pod.loopEnd;
    slot.loopXfade = pod.loopXfade;
    slot.layerMix = pod.layerMix;
    slot.gainDb = pod.gainDb;

    const PlayDecision decision = resolvePlay (PresetQuery {}, &slot);
    if (! decision.fromSlot && ! decision.suppressPreset)
        return false;
    if (! decision.fromSlot)
    {
        take = {};
        drumKeep = 1.f;
        return true;
    }

    const int voicePart = drumVoice >= 0 ? Drums : part;
    Voice* voice = allocate (voicePart);
    if (voice == nullptr)
    {
        take.kind = decision.kind;
        take.synthGain = decision.synthGain;
        drumKeep = decision.synthGain;
        return true;
    }

    const auto amp = ampFor (bank, voicePart, presetOf (params, voicePart));
    const float srF = (float) sr;
    const double semis = drumVoice >= 0
                             ? (double) params.drumTuneSt[drumVoice] + (double) decision.cents / 100.0
                             : (double) (midiNote - decision.root) + (double) decision.cents / 100.0;
    const double ratioMin = drumVoice >= 0 ? 0.25 : 0.05;
    const double ratioMax = drumVoice >= 0 ? 4.0 : 16.0;
    Voice fresh;
    fresh.on = true;
    fresh.part = voicePart;
    fresh.drumVoice = drumVoice;
    fresh.midiNote = midiNote;
    fresh.age = serial++;
    fresh.audio = pod.audio;
    fresh.reverse = decision.reverse;
    fresh.ratio = std::clamp (std::pow (2.0, semis / 12.0) * (pod.audio->rate / sr), ratioMin, ratioMax);
    fresh.end16 = end16;
    fresh.fade = 0.f;
    fresh.fadeInc = 1.f / (drumVoice >= 0 ? 32.f : 64.f);
    fresh.stopping = false;
    float trim = amp.trim * std::clamp (velocity / 127.f, 0.05f, 1.f);
    if (drumVoice >= 0)
        trim *= ampDb (params.drumLevelDb[drumVoice]);
    fresh.gain = trim * decision.sampleMix * ampDb (decision.gainDb);
    fresh.pan = (part == Stab || part == Pad) ? 0.15f : 0.f;
    fresh.cut = amp.cut;
    fresh.res = amp.res;
    fresh.envOct = drumVoice >= 0 ? 0.f : amp.envOct;
    fresh.fEnv = 1.f;
    fresh.fCoef = expDecay (std::max (4.f, drumVoice >= 0 ? 30.f : amp.tau), srF);
    fresh.hpfHz = drumVoice >= 0 ? 30.f : amp.hpf;
    fresh.hpState = 0.f;
    fresh.ladder.reset();
    if (drumVoice >= 0)
    {
        const float decay = 80.f * std::pow (2.f, (params.drumDecay[drumVoice] - 0.5f) * 2.f);
        fresh.env.start (0.5f, decay, 0.f, 12.f, srF);
    }
    else
        fresh.env.start (amp.a, amp.d, amp.s, amp.r, srF);

    const double startFrame = std::min (std::clamp ((double) decision.startSec, 0.0, 60.0) * pod.audio->rate,
                                         (double) pod.audio->frames - 1.0);
    fresh.pos = startFrame;
    fresh.loop = decision.loop;
    SampleLoop loop;
    loop.on = decision.loop;
    loop.start = decision.loopStart;
    loop.end = decision.loopEnd;
    loop.xfade = decision.loopXfade;
    const auto region = makeLoopRegion (loop, pod.audio->rate, pod.audio->frames, startFrame);
    fresh.loopA = region.a;
    fresh.loopB = region.b;
    fresh.xfade = region.xfade;
    *voice = fresh;

    take.kind = decision.kind;
    take.synthGain = decision.synthGain;
    drumKeep = decision.synthGain;
    return true;
}

Sampler::Take Sampler::take (int part, int midiNote, int velocity, float end16,
                             const PresetBank& bank, const EngineParams& params) noexcept
{
    Take result;
    if (part < 0 || part >= Drums)
        return result;
    float drumKeep = 1.f;
    if (playUserSlot (part, -1, midiNote, velocity, end16, bank, params, result, drumKeep))
        return result;
    const auto& spec = sampleSpecFor (bank, part, presetOf (params, part));
    if (spec.mode == SampleMode::Synth)
        return result;
    if (params.library == nullptr)
    {
        result.kind = 1;
        result.synthGain = 0.f;
        return result;
    }

    const int sliceIndex = sliceCursor[part]++;
    const auto hit = resolveLane (bank, params, part, midiNote, sliceIndex);
    const SampleSpec& played = hit.spec != nullptr ? *hit.spec : spec;
    SampleChoice choice = hit.choice;
    const SampleAudio* audio = hit.audio;
    bool fromPack = false;
    if ((audio == nullptr || audio->frames < 8) && ! hit.pending)
    {
        const int drumPreset = presetOf (params, Drums);
        const auto& drum = bank.drums[drumPreset];
        if (drum.sample.mode != SampleMode::Synth)
        {
            static constexpr int kPack[] = { Perc, HatO, Ride, HatC, Kick, Snare, Clap, Rim };
            int pick = params.samplePick[part];
            if (pick < 0)
                pick = 0;
            for (int alt : kPack)
            {
                const auto& pool = drum.voicePool[alt];
                if (pool.empty())
                    continue;
                const int n = (int) pool.size();
                for (int i = 0; i < n; ++i)
                {
                    const SampleAudio* got = nullptr;
                    if (probeFile (params.library, pool[(size_t) ((pick + i) % n)].c_str(), got) == FileState::Ready)
                    {
                        audio = got;
                        choice.file = pool[(size_t) ((pick + i) % n)].c_str();
                        choice.root = played.root > 0 ? played.root : 60;
                        fromPack = true;
                        break;
                    }
                }
                if (audio != nullptr)
                    break;
            }
        }
    }
    if (audio == nullptr || audio->frames < 8)
    {
        result.kind = 1;
        result.synthGain = 0.f;
        return result;
    }

    Voice* slot = allocate (part);
    if (slot == nullptr)
    {
        result.kind = 1;
        result.synthGain = 0.f;
        return result;
    }

    const auto amp = ampFor (bank, part, presetOf (params, part));
    const float srF = (float) sr;
    Voice fresh;
    fresh.on = true;
    fresh.part = part;
    fresh.midiNote = midiNote;
    fresh.age = serial++;
    fresh.audio = audio;
    fresh.reverse = played.reverse;
    fresh.ratio = std::clamp ((midiHz (midiNote) / std::max (20.f, midiHz (choice.root))) * (audio->rate / sr), 0.05, 16.0);
    fresh.end16 = end16;
    fresh.fade = 0.f;
    fresh.fadeInc = 1.f / 64.f;
    fresh.stopping = false;
    fresh.gain = amp.trim * std::clamp (velocity / 127.f, 0.05f, 1.f);
    const bool layered = &played == &spec && played.mode == SampleMode::Layer;
    if (layered)
        fresh.gain *= std::clamp (played.layerMix, 0.f, 1.f);
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

    double startFrame = fromPack ? 0.0 : std::clamp ((double) played.start, 0.0, 16.0) * audio->rate;
    if (choice.slice && audio->sliceCount > 1)
    {
        const int s = (choice.sliceIndex % audio->sliceCount + audio->sliceCount) % audio->sliceCount;
        startFrame = (double) audio->slices[s];
    }
    fresh.pos = startFrame;
    fresh.loop = ! fromPack && played.loop.on && part != Hits && part != Vox;
    const auto region = makeLoopRegion (played.loop, audio->rate, audio->frames, startFrame);
    fresh.loopA = region.a;
    fresh.loopB = region.b;
    fresh.xfade = region.xfade;
    if (fresh.loopB > audio->frames - 1)
        fresh.loopB = (double) audio->frames - 1.0;

    *slot = fresh;
    if (layered)
    {
        result.kind = 2;
        result.synthGain = 1.f - std::clamp (played.layerMix, 0.f, 1.f);
    }
    else
    {
        result.kind = 1;
        result.synthGain = 0.f;
    }
    return result;
}

float Sampler::takeDrum (int voice, int velocity, const PresetBank& bank, const EngineParams& params) noexcept
{
    if (voice < 0 || voice >= NumDrumVoices || velocity <= 0)
        return 1.f;
    Take taken;
    float keep = 1.f;
    if (playUserSlot (Drums, voice, 60, velocity, 1.0e9f, bank, params, taken, keep))
        return keep;
    const int preset = presetOf (params, Drums);
    const auto& spec = sampleSpecFor (bank, Drums, preset);
    if (spec.mode == SampleMode::Synth)
        return 1.f;
    if (params.library == nullptr)
        return 0.f;

    // Own folder first. A missing hit then comes from the rest of the pack
    // (perc, open hat, ride, then the other folders). The circuit model is not a source.
    const auto& drum = bank.drums[preset];
    const SampleAudio* audio = nullptr;
    bool pending = false;
    auto scanPool = [&] (const std::vector<std::string>& pool, int start, bool firstOnlyPending) noexcept
    {
        if (pool.empty())
            return;
        const int n = (int) pool.size();
        if (start < 0)
            start = 0;
        start %= n;
        const SampleAudio* got = nullptr;
        const auto state = probeFile (params.library, pool[(size_t) start].c_str(), got);
        if (state == FileState::Ready)
        {
            audio = got;
            return;
        }
        if (state == FileState::Pending)
        {
            pending = true;
            if (firstOnlyPending)
                return;
        }
        for (int i = 1; i < n && audio == nullptr; ++i)
        {
            const auto extra = probeFile (params.library, pool[(size_t) ((start + i) % n)].c_str(), got);
            if (extra == FileState::Ready)
                audio = got;
            else if (extra == FileState::Pending)
                pending = true;
        }
    };

    int shift = 0;
    if (voice == TomMid)
        shift = 1;
    else if (voice == TomLow)
        shift = 2;
    int pick = params.samplePick[Drums];
    if (pick < 0)
        pick = 0;
    const auto& own = drum.voicePool[voice];
    if (! own.empty())
        scanPool (own, pick + shift, true);
    else
    {
        const char* file = spec.voiceFile[voice][0] != 0 ? spec.voiceFile[voice] : spec.file;
        const SampleAudio* got = nullptr;
        const auto state = probeFile (params.library, file, got);
        if (state == FileState::Ready)
            audio = got;
        else if (state == FileState::Pending)
            pending = true;
    }
    if (audio == nullptr && ! pending)
    {
        // Crash, tom, shaker, noise, or a missing folder: PERC, then OH, then RIDE.
        static constexpr int kPack[] = { Perc, HatO, Ride, HatC, Kick, Snare, Clap, Rim, TomLow, TomMid };
        for (int alt : kPack)
        {
            if (alt == voice || audio != nullptr)
                continue;
            scanPool (drum.voicePool[alt], pick, false);
        }
    }
    if (audio == nullptr || audio->frames < 8)
        return 0.f;

    Voice* slot = allocate (Drums);
    if (slot == nullptr)
        return 0.f;

    const auto amp = ampFor (bank, Drums, presetOf (params, Drums));
    const float tune = std::pow (2.f, params.drumTuneSt[voice] / 12.f);
    Voice fresh;
    fresh.on = true;
    fresh.part = Drums;
    fresh.drumVoice = voice;
    fresh.age = serial++;
    fresh.audio = audio;
    fresh.reverse = spec.reverse;
    fresh.ratio = std::clamp (tune * (audio->rate / sr), 0.25, 4.0);
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
    const auto region = makeLoopRegion (spec.loop, audio->rate, audio->frames, fresh.pos);
    fresh.loopA = region.a;
    fresh.loopB = region.b;
    fresh.xfade = region.xfade;
    *slot = fresh;

    return 0.f;
}

void Sampler::render (float* laneL, float* laneR, float prev16, float pos16, bool playing) noexcept
{
    if (laneL == nullptr || laneR == nullptr)
        return;
    for (int i = 0; i < NumParts; ++i)
        laneL[i] = laneR[i] = 0.f;
    for (int v = 0; v < NumDrumVoices; ++v)
        drumOutL[v] = drumOutR[v] = 0.f;

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
            if (v.part == Drums && v.drumVoice >= 0 && v.drumVoice < NumDrumVoices)
            {
                drumOutL[v.drumVoice] += l;
                drumOutR[v.drumVoice] += r;
            }
        }
    }
}

} // namespace rolla
