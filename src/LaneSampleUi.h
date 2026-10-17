#pragma once

#include "PluginProcessor.h"

#include <type_traits>
#include <utility>

namespace rolla
{

// Per-lane playback source. 0 synth, 1 sample, 2 layer.
// The sampler owns the real flags. This probe calls these RollaProcessor
// methods when they exist, and otherwise keeps a project-state stub that
// writes the mode the engine already reads (PresetBank sample blocks):
//   int  laneSampleMode (int part) const;
//   void setLaneSampleMode (int part, int mode);
//   bool laneSampleMissing (int part) const;
inline constexpr const char* kLaneModeNames[] = { "SYNTH", "SAMPLE", "LAYER" };

inline juce::String laneModeProperty (int part)
{
    return "uiLaneMode" + juce::String (part);
}

inline SampleSpec* sampleSpecMutable (PresetBank& bank, int part, int preset) noexcept
{
    const int n = bank.count[part] > 0 ? bank.count[part] : PresetBank::kPer;
    const int i = juce::jlimit (0, n - 1, preset);
    switch (part)
    {
        case Bass: return &bank.bass[i].sample;
        case Stab: return &bank.stab[i].sample;
        case Hits: return &bank.hits[i].sample;
        case Pluck: return &bank.pluck[i].sample;
        case Pad: return &bank.pad[i].sample;
        case Vox: return &bank.vox[i].sample;
        case Drums: return &bank.drums[i].sample;
        default: break;
    }
    return nullptr;
}

namespace detail
{

template <typename T, typename = void>
struct HasLaneModeGet : std::false_type {};

template <typename T>
struct HasLaneModeGet<T, std::void_t<decltype (std::declval<const T&>().laneSampleMode (0))>> : std::true_type {};

template <typename T, typename = void>
struct HasLaneModeSet : std::false_type {};

template <typename T>
struct HasLaneModeSet<T, std::void_t<decltype (std::declval<T&>().setLaneSampleMode (0, 0))>> : std::true_type {};

template <typename T, typename = void>
struct HasLaneMissing : std::false_type {};

template <typename T>
struct HasLaneMissing<T, std::void_t<decltype (std::declval<const T&>().laneSampleMissing (0))>> : std::true_type {};

inline int presetIndexOf (const RollaProcessor& proc, int part)
{
    const auto id = juce::String (partId (part)) + "Preset";
    if (auto* v = proc.apvts.getRawParameterValue (id))
        return juce::jlimit (0, PresetBank::kPer - 1, (int) v->load());
    return 0;
}

inline int stubMode (const RollaProcessor& proc, int part)
{
    const auto key = laneModeProperty (part);
    if (proc.apvts.state.hasProperty (key))
        return juce::jlimit (0, 2, (int) proc.apvts.state.getProperty (key));
    const auto bank = proc.copyPresets();
    const auto& spec = sampleSpecFor (bank, part, presetIndexOf (proc, part));
    return juce::jlimit (0, 2, (int) spec.mode);
}

inline bool applyStubMode (RollaProcessor& proc, int part, int mode)
{
    bool changed = false;
    proc.modifyPresets ([&] (PresetBank& bank)
    {
        const int n = bank.count[part] > 0 ? bank.count[part] : PresetBank::kPer;
        for (int i = 0; i < n; ++i)
        {
            if (auto* spec = sampleSpecMutable (bank, part, i))
            {
                if ((int) spec->mode != mode)
                {
                    spec->mode = (SampleMode) mode;
                    changed = true;
                }
            }
        }
    });
    return changed;
}

template <typename Proc>
int readMode (const Proc& proc, int part)
{
    if constexpr (HasLaneModeGet<Proc>::value)
        return juce::jlimit (0, 2, static_cast<int> (proc.laneSampleMode (part)));
    else
        return stubMode (proc, part);
}

template <typename Proc>
void writeMode (Proc& proc, int part, int mode)
{
    mode = juce::jlimit (0, 2, mode);
    if constexpr (HasLaneModeSet<Proc>::value)
    {
        proc.setLaneSampleMode (part, mode);
    }
    else
    {
        if (proc.apvts.state.hasProperty (laneModeProperty (part))
            && (int) proc.apvts.state.getProperty (laneModeProperty (part)) == mode)
            return;
        proc.undo.beginNewTransaction();
        proc.apvts.state.setProperty (laneModeProperty (part), mode, &proc.undo);
        if (applyStubMode (proc, part, mode))
            proc.sampleLibrary().watch (proc.copyPresets());
    }
}

template <typename Proc>
bool readMissing (const Proc& proc, int part)
{
    if constexpr (HasLaneMissing<Proc>::value)
        return static_cast<bool> (proc.laneSampleMissing (part));
    else
        return false;
}

} // namespace detail

// UI-facing view of one lane's sample flags.
class LaneSampleProbe
{
public:
    explicit LaneSampleProbe (RollaProcessor& processor) noexcept
        : proc (processor)
    {
        captureFactory();
    }

    void captureFactory()
    {
        const auto bank = proc.copyPresets();
        for (int part = 0; part < NumParts; ++part)
            if (! touched[part])
                for (int i = 0; i < PresetBank::kPer; ++i)
                    factory[part][i] = (int) sampleSpecFor (bank, part, i).mode;
        seenGen = proc.presetGeneration();
    }

    int mode (int part) const
    {
        if (part < 0 || part >= NumParts)
            return 0;
        return detail::readMode (proc, part);
    }

    void setMode (int part, int next)
    {
        if (part < 0 || part >= NumParts)
            return;
        detail::writeMode (proc, part, next);
    }

    bool missing (int part) const
    {
        if (part < 0 || part >= NumParts)
            return false;
        return detail::readMissing (proc, part);
    }

    // Push a restored stub onto the preset blocks the engine reads.
    // No-op once RollaProcessor::setLaneSampleMode exists.
    void reconcile()
    {
        if constexpr (detail::HasLaneModeSet<RollaProcessor>::value)
            return;

        if (proc.presetGeneration() != seenGen)
            captureFactory();

        bool changed = false;
        for (int part = 0; part < NumParts; ++part)
        {
            const auto key = laneModeProperty (part);
            if (proc.apvts.state.hasProperty (key))
            {
                const int next = juce::jlimit (0, 2, (int) proc.apvts.state.getProperty (key));
                changed = detail::applyStubMode (proc, part, next) || changed;
                touched[part] = true;
            }
            else if (touched[part])
            {
                int saved[PresetBank::kPer];
                for (int i = 0; i < PresetBank::kPer; ++i)
                    saved[i] = factory[part][i];
                proc.modifyPresets ([&] (PresetBank& bank)
                {
                    const int n = bank.count[part] > 0 ? bank.count[part] : PresetBank::kPer;
                    for (int i = 0; i < n; ++i)
                    {
                        if (auto* spec = sampleSpecMutable (bank, part, i))
                        {
                            if ((int) spec->mode != saved[i])
                            {
                                spec->mode = (SampleMode) saved[i];
                                changed = true;
                            }
                        }
                    }
                });
                touched[part] = false;
            }
        }
        if (changed)
            proc.sampleLibrary().watch (proc.copyPresets());
    }

    bool engineModeApi() const noexcept
    {
        return detail::HasLaneModeSet<RollaProcessor>::value;
    }

private:
    RollaProcessor& proc;
    int factory[NumParts][PresetBank::kPer] {};
    bool touched[NumParts] {};
    std::uint32_t seenGen = 0;
};

} // namespace rolla
