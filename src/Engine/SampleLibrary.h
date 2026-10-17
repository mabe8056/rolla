#pragma once

#include "SampleSpec.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace rolla
{

struct PresetBank;

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

    // Message thread. Queues every relative path named by the bank.
    void watch (const PresetBank& bank);

    // Audio thread. nullptr if the file is not ready.
    const SampleAudio* tryGet (const char* relative) const noexcept;
    // True only after a load attempt failed, or the path is empty. Pending loads are not missing.
    bool missing (const char* relative) const noexcept;
    bool ready (const char* relative) const noexcept;

private:
    struct Slot
    {
        char path[160] {};
        std::atomic<const SampleAudio*> audio { nullptr };
        std::atomic<int> status { 0 }; // 0 empty, 1 loading, 2 ready, 3 missing
    };

    void threadMain();
    void enqueue (const std::string& relative);
    void loadOne (const std::string& relative);
    Slot* slotFor (const char* relative) noexcept;
    const Slot* slotFor (const char* relative) const noexcept;

    mutable std::mutex rootMu;
    juce::File rootDir;
    std::mutex jobMu;
    std::condition_variable jobCv;
    std::vector<std::string> jobs;
    std::thread worker;
    std::atomic<bool> stop { false };

    static constexpr int kSlots = 384;
    Slot slots[kSlots];
    std::mutex slotMu;
    std::vector<std::unique_ptr<SampleAudio>> kept;
};

void ensureSampleFolders (const juce::File& root);

// Unique relative paths named by the loaded presets, including drum voice pools.
void collectReferencedSamples (const PresetBank& bank, std::vector<std::string>& out);

} // namespace rolla
