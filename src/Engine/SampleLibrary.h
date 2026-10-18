#pragma once

#include "SampleSpec.h"
#include "../Sampler/SamplePrep.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace rolla
{

struct PresetBank;

// One scanned file. The snapshot is immutable after it is published.
struct SampleEntry
{
    char path[256] {};
    int lane = -1;
    int voice = -1;
    int root = 60;
    int rootPc = -1;
    std::uint8_t styles = 0;
    char pack[64] {};
    char artist[64] {};
    char category[32] {};
    bool onDisk = false;
    // A second pool for files flagged usable in either lane (PLUCK/STAB, RIM/PERC).
    int altLane = -1;
    int altVoice = -1;
    bool phraseLoop = false;
    int fileBpm = 0;
};

// Directory listing joined with manifest metadata. Audio is not loaded here.
struct SampleIndex
{
    std::vector<SampleEntry> entries;
    int found = 0;
    int missing = 0;
};

// End index of a case-insensitive token, or -1. Search starts at `from`.
// Word tokens need a non-alphanumeric boundary on both sides, so "Effects"
// and a separated "_FX" match and a longer word that only contains the
// letters does not. A digit token such as "303" may be followed by a letter
// (303fx) and is rejected when a digit sits on either side (1303, 3031).
inline int sampleTokenEnd (const char* text, const char* token, int from) noexcept
{
    if (text == nullptr || token == nullptr || token[0] == 0 || from < 0)
        return -1;
    int n = 0;
    bool digits = true;
    for (; token[n] != 0; ++n)
    {
        const char c = token[n];
        if (c < '0' || c > '9')
            digits = false;
    }
    auto lower = [] (char c) noexcept
    {
        if (c >= 'A' && c <= 'Z')
            return (char) (c + 32);
        return c;
    };
    auto alnum = [] (char c) noexcept
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    };
    for (int i = from; text[i] != 0; ++i)
    {
        bool match = true;
        for (int k = 0; k < n; ++k)
        {
            if (text[i + k] == 0 || lower (text[i + k]) != lower (token[k]))
            {
                match = false;
                break;
            }
        }
        if (! match)
            continue;
        const char before = i > 0 ? text[i - 1] : 0;
        const char after = text[i + n];
        if (digits)
        {
            if (before >= '0' && before <= '9')
                continue;
            if (after >= '0' && after <= '9')
                continue;
            return i + n;
        }
        if (i > 0 && alnum (before))
            continue;
        if (after != 0 && alnum (after))
            continue;
        return i + n;
    }
    return -1;
}

inline bool sampleHasToken (const char* text, const char* token) noexcept
{
    return sampleTokenEnd (text, token, 0) >= 0;
}

// Tokens in order. A null later token ends the phrase.
inline bool samplePhrase (const char* text, const char* a, const char* b, const char* c) noexcept
{
    if (a == nullptr || a[0] == 0)
        return false;
    const int endA = sampleTokenEnd (text, a, 0);
    if (endA < 0)
        return false;
    if (b == nullptr || b[0] == 0)
        return true;
    const int endB = sampleTokenEnd (text, b, endA);
    if (endB < 0)
        return false;
    if (c == nullptr || c[0] == 0)
        return true;
    return sampleTokenEnd (text, c, endB) >= 0;
}

inline bool sampleTextEqual (const char* a, const char* b) noexcept;

// styles 0 fits every dialect. Otherwise bit 0 is UK MIN, then LONG, POP, GASKIN.
inline bool sampleStyleAllows (std::uint8_t styles, int dialect) noexcept
{
    if (styles == 0 || dialect < 0 || dialect >= NumDialects)
        return true;
    return (styles & (std::uint8_t) (1u << dialect)) != 0;
}

// Edit this table. lanes 0 means PAD, PLUCK, STAB and HITS.
// Reject rows drop a filename or folder that contains the token.
// "fx" plus the whole words effect / effects, so "Effects" and "_FX" match.
// HitsAllow rows are the HITS allow-list (tokens in order).
// HitsPitch is the word Hit plus a pitch or key already in the name.
inline constexpr int kRolePad = 1 << 0;
inline constexpr int kRolePluck = 1 << 1;
inline constexpr int kRoleStab = 1 << 2;
inline constexpr int kRoleHits = 1 << 3;

enum class RoleRule : int
{
    Reject = 0,
    HitsAllow = 1,
    HitsPitch = 2
};

struct RoleRow
{
    RoleRule rule;
    int lanes;
    const char* a;
    const char* b;
    const char* c;
};

inline constexpr RoleRow kRoleTable[] = {
    { RoleRule::Reject, 0, "fx", nullptr, nullptr },
    { RoleRule::Reject, 0, "effect", nullptr, nullptr },
    { RoleRule::Reject, 0, "effects", nullptr, nullptr },
    { RoleRule::Reject, 0, "sfx", nullptr, nullptr },
    { RoleRule::Reject, 0, "noise", nullptr, nullptr },
    { RoleRule::Reject, 0, "vinyl", nullptr, nullptr },
    { RoleRule::Reject, 0, "crackle", nullptr, nullptr },
    { RoleRule::Reject, 0, "texture", nullptr, nullptr },
    { RoleRule::Reject, 0, "background", nullptr, nullptr },
    { RoleRule::Reject, 0, "drone", nullptr, nullptr },
    { RoleRule::Reject, 0, "riser", nullptr, nullptr },
    { RoleRule::Reject, 0, "sweep", nullptr, nullptr },
    { RoleRule::Reject, 0, "impact", nullptr, nullptr },
    { RoleRule::Reject, 0, "lazer", nullptr, nullptr },
    { RoleRule::Reject, 0, "laser", nullptr, nullptr },
    { RoleRule::Reject, 0, "razor", nullptr, nullptr },
    { RoleRule::Reject, 0, "telephone", nullptr, nullptr },
    { RoleRule::Reject, 0, "whoosh", nullptr, nullptr },
    { RoleRule::Reject, 0, "downlifter", nullptr, nullptr },
    { RoleRule::Reject, 0, "uplifter", nullptr, nullptr },
    { RoleRule::Reject, 0, "downlift", nullptr, nullptr },
    { RoleRule::Reject, 0, "uplift", nullptr, nullptr },
    { RoleRule::Reject, 0, "atmos", nullptr, nullptr },
    { RoleRule::Reject, 0, "siren", nullptr, nullptr },
    { RoleRule::Reject, 0, "scratch", nullptr, nullptr },
    { RoleRule::Reject, 0, "glitch", nullptr, nullptr },
    { RoleRule::Reject, 0, "alarm", nullptr, nullptr },
    { RoleRule::Reject, kRoleHits, "acid", nullptr, nullptr },
    { RoleRule::Reject, kRoleHits, "303", nullptr, nullptr },
    { RoleRule::HitsAllow, kRoleHits, "chord", nullptr, nullptr },
    { RoleRule::HitsAllow, kRoleHits, "stab", nullptr, nullptr },
    { RoleRule::HitsAllow, kRoleHits, "stabber", nullptr, nullptr },
    { RoleRule::HitsAllow, kRoleHits, "rave", nullptr, nullptr },
    { RoleRule::HitsAllow, kRoleHits, "organ", nullptr, nullptr },
    { RoleRule::HitsAllow, kRoleHits, "piano", nullptr, nullptr },
    { RoleRule::HitsAllow, kRoleHits, "keys", nullptr, nullptr },
    { RoleRule::HitsAllow, kRoleHits, "synth", "hit", nullptr },
    { RoleRule::HitsAllow, kRoleHits, "tat", "synth", nullptr },
    { RoleRule::HitsAllow, kRoleHits, "ver", "synth", "hit" },
    { RoleRule::HitsPitch, kRoleHits, "hit", nullptr, nullptr },
};

inline int roleBit (int lane) noexcept
{
    switch (lane)
    {
        case Pad: return kRolePad;
        case Pluck: return kRolePluck;
        case Stab: return kRoleStab;
        case Hits: return kRoleHits;
        default: return 0;
    }
}

inline bool roleRowApplies (const RoleRow& row, int lane) noexcept
{
    const int bit = roleBit (lane);
    if (bit == 0)
        return false;
    return row.lanes == 0 || (row.lanes & bit) != 0;
}

inline bool sampleRoleRejected (const char* text, int lane) noexcept
{
    if (text == nullptr || text[0] == 0)
        return false;
    for (const auto& row : kRoleTable)
    {
        if (row.rule != RoleRule::Reject || ! roleRowApplies (row, lane))
            continue;
        if (samplePhrase (text, row.a, row.b, row.c))
            return true;
    }
    return false;
}

inline bool sampleHitsAllowed (const char* text) noexcept
{
    if (text == nullptr || text[0] == 0)
        return false;
    for (const auto& row : kRoleTable)
    {
        if (row.rule == RoleRule::HitsAllow && samplePhrase (text, row.a, row.b, row.c))
            return true;
        if (row.rule == RoleRule::HitsPitch && row.a != nullptr
            && sampleHasToken (text, row.a) && midiFromFilename (text) >= 0)
            return true;
    }
    return false;
}

// Role lanes reject noise, fx and vinyl on the whole relative path and on the
// category. HITS then keeps only the allow-list. Acid is out of HITS in every
// dialect. Style bits still apply only when a dialect is set.
inline bool samplePoolAllows (const SampleEntry& entry, int lane, int dialect) noexcept
{
    if (sampleRoleRejected (entry.path, lane) || sampleRoleRejected (entry.category, lane))
        return false;
    if (lane == Hits && ! sampleHitsAllowed (entry.path) && ! sampleHitsAllowed (entry.category))
        return false;
    if (dialect < 0 || dialect >= NumDialects)
        return true;
    return sampleStyleAllows (entry.styles, dialect);
}

// The filter the sampler and the offline renderer both use to draw one file.
inline PoolFilter poolFilterFor (const SampleSpec& spec, int part, int voice, int dialect) noexcept
{
    PoolFilter filter = spec.filter;
    filter.on = true;
    if (filter.lane < 0)
        filter.lane = part;
    if (voice >= 0)
        filter.voice = (voice == TomLow || voice == TomMid) ? Perc : voice;
    filter.dialect = dialect;
    return filter;
}

inline bool sampleTextEqual (const char* a, const char* b) noexcept
{
    if (a == nullptr || b == nullptr)
        return false;
    while (*a != 0 && *b != 0)
    {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z')
            ca = (char) (ca + 32);
        if (cb >= 'A' && cb <= 'Z')
            cb = (char) (cb + 32);
        if (ca != cb)
            return false;
        ++a;
        ++b;
    }
    return *a == 0 && *b == 0;
}

inline bool sampleEntryShape (const SampleEntry& entry, int part, const PoolFilter& filter, bool useCategory) noexcept
{
    if (! entry.onDisk || entry.path[0] == 0)
        return false;
    const int lane = filter.lane >= 0 ? filter.lane : part;
    const bool primary = entry.lane == lane && (filter.voice < 0 || entry.voice == filter.voice);
    const bool alternate = entry.altLane == lane && (filter.voice < 0 || entry.altVoice == filter.voice);
    if (! primary && ! alternate)
        return false;
    if (filter.loop == 1 && ! entry.phraseLoop)
        return false;
    if (filter.loop == 0 && entry.phraseLoop)
        return false;
    if (filter.rootPc >= 0 && entry.rootPc != filter.rootPc)
        return false;
    if (filter.pack[0] != 0 && ! sampleTextEqual (entry.pack, filter.pack))
        return false;
    if (useCategory && filter.category[0] != 0 && ! sampleTextEqual (entry.category, filter.category))
        return false;
    if (filter.dialect >= 0 && filter.dialect < NumDialects && ! sampleStyleAllows (entry.styles, filter.dialect))
        return false;
    return true;
}

inline bool sampleEntryMatches (const SampleEntry& entry, int part, const PoolFilter& filter) noexcept
{
    const int lane = filter.lane >= 0 ? filter.lane : part;
    if (! sampleEntryShape (entry, part, filter, true))
        return false;
    return samplePoolAllows (entry, lane, filter.dialect);
}

// Same lane, after the strict list came up empty. Category is ignored.
// Noise, fx, vinyl and acid stay out. HITS then accepts a pitched name that
// missed the allow-list. PAD, PLUCK and STAB accept whatever the reject list left.
inline bool sampleEntryFallback (const SampleEntry& entry, int part, const PoolFilter& filter) noexcept
{
    const int lane = filter.lane >= 0 ? filter.lane : part;
    if (roleBit (lane) == 0)
        return false;
    if (! sampleEntryShape (entry, part, filter, false))
        return false;
    if (sampleRoleRejected (entry.path, lane) || sampleRoleRejected (entry.category, lane))
        return false;
    if (lane == Hits)
        return sampleHitsAllowed (entry.path) || sampleHitsAllowed (entry.category)
            || midiFromFilename (entry.path) >= 0;
    return true;
}

inline int sampleIndexCount (const SampleIndex& index, int part, const PoolFilter& filter) noexcept
{
    int strict = 0;
    for (const auto& entry : index.entries)
        if (sampleEntryMatches (entry, part, filter))
            ++strict;
    if (strict > 0 || roleBit (filter.lane >= 0 ? filter.lane : part) == 0)
        return strict;
    int loose = 0;
    for (const auto& entry : index.entries)
        if (sampleEntryFallback (entry, part, filter))
            ++loose;
    return loose;
}

inline const SampleEntry* sampleIndexAt (const SampleIndex& index, int part, const PoolFilter& filter, int ordinal) noexcept
{
    if (ordinal < 0)
        return nullptr;
    const int lane = filter.lane >= 0 ? filter.lane : part;
    int strict = 0;
    for (const auto& entry : index.entries)
        if (sampleEntryMatches (entry, part, filter))
            ++strict;
    const bool fallback = strict == 0 && roleBit (lane) != 0;
    int seen = 0;
    for (const auto& entry : index.entries)
    {
        const bool keep = fallback ? sampleEntryFallback (entry, part, filter)
                                   : sampleEntryMatches (entry, part, filter);
        if (! keep)
            continue;
        if (seen == ordinal)
            return &entry;
        ++seen;
    }
    return nullptr;
}

// The recipe's own filter when the user's library has files for it; otherwise
// the same lane, widened one step at a time (any loop, then any category,
// then any pack or root) until something matches. A factory recipe whose
// category the library lacks (e.g. the vox "loops" recipe without tempo-locked
// loops on disk) still plays a sample from its own lane instead of going
// silent. There is no synth fallback either way. Sampler, preload and the
// renderer all draw through this, so they agree on the file.
inline PoolFilter poolFilterWithFiles (const SampleIndex& index, const SampleSpec& spec, int part, int voice,
                                       int dialect) noexcept
{
    PoolFilter f = poolFilterFor (spec, part, voice, dialect);
    if (sampleIndexCount (index, part, f) > 0)
        return f;
    f.loop = -1;
    if (sampleIndexCount (index, part, f) > 0)
        return f;
    f.category[0] = 0;
    if (sampleIndexCount (index, part, f) > 0)
        return f;
    f.pack[0] = 0;
    f.rootPc = -1;
    return f;
}

// One roll's draw from a lane pool. mute is silence: the lane does not
// fall through to a synth, and it does not borrow another lane.
struct LanePick
{
    const SampleEntry* entry = nullptr;
    bool mute = false;
};

inline LanePick pickLaneSample (const SampleIndex& index, int part, const PoolFilter& filter, int ordinal) noexcept
{
    LanePick pick;
    const int n = sampleIndexCount (index, part, filter);
    if (n <= 0)
    {
        pick.mute = true;
        return pick;
    }
    if (ordinal < 0)
        ordinal = 0;
    pick.entry = sampleIndexAt (index, part, filter, ordinal % n);
    pick.mute = pick.entry == nullptr;
    return pick;
}

// Immutable audio published to the realtime thread. Never freed while the library is alive.
struct SampleAudio
{
    int frames = 0;
    double rate = 48000.0;
    std::vector<float> left;
    std::vector<float> right;
    int sliceCount = 1;
    int slices[24] { 0 };
    // First audible frame (2 ms early). Vox phrases often open on silence.
    int lead = 0;
};

// Loads preset-referenced files from the user sample folder on a background thread.
// The audio thread only reads atomics. Samples are never embedded in the plugin.
class SampleLibrary
{
public:
    SampleLibrary();
    ~SampleLibrary();

    SampleLibrary (const SampleLibrary&) = delete;
    SampleLibrary& operator= (const SampleLibrary&) = delete;

    static juce::File defaultRoot();
    void setRoot (const juce::File& root);
    juce::File root() const;

    // Message thread. Queues the relative paths a bank names explicitly.
    // Factory playback does not use this to preload a lane; the index does that lazily.
    void watch (const PresetBank& bank);

    // Message thread. Loads these files next, ahead of the background watch list.
    // ROLL and preset changes call this so the lanes are ready before the downbeat.
    void preload (const std::vector<std::string>& relativePaths);

    // Message thread. Scans <root>/<LANE>/(<VOICE>/) on the worker and joins manifest metadata.
    // A SAMPLE_MANIFEST_v2.json or SAMPLE_MANIFEST.json in the root overrides this text.
    void startIndex (const char* manifestJson, int size);

    // Copy of the published snapshot. Empty until the scan finishes. Safe on the audio thread once per block.
    std::shared_ptr<const SampleIndex> index() const;

    // -1 until a scan has been published.
    int indexedFound() const noexcept { return indexedFoundCount.load (std::memory_order_acquire); }
    int indexedMissing() const noexcept { return indexedMissingCount.load (std::memory_order_acquire); }

    // Audio thread. Asks the worker to load one file. Drops the request when the ring is full.
    void request (const char* relative) const noexcept;

    // Audio thread. nullptr if the file is not ready.
    const SampleAudio* tryGet (const char* relative) const noexcept;
    // True only after a load attempt failed, or the path is empty. Pending loads are not missing.
    bool missing (const char* relative) const noexcept;
    bool ready (const char* relative) const noexcept;

    // Any thread. True when no request or job is queued, no index scan is due,
    // and the worker is not loading. Offline export waits on this so a lane
    // whose file was requested during a pre-roll is loaded before the bounce.
    bool idle() const;
    // How many times playback asked for a file that was not loaded yet,
    // duplicates and full-ring drops included. An offline bounce that saw this
    // move (from any engine) may have played a note without its sample.
    std::uint64_t notReadyRequests() const noexcept { return notReady.load (std::memory_order_acquire); }

private:
    struct Slot
    {
        char path[256] {};
        std::atomic<const SampleAudio*> audio { nullptr };
        std::atomic<int> status { 0 }; // 0 empty, 1 loading, 2 ready, 3 missing
    };

    void threadMain();
    void enqueue (const std::string& relative);
    void loadOne (const std::string& relative);
    void buildIndex();
    bool cachedGain (const std::string& relative, juce::int64 bytes, float& gainDb);
    void rememberGain (const std::string& relative, juce::int64 bytes, float gainDb);
    Slot* slotFor (const char* relative) noexcept;
    Slot* claimSlot (const char* relative) noexcept;
    const Slot* slotFor (const char* relative) const noexcept;

    mutable std::mutex rootMu;
    juce::File rootDir;
    mutable std::mutex jobMu;
    mutable std::condition_variable jobCv;
    std::vector<std::string> jobs;
    std::thread worker;
    std::atomic<bool> stop { false };
    std::atomic<bool> indexKick { false };
    std::atomic<std::uint32_t> indexGen { 0 };

    std::mutex manifestMu;
    std::string embeddedManifest;

    std::mutex gainMu;
    bool gainsLoaded = false;
    struct GainRow
    {
        juce::int64 bytes = 0;
        float gainDb = 0.f;
    };
    std::map<std::string, GainRow> gains;

    mutable std::mutex indexMu;
    std::shared_ptr<const SampleIndex> indexSnap;
    std::atomic<int> indexedFoundCount { -1 };
    std::atomic<int> indexedMissingCount { -1 };

    static constexpr int kReq = 512;
    mutable char reqRing[kReq][256] {};
    mutable std::atomic<int> reqHead { 0 };
    mutable std::atomic<int> reqTail { 0 };
    mutable std::atomic<std::uint64_t> notReady { 0 };
    std::atomic<int> working { 0 };

    // Power of two. Larger than the 2,697-file manifest so a session of rolls never runs out.
    static constexpr int kSlots = 8192;
    std::unique_ptr<Slot[]> slots { new Slot[kSlots] };
    std::atomic<bool> tableFullLogged { false };
    std::mutex slotMu;
    std::vector<std::unique_ptr<SampleAudio>> kept;
};

struct InstalledPack
{
    std::unique_ptr<SampleLibrary> library;
    std::shared_ptr<const SampleIndex> index;
};

// Scans the default sample root when it already holds audio. Empty when this machine has no pack.
InstalledPack openInstalledPack (const char* manifestJson, int manifestSize);

void ensureSampleFolders (const juce::File& root);

// The files the sampler will reach for first with these presets and picks:
// each lane's preferred pool entry and its next neighbours (window), and every drum voice.
void collectPlayFiles (const PresetBank& bank, const SampleIndex& index, const int preset[NumParts],
                       const int samplePick[NumParts], int dialect, int window, std::vector<std::string>& out);

// Per-user cache files (kind "index" or "loudness"). Never inside the sample root.
juce::File sampleCacheFor (const juce::File& sampleRoot, const char* kind);
juce::File loudnessCacheFor (const juce::File& sampleRoot);

// Unique relative paths named by the loaded presets, including drum voice pools.
void collectReferencedSamples (const PresetBank& bank, std::vector<std::string>& out);

} // namespace rolla
