#pragma once

#include "PlayDecision.h"
#include "../Engine/SampleLibrary.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

namespace rolla
{

// Immutable snapshot the audio thread reads. Never mutated after publish.
struct UserSlotPod
{
    std::int8_t mode = 0;   // 0 off, 1 sample, 2 layer
    std::int8_t status = 0; // 0 idle, 1 loading, 2 ready, 3 missing
    std::int8_t reverse = 0;
    std::int8_t loopOn = 0;
    int root = 60;
    float cents = 0.f;
    float start = 0.f;
    float loopStart = 0.f;
    float loopEnd = 0.f;
    float loopXfade = 0.f;
    float layerMix = 0.5f;
    float gainDb = 0.f;
    float prepGainDb = 0.f;
    std::int8_t prepCapped = 0;
    char file[160] {};
    const SampleAudio* audio = nullptr;
};

struct UserSlotView
{
    UserSlotPod slots[kUserSlotCount];
};

// Fourteen session slots. Paths persist in the host chunk. Audio does not.
// Loading, prep and root detection run on a background thread. The audio
// thread only loads the published view pointer.
class UserSlots
{
public:
    UserSlots();
    ~UserSlots();

    UserSlots (const UserSlots&) = delete;
    UserSlots& operator= (const UserSlots&) = delete;

    void setRoot (const juce::File& root);
    juce::File root() const;

    // Message thread. Copies an outside file into Samples/USER/... (the source
    // is not modified). A file already under the root is referenced in place.
    bool assign (int slot, const juce::File& file, juce::String& error);
    void clear (int slot);
    void setMode (int slot, int mode);
    void setRootNote (int slot, int midiNote, float cents);
    void setStart (int slot, float seconds);
    void setReverse (int slot, bool reverse);
    void setLoop (int slot, bool on, float start, float end, float xfade);
    void setLayerMix (int slot, float mix);
    void setGainDb (int slot, float db);

    int mode (int slot) const noexcept;
    bool warning (int slot) const noexcept;
    int status (int slot) const noexcept;
    juce::String file (int slot) const;
    int rootNote (int slot) const;
    float prepGainDb (int slot) const;
    bool prepCapped (int slot) const;
    juce::String lastError (int slot) const;

    // Audio thread.
    const UserSlotView* published() const noexcept;

    void writeState (juce::ValueTree& parent) const;
    void readState (const juce::ValueTree& parent);

    std::uint64_t residentBytes() const noexcept;

private:
    struct Edit
    {
        int mode = 0;
        int status = 0;
        std::string file;
        int root = 60;
        float cents = 0.f;
        bool rootEdited = false;
        float start = 0.f;
        bool reverse = false;
        bool loopOn = false;
        float loopStart = 0.f;
        float loopEnd = 0.f;
        float loopXfade = 0.f;
        float layerMix = 0.5f;
        float gainDb = 0.f;
        float prepGainDb = 0.f;
        bool prepCapped = false;
        std::string error;
        const SampleAudio* audio = nullptr;
        std::uint64_t bytes = 0;
    };

    void threadMain();
    void enqueue (int slot);
    void loadSlot (int slot);
    void publish();
    void ensureUserFolders() const;
    bool validSlot (int slot) const noexcept;
    static bool safeRelative (const std::string& path) noexcept;

    mutable std::mutex editMu;
    Edit edit[kUserSlotCount];
    std::atomic<int> modeAtomic[kUserSlotCount] {};
    std::atomic<int> warnAtomic[kUserSlotCount] {};
    std::atomic<int> statusAtomic[kUserSlotCount] {};
    std::atomic<std::uint64_t> resident { 0 };

    std::atomic<const UserSlotView*> view { nullptr };
    std::vector<std::unique_ptr<UserSlotView>> keptViews;
    std::vector<std::unique_ptr<SampleAudio>> keptAudio;

    mutable std::mutex rootMu;
    juce::File rootDir;

    std::mutex jobMu;
    std::condition_variable jobCv;
    std::vector<int> jobs;
    std::thread worker;
    std::atomic<bool> stop { false };
};

} // namespace rolla
