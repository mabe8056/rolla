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

float renderWav (const rolla::PresetBank& bank, const rolla::Groove& groove, int dialect, const juce::File& file)
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

} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juce;

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
            const float peak = renderWav (bank, groove, style, wav);
            if (peak < 0.01f)
            {
                std::cerr << "FAIL silence " << wav.getFullPathName() << "\n";
                ++failures;
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
