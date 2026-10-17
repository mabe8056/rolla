#include "Theme.h"

#include <BinaryData.h>

#include <cstdlib>
#include <mutex>

namespace rolla
{
namespace
{

struct Faces
{
    juce::Typeface::Ptr display600;
    juce::Typeface::Ptr display700;
    juce::Typeface::Ptr display800;
    juce::Typeface::Ptr mono400;
    juce::Typeface::Ptr mono500;
    juce::Typeface::Ptr mono600;
    juce::Typeface::Ptr mono700;
};

// Released before this function's own static destructor. On macOS the CoreText
// registry is created while the first typeface is built, so it dies before a
// typeface held by an earlier static, and ~CoreTextTypeface locks a dead mutex.
Faces* liveFaces = nullptr;

void releaseFaces()
{
    if (liveFaces == nullptr)
        return;
    liveFaces->display600 = nullptr;
    liveFaces->display700 = nullptr;
    liveFaces->display800 = nullptr;
    liveFaces->mono400 = nullptr;
    liveFaces->mono500 = nullptr;
    liveFaces->mono600 = nullptr;
    liveFaces->mono700 = nullptr;
    liveFaces = nullptr;
}

Faces& faces()
{
    static Faces loaded;
    static std::once_flag once;
    std::call_once (once, []
    {
        loaded.display600 = juce::Typeface::createSystemTypefaceFor (BinaryData::UnboundedSemiBold_ttf,
                                                                      BinaryData::UnboundedSemiBold_ttfSize);
        loaded.display700 = juce::Typeface::createSystemTypefaceFor (BinaryData::UnboundedBold_ttf,
                                                                      BinaryData::UnboundedBold_ttfSize);
        loaded.display800 = juce::Typeface::createSystemTypefaceFor (BinaryData::UnboundedExtraBold_ttf,
                                                                      BinaryData::UnboundedExtraBold_ttfSize);
        loaded.mono400 = juce::Typeface::createSystemTypefaceFor (BinaryData::MartianMonoSemiCondRegular_ttf,
                                                                   BinaryData::MartianMonoSemiCondRegular_ttfSize);
        loaded.mono500 = juce::Typeface::createSystemTypefaceFor (BinaryData::MartianMonoSemiCondMedium_ttf,
                                                                   BinaryData::MartianMonoSemiCondMedium_ttfSize);
        loaded.mono600 = juce::Typeface::createSystemTypefaceFor (BinaryData::MartianMonoSemiCondSemiBold_ttf,
                                                                   BinaryData::MartianMonoSemiCondSemiBold_ttfSize);
        loaded.mono700 = juce::Typeface::createSystemTypefaceFor (BinaryData::MartianMonoSemiCondBold_ttf,
                                                                   BinaryData::MartianMonoSemiCondBold_ttfSize);
        liveFaces = &loaded;
        std::atexit (&releaseFaces);
    });
    return loaded;
}

struct FaceSpec
{
    juce::Typeface::Ptr Faces::* face;
    float px;
    float kern;
    float line;
};

FaceSpec specFor (Theme::Role role) noexcept
{
    using Role = Theme::Role;
    switch (role)
    {
        case Role::wordmark: return { &Faces::display800, 16.f, -0.0161f, 24.f };
        case Role::roll:     return { &Faces::display700, 34.f, -0.0242f, 48.f };
        case Role::section:  return { &Faces::display700, 14.f,  0.f,     24.f };
        case Role::macro:    return { &Faces::display600, 11.f,  0.0323f, 16.f };
        case Role::tile:     return { &Faces::display600, 12.f,  0.0161f, 16.f };
        case Role::preset:   return { &Faces::mono700,    13.f,  0.f,     16.f };
        case Role::lane:     return { &Faces::mono600,    12.f,  0.0167f, 16.f };
        case Role::voice:    return { &Faces::mono700,    11.f,  0.0167f, 16.f };
        case Role::pill:     return { &Faces::mono600,    11.f,  0.f,     16.f };
        case Role::button:   return { &Faces::mono600,    10.f,  0.0333f, 16.f };
        case Role::caption:  return { &Faces::mono500,    10.f,  0.0500f, 16.f };
        case Role::micro:    return { &Faces::mono500,    10.f,  0.f,     12.f };
        case Role::value:    return { &Faces::mono500,    12.f,  0.f,     16.f };
        case Role::macroval: return { &Faces::mono600,    15.f,  0.f,     16.f };
        case Role::microval: return { &Faces::mono400,    10.f,  0.f,     12.f };
        case Role::meta:     return { &Faces::mono400,    10.f,  0.f,     16.f };
        case Role::keycap:   return { &Faces::mono500,    10.f,  0.0333f, 16.f };
    }
    return { &Faces::mono400, 10.f, 0.f, 16.f };
}

} // namespace

juce::Font Theme::font (Role role)
{
    const auto spec = specFor (role);
    auto& loaded = faces();
    const auto type = loaded.*(spec.face);
    return juce::Font (juce::FontOptions (type).withPointHeight (spec.px).withKerningFactor (spec.kern));
}

float Theme::lineHeight (Role role) noexcept
{
    return specFor (role).line;
}

void Theme::warmTypefaces()
{
    (void) faces();
}

} // namespace rolla
