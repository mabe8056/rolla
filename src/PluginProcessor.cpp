#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "DspUtil.h"
#include "Generator/Drive.h"
#include "Generator/Intensity.h"
#include "Generator/MidiFile.h"
#include "Export/RollExport.h"
#include "Engine/RollaSettings.h"
#include "GenreBrain/GenreTargetsJson.h"
#include "GenreBrain/KitMeshJson.h"
#include "GenreBrain/LanePools.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <BinaryData.h>

#include "ui/Theme.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <thread>
#include <string>
#include <vector>

namespace rolla
{
namespace
{

juce::AudioProcessorValueTreeState::ParameterLayout createLayout (const PresetBank& bank)
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    auto add = [&p] (std::unique_ptr<RangedAudioParameter> param)
    {
        p.push_back (std::move (param));
    };

    const auto unit = NormalisableRange<float> (0.f, 1.f, 0.001f);
    add (std::make_unique<AudioParameterFloat> (ParameterID { "dark", 1 }, "Dark", unit, 0.55f));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "busy", 1 }, "Busy", unit, 0.40f));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "bounce", 1 }, "Bounce", unit, 0.45f));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "stab", 1 }, "Stab", unit, 0.55f));
    add (std::make_unique<AudioParameterFloat> (
        ParameterID { "styleIntensity", 1 }, "Style Intensity", unit, 0.5f));
    add (std::make_unique<AudioParameterInt> (
        ParameterID { "intensity", 1 }, "Intensity", 0, 100, 75));

    StringArray styleNames;
    for (int i = 0; i < NumDialects; ++i)
        styleNames.add (dialectName (i));
    add (std::make_unique<AudioParameterChoice> (
        ParameterID { "dialect", 1 }, "Dialect", styleNames, 0));

    add (std::make_unique<AudioParameterChoice> (
        ParameterID { "key", 1 }, "Key",
        StringArray { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 2));

    add (std::make_unique<AudioParameterFloat> (
        ParameterID { "swing", 1 }, "Swing",
        NormalisableRange<float> (0.50f, 0.57f, 0.001f), 0.52f));

    add (std::make_unique<AudioParameterChoice> (
        ParameterID { "length", 1 }, "Length",
        StringArray { "2", "4", "8" }, 2));

    add (std::make_unique<AudioParameterBool> (ParameterID { "fill", 1 }, "Fill", false));
    add (std::make_unique<AudioParameterBool> (ParameterID { "duck", 1 }, "Duck", true));
    add (std::make_unique<AudioParameterBool> (ParameterID { "drumsToMain", 1 }, "Drums To Main", true));
    add (std::make_unique<AudioParameterFloat> (ParameterID { "drumsAccent", 1 }, "Drums Accent", unit, 0.60f));
    add (std::make_unique<AudioParameterFloat> (
        ParameterID { "output", 1 }, "Output",
        NormalisableRange<float> (-24.f, 6.f, 0.01f), 0.f));

    add (std::make_unique<AudioParameterBool> (ParameterID { "roll", 1 }, "Roll", false));
    add (std::make_unique<AudioParameterBool> (
        ParameterID { "reduceMotion", 1 }, "Reduce Motion", false,
        AudioParameterBoolAttributes().withAutomatable (false)));
    add (std::make_unique<AudioParameterBool> (ParameterID { "hitsInStab", 1 }, "Hits In Stab", false));

    for (int v = 0; v < NumDrumVoices; ++v)
    {
        const auto id = String (v);
        const auto label = String (drumName (v));
        add (std::make_unique<AudioParameterFloat> (
            ParameterID { "dTune" + id, 1 }, label + " Tune",
            NormalisableRange<float> (-12.f, 12.f, 0.01f), 0.f));
        add (std::make_unique<AudioParameterFloat> (
            ParameterID { "dDecay" + id, 1 }, label + " Decay", unit, 0.5f));
        add (std::make_unique<AudioParameterFloat> (
            ParameterID { "dLevel" + id, 1 }, label + " Level",
            NormalisableRange<float> (-24.f, 6.f, 0.01f), 0.f));
        add (std::make_unique<AudioParameterBool> (ParameterID { "dAccent" + id, 1 }, label + " Accent", false));
        add (std::make_unique<AudioParameterFloat> (
            ParameterID { "dAccAmt" + id, 1 }, label + " Accent Amount", unit, 0.60f));
    }

    const char* names[NumParts][PresetBank::kPer] = {};
    for (int i = 0; i < PresetBank::kPer; ++i)
    {
        names[Bass][i] = bank.bass[i].name;
        names[Stab][i] = bank.stab[i].name;
        names[Hits][i] = bank.hits[i].name;
        names[Pluck][i] = bank.pluck[i].name;
        names[Pad][i] = bank.pad[i].name;
        names[Vox][i] = bank.vox[i].name;
        names[Drums][i] = bank.drums[i].name;
    }

    for (int part = 0; part < NumParts; ++part)
    {
        const auto id = String (partId (part));
        const auto label = String (partName (part));
        add (std::make_unique<AudioParameterBool> (ParameterID { id + "Mute", 1 }, label + " Mute", part == Hits));
        add (std::make_unique<AudioParameterBool> (ParameterID { id + "Keep", 1 }, label + " Keep", false));
        StringArray presets;
        for (int i = 0; i < PresetBank::kPer; ++i)
            presets.add (names[part][i] != nullptr && names[part][i][0] != 0 ? names[part][i] : ("Preset " + String (i + 1)));
        add (std::make_unique<AudioParameterChoice> (ParameterID { id + "Preset", 1 }, label + " Preset", presets, 0));
        add (std::make_unique<AudioParameterFloat> (ParameterID { id + "Tone", 1 }, label + " Tone", unit, 0.50f));
        add (std::make_unique<AudioParameterFloat> (ParameterID { id + "Punch", 1 }, label + " Punch", unit, 0.50f));
        add (std::make_unique<AudioParameterFloat> (
            ParameterID { id + "Level", 1 }, label + " Level",
            NormalisableRange<float> (-24.f, 6.f, 0.01f), 0.f));
        // A trigger, like ROLL: automating it re-rolls that lane on the host's beat.
        add (std::make_unique<AudioParameterBool> (ParameterID { id + "Shuffle", 1 }, label + " Shuffle", false));
    }

    // Appended last so saved sessions and host automation keep their parameter indices.
    for (int part = 0; part < NumParts; ++part)
        add (std::make_unique<AudioParameterBool> (ParameterID { String (partId (part)) + "Solo", 1 },
                                                   String (partName (part)) + " Solo", false));

    return { p.begin(), p.end() };
}

const char* factoryPresetName (int part, int index) noexcept
{
    static const char* names[NumParts][PresetBank::kPer] = {
        { "Pluck", "Round", "Click" },
        { "Dub", "Swell", "Click" },
        { "Warehouse", "Piano Hit", "Hoover" },
        { "Hook", "Soft", "Bright" },
        { "Air", "Wide", "Dark" },
        { "Close", "Booth", "Chop" },
        { "909 Tight", "909 Room", "909 Dry" },
    };
    if (part < 0 || part >= NumParts || index < 0 || index >= PresetBank::kPer)
        return "Preset";
    return names[part][index];
}

void writePresetName (char* dest, int size, const char* name)
{
    if (dest == nullptr || size <= 0)
        return;
    std::snprintf (dest, (size_t) size, "%s", name != nullptr ? name : "");
}

std::shared_ptr<PresetBank> makeStartupBank()
{
    auto bank = std::make_shared<PresetBank>();
    loadDefaultPresets (*bank);
    for (int i = 0; i < PresetBank::kPer; ++i)
    {
        writePresetName (bank->bass[i].name, (int) sizeof bank->bass[i].name, factoryPresetName (Bass, i));
        writePresetName (bank->stab[i].name, (int) sizeof bank->stab[i].name, factoryPresetName (Stab, i));
        writePresetName (bank->hits[i].name, (int) sizeof bank->hits[i].name, factoryPresetName (Hits, i));
        writePresetName (bank->pluck[i].name, (int) sizeof bank->pluck[i].name, factoryPresetName (Pluck, i));
        writePresetName (bank->pad[i].name, (int) sizeof bank->pad[i].name, factoryPresetName (Pad, i));
        writePresetName (bank->vox[i].name, (int) sizeof bank->vox[i].name, factoryPresetName (Vox, i));
        writePresetName (bank->drums[i].name, (int) sizeof bank->drums[i].name, factoryPresetName (Drums, i));
    }
    return bank;
}

std::shared_ptr<PresetBank> cachedParsedBank()
{
    static std::mutex mu;
    static std::shared_ptr<PresetBank> cache;
    std::lock_guard<std::mutex> lock (mu);
    if (cache == nullptr)
    {
        cache = std::make_shared<PresetBank>();
        loadPresetsFromJson (*cache, BinaryData::presets_json, BinaryData::presets_jsonSize);
    }
    return cache;
}

float raw (const juce::AudioProcessorValueTreeState& tree, const char* id)
{
    if (auto* v = tree.getRawParameterValue (id))
        return v->load();
    return 0.f;
}

uint32_t freshSeed()
{
    try
    {
        std::random_device rd;
        auto s = (uint32_t) rd();
        return s == 0 ? 1u : s;
    }
    catch (...)
    {
        return (uint32_t) juce::Time::getMillisecondCounter() | 1u;
    }
}

juce::String encodeBlock (const void* data, size_t bytes)
{
    juce::MemoryBlock mb (data, bytes);
    return mb.toBase64Encoding();
}

bool decodeBlock (const juce::String& text, void* data, size_t bytes)
{
    juce::MemoryBlock mb;
    if (! mb.fromBase64Encoding (text) || mb.getSize() != bytes)
        return false;
    std::memcpy (data, mb.getData(), bytes);
    return true;
}

} // namespace

RollaProcessor::RollaProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Drums", juce::AudioChannelSet::stereo(), true)),
      presets (makeStartupBank()),
      apvts (*this, &undo, "ROLLA", createLayout (*presets))
{
    for (auto& pick : drumPick)
        pick.store (-1, std::memory_order_relaxed);
    apvts.addParameterListener ("roll", this);
    apvts.addParameterListener ("dialect", this);
    for (const char* id : { "dark", "busy", "bounce", "stab", "styleIntensity", "fill", "hitsInStab" })
        apvts.addParameterListener (id, this);
    apvts.addParameterListener ("intensity", this);
    for (int i = 0; i < NumParts; ++i)
    {
        apvts.addParameterListener (juce::String (partId (i)) + "Shuffle", this);
        apvts.addParameterListener (juce::String (partId (i)) + "Keep", this);
    }

    mixProgram.loadFromMemory (BinaryData::mix_profiles_json, BinaryData::mix_profiles_jsonSize,
                               BinaryData::main_sum_trim_json, BinaryData::main_sum_trim_jsonSize,
                               BinaryData::mix_makeup_json, BinaryData::mix_makeup_jsonSize);
    startupT0 = juce::Time::getMillisecondCounterHiRes();
    startupThread = std::thread ([this] { deferredStartup(); });
    prefetchTimer.startTimer (50);
}

RollaProcessor::~RollaProcessor()
{
    prefetchTimer.stopTimer();
    startupStop.store (true);
    if (startupThread.joinable())
        startupThread.join();

    apvts.removeParameterListener ("roll", this);
    apvts.removeParameterListener ("dialect", this);
    for (const char* id : { "dark", "busy", "bounce", "stab", "styleIntensity", "fill", "hitsInStab" })
        apvts.removeParameterListener (id, this);
    apvts.removeParameterListener ("intensity", this);
    for (int i = 0; i < NumParts; ++i)
    {
        apvts.removeParameterListener (juce::String (partId (i)) + "Shuffle", this);
        apvts.removeParameterListener (juce::String (partId (i)) + "Keep", this);
    }

    std::lock_guard<std::mutex> lock (async->mu);
    async->alive.store (false);
}

void RollaProcessor::prefetchPlayFiles()
{
    const auto index = library.index();
    const auto bank = sharePresets();
    if (index == nullptr || bank == nullptr)
        return;
    int preset[NumParts] {};
    int pick[NumParts] {};
    std::uint64_t key = 1469598103934665603ull;
    auto mix = [&key] (std::uint64_t v) { key = (key ^ v) * 1099511628211ull; };
    mix ((std::uint64_t) (std::uintptr_t) index.get());
    mix ((std::uint64_t) (std::uintptr_t) bank.get());
    const int dialect = (int) raw (apvts, "dialect");
    mix ((std::uint64_t) dialect);
    for (int i = 0; i < NumParts; ++i)
    {
        preset[i] = presetSlot (*bank, i, (int) raw (apvts, (juce::String (partId (i)) + "Preset").toRawUTF8()));
        pick[i] = samplePick[i].load (std::memory_order_relaxed);
        mix ((std::uint64_t) (std::uint32_t) preset[i]);
        mix ((std::uint64_t) (std::uint32_t) pick[i]);
    }
    int voicePick[NumDrumVoices] {};
    for (int v = 0; v < NumDrumVoices; ++v)
    {
        voicePick[v] = drumPick[v].load (std::memory_order_relaxed);
        mix ((std::uint64_t) (std::uint32_t) voicePick[v]);
    }
    if (key == prefetchedKey)
        return;
    prefetchedKey = key;
    std::vector<std::string> paths;
    collectPlayFiles (*bank, *index, preset, pick, dialect, 8, paths, voicePick);
    library.preload (paths);
}

std::shared_ptr<const PresetBank> RollaProcessor::sharePresets() const
{
    std::lock_guard<std::mutex> lock (presetMu);
    return presets;
}

PresetBank RollaProcessor::copyPresets() const
{
    const auto held = sharePresets();
    return held != nullptr ? *held : PresetBank {};
}

void RollaProcessor::modifyPresets (const std::function<void (PresetBank&)>& edit)
{
    std::lock_guard<std::mutex> lock (presetMu);
    auto next = std::make_shared<PresetBank> (*presets);
    if (edit)
        edit (*next);
    presets = std::move (next);
    presetGen.fetch_add (1, std::memory_order_release);
}

void RollaProcessor::waitUntilReady()
{
    if (startupThread.joinable())
        startupThread.join();
}

void RollaProcessor::publishInitial (const Groove& g)
{
    {
        std::lock_guard<std::mutex> lock (grooveMu);
        if (grooveEpoch.load (std::memory_order_relaxed) != 0)
            return;
        sourceGroove = g;
        sourceReady.store (true, std::memory_order_relaxed);
    }
    publishDerived();
}

void RollaProcessor::storeSource (const Groove& g)
{
    std::lock_guard<std::mutex> lock (grooveMu);
    sourceGroove = g;
    sourceReady.store (true, std::memory_order_relaxed);
}

Groove RollaProcessor::copySource() const
{
    std::lock_guard<std::mutex> lock (grooveMu);
    return sourceReady.load (std::memory_order_relaxed) ? sourceGroove : groove;
}

void RollaProcessor::publishDerived()
{
    if (! sourceReady.load (std::memory_order_relaxed))
        return;
    const Groove src = copySource();
    std::uint32_t keep = 0;
    std::uint32_t sample = 0;
    for (int i = 0; i < NumParts; ++i)
    {
        if (raw (apvts, (juce::String (partId (i)) + "Keep").toRawUTF8()) >= 0.5f)
            keep |= 1u << i;
        if (i < Drums && slots.mode (i) != 0)
            sample |= 1u << i;
    }
    const auto plan = planIntensity ((int) raw (apvts, "dialect"),
                                     (int) raw (apvts, "intensity"),
                                     raw (apvts, "stab"));
    Groove dst;
    applyIntensity (src, dst, plan, keep, sample);
    publish (dst);
}

void RollaProcessor::reapplyIntensity()
{
    publishDerived();
}

void RollaProcessor::post (std::function<void (RollaProcessor&)> fn)
{
    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr || ! fn)
        return;
    std::weak_ptr<AsyncState> weak = async;
    const uint32_t epoch = async->epoch.load (std::memory_order_acquire);
    auto* self = this;
    mm->callAsync ([weak, epoch, self, fn]
    {
        auto locked = weak.lock();
        if (locked == nullptr)
            return;
        std::lock_guard<std::mutex> guard (locked->mu);
        if (! locked->alive.load() || locked->epoch.load() != epoch)
            return;
        if (self->restoring.load (std::memory_order_acquire))
            return;
        fn (*self);
    });
}

void RollaProcessor::deferredStartup()
{
    const double started = startupT0;
    try
    {
        Theme::warmTypefaces();
        if (startupStop.load())
            return;

        auto parsed = cachedParsedBank();
        if (startupStop.load())
            return;
        {
            std::lock_guard<std::mutex> lock (presetMu);
            if (presetGen.load (std::memory_order_relaxed) == 0 && parsed != nullptr)
                presets = std::make_shared<PresetBank> (*parsed);
        }
        presetGen.fetch_add (1, std::memory_order_release);

        if (! startupStop.load() && grooveEpoch.load (std::memory_order_relaxed) == 0)
        {
            const uint32_t seeds[NumParts] = { 0x0B17u, 0x51ABu, 0x19C3u, 0x77E1u, 0x0A11u, 0xBEEFu, 0x0909u };
            Groove initial {};
            generate (initial, currentSettings(), seeds, 0x7fu);
            publishInitial (initial);
        }

        if (! startupStop.load() && ! rootChosen.load())
        {
            // Bundle first; a saved root only if it is real audio outside a temp
            // folder; then ~/Music/ROLLA/Samples (SampleLibrary::resolveSampleRoot).
            const auto props = settings::open();
            const auto samples = SampleLibrary::resolveSampleRoot (props->getValue ("samplesRoot"));
            std::lock_guard<std::mutex> lock (rootSetupMu);
            if (! rootChosen.load())
            {
                library.setRoot (samples);
                library.startIndex (BinaryData::SAMPLE_MANIFEST_json, BinaryData::SAMPLE_MANIFEST_jsonSize);
                slots.setRoot (SampleLibrary::userSlotRootFor (samples));
                refreshSampleInventory();
            }
        }
        else if (! startupStop.load())
            refreshSampleInventory();
    }
    catch (...)
    {
    }

    startupMs.store (juce::Time::getMillisecondCounterHiRes() - started, std::memory_order_relaxed);
    startupDone.store (true);
    if (reshapeAfterStartup.exchange (false))
        requestReshape();
}

void RollaProcessor::applyDialect (int dialect)
{
    if (dialect < 0 || dialect >= NumDialects)
        return;
    const auto bank = sharePresets();
    if (bank == nullptr)
        return;
    const auto row = bank->dialect[dialect];
    auto setDenorm = [this] (const juce::String& id, float denorm)
    {
        if (auto* p = apvts.getParameter (id))
        {
            const float norm = p->convertTo0to1 (denorm);
            if (std::fabs (p->getValue() - norm) < 1.0e-4f)
                return;
            p->beginChangeGesture();
            p->setValueNotifyingHost (norm);
            p->endChangeGesture();
        }
    };
    for (int part = 0; part < NumParts; ++part)
    {
        const int idx = row.presetIndex[part];
        if (idx >= 0)
            setDenorm (juce::String (partId (part)) + "Preset", (float) idx);
    }
    setDenorm ("hitsMute", row.hitsMute ? 1.f : 0.f);
    setDenorm ("swing", row.swing);
}

void RollaProcessor::parameterChanged (const juce::String& parameterID, float newValue)
{
    if (restoring.load (std::memory_order_acquire))
        return;

    if (parameterID == "dialect")
    {
        const int dialect = (int) newValue;
        post ([dialect] (RollaProcessor& self)
        {
            self.applyDialect (dialect);
            // Style picks the presets and also the grid. Unkept lanes follow now,
            // then intensity arranges that new source.
            self.reshapeUnkept();
        });
        return;
    }

    if (parameterID == "dark" || parameterID == "busy" || parameterID == "bounce" || parameterID == "stab"
        || parameterID == "styleIntensity" || parameterID == "fill" || parameterID == "hitsInStab")
    {
        requestReshape();
        return;
    }

    if (parameterID == "intensity" || parameterID.endsWith ("Keep"))
    {
        post ([] (RollaProcessor& self) { self.reapplyIntensity(); });
        return;
    }

    if (newValue < 0.5f)
        return;

    int part = -1;
    if (parameterID != "roll")
    {
        for (int i = 0; i < NumParts; ++i)
            if (parameterID == juce::String (partId (i)) + "Shuffle")
                part = i;
        if (part < 0)
            return;
    }

    post ([part] (RollaProcessor& self)
    {
        if (part >= 0)
            self.clearAutoKeep (part);
        self.regenerate (part);
    });
}

void RollaProcessor::triggerRoll()
{
    if (auto* p = apvts.getParameter ("roll"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (0.f);
        p->setValueNotifyingHost (1.f);
        p->endChangeGesture();
    }
}

void RollaProcessor::setExclusiveSolo (int part)
{
    if (part < 0 || part >= NumParts)
        return;
    for (int i = 0; i < NumParts; ++i)
    {
        auto* p = apvts.getParameter (juce::String (partId (i)) + "Solo");
        const float target = i == part ? 1.f : 0.f;
        // Only touch lanes that change, so the host records no empty gestures.
        if (p == nullptr || (p->getValue() >= 0.5f) == (target >= 0.5f))
            continue;
        p->beginChangeGesture();
        p->setValueNotifyingHost (target);
        p->endChangeGesture();
    }
}

void RollaProcessor::triggerShuffle (int part)
{
    if (part < 0 || part >= NumParts)
        return;
    if (auto* p = apvts.getParameter (juce::String (partId (part)) + "Shuffle"))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (0.f);
        p->setValueNotifyingHost (1.f);
        p->endChangeGesture();
    }
}

Settings RollaProcessor::currentSettings() const
{
    Settings s;
    s.dark = raw (apvts, "dark");
    s.busy = raw (apvts, "busy");
    s.bounce = raw (apvts, "bounce");
    s.stab = raw (apvts, "stab");
    s.styleIntensity = raw (apvts, "styleIntensity");
    s.dialect = (int) raw (apvts, "dialect");
    s.key = (int) raw (apvts, "key");
    s.bars = barsFromIndex ((int) raw (apvts, "length"));
    s.fill = raw (apvts, "fill") >= 0.5f;
    s.hitsInStab = raw (apvts, "hitsInStab") >= 0.5f;
    return s;
}

void RollaProcessor::regenerate (int onlyPart)
{
    Settings s = currentSettings();
    const auto sound = sharePresets();
    Groove next = copySource();
    uint32_t seeds[NumParts] {};
    uint32_t mask = 0;
    int presetFor[NumParts];
    for (int i = 0; i < NumParts; ++i)
        presetFor[i] = (int) std::lround (raw (apvts, (juce::String (partId (i)) + "Preset").toRawUTF8()));

    for (int i = 0; i < NumParts; ++i)
    {
        seeds[i] = i == Drums ? next.drums.seed : next.lane[i].seed;
        if (seeds[i] == 0)
            seeds[i] = 1;

        const auto id = juce::String (partId (i));
        const bool keep = raw (apvts, (id + "Keep").toRawUTF8()) >= 0.5f;
        const bool mute = raw (apvts, (id + "Mute").toRawUTF8()) >= 0.5f;
        // ROLL skips kept and muted lanes. A lane's own shuffle (onlyPart) does not.
        const bool hit = onlyPart >= 0 ? (i == onlyPart) : (! keep && ! mute);
        if (hit)
        {
            seeds[i] = freshSeed();
            mask |= 1u << (unsigned) i;
            samplePick[i].store ((int) (seeds[i] % 10007u), std::memory_order_relaxed);
            // A re-rolled kit drops the last genre draw; applyGenreBrain may set new picks.
            if (i == Drums)
                for (auto& pick : drumPick)
                    pick.store (-1, std::memory_order_relaxed);
            const int picked = sound != nullptr ? pickPresetForRoll (*sound, i, s.dialect, seeds[i]) : -1;
            if (picked >= 0)
            {
                presetFor[i] = picked;
                if (auto* param = apvts.getParameter (juce::String (partId (i)) + "Preset"))
                {
                    const float norm = param->convertTo0to1 ((float) picked);
                    if (std::fabs (param->getValue() - norm) >= 1.0e-4f)
                        param->setValueNotifyingHost (norm);
                }
            }
        }
    }

    if (mask == 0)
        return;

    applyGenreBrain (mask, s, seeds, presetFor);
    reshapeLanes (next, s, seeds, mask);
    storeSource (next);
    publishDerived();
}

struct RollaProcessor::GenreBrainState
{
    genre::GenreTargets targets;
    genre::FeatureIndex features;
    genre::MeshRules mesh = genre::MeshRules::fromMixSpec();
    genre::Kit last;
};

// The genre brain picks each re-rolled melodic lane's file from its pool: one seeded
// kit draw weighted by fit to the style's targets (SoundSelector.h). The choice goes
// back as samplePick, so the existing pick path plays it. Drum voices draw in the
// same kit and go back as drumPick[voice] (takeDrum reads it for the voice's pool).
// No feature data (tests, CI, a build without Genre/): nothing changes.
void RollaProcessor::applyGenreBrain (std::uint32_t mask, const Settings& s, const std::uint32_t* seeds, const int* presetFor)
{
    if (genreBrain == nullptr)
    {
        // ROLLA_GENRE_DATA_DIR (box, tests), else <bundle>/Contents/Resources/Genre,
        // beside the injected sample pack. Not the host's executable folder.
        const auto env = juce::SystemStats::getEnvironmentVariable ("ROLLA_GENRE_DATA_DIR", {});
        const juce::File dir = env.isNotEmpty() ? juce::File (env) : SampleLibrary::bundledRoot().getSiblingFile ("Genre");
        genreBrain = std::make_shared<GenreBrainState>();
        genreBrain->features = genre::loadSampleFeatures (dir.getChildFile ("sample_features.json"));
        genreBrain->targets = genre::loadGenreTargets (dir.getChildFile ("genre_targets.json"));
        genreBrain->mesh = genre::loadMeshRules (dir.getChildFile ("mesh_rules.json"));
        if (genreBrain->features.size() > 0)
            juce::Logger::writeToLog ("ROLLA genre brain: " + juce::String ((int) genreBrain->features.size()) + " sample features, targets "
                                      + (genreBrain->targets.fromData ? "from data" : "neutral"));
        else if (genreBrain->features.fromData)
            // A file that parsed to nothing (wrong shape) must never fail silently.
            juce::Logger::writeToLog ("ROLLA genre brain: ERROR sample_features.json loaded 0 entries (wrong format?); "
                                      "sounds use the seed draw");
    }
    auto& genreState = *genreBrain;
    if (genreState.features.size() == 0)
        return;
    const auto index = library.index();
    const auto bank = sharePresets();
    if (index == nullptr || bank == nullptr)
        return;

    std::array<std::vector<genre::SampleFeatures>, genre::kLanes> pools;
    std::uint64_t seed = 0x9e3779b97f4a7c15ull;
    bool any = false;
    for (int part : { Bass, Stab, Hits, Pluck, Pad, Vox })
    {
        if ((mask & (1u << (unsigned) part)) == 0)
            continue;
        const auto& spec = sampleSpecFor (*bank, part, presetSlot (*bank, part, presetFor[part]));
        if (! spec.filter.on)
            continue; // a fixed preset file, not a pool
        const genre::Lane lane = genre::laneForPart (part, -1);
        const PoolFilter filter = poolFilterFor (spec, part, spec.filter.voice, s.dialect);
        pools[(size_t) lane] = genre::featurePool (*index, part, filter, genreState.features, lane);
        any = any || ! pools[(size_t) lane].empty();
        seed = (seed ^ seeds[part]) * 0x100000001b3ull;
    }

    // Drums: each voice's scanned pool joins the same draw, so the kick anchors the
    // pack tendency and meshes with the bass. A voice with an explicit voicePool keeps
    // today's pick. Rim shares the PERC lane with Perc, so Perc fills it and Rim keeps
    // the shared pick; the toms follow Perc's draw (same pool) with today's +1/+2 offset.
    std::array<int, genre::kLanes> drumVoiceOf {};
    drumVoiceOf.fill (-1);
    const bool drumsRolled = (mask & (1u << (unsigned) Drums)) != 0;
    const int drumKit = presetSlot (*bank, Drums, presetFor[Drums]);
    if (drumsRolled)
    {
        const auto& drums = sampleSpecFor (*bank, Drums, drumKit);
        for (int voice : { Kick, Snare, Clap, HatC, HatO, Ride, Perc })
        {
            if (! bank->drums[drumKit].voicePool[voice].empty())
                continue;
            const genre::Lane lane = genre::laneForPart (Drums, voice);
            if (lane == genre::Lane::Count || drumVoiceOf[(size_t) lane] >= 0)
                continue;
            const PoolFilter filter = poolFilterFor (drums, Drums, voice, s.dialect);
            pools[(size_t) lane] = genre::featurePool (*index, Drums, filter, genreState.features, lane);
            if (pools[(size_t) lane].empty())
                continue;
            drumVoiceOf[(size_t) lane] = voice;
            any = true;
        }
        seed = (seed ^ seeds[Drums]) * 0x100000001b3ull;
    }
    if (! any)
        return;

    const auto style = (genre::Style) std::clamp (s.dialect, 0, genre::kStyles - 1);
    // Meshing (KitMesh.h): a candidate that clashes with the lanes already drawn
    // (kick/bass sub, bass/stab low mids, stab/pad crowding, key fit after retune)
    // is re-drawn. The roll's key; natural minor until the scale picker lands.
    genre::MeshContext ctx;
    ctx.keyRootPc = ((s.key % 12) + 12) % 12;
    ctx.scaleMask = genre::kScaleMinor;
    ctx.degreeWeights = genreState.targets.lane (style, genre::Lane::Stab).pcWeights;
    ctx.chordAsk = true;
    ctx.rules = genreState.mesh;
    genre::SelectorOptions opt;
    opt.pairPenalty = genre::pairPenaltyHook (ctx);
    opt.maxTries = 8;
    const genre::Kit kit = genre::drawKit (pools, style, genreState.targets, seed, &genreState.last, opt);
    for (int part : { Bass, Stab, Hits, Pluck, Pad, Vox })
    {
        const genre::Lane lane = genre::laneForPart (part, -1);
        const int chosen = kit.at (lane);
        if ((mask & (1u << (unsigned) part)) == 0 || chosen < 0)
            continue;
        samplePick[part].store (chosen, std::memory_order_relaxed);
        genreState.last.pick[(size_t) lane] = chosen;
    }
    for (int li = 0; li < genre::kLanes; ++li)
    {
        const int voice = drumVoiceOf[(size_t) li];
        const int chosen = kit.pick[(size_t) li];
        if (voice < 0 || chosen < 0)
            continue;
        drumPick[voice].store (chosen, std::memory_order_relaxed);
        genreState.last.pick[(size_t) li] = chosen;
        if (voice != Perc)
            continue;
        for (int tom : { TomMid, TomLow })
            if (bank->drums[drumKit].voicePool[tom].empty())
                drumPick[tom].store (chosen + (tom == TomMid ? 1 : 2), std::memory_order_relaxed);
    }
}

void RollaProcessor::requestReshape()
{
    if (restoring.load (std::memory_order_acquire))
        return;
    if (! startupDone.load (std::memory_order_acquire))
    {
        reshapeAfterStartup.store (true);
        return;
    }

    auto* mm = juce::MessageManager::getInstanceWithoutCreating();
    if (mm == nullptr)
        return;
    if (reshapePending.exchange (true))
        return;

    std::weak_ptr<AsyncState> weak = async;
    const uint32_t epoch = async->epoch.load (std::memory_order_acquire);
    auto* self = this;
    mm->callAsync ([weak, epoch, self]
    {
        auto locked = weak.lock();
        if (locked == nullptr)
            return;
        std::lock_guard<std::mutex> guard (locked->mu);
        self->reshapePending.store (false);
        if (! locked->alive.load() || locked->epoch.load() != epoch)
            return;
        if (self->restoring.load (std::memory_order_acquire))
            return;
        self->reshapeUnkept();
    });
}

void RollaProcessor::reshapeUnkept()
{
    if (restoring.load (std::memory_order_acquire) || ! startupDone.load (std::memory_order_acquire))
        return;

    Settings s = currentSettings();
    // Reshape the source phrase. The playing groove is that phrase after
    // planIntensity / applyIntensity, and a kept lane there may already be
    // shortened. Starting from the source keeps the full roll.
    Groove next = copySource();
    uint32_t seeds[NumParts] {};
    uint32_t mask = 0;

    for (int i = 0; i < NumParts; ++i)
    {
        seeds[i] = i == Drums ? next.drums.seed : next.lane[i].seed;
        if (seeds[i] == 0)
            seeds[i] = 1;
        const bool keep = raw (apvts, (juce::String (partId (i)) + "Keep").toRawUTF8()) >= 0.5f;
        if (! keep)
            mask |= 1u << (unsigned) i;
    }

    if (mask == 0)
        return;

    reshapeLanes (next, s, seeds, mask);
    storeSource (next);
    publishDerived();
}

void RollaProcessor::publish (const Groove& g)
{
    std::lock_guard<std::mutex> lock (grooveMu);
    groove = g;
    grooveEpoch.fetch_add (1, std::memory_order_relaxed);
}

Groove RollaProcessor::copyGroove() const
{
    std::lock_guard<std::mutex> lock (grooveMu);
    return groove;
}

void RollaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedRate = sampleRate > 1000.0 ? sampleRate : 48000.0;
    engine.prepare (preparedRate, samplesPerBlock);
    setLatencySamples (engine.latencySamples());
    phraseOffset = 0.0;
    hostRolling = false;
    midiLate.clear();
    midiNow.clear();
    midiNext.clear();
    midiLate.ensureSize (8192);
    midiNow.ensureSize (8192);
    midiNext.ensureSize (8192);
    applyRememberedSamples();
}

void RollaProcessor::releaseResources()
{
    engine.reset();
}

bool RollaProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.outputBuses.size() > 1)
    {
        const auto& drums = layouts.outputBuses.getReference (1);
        if (! drums.isDisabled() && drums != juce::AudioChannelSet::stereo())
            return false;
    }

    const auto input = layouts.getMainInputChannelSet();
    return input.isDisabled();
}

EngineParams RollaProcessor::readParams (std::shared_ptr<const PresetBank>& hold) const
{
    {
        std::lock_guard<std::mutex> lock (presetMu);
        hold = presets;
    }
    EngineParams params;
    params.presets = hold.get();
    params.mix = mixProgram.loaded ? &mixProgram : nullptr;
    params.duck = raw (apvts, "duck") >= 0.5f;
    params.drumsToMain = raw (apvts, "drumsToMain") >= 0.5f;
    params.swing = raw (apvts, "swing");
    params.outputDb = raw (apvts, "output");
    params.drumsAccent = raw (apvts, "drumsAccent");
    params.bars = barsFromIndex ((int) raw (apvts, "length"));
    params.key = (int) raw (apvts, "key");
    params.dialect = (int) raw (apvts, "dialect");
    params.library = &library;
    params.userSlots = slots.published();
    params.sampleShaping = true;
    for (int i = 0; i < NumParts; ++i)
    {
        const auto id = juce::String (partId (i));
        params.tone[i] = raw (apvts, (id + "Tone").toRawUTF8());
        params.punch[i] = raw (apvts, (id + "Punch").toRawUTF8());
        params.levelDb[i] = raw (apvts, (id + "Level").toRawUTF8());
        params.mute[i] = raw (apvts, (id + "Mute").toRawUTF8()) >= 0.5f;
        params.preset[i] = (int) raw (apvts, (id + "Preset").toRawUTF8());
        params.samplePick[i] = samplePick[i].load (std::memory_order_relaxed);
    }
    // Solo is folded in here, after the mute read, so playback, the drums bus, the full
    // bounce and the stems all hear it. regenerate() reads the raw Mute parameter, so
    // ROLL still treats a lane that is only silenced by someone else's solo as unmuted.
    bool solo[NumParts] {};
    for (int i = 0; i < NumParts; ++i)
        solo[i] = raw (apvts, (juce::String (partId (i)) + "Solo").toRawUTF8()) >= 0.5f;
    foldSolo (params.mute, solo);
    params.kickKeyFollow = true;
    for (int v = 0; v < NumDrumVoices; ++v)
    {
        params.drumPick[v] = drumPick[v].load (std::memory_order_relaxed);
        const auto id = juce::String (v);
        params.drumTuneSt[v] = raw (apvts, ("dTune" + id).toRawUTF8());
        params.drumDecay[v] = raw (apvts, ("dDecay" + id).toRawUTF8());
        params.drumLevelDb[v] = raw (apvts, ("dLevel" + id).toRawUTF8());
        params.drumAccentOn[v] = raw (apvts, ("dAccent" + id).toRawUTF8()) >= 0.5f;
        params.drumAccentAmt[v] = raw (apvts, ("dAccAmt" + id).toRawUTF8());
    }
    return params;
}

namespace
{
// A backward host jump while playing is a host loop wrap (not a seek) when it
// lands on the loop start from the loop end. Without loop points (FL's pattern
// loop) it has to land on a bar line, back by a whole number of bars.
bool isHostLoopWrap (const juce::AudioPlayHead::PositionInfo& pos, double expect, double now, double slack)
{
    const double back = expect - now;
    if (back <= slack)
        return false;
    if (pos.getIsLooping())
        if (const auto loop = pos.getLoopPoints())
            if (loop->ppqEnd - loop->ppqStart > slack)
                return std::abs (now - loop->ppqStart) <= slack && std::abs (expect - loop->ppqEnd) <= slack;
    double barBeats = 4.0;
    if (const auto sig = pos.getTimeSignature())
        if (sig->numerator > 0 && sig->denominator > 0)
            barBeats = sig->numerator * 4.0 / sig->denominator;
    const double bars = std::round (back / barBeats);
    const double intoBar = wrapLoop (now, barBeats);
    return bars >= 1.0 && std::abs (back - bars * barBeats) <= slack
           && (intoBar <= slack || barBeats - intoBar <= slack);
}
} // namespace

void RollaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.clear (ch, 0, n);

    if (n <= 0)
        return;

    float bpm = 130.f;
    bool hostPlaying = false;
    double hostPpq = 0.0;
    juce::Optional<juce::AudioPlayHead::PositionInfo> pos;
    if (auto* head = getPlayHead())
        pos = head->getPosition();
    if (pos)
    {
        if (auto b = pos->getBpm())
            bpm = (float) std::clamp (*b, 20.0, 400.0);
        hostPlaying = pos->getIsPlaying();
        if (auto ppq = pos->getPpqPosition())
            hostPpq = *ppq;
    }

    // Free-run phrase: follow the host, but play through its loop wraps. Any
    // other jump (a seek) or a fresh start goes back to following the host.
    if (hostPlaying)
    {
        const double blockBeats = (double) n * (double) bpm / (60.0 * preparedRate);
        if (! hostRolling)
            phraseOffset = 0.0;
        else
        {
            const double slack = std::max (blockBeats, hostLastBeats) + 1.0e-3;
            const double drift = hostPpq - hostExpectPpq;
            // Lengths are 2, 4 or 8 bars, so the offset can wrap at 8 bars.
            if (std::abs (drift) > slack)
                phraseOffset = drift < 0.0 && isHostLoopWrap (*pos, hostExpectPpq, hostPpq, slack)
                                   ? wrapLoop (phraseOffset - drift, (double) kMaxBars * 4.0)
                                   : 0.0;
        }
        hostExpectPpq = hostPpq + blockBeats;
        hostLastBeats = blockBeats;
    }
    hostRolling = hostPlaying;

    const bool previewOn = preview.load (std::memory_order_relaxed);
    Transport transport;
    transport.sampleRate = preparedRate;
    transport.bpm = bpm;
    if (hostPlaying)
    {
        transport.playing = true;
        transport.ppqStart = hostPpq + phraseOffset;
    }
    else if (previewOn)
    {
        transport.playing = true;
        transport.ppqStart = previewPpq;
        previewPpq += (double) n * (double) bpm / 60.0;
    }
    else
    {
        transport.playing = false;
        previewPpq = 0.0;
    }

    lastBpm.store (bpm);
    playPpq.store (transport.playing ? transport.ppqStart : 0.0);
    uiPlaying.store (transport.playing);

    std::shared_ptr<const PresetBank> heldPresets;
    EngineParams params = readParams (heldPresets);
    // The index must stay alive for this block. readParams is shared with the
    // offline bounce, which holds its own copy.
    const auto indexHold = library.index();
    params.sampleIndex = indexHold.get();
    params.bpm = bpm;

    float* mainL = buffer.getWritePointer (0);
    float* mainR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    float* drumL = nullptr;
    float* drumR = nullptr;
    if (auto* bus = getBus (false, 1); bus != nullptr && bus->isEnabled())
    {
        const int start = getChannelIndexInProcessBlockBuffer (false, 1, 0);
        if (start >= 0 && start + 1 < buffer.getNumChannels())
        {
            drumL = buffer.getWritePointer (start);
            drumR = buffer.getWritePointer (start + 1);
        }
    }

    const Groove local = copyGroove();
    engine.process (local, params, transport, mainL, mainR, drumL, drumR, n, midi, true);
    delayMidiOut (midi, n);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* data = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
            data[i] = flush (data[i]);
    }
}

// The master tp_guard delays the audio by its latency, and the host shifts the
// whole plugin back by setLatencySamples. The pattern MIDI has to ride the same
// delay, or it lands that many samples ahead of the sound.
void RollaProcessor::delayMidiOut (juce::MidiBuffer& midi, int n)
{
    const int lat = getLatencySamples();
    if (lat <= 0)
        return;
    midiNow.clear();
    midiNext.clear();
    for (const auto meta : midiLate)
    {
        if (meta.samplePosition < n)
            midiNow.addEvent (meta.getMessage(), meta.samplePosition);
        else
            midiNext.addEvent (meta.getMessage(), meta.samplePosition - n);
    }
    for (const auto meta : midi)
    {
        const int at = meta.samplePosition + lat;
        if (at < n)
            midiNow.addEvent (meta.getMessage(), at);
        else
            midiNext.addEvent (meta.getMessage(), at - n);
    }
    midi.swapWith (midiNow);
    midiLate.swapWith (midiNext);
}

void RollaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree tree = apvts.copyState();
    const Groove local = sourceReady.load (std::memory_order_relaxed) ? copySource() : copyGroove();
    tree.setProperty ("grooveBytes", (int) sizeof (Groove), nullptr);
    tree.setProperty ("groove", encodeBlock (&local, sizeof (Groove)), nullptr);
    int editedMask = 0;
    for (int i = 0; i < NumParts; ++i)
        if (edited[i] != 0)
            editedMask |= 1 << i;
    tree.setProperty ("editedMask", editedMask, nullptr);
    if (voxSampleName.isNotEmpty())
        tree.setProperty ("voxSampleName", voxSampleName, nullptr);

    const int editorW = editorWidth.load (std::memory_order_relaxed);
    const int editorH = editorHeight.load (std::memory_order_relaxed);
    if (editorW > 0 && editorH > 0)
    {
        tree.setProperty ("editorWidth", editorW, nullptr);
        tree.setProperty ("editorHeight", editorH, nullptr);
        for (int i = 0; i < NumParts; ++i)
            tree.setProperty ("samplePick" + juce::String (i), samplePick[i].load(), nullptr);
    }
    // Only drawn voices are written, so a session without genre picks saves as before.
    for (int v = 0; v < NumDrumVoices; ++v)
    {
        const auto key = "drumPick" + juce::String (v);
        const int pick = drumPick[v].load (std::memory_order_relaxed);
        if (pick >= 0)
            tree.setProperty (key, pick, nullptr);
        else
            tree.removeProperty (key, nullptr);
    }
    {
        std::lock_guard<std::mutex> lock (sampleMu);
        for (int v = 0; v < NumDrumVoices; ++v)
        {
            const auto key = "sample" + juce::String (v);
            const auto rateKey = "sampleRate" + juce::String (v);
            if (! remembered[v].set || remembered[v].mono.empty())
            {
                tree.removeProperty (key, nullptr);
                tree.removeProperty (rateKey, nullptr);
                tree.removeProperty ("sampleName" + juce::String (v), nullptr);
                continue;
            }
            tree.setProperty (key, encodeBlock (remembered[v].mono.data(), remembered[v].mono.size() * sizeof (float)), nullptr);
            tree.setProperty (rateKey, remembered[v].rate, nullptr);
            tree.setProperty ("sampleN" + juce::String (v), (int) remembered[v].mono.size(), nullptr);
            tree.setProperty ("sampleName" + juce::String (v), remembered[v].name, nullptr);
        }
    }

    slots.writeState (tree);

    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void RollaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr)
        return;
    auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.isValid())
        return;

    {
        std::lock_guard<std::mutex> lock (async->mu);
        async->epoch.fetch_add (1);
        restoring.store (true);
    }

    apvts.replaceState (tree);

    if ((int) tree.getProperty ("grooveBytes") == (int) sizeof (Groove))
    {
        Groove restored {};
        if (decodeBlock (tree.getProperty ("groove").toString(), &restored, sizeof (Groove)))
            storeSource (restored);
    }

    {
        std::lock_guard<std::mutex> lock (sampleMu);
        for (int v = 0; v < NumDrumVoices; ++v)
        {
            const int n = (int) tree.getProperty ("sampleN" + juce::String (v));
            const double rate = (double) tree.getProperty ("sampleRate" + juce::String (v));
            remembered[v] = {};
            if (n > 8 && n < 2000000)
            {
                remembered[v].mono.resize ((size_t) n);
                if (decodeBlock (tree.getProperty ("sample" + juce::String (v)).toString(),
                                 remembered[v].mono.data(),
                                 (size_t) n * sizeof (float)))
                {
                    remembered[v].rate = rate > 1000.0 ? rate : 48000.0;
                    remembered[v].set = true;
                    remembered[v].name = tree.getProperty ("sampleName" + juce::String (v)).toString();
                }
                else
                    remembered[v] = {};
            }
        }
    }

    if (preparedRate > 1000.0)
        applyRememberedSamples();

    const int editedMask = (int) tree.getProperty ("editedMask");
    for (int i = 0; i < NumParts; ++i)
        edited[i] = (editedMask & (1 << i)) != 0 ? 1 : 0;
    voxSampleName = tree.getProperty ("voxSampleName").toString();

    if (tree.hasProperty ("editorWidth") && tree.hasProperty ("editorHeight"))
    {
        const int w = (int) tree.getProperty ("editorWidth");
        const int h = (int) tree.getProperty ("editorHeight");
        if (w >= 768 && h >= 480 && w <= 2048 && h <= 1280)
        {
            editorWidth.store (w, std::memory_order_relaxed);
            editorHeight.store (h, std::memory_order_relaxed);
            projectEditorSerial.fetch_add (1, std::memory_order_relaxed);
        }
    }

    for (int i = 0; i < NumParts; ++i)
        if (tree.hasProperty ("samplePick" + juce::String (i)))
            samplePick[i].store ((int) tree.getProperty ("samplePick" + juce::String (i)), std::memory_order_relaxed);
    // An old session has no drumPick: every voice goes back to the shared drum pick.
    for (int v = 0; v < NumDrumVoices; ++v)
    {
        const auto key = "drumPick" + juce::String (v);
        const int pick = tree.hasProperty (key) ? (int) tree.getProperty (key) : -1;
        drumPick[v].store (pick < 0 ? -1 : pick, std::memory_order_relaxed);
    }

    slots.readState (tree);
    // The sound panel is editor-only. A project load closes it; it is not restored.
    projectEditorSerial.fetch_add (1, std::memory_order_relaxed);

    restoring.store (false);
    if (sourceReady.load (std::memory_order_relaxed))
        publishDerived();
}

bool RollaProcessor::laneSampleMissing (int part) const
{
    if (part < 0 || part >= NumParts)
        return false;
    if (part == Drums)
    {
        for (int slot = SlotKick; slot <= SlotPerc; ++slot)
            if (slots.warning (slot))
                return true;
    }
    else if (slots.mode (part) != 0)
        return slots.warning (part);

    const auto bank = sharePresets();
    if (bank == nullptr)
        return false;
    const int preset = presetSlot (*bank, part, (int) raw (apvts, (juce::String (partId (part)) + "Preset").toRawUTF8()));
    const auto& spec = sampleSpecFor (*bank, part, preset);
    if (spec.mode == SampleMode::Synth || spec.filter.on)
        return false;

    auto absent = [&] (const char* file)
    {
        return file == nullptr || file[0] == 0 || library.missing (file);
    };

    if (part == Drums)
    {
        bool named = false;
        for (int v = 0; v < NumDrumVoices; ++v)
        {
            const int slot = userSlotForDrum (v);
            if (slot >= 0 && slots.mode (slot) != 0)
                continue;
            const int pick = samplePick[part].load (std::memory_order_relaxed);
            const char* file = drumVoiceChoice (bank->drums[preset], v, pick);
            if (file == nullptr || file[0] == 0)
                continue;
            named = true;
            if (library.missing (file))
                return true;
        }
        if (named)
            return false;
    }

    const int pick = samplePick[part].load (std::memory_order_relaxed);
    if (spec.zoneCount > 0)
    {
        for (int i = 0; i < spec.zoneCount; ++i)
            if (absent (spec.zones[i].file))
                return true;
        return false;
    }
    const auto choice = chooseSample (spec, spec.root, pick, 0);
    return absent (choice.file);
}

void RollaProcessor::setSamplesRoot (const juce::File& folder)
{
    if (folder == juce::File())
        return;
    rootChosen.store (true);
    std::lock_guard<std::mutex> lock (rootSetupMu);
    library.setRoot (folder);
    library.startIndex (BinaryData::SAMPLE_MANIFEST_json, BinaryData::SAMPLE_MANIFEST_jsonSize);
    slots.setRoot (SampleLibrary::userSlotRootFor (folder));
    refreshSampleInventory();
    // A temp folder (a test's tone pack) plays for this session but is never saved.
    if (settings::isTempLocation (folder))
        return;
    const auto props = settings::open();
    props->setValue ("samplesRoot", folder.getFullPathName());
    props->saveIfNeeded();
}

juce::File RollaProcessor::samplesRoot() const
{
    return library.root();
}

void RollaProcessor::refreshSampleInventory()
{
    const int found = library.indexedFound();
    if (found < 0)
        return;
    samplesFoundCount.store (found, std::memory_order_relaxed);
    samplesMissingCount.store (library.indexedMissing(), std::memory_order_relaxed);
}

juce::String RollaProcessor::sampleInventoryText() const
{
    const int found = samplesFound();
    const int missing = samplesMissing();
    if (found < 0 || missing < 0)
        return "samples …";
    return juce::String (found) + " found / " + juce::String (missing) + " missing";
}

RollaProcessor::SamplesStatus RollaProcessor::samplesStatus() const
{
    SamplesStatus st;
    st.root = samplesRoot();
    st.found = samplesFound();
    const auto bundle = SampleLibrary::bundledRoot();
    st.bundled = st.root.getFullPathName().isNotEmpty() && st.root == bundle;
    if (st.found == 0 || ! st.bundled)
        st.message = "Samples not loaded: " + st.root.getFullPathName()
                   + " (" + juce::String (juce::jmax (0, st.found)) + " files)";
    return st;
}

void RollaProcessor::setEditorSize (int width, int height) noexcept
{
    editorWidth.store (width, std::memory_order_relaxed);
    editorHeight.store (height, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* RollaProcessor::createEditor()
{
    return new RollaEditor (*this);
}

void RollaProcessor::markEdited (int part)
{
    if (part < 0 || part >= NumParts || restoring.load())
        return;
    edited[part] = 1;
    if (auto* p = apvts.getParameter (juce::String (partId (part)) + "Keep"))
    {
        if (p->getValue() >= 0.5f)
            return;
        undo.beginNewTransaction();
        p->beginChangeGesture();
        p->setValueNotifyingHost (1.f);
        p->endChangeGesture();
    }
}

bool RollaProcessor::laneEdited (int part) const noexcept
{
    return part >= 0 && part < NumParts && edited[part] != 0;
}

void RollaProcessor::clearEdited (int part) noexcept
{
    if (part >= 0 && part < NumParts)
        edited[part] = 0;
}

void RollaProcessor::clearAutoKeep (int part)
{
    if (part < 0 || part >= NumParts || ! laneEdited (part))
        return;
    clearEdited (part);
    if (auto* keep = apvts.getParameter (juce::String (partId (part)) + "Keep"))
    {
        if (keep->getValue() < 0.5f)
            return;
        keep->beginChangeGesture();
        keep->setValueNotifyingHost (0.f);
        keep->endChangeGesture();
    }
}

bool RollaProcessor::loadVoxFile (const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Could not read " + file.getFileName();
        return false;
    }
    voxSampleName = file.getFileName();
    markEdited (Vox);
    error = voxSampleName;
    return true;
}

void RollaProcessor::clearVoxSample()
{
    voxSampleName.clear();
}

bool RollaProcessor::loadUserSlot (int slot, const juce::File& file, juce::String& error)
{
    const bool ok = slots.assign (slot, file, error);
    if (ok && ! restoring.load (std::memory_order_acquire))
        reapplyIntensity();
    return ok;
}

void RollaProcessor::clearUserSlot (int slot)
{
    slots.clear (slot);
    if (! restoring.load (std::memory_order_acquire))
        reapplyIntensity();
}

void RollaProcessor::setUserSlotMode (int slot, int mode)
{
    slots.setMode (slot, mode);
    if (! restoring.load (std::memory_order_acquire))
        reapplyIntensity();
}

int RollaProcessor::userSlotMode (int slot) const noexcept
{
    return slots.mode (slot);
}

bool RollaProcessor::userSlotWarning (int slot) const noexcept
{
    return slots.warning (slot);
}

juce::String RollaProcessor::userSlotFile (int slot) const
{
    return slots.file (slot);
}

int RollaProcessor::userSlotRoot (int slot) const
{
    return slots.rootNote (slot);
}

void RollaProcessor::setUserSlotRoot (int slot, int midiNote, float cents)
{
    slots.setRootNote (slot, midiNote, cents);
}

juce::String RollaProcessor::userSlotError (int slot) const
{
    return slots.lastError (slot);
}

namespace
{
std::atomic<int> exportSerial { 1 };

float hostBpmOf (const RollaProcessor& proc)
{
    float bpm = proc.getPlayBpm();
    if (auto* head = proc.getPlayHead())
        if (auto pos = head->getPosition())
            if (auto b = pos->getBpm())
                bpm = (float) *b;
    if (bpm < 20.f || bpm > 400.f)
        bpm = 130.f;
    return bpm;
}
}

juce::String RollaProcessor::exportMidiTo (const juce::File& dir)
{
    auto result = dir.createDirectory();
    if (result.failed())
        return "Could not create " + dir.getFullPathName();

    waitUntilReady();
    const Groove local = exportGroove();
    const int key = (int) raw (apvts, "key");
    const float swing = raw (apvts, "swing");
    const double bpm = (double) hostBpmOf (*this);
    std::string error;
    // Every lane gets its file, an empty one too (a loop-long track with the
    // tempo), so a folder export always has the same 7 lanes + full to drop into FL.
    for (int p = 0; p < NumParts; ++p)
    {
        const auto file = dir.getChildFile (rollexport::baseName (local, key, bpm, p) + ".mid");
        if (! MidiExport::writePart (file.getFullPathName().toStdString(), local, p, key, swing, bpm, error))
            return juce::String (error);
    }
    const auto all = dir.getChildFile (rollexport::baseName (local, key, bpm, rollexport::kFull) + ".mid");
    if (! MidiExport::writeMultitrack (all.getFullPathName().toStdString(), local, key, swing, bpm, error))
        return juce::String (error);
    return dir.getFullPathName();
}

void RollaProcessor::rememberSample (int voice, const float* mono, int n, double rate, const juce::String& name)
{
    if (voice < 0 || voice >= NumDrumVoices)
        return;
    std::lock_guard<std::mutex> lock (sampleMu);
    remembered[voice].mono.assign (mono, mono + n);
    remembered[voice].rate = rate;
    remembered[voice].set = true;
    remembered[voice].name = name;
}

void RollaProcessor::applyRememberedSamples()
{
    std::lock_guard<std::mutex> lock (sampleMu);
    for (int v = 0; v < NumDrumVoices; ++v)
    {
        if (remembered[v].set && ! remembered[v].mono.empty())
            engine.loadDrumSample (v, remembered[v].mono.data(), (int) remembered[v].mono.size(), remembered[v].rate);
        else
            engine.clearDrumSample (v);
    }
}

void RollaProcessor::setDrumSampleSlot (DrumSampleSlot* slot) noexcept
{
    drumSlot.store (slot, std::memory_order_relaxed);
    engine.setDrumSampleSlot (slot);
}

bool RollaProcessor::loadDrumFile (int voice, const juce::File& file, juce::String& error)
{
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Could not read " + file.getFileName();
        return false;
    }
    const double rate = reader->sampleRate;
    const auto maxSamples = (juce::int64) std::min (reader->lengthInSamples, (juce::int64) (rate * 2.0));
    if (maxSamples < 16)
    {
        error = "Sample is too short.";
        return false;
    }
    juce::AudioBuffer<float> buf ((int) reader->numChannels, (int) maxSamples);
    reader->read (&buf, 0, (int) maxSamples, 0, true, true);
    std::vector<float> mono ((size_t) maxSamples, 0.f);
    const float inv = 1.f / (float) std::max (1, buf.getNumChannels());
    for (int i = 0; i < (int) maxSamples; ++i)
    {
        float s = 0.f;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            s += buf.getSample (ch, i);
        mono[(size_t) i] = s * inv;
    }
    rememberSample (voice, mono.data(), (int) mono.size(), rate, file.getFileName());
    if (preparedRate > 1000.0)
        engine.loadDrumSample (voice, mono.data(), (int) mono.size(), rate);
    error = file.getFileName();
    return true;
}

void RollaProcessor::clearDrumVoice (int voice)
{
    if (voice < 0 || voice >= NumDrumVoices)
        return;
    {
        std::lock_guard<std::mutex> lock (sampleMu);
        remembered[voice] = {};
    }
    engine.clearDrumSample (voice);
}

bool RollaProcessor::drumVoiceLoaded (int voice) const noexcept
{
    return engine.hasDrumSample (voice);
}

juce::String RollaProcessor::getDrumSampleName (int voice) const
{
    if (voice < 0 || voice >= NumDrumVoices)
        return {};
    std::lock_guard<std::mutex> lock (sampleMu);
    return remembered[voice].name;
}

juce::String RollaProcessor::exportMidiToDocuments()
{
    return exportMidiTo (juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("ROLLA"));
}

juce::File RollaProcessor::exportCache() const
{
    juce::File dir = settings::cacheOverride();
    if (dir != juce::File())
        dir = dir.getChildFile ("ROLLA");
    else
    {
       #if JUCE_MAC
        dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                  .getChildFile ("Library").getChildFile ("Caches").getChildFile ("ROLLA");
       #elif JUCE_WINDOWS
        dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                  .getChildFile ("ROLLA").getChildFile ("Cache");
       #else
        dir = juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                  .getChildFile (".cache").getChildFile ("ROLLA");
       #endif
    }
    dir.createDirectory();
    return dir;
}

void RollaProcessor::pruneExportCache (const juce::File& keep) const
{
    // Drags leave their files behind for the host to copy. Anything older than
    // 30 minutes is gone; the folder of the drag in progress always stays.
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::minutes (30.0);
    const auto keepDir = keep.isDirectory() ? keep : keep.getParentDirectory();
    for (const auto& entry : juce::RangedDirectoryIterator (exportCache(), false, "*", juce::File::findFilesAndDirectories))
    {
        const auto file = entry.getFile();
        if (file == keep || file == keepDir)
            continue;
        if (file.getLastModificationTime() >= cutoff)
            continue;
        if (file.isDirectory())
            file.deleteRecursively();
        else if (file.getFileName().startsWith ("ROLLA_"))
            file.deleteFile();
    }
}

juce::File RollaProcessor::newExportFolder() const
{
    const auto dir = exportCache().getChildFile (juce::String (juce::Time::currentTimeMillis()) + "_"
                                                 + juce::String (exportSerial.fetch_add (1)));
    dir.createDirectory();
    return dir;
}

Groove RollaProcessor::exportGroove() const
{
    std::shared_ptr<const PresetBank> held;
    const EngineParams params = readParams (held);
    return rollexport::midiGroove (rollexport::atLength (copyGroove(), params.bars), params.presets,
                                   params.preset[Bass], params.dialect);
}

juce::StringArray RollaProcessor::writeTempMidi()
{
    juce::StringArray files;
    waitUntilReady();
    const Groove local = exportGroove();
    const int key = (int) raw (apvts, "key");
    const double bpm = (double) hostBpmOf (*this);
    const auto file = newExportFolder().getChildFile (rollexport::baseName (local, key, bpm, rollexport::kFull) + ".mid");
    std::string error;
    if (! MidiExport::writeMultitrack (file.getFullPathName().toStdString(), local, key, raw (apvts, "swing"), bpm, error))
        return files;
    pruneExportCache (file);
    files.add (file.getFullPathName());
    return files;
}

juce::StringArray RollaProcessor::writeLaneMidiFiles()
{
    juce::StringArray files;
    waitUntilReady();
    const Groove local = exportGroove();
    const int key = (int) raw (apvts, "key");
    const float swing = raw (apvts, "swing");
    const double bpm = (double) hostBpmOf (*this);
    const auto dir = newExportFolder();
    std::string error;
    for (int part = 0; part < NumParts; ++part)
    {
        const auto file = dir.getChildFile (rollexport::baseName (local, key, bpm, part) + ".mid");
        if (! MidiExport::writePart (file.getFullPathName().toStdString(), local, part, key, swing, bpm, error))
            return {};
        files.add (file.getFullPathName());
    }
    pruneExportCache (dir);
    return files;
}

juce::StringArray RollaProcessor::writeLaneMidi (int part)
{
    juce::StringArray files;
    if (part < 0 || part >= NumParts)
        return files;
    waitUntilReady();
    const Groove local = exportGroove();
    const int key = (int) raw (apvts, "key");
    const double bpm = (double) hostBpmOf (*this);
    const auto file = newExportFolder().getChildFile (rollexport::baseName (local, key, bpm, part) + ".mid");
    std::string error;
    if (! MidiExport::writePart (file.getFullPathName().toStdString(), local, part, key, raw (apvts, "swing"), bpm, error))
        return files;
    pruneExportCache (file);
    files.add (file.getFullPathName());
    return files;
}

bool RollaProcessor::writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sampleRate) const
{
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    if (auto* fileStream = dynamic_cast<juce::FileOutputStream*> (stream.get()))
        if (fileStream->failedToOpen())
            return false;

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriter::Options{}
                             .withSampleRate (sampleRate)
                             .withNumChannels (audio.getNumChannels())
                             .withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return false;
    return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

void RollaProcessor::prepareOffline (Engine& offline, double rate) const
{
    offline.prepare (rate, 1024);
    offline.setDrumSampleSlot (drumSlot.load (std::memory_order_relaxed));
    std::lock_guard<std::mutex> lock (sampleMu);
    for (int v = 0; v < NumDrumVoices; ++v)
        if (remembered[v].set && ! remembered[v].mono.empty())
            offline.loadDrumSample (v, remembered[v].mono.data(), (int) remembered[v].mono.size(), remembered[v].rate);
}

juce::AudioBuffer<float> RollaProcessor::bounceLoop (Engine& offline, const Groove& g, int part, double rate, double bpm) const
{
    std::shared_ptr<const PresetBank> held;
    EngineParams params = readParams (held);
    const auto indexHold = library.index();
    params.sampleIndex = indexHold.get();
    params.bpm = (float) bpm;
    if (part >= 0 && part < NumParts)
    {
        // A stem is that lane alone. Solo renders bypass E21 (MIX_SPEC §9.4), so
        // the stems keep the lane balance the mix program sets.
        // A lane the user muted stays muted: a muted lane never sounds.
        for (int i = 0; i < NumParts; ++i)
            params.mute[i] = params.mute[i] || i != part;
        params.bypassSumTrim = true;
    }
    return rollexport::renderLoop (offline, g, params, rate, bpm, &library).audio;
}

std::vector<juce::AudioBuffer<float>> RollaProcessor::bounceParts (const Groove& g, const std::vector<int>& parts,
                                                                    double rate, double bpm) const
{
    // One engine per stem, all at once: an 8-bar stem is several seconds of
    // offline audio, and the drag waits for every file.
    std::vector<juce::AudioBuffer<float>> out (parts.size());
    std::vector<std::thread> workers;
    workers.reserve (parts.size());
    for (size_t i = 0; i < parts.size(); ++i)
        workers.emplace_back ([this, &out, &g, &parts, i, rate, bpm]
        {
            Engine offline;
            prepareOffline (offline, rate);
            out[i] = bounceLoop (offline, g, parts[i], rate, bpm);
        });
    for (auto& w : workers)
        w.join();
    return out;
}

juce::StringArray RollaProcessor::writeLaneAudio (int part)
{
    juce::StringArray files;
    if (part < 0 || part >= NumParts)
        return files;
    waitUntilReady();
    constexpr double rate = 48000.0;
    const double bpm = (double) hostBpmOf (*this);
    const Groove local = copyGroove();
    Engine offline;
    prepareOffline (offline, rate);
    const auto audio = bounceLoop (offline, local, part, rate, bpm);
    if (audio.getNumSamples() < 64)
        return files;
    const auto file = newExportFolder().getChildFile (rollexport::baseName (local, (int) raw (apvts, "key"), bpm, part) + ".wav");
    if (! writeWav (file, audio, rate))
        return files;
    pruneExportCache (file);
    files.add (file.getFullPathName());
    return files;
}

juce::StringArray RollaProcessor::writeStemAudio()
{
    juce::StringArray files;
    waitUntilReady();
    constexpr double rate = 48000.0;
    const double bpm = (double) hostBpmOf (*this);
    const Groove local = copyGroove();
    const int key = (int) raw (apvts, "key");
    std::vector<int> parts;
    for (int part = 0; part < NumParts; ++part)
        parts.push_back (part);
    const auto audio = bounceParts (local, parts, rate, bpm);
    const auto dir = newExportFolder();
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (audio[i].getNumSamples() < 64)
            return {};
        const auto file = dir.getChildFile (rollexport::baseName (local, key, bpm, parts[i]) + ".wav");
        if (! writeWav (file, audio[i], rate))
            return {};
        files.add (file.getFullPathName());
    }
    pruneExportCache (dir);
    return files;
}

juce::String RollaProcessor::exportStemsTo (const juce::File& dir)
{
    auto result = dir.createDirectory();
    if (result.failed())
        return "Could not create " + dir.getFullPathName();
    waitUntilReady();
    constexpr double rate = 48000.0;
    const double bpm = (double) hostBpmOf (*this);
    const Groove local = copyGroove();
    const int key = (int) raw (apvts, "key");
    std::vector<int> parts { rollexport::kFull };
    for (int part = 0; part < NumParts; ++part)
        parts.push_back (part);
    const auto audio = bounceParts (local, parts, rate, bpm);
    for (size_t i = 0; i < parts.size(); ++i)
    {
        const auto file = dir.getChildFile (rollexport::baseName (local, key, bpm, parts[i]) + ".wav");
        if (audio[i].getNumSamples() < 64 || ! writeWav (file, audio[i], rate))
            return "Could not write " + file.getFullPathName();
    }
    return dir.getFullPathName();
}

} // namespace rolla

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new rolla::RollaProcessor();
}
