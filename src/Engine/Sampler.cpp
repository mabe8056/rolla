#include "Sampler.h"
#include "Engine.h"
#include "../Drums/DrumMachine.h"
#include "../Sampler/PlayDecision.h"
#include "../Sampler/UserSlots.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace rolla
{
namespace
{

constexpr float kSincPi = 3.14159265358979323846f;

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

// Plugin builds play samples only: no preset or saved state can hand a lane
// (or a drum voice) to the synth, and a LAYER preset drops its synth half.
// Tools and tests keep SYNTH presets so they can render without a sample pack.
#if ROLLA_PLUGIN_BUILD
constexpr bool kSamplesOnly = true;
#else
constexpr bool kSamplesOnly = false;
#endif

inline bool synthMode (const SampleSpec& spec) noexcept
{
    return ! kSamplesOnly && spec.mode == SampleMode::Synth;
}

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
    library->request (file);
    return FileState::Pending;
}

FileState peekFile (const SampleLibrary* library, const char* file, const SampleAudio*& audio) noexcept
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

// Bass may move a full octave from its file (octave bounces); other lanes 7 st.
// Vox one-shots and chops keep the voice they were recorded with.
ShiftRule shiftRuleFor (int part) noexcept
{
    ShiftRule rule;
    if (part == Bass)
        rule.cap = 12.0;
    else if (part == Vox)
        rule.retune = false;
    return rule;
}

struct LaneHit
{
    const SampleSpec* spec = nullptr;
    SampleChoice choice {};
    const SampleAudio* audio = nullptr;
    bool pending = false;
    // The first still-loading file the note waits on (nullptr when none).
    const char* waitFile = nullptr;
};

// Preferred file is requested. Already-loaded neighbours can play immediately.
// A still-loading preferred file does not fall through to the synth.
bool readyFromIndex (const SampleLibrary* library, const SampleIndex* index, int part,
                     const SampleSpec& spec, int pick, int voice, int dialect, LaneHit& hit) noexcept
{
    if (library == nullptr || index == nullptr)
        return false;
    if (voice < 0 && ! spec.filter.on)
        return false;
    const int queryVoice = voice >= 0 ? voice : spec.filter.voice;
    const PoolFilter filter = poolFilterWithFiles (*index, spec, part, queryVoice, dialect);
    const int n = sampleIndexCount (*index, part, filter);
    if (n <= 0)
        return false;
    if (pick < 0)
        pick = 0;
    const int start = pick % n;
    const SampleAudio* audio = nullptr;
    auto adopt = [&] (const SampleEntry* entry, const SampleAudio* got) noexcept
    {
        hit.spec = &spec;
        hit.choice.file = entry->path;
        hit.choice.root = entry->root > 0 ? entry->root : 60;
        hit.choice.phraseLoop = entry->phraseLoop;
        hit.choice.fileBpm = entry->fileBpm;
        hit.audio = got;
        hit.pending = false;
    };
    const auto* preferred = sampleIndexAt (*index, part, filter, start);
    if (preferred != nullptr)
    {
        const auto state = peekFile (library, preferred->path, audio);
        if (state == FileState::Ready)
        {
            adopt (preferred, audio);
            return true;
        }
        if (state == FileState::Pending)
        {
            library->request (preferred->path);
            hit.pending = true;
            hit.waitFile = preferred->path;
        }
    }
    const int window = n < 32 ? n : 32;
    for (int i = 1; i < window; ++i)
    {
        const auto* entry = sampleIndexAt (*index, part, filter, (start + i) % n);
        if (entry == nullptr)
            continue;
        const auto state = peekFile (library, entry->path, audio);
        if (state == FileState::Ready)
        {
            adopt (entry, audio);
            return true;
        }
    }
    if (hit.pending)
    {
        hit.spec = &spec;
        return false;
    }
    bool requested = false;
    const int limit = n < 64 ? n : 64;
    for (int i = 1; i < limit; ++i)
    {
        const auto* entry = sampleIndexAt (*index, part, filter, (start + i) % n);
        if (entry == nullptr)
            continue;
        const auto state = peekFile (library, entry->path, audio);
        if (state == FileState::Ready)
        {
            adopt (entry, audio);
            return true;
        }
        if (state == FileState::Pending && ! requested)
        {
            library->request (entry->path);
            hit.pending = true;
            hit.waitFile = entry->path;
            hit.spec = &spec;
            requested = true;
        }
    }
    return false;
}

bool readyChoice (const SampleLibrary* library, const SampleSpec& spec, int midiNote,
                  int pick, int sliceIndex, LaneHit& hit) noexcept
{
    if (synthMode (spec))
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
    {
        hit.pending = true;
        hit.waitFile = choice.file;
    }

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
        {
            hit.pending = true;
            if (hit.waitFile == nullptr)
                hit.waitFile = extras[i];
        }
    }
    return false;
}

// The chop gate (SampleChop.h) for one loaded file, with the inputs take() uses.
ChopPlan chopPlanFor (int part, const SampleAudio& audio, const char* file, const EngineParams& params) noexcept
{
    const bool hardName = sampleTextListed (file, kHardBroadband, 6);
    const bool padBed = part == Pad && sampleTextListed (file, kSoftPadBed, 4);
    const auto seed = (std::uint32_t) std::max (0, params.samplePick[part]);
    return planSampleChop (part, audio.frames, audio.rate, params.bpm, seed,
                           audio.slices, audio.sliceCount, audio.loudStart,
                           audio.flatness, hardName, padBed);
}

// HITS / STAB: a blocked chop drops that file, not the note. The next loaded
// file in the same pool whose chop is not blocked plays instead, the way a
// missing file swaps within its pool. The first still-loading neighbour is
// requested so a later note can use it. False when nothing usable is loaded.
bool unblockedSwap (const SampleLibrary* library, const SampleIndex* index, const SampleSpec& spec,
                    bool recipe, int part, const EngineParams& params, const char* blockedFile,
                    SampleChoice& choice, const SampleAudio*& audio, ChopPlan& chop) noexcept
{
    if (library == nullptr || (part != Hits && part != Stab))
        return false;
    bool requested = false;
    // Returns true when `file` is loaded and its chop plays (or it is a phrase loop, which is not chopped).
    auto usable = [&] (const char* file, bool phraseLoop, int fileBpm, const SampleAudio*& got, ChopPlan& plan) noexcept
    {
        got = nullptr;
        plan = ChopPlan {};
        if (file == nullptr || file[0] == 0)
            return false;
        if (blockedFile != nullptr && std::strcmp (file, blockedFile) == 0)
            return false;
        const auto state = peekFile (library, file, got);
        if (state == FileState::Pending && ! requested)
        {
            library->request (file);
            requested = true;
        }
        if (state != FileState::Ready || got == nullptr)
            return false;
        if (! (phraseLoop && fileBpm > 0))
            plan = chopPlanFor (part, *got, file, params);
        return ! plan.blocked;
    };

    if (recipe && index != nullptr)
    {
        const PoolFilter filter = poolFilterWithFiles (*index, spec, part, spec.filter.voice, params.dialect);
        const int n = sampleIndexCount (*index, part, filter);
        if (n <= 1)
            return false;
        const int start = std::max (0, params.samplePick[part]) % n;
        const int limit = n < 64 ? n : 64;
        for (int i = 1; i < limit; ++i)
        {
            const auto* entry = sampleIndexAt (*index, part, filter, (start + i) % n);
            if (entry == nullptr)
                continue;
            const SampleAudio* got = nullptr;
            ChopPlan plan;
            if (! usable (entry->path, entry->phraseLoop, entry->fileBpm, got, plan))
                continue;
            choice.file = entry->path;
            choice.root = entry->root > 0 ? entry->root : 60;
            choice.phraseLoop = entry->phraseLoop;
            choice.fileBpm = entry->fileBpm;
            choice.slice = false;
            audio = got;
            chop = plan;
            return true;
        }
        return false;
    }

    // A preset's own file list: its file, its pool, its zones.
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
        const SampleAudio* got = nullptr;
        ChopPlan plan;
        if (! usable (extras[i], false, 0, got, plan))
            continue;
        choice.file = extras[i];
        audio = got;
        chop = plan;
        return true;
    }
    return false;
}

// The chosen preset wins. A confirmed-missing file tries the rest of the lane.
// A file that is still loading does not get replaced by another sample.
// If nothing is ready, take() plays the matching synth instead of silence.
LaneHit resolveLane (const PresetBank& bank, const EngineParams& params, int part,
                     int midiNote, int sliceIndex) noexcept
{
    LaneHit hit;
    const int current = presetOf (params, part);
    const auto& currentSpec = specAt (bank, part, current);
    if (synthMode (currentSpec))
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

double kickKeyFollowRatio (const PitchInfo& pitch, int key) noexcept
{
    // A chord reading on a kick is noise, not a fundamental.
    if (! pitch.pitched() || pitch.isChord || ! (pitch.conf >= kKickFollowConfMin))
        return 1.0;
    double f0 = 440.0 * std::pow (2.0, ((double) pitch.rootPitch - 69.0) / 12.0);
    if (! std::isfinite (f0) || f0 <= 1.0)
        return 1.0;
    // The probe may read the body's octave or a harmonic: fold into the kick register.
    while (f0 >= 80.0)
        f0 *= 0.5;
    while (f0 < 40.0)
        f0 *= 2.0;
    const double target = (double) DrumMachine::kickHzForKey (key);
    const double semis = std::clamp (12.0 * std::log2 (target / f0), -kKickFollowMaxSt, kKickFollowMaxSt);
    return std::pow (2.0, semis / 12.0);
}

void Sampler::Voice::Env::start (float aMs, float dMs, float sGain, float rMs, float sr) noexcept
{
    stage = 1;
    amp = 0.f;
    sustain = std::clamp (sGain, 0.f, 1.f);
    attInc = 1.f / std::max (1.f, aMs * 0.001f * sr);
    // No attack: full level from the first sample (a sample's own attack is in the file).
    if (aMs <= 0.f)
    {
        amp = 1.f;
        stage = 2;
    }
    decC = coefFromMs (std::max (5.f, dMs) / 3.f, sr);
    // At least a 4 ms time constant: a shorter release clicks on square or saw samples.
    relC = coefFromMs (std::max (12.f, rMs) / 3.f, sr);
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
    {
        amp = sustain;
        // A drum hit (sustain 0) that has decayed is silent: free the voice. Silent
        // voices held to the end of a long file filled the pool and forced steals.
        if (sustain <= 0.f)
        {
            amp = 0.f;
            stage = 0;
            return false;
        }
    }
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
    for (auto& v : tails)
        v = {};
    for (int& c : sliceCursor)
        c = 0;
    for (auto& h : held)
        h = {};
    heldCount = 0;
    pendingNote = false;
    pendingFile = nullptr;
    lateStart = false;
    stepEst = 0.f;
}

bool Sampler::opensOnStep (const SampleAudio& a) noexcept
{
    const int n = std::min (a.frames, (int) a.left.size());
    if (n < 2)
        return false;
    const bool stereo = (int) a.right.size() >= n;
    const int span = std::clamp ((int) (0.001 * a.rate), 8, n - 1);
    double first = 0.0, moves = 0.0;
    for (int c = 0; c < (stereo ? 2 : 1); ++c)
    {
        const auto& x = c == 0 ? a.left : a.right;
        first = std::max (first, (double) std::fabs (x[0]));
        double sum = 0.0;
        for (int k = 1; k <= span; ++k)
            sum += std::fabs ((double) x[(size_t) k] - (double) x[(size_t) k - 1]);
        moves = std::max (moves, sum / (double) span);
    }
    return first > 1.0e-3 && first > 4.0 * moves;
}

void Sampler::armAttack (Voice& v) noexcept
{
    // A note that starts mid-file (slice, start offset, vox lead-in, reverse)
    // fades in over 3 ms so the cut does not click. One that starts on the
    // file's first sample fades in over 0.5 ms when the file opens on a step
    // (opensOnStep: trimmed onto a non-zero sample, like most pack kicks, rims
    // and bass stabs), so it no longer opens on a full-level step. A file that
    // opens into noise or rises from zero plays as recorded: fading it moved
    // a noisy kick's attack bands by up to 4 dB.
    const bool fileStart = ! v.reverse && ! v.tempoSync && v.pos < 0.5;
    const bool fade = ! fileStart || (v.audio != nullptr && opensOnStep (*v.audio));
    v.attackN = fade ? std::max (8, (int) ((fileStart ? 0.0005 : 0.003) * sr)) : 0;
    v.attackLeft = v.attackN;
    v.fade = 1.f;
    v.fadeInc = 0.f;
    v.releaseLeft = 0;
    v.releaseN = 0;
    v.noteEdge = 1.f;
    v.stopping = false;
}

float Sampler::audibleLevel (const Voice& v) noexcept
{
    if (! v.on)
        return 0.f;
    // Born this sample, or still in its attack: about to be loud, whatever env.amp says.
    if (v.born)
        return 1.0e9f;
    float g = v.env.stage == 1 ? 1.f : v.env.amp;
    if (v.attackN > 0 && v.attackLeft > 0)
        g = std::max (g, 1.f);
    if (v.stopping && v.releaseN > 0)
    {
        const float t = std::clamp ((float) v.releaseLeft / (float) v.releaseN, 0.f, 1.f);
        g *= 0.5f * (1.f - std::cos (kSincPi * t));
    }
    return g * std::fabs (v.gain) * v.fade * v.noteEdge;
}

void Sampler::park (const Voice& dying) noexcept
{
    Voice* dest = nullptr;
    Voice* quiet = nullptr;
    float quietG = 1.0e9f;
    for (auto& t : tails)
    {
        if (! t.on)
        {
            dest = &t;
            break;
        }
        const float g = audibleLevel (t);
        if (g < quietG)
        {
            quietG = g;
            quiet = &t;
        }
    }
    if (dest == nullptr)
        dest = quiet;
    if (dest == nullptr)
        return;
    *dest = dying;
    // Keep the attack edge running: zeroing it mid-attack stepped the voice up
    // to full level. The release multiplies on top, so both edges stay smooth.
    // A voice already releasing keeps its own release: restarting at 1 is a step.
    if (! (dying.stopping && dying.releaseN > 0))
    {
        dest->releaseN = std::max (8, (int) (0.004 * sr));
        dest->releaseLeft = dest->releaseN;
    }
    dest->stopping = true;
    dest->fadeInc = 0.f;
}

void Sampler::chokeDrum (int drumVoice) noexcept
{
    const int n = std::max (8, (int) (0.005 * sr));
    for (auto& v : voices)
    {
        if (! v.on || v.part != Drums || v.drumVoice != drumVoice || v.born || v.stopping)
            continue;
        v.releaseN = n;
        v.releaseLeft = n;
        v.stopping = true;
        v.fadeInc = 0.f;
    }
}

void Sampler::releaseNote (int part, int midiNote) noexcept
{
    for (auto& v : voices)
        if (v.on && v.part == part && v.midiNote == midiNote)
            v.env.release();
    for (auto& h : held)
        if (h.on && h.part == part && h.midiNote == midiNote)
            dropHeld (h);
}

void Sampler::releaseAll() noexcept
{
    for (auto& v : voices)
        if (v.on)
            v.env.release();
    for (auto& h : held)
        dropHeld (h);
}

Sampler::Voice* Sampler::allocate (int part) noexcept
{
    Voice* free = nullptr;
    Voice* quiet = nullptr;
    float quietG = 1.0e8f;
    Voice* const first = part == Drums ? voices : voices + kDrumVoices;
    Voice* const last = part == Drums ? voices + kDrumVoices : voices + kDrumVoices + kMelodicVoices;
    for (Voice* p = first; p != last; ++p)
    {
        Voice& v = *p;
        if (! v.on)
        {
            free = &v;
            break;
        }
        // env.amp alone picked a voice born this same sample (amp still 0): the
        // kick was parked 4 ms after it started and the hat took its slot.
        const float g = audibleLevel (v);
        if (g < quietG)
        {
            quietG = g;
            quiet = &v;
        }
    }
    Voice* slot = free != nullptr ? free : quiet;
    if (slot == nullptr)
        return nullptr;
    if (slot->on)
        park (*slot);
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
    // The release has reached zero. This used to be noticed only after the edge
    // was computed, so the last sample of every steal, choke or relocate fade
    // played at full level: a one-sample spike 4 ms after the hit.
    if (v.stopping && v.releaseN > 0 && v.releaseLeft <= 0)
    {
        v.on = false;
        return;
    }

    const double readAt = v.pos;
    const double n = (double) v.audio->frames;
    auto filePos = [&] (double p) noexcept -> double
    {
        if (v.loop && v.loopB > v.loopA + 8.0)
        {
            const double span = v.loopB - v.loopA;
            double x = std::fmod (p - v.loopA, span);
            if (x < 0.0)
                x += span;
            p = v.loopA + x;
        }
        if (p < 0.0)
            p = 0.0;
        if (p > n - 1.0)
            p = n - 1.0;
        return v.reverse ? (n - 1.0 - p) : p;
    };

    double index = 0.0;
    const double unity = v.audio->rate > 1.0 ? v.audio->rate / sr : 1.0;
    if (v.tempoSync)
    {
        const double grain = std::max (64.0, 0.030 * v.audio->rate);
        if (v.spliceLeft <= 0 && std::fabs (v.pos - v.anchor) > grain)
        {
            v.splicePos = v.pos;
            v.spliceLeft = std::max (8, (int) (0.008 * sr));
            v.pos = v.anchor;
        }
        index = filePos (v.pos);
    }
    else
    {
        if (v.loop && v.loopB > v.loopA + 8.0 && v.pos >= v.loopB)
        {
            const double span = v.loopB - v.loopA;
            v.pos = v.loopA + std::fmod (v.pos - v.loopA, span);
        }
        index = v.reverse ? (n - 1.0 - v.pos) : v.pos;
    }

    float ls = readSinc (v.audio->left, index);
    float rs = readSinc (v.audio->right, index);

    if (! v.tempoSync && v.loop && v.xfade > 4.0 && v.pos > v.loopB - v.xfade && v.pos < v.loopB)
    {
        const float t = (float) ((v.pos - (v.loopB - v.xfade)) / v.xfade);
        const double head = v.loopA + (v.pos - (v.loopB - v.xfade));
        const double hi = v.reverse ? (n - 1.0 - head) : head;
        const float w = std::sin (t * 0.5f * kSincPi);
        const float u = std::cos (t * 0.5f * kSincPi);
        ls = ls * u + readSinc (v.audio->left, hi) * w;
        rs = rs * u + readSinc (v.audio->right, hi) * w;
    }
    else if (v.tempoSync && v.spliceLeft > 0)
    {
        const int fadeN = std::max (8, (int) (0.008 * sr));
        const float t = std::clamp (1.f - (float) v.spliceLeft / (float) fadeN, 0.f, 1.f);
        const float w = std::sin (t * 0.5f * kSincPi);
        const float u = std::cos (t * 0.5f * kSincPi);
        const double si = filePos (v.splicePos);
        ls = readSinc (v.audio->left, si) * u + ls * w;
        rs = readSinc (v.audio->right, si) * u + rs * w;
        v.splicePos += unity;
        --v.spliceLeft;
    }

    if (v.tempoSync)
    {
        v.pos += unity;
        v.anchor += unity * (double) v.stretch;
    }
    else
        v.pos += v.ratio;
    if (v.chopEnd > v.chopStart && v.pos >= v.chopEnd)
        v.on = false;
    if (! v.loop && v.pos >= n - 1.0)
        v.on = false;
    // The file as recorded unless TONE moved: no ladder, no HPF, and L and R
    // always through the same path (the old path filtered L only and folded
    // 15% of it into R, which made stereo samples lopsided and partly mono).
    const float srF = (float) sr;
    if (v.lowpass)
    {
        if (v.fEnv > 0.f)
            v.fEnv *= v.fCoef;
        const float fc = std::clamp (v.cut * std::pow (2.f, v.envOct * v.fEnv), 40.f, srF * 0.45f);
        ls = v.ladder.process (ls, fc, v.res, srF);
        rs = v.ladderR.process (rs, fc, v.res, srF);
    }
    if (v.hpfHz > 0.f)
    {
        const float c = std::exp (-2.f * kSincPi * v.hpfHz / srF);
        v.hpState = c * v.hpState + (1.f - c) * ls;
        v.hpStateR = c * v.hpStateR + (1.f - c) * rs;
        ls -= v.hpState;
        rs -= v.hpStateR;
    }
    if (v.gateLeft > 0 && --v.gateLeft == 0)
        v.env.release();

    float edge = v.noteEdge;
    if (v.chopFade > 0 && v.chopEnd > v.chopStart)
        edge *= chopEdge ((int) readAt, (int) v.chopStart, (int) v.chopEnd, v.chopFade);
    if (v.attackN > 0 && v.attackLeft > 0)
    {
        const float t = 1.f - (float) v.attackLeft / (float) v.attackN;
        edge *= 0.5f * (1.f - std::cos (kSincPi * t));
        --v.attackLeft;
    }
    if (v.releaseN > 0 && v.releaseLeft > 0)
    {
        const float t = (float) v.releaseLeft / (float) v.releaseN;
        edge *= 0.5f * (1.f - std::cos (kSincPi * t));
        --v.releaseLeft;
    }
    if (! v.loop)
    {
        const double remain = n - v.pos;
        const double fade = std::max (8.0, 0.004 * (v.audio->rate > 1.0 ? v.audio->rate : sr));
        if (remain < fade)
        {
            const float t = std::clamp ((float) (remain / fade), 0.f, 1.f);
            edge *= 0.5f * (1.f - std::cos (kSincPi * t));
        }
    }

    const float env = v.env.amp;
    v.fade = std::clamp (v.fade + v.fadeInc, 0.f, 1.f);
    const float g = env * v.gain * v.fade * edge;
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
        // A vox sample plays as recorded. The preset's decayMs / brightness / trimDb
        // shape the formant synth (Engine.cpp), not a file the loader already set to
        // -24 LUFS: sustain 0 with a 140 ms decay faded a 278 ms shot out (~-11 dB,
        // shape lost), the 4-pole at 4.6 kHz is -3 dB near 2 kHz, the 200 Hz HPF ate
        // the fundamental and ampDb (-8) * 0.25 was another -20 dB (issue #68).
        // TONE still darkens it (cut / 4 at 0); PUNCH still moves the attack.
        a = { 3.f, 200.f, 1.f, 30.f, 20000.f, 0.f, 0.f, 40.f, 40.f, 1.f };
    }
    else
    {
        const auto& p = bank.drums[i];
        a = { 1.f, 180.f, 0.f, 20.f, 8000.f, 0.02f, 0.f, 20.f, 30.f, ampDb (p.trimDb) * 0.5f };
    }
    return a;
}

// Samples play the file as recorded. TONE away from neutral (0.5) is the only
// thing that filters them: darker sweeps a lowpass from 20 kHz down to the lane
// preset's cutoff at 0 (with the preset's filter envelope), brighter raises a
// highpass up to 320 Hz at 1.
struct SampleFilter
{
    bool lowpass = false;
    float cut = 20000.f;
    float envOct = 0.f;
    float hpf = 0.f;
};

SampleFilter sampleFilter (const EngineParams& params, int part, const Amp& amp) noexcept
{
    SampleFilter f;
    if (! params.sampleShaping || part < 0 || part >= NumParts)
        return f;
    const float t = std::clamp (params.tone[part], 0.f, 1.f) - 0.5f;
    if (t <= -0.01f)
    {
        const float k = std::min (1.f, -t * 2.f);
        f.lowpass = true;
        f.cut = 20000.f * std::pow (std::max (40.f, amp.cut) / 20000.f, k);
        f.envOct = amp.envOct * k;
    }
    else if (t >= 0.01f)
        f.hpf = 20.f * std::pow (2.f, 4.f * std::min (1.f, t * 2.f));
    return f;
}

// A template so it can take the sampler's private Voice.
template <typename VoiceT>
void applyFilter (VoiceT& v, const SampleFilter& f) noexcept
{
    v.lowpass = f.lowpass;
    v.cut = f.cut;
    v.envOct = f.envOct;
    v.hpfHz = f.hpf;
    v.hpState = v.hpStateR = 0.f;
    v.ladder.reset();
    v.ladderR.reset();
}

// PUNCH on samples: 0.5 (the default) is the file as recorded. Below, a slower
// attack (up to 40 ms at 0, round). Above, the note decays away instead of
// sustaining (about 50 ms at 1, snappy).
struct SamplePunch
{
    float aMs = 0.f;
    float dMs = 5.f;
    float s = 1.f;
};

SamplePunch samplePunch (const EngineParams& params, int part) noexcept
{
    SamplePunch e;
    if (! params.sampleShaping || part < 0 || part >= NumParts)
        return e;
    const float p = std::clamp (params.punch[part], 0.f, 1.f) - 0.5f;
    if (p <= -0.01f)
        e.aMs = 40.f * std::min (1.f, -p * 2.f);
    else if (p >= 0.01f)
    {
        e.s = 0.f;
        e.dMs = 1500.f * std::pow (2.f, -5.f * std::min (1.f, p * 2.f));
    }
    return e;
}

// DECAY 0.5 (the default) and up plays a drum hit's whole length. Below 0.5 the
// hit is cut at a fraction of what is left of the file (1/8 at 0) and released
// over 12 ms. -1 means no gate.
int drumGate (float decayKnob, const SampleAudio& audio, double startFrame, double ratio) noexcept
{
    const float d = std::clamp (decayKnob, 0.f, 1.f);
    if (d >= 0.5f)
        return -1;
    const double left = std::max (8.0, ((double) audio.frames - startFrame) / std::max (1.0e-3, ratio));
    return std::max (8, (int) (left * std::pow (2.0, (double) (d - 0.5f) * 6.0)));
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
        // The slot owns the voice. Until its file is ready the synth stays off.
        take.kind = 1;
        take.synthGain = 0.f;
        drumKeep = 0.f;
        // A melodic note waits for the slot file instead of being lost.
        if (slot.loading && drumVoice < 0)
        {
            pendingNote = true;
            pendingFile = nullptr;
        }
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
    notePicked (voicePart, drumVoice, pod.audio);
    fresh.reverse = decision.reverse;
    fresh.ratio = std::clamp (std::pow (2.0, semis / 12.0) * (pod.audio->rate / sr), ratioMin, ratioMax);
    fresh.end16 = end16;
    fresh.fade = 0.f;
    fresh.fadeInc = drumVoice >= 0 ? 1.f / std::max (8.f, 0.002f * srF) : 1.f / 64.f;
    fresh.stopping = false;
    float trim = amp.trim * std::clamp (velocity / 127.f, 0.05f, 1.f);
    if (drumVoice >= 0)
        trim *= ampDb (params.drumLevelDb[drumVoice]) * drumGain;
    fresh.gain = trim * decision.sampleMix * ampDb (decision.gainDb);
    fresh.pan = (part == Stab || part == Pad) ? 0.15f : 0.f;
    fresh.res = amp.res;
    fresh.fEnv = 1.f;
    fresh.fCoef = expDecay (std::max (4.f, drumVoice >= 0 ? 30.f : amp.tau), srF);
    applyFilter (fresh, sampleFilter (params, voicePart, amp));
    // Natural length: the file's own envelope, released at the note end (melodic)
    // or by the DECAY gate (drums). The 3 ms raised-cosine attack is armAttack.
    {
        const auto pe = samplePunch (params, voicePart);
        fresh.env.start (pe.aMs, pe.dMs, pe.s, drumVoice >= 0 ? 12.f : amp.r, srF);
    }

    double startFrame = std::min (std::clamp ((double) decision.startSec, 0.0, 60.0) * pod.audio->rate,
                                   (double) pod.audio->frames - 1.0);
    // A dropped vox file left at start 0 opens on its voice, not its lead-in.
    // A start the user set is kept.
    if (part == Vox && drumVoice < 0 && decision.startSec <= 0.f && ! decision.reverse && pod.audio->voicedCount > 0)
        startFrame = (double) voxStart (0, false, 0, pod.audio->voiced, pod.audio->voicedCount, pod.audio->voicedFloor,
                                        pod.audio->left.data(), pod.audio->right.empty() ? nullptr : pod.audio->right.data(),
                                        pod.audio->frames, pod.audio->rate);
    if (drumVoice >= 0)
        fresh.gateLeft = drumGate (params.drumDecay[drumVoice], *pod.audio, startFrame, fresh.ratio);
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
    armAttack (fresh);
    fresh.born = true;
    *voice = fresh;

    take.kind = decision.kind;
    take.synthGain = decision.synthGain;
    drumKeep = decision.synthGain;
    return true;
}

Sampler::Take Sampler::take (int part, int midiNote, int velocity, float end16,
                             const PresetBank& bank, const EngineParams& params, ChordAsk ask) noexcept
{
    // A new note on a lane replaces an older held copy of the same pitch; mono
    // lanes (BASS, VOX) replace every held note, so a late start never stacks.
    for (auto& h : held)
        if (h.on && h.part == part && (h.midiNote == midiNote || part == Bass || part == Vox))
            dropHeld (h);
    pendingNote = false;
    pendingFile = nullptr;
    const Take result = startNote (part, midiNote, velocity, end16, bank, params, ask);
    // Only notes with a known length are held; a live key without one could start after its release.
    if (pendingNote && velocity > 0 && end16 < 1.0e8f)
        holdNote (part, midiNote, velocity, end16, ask, pendingFile);
    pendingNote = false;
    pendingFile = nullptr;
    return result;
}

void Sampler::holdNote (int part, int midiNote, int velocity, float end16, ChordAsk ask, const char* file) noexcept
{
    Held* slot = nullptr;
    for (auto& h : held)
        if (! h.on)
        {
            slot = &h;
            break;
        }
    if (slot == nullptr)
        return;
    slot->on = true;
    slot->born = true;
    slot->part = part;
    slot->midiNote = midiNote;
    slot->velocity = velocity;
    slot->end16 = end16;
    slot->ask = ask;
    slot->wait = 0;
    slot->sweep = 0;
    slot->file[0] = 0;
    if (file != nullptr)
    {
        std::strncpy (slot->file, file, sizeof (slot->file) - 1);
        slot->file[sizeof (slot->file) - 1] = 0;
    }
    ++heldCount;
}

void Sampler::dropHeld (Held& h) noexcept
{
    if (! h.on)
        return;
    h.on = false;
    heldCount = std::max (0, heldCount - 1);
}

void Sampler::retryHeld (const PresetBank& bank, const EngineParams& params,
                         float prev16, float pos16, bool playing) noexcept
{
    if (heldCount <= 0)
        return;
    // Cheap poll of the awaited file every 32 samples; a full re-resolve every
    // 2048 samples covers a pick that moved on (an absent file swaps to the next).
    constexpr int kPoll = 32;
    constexpr int kSweep = 2048 / kPoll;
    for (auto& h : held)
    {
        if (! h.on)
            continue;
        if (h.born)
        {
            h.born = false;
            continue;
        }
        if (! playing || pos16 < prev16 || pos16 >= h.end16)
        {
            dropHeld (h);
            continue;
        }
        if (--h.wait > 0)
            continue;
        h.wait = kPoll;
        bool due = ++h.sweep >= kSweep;
        if (h.file[0] != 0 && params.library != nullptr
            && (params.library->ready (h.file) || params.library->missing (h.file)))
            due = true;
        if (! due)
            continue;
        h.sweep = 0;
        pendingNote = false;
        pendingFile = nullptr;
        lateStart = true;
        const Take started = startNote (h.part, h.midiNote, h.velocity, h.end16, bank, params, h.ask);
        lateStart = false;
        if (pendingNote)
        {
            // Still loading (perhaps a pool neighbour now): keep waiting on it.
            if (pendingFile != nullptr && std::strcmp (pendingFile, h.file) != 0)
            {
                std::strncpy (h.file, pendingFile, sizeof (h.file) - 1);
                h.file[sizeof (h.file) - 1] = 0;
            }
        }
        else
        {
            const int donePart = h.part;
            const float doneEnd = h.end16;
            dropHeld (h);
            // A recorded chord sounds every tone: the other held tones of that chord stay quiet.
            if (started.chordFile)
                for (auto& other : held)
                    if (other.on && other.part == donePart && other.end16 == doneEnd)
                        dropHeld (other);
        }
        pendingNote = false;
        pendingFile = nullptr;
    }
}

Sampler::Take Sampler::startNote (int part, int midiNote, int velocity, float end16,
                                  const PresetBank& bank, const EngineParams& params, ChordAsk ask) noexcept
{
    Take result;
    if (part < 0 || part >= Drums)
        return result;
    float drumKeep = 1.f;
    if (playUserSlot (part, -1, midiNote, velocity, end16, bank, params, result, drumKeep))
        return result;
    const auto& spec = sampleSpecFor (bank, part, presetOf (params, part));
    if (synthMode (spec))
        return result;
    // No library: the factory file cannot load. Same-pool swap needs a library,
    // and a missing sample never falls through to the lane synth.
    if (params.library == nullptr)
    {
        result.kind = 1;
        result.synthGain = 0.f;
        return result;
    }

    // A held note retried late keeps the slice the lane would draw next.
    const int sliceIndex = lateStart ? sliceCursor[part] : sliceCursor[part]++;
    LaneHit hit;
    const bool recipe = spec.filter.on && params.sampleIndex != nullptr;
    if (recipe)
        readyFromIndex (params.library, params.sampleIndex, part, spec, params.samplePick[part], -1, params.dialect, hit);
    // A scanned lane stays in that lane. An empty filtered pool mutes the note.
    // It does not borrow another preset file, the drum pack, or the synth.
    if (hit.audio == nullptr && ! hit.pending && ! recipe)
        hit = resolveLane (bank, params, part, midiNote, sliceIndex);
    const SampleSpec& played = hit.spec != nullptr ? *hit.spec : spec;
    SampleChoice choice = hit.choice;
    const SampleAudio* audio = hit.audio;
    bool fromPack = false;
    if ((audio == nullptr || audio->frames < 8) && ! hit.pending && ! recipe)
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
            if (audio == nullptr && params.sampleIndex != nullptr)
            {
                for (int alt : kPack)
                {
                    LaneHit packHit;
                    const int shifted = pick + (alt == TomMid ? 1 : alt == TomLow ? 2 : 0);
                    if (readyFromIndex (params.library, params.sampleIndex, Drums, drum.sample, shifted, alt, params.dialect, packHit)
                        && packHit.audio != nullptr)
                    {
                        audio = packHit.audio;
                        choice.file = packHit.choice.file;
                        choice.root = packHit.choice.root > 0 ? packHit.choice.root : 60;
                        choice.phraseLoop = packHit.choice.phraseLoop;
                        choice.fileBpm = packHit.choice.fileBpm;
                        fromPack = true;
                        break;
                    }
                }
            }
        }
    }
    // Still loading, or confirmed missing after the same-pool swap: the sample
    // owns this note. The lane synth does not fill the gap. A still-loading file
    // is not the end of the note: take() holds it and retryHeld() starts it late.
    if (audio == nullptr || audio->frames < 8)
    {
        if (hit.pending)
        {
            pendingNote = true;
            pendingFile = hit.waitFile;
        }
        result.kind = 1;
        result.synthGain = 0.f;
        return result;
    }

    // Slice in memory. A blocked chop is broadband: that file is not played.
    // HITS and STAB swap to another file in the same pool whose chop plays, so
    // one broadband file cannot silence the lane for the whole roll. Only when
    // no such file is loaded does the note stay silent (a PAD bed always does).
    ChopPlan chop;
    const bool phrase = choice.phraseLoop && choice.fileBpm > 0;
    if (! fromPack && ! phrase && choice.file != nullptr && choice.file[0] != 0
        && (part == Hits || part == Stab || part == Pad))
    {
        const bool padBed = part == Pad && sampleTextListed (choice.file, kSoftPadBed, 4);
        chop = chopPlanFor (part, *audio, choice.file, params);
        if (chop.blocked && (part == Hits || part == Stab)
            && unblockedSwap (params.library, params.sampleIndex, played, recipe, part, params,
                              choice.file, choice, audio, chop))
            chop.blocked = false;
        if (chop.blocked && (part == Hits || part == Stab || padBed))
        {
            result.kind = 1;
            result.synthGain = 0.f;
            return result;
        }
    }

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
    notePicked (part, -1, audio);
    fresh.reverse = played.reverse;
    // Pitch comes from the measured root (PitchProbe.h), not the manifest or a
    // default C: unpitched, low-confidence and vox files play as recorded.
    const auto& pitch = audio->pitch;
    const bool chordFile = pitch.isChord && pitch.conf >= kPitchConfMin && part != Bass && part != Vox;
    const int toward = midiNote + (chordFile ? ask.rootDelta + chordFileOffset (pitch.quality, ask.quality) : 0);
    const double semis = tonalShift (pitch, audio->namedRoot, (double) toward, shiftRuleFor (part));
    fresh.ratio = std::clamp (std::pow (2.0, semis / 12.0) * (audio->rate / sr), 0.05, 16.0);
    fresh.end16 = end16;
    fresh.fade = 0.f;
    fresh.fadeInc = 1.f / 64.f;
    fresh.stopping = false;
    fresh.gain = amp.trim * std::clamp (velocity / 127.f, 0.05f, 1.f);
    // A factory LAYER preset keeps its synth half only outside the plugin.
    const bool layered = &played == &spec && played.mode == SampleMode::Layer && ! kSamplesOnly;
    if (layered)
        fresh.gain *= std::clamp (played.layerMix, 0.f, 1.f);
    fresh.pan = (part == Stab || part == Pad) ? 0.15f : 0.f;
    fresh.res = amp.res;
    fresh.fEnv = 1.f;
    fresh.fCoef = expDecay (std::max (4.f, amp.tau), srF);
    applyFilter (fresh, sampleFilter (params, part, amp));
    // Natural length: no synth ADSR on a sample. It sustains at full level and
    // releases at the note end with the preset's release.
    {
        const auto pe = samplePunch (params, part);
        fresh.env.start (pe.aMs, pe.dMs, pe.s, amp.r, srF);
    }

    // A preset's start offset and loop points were set for its own file. A file
    // drawn from a pool or a recipe plays from its first sample, once.
    const bool ownFile = ! fromPack && choice.file != nullptr && played.file[0] != 0
                         && std::strcmp (choice.file, played.file) == 0;
    double startFrame = ownFile ? std::clamp ((double) played.start, 0.0, 16.0) * audio->rate : 0.0;
    if (choice.slice && audio->sliceCount > 1)
    {
        const int s = (choice.sliceIndex % audio->sliceCount + audio->sliceCount) % audio->sliceCount;
        startFrame = (double) audio->slices[s];
    }
    // A vox chop that would start in the file's lead-in silence starts at the first word.
    if (part == Vox && startFrame < (double) audio->lead)
        startFrame = (double) audio->lead;
    // VOX chops start on the voice, not on a lead-in or a breath (in memory).
    if (part == Vox && ! fromPack && ! phrase && audio->voicedCount > 0)
        startFrame = (double) voxStart ((int) startFrame, choice.slice, choice.sliceIndex, audio->voiced, audio->voicedCount,
                                        audio->voicedFloor, audio->left.data(),
                                        audio->right.empty() ? nullptr : audio->right.data(), audio->frames, audio->rate);
    fresh.pos = startFrame;
    fresh.loop = ownFile && played.loop.on && part != Hits && part != Vox;
    const auto region = makeLoopRegion (played.loop, audio->rate, audio->frames, startFrame);
    fresh.loopA = region.a;
    fresh.loopB = region.b;
    fresh.xfade = region.xfade;
    if (fresh.loopB > audio->frames - 1)
        fresh.loopB = (double) audio->frames - 1.0;
    if (choice.phraseLoop && choice.fileBpm > 0)
    {
        const float host = params.bpm >= 20.f ? params.bpm : 130.f;
        fresh.tempoSync = true;
        fresh.stretch = phraseTempoStretch (choice.fileBpm, host);
        fresh.ratio = audio->rate / sr;
        fresh.reverse = false;
        fresh.loop = true;
        fresh.loopA = 0.0;
        fresh.loopB = std::max (8.0, (double) audio->frames - 1.0);
        fresh.xfade = 0.0;
        // Vox phrases open on the first word, not on the file's lead-in silence.
        fresh.pos = part == Vox ? (double) audio->lead : 0.0;
        fresh.anchor = fresh.pos;
        fresh.spliceLeft = 0;
    }
    else if (! fromPack && ! played.reverse && chopPlanOn (chop))
    {
        fresh.pos = (double) chop.start;
        if (chop.hold)
        {
            fresh.loop = true;
            fresh.loopA = (double) chop.start;
            fresh.loopB = (double) std::max (chop.start + 1, chop.end - 1);
            fresh.xfade = (double) chop.fade;
        }
        else
        {
            fresh.loop = false;
            fresh.loopA = 0.0;
            fresh.loopB = 0.0;
            fresh.xfade = 0.0;
            fresh.chopStart = (double) chop.start;
            fresh.chopEnd = (double) chop.end;
            fresh.chopFade = chop.fade;
        }
    }

    armAttack (fresh);
    if (lateStart)
    {
        // Started after its file finished loading: a 12 ms raised-cosine fade-in,
        // so the late entry is not a click.
        fresh.attackN = std::max (8, (int) (0.012 * sr));
        fresh.attackLeft = fresh.attackN;
    }
    fresh.born = true;
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
    result.chordFile = chordFile && ! fresh.tempoSync;
    return result;
}

float Sampler::takeDrum (int voice, int velocity, const PresetBank& bank, const EngineParams& params,
                         float gainScale) noexcept
{
    if (voice < 0 || voice >= NumDrumVoices || velocity <= 0)
        return 1.f;
    // Choke group: the closed hat releases a ringing open hat over 5 ms.
    if (voice == HatC)
        chokeDrum (HatO);
    if (! params.sampleShaping || ! (gainScale > 0.f))
        gainScale = 1.f;
    Take taken;
    float keep = 1.f;
    drumGain = gainScale;
    const bool slotOwned = playUserSlot (Drums, voice, 60, velocity, 1.0e9f, bank, params, taken, keep);
    drumGain = 1.f;
    if (slotOwned)
        return keep;
    const int preset = presetOf (params, Drums);
    const auto& spec = sampleSpecFor (bank, Drums, preset);
    if (synthMode (spec))
        return 1.f;
    // No library: there is no pool to swap from, and the 909 is not a fallback.
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
    // A genre-drawn voice pick is an ordinal in this voice's scanned pool, so it only
    // applies there. An explicit voice pool and the pack fallback keep the shared pick.
    const int indexPick = params.drumPick[voice] >= 0 ? params.drumPick[voice] : pick + shift;
    const auto& own = drum.voicePool[voice];
    if (! own.empty())
        scanPool (own, pick + shift, true);
    else if (params.sampleIndex != nullptr)
    {
        LaneHit indexed;
        if (readyFromIndex (params.library, params.sampleIndex, Drums, spec, indexPick, voice, params.dialect, indexed))
            audio = indexed.audio;
        else if (indexed.pending)
            pending = true;
    }
    if (audio == nullptr && ! pending && own.empty())
    {
        const char* file = spec.voiceFile[voice][0] != 0 ? spec.voiceFile[voice] : spec.file;
        const SampleAudio* got = nullptr;
        const auto state = peekFile (params.library, file, got);
        if (state == FileState::Ready)
            audio = got;
        else if (state == FileState::Pending)
        {
            params.library->request (file);
            pending = true;
        }
    }
    // Key follow only trusts the kick's own file, not a hit borrowed from the pack.
    const bool ownFile = audio != nullptr;
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
        if (audio == nullptr && ! pending && params.sampleIndex != nullptr)
        {
            for (int alt : kPack)
            {
                if (alt == voice || audio != nullptr)
                    continue;
                LaneHit indexed;
                const int shifted = pick + (alt == TomMid ? 1 : alt == TomLow ? 2 : 0);
                if (readyFromIndex (params.library, params.sampleIndex, Drums, spec, shifted, alt, params.dialect, indexed))
                    audio = indexed.audio;
                else if (indexed.pending)
                {
                    pending = true;
                    break;
                }
            }
        }
    }
    // Still loading, or the pool has nothing ready: stay silent. Not the 909.
    if (audio == nullptr || audio->frames < 8)
        return 0.f;

    Voice* slot = allocate (Drums);
    if (slot == nullptr)
        return 0.f;

    const auto amp = ampFor (bank, Drums, presetOf (params, Drums));
    const float tune = std::pow (2.f, params.drumTuneSt[voice] / 12.f);
    // Multiplies the TUNE knob; exactly 1 when off, so the ratio is bit-identical.
    const double keyFollow = voice == Kick && params.kickKeyFollow && ownFile
                                 ? kickKeyFollowRatio (audio->pitch, params.key)
                                 : 1.0;
    Voice fresh;
    fresh.on = true;
    fresh.part = Drums;
    fresh.drumVoice = voice;
    fresh.age = serial++;
    fresh.audio = audio;
    notePicked (Drums, voice, audio);
    fresh.reverse = spec.reverse;
    fresh.ratio = std::clamp (tune * (audio->rate / sr) * keyFollow, 0.25, 4.0);
    fresh.end16 = 1.0e9f;
    fresh.fade = 0.f;
    fresh.fadeInc = 1.f / std::max (8.f, 0.002f * (float) sr);
    fresh.gain = amp.trim * std::clamp (velocity / 127.f, 0.05f, 1.f) * ampDb (params.drumLevelDb[voice]) * gainScale;
    if (spec.mode == SampleMode::Layer)
        fresh.gain *= std::clamp (spec.layerMix, 0.f, 1.f);
    fresh.res = amp.res;
    fresh.fEnv = 1.f;
    fresh.fCoef = expDecay (30.f, (float) sr);
    SampleFilter filter = sampleFilter (params, Drums, amp);
    filter.envOct = 0.f;
    applyFilter (fresh, filter);
    // Natural length at DECAY 0.5: the hit's own tail (the old 80 ms decay with
    // no sustain cut kick, clap and open-hat tails). Lower DECAY gates it shorter.
    {
        const auto pe = samplePunch (params, Drums);
        fresh.env.start (pe.aMs, pe.dMs, pe.s, 12.f, (float) sr);
    }
    fresh.pos = std::clamp ((double) spec.start, 0.0, 4.0) * audio->rate;
    fresh.gateLeft = drumGate (params.drumDecay[voice], *audio, fresh.pos, fresh.ratio);
    fresh.loop = spec.loop.on;
    const auto region = makeLoopRegion (spec.loop, audio->rate, audio->frames, fresh.pos);
    fresh.loopA = region.a;
    fresh.loopB = region.b;
    fresh.xfade = region.xfade;
    armAttack (fresh);
    fresh.born = true;
    *slot = fresh;

    return 0.f;
}

void Sampler::render (float* laneL, float* laneR, float prev16, float pos16, bool playing, float loopEnd16) noexcept
{
    if (laneL == nullptr || laneR == nullptr)
        return;
    for (int i = 0; i < NumParts; ++i)
        laneL[i] = laneR[i] = 0.f;
    for (int v = 0; v < NumDrumVoices; ++v)
        drumOutL[v] = drumOutR[v] = 0.f;

    // The loop-end edge's length comes from the playhead step. pos16 is a float,
    // so near 16th 128 its sample-to-sample step jitters by a few percent; a
    // window built from the raw step moved every voice's edge by that much each
    // sample, a click just before the wrap. A tempo change resets the estimate.
    const float rawStep = pos16 - prev16;
    if (playing && rawStep > 0.f)
    {
        if (stepEst <= 0.f || std::fabs (rawStep - stepEst) > 0.25f * stepEst)
            stepEst = rawStep;
        else
            stepEst += (1.f / 64.f) * (rawStep - stepEst);
    }

    auto renderOne = [&] (Voice& v)
    {
        if (! v.on)
            return;
        // The voice is born on the same sample the playhead wraps back through
        // the downbeat. That wrap is how step 0 is reached, not the end of the note.
        const bool born = v.born;
        v.born = false;
        const float prevEdge = v.noteEdge;
        v.noteEdge = 1.f;
        if (playing && loopEnd16 > 1.f)
        {
            const float stop = std::min (v.end16, loopEnd16);
            const float step = pos16 - prev16;
            if (step > 0.f)
            {
                const float est = stepEst > 0.f ? stepEst : step;
                const float window = std::max (est, est * (float) (0.010 * sr));
                const float remain = stop - pos16;
                if (remain <= 0.f)
                {
                    v.on = false;
                    return;
                }
                if (remain < window)
                    v.noteEdge = 0.5f * (1.f - std::cos (kSincPi * remain / window));
            }
            else if (! born && prev16 < stop && pos16 + 1.0e-3f < prev16)
            {
                // Loop wrap or a host relocate cut the note: fade it over 4 ms, never a hard cut.
                // A voice still in its attack keeps that edge (zeroing it stepped up to full).
                // One already fading (stolen, choked) keeps its own release.
                if (! v.stopping)
                {
                    v.releaseN = std::max (8, (int) (0.004 * sr));
                    v.releaseLeft = v.releaseN;
                    v.stopping = true;
                }
                // The loop-end edge already faded this voice. Release from that
                // level: snapping back to 1 stepped every tail still ringing on beat 1,
                // stolen voices parked inside the loop-end fade included.
                v.gain *= std::clamp (prevEdge, 0.f, 1.f);
            }
        }
        else if (playing && v.end16 < 1.0e8f)
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
    };
    for (auto& v : voices)
        renderOne (v);
    for (auto& v : tails)
        renderOne (v);
}

std::string Sampler::lastPickedFile (int part, int drumVoice) const
{
    const SampleAudio* a = nullptr;
    if (drumVoice >= 0 && drumVoice < NumDrumVoices)
        a = lastDrumAudio[drumVoice].load (std::memory_order_relaxed);
    else if (part >= 0 && part < NumParts)
        a = lastAudio[part].load (std::memory_order_relaxed);
    return a != nullptr ? a->path : std::string();
}

} // namespace rolla
