#include "Theme.h"

#include <BinaryData.h>

#include <mutex>

namespace rolla
{
namespace
{

struct Faces
{
    juce::Typeface::Ptr sansSemi;
    juce::Typeface::Ptr sansBold;
    juce::Typeface::Ptr mono;
    juce::Typeface::Ptr monoMed;
    juce::Typeface::Ptr monoSemi;
};

Faces& faces()
{
    static Faces loaded;
    static std::once_flag once;
    std::call_once (once, []
    {
        loaded.sansSemi = juce::Typeface::createSystemTypefaceFor (BinaryData::InstrumentSansSemiBold_ttf,
                                                                    BinaryData::InstrumentSansSemiBold_ttfSize);
        loaded.sansBold = juce::Typeface::createSystemTypefaceFor (BinaryData::InstrumentSansBold_ttf,
                                                                    BinaryData::InstrumentSansBold_ttfSize);
        loaded.mono = juce::Typeface::createSystemTypefaceFor (BinaryData::GeistMonoRegular_ttf,
                                                                BinaryData::GeistMonoRegular_ttfSize);
        loaded.monoMed = juce::Typeface::createSystemTypefaceFor (BinaryData::GeistMonoMedium_ttf,
                                                                   BinaryData::GeistMonoMedium_ttfSize);
        loaded.monoSemi = juce::Typeface::createSystemTypefaceFor (BinaryData::GeistMonoSemiBold_ttf,
                                                                    BinaryData::GeistMonoSemiBold_ttfSize);
    });
    return loaded;
}

// Point size is the em square the spec calls "px". Font::withHeight is ascent+descent,
// which draws the same face about 20% smaller.
juce::Font face (const juce::Typeface::Ptr& type, float points)
{
    return juce::Font (juce::FontOptions (type).withPointHeight (points));
}

} // namespace

juce::Font Theme::sansSemi (float height) { return face (faces().sansSemi, height); }
juce::Font Theme::sansBold (float height) { return face (faces().sansBold, height); }
juce::Font Theme::mono (float height) { return face (faces().mono, height); }
juce::Font Theme::monoMed (float height) { return face (faces().monoMed, height); }
juce::Font Theme::monoSemi (float height) { return face (faces().monoSemi, height); }

void Theme::warmTypefaces()
{
    (void) faces();
}

} // namespace rolla
