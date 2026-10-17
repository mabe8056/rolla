#pragma once

#include "SampleSpec.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
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
};

// Directory listing joined with manifest metadata. Audio is not loaded here.
struct SampleIndex
{
    std::vector<SampleEntry> entries;
    int found = 0;
    int missing = 0;
};

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

inline bool sampleEntryMatches (const SampleEntry& entry, int part, const PoolFilter& filter) noexcept
{
    if (! entry.onDisk || entry.path[0] == 0)
        return false;
    const int lane = filter.lane >= 0 ? filter.lane : part;
    if (entry.lane != lane)
        return false;
    if (filter.voice >= 0 && entry.voice != filter.voice)
        return false;
    if (filter.rootPc >= 0 && entry.rootPc != filter.rootPc)
        return false;
    if (filter.pack[0] != 0 && ! sampleTextEqual (entry.pack, filter.pack))
        return false;
    if (filter.category[0] != 0 && ! sampleTextEqual (entry.category, filter.category))
        return false;
    return true;
}

inline int sampleIndexCount (const SampleIndex& index, int part, const PoolFilter& filter) noexcept
{
    int n = 0;
    for (const auto& entry : index.entries)
        if (sampleEntryMatches (entry, part, filter))
            ++n;
    return n;
}

inline const SampleEntry* sampleIndexAt (const SampleIndex& index, int part, const PoolFilter& filter, int ordinal) noexcept
{
    if (ordinal < 0)
        return nullptr;
    int seen = 0;
    for (const auto& entry : index.entries)
    {
        if (! sampleEntryMatches (entry, part, filter))
            continue;
        if (seen == ordinal)
            return &entry;
        ++seen;
    }
    return nullptr;
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
    Slot* slotFor (const char* relative) noexcept;
    const Slot* slotFor (const char* relative) const noexcept;

    mutable std::mutex rootMu;
    juce::File rootDir;
    std::mutex jobMu;
    mutable std::condition_variable jobCv;
    std::vector<std::string> jobs;
    std::thread worker;
    std::atomic<bool> stop { false };
    std::atomic<bool> indexKick { false };
    std::atomic<std::uint32_t> indexGen { 0 };

    std::mutex manifestMu;
    std::string embeddedManifest;

    mutable std::mutex indexMu;
    std::shared_ptr<const SampleIndex> indexSnap;
    std::atomic<int> indexedFoundCount { -1 };
    std::atomic<int> indexedMissingCount { -1 };

    static constexpr int kReq = 64;
    mutable char reqRing[kReq][256] {};
    mutable std::atomic<int> reqHead { 0 };
    mutable std::atomic<int> reqTail { 0 };

    static constexpr int kSlots = 384;
    Slot slots[kSlots];
    std::mutex slotMu;
    std::vector<std::unique_ptr<SampleAudio>> kept;
};

void ensureSampleFolders (const juce::File& root);

// Unique relative paths named by the loaded presets, including drum voice pools.
void collectReferencedSamples (const PresetBank& bank, std::vector<std::string>& out);

} // namespace rolla
