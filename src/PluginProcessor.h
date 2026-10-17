#pragma once

#include "Engine/Engine.h"
#include "Engine/SampleLibrary.h"
#include "Sampler/UserSlots.h"
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
    juce::StringArray writeTempMidi();
    bool loadDrumFile (int voice, const juce::File& file, juce::String& error);
    void clearDrumVoice (int voice);
    void setDrumSampleSlot (DrumSampleSlot* slot) noexcept;
    bool drumVoiceLoaded (int voice) const noexcept;
    juce::String getDrumSampleName (int voice) const;

    void triggerRoll();
    void triggerShuffle (int part);

    bool laneSampleMissing (int part) const;
    void setSamplesRoot (const juce::File& folder);
    juce::File samplesRoot() const;
    SampleLibrary& sampleLibrary() noexcept { return library; }

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
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    void applyDialect (int dialect);
    void regenerate (int onlyPart);
    Settings currentSettings() const;
    void publish (const Groove& g);
    void publishInitial (const Groove& g);
    void deferredStartup();
    void rememberSample (int voice, const float* mono, int n, double rate, const juce::String& name);
    void applyRememberedSamples();

    struct AsyncState
    {
        std::mutex mu;
        std::atomic<bool> alive { true };
        std::atomic<uint32_t> epoch { 1 };
    };

    Engine engine;
    std::uint8_t edited[NumParts] {};
    juce::String voxSampleName;
    std::shared_ptr<AsyncState> async = std::make_shared<AsyncState>();
    std::atomic<bool> restoring { false };

    mutable std::mutex grooveMu;
    Groove groove {};

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

    std::atomic<int> editorWidth { 0 };
    std::atomic<int> editorHeight { 0 };
    std::atomic<std::uint32_t> projectEditorSerial { 0 };

    std::atomic<bool> startupStop { false };
    std::atomic<bool> rootChosen { false };
    std::mutex rootSetupMu;
    std::atomic<bool> startupDone { false };
    std::atomic<double> startupMs { 0.0 };
    double startupT0 = 0.0;
    std::thread startupThread;
    std::atomic<std::uint32_t> grooveEpoch { 0 };

    std::atomic<bool> preview { false };
    std::atomic<bool> uiPlaying { false };
    std::atomic<double> playPpq { 0.0 };
    std::atomic<float> lastBpm { 130.f };
    double previewPpq = 0.0;
    double preparedRate = 48000.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RollaProcessor)
};

} // namespace rolla
