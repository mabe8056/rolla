#include "PluginEditor.h"
#include "PluginProcessor.h"

#include <juce_graphics/juce_graphics.h>

// Headless render of the 1024×640 editor. Pass an output path and, optionally,
// "--missing" to force the stab lane into SAMPLE mode so the warning icon shows.
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::String outPath = argc > 1 ? juce::String (argv[1]) : juce::String ("editor@2x.png");
    bool showMissing = false;
    int lane = 0;
    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--missing")
            showMissing = true;
        else if (arg.startsWith ("--lane="))
            lane = arg.substring (7).getIntValue();
    }

    rolla::RollaProcessor processor;
    if (showMissing)
        processor.apvts.state.setProperty ("uiLaneMode1", 1, nullptr);

    rolla::RollaEditor editor (processor);
    editor.setSize (1024, 640);
    if (lane != 0)
        editor.selectLane (lane);

    juce::Image image (juce::Image::ARGB, 2048, 1280, true);
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (2.f));
        editor.paintEntireComponent (g, true);
    }

    juce::File file (outPath);
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::FileOutputStream stream (file);
    if (! stream.openedOk())
        return 1;

    juce::PNGImageFormat png;
    return png.writeImageToStream (image, stream) ? 0 : 1;
}
