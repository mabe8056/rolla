#include "Presets.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <juce_core/juce_core.h>

namespace rolla
{
namespace
{

void copyName (char* dst, int n, const juce::String& s, const char* fallback)
{
    const auto text = s.isNotEmpty() ? s : juce::String (fallback);
    text.copyToUTF8 (dst, (size_t) n);
}

float num (const juce::var& o, const char* key, float fallback)
{
    if (auto* obj = o.getDynamicObject())
        if (obj->hasProperty (key))
            return (float) obj->getProperty (key);
    return fallback;
}

bool flag (const juce::var& o, const char* key, bool fallback)
{
    if (auto* obj = o.getDynamicObject())
        if (obj->hasProperty (key))
            return (bool) obj->getProperty (key);
    return fallback;
}

juce::String str (const juce::var& o, const char* key, const char* fallback)
{
    if (auto* obj = o.getDynamicObject())
        if (obj->hasProperty (key))
            return obj->getProperty (key).toString();
    return fallback;
}

int oscFrom (const juce::String& s)
{
    if (s == "tri") return 1;
    if (s == "sine2") return 2;
    if (s == "organ") return 3;
    return 0;
}

int modeFrom (const juce::String& s)
{
    if (s == "swell" || s == "pad") return 1;
    if (s == "click") return 2;
    return 0;
}

int engineFrom (const juce::String& s)
{
    if (s == "organ" || s == "additive") return 1;
    if (s == "fm") return 2;
    if (s == "pwm" || s == "pulse") return 3;
    return 0;
}

int waveFrom (const juce::String& s)
{
    if (s == "saw") return 0;
    if (s == "tri") return 1;
    if (s == "sine" || s == "sine2") return 2;
    if (s == "organ") return 3;
    if (s == "pulse" || s == "pwm" || s == "square") return 4;
    if (s == "fm") return 5;
    return -1;
}

bool hasKey (const juce::var& o, const char* key)
{
    if (auto* obj = o.getDynamicObject())
        return obj->hasProperty (key);
    return false;
}

void readExtras (Extras& ex, const juce::var& o)
{
    auto apply = [&] (const juce::var& src)
    {
        if (hasKey (src, "pw"))
            ex.pw = std::clamp (num (src, "pw", ex.pw), 0.05f, 0.95f);
        ex.pwmHz = std::max (0.f, num (src, "pwmHz", ex.pwmHz));
        ex.pwmDepth = std::clamp (num (src, "pwmDepth", ex.pwmDepth), 0.f, 0.45f);
        if (hasKey (src, "unison"))
            ex.unison = std::clamp ((int) num (src, "unison", (float) ex.unison), 1, 5);
        ex.detuneCents = std::clamp (num (src, "detuneCents", ex.detuneCents), 0.f, 50.f);
        ex.spread = std::clamp (num (src, "spread", ex.spread), 0.f, 1.f);
        if (hasKey (src, "wave"))
            ex.wave = waveFrom (str (src, "wave", "saw"));
        if (hasKey (src, "engine"))
            ex.engine = engineFrom (str (src, "engine", "timbre"));
        ex.fmRatio = std::clamp (num (src, "fmRatio", ex.fmRatio), 0.5f, 16.f);
        ex.fmIndex = std::clamp (num (src, "fmIndex", ex.fmIndex), 0.f, 10.f);
        ex.fmIndexEnd = std::clamp (num (src, "fmIndexEnd", ex.fmIndexEnd), 0.f, 10.f);
        ex.fmDecayMs = std::max (1.f, num (src, "fmDecayMs", ex.fmDecayMs));
        ex.noiseDb = num (src, "noiseDb", ex.noiseDb);
        ex.noiseHpfHz = std::max (20.f, num (src, "noiseHpfHz", ex.noiseHpfHz));
        ex.noiseBpHz = std::max (0.f, num (src, "noiseBpHz", ex.noiseBpHz));
        ex.noiseDecayMs = std::clamp (num (src, "noiseDecayMs", ex.noiseDecayMs), 0.f, 100.f);
        ex.pitchEnvSt = std::clamp (num (src, "pitchEnvSt", ex.pitchEnvSt), -24.f, 24.f);
        ex.pitchEnvTauMs = std::max (1.f, num (src, "pitchEnvTauMs", ex.pitchEnvTauMs));
        ex.filterAttackMs = std::max (0.f, num (src, "filterAttackMs", ex.filterAttackMs));
        ex.subLpfHz = std::max (0.f, num (src, "subLpfHz", ex.subLpfHz));
        ex.osc2Db = num (src, "osc2Db", ex.osc2Db);
        ex.osc2Oct = std::clamp ((int) num (src, "osc2Oct", (float) ex.osc2Oct), -3, 3);
        if (hasKey (src, "osc2Wave"))
            ex.osc2Wave = std::clamp (waveFrom (str (src, "osc2Wave", "saw")), 0, 3);
        ex.chorusMix = std::clamp (num (src, "chorusMix", ex.chorusMix), 0.f, 1.f);
        ex.chorusRateHz = std::max (0.01f, num (src, "chorusRateHz", ex.chorusRateHz));
        ex.monoBelowHz = std::max (0.f, num (src, "monoBelowHz", ex.monoBelowHz));
        ex.reverbSeconds = std::clamp (num (src, "reverbSeconds", ex.reverbSeconds), 0.2f, 4.f);
        ex.delaySendDb = num (src, "delaySendDb", ex.delaySendDb);
        ex.reverbSendDb = num (src, "reverbSendDb", ex.reverbSendDb);
        ex.duckDb = num (src, "duckDb", ex.duckDb);
        ex.duckRecoverMs = std::clamp (num (src, "duckRecoverMs", ex.duckRecoverMs), 20.f, 250.f);
        ex.duckGate = flag (src, "duckGate", ex.duckGate);
        if (hasKey (src, "vowelTo"))
            ex.vowelTo = std::clamp ((int) num (src, "vowelTo", (float) ex.vowelTo), -1, 3);
        ex.morphMs = std::max (0.f, num (src, "morphMs", ex.morphMs));
        ex.size = std::clamp (num (src, "size", ex.size), 0.f, 1.f);
        if (auto* obj = src.getDynamicObject())
        {
            if (obj->hasProperty ("targets"))
            {
                const auto t = obj->getProperty ("targets");
                ex.targetsLufs = num (t, "lufsS", ex.targetsLufs);
                ex.targetsPeak = num (t, "peakDb", num (t, "peak_dBFS", ex.targetsPeak));
            }
        }
    };

    apply (o);
    if (auto* obj = o.getDynamicObject())
        if (obj->hasProperty ("ex"))
            apply (obj->getProperty ("ex"));
}

void readStyles (SampleSpec& spec, const juce::var& preset)
{
    spec.styles = 0;
    auto* obj = preset.getDynamicObject();
    if (obj == nullptr || ! obj->hasProperty ("styles"))
        return;
    if (auto* arr = obj->getProperty ("styles").getArray())
    {
        for (const auto& item : *arr)
        {
            const auto s = item.toString().toLowerCase();
            if (s == "ukmin")
                spec.styles = (std::uint8_t) (spec.styles | 1u);
            else if (s == "long")
                spec.styles = (std::uint8_t) (spec.styles | 2u);
            else if (s == "pop")
                spec.styles = (std::uint8_t) (spec.styles | 4u);
            else if (s == "gaskin")
                spec.styles = (std::uint8_t) (spec.styles | 8u);
        }
    }
}

void readVoicePools (DrumPatch& drum, const juce::var& preset)
{
    for (int v = 0; v < NumDrumVoices; ++v)
        drum.voicePool[v].clear();
    auto* obj = preset.getDynamicObject();
    if (obj == nullptr || ! obj->hasProperty ("sample"))
        return;
    auto* sampleObj = obj->getProperty ("sample").getDynamicObject();
    if (sampleObj == nullptr || ! sampleObj->hasProperty ("voicePools"))
        return;
    const auto pools = sampleObj->getProperty ("voicePools");
    auto* poolsObj = pools.getDynamicObject();
    if (poolsObj == nullptr)
        return;
    const char* keys[] = { "kick", "snare", "clap", "rim", "tomLow", "tomMid", "hatC", "hatO", "ride", "perc" };
    for (int v = 0; v < NumDrumVoices && v < 10; ++v)
    {
        if (auto* arr = poolsObj->getProperty (keys[v]).getArray())
        {
            const int n = std::min (64, arr->size());
            drum.voicePool[v].reserve ((size_t) n);
            for (int i = 0; i < n; ++i)
            {
                const auto path = arr->getReference (i).toString();
                if (path.isNotEmpty())
                    drum.voicePool[v].push_back (path.toStdString());
            }
        }
    }
}

void readSample (SampleSpec& spec, const juce::var& preset)
{
    readStyles (spec, preset);
    auto* obj = preset.getDynamicObject();
    if (obj == nullptr || ! obj->hasProperty ("sample"))
        return;
    const auto s = obj->getProperty ("sample");
    const auto mode = str (s, "mode", "SYNTH");
    spec.mode = sampleModeFrom (mode.toRawUTF8());
    const auto file = str (s, "file", "");
    copySamplePath (spec.file, 96, file.toRawUTF8());
    spec.root = midiFromNoteName (str (s, "root", "").toRawUTF8(), spec.root);
    spec.start = std::max (0.f, num (s, "start", spec.start));
    spec.reverse = flag (s, "reverse", spec.reverse);
    spec.slice = flag (s, "slice", spec.slice);
    spec.layerMix = std::clamp (num (s, "layerMix", spec.layerMix), 0.f, 1.f);
    if (auto* sampleObj = s.getDynamicObject())
    {
        if (sampleObj->hasProperty ("poolFilter"))
        {
            const auto filter = sampleObj->getProperty ("poolFilter");
            spec.filter.on = true;
            const auto lane = str (filter, "lane", "");
            if (lane.isNotEmpty())
                spec.filter.lane = partFromLaneName (lane.toRawUTF8());
            const auto voice = str (filter, "voice", "");
            if (voice.isNotEmpty())
                spec.filter.voice = voiceFromFolderName (voice.toRawUTF8());
            copySamplePath (spec.filter.pack, 64, str (filter, "pack", "").toRawUTF8());
            copySamplePath (spec.filter.category, 32, str (filter, "category", "").toRawUTF8());
            if (auto* filterObj = filter.getDynamicObject())
            {
                if (filterObj->hasProperty ("root"))
                {
                    const auto root = filterObj->getProperty ("root");
                    if (root.isString())
                    {
                        const auto text = root.toString();
                        int midi = midiFromNoteName (text.toRawUTF8(), -1);
                        if (midi < 0 && text.isNotEmpty())
                            midi = midiFromNoteName ((text + "3").toRawUTF8(), -1);
                        if (midi >= 0)
                            spec.filter.rootPc = midi % 12;
                    }
                    else if (! root.isVoid())
                    {
                        const int pc = (int) root;
                        if (pc >= 0)
                            spec.filter.rootPc = pc % 12;
                    }
                }
                if (filterObj->hasProperty ("loop"))
                    spec.filter.loop = (double) filterObj->getProperty ("loop") > 0.5 ? 1 : 0;
            }
        }
    }
    if (auto* sampleObj = s.getDynamicObject())
    {
        if (sampleObj->hasProperty ("loop"))
        {
            const auto loop = sampleObj->getProperty ("loop");
            spec.loop.on = flag (loop, "on", spec.loop.on);
            spec.loop.start = std::max (0.f, num (loop, "start", spec.loop.start));
            spec.loop.end = std::max (0.f, num (loop, "end", spec.loop.end));
            spec.loop.xfade = std::max (0.f, num (loop, "xfade", spec.loop.xfade));
        }
        if (sampleObj->hasProperty ("pool"))
        {
            if (auto* arr = sampleObj->getProperty ("pool").getArray())
            {
                spec.poolCount = std::min (SampleSpec::kPool, arr->size());
                for (int i = 0; i < spec.poolCount; ++i)
                {
                    const auto& item = arr->getReference (i);
                    if (item.getDynamicObject() != nullptr)
                    {
                        copySamplePath (spec.pool[i], 96, str (item, "file", "").toRawUTF8());
                        spec.poolRoot[i] = midiFromNoteName (str (item, "root", "").toRawUTF8(), 0);
                    }
                    else
                    {
                        copySamplePath (spec.pool[i], 96, item.toString().toRawUTF8());
                        spec.poolRoot[i] = 0;
                    }
                }
            }
        }
        if (sampleObj->hasProperty ("zones"))
        {
            if (auto* arr = sampleObj->getProperty ("zones").getArray())
            {
                spec.zoneCount = std::min (SampleSpec::kZones, arr->size());
                for (int i = 0; i < spec.zoneCount; ++i)
                {
                    const auto& z = arr->getReference (i);
                    copySamplePath (spec.zones[i].file, 96, str (z, "file", "").toRawUTF8());
                    spec.zones[i].root = midiFromNoteName (str (z, "root", "").toRawUTF8(), spec.root);
                    spec.zones[i].lo = midiFromNoteName (str (z, "lo", "").toRawUTF8(), 0);
                    spec.zones[i].hi = midiFromNoteName (str (z, "hi", "").toRawUTF8(), 127);
                }
            }
        }
        if (sampleObj->hasProperty ("voices"))
        {
            const auto voices = sampleObj->getProperty ("voices");
            const char* keys[] = { "kick", "snare", "clap", "rim", "tomLow", "tomMid", "hatC", "hatO", "ride", "perc" };
            for (int v = 0; v < NumDrumVoices && v < 10; ++v)
                copySamplePath (spec.voiceFile[v], 96, str (voices, keys[v], "").toRawUTF8());
        }
    }
}

void setId (char* dst, const char* s)
{
    juce::String (s).copyToUTF8 (dst, 24);
}

void fillDialect (DialectDefaults& d, const char* bass, const char* stab, const char* hits,
                  const char* pluck, const char* pad, const char* vox, const char* drums,
                  bool hitsMute, float swing)
{
    setId (d.presetId[Bass], bass);
    setId (d.presetId[Stab], stab);
    setId (d.presetId[Hits], hits);
    setId (d.presetId[Pluck], pluck);
    setId (d.presetId[Pad], pad);
    setId (d.presetId[Vox], vox);
    setId (d.presetId[Drums], drums);
    d.hitsMute = hitsMute;
    d.swing = swing;
}

int presetCountIn (const juce::var& root, const char* part)
{
    auto* obj = root.getDynamicObject();
    if (obj == nullptr)
        return 0;
    auto* partsObj = obj->getProperty ("parts").getDynamicObject();
    if (partsObj == nullptr)
        return 0;
    auto* partObj = partsObj->getProperty (part).getDynamicObject();
    if (partObj == nullptr)
        return 0;
    if (auto* arr = partObj->getProperty ("presets").getArray())
        return std::min (PresetBank::kPer, arr->size());
    return 0;
}

const juce::var* presetAt (const juce::var& root, const char* part, int index)
{
    auto* obj = root.getDynamicObject();
    if (obj == nullptr)
        return nullptr;
    auto parts = obj->getProperty ("parts");
    auto* partsObj = parts.getDynamicObject();
    if (partsObj == nullptr)
        return nullptr;
    auto partVar = partsObj->getProperty (part);
    auto* partObj = partVar.getDynamicObject();
    if (partObj == nullptr)
        return nullptr;
    auto presets = partObj->getProperty ("presets");
    if (auto* arr = presets.getArray())
        if (index >= 0 && index < arr->size())
            return &arr->getReference (index);
    return nullptr;
}

} // namespace

void loadDefaultPresets (PresetBank& bank)
{
    // A PresetBank temporary is about a megabyte. Construct the blank on the heap so
    // pluginval's 512 KB macOS test thread can open the plugin.
    auto blank = std::make_unique<PresetBank>();
    bank = std::move (*blank);
    fillDialect (bank.dialect[UkMin], "pluck", "dub", "warehouse", "hook", "air", "close", "tight", true, 0.52f);
    fillDialect (bank.dialect[Long], "round", "swell", "dusty", "bright", "wide", "booth", "tight", true, 0.51f);
    fillDialect (bank.dialect[Pop], "click", "click", "rave", "soft", "air", "chop", "tight", true, 0.50f);
    fillDialect (bank.dialect[Gaskin], "round", "swell", "warehouse", "hook", "dark", "chop", "room", false, 0.50f);
    auto& g = bank.dialect[Gaskin];
    g.bassDuck = true;
    g.bassGlide = true;
    g.bassDuckDb = 7.f;
    g.bassDuckMs = 110.f;
    g.bassGlideMs = 0.f;
    setId (g.bassWhen, "round");
    bank.fromJson = false;
}

bool loadPresetsFromJson (PresetBank& bank, const char* json, int size)
{
    loadDefaultPresets (bank);
    if (json == nullptr || size <= 0)
        return false;
    const auto parsed = juce::JSON::parse (juce::String::fromUTF8 (json, size));
    if (parsed.isVoid() || parsed.getDynamicObject() == nullptr)
        return false;

    for (int i = 0; i < PresetBank::kPer; ++i)
    {
        if (const auto* p = presetAt (parsed, "bass", i))
        {
            auto& b = bank.bass[i];
            copyName (b.id, 24, str (*p, "id", b.id), b.id);
            copyName (b.name, 32, str (*p, "name", b.name), b.name);
            b.osc = oscFrom (str (*p, "osc", "saw"));
            b.subDb = num (*p, "subDb", b.subDb);
            b.oct2Db = num (*p, "oct2Db", b.oct2Db);
            b.cutoffHz = num (*p, "cutoffHz", b.cutoffHz);
            b.resonance = num (*p, "resonance", b.resonance);
            b.filterEnvOct = num (*p, "filterEnvOct", b.filterEnvOct);
            b.filterTauMs = num (*p, "filterTauMs", b.filterTauMs);
            b.aMs = num (*p, "ampAMs", b.aMs);
            b.dMs = num (*p, "ampDMs", b.dMs);
            b.sDb = num (*p, "ampSDb", b.sDb);
            b.rMs = num (*p, "ampRMs", b.rMs);
            b.glideMs = num (*p, "glideMs", b.glideMs);
            b.driveDb = num (*p, "driveDb", b.driveDb);
            b.duckDb = num (*p, "duckDb", b.duckDb);
            b.duckRecoverMs = num (*p, "duckRecoverMs", b.duckRecoverMs);
            b.hpfHz = num (*p, "hpfHz", b.hpfHz);
            b.gate = flag (*p, "gate", b.gate);
            b.trimDb = num (*p, "trimDb", b.trimDb);
            b.octave = (int) num (*p, "octave", (float) b.octave);
            if (hasKey (*p, "filterEnvSt"))
                b.filterEnvOct = num (*p, "filterEnvSt", b.filterEnvOct * 12.f) / 12.f;
            readExtras (b.ex, *p);
            readSample (b.sample, *p);
        }
        if (const auto* p = presetAt (parsed, "stab", i))
        {
            auto& b = bank.stab[i];
            copyName (b.id, 24, str (*p, "id", b.id), b.id);
            copyName (b.name, 32, str (*p, "name", b.name), b.name);
            b.mode = modeFrom (str (*p, "mode", "dub"));
            b.cutoffHz = num (*p, "cutoffHz", b.cutoffHz);
            b.envOct = num (*p, "envOct", b.envOct);
            b.tauMs = num (*p, "tauMs", b.tauMs);
            b.aMs = num (*p, "ampAMs", b.aMs);
            b.dMs = num (*p, "ampDMs", b.dMs);
            b.sDb = num (*p, "ampSDb", b.sDb);
            b.rMs = num (*p, "ampRMs", b.rMs);
            b.unison = std::clamp ((int) num (*p, "unison", (float) b.unison), 1, 5);
            if (hasKey (*p, "envSt"))
                b.envOct = num (*p, "envSt", b.envOct * 12.f) / 12.f;
            if (auto* obj = p->getDynamicObject())
            {
                if (obj->hasProperty ("chord"))
                {
                    if (auto* arr = obj->getProperty ("chord").getArray())
                    {
                        b.chordN = std::min (4, arr->size());
                        for (int c = 0; c < b.chordN; ++c)
                            b.chordIv[c] = (int) arr->getReference (c);
                        b.chordOn = b.chordN > 1;
                    }
                }
            }
            if (hasKey (*p, "chordOn"))
                b.chordOn = flag (*p, "chordOn", b.chordOn);
            readExtras (b.ex, *p);
            b.detuneCents = num (*p, "detuneCents", b.detuneCents);
            b.spread = num (*p, "spread", b.spread);
            b.hpfHz = num (*p, "hpfHz", b.hpfHz);
            b.delaySend = num (*p, "delaySend", b.delaySend);
            b.delayFeedback = num (*p, "delayFeedback", b.delayFeedback);
            b.delayBeats = num (*p, "delayBeats", b.delayBeats);
            b.reverbSend = num (*p, "reverbSend", b.reverbSend);
            b.hold = flag (*p, "hold", b.hold);
            b.trimDb = num (*p, "trimDb", b.trimDb);
            readSample (b.sample, *p);
        }
        auto readTone = [&] (const char* part, TonePatch& b)
        {
            if (const auto* p = presetAt (parsed, part, i))
            {
                copyName (b.id, 24, str (*p, "id", b.id), b.id);
                copyName (b.name, 32, str (*p, "name", b.name), b.name);
                b.cutoffHz = num (*p, "cutoffHz", b.cutoffHz);
                b.decayMs = num (*p, "decayMs", b.decayMs);
                b.attackMs = num (*p, "attackMs", b.attackMs);
                b.releaseMs = num (*p, "releaseMs", b.releaseMs);
                b.brightness = num (*p, "brightness", b.brightness);
                b.detune = num (*p, "detune", b.detune);
                b.decayScale = num (*p, "decayScale", b.decayScale);
                b.detuneCents = num (*p, "detuneCents", b.detuneCents);
                b.breath = num (*p, "breath", b.breath);
                b.reverbSend = num (*p, "reverbSend", b.reverbSend);
                b.trimDb = num (*p, "trimDb", b.trimDb);
                readExtras (b.ex, *p);
                readSample (b.sample, *p);
            }
        };
        readTone ("hits", bank.hits[i]);
        readTone ("pluck", bank.pluck[i]);
        readTone ("pad", bank.pad[i]);
        readTone ("vox", bank.vox[i]);

        if (const auto* p = presetAt (parsed, "drums", i))
        {
            auto& b = bank.drums[i];
            copyName (b.id, 24, str (*p, "id", b.id), b.id);
            copyName (b.name, 32, str (*p, "name", b.name), b.name);
            b.kickHz = num (*p, "kickHz", b.kickHz);
            b.kickDecayMs = num (*p, "kickDecayMs", b.kickDecayMs);
            b.kickClick = num (*p, "kickClick", b.kickClick);
            b.kickSweep = num (*p, "kickSweep", b.kickSweep);
            b.snareTone = num (*p, "snareTone", b.snareTone);
            b.snareSnap = num (*p, "snareSnap", b.snareSnap);
            b.snareDecayMs = num (*p, "snareDecayMs", b.snareDecayMs);
            b.clapDecayMs = num (*p, "clapDecayMs", b.clapDecayMs);
            b.hatDecayMs = num (*p, "hatDecayMs", b.hatDecayMs);
            b.hatOpenDecayMs = num (*p, "hatOpenDecayMs", b.hatOpenDecayMs);
            b.hatHpHz = num (*p, "hatHpHz", b.hatHpHz);
            b.tomDecayMs = num (*p, "tomDecayMs", b.tomDecayMs);
            b.room = num (*p, "room", b.room);
            b.drive = num (*p, "drive", b.drive);
            b.trimDb = num (*p, "trimDb", b.trimDb);
            readSample (b.sample, *p);
            readVoicePools (b, *p);
        }
    }

    const int counts[NumParts] = {
        presetCountIn (parsed, "bass"),
        presetCountIn (parsed, "stab"),
        presetCountIn (parsed, "hits"),
        presetCountIn (parsed, "pluck"),
        presetCountIn (parsed, "pad"),
        presetCountIn (parsed, "vox"),
        presetCountIn (parsed, "drums"),
    };
    for (int part = 0; part < NumParts; ++part)
        if (counts[part] > 0)
            bank.count[part] = counts[part];

    if (auto* root = parsed.getDynamicObject())
    {
        if (root->hasProperty ("dialects"))
        {
            const auto dialects = root->getProperty ("dialects");
            for (int d = 0; d < NumDialects; ++d)
            {
                if (auto* obj = dialects.getDynamicObject())
                {
                    if (! obj->hasProperty (dialectId (d)))
                        continue;
                    const auto row = obj->getProperty (dialectId (d));
                    auto& slot = bank.dialect[d];
                    for (int part = 0; part < NumParts; ++part)
                    {
                        const auto id = str (row, partId (part), slot.presetId[part]);
                        id.copyToUTF8 (slot.presetId[part], 24);
                    }
                    slot.hitsMute = flag (row, "hitsMute", slot.hitsMute);
                    slot.swing = std::clamp (num (row, "swing", slot.swing), 0.50f, 0.57f);
                    if (auto* rowObj = row.getDynamicObject())
                    {
                        if (rowObj->hasProperty ("overrides"))
                        {
                            const auto ov = rowObj->getProperty ("overrides");
                            if (auto* ovObj = ov.getDynamicObject())
                            {
                                if (ovObj->hasProperty ("bass"))
                                {
                                    const auto b = ovObj->getProperty ("bass");
                                    const auto when = str (b, "whenPreset", slot.bassWhen);
                                    when.copyToUTF8 (slot.bassWhen, 24);
                                    if (hasKey (b, "duckDb"))
                                    {
                                        slot.bassDuck = true;
                                        slot.bassDuckDb = num (b, "duckDb", slot.bassDuckDb);
                                        slot.bassDuckMs = num (b, "duckRecoverMs", slot.bassDuckMs);
                                    }
                                    if (hasKey (b, "glideMs"))
                                    {
                                        slot.bassGlide = true;
                                        slot.bassGlideMs = num (b, "glideMs", slot.bassGlideMs);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (int d = 0; d < NumDialects; ++d)
        for (int part = 0; part < NumParts; ++part)
            bank.dialect[d].presetIndex[part] = presetIndex (bank, part, bank.dialect[d].presetId[part]);

    bank.fromJson = true;
    return true;
}

int presetIndex (const PresetBank& bank, int part, const char* id) noexcept
{
    if (id == nullptr || id[0] == 0 || part < 0 || part >= NumParts)
        return -1;
    const int n = bank.count[part] > 0 ? bank.count[part] : PresetBank::kPer;
    for (int i = 0; i < n; ++i)
    {
        const char* have = "";
        switch (part)
        {
            case Bass: have = bank.bass[i].id; break;
            case Stab: have = bank.stab[i].id; break;
            case Hits: have = bank.hits[i].id; break;
            case Pluck: have = bank.pluck[i].id; break;
            case Pad: have = bank.pad[i].id; break;
            case Vox: have = bank.vox[i].id; break;
            case Drums: have = bank.drums[i].id; break;
            default: break;
        }
        if (std::strcmp (have, id) == 0)
            return i;
    }
    return -1;
}

void applyDialectBass (const PresetBank& bank, int dialect, BassPatch& bass) noexcept
{
    if (dialect < 0 || dialect >= NumDialects)
        return;
    const auto& slot = bank.dialect[dialect];
    if (slot.bassWhen[0] != 0 && std::strcmp (slot.bassWhen, bass.id) != 0)
        return;
    if (slot.bassDuck)
    {
        bass.duckDb = slot.bassDuckDb;
        bass.duckRecoverMs = slot.bassDuckMs;
    }
    if (slot.bassGlide)
        bass.glideMs = slot.bassGlideMs;
}

} // namespace rolla
