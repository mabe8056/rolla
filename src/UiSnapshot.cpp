#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "ui/Theme.h"

#include <juce_graphics/juce_graphics.h>

#include <cstdio>
#include <memory>

// Headless render of the 1024×640 editor. Pass an output path and, optionally,
// "--missing" to force the stab lane into SAMPLE mode so the warning icon shows.
int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::String outPath = argc > 1 ? juce::String (argv[1]) : juce::String ("editor@2x.png");
    bool showMissing = false;
    bool openLane = false;
    int lane = 0;
    float scale = 2.f;
    for (int i = 2; i < argc; ++i)
    {
        const juce::String arg (argv[i]);
        if (arg == "--missing")
            showMissing = true;
        else if (arg.startsWith ("--lane="))
        {
            openLane = true;
            lane = arg.substring (7).getIntValue();
        }
        else if (arg.startsWith ("--scale="))
            scale = (float) arg.substring (8).getDoubleValue();
    }
    if (scale < 1.f)
        scale = 1.f;

    const bool bench = outPath == "--bench" || outPath == "--bench-later";
    if (outPath == "--bench-later")
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        rolla::RollaProcessor later;
        const double t1 = juce::Time::getMillisecondCounterHiRes();
        later.waitUntilReady();
        const double t2 = juce::Time::getMillisecondCounterHiRes();
        rolla::RollaEditor laterEditor (later);
        const double t3 = juce::Time::getMillisecondCounterHiRes();
        std::printf ("instantiate_ms %.2f\nstartup_wait_ms %.2f\nstartup_work_ms %.2f\neditor_opened_later_ms %.2f\n",
                     t1 - t0, t2 - t1, later.startupMillis(), t3 - t2);
        return 0;
    }
    const int repeats = bench ? 4 : 1;
    double instMs = 0.0;
    double editMs = 0.0;
    for (int n = 0; n < repeats; ++n)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        auto processor = std::make_unique<rolla::RollaProcessor>();
        const double t1 = juce::Time::getMillisecondCounterHiRes();
        if (showMissing)
            processor->apvts.state.setProperty ("uiLaneMode1", 1, nullptr);
        auto editor = std::make_unique<rolla::RollaEditor> (*processor);
        const double t2 = juce::Time::getMillisecondCounterHiRes();
        if (n == 0)
        {
            instMs = t1 - t0;
            editMs = t2 - t1;
        }
        if (! bench)
            break;
        if (n == repeats - 1)
        {
            const double tReady0 = juce::Time::getMillisecondCounterHiRes();
            processor->waitUntilReady();
            const double readyMs = juce::Time::getMillisecondCounterHiRes() - tReady0;
            editor.reset();
            const double tEdit0 = juce::Time::getMillisecondCounterHiRes();
            editor = std::make_unique<rolla::RollaEditor> (*processor);
            const double editorReadyMs = juce::Time::getMillisecondCounterHiRes() - tEdit0;
            const auto sample = rolla::Theme::font (rolla::Theme::Role::lane);
            std::printf ("instantiate_ms %.2f\neditor_ms %.2f\n", instMs, editMs);
            std::printf ("instantiate_warm_ms %.2f\neditor_warm_ms %.2f\n", t1 - t0, t2 - t1);
            std::printf ("startup_wait_ms %.2f\nstartup_work_ms %.2f\n", readyMs, processor->startupMillis());
            std::printf ("editor_after_ready_ms %.2f\n", editorReadyMs);
            std::printf ("lane_height_px %.2f\nlane_point_px %.2f\n",
                         sample.getHeight(), sample.getHeightInPoints());
        }
        editor.reset();
        processor.reset();
    }
    if (bench)
    {
        auto later = std::make_unique<rolla::RollaProcessor>();
        later->waitUntilReady();
        const double tOpen = juce::Time::getMillisecondCounterHiRes();
        auto laterEditor = std::make_unique<rolla::RollaEditor> (*later);
        std::printf ("editor_opened_later_ms %.2f\n", juce::Time::getMillisecondCounterHiRes() - tOpen);
        return 0;
    }

    rolla::RollaProcessor processor;
    processor.waitUntilReady();
    if (showMissing)
        processor.apvts.state.setProperty ("uiLaneMode1", 1, nullptr);

    rolla::RollaEditor editor (processor);
    editor.setSize (1024, 640);
    if (openLane)
        editor.selectLane (lane);

    {
        auto measureWidth = [] (rolla::Theme::Role role, const juce::String& text)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (rolla::Theme::font (role), text, 0.f, 0.f);
            return glyphs.getBoundingBox (0, -1, true).getWidth();
        };
        auto capHeight = [] (rolla::Theme::Role role)
        {
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (rolla::Theme::font (role), "H", 0.f, 0.f);
            if (glyphs.getNumGlyphs() < 1)
                return 0.f;
            juce::Path path;
            glyphs.getGlyph (0).createPath (path);
            return path.getBounds().getHeight();
        };
        std::printf ("jackin_width_px %.2f\nlane_cap_px %.2f\nsection_cap_px %.2f\n",
                     measureWidth (rolla::Theme::Role::pill, "JACKIN 90s"),
                     capHeight (rolla::Theme::Role::lane),
                     capHeight (rolla::Theme::Role::section));
        std::printf ("lane_i_px %.2f\nlane_m_px %.2f\nword_i_px %.2f\nword_m_px %.2f\n",
                     measureWidth (rolla::Theme::Role::lane, "I"),
                     measureWidth (rolla::Theme::Role::lane, "M"),
                     measureWidth (rolla::Theme::Role::wordmark, "I"),
                     measureWidth (rolla::Theme::Role::wordmark, "M"));
    }

    const int pixelsW = juce::roundToInt (1024.f * scale);
    const int pixelsH = juce::roundToInt (640.f * scale);
    juce::Image image (juce::Image::ARGB, pixelsW, pixelsH, true);
    {
        juce::Graphics g (image);
        g.addTransform (juce::AffineTransform::scale (scale));
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
