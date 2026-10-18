#pragma once

// rolla-fingerprint: the one feature extractor, run on both sides (Job G2).
//
//   rolla-fingerprint scan <dir> [--lane LANE] --out fp.json
//       Every wav/aif under dir. The lane comes from the folder (KICK, CH, OH, BASS,
//       STAB, VOX/chops, ...) unless --lane is given.
//   rolla-fingerprint stems <dir> [--styles track_style.csv] --out fp.json
//       Demucs stems at <dir>/<track>/sep/{drums,bass,other,vocals}.wav (chunked
//       copies are skipped). Onsets are sliced and labelled: drums by band (kick,
//       hat, clap, perc), bass, other (pad when it rings 400 ms, else stab), vocals.
//       The CSV is track_id,style (ukmin, long, pop, gaskin); private, never committed.
//   rolla-fingerprint targets <fp.json>... [--k 3] --out genre_targets.json
//       Per style and lane: normalised k-means regions and a hit threshold.
//
// The plugin reads genre_targets.json from the bundle's Resources/Genre folder
// (injected at packaging, like the sample pack). Reference audio, fingerprints and
// targets stay private.
//
// Header-only so rolla-sampler-test runs the same code in CI.

#include "../Sampler/Fingerprint.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace rolla::fptool
{

struct Entry
{
    juce::String path, lane, track, style;
    Fingerprint fp;
};

inline const juce::StringArray& lanes()
{
    static const juce::StringArray all { "KICK", "CLAP", "SNARE", "HAT_C", "HAT_O", "PERC", "RIDE",
                                         "BASS", "STAB", "PLUCK", "PAD", "HITS", "VOX" };
    return all;
}

// Folder names to a lane, nearest folder first. Empty when none fits.
inline juce::String laneFromPath (const juce::String& relative)
{
    auto parts = juce::StringArray::fromTokens (relative.replaceCharacter ('\\', '/'), "/", "");
    for (int i = parts.size() - 2; i >= 0; --i)
    {
        const auto p = parts[i].toUpperCase();
        if (lanes().contains (p))
            return p;
        if (p == "CH" || p == "HATC" || p == "HH" || p == "CLOSED")
            return "HAT_C";
        if (p == "OH" || p == "HATO" || p == "OPEN")
            return "HAT_O";
        if (p == "RIM" || p == "SHAKER" || p == "TOM" || p == "TOMS")
            return "PERC";
    }
    return {};
}

inline bool readMono (const juce::File& file, std::vector<float>& out, double maxSeconds = 900.0)
{
    out.clear();
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate < 8000.0 || reader->lengthInSamples < 16)
        return false;
    const int n = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * maxSeconds));
    const int ch = (int) std::max (1u, reader->numChannels);
    juce::AudioBuffer<float> buf (ch, n);
    if (! reader->read (&buf, 0, n, 0, true, true))
        return false;
    std::vector<float> mono ((size_t) n, 0.f);
    for (int c = 0; c < buf.getNumChannels(); ++c)
    {
        const float* p = buf.getReadPointer (c);
        for (int i = 0; i < n; ++i)
            mono[(size_t) i] += p[i] / (float) buf.getNumChannels();
    }
    if (std::fabs (reader->sampleRate - kFingerprintRate) < 1.0)
    {
        out = std::move (mono);
        return true;
    }
    const double ratio = reader->sampleRate / kFingerprintRate;
    const int outN = (int) std::floor ((double) n / ratio) - 4;
    if (outN < 16)
        return false;
    out.assign ((size_t) outN, 0.f);
    juce::LagrangeInterpolator interp;
    interp.process (ratio, mono.data(), out.data(), outN);
    return true;
}

// Onsets: 5 ms RMS rising 2.5x over the last 100 ms, above -45 dB of the loudest
// hop, at least 60 ms apart. Slices run to the next onset, at most 1.5 s.
inline std::vector<std::pair<int, int>> sliceOnsets (const std::vector<float>& x)
{
    std::vector<std::pair<int, int>> out;
    const int hop = (int) (0.005 * kFingerprintRate);
    const int n = (int) x.size() / hop;
    if (n < 4)
        return out;
    std::vector<float> rms ((size_t) n);
    float top = 0.f;
    for (int h = 0; h < n; ++h)
    {
        double s = 0.0;
        for (int i = h * hop; i < (h + 1) * hop; ++i)
            s += (double) x[(size_t) i] * x[(size_t) i];
        rms[(size_t) h] = (float) std::sqrt (s / hop);
        top = std::max (top, rms[(size_t) h]);
    }
    const float quiet = top * std::pow (10.f, -45.f / 20.f);
    std::vector<int> onsets;
    for (int h = 1; h < n; ++h)
    {
        double recent = 0.0;
        const int from = std::max (0, h - 20);
        for (int j = from; j < h; ++j)
            recent += rms[(size_t) j];
        recent /= std::max (1, h - from);
        if (rms[(size_t) h] > quiet && rms[(size_t) h] > 2.5 * recent + 1.0e-6
            && (onsets.empty() || h - onsets.back() >= 12))
            onsets.push_back (h);
    }
    const int maxLen = (int) (1.5 * kFingerprintRate);
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int a = std::max (0, onsets[i] * hop - hop);
        const int next = i + 1 < onsets.size() ? onsets[i + 1] * hop - hop : (int) x.size();
        const int b = std::min ({ next, a + maxLen, (int) x.size() });
        if (b - a >= (int) (0.04 * kFingerprintRate))
            out.emplace_back (a, b);
    }
    return out;
}

// Band shares of the first 60 ms: under 150 Hz, 150 Hz - 5 kHz, over 5 kHz.
inline void bandShares (const float* x, int n, double& low, double& mid, double& high)
{
    const int N = 4096;
    std::vector<std::complex<double>> buf ((size_t) N);
    const int m = std::min (n, (int) (0.06 * kFingerprintRate));
    for (int i = 0; i < N; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * pitchprobe::kPi * i / (N - 1));
        buf[(size_t) i] = { i < m ? (double) x[i] * w : 0.0, 0.0 };
    }
    pitchprobe::fft (buf);
    low = mid = high = 0.0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * kFingerprintRate / N, p = std::norm (buf[(size_t) k]);
        (f < 150.0 ? low : (f < 5000.0 ? mid : high)) += p;
    }
    const double t = std::max (1.0e-12, low + mid + high);
    low /= t;
    mid /= t;
    high /= t;
}

inline juce::String drumLane (const float* x, int n, const Fingerprint& fp)
{
    double low, mid, high;
    bandShares (x, n, low, mid, high);
    if (low >= 0.5)
        return "KICK";
    if (high >= 0.45 && low < 0.1)
        return fp.decay40Ms < 150.f ? "HAT_C" : "HAT_O";
    if (low < 0.2 && mid + high >= 0.8 && fp.flatness > 0.3f)
        return "CLAP";
    return "PERC";
}

inline std::map<juce::String, juce::String> readStyles (const juce::File& csv)
{
    std::map<juce::String, juce::String> out;
    juce::StringArray lines;
    csv.readLines (lines);
    for (const auto& line : lines)
    {
        auto cells = juce::StringArray::fromTokens (line, ",", "\"");
        if (cells.size() < 2 || cells[0].trim().equalsIgnoreCase ("track_id"))
            continue;
        out[cells[0].trim()] = cells[1].trim().toLowerCase();
    }
    return out;
}

inline std::vector<Entry> scanDir (const juce::File& dir, const juce::String& laneOverride)
{
    std::vector<Entry> out;
    auto files = dir.findChildFiles (juce::File::findFiles, true, "*.wav;*.WAV;*.aif;*.aiff;*.AIF;*.AIFF");
    files.sort();
    std::vector<float> mono;
    for (const auto& f : files)
    {
        Entry e;
        e.path = f.getRelativePathFrom (dir).replaceCharacter ('\\', '/');
        e.lane = laneOverride.isNotEmpty() ? laneOverride : laneFromPath (e.path);
        if (e.lane.isEmpty() || ! readMono (f, mono, 20.0))
            continue;
        e.fp = fingerprintOf (mono.data(), (int) mono.size());
        if (e.fp.valid)
            out.push_back (std::move (e));
    }
    return out;
}

inline std::vector<Entry> sliceStems (const juce::File& dir, const std::map<juce::String, juce::String>& styles,
                                      int maxPerStem = 300)
{
    std::vector<Entry> out;
    auto tracks = dir.findChildFiles (juce::File::findDirectories, false);
    tracks.sort();
    std::vector<float> mono;
    for (const auto& track : tracks)
    {
        const auto sep = track.getChildFile ("sep");
        if (! sep.isDirectory())
            continue;
        const auto id = track.getFileName();
        const auto found = styles.find (id);
        const juce::String style = found != styles.end() ? found->second : juce::String();
        for (const char* stem : { "drums", "bass", "other", "vocals" })
        {
            const auto file = sep.getChildFile (juce::String (stem) + ".wav");
            if (! file.existsAsFile() || ! readMono (file, mono))
                continue;
            int kept = 0;
            for (const auto& [a, b] : sliceOnsets (mono))
            {
                if (kept >= maxPerStem)
                    break;
                Entry e;
                e.fp = fingerprintOf (mono.data() + a, b - a);
                if (! e.fp.valid)
                    continue;
                const juce::String s (stem);
                if (s == "drums")
                    e.lane = drumLane (mono.data() + a, b - a, e.fp);
                else if (s == "bass")
                    e.lane = "BASS";
                else if (s == "other")
                    e.lane = e.fp.decay40Ms >= 400.f ? "PAD" : "STAB";
                else
                    e.lane = "VOX";
                e.track = id;
                e.style = style;
                e.path = id + "/sep/" + s + ".wav@" + juce::String (a);
                out.push_back (std::move (e));
                ++kept;
            }
        }
    }
    return out;
}

inline const char* qualityName (ChordQuality q)
{
    switch (q)
    {
        case ChordQuality::Major: return "maj";
        case ChordQuality::Minor: return "min";
        case ChordQuality::Neutral: return "neutral";
        default: return "";
    }
}

inline juce::var toJson (const std::vector<Entry>& entries)
{
    juce::Array<juce::var> files;
    for (const auto& e : entries)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("path", e.path);
        o->setProperty ("lane", e.lane);
        if (e.track.isNotEmpty())
            o->setProperty ("track", e.track);
        if (e.style.isNotEmpty())
            o->setProperty ("style", e.style);
        auto* f = new juce::DynamicObject();
        juce::Array<juce::var> mean, spread;
        for (int i = 0; i < kMfcc; ++i)
        {
            mean.add (e.fp.mfccMean[i]);
            spread.add (e.fp.mfccStd[i]);
        }
        f->setProperty ("mfcc_mean", mean);
        f->setProperty ("mfcc_std", spread);
        f->setProperty ("centroid_hz", e.fp.centroidHz);
        f->setProperty ("rolloff85_hz", e.fp.rolloffHz);
        f->setProperty ("flatness", e.fp.flatness);
        f->setProperty ("attack_ms", e.fp.attackMs);
        f->setProperty ("decay20_ms", e.fp.decay20Ms);
        f->setProperty ("decay40_ms", e.fp.decay40Ms);
        f->setProperty ("sub_ratio", e.fp.subRatio);
        f->setProperty ("rms_db", e.fp.rmsDb);
        f->setProperty ("root_pitch", e.fp.pitch.rootPitch);
        f->setProperty ("root_conf", e.fp.pitch.conf);
        f->setProperty ("is_chord", e.fp.pitch.isChord);
        f->setProperty ("quality", qualityName (e.fp.pitch.quality));
        o->setProperty ("f", juce::var (f));
        files.add (juce::var (o));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("extractor", "rolla-fingerprint/1");
    root->setProperty ("files", files);
    return juce::var (root);
}

inline Fingerprint fingerprintFromJson (const juce::var& f)
{
    Fingerprint fp;
    const auto* mean = f["mfcc_mean"].getArray();
    const auto* spread = f["mfcc_std"].getArray();
    if (mean == nullptr || spread == nullptr || mean->size() != kMfcc || spread->size() != kMfcc)
        return fp;
    for (int i = 0; i < kMfcc; ++i)
    {
        fp.mfccMean[i] = (float) (double) (*mean)[i];
        fp.mfccStd[i] = (float) (double) (*spread)[i];
    }
    fp.centroidHz = (float) (double) f["centroid_hz"];
    fp.rolloffHz = (float) (double) f["rolloff85_hz"];
    fp.flatness = (float) (double) f["flatness"];
    fp.attackMs = (float) (double) f["attack_ms"];
    fp.decay20Ms = (float) (double) f["decay20_ms"];
    fp.decay40Ms = (float) (double) f["decay40_ms"];
    fp.subRatio = (float) (double) f["sub_ratio"];
    fp.rmsDb = (float) (double) f["rms_db"];
    fp.valid = true;
    return fp;
}

inline juce::var buildTargets (const std::vector<Entry>& entries, int k)
{
    std::vector<std::vector<float>> all;
    for (const auto& e : entries)
        if (e.style.isNotEmpty() && e.fp.valid)
            all.push_back (e.fp.vector());
    const auto norm = FeatureNorm::of (all);
    std::map<juce::String, std::map<juce::String, std::vector<std::vector<float>>>> groups;
    for (const auto& e : entries)
        if (e.style.isNotEmpty() && e.fp.valid)
            groups[e.style][e.lane].push_back (norm.apply (e.fp.vector()));

    auto floats = [] (const std::vector<float>& v)
    {
        juce::Array<juce::var> a;
        for (float x : v)
            a.add (x);
        return a;
    };
    auto* styles = new juce::DynamicObject();
    for (const auto& [style, byLane] : groups)
    {
        auto* lanesObj = new juce::DynamicObject();
        for (const auto& [lane, xs] : byLane)
        {
            const auto t = fitTarget (xs, k);
            juce::Array<juce::var> clusters;
            for (const auto& c : t.clusters)
            {
                auto* co = new juce::DynamicObject();
                co->setProperty ("centroid", floats (c.centroid));
                co->setProperty ("spread", floats (c.spread));
                co->setProperty ("weight", c.weight);
                clusters.add (juce::var (co));
            }
            auto* lo = new juce::DynamicObject();
            lo->setProperty ("clusters", clusters);
            lo->setProperty ("threshold", t.threshold);
            lo->setProperty ("count", (int) xs.size());
            lanesObj->setProperty (lane, juce::var (lo));
        }
        styles->setProperty (style, juce::var (lanesObj));
    }
    juce::Array<juce::var> names;
    for (int i = 0; i < kFingerprintDims; ++i)
        names.add (juce::String (fingerprintName (i)));
    auto* norms = new juce::DynamicObject();
    norms->setProperty ("mean", floats (norm.mean));
    norms->setProperty ("std", floats (norm.std));
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("extractor", "rolla-fingerprint/1");
    root->setProperty ("features", names);
    root->setProperty ("norm", juce::var (norms));
    root->setProperty ("styles", juce::var (styles));
    return juce::var (root);
}

inline std::vector<Entry> entriesFromJson (const juce::var& doc)
{
    std::vector<Entry> out;
    if (const auto* files = doc["files"].getArray())
        for (const auto& f : *files)
        {
            Entry e;
            e.path = f["path"].toString();
            e.lane = f["lane"].toString();
            e.track = f["track"].toString();
            e.style = f["style"].toString();
            e.fp = fingerprintFromJson (f["f"]);
            if (e.fp.valid && e.lane.isNotEmpty())
                out.push_back (std::move (e));
        }
    return out;
}

inline int usage()
{
    std::cerr << "usage:\n"
                 "  rolla-fingerprint scan <dir> [--lane LANE] --out fp.json\n"
                 "  rolla-fingerprint stems <dir> [--styles track_style.csv] --out fp.json\n"
                 "  rolla-fingerprint targets <fp.json>... [--k 3] --out genre_targets.json\n";
    return 2;
}

// args exclude the program name.
inline int run (const juce::StringArray& args)
{
    if (args.size() < 2)
        return usage();
    const auto mode = args[0];
    juce::String out, lane, styles;
    int k = 3;
    juce::StringArray inputs;
    for (int i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--out" && i + 1 < args.size())
            out = args[++i];
        else if (args[i] == "--lane" && i + 1 < args.size())
            lane = args[++i].toUpperCase();
        else if (args[i] == "--styles" && i + 1 < args.size())
            styles = args[++i];
        else if (args[i] == "--k" && i + 1 < args.size())
            k = juce::jlimit (1, 8, args[++i].getIntValue());
        else
            inputs.add (args[i]);
    }
    if (out.isEmpty() || inputs.isEmpty())
        return usage();
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    juce::var doc;
    if (mode == "scan")
    {
        const auto entries = scanDir (cwd.getChildFile (inputs[0]), lane);
        std::cout << "fingerprinted " << entries.size() << " files\n";
        doc = toJson (entries);
    }
    else if (mode == "stems")
    {
        std::map<juce::String, juce::String> map;
        if (styles.isNotEmpty())
            map = readStyles (cwd.getChildFile (styles));
        const auto entries = sliceStems (cwd.getChildFile (inputs[0]), map);
        std::cout << "fingerprinted " << entries.size() << " slices\n";
        doc = toJson (entries);
    }
    else if (mode == "targets")
    {
        std::vector<Entry> entries;
        for (const auto& in : inputs)
        {
            auto more = entriesFromJson (juce::JSON::parse (cwd.getChildFile (in)));
            entries.insert (entries.end(), more.begin(), more.end());
        }
        std::cout << "targets from " << entries.size() << " fingerprints\n";
        doc = buildTargets (entries, k);
    }
    else
        return usage();
    if (! cwd.getChildFile (out).replaceWithText (juce::JSON::toString (doc)))
    {
        std::cerr << "cannot write " << out << "\n";
        return 1;
    }
    return 0;
}

} // namespace rolla::fptool
