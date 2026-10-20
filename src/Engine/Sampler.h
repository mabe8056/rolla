#pragma once

#include "../Sampler/BassMatch.h"
#include "SampleLibrary.h"
#include <atomic>
#include <cstring>
#include <string>
#include "../DspUtil.h"
#include "../Generator/Presets.h"
#include "../GenreBrain/Treatment.h"

namespace rolla
{

struct EngineParams;

// Per-lane adaptive treatments (GenreBrain/Treatment.h), decided at roll time for
// the drawn samples and published through EngineParams::treatments. Null there (the
// default) leaves every voice exactly as before. User slots are never treated: the
// user's own sample keeps the user's settings.
//
// A slot with a file only treats that file (library-relative path, as in
// SampleAudio::path); a pool swap to another file plays it untreated. An empty
// file treats whatever the lane plays.
//
// Publish like UserSlotView: build a set off the audio thread, then swap the
// pointer. The audio thread only reads it.
struct TreatmentSet
{
    struct Slot
    {
        bool on = false;
        genre::Treatment treatment;
        char file[96] {};
    };

    Slot part[NumParts] {};       // melodic lanes; part[Drums] is unused (see drum)
    Slot drum[NumDrumVoices] {};  // drum voices (takeDrum)

    void set (int lane, const genre::Treatment& t, const char* file = nullptr) noexcept
    {
        if (lane >= 0 && lane < NumParts && lane != Drums)
            fill (part[lane], t, file);
    }

    void setDrum (int voice, const genre::Treatment& t, const char* file = nullptr) noexcept
    {
        if (voice >= 0 && voice < NumDrumVoices)
            fill (drum[voice], t, file);
    }

    void clear() noexcept
    {
        for (auto& s : part)
            s = {};
        for (auto& s : drum)
            s = {};
    }

    // The treatment for a voice about to start, or null. drumVoice >= 0 picks a drum slot.
    const genre::Treatment* find (int lane, int drumVoice, const char* path) const noexcept
    {
        const Slot* s = nullptr;
        if (drumVoice >= 0)
            s = drumVoice < NumDrumVoices ? &drum[drumVoice] : nullptr;
        else if (lane >= 0 && lane < NumParts && lane != Drums)
            s = &part[lane];
        if (s == nullptr || ! s->on)
            return nullptr;
        if (s->file[0] != 0 && (path == nullptr || std::strcmp (s->file, path) != 0))
            return nullptr;
        return &s->treatment;
    }

private:
    static void fill (Slot& s, const genre::Treatment& t, const char* file) noexcept
    {
        s.on = true;
        s.treatment = t;
        copySamplePath (s.file, (int) sizeof s.file, file);
    }
};

// Where a genre lane plays in the engine: a melodic part, or a drum voice of the
// Drums part. False for lanes with no engine slot (FX).
inline bool engineSlotFor (genre::Lane lane, int& part, int& drumVoice) noexcept
{
    part = -1;
    drumVoice = -1;
    switch (lane)
    {
        case genre::Lane::Kick:  drumVoice = Kick; break;
        case genre::Lane::Clap:  drumVoice = Clap; break;
        case genre::Lane::Snare: drumVoice = Snare; break;
        case genre::Lane::HatC:  drumVoice = HatC; break;
        case genre::Lane::HatO:  drumVoice = HatO; break;
        case genre::Lane::Perc:  drumVoice = Perc; break;
        case genre::Lane::Ride:  drumVoice = Ride; break;
        case genre::Lane::Bass:  part = Bass; break;
        case genre::Lane::Stab:  part = Stab; break;
        case genre::Lane::Pluck: part = Pluck; break;
        case genre::Lane::Pad:   part = Pad; break;
        case genre::Lane::Hits:  part = Hits; break;
        case genre::Lane::Vox:   part = Vox; break;
        default: return false;
    }
    if (drumVoice >= 0)
        part = Drums;
    return true;
}

// Realtime sample voices. take() / takeDrum() run on the audio thread and only
// read a published SampleAudio. Rendering uses a 16-tap windowed sinc.
class Sampler
{
public:
    struct Take
    {
        int kind = 0;       // 0 synth only, 1 sample replaces synth, 2 layer
        float synthGain = 1.f;
        // The sample is a recorded chord: it already sounds the other chord
        // tones, so the caller plays it once per chord, not once per note.
        bool chordFile = false;
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // True when the file opens on a step: its first sample sits well above how
    // far the signal moves per sample over the next millisecond (a kick trimmed
    // onto its peak). Only those get the 0.5 ms file-start fade; a file that
    // opens into noise or rises from zero plays its first samples as recorded.
    static bool opensOnStep (const SampleAudio& audio) noexcept;

    // ask: the chord this note opens (empty for a single note). A chord file plays
    // from that chord's root, and moves to a root where its quality fits.
    // anchorNote >= 0 (a chord's centre, sample-true): every tone of the chord takes the
    // octave fold of the anchor, so the voicing sounds as written instead of each tone
    // folding on its own into a cluster.
    // BASSMATCH: the bass line being played (one octave choice per line), set by the Engine each block.
    void setBassLine (const bassmatch::Line& line, int base) noexcept
    {
        bassLine = line;
        bassLineBase = base;
    }

    Take take (int part, int midiNote, int velocity, float end16,
               const PresetBank& bank, const EngineParams& params,
               ChordAsk ask = {}, int anchorNote = -1) noexcept;

    // Returns the gain the synthesized drum voice should keep (0 = sample replaced it).
    // gainScale: per-hit accent (DRUMS ACCENT on the downbeat, the voice's ACCENT).
    float takeDrum (int voice, int velocity, const PresetBank& bank, const EngineParams& params,
                    float gainScale = 1.f) noexcept;
    void releaseNote (int part, int midiNote) noexcept;

    // Audio thread, once per sample before render(). A note whose file was still
    // loading when it was triggered is held here (fixed slots, no allocation) and
    // starts, with a short fade-in, as soon as its file is ready inside the note's
    // length. It is dropped at the note end, a loop wrap, a release or a stop.
    void retryHeld (const PresetBank& bank, const EngineParams& params,
                    float prev16, float pos16, bool playing) noexcept;
    // Notes waiting on a loading file. Tests and status.
    int heldNotes() const noexcept { return heldCount; }

    void releaseAll() noexcept;
    // loopEnd16 > 0 fades every voice out at the phrase end so nothing runs into the next bar.
    void render (float* laneL, float* laneR, float prev16, float pos16, bool playing, float loopEnd16 = 0.f) noexcept;

    // The file the last note on a lane (or drum voice) started, library-relative;
    // empty if nothing played. Read from the message thread by tests and status.
    std::string lastPickedFile (int part, int drumVoice = -1) const;

    // Filled by render(). Melodic playback stays on the lane buses; drum hits are also split per voice.
    float drumOutL[NumDrumVoices] {};
    float drumOutR[NumDrumVoices] {};

private:
    std::atomic<const SampleAudio*> lastAudio[NumParts] {};
    std::atomic<const SampleAudio*> lastDrumAudio[NumDrumVoices] {};
    void notePicked (int part, int drumVoice, const SampleAudio* audio) noexcept
    {
        if (audio == nullptr)
            return;
        if (drumVoice >= 0 && drumVoice < NumDrumVoices)
            lastDrumAudio[drumVoice].store (audio, std::memory_order_relaxed);
        else if (part >= 0 && part < NumParts)
            lastAudio[part].store (audio, std::memory_order_relaxed);
    }

    struct Voice
    {
        bool on = false;
        // Set for the sample that starts the voice. The transport rewind that
        // opens a downbeat looks like a loop wrap, and that wrap must not kill
        // a voice that has not sounded yet.
        bool born = false;
        int part = 0;
        int drumVoice = -1;
        int midiNote = 60;
        int age = 0;
        const SampleAudio* audio = nullptr;
        bool reverse = false;
        double pos = 0.0;
        double ratio = 1.0;
        double loopA = 0.0;
        double loopB = 0.0;
        double xfade = 0.0;
        // In-memory slice of a longer file. chopEnd 0 plays the whole buffer.
        // The published SampleAudio is never edited.
        double chopStart = 0.0;
        double chopEnd = 0.0;
        int chopFade = 0;
        bool loop = false;
        // Phrase loops keep their pitch and follow the host tempo.
        bool tempoSync = false;
        float stretch = 1.f;
        double anchor = 0.0;
        double splicePos = 0.0;
        int spliceLeft = 0;
        float fade = 1.f;
        float fadeInc = 0.f;
        bool stopping = false;
        // Raised-cosine edges. A linear step at a sample boundary is a click.
        int attackLeft = 0;
        int attackN = 0;
        int releaseLeft = 0;
        int releaseN = 0;
        float noteEdge = 1.f;
        // Treatment decay trim: output samples until the trim fade starts (0 = no
        // trim), then a raised-cosine release of trimFadeN samples.
        int trimLeft = 0;
        int trimFadeN = 0;
        float gain = 0.2f;
        float pan = 0.f;
        float end16 = 1.0e9f;
        // BASSMATCH: the file's last 10 ms fade out instead of stopping on a loud sample.
        bool eofFade = false;
        float cut = 8000.f;
        float res = 0.05f;
        float envOct = 0.f;
        float fEnv = 1.f;
        float fCoef = 0.99f;
        // Sample voices run unfiltered unless TONE is off neutral (sampleFilter).
        // When they are filtered, L and R take the same path.
        bool lowpass = false;
        float hpfHz = 0.f;
        float hpState = 0.f;
        float hpStateR = 0.f;
        Ladder ladder;
        Ladder ladderR;
        // Output samples until a drum hit releases (DECAY below 0.5); -1 plays the whole file.
        int gateLeft = -1;
        struct Env
        {
            int stage = 0;
            float amp = 0, sustain = 0, attInc = 0, decC = 0, relC = 0;
            void start (float aMs, float dMs, float sGain, float rMs, float sr) noexcept;
            void release() noexcept;
            bool tick() noexcept;
        } env;
    };

    struct Held
    {
        bool on = false;
        // Set for the sample it was held on: that sample may also be the
        // downbeat rewind, which is not a loop wrap for this note.
        bool born = false;
        int part = 0;
        int midiNote = 60;
        int velocity = 0;
        float end16 = 0.f;
        ChordAsk ask {};
        int anchor = -1;
        int wait = 0;
        int sweep = 0;
        char file[256] {};
    };
    static constexpr int kHeld = 16;
    bassmatch::Line bassLine {};
    int bassLineBase = 26;
    Held held[kHeld];
    int heldCount = 0;
    // Set by start() when the note was dropped only because a file is still loading.
    bool pendingNote = false;
    const char* pendingFile = nullptr;
    // Set while retryHeld() starts a held note: the voice fades in.
    bool lateStart = false;
    Take startNote (int part, int midiNote, int velocity, float end16,
                const PresetBank& bank, const EngineParams& params, ChordAsk ask, int anchorNote = -1) noexcept;
    void holdNote (int part, int midiNote, int velocity, float end16, ChordAsk ask, const char* file, int anchorNote = -1) noexcept;
    void dropHeld (Held& h) noexcept;

    // Drum hits steal only drum voices, melodic notes only melodic voices.
    Voice* allocate (int part) noexcept;
    void park (const Voice& dying) noexcept;
    // What a voice still puts out: env, release edge and gain. A voice born this
    // sample (env still 0) is never the quietest: stealing it chopped the new kick.
    static float audibleLevel (const Voice& v) noexcept;
    // Closed hat chokes the open hat: a 5 ms raised-cosine release, never a hard cut.
    void chokeDrum (int drumVoice) noexcept;
    void armAttack (Voice& voice) noexcept;
    // Loop region and decay trim from a Treatment; call after armAttack.
    void applyTreatment (Voice& voice, const genre::Treatment& t) const noexcept;
    float readSinc (const std::vector<float>& src, double index) const noexcept;
    void readVoice (Voice& v, float& l, float& r) noexcept;
    // True when a user slot owns this note (playing, or still loading).
    bool playUserSlot (int part, int drumVoice, int midiNote, int velocity, float end16,
                       const PresetBank& bank, const EngineParams& params,
                       Take& take, float& drumKeep) noexcept;

    double sr = 48000.0;
    // takeDrum's gainScale, seen by playUserSlot for a drum voice. Audio thread only.
    float drumGain = 1.f;
    int serial = 1;
    // The playhead's 16ths per sample, smoothed (render). Audio thread only.
    float stepEst = 0.f;
    int sliceCursor[NumParts] {};
    // Two pools. With one shared pool a full drum kit stole bass notes (a bass
    // voice's gain ranks below a drum tail's), and in a stem bounce the muted
    // drums still played into the pool and cut the soloed bass.
    static constexpr int kDrumVoices = 40;
    static constexpr int kMelodicVoices = 32;
    Voice voices[kDrumVoices + kMelodicVoices];
    // Stolen voices finish their release here. Evicting a tail is a cut, so keep plenty.
    Voice tails[32];
};

const SampleSpec& sampleSpecFor (const PresetBank& bank, int part, int preset) noexcept;

// Kick key follow (PRESET_SPEC 9.2). Pitch confidence a kick needs before it is retuned:
// lower than kPitchConfMin, since a kick's tail is a decaying sine with a chirp on top.
inline constexpr float kKickFollowConfMin = 0.3f;
inline constexpr double kKickFollowMaxSt = 3.0;

// Playback ratio that moves a sampled kick's measured fundamental (tail f0, folded
// into 40-80 Hz) toward DrumMachine::kickHzForKey (key), within +-3 st so the body
// keeps its character. 1 for an unpitched or low-confidence file.
double kickKeyFollowRatio (const PitchInfo& pitch, int key) noexcept;

} // namespace rolla
