#pragma once

#include "PluginProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace rolla
{

class RollaEditor : public juce::AudioProcessorEditor,
                    public juce::DragAndDropContainer,
                    private juce::AudioProcessorValueTreeState::Listener,
                    private juce::Timer
{
public:
    explicit RollaEditor (RollaProcessor&);
    ~RollaEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    void setEditorScale (float scale);
    void selectLane (int part);

private:
    void timerCallback() override;
    void parameterChanged (const juce::String& parameterID, float newValue) override;
    bool everyLaneKept() const;
    juce::Point<int> initialSize() const;
    void rememberSize();

    RollaProcessor& proc;
    struct Content;
    struct SizeConstrainer;
    std::unique_ptr<juce::LookAndFeel> look;
    std::unique_ptr<SizeConstrainer> constrainer;
    std::unique_ptr<juce::PropertiesFile> editorSettings;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::TooltipWindow> tooltip;
    std::atomic<bool> rollPulse { false };
    std::uint32_t appliedProjectSerial = 0;
    bool sizeDirty = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RollaEditor)
};

} // namespace rolla
