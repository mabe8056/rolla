#pragma once

#include "Engine/Engine.h"
#include "Engine/SampleLibrary.h"
#include "Mix/MixProgram.h"
#include "Sampler/UserSlots.h"
#include "Sampler/RetuneGuard.h"
#include "Generator/Generator.h"
#include "Generator/Pattern.h"
#include "Generator/Presets.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace rolla
{

inline bool anyLaneSoloed (const bool (&solo)[NumParts]) noexcept
{
    for (bool s : solo)
        if (s)
            return true;
    return false;
}

// Solo is playback-only. With any lane soloed, every other lane plays as muted;
// a soloed lane the user also muted stays muted. With no solo this changes nothing.
inline void foldSolo (bool (&mute)[NumParts], const bool (&solo)[NumParts]) noexcept
{
    if (! anyLaneSoloed (solo))
        return;
    for (int i = 0; i < NumParts; ++i)
        mute[i] = mute[i] || ! solo[i];
}

class RollaProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener
{
public:
    RollaProcessor();
    ~RollaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ROLLA"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Groove"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Snapshot. A PresetBank is about a megabyte; copying it by value overflows the
    // 512 KB macOS thread pluginval uses to open the plugin.
    std::shared_ptr<const PresetBank> sharePresets() const;
    PresetBank copyPresets() const;
    void modifyPresets (const std::function<void (PresetBank&)>& edit);
    std::uint32_t presetGeneration() const noexcept { return presetGen.load (std::memory_order_acquire); }

    // Blocks until preset JSON, the first pattern, and the sample-folder setup have finished.
    void waitUntilReady();
    double startupMillis() const noexcept { return startupMs.load (std::memory_order_relaxed); }

private:
    mutable std::mutex presetMu;
    std::shared_ptr<PresetBank> presets;
    std::atomic<std::uint32_t> presetGen { 0 };

public:
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    juce::UndoManager& getUndo() noexcept { return undo; }

    void markEdited (int part);
    bool laneEdited (int part) const noexcept;
    void clearEdited (int part) noexcept;
    // Shuffle drops the keep that a manual edit turned on. A lock the user set stays.
    void clearAutoKeep (int part);

    bool hasVoxSample() const noexcept { return voxSampleName.isNotEmpty(); }
    juce::String getVoxSampleName() const { return voxSampleName; }
    bool loadVoxFile (const juce::File& file, juce::String& error);
    void clearVoxSample();

    juce::String exportMidiTo (const juce::File& dir);

    Groove copyGroove() const;
    bool isPreviewOn() const noexcept { return preview.load (std::memory_order_relaxed); }
    void setPreview (bool on) noexcept { preview.store (on, std::memory_order_relaxed); }
    double getPlayPpq() const noexcept { return playPpq.load (std::memory_order_relaxed); }
    float getPlayBpm() const noexcept { return lastBpm.load (std::memory_order_relaxed); }
    bool isHostOrPreviewPlaying() const noexcept { return uiPlaying.load (std::memory_order_relaxed); }

    juce::String exportMidiToDocuments();
    // One type-1 MIDI file: tempo track, then one track per lane.
    juce::StringArray writeTempMidi();
    // One single-track MIDI file per lane (all 7, empty lanes included), one loop
    // (LENGTH) long, in one fresh cache folder. Same names as writeLaneMidi.
    juce::StringArray writeLaneMidiFiles();
    // One WAV stem per lane, one loop long (the LENGTH setting), seamless and
    // downbeat-aligned. Same cache and drag path as the MIDI file.
    juce::StringArray writeStemAudio();
    // Stems plus the full mix, written straight into `dir`.
    juce::String exportStemsTo (const juce::File& dir);

    // Drag-out files live here so FL Studio on macOS can read them after
    // performExternalDragDropOfFiles.
    juce::File exportCache() const;
    juce::StringArray writeLaneMidi (int part);
    juce::StringArray writeLaneAudio (int part);
    bool loadDrumFile (int voice, const juce::File& file, juce::String& error);
    void clearDrumVoice (int voice);
    void setDrumSampleSlot (DrumSampleSlot* slot) noexcept;
    bool drumVoiceLoaded (int voice) const noexcept;
    juce::String getDrumSampleName (int voice) const;

    void triggerRoll();
    void triggerShuffle (int part);

    // Status line after the originality floor refused a ROLL / shuffle / reshape
    // and the previous roll was kept ("Roll refused: ..."). Empty again after
    // the next accepted roll. serial changes on every set or clear.
    struct RollNotice
    {
        juce::String text;
        std::uint32_t serial = 0;
    };
    RollNotice rollNotice() const;
    // Alt-click on a solo button: solo `part` and clear every other lane's solo.
    void setExclusiveSolo (int part);

    bool laneSampleMissing (int part) const;
    void setSamplesRoot (const juce::File& folder);
    juce::File samplesRoot() const;
    SampleLibrary& sampleLibrary() noexcept { return library; }

    // Files the background index found on disk, and manifest entries that are absent.
    // -1 until the first scan is published.
    int samplesFound() const noexcept
    {
        const int n = library.indexedFound();
        return n >= 0 ? n : samplesFoundCount.load (std::memory_order_relaxed);
    }
    int samplesMissing() const noexcept
    {
        if (library.indexedFound() >= 0)
            return library.indexedMissing();
        return samplesMissingCount.load (std::memory_order_relaxed);
    }
    juce::String sampleInventoryText() const;

    // Where samples come from and whether that is the pack built into the plugin.
    // message is empty when the bundled pack is active and indexed with files;
    // otherwise "Samples not loaded: <path> (<n> files)" for the UI banner.
    struct SamplesStatus
    {
        juce::File root;
        int found = -1;
        bool bundled = false;
        juce::String message;
    };
    SamplesStatus samplesStatus() const;

    // One-line UI notice (e.g. the Key + Scale guard: "NO SAFE FILE: STAB muted in D# Phrygian").
    // Any thread may set it; never call from the audio thread (takes a lock, copies a String).
    // An empty string clears it. The editor polls getUiNotice() and shows it in the status strip.
    void setUiNotice (const juce::String& text)
    {
        {
            const std::lock_guard<std::mutex> lock (noticeMu);
            uiNotice = text;
        }
        uiNoticeSerial.fetch_add (1, std::memory_order_release);
    }
    void clearUiNotice() { setUiNotice ({}); }
    juce::String getUiNotice() const
    {
        const std::lock_guard<std::mutex> lock (noticeMu);
        return uiNotice;
    }
    // Bumps on every set; lets the editor skip the lock when nothing changed.
    std::uint32_t getUiNoticeSerial() const noexcept { return uiNoticeSerial.load (std::memory_order_acquire); }
    // Retune guard notice for the header, e.g. "NO SAFE FILE: BASS in F# Dorian".
    // Empty when every pitched lane has a file that survives the key. Any thread.
    juce::String retuneNotice() const;
    // Tests: whether the guard muted a lane's sampler for this pattern.
    bool retuneMutedLane (int part) const noexcept
    {
        return part >= 0 && part < NumParts && retuneMuted[part].load (std::memory_order_relaxed);
    }

    // Library-relative file the last note on a lane (or drum voice) played. Tests only.
    std::string lastPickedFile (int part, int drumVoice = -1) const { return engine.lastPickedFile (part, drumVoice); }

    // 14 session slots (6 lanes + 8 drum voices). Paths persist; audio does not.
    bool loadUserSlot (int slot, const juce::File& file, juce::String& error);
    void clearUserSlot (int slot);
    void setUserSlotMode (int slot, int mode);
    int userSlotMode (int slot) const noexcept;
    bool userSlotWarning (int slot) const noexcept;
    juce::String userSlotFile (int slot) const;
    int userSlotRoot (int slot) const;
    void setUserSlotRoot (int slot, int midiNote, float cents);
    juce::String userSlotError (int slot) const;
    UserSlots& userSlots() noexcept { return slots; }

    // Editor size in this instance. 0 until a project state or the editor sets it.
    void setEditorSize (int width, int height) noexcept;
    int getEditorWidth() const noexcept { return editorWidth.load (std::memory_order_relaxed); }
    int getEditorHeight() const noexcept { return editorHeight.load (std::memory_order_relaxed); }
    std::uint32_t getProjectEditorSerial() const noexcept { return projectEditorSerial.load (std::memory_order_relaxed); }

private:
    mutable std::mutex noticeMu;
    juce::String uiNotice;
    std::atomic<std::uint32_t> uiNoticeSerial { 0 };

    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void applyDialect (int dialect);
    void regenerate (int onlyPart);
    void setRollNotice (const juce::String& text);
    mutable std::mutex noticeMu;
    RollNotice notice;
    void requestReshape();
    void reshapeUnkept();
    EngineParams readParams (std::shared_ptr<const PresetBank>& hold) const;
    void refreshSampleInventory();
    Settings currentSettings() const;
    void prepareOffline (Engine& offline, double rate) const;
    // One loop of `part` (or rollexport::kFull for the whole mix) through the plugin path.
    juce::AudioBuffer<float> bounceLoop (Engine& offline, const Groove& g, int part, double rate, double bpm) const;
    // Bounces several parts at once, one offline engine per part.
    std::vector<juce::AudioBuffer<float>> bounceParts (const Groove& g, const std::vector<int>& parts, double rate, double bpm) const;
    // A fresh subfolder of the cache for one drag, so file names stay clean and unique.
    juce::File newExportFolder() const;
    // The playing groove, with the bass in the octave the engine plays (MIDI files).
    Groove exportGroove() const;
    void pruneExportCache (const juce::File& keep) const;
    bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate) const;
    void publish (const Groove& g);
    void publishInitial (const Groove& g);
    void publishDerived();
    void reapplyIntensity();
    void storeSource (const Groove& g);
    Groove copySource() const;
    void post (std::function<void (RollaProcessor&)> fn);
    void deferredStartup();
    void rememberSample (int voice, const float* mono, int n, double rate, const juce::String& name);
    void applyRememberedSamples();
    // Message thread. Loads the files the current presets and picks will play, so a
    // freshly rolled lane is ready before the downbeat instead of starting silent.
    void prefetchPlayFiles();
    // Message thread (prefetch timer). Checks each pitched lane's pick against the
    // retune guard for the shifts this roll needs and moves samplePick to the next
    // pool file that survives. Key locked and none survives: the lane is muted and
    // retuneNotice() says so. Key Auto: the key moves to the nearest one where every
    // lane survives (today's when it does). Never the audio thread.
    void guardRetunes();

    struct AsyncState
    {
        std::mutex mu;
        std::atomic<bool> alive { true };
        std::atomic<uint32_t> epoch { 1 };
    };

    Engine engine;
    MixProgram mixProgram;
    std::uint8_t edited[NumParts] {};
    juce::String voxSampleName;
    std::shared_ptr<AsyncState> async = std::make_shared<AsyncState>();
    std::atomic<bool> restoring { false };

    mutable std::mutex grooveMu;
    Groove groove {};
    Groove sourceGroove {};
    std::atomic<bool> sourceReady { false };

    struct Remembered
    {
        std::vector<float> mono;
        double rate = 0.0;
        bool set = false;
        juce::String name;
    };
    Remembered remembered[NumDrumVoices];
    mutable std::mutex sampleMu;

    SampleLibrary library;
    UserSlots slots;
    std::atomic<int> samplePick[NumParts] {};
    std::atomic<int> samplesFoundCount { -1 };
    std::atomic<int> samplesMissingCount { -1 };

    std::atomic<int> editorWidth { 0 };
    std::atomic<int> editorHeight { 0 };
    std::atomic<std::uint32_t> projectEditorSerial { 0 };

    std::atomic<bool> startupStop { false };
    std::atomic<bool> rootChosen { false };
    std::mutex rootSetupMu;
    std::atomic<bool> startupDone { false };
    std::atomic<double> startupMs { 0.0 };
    std::atomic<bool> reshapePending { false };
    std::atomic<bool> reshapeAfterStartup { false };
    double startupT0 = 0.0;
    std::thread startupThread;
    std::atomic<std::uint32_t> grooveEpoch { 0 };

    std::atomic<bool> preview { false };
    std::atomic<bool> uiPlaying { false };
    std::atomic<double> playPpq { 0.0 };
    std::atomic<float> lastBpm { 130.f };
    double previewPpq = 0.0;
    // Free-running phrase (audio thread only). FL Studio loops its pattern, so its
    // ppq wraps every pattern length while the transport keeps playing. Each host
    // loop wrap adds the wrapped distance here, so an 8-bar roll plays through a
    // 4-bar host loop. Cleared on stop -> start and on a real seek.
    double phraseOffset = 0.0;
    double hostExpectPpq = 0.0;
    double hostLastBeats = 0.0;
    bool hostRolling = false;
    double preparedRate = 48000.0;
    // Pattern MIDI held back by the tp_guard latency (see delayMidiOut). Audio thread only.
    juce::MidiBuffer midiLate, midiNow, midiNext;
    void delayMidiOut (juce::MidiBuffer& midi, int n);
    // The live engine's drum slot, so offline bounces hear the same user drum samples.
    std::atomic<DrumSampleSlot*> drumSlot { nullptr };

    std::uint64_t prefetchedKey = 0;
    // Retune guard state. Message thread only, except the cache (locked).
    RetuneTolerances retuneTol;
    RetuneCache retuneCache;
    std::uint64_t guardedStamp = 0;
    bool guardPending = false;
    // Key locked, no surviving file: the sampler is silent on the lane (readParams).
    std::atomic<bool> retuneMuted[NumParts] {};
    void setRetuneNotice (const juce::String& text); // writes the UI notice
    struct PrefetchTimer : juce::Timer
    {
        explicit PrefetchTimer (RollaProcessor& p) : owner (p) {}
        void timerCallback() override { owner.prefetchPlayFiles(); }
        RollaProcessor& owner;
    };
    // Last member, so it stops first on destruction.
    PrefetchTimer prefetchTimer { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RollaProcessor)
};

} // namespace rolla
