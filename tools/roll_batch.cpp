#include "PatternStats.h"
#include "Engine/Engine.h"
#include "Generator/Generator.h"
#include "Generator/Intensity.h"
#include "Generator/MidiFile.h"
#include "Generator/OriginalityJson.h"
#include "Generator/Presets.h"
#include "Mix/MixProgram.h"
#include "../tests/common/ScratchProfile.h"

#include <BinaryData.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

const char* usageText();

namespace
{

// Fallback 16th swing when the preset bank has none: the genre model v1.2
// style targets, clamped to its 53-57% hat band.
float swingFor (int dialect)
{
    switch (dialect)
    {
        case rolla::UkMin: return 0.537f;
        case rolla::Long: return 0.54f;
        case rolla::Pop: return 0.567f;
        default: return 0.565f;
    }
}

// Redraws behind one written roll: the originality / thin-cell redraws of
// every generate() call that roll took, plus the batch skeleton redraws.
struct RollRedraws
{
    int orig = 0;
    int guard = 0;
    int skeleton = 0;
    std::vector<rolla::Groove::RedrawEntry> log;

    int total() const { return orig + guard + skeleton; }

    void take (const rolla::Groove& g)
    {
        orig += g.origRedraws;
        guard += g.guardRedraws;
        for (int i = 0; i < g.redrawLogCount && i < rolla::Groove::kMaxRedrawLog; ++i)
            log.push_back (g.redrawLog[i]);
    }
};

const char* laneName (int lane)
{
    if (lane >= 0 && lane < rolla::NumParts)
        return rolla::partId (lane);
    return lane == -1 ? "merged_low" : "none";
}

const char* metricText (int metric)
{
    return metric >= rolla::orig::MetricNone && metric <= rolla::orig::MetricBannedFigure ? rolla::orig::metricName (metric) : "other";
}

std::string redrawJson (const RollRedraws& r)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision (3);
    out << "\"redraws\": " << r.total() << ",\n  \"redraws_orig\": " << r.orig << ",\n  \"redraws_guard\": " << r.guard
        << ",\n  \"redraws_skeleton\": " << r.skeleton << ",\n  \"redraw_log\": [";
    for (size_t i = 0; i < r.log.size(); ++i)
    {
        const auto& e = r.log[i];
        out << (i ? ", " : "") << "{\"lane\": \"" << laneName (e.lane) << "\", \"ref\": \"" << e.ref
            << "\", \"metric\": \"" << metricText (e.metric) << "\", \"score\": " << e.score << "}";
    }
    out << "]";
    return out.str();
}

// Always written, also when the WAV is skipped (no sample energy deletes the sidecar).
void writeRollJson (const std::filesystem::path& dir, int style, const std::string& name,
                    const std::uint32_t seeds[rolla::NumParts], const RollRedraws& r)
{
    std::ofstream out (dir / "roll.json");
    out << "{\n  \"style\": \"" << rolla::dialectId (style) << "\",\n  \"roll\": \"" << name << "\",\n  \"seeds\": [";
    for (int p = 0; p < rolla::NumParts; ++p)
        out << (p ? ", " : "") << seeds[p];
    out << "],\n  " << redrawJson (r) << "\n}\n";
    // Every redraw behind the roll, one stderr line each (count, reference
    // stream id and score), so a gate log shows what the guard caught.
    for (std::size_t i = 0; i < r.log.size(); ++i)
        std::cerr << "REDRAW " << rolla::dialectId (style) << " " << name << " #" << (i + 1) << "/" << r.total()
                  << " lane=" << laneName (r.log[i].lane) << " metric=" << metricText (r.log[i].metric)
                  << " ref=" << (r.log[i].ref[0] ? r.log[i].ref : "-") << " score=" << r.log[i].score << "\n";
    if (r.skeleton > 0)
        std::cerr << "REDRAW " << rolla::dialectId (style) << " " << name << " skeleton=" << r.skeleton << " (batch twin)\n";
}

struct Knobs
{
    float dark = 0.55f;
    float busy = 0.40f;
    float bounce = 0.45f;
    float stab = 0.55f;
    float styleIntensity = 0.5f;
    float tone[rolla::NumParts] {};
    float punch[rolla::NumParts] {};
    float levelDb[rolla::NumParts] {};
    int intensity = 75;
    bool stems = false;
    bool midiOnly = false;     // --midi-only: write the MIDI and roll.json, no WAV render
    bool styleFolders = false; // --folder-names styles
    unsigned stemMask = 0x7fu; // --stem-lanes picks which stems --stems writes

    Knobs()
    {
        for (int i = 0; i < rolla::NumParts; ++i)
        {
            tone[i] = 0.5f;
            punch[i] = 0.5f;
            levelDb[i] = 0.f;
        }
    }
};

int laneOf (const std::string& name)
{
    if (name == "BASS") return rolla::Bass;
    if (name == "STAB") return rolla::Stab;
    if (name == "HITS") return rolla::Hits;
    if (name == "PLUCK") return rolla::Pluck;
    if (name == "PAD") return rolla::Pad;
    if (name == "VOX") return rolla::Vox;
    if (name == "DRUMS") return rolla::Drums;
    return -1;
}

std::string upperCopy (std::string text)
{
    for (char& c : text)
        c = (char) std::toupper ((unsigned char) c);
    return text;
}

bool parseKnob (const std::string& text, Knobs& knobs)
{
    const auto eq = text.find ('=');
    if (eq == std::string::npos || eq == 0)
        return false;
    const std::string name = upperCopy (text.substr (0, eq));
    const float value = std::strtof (text.c_str() + eq + 1, nullptr);
    const auto unit = [] (float v) { return std::clamp (v, 0.f, 1.f); };
    if (name == "DARK") knobs.dark = unit (value);
    else if (name == "BUSY") knobs.busy = unit (value);
    else if (name == "BOUNCE") knobs.bounce = unit (value);
    else if (name == "STAB") knobs.stab = unit (value);
    else if (name == "STYLE" || name == "STYLEINTENSITY") knobs.styleIntensity = unit (value);
    else if (name == "TONE")
        for (int i = 0; i < rolla::NumParts; ++i) knobs.tone[i] = unit (value);
    else if (name == "PUNCH")
        for (int i = 0; i < rolla::NumParts; ++i) knobs.punch[i] = unit (value);
    else if (name == "LEVEL")
        for (int i = 0; i < rolla::NumParts; ++i) knobs.levelDb[i] = value;
    else if (name == "INTENSITY")
        knobs.intensity = std::clamp ((int) std::lround (value), 0, 100);
    else
    {
        const auto dot = name.find ('.');
        if (dot == std::string::npos)
            return false;
        const int lane = laneOf (name.substr (0, dot));
        const std::string which = name.substr (dot + 1);
        if (lane < 0)
            return false;
        if (which == "TONE") knobs.tone[lane] = unit (value);
        else if (which == "PUNCH") knobs.punch[lane] = unit (value);
        else if (which == "LEVEL") knobs.levelDb[lane] = value;
        else return false;
    }
    return true;
}

rolla::Settings rollSettings (int dialect, const Knobs& knobs)
{
    rolla::Settings s;
    s.dark = knobs.dark;
    s.busy = knobs.busy;
    s.bounce = knobs.bounce;
    s.stab = knobs.stab;
    s.styleIntensity = knobs.styleIntensity;
    s.dialect = dialect;
    s.key = 2;
    s.bars = 8;
    s.fill = false;
    return s;
}

const char* presetName (const rolla::PresetBank& bank, int part, int index)
{
    const int i = rolla::presetSlot (bank, part, index);
    switch (part)
    {
        case rolla::Bass: return bank.bass[i].id;
        case rolla::Stab: return bank.stab[i].id;
        case rolla::Hits: return bank.hits[i].id;
        case rolla::Pluck: return bank.pluck[i].id;
        case rolla::Pad: return bank.pad[i].id;
        case rolla::Vox: return bank.vox[i].id;
        case rolla::Drums: return bank.drums[i].id;
        default: break;
    }
    return "";
}

const char* stemName (int part)
{
    static const char* names[] = { "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX", "DRUMS" };
    return names[part];
}

bool loadMix (rolla::MixProgram& program)
{
    return program.loadFromMemory (BinaryData::mix_profiles_json, BinaryData::mix_profiles_jsonSize,
                                   BinaryData::main_sum_trim_json, BinaryData::main_sum_trim_jsonSize,
                                   BinaryData::mix_makeup_json, BinaryData::mix_makeup_jsonSize);
}

void seedsFor (int style, int roll, std::uint32_t seeds[rolla::NumParts])
{
    for (int p = 0; p < rolla::NumParts; ++p)
        seeds[p] = (std::uint32_t) (roll + 1) * 0x85EBCA6Bu
                 ^ ((std::uint32_t) style * 0xC2B2AE35u + (std::uint32_t) p * 0x27D4EB2Du);
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sr, int start, int num)
{
    file.getParentDirectory().createDirectory();
    file.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::OutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;
    juce::AudioFormatWriterOptions options;
    options = options.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24);
    auto writer = wav.createWriterFor (stream, options);
    if (writer == nullptr)
        return false;
    return writer->writeFromAudioSampleBuffer (audio, start, num);
}

struct Rendered
{
    float peak = 0.f;
    int kickMidiSample = -1;
};

// Renders the plugin path: mix program, sum trim from the lanes that are
// actually open, and the master true-peak guard. The guard's delay is dropped
// from the front so the downbeat sits at file sample 0, the way a host that
// honours setLatencySamples hears it.
Rendered renderWav (const rolla::PresetBank& bank, const rolla::Groove& groove, int dialect,
                    const juce::File& file, const rolla::SampleLibrary* library, const rolla::SampleIndex* index,
                    const rolla::MixProgram* mix, const Knobs& knobs, const int preset[rolla::NumParts],
                    const int samplePick[rolla::NumParts], bool hitsMute, float swing, bool soloPart, int solo)
{
    constexpr double sr = 48000.0;
    constexpr double bpm = 130.0;
    constexpr int bars = 8;
    const int loopN = (int) std::llround (bars * 4.0 * 60.0 / bpm * sr);
    const int tailN = (int) std::llround (0.70 * sr);
    const int total = loopN + tailN;

    rolla::Engine engine;
    engine.prepare (sr, 256);
    rolla::EngineParams params;
    for (int i = 0; i < rolla::NumParts; ++i)
    {
        params.tone[i] = knobs.tone[i];
        params.punch[i] = knobs.punch[i];
        params.levelDb[i] = knobs.levelDb[i];
        // A stem solos its lane but never un-mutes a lane the style mutes.
        params.mute[i] = soloPart ? i != solo : false;
        params.preset[i] = preset != nullptr ? preset[i] : 0;
        params.samplePick[i] = samplePick != nullptr ? samplePick[i] : 0;
    }
    for (int v = 0; v < rolla::NumDrumVoices; ++v)
        params.drumDecay[v] = 0.5f;
    if (hitsMute)
        params.mute[rolla::Hits] = true;
    params.duck = true;
    params.drumsToMain = true;
    params.swing = swing;
    params.outputDb = 0.f;
    params.drumsAccent = 0.6f;
    params.bars = bars;
    params.key = groove.key;
    params.dialect = dialect;
    params.presets = &bank;
    params.library = library;
    params.sampleIndex = index;
    params.bpm = (float) bpm;
    params.mix = mix != nullptr && mix->loaded ? mix : nullptr;
    params.tpGuard = true;

    juce::AudioBuffer<float> audio (2, total);
    audio.clear();
    int done = 0;
    double ppq = 0.0;
    int kickAt = -1;
    while (done < total)
    {
        int n = std::min (256, total - done);
        // Split on the loop boundary. Advancing a whole block past the end
        // wraps the playhead into bar 1 while still playing, and that hit
        // releases into the tail.
        if (done < loopN)
            n = std::min (n, loopN - done);
        rolla::Transport transport;
        transport.playing = done < loopN;
        transport.bpm = bpm;
        transport.ppqStart = ppq;
        transport.sampleRate = sr;
        juce::MidiBuffer midi;
        engine.process (groove, params, transport,
                        audio.getWritePointer (0, done),
                        audio.getWritePointer (1, done),
                        nullptr, nullptr, n, midi, true);
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            if (! msg.isNoteOn() || msg.getChannel() != 10 || msg.getNoteNumber() != rolla::drumMidiNote (rolla::Kick))
                continue;
            const int at = done + meta.samplePosition;
            if (kickAt < 0 || at < kickAt)
                kickAt = at;
        }
        if (transport.playing)
            ppq += (double) n * bpm / (60.0 * sr);
        done += n;
    }

    const int lat = std::clamp (engine.latencySamples(), 0, total - 1);
    const int keep = total - lat;
    Rendered out;
    out.kickMidiSample = kickAt;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* data = audio.getReadPointer (ch);
        for (int i = lat; i < total; ++i)
            out.peak = std::max (out.peak, std::fabs (data[i]));
    }
    if (! writeWav (file, audio, sr, lat, keep))
        std::cerr << "wav write failed " << file.getFullPathName() << "\n";
    return out;
}

void writeStats (const std::filesystem::path& path,
                 const rolla::StyleStat stats[rolla::NumDialects],
                 const rolla::MacroDelta& busy,
                 const rolla::MacroDelta& bounce,
                 const rolla::MacroDelta& dark,
                 const rolla::MacroDelta& stab)
{
    const int lanes[] = { rolla::Bass, rolla::Stab, rolla::Hits, rolla::Pluck, rolla::Pad, rolla::Vox, rolla::Drums };
    std::ofstream out (path);
    out << std::fixed << std::setprecision (2);
    out << "# ROLLA corpus rolls\n\n";
    out << "Bass phrases come from the phrase grammar. No stored reference cells.\n\n";
    out << "Each style is 6000 seeds. A pattern counts as distinct when onset, length, and pitch differ "
           "(chord tones for stab, hits, and pad; a per-step voice mask for drums). "
           "Edit distance is the mean pairwise Levenshtein of the first 48 patterns (1128 pairs).\n\n";
    out << "| style | lane | unique | mean edit | rules |\n| --- | --- | ---: | ---: | ---: |\n";
    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        for (int part : lanes)
        {
            const auto& lane = stats[style].lane[part];
            out << "| " << rolla::dialectName (style)
                << " | " << rolla::partId (part)
                << " | " << lane.unique
                << " | " << lane.meanEdit
                << " | " << stats[style].rulesPassed << "/" << stats[style].rolls
                << " |\n";
        }
    }
    out << "\nMacros measured on UK MIN, 200 rolls at each end. "
           "BUSY moves notes per bar, BOUNCE moves the share of bass onsets on odd sixteenths, "
           "DARK moves the dark pitch-class share and the mean interval, STAB moves the stab count.\n\n";
    auto row = [&] (const char* name, const rolla::MacroDelta& d)
    {
        out << "| " << name
            << " | " << d.notesLo << " → " << d.notesHi
            << " | " << d.oddLo << " → " << d.oddHi
            << " | " << d.darkLo << " → " << d.darkHi
            << " | " << d.ivLo << " → " << d.ivHi
            << " | " << d.offLo << " → " << d.offHi
            << " | " << d.stabLo << " → " << d.stabHi
            << " |\n";
    };
    out << "| knob | notes/bar | odd 16ths | dark pc | mean iv | off kick | stabs |\n| --- | --- | --- | --- | --- | --- | --- |\n";
    row ("BUSY 0.05→0.95", busy);
    row ("BOUNCE 0.05→0.95", bounce);
    row ("DARK 0.05→0.95", dark);
    row ("STAB 0→1", stab);
    out << "\nTwenty rolls per style are under tools/out/<style>/roll_XX/ as type-0 lane MIDIs, "
           "one type-1 combined MIDI, and a 48 kHz 24-bit WAV at 130 BPM. "
           "Swing is 53.7% UK MIN, 54% LONG, 56.7% POP and 56.5% JACKIN (genre model v1.2 band 53-57%). "
           "Each roll folder has a roll.json with its seeds and originality redraws. WAV files are gitignored.\n";
}

// Old ids (ukmin|long|pop|jackin), the style names (roller|deep|bouncy|jackin),
// or 0-3, in any case.
int styleId (const std::string& name)
{
    std::string s = name;
    std::transform (s.begin(), s.end(), s.begin(), [] (unsigned char c) { return (char) std::tolower (c); });
    if (s == "ukmin" || s == "roller" || s == "0") return rolla::UkMin;
    if (s == "long" || s == "deep" || s == "1") return rolla::Long;
    if (s == "pop" || s == "bouncy" || s == "2") return rolla::Pop;
    if (s == "jackin" || s == "3") return rolla::Jackin;
    if (s == "all") return -2;
    return -1;
}

// Output folder per style: the old ids by default (scripts depend on them),
// or the style names with --folder-names styles.
std::string folderOf (int style, bool styleNames)
{
    static const char* names[] = { "roller", "deep", "bouncy", "jackin" };
    return styleNames ? names[std::clamp (style, 0, rolla::NumDialects - 1)] : rolla::dialectId (style);
}

std::string jsonEscape (const std::string& text)
{
    std::string out;
    out.reserve (text.size());
    for (char c : text)
    {
        if (c == '\\' || c == '"')
            out.push_back ('\\');
        out.push_back (c);
    }
    return out;
}

std::string pickedFile (const rolla::SampleIndex* index, const rolla::SampleSpec& spec,
                        int part, int voice, int pick, int dialect)
{
    if (index == nullptr)
        return {};
    const int queryVoice = voice >= 0 ? voice : spec.filter.voice;
    const rolla::PoolFilter filter = rolla::poolFilterWithFiles (*index, spec, part, queryVoice, dialect);
    const int n = rolla::sampleIndexCount (*index, part, filter);
    if (n <= 0)
        return {};
    if (pick < 0)
        pick = 0;
    const auto* entry = rolla::sampleIndexAt (*index, part, filter, pick % n);
    return entry != nullptr ? entry->path : "";
}

void writeSidecar (const juce::File& wav, int style, const Knobs& knobs, bool hitsMute, float swing,
                   const rolla::PresetBank& bank, const int preset[rolla::NumParts],
                   const int samplePick[rolla::NumParts], const std::string fileOf[rolla::NumParts],
                   const std::string drumOf[rolla::NumDrumVoices], int kickMidi, const RollRedraws& redraws)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision (3);
    out << "{\n  \"style\": \"" << rolla::dialectId (style) << "\",\n";
    out << "  " << redrawJson (redraws) << ",\n";
    out << "  \"intensity\": " << knobs.intensity << ",\n";
    out << "  \"hitsMute\": " << (hitsMute ? "true" : "false") << ",\n";
    out << "  \"swing\": " << swing << ",\n";
    out << "  \"kickMidiSample\": " << kickMidi << ",\n";
    out << "  \"presets\": {";
    for (int i = 0; i < rolla::NumParts; ++i)
        out << (i ? ", " : "") << "\"" << rolla::partId (i) << "\": \"" << jsonEscape (presetName (bank, i, preset[i])) << "\"";
    out << "},\n  \"presetIndex\": {";
    for (int i = 0; i < rolla::NumParts; ++i)
        out << (i ? ", " : "") << "\"" << rolla::partId (i) << "\": " << preset[i];
    out << "},\n  \"samplePick\": {";
    for (int i = 0; i < rolla::NumParts; ++i)
        out << (i ? ", " : "") << "\"" << rolla::partId (i) << "\": " << samplePick[i];
    out << "},\n  \"files\": {";
    bool first = true;
    for (int i = 0; i < rolla::NumParts; ++i)
    {
        if (fileOf[i].empty())
            continue;
        out << (first ? "" : ", ") << "\"" << rolla::partId (i) << "\": \"" << jsonEscape (fileOf[i]) << "\"";
        first = false;
    }
    static const char* voiceName[] = { "kick", "snare", "clap", "rim", "tomLow", "tomMid", "hatC", "hatO", "ride", "perc" };
    for (int v = 0; v < rolla::NumDrumVoices; ++v)
    {
        if (drumOf[v].empty())
            continue;
        out << (first ? "" : ", ") << "\"" << voiceName[v] << "\": \"" << jsonEscape (drumOf[v]) << "\"";
        first = false;
    }
    out << "}\n}\n";
    wav.withFileExtension (".json").replaceWithText (out.str());
}

int renderBatch (const std::string& samples, const std::string& styleName, int count, std::uint32_t seed,
                 const std::string& outDir, const Knobs& knobs)
{
    const int one = styleId (styleName);
    if (one == -1 || count < 1)
    {
        std::cerr << usageText();
        return 2;
    }
    rolla::PresetBank bank;
    rolla::loadPresetsFromJson (bank, BinaryData::presets_json, BinaryData::presets_jsonSize);
    rolla::MixProgram mix;
    if (! loadMix (mix) || ! mix.loaded)
    {
        std::cerr << "FAIL mix program did not load\n";
        return 1;
    }

    std::unique_ptr<rolla::SampleLibrary> library;
    std::shared_ptr<const rolla::SampleIndex> index;
    if (! samples.empty())
    {
        library = std::make_unique<rolla::SampleLibrary>();
        library->setRoot (juce::File (samples));
        library->startIndex (BinaryData::SAMPLE_MANIFEST_json, BinaryData::SAMPLE_MANIFEST_jsonSize);
        library->watch (bank);
        const auto indexedBy = juce::Time::getMillisecondCounter() + 20000;
        while (library->indexedFound() < 0 && juce::Time::getMillisecondCounter() < indexedBy)
            juce::Thread::sleep (20);
        index = library->index();
        if (index == nullptr)
            std::cerr << "sample index did not finish; WAV energy may be missing\n";
    }

    const int styles[4] = { rolla::UkMin, rolla::Long, rolla::Pop, rolla::Jackin };
    const int nStyles = one == -2 ? 4 : 1;
    int wavs = 0;
    int silent = 0;
    int origFails = 0;
    for (int s = 0; s < nStyles; ++s)
    {
        const int style = one == -2 ? styles[s] : one;
        const auto settings = rollSettings (style, knobs);
        const auto& row = bank.dialect[style];
        const float swing = row.swing > 0.f ? row.swing : swingFor (style);
        std::vector<rolla::Groove> seen;
        seen.reserve ((size_t) count);
        for (int roll = 0; roll < count; ++roll)
        {
            std::ostringstream name;
            name << "roll_" << std::setw (2) << std::setfill ('0') << roll;
            const auto dir = std::filesystem::path (outDir) / folderOf (style, knobs.styleFolders) / name.str();
            std::filesystem::create_directories (dir);
            std::uint32_t seeds[rolla::NumParts];
            for (int p = 0; p < rolla::NumParts; ++p)
                seeds[p] = seed + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u + (std::uint32_t) style * 101u;
            rolla::Groove groove {};
            RollRedraws redraws;
            // The originality floor refuses a roll it cannot clear (generate() returns
            // false and leaves the groove alone). A refused roll is never written.
            if (! rolla::generate (groove, settings, seeds, 0x7fu))
            {
                ++origFails;
                std::cerr << "ORIG_REFUSED " << rolla::dialectId (style) << " " << name.str()
                          << ": no composition cleared the originality floor; roll not written\n";
                continue;
            }
            redraws.take (groove);
            // v2.1 skeleton cap is a runtime redraw, not a constant fitted to the gate seeds.
            for (int guard = 0; guard < 8; ++guard)
            {
                bool close = false;
                for (const auto& prev : seen)
                    if (rolla::bassSkeletonSimilarity (groove, prev) >= 0.55f)
                        close = true;
                if (! close)
                    break;
                seeds[rolla::Bass] ^= 0x6D2B79F5u * (std::uint32_t) (guard + 1);
                ++redraws.skeleton;
                if (! rolla::generate (groove, settings, seeds, 0x7fu))
                {
                    // Refused: groove still holds the last roll that cleared.
                    ++origFails;
                    std::cerr << "ORIG_REFUSED " << rolla::dialectId (style) << " " << name.str()
                              << " (skeleton redraw " << guard << "): kept the last original draw\n";
                    break;
                }
                redraws.take (groove);
            }
            writeRollJson (dir, style, name.str(), seeds, redraws);
            seen.push_back (groove);
            rolla::Groove arranged;
            const auto plan = rolla::planIntensity (style, knobs.intensity, settings.stab);
            rolla::applyIntensity (groove, arranged, plan, 0, 0);
            int preset[rolla::NumParts] {};
            int samplePick[rolla::NumParts] {};
            for (int p = 0; p < rolla::NumParts; ++p)
            {
                preset[p] = row.presetIndex[p] >= 0 ? row.presetIndex[p] : 0;
                const int rolled = rolla::pickPresetForRoll (bank, p, style, seeds[p]);
                if (rolled >= 0)
                    preset[p] = rolled;
                samplePick[p] = (int) (seeds[p] % 10007u);
            }
            std::string fileOf[rolla::NumParts];
            std::string drumOf[rolla::NumDrumVoices];
            if (index != nullptr && library != nullptr)
            {
                for (int p = 0; p < rolla::Drums; ++p)
                    fileOf[p] = pickedFile (index.get(), rolla::specAt (bank, p, preset[p]), p, -1, samplePick[p], style);
                const auto& drums = rolla::specAt (bank, rolla::Drums, preset[rolla::Drums]);
                fileOf[rolla::Drums] = pickedFile (index.get(), drums, rolla::Drums, rolla::Kick, samplePick[rolla::Drums], style);
                const int voices[] = { rolla::Kick, rolla::Snare, rolla::Clap, rolla::Rim, rolla::HatC, rolla::HatO, rolla::Ride, rolla::Perc };
                for (int voice : voices)
                    drumOf[voice] = pickedFile (index.get(), drums, rolla::Drums, voice, samplePick[rolla::Drums], style);
                // The same list the plugin preloads on ROLL, so the renderer cannot drift from it.
                // Bounded wait (logged) so no lane's first note plays before its file is in memory.
                rolla::prefetchPlayFiles (*library, bank, *index, preset, samplePick, style, 60000, "roll_batch");
            }
            std::vector<std::string> written;
            std::string error;
            if (! rolla::MidiExport::writeAll (dir.string(), arranged, settings.key, swing, 130.0, written, error))
            {
                std::cerr << "FAIL " << error << "\n";
                return 1;
            }
            if (knobs.midiOnly)
            {
                std::cout << rolla::dialectId (style) << " " << name.str() << " redraws=" << redraws.total() << " midi-only\n";
                continue;
            }
            const auto wav = juce::File (dir.string())
                                 .getChildFile ("ROLLA_" + juce::String (rolla::keyName (settings.key)) + "min_"
                                                + rolla::dialectId (style) + ".wav");
            const Rendered rendered = renderWav (bank, arranged, style, wav, library.get(), index.get(),
                                                 &mix, knobs, preset, samplePick, row.hitsMute, swing, false, -1);
            if (rendered.peak < 0.01f)
            {
                wav.deleteFile();
                wav.withFileExtension (".json").deleteFile();
                ++silent;
                std::cout << "SKIPPED clicks tails loudness " << rolla::dialectId (style) << " " << name.str()
                          << " redraws=" << redraws.total() << " (no sample energy)\n";
            }
            else
            {
                writeSidecar (wav, style, knobs, row.hitsMute, swing, bank, preset, samplePick, fileOf, drumOf, rendered.kickMidiSample,
                              redraws);
                if (knobs.stems)
                {
                    for (int part = 0; part < rolla::NumParts; ++part)
                    {
                        if ((knobs.stemMask & (1u << (unsigned) part)) == 0)
                            continue;
                        const auto stem = wav.getSiblingFile (wav.getFileNameWithoutExtension() + "_" + stemName (part) + ".wav");
                        renderWav (bank, arranged, style, stem, library.get(), index.get(),
                                   &mix, knobs, preset, samplePick, row.hitsMute, swing, true, part);
                    }
                }
                ++wavs;
                std::cout << rolla::dialectId (style) << " " << name.str()
                          << " redraws=" << redraws.total()
                          << " peak=" << rendered.peak
                          << " kickMidi=" << rendered.kickMidiSample
                          << " bass=" << presetName (bank, rolla::Bass, preset[rolla::Bass])
                          << " kick=" << drumOf[rolla::Kick] << "\n";
            }
        }
    }
    std::cout << "wav files " << wavs << " skipped " << silent << " orig_refused " << origFails << "\n";
    if (! samples.empty() && wavs == 0)
        return 1;
    return origFails > 0 ? 1 : 0;
}

} // namespace

const char* usageText()
{
    return "rolla-rolls --samples DIR --style roller|deep|bouncy|jackin|ukmin|long|pop|all --count N --seed S\n"
           "            [--out DIR] [--stems] [--stem-lanes DRUMS,BASS] [--folder-names ids|styles] [--midi-only]\n"
           "            (style is case-insensitive; folders default to the ids ukmin/long/pop/jackin,\n"
           "            'styles' names them roller/deep/bouncy/jackin)\n"
           "            [--knob DARK=0.55] [--knob BUSY=0.40] [--knob BOUNCE=0.45] [--knob STAB=0.55]\n"
           "            [--knob TONE=0.5] [--knob PUNCH=0.5] [--knob LEVEL=0] [--knob INTENSITY=75]\n"
           "            [--knob BASS.TONE=0.5] [--knob BASS.PUNCH=0.5] [--knob BASS.LEVEL=0]\n"
           "            [--orig-index FILE|none]   hashed reference index (default: $ROLLA_ORIG_INDEX, else\n"
           "                                       <ROLLA settings folder>/Originality/orig_index.json if present)\n"
           "rolla-rolls --build-orig-index DIR [--orig-out FILE]\n"
           "            hash every .mid/.midi under DIR into an index (default out: the default index path);\n"
           "            prints the id -> file list, which stays out of the index\n";
}

namespace
{

// The external index for this run: an explicit file, "none", or the default path if it exists.
// The shipped banned-skeleton index loads as in the plugin ($ROLLA_GENRE_DATA_DIR/orig_builtin.json).
bool loadOrigIndex (const std::string& choice)
{
    {
        juce::String why;
        auto shipped = rolla::orig::loadIndex (rolla::orig::builtinIndexFile(), &why);
        std::cout << "originality: shipped banned-skeleton index "
                  << (shipped != nullptr ? juce::String ((int) shipped->refs.size()) + " streams" : "absent (" + why + ")") << "\n";
        rolla::orig::setBuiltinIndex (std::move (shipped));
    }
    if (choice == "none")
    {
        rolla::orig::setIndex (nullptr);
        std::cout << "originality: built-in guard only (--orig-index none)\n";
        return true;
    }
    const juce::File file = choice.empty() ? rolla::orig::defaultIndexFile() : juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (choice));
    juce::String error;
    auto index = rolla::orig::loadIndex (file, &error);
    if (index == nullptr)
    {
        rolla::orig::setIndex (nullptr);
        if (! choice.empty())
        {
            std::cerr << "FAIL --orig-index " << choice << ": " << error << "\n";
            return false;
        }
        std::cout << "originality: built-in guard only (" << error << ")\n";
        return true;
    }
    std::cout << "originality: built-in guard + " << index->refs.size() << " reference streams from "
              << file.getFullPathName() << "\n";
    rolla::orig::setIndex (std::move (index));
    return true;
}

int buildOrigIndex (const std::string& folder, const std::string& outPath)
{
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (folder));
    if (folder.empty() || ! dir.isDirectory())
    {
        std::cerr << "FAIL --build-orig-index needs a folder of reference MIDI\n";
        return 2;
    }
    rolla::orig::Index index;
    const auto report = rolla::orig::buildIndex (dir, index);
    const juce::File out = outPath.empty() ? rolla::orig::defaultIndexFile() : juce::File::getCurrentWorkingDirectory().getChildFile (juce::String (outPath));
    if (! rolla::orig::writeIndex (index, out))
    {
        std::cerr << "FAIL could not write " << out.getFullPathName() << "\n";
        return 1;
    }
    for (const auto& row : report.idToPath)
        std::cout << row.first << "  " << row.second << "\n";
    std::cout << "wrote " << out.getFullPathName() << ": " << report.files << " files, "
              << report.streams << " reference streams (hashes only)\n";
    return report.streams > 0 ? 0 : 1;
}

} // namespace

int main (int argc, char** argv)
{
    // Never the real user profile: settings and caches go to a scratch folder
    // (or the one a calling script set up). --samples DIR is still read in place.
    if (rolla::test::useScratchProfile ("rolls", true).empty())
    {
        std::cerr << "could not set up a scratch profile\n";
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI juce;

    std::string samples, style = "all", out = "rolls";
    int count = 20;
    std::uint32_t seed = 1;
    std::string origIndex, buildFrom, origOut;
    bool batch = false;
    Knobs knobs;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        const char* value = nullptr;
        auto take = [&] (const char* flag) -> bool
        {
            if (arg != flag || i + 1 >= argc)
                return false;
            value = argv[++i];
            return true;
        };
        if (arg == "--help")
        {
            std::cout << usageText();
            return 0;
        }
        if (arg == "--stems") { knobs.stems = true; batch = true; }
        else if (arg == "--midi-only") { knobs.midiOnly = true; batch = true; }
        else if (take ("--stem-lanes"))
        {
            // Comma list of BASS,STAB,HITS,PLUCK,PAD,VOX,DRUMS. Implies --stems.
            knobs.stems = true;
            knobs.stemMask = 0;
            const juce::StringArray names = juce::StringArray::fromTokens (juce::String (value).toUpperCase(), ",", "");
            for (int part = 0; part < rolla::NumParts; ++part)
                if (names.contains (stemName (part)))
                    knobs.stemMask |= 1u << (unsigned) part;
            batch = true;
        }
        else if (take ("--samples")) { samples = value; batch = true; }
        else if (take ("--style")) { style = value; batch = true; }
        else if (take ("--count")) { count = std::max (1, std::atoi (value)); batch = true; }
        else if (take ("--seed")) { seed = (std::uint32_t) std::strtoul (value, nullptr, 0); batch = true; }
        else if (take ("--out")) { out = value; batch = true; }
        else if (take ("--folder-names"))
        {
            const std::string v = value;
            if (v != "ids" && v != "styles")
            {
                std::cerr << "bad --folder-names " << v << " (ids|styles)\n" << usageText();
                return 2;
            }
            knobs.styleFolders = v == "styles";
            batch = true;
        }
        else if (take ("--orig-index")) { origIndex = value; }
        else if (take ("--build-orig-index")) { buildFrom = value; }
        else if (take ("--orig-out")) { origOut = value; }
        else if (take ("--knob"))
        {
            if (! parseKnob (value, knobs))
            {
                std::cerr << "bad knob " << value << "\n" << usageText();
                return 2;
            }
            batch = true;
        }
    }
    if (! buildFrom.empty())
        return buildOrigIndex (buildFrom, origOut);
    if (! loadOrigIndex (origIndex))
        return 2;
    if (batch)
        return renderBatch (samples, style, count, seed, out, knobs);

    const std::filesystem::path root = argc > 1 && argv[1][0] != '-' ? argv[1] : "tools/out";
    std::filesystem::create_directories (root);

    rolla::StyleStat stats[rolla::NumDialects] {};
    const int nondet = rolla::measureVariety (stats);
    rolla::MacroDelta busy, bounce, dark, stab;
    rolla::measureMacros (busy, bounce, dark, stab);
    writeStats (root / "STATS.md", stats, busy, bounce, dark, stab);
    std::cout << "wrote " << (root / "STATS.md") << "\n";

    int failures = nondet;
    for (int dialect = 0; dialect < rolla::NumDialects; ++dialect)
    {
        std::cout << rolla::dialectName (dialect) << " rules " << stats[dialect].rulesPassed
                  << "/" << stats[dialect].rolls << "\n";
        const int lanes[] = { rolla::Bass, rolla::Stab, rolla::Hits, rolla::Pluck, rolla::Pad, rolla::Vox, rolla::Drums };
        for (int part : lanes)
        {
            const auto& lane = stats[dialect].lane[part];
            std::cout << "  " << rolla::partId (part) << " unique " << lane.unique
                      << " edit " << lane.meanEdit << "\n";
            if (lane.unique < 5000 || stats[dialect].rulesPassed != stats[dialect].rolls)
                ++failures;
        }
    }

    rolla::PresetBank bank;
    rolla::loadPresetsFromJson (bank, BinaryData::presets_json, BinaryData::presets_jsonSize);

    constexpr int kRolls = 20;
    constexpr double bpm = 130.0;
    int midis = 0;
    int wavs = 0;
    bool toldSilence = false;
    std::vector<rolla::Groove> seen[rolla::NumDialects];
    for (int dialect = 0; dialect < rolla::NumDialects; ++dialect)
    {
        const Knobs plain;
        const auto settings = rollSettings (dialect, plain);
        for (int roll = 0; roll < kRolls; ++roll)
        {
            std::ostringstream name;
            name << "roll_" << std::setw (2) << std::setfill ('0') << roll;
            const auto dir = root / rolla::dialectId (dialect) / name.str();
            std::filesystem::create_directories (dir);

            std::uint32_t seeds[rolla::NumParts];
            seedsFor (dialect, roll, seeds);
            rolla::Groove groove {};
            RollRedraws redraws;
            if (! rolla::generate (groove, settings, seeds, 0x7fu))
            {
                std::cerr << "FAIL ORIG_REFUSED " << rolla::dialectId (dialect) << " " << name.str() << "\n";
                ++failures;
                continue;
            }
            redraws.take (groove);
            for (int guard = 0; guard < 16; ++guard)
            {
                bool close = false;
                for (const auto& prev : seen[dialect])
                    if (rolla::bassSkeletonSimilarity (groove, prev) >= 0.55f)
                        close = true;
                if (! close)
                    break;
                seeds[rolla::Bass] ^= 0x9E3779B9u + (std::uint32_t) guard * 0x85EBCA6Bu;
                ++redraws.skeleton;
                if (! rolla::generate (groove, settings, seeds, 0x7fu))
                {
                    std::cerr << "FAIL ORIG_REFUSED " << rolla::dialectId (dialect) << " " << name.str()
                              << " (skeleton redraw " << guard << ")\n";
                    ++failures;
                    break;
                }
                redraws.take (groove);
            }
            seen[dialect].push_back (groove);
            writeRollJson (dir, dialect, name.str(), seeds, redraws);

            std::vector<std::string> written;
            std::string error;
            if (! rolla::MidiExport::writeAll (dir.string(), groove, settings.key, swingFor (dialect), bpm, written, error))
            {
                std::cerr << "FAIL " << error << "\n";
                ++failures;
            }
            else
                midis += (int) written.size();

            const auto wav = juce::File (dir.string())
                                 .getChildFile ("ROLLA_" + juce::String (rolla::keyName (settings.key)) + "min_"
                                                + rolla::dialectId (dialect) + ".wav");
            int preset[rolla::NumParts] {};
            int samplePick[rolla::NumParts] {};
            const float peak = renderWav (bank, groove, dialect, wav, nullptr, nullptr, nullptr, plain,
                                          preset, samplePick, false, swingFor (dialect), false, -1).peak;
            if (peak < 0.01f)
            {
                wav.deleteFile();
                if (! toldSilence)
                {
                    std::cerr << "SKIPPED clicks tails loudness: factory presets are sample-only and no sample library is loaded.\n";
                    toldSilence = true;
                }
            }
            else
                ++wavs;
            std::cout << rolla::dialectId (dialect) << " " << name.str()
                      << " bass=" << groove.lane[rolla::Bass].count
                      << " stab=" << groove.lane[rolla::Stab].count
                      << " guard=" << groove.guardRef
                      << " redraws=" << redraws.total()
                      << " peak=" << peak << "\n";
        }
    }
    std::cout << "midi files " << midis << " wav files " << wavs << "\n";
    std::cout << (failures ? "FAIL\n" : "PASS\n");
    return failures ? 1 : 0;
}
