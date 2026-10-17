#include "PatternStats.h"
#include "Engine/Engine.h"
#include "Generator/CorpusData.h"
#include "Generator/Generator.h"
#include "Generator/MidiFile.h"
#include "Generator/Presets.h"

#include <BinaryData.h>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{

float swingFor (int dialect)
{
    switch (dialect)
    {
        case rolla::UkMin: return 0.52f;
        case rolla::Long: return 0.51f;
        default: return 0.50f;
    }
}

rolla::Settings rollSettings (int dialect)
{
    rolla::Settings s;
    s.dark = 0.55f;
    s.busy = 0.40f;
    s.bounce = 0.45f;
    s.stab = 0.55f;
    s.dialect = dialect;
    s.key = 2;
    s.bars = 8;
    s.fill = false;
    return s;
}

void seedsFor (int style, int roll, std::uint32_t seeds[rolla::NumParts])
{
    for (int p = 0; p < rolla::NumParts; ++p)
        seeds[p] = (std::uint32_t) (roll + 1) * 0x85EBCA6Bu
                 ^ ((std::uint32_t) style * 0xC2B2AE35u + (std::uint32_t) p * 0x27D4EB2Du);
}

bool writeWav (const juce::File& file, const juce::AudioBuffer<float>& audio, double sr)
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
    return writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

float renderWav (const rolla::PresetBank& bank, const rolla::Groove& groove, int dialect, const juce::File& file,
                const rolla::SampleLibrary* library, const rolla::SampleIndex* index)
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
        params.tone[i] = 0.5f;
        params.punch[i] = 0.5f;
        params.levelDb[i] = 0.f;
        params.mute[i] = false;
        params.preset[i] = 0;
    }
    params.duck = true;
    params.drumsToMain = true;
    params.swing = swingFor (dialect);
    params.outputDb = 0.f;
    params.drumsAccent = 0.6f;
    params.bars = bars;
    params.key = groove.key;
    params.dialect = dialect;
    params.presets = &bank;
    params.library = library;
    params.sampleIndex = index;

    juce::AudioBuffer<float> audio (2, total);
    audio.clear();
    int done = 0;
    double ppq = 0.0;
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
        if (transport.playing)
            ppq += (double) n * bpm / (60.0 * sr);
        done += n;
    }

    float peak = 0.f;
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* data = audio.getReadPointer (ch);
        for (int i = 0; i < total; ++i)
            peak = std::max (peak, std::fabs (data[i]));
    }
    if (! writeWav (file, audio, sr))
        std::cerr << "wav write failed " << file.getFullPathName() << "\n";
    return peak;
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
    out << "Vocabulary extracted by tools/extract_corpus.py into data/corpus.json and src/Generator/CorpusData.h.\n\n";
    out << "| cells | count |\n| --- | ---: |\n";
    out << "| bass | " << rolla::corpus::kBassN << " |\n";
    out << "| contours | " << rolla::corpus::kContourN << " |\n";
    out << "| stab | " << rolla::corpus::kStabN << " |\n";
    out << "| voicings | " << rolla::corpus::kVoicingN << " |\n";
    out << "| drums | " << rolla::corpus::kDrumN << " |\n";
    out << "| chops | " << rolla::corpus::kChopN << " |\n";
    out << "| banned 2-bar windows | " << rolla::corpus::kBanN << " |\n\n";
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
           "Swing is 52% UK MIN, 51% LONG, 50% POP and GASKIN 90s. WAV files are gitignored.\n";
}

int styleId (const std::string& name)
{
    const std::string s = name;
    if (s == "ukmin" || s == "roller" || s == "0") return rolla::UkMin;
    if (s == "long" || s == "deep" || s == "1") return rolla::Long;
    if (s == "pop" || s == "bouncy" || s == "2") return rolla::Pop;
    if (s == "gaskin" || s == "jackin" || s == "3") return rolla::Gaskin;
    if (s == "all") return -2;
    return -1;
}

void preloadPack (rolla::SampleLibrary& library, const rolla::SampleIndex& index)
{
    std::vector<std::string> paths;
    auto add = [&] (int part, int voice)
    {
        rolla::PoolFilter filter;
        filter.on = true;
        filter.lane = part;
        filter.voice = voice;
        if (const auto* entry = rolla::sampleIndexAt (index, part, filter, 0))
        {
            library.request (entry->path);
            paths.emplace_back (entry->path);
        }
    };
    const int parts[] = { rolla::Bass, rolla::Stab, rolla::Hits, rolla::Pluck, rolla::Pad, rolla::Vox };
    for (int part : parts)
        add (part, -1);
    const int voices[] = { rolla::Kick, rolla::Snare, rolla::Clap, rolla::Rim, rolla::HatC, rolla::HatO, rolla::Ride, rolla::Perc };
    for (int voice : voices)
        add (rolla::Drums, voice);
    const auto readyBy = juce::Time::getMillisecondCounter() + 20000;
    while (juce::Time::getMillisecondCounter() < readyBy)
    {
        bool waiting = false;
        for (const auto& path : paths)
            if (! library.ready (path.c_str()) && ! library.missing (path.c_str()))
                waiting = true;
        if (! waiting)
            break;
        juce::Thread::sleep (20);
    }
}

int renderBatch (const std::string& samples, const std::string& styleName, int count, std::uint32_t seed, const std::string& outDir)
{
    const int one = styleId (styleName);
    if (one == -1 || count < 1)
    {
        std::cerr << "usage: rolla-rolls --samples DIR --style ukmin|long|pop|gaskin|all --count N --seed S [--out DIR]\n";
        return 2;
    }
    rolla::PresetBank bank;
    rolla::loadPresetsFromJson (bank, BinaryData::presets_json, BinaryData::presets_jsonSize);

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
        if (index != nullptr)
            preloadPack (*library, *index);
        else
            std::cerr << "sample index did not finish; WAV energy may be missing\n";
    }

    const int styles[4] = { rolla::UkMin, rolla::Long, rolla::Pop, rolla::Gaskin };
    const int nStyles = one == -2 ? 4 : 1;
    int wavs = 0;
    int silent = 0;
    for (int s = 0; s < nStyles; ++s)
    {
        const int style = one == -2 ? styles[s] : one;
        const auto settings = rollSettings (style);
        for (int roll = 0; roll < count; ++roll)
        {
            std::ostringstream name;
            name << "roll_" << std::setw (2) << std::setfill ('0') << roll;
            const auto dir = std::filesystem::path (outDir) / rolla::dialectId (style) / name.str();
            std::filesystem::create_directories (dir);
            std::uint32_t seeds[rolla::NumParts];
            for (int p = 0; p < rolla::NumParts; ++p)
                seeds[p] = seed + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u + (std::uint32_t) style * 101u;
            rolla::Groove groove {};
            rolla::generate (groove, settings, seeds, 0x7fu);
            std::vector<std::string> written;
            std::string error;
            if (! rolla::MidiExport::writeAll (dir.string(), groove, settings.key, swingFor (style), 130.0, written, error))
            {
                std::cerr << "FAIL " << error << "\n";
                return 1;
            }
            const auto wav = juce::File (dir.string())
                                 .getChildFile ("ROLLA_" + juce::String (rolla::keyName (settings.key)) + "min_"
                                                + rolla::dialectId (style) + ".wav");
            const float peak = renderWav (bank, groove, style, wav, library.get(), index.get());
            if (peak < 0.01f)
            {
                wav.deleteFile();
                ++silent;
                std::cout << "SKIPPED clicks tails loudness " << rolla::dialectId (style) << " " << name.str()
                          << " (no sample energy)\n";
            }
            else
            {
                ++wavs;
                std::cout << rolla::dialectId (style) << " " << name.str() << " peak=" << peak << "\n";
            }
        }
    }
    std::cout << "wav files " << wavs << " skipped " << silent << "\n";
    if (! samples.empty() && wavs == 0)
        return 1;
    return 0;
}

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

    std::string samples, style = "all", out = "rolls";
    int count = 20;
    std::uint32_t seed = 1;
    bool batch = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        auto take = [&] (const char* flag) -> const char*
        {
            if (arg != flag || i + 1 >= argc)
                return nullptr;
            return argv[++i];
        };
        if (arg == "--help")
        {
            std::cout << "rolla-rolls --samples DIR --style ukmin|long|pop|gaskin|all --count N --seed S [--out DIR]\n";
            return 0;
        }
        if (const char* v = take ("--samples")) { samples = v; batch = true; }
        else if (const char* v = take ("--style")) { style = v; batch = true; }
        else if (const char* v = take ("--count")) { count = std::max (1, std::atoi (v)); batch = true; }
        else if (const char* v = take ("--seed")) { seed = (std::uint32_t) std::strtoul (v, nullptr, 0); batch = true; }
        else if (const char* v = take ("--out")) { out = v; batch = true; }
    }
    if (batch)
        return renderBatch (samples, style, count, seed, out);

    const std::filesystem::path root = argc > 1 ? argv[1] : "tools/out";
    std::filesystem::create_directories (root);

    rolla::StyleStat stats[rolla::NumDialects] {};
    const int nondet = rolla::measureVariety (stats);
    rolla::MacroDelta busy, bounce, dark, stab;
    rolla::measureMacros (busy, bounce, dark, stab);
    writeStats (root / "STATS.md", stats, busy, bounce, dark, stab);
    std::cout << "wrote " << (root / "STATS.md") << "\n";

    int failures = nondet;
    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        std::cout << rolla::dialectName (style) << " rules " << stats[style].rulesPassed
                  << "/" << stats[style].rolls << "\n";
        const int lanes[] = { rolla::Bass, rolla::Stab, rolla::Hits, rolla::Pluck, rolla::Pad, rolla::Vox, rolla::Drums };
        for (int part : lanes)
        {
            const auto& lane = stats[style].lane[part];
            std::cout << "  " << rolla::partId (part) << " unique " << lane.unique
                      << " edit " << lane.meanEdit << "\n";
            if (lane.unique < 5000 || stats[style].rulesPassed != stats[style].rolls)
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
    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        const auto settings = rollSettings (style);
        for (int roll = 0; roll < kRolls; ++roll)
        {
            std::ostringstream name;
            name << "roll_" << std::setw (2) << std::setfill ('0') << roll;
            const auto dir = root / rolla::dialectId (style) / name.str();
            std::filesystem::create_directories (dir);

            std::uint32_t seeds[rolla::NumParts];
            seedsFor (style, roll, seeds);
            rolla::Groove groove {};
            rolla::generate (groove, settings, seeds, 0x7fu);
            for (int guard = 0; guard < 16; ++guard)
            {
                bool close = false;
                for (const auto& prev : seen[style])
                    if (rolla::bassSkeletonSimilarity (groove, prev) >= 0.55f)
                        close = true;
                if (! close)
                    break;
                seeds[rolla::Bass] ^= 0x9E3779B9u + (std::uint32_t) guard * 0x85EBCA6Bu;
                rolla::generate (groove, settings, seeds, 0x7fu);
            }
            seen[style].push_back (groove);

            std::vector<std::string> written;
            std::string error;
            if (! rolla::MidiExport::writeAll (dir.string(), groove, settings.key, swingFor (style), bpm, written, error))
            {
                std::cerr << "FAIL " << error << "\n";
                ++failures;
            }
            else
                midis += (int) written.size();

            const auto wav = juce::File (dir.string())
                                 .getChildFile ("ROLLA_" + juce::String (rolla::keyName (settings.key)) + "min_"
                                                + rolla::dialectId (style) + ".wav");
            const float peak = renderWav (bank, groove, style, wav, nullptr, nullptr);
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
            std::cout << rolla::dialectId (style) << " " << name.str()
                      << " bass=" << groove.lane[rolla::Bass].count
                      << " stab=" << groove.lane[rolla::Stab].count
                      << " guard=" << groove.guardRef
                      << " peak=" << peak << "\n";
        }
    }
    std::cout << "midi files " << midis << " wav files " << wavs << "\n";
    std::cout << (failures ? "FAIL\n" : "PASS\n");
    return failures ? 1 : 0;
}
