#pragma once

// rolla-fingerprint: the one feature extractor, run on both sides (Job G2).
//
//   rolla-fingerprint scan <dir> [--lane LANE] --out fp.json
//       Every wav/aif under dir. The lane comes from the folder (KICK, CH, OH, BASS,
//       STAB, VOX/chops, ...) unless --lane is given.
//   rolla-fingerprint stems <dir> [--styles track_style.csv] [--min-conf 0.5] --out fp.json
//       Demucs stems at <dir>/<track>/sep/{drums,bass,other,vocals}.wav (chunked
//       copies are skipped).
//   rolla-fingerprint scan-stems <track_dir> --track-id T [--style S] [--min-conf 0.5] --out fp.json
//       One track's stems (<track_dir>/sep/... or <track_dir>/...).
//   rolla-fingerprint targets <fp.json>... [--k 3] --out genre_targets.json
//       Per style and lane: normalised k-means regions and a hit threshold.
//
// Stem slices are labelled with a confidence (label_conf) beside the onset's
// (onset_conf); --min-conf drops slices under it on either:
//   drums  KICK (lows own the hit), HAT_C / HAT_O (bright, split at 150 ms to
//          -40 dB), RIDE (bright with a long metallic tail), CLAP vs SNARE (noisy
//          mids; a clap's tail is mostly over 1 kHz), PERC otherwise.
//   bass   BASS, except kick bleed: an onset within 15 ms of a kick whose energy
//          is mostly under 45 Hz.
//   other  PLUCK (gone to -20 dB in 120 ms), PAD (rings 400 ms with a stable
//          pitch), FX (rings 400 ms, unpitched), STAB otherwise.
//   vocals VOX.
// Stem output is keyed by track id only: no paths, file or artist names. The CSV
// (track or track_id, style[, id]) is private and never committed.
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
    juce::String path;   // scan: library-relative path. Stems: empty.
    juce::String lane, track, stem, style;
    double onsetSec = -1.0;
    float onsetConf = 1.f, labelConf = 1.f;
    Fingerprint fp;
};

inline const juce::StringArray& lanes()
{
    static const juce::StringArray all { "KICK", "CLAP", "SNARE", "HAT_C", "HAT_O", "PERC", "RIDE",
                                         "BASS", "STAB", "PLUCK", "PAD", "FX", "HITS", "VOX" };
    return all;
}

inline bool tonalLane (const juce::String& lane)
{
    return lane == "STAB" || lane == "PAD" || lane == "PLUCK" || lane == "HITS";
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

struct Slice
{
    int a = 0, b = 0;
    float conf = 1.f; // how clearly the onset stands out (rise 2.5x -> 0.5, 7.5x and up -> 1)
};

// Onsets: 5 ms RMS rising 2.5x over the last 100 ms, above -45 dB of the loudest
// hop, at least 60 ms apart. Slices run to the next onset, at most 1.5 s.
inline std::vector<Slice> sliceOnsets (const std::vector<float>& x)
{
    std::vector<Slice> out;
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
    std::vector<std::pair<int, float>> onsets;
    for (int h = 1; h < n; ++h)
    {
        double recent = 0.0;
        const int from = std::max (0, h - 20);
        for (int j = from; j < h; ++j)
            recent += rms[(size_t) j];
        recent /= std::max (1, h - from);
        const double rise = rms[(size_t) h] / (recent + 1.0e-6);
        if (rms[(size_t) h] > quiet && rise > 2.5 && (onsets.empty() || h - onsets.back().first >= 12))
            onsets.emplace_back (h, (float) std::clamp (0.5 + (rise - 2.5) / 10.0, 0.5, 1.0));
    }
    const int maxLen = (int) (1.5 * kFingerprintRate);
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const int a = std::max (0, onsets[i].first * hop - hop);
        const int next = i + 1 < onsets.size() ? onsets[i + 1].first * hop - hop : (int) x.size();
        const int b = std::min ({ next, a + maxLen, (int) x.size() });
        if (b - a >= (int) (0.04 * kFingerprintRate))
            out.push_back ({ a, b, onsets[i].second });
    }
    return out;
}

// Energy shares of x[from, to) (seconds from the slice start) split at the edges (Hz).
inline std::vector<double> bandShares (const float* x, int n, double from, double to, std::initializer_list<double> edges)
{
    const int N = 4096;
    std::vector<std::complex<double>> buf ((size_t) N);
    const int a = std::min (n, (int) (from * kFingerprintRate));
    const int m = std::min (n - a, std::min (N, (int) ((to - from) * kFingerprintRate)));
    for (int i = 0; i < N; ++i)
    {
        const double w = i < m ? 0.5 - 0.5 * std::cos (2.0 * pitchprobe::kPi * i / std::max (1, m - 1)) : 0.0;
        buf[(size_t) i] = { i < m ? (double) x[a + i] * w : 0.0, 0.0 };
    }
    pitchprobe::fft (buf);
    std::vector<double> share (edges.size() + 1, 0.0);
    double total = 0.0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * kFingerprintRate / N, p = std::norm (buf[(size_t) k]);
        size_t band = 0;
        for (double e : edges)
            if (f >= e)
                ++band;
        share[band] += p;
        total += p;
    }
    for (auto& s : share)
        s /= std::max (1.0e-12, total);
    return share;
}

// How far v sits past a threshold, as a 0.5..1 confidence (at the line: 0.5).
inline float margin (double v, double line, double span)
{
    return (float) std::clamp (0.5 + 0.5 * (v - line) / span, 0.5, 1.0);
}

inline juce::String drumLane (const float* x, int n, const Fingerprint& fp, float& conf)
{
    const auto b = bandShares (x, n, 0.0, 0.06, { 150.0, 5000.0 });
    const double low = b[0], mid = b[1], high = b[2];
    if (low >= 0.5)
    {
        conf = margin (low, 0.5, 0.4);
        return "KICK";
    }
    if (high >= 0.45 && low < 0.1)
    {
        conf = margin (high, 0.45, 0.4);
        if (fp.decay40Ms >= 400.f)
            return "RIDE";
        conf = std::min (conf, margin (std::fabs (fp.decay40Ms - 150.f), 0.0, 100.0));
        return fp.decay40Ms < 150.f ? "HAT_C" : "HAT_O";
    }
    if (low < 0.2 && mid + high >= 0.8 && fp.flatness > 0.3f)
    {
        // A clap's noise tail (30-150 ms) sits mostly over 1 kHz; a snare keeps its body.
        const auto tail = bandShares (x, n, 0.03, 0.15, { 1000.0 });
        conf = std::min (margin (mid + high, 0.8, 0.2), margin (std::fabs (tail[1] - 0.7), 0.0, 0.2));
        return tail[1] >= 0.7 ? "CLAP" : "SNARE";
    }
    conf = 0.5f;
    return "PERC";
}

inline juce::String otherLane (const Fingerprint& fp, float& conf)
{
    if (fp.decay20Ms < 120.f)
    {
        conf = margin (120.0 - fp.decay20Ms, 0.0, 60.0);
        return "PLUCK";
    }
    if (fp.decay20Ms >= 400.f || fp.truncated)
    {
        const bool stable = fp.pitch.pitched() && fp.pitch.conf >= kPitchConfMin;
        conf = std::min (margin (fp.decay20Ms, 400.0, 400.0), stable ? fp.pitch.conf : margin (kPitchConfMin - fp.pitch.conf, 0.0, 0.5));
        return stable ? "PAD" : "FX";
    }
    conf = std::min (margin (fp.decay20Ms - 120.0, 0.0, 100.0), margin (400.0 - fp.decay20Ms, 0.0, 100.0));
    return "STAB";
}

struct TrackInfo
{
    juce::String id, style;
};

// Columns by header name: track (or folder) is the stem folder, track_id (or id)
// the id written out, style the style. Without a header: folder,style.
inline std::map<juce::String, TrackInfo> readStyles (const juce::File& csv)
{
    std::map<juce::String, TrackInfo> out;
    juce::StringArray lines;
    csv.readLines (lines);
    int folderCol = 0, idCol = 0, styleCol = 1;
    bool header = false;
    for (const auto& line : lines)
    {
        auto cells = juce::StringArray::fromTokens (line, ",", "\"");
        for (auto& c : cells)
            c = c.trim().unquoted();
        if (cells.isEmpty() || cells[0].isEmpty())
            continue;
        if (! header && (cells.contains ("track", true) || cells.contains ("track_id", true)))
        {
            header = true;
            const int track = cells.indexOf ("track", true), folder = cells.indexOf ("folder", true);
            const int trackId = cells.indexOf ("track_id", true), id = cells.indexOf ("id", true);
            folderCol = folder >= 0 ? folder : (track >= 0 ? track : trackId);
            idCol = trackId >= 0 ? trackId : (id >= 0 ? id : folderCol);
            styleCol = std::max (0, cells.indexOf ("style", true));
            continue;
        }
        if (cells.size() <= std::max ({ folderCol, idCol, styleCol }))
            continue;
        out[cells[folderCol]] = { cells[idCol], cells[styleCol].toLowerCase() };
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
        e.fp = fingerprintOf (mono.data(), (int) mono.size(), kFingerprintRate,
                              tonalLane (e.lane) ? kPitchFloorTonalHz : kPitchFloorHz);
        if (e.fp.valid)
            out.push_back (std::move (e));
    }
    return out;
}

// One track: dir/sep/<stem>.wav, or dir/<stem>.wav.
inline std::vector<Entry> sliceTrack (const juce::File& dir, const TrackInfo& info, float minConf, int maxPerStem = 300)
{
    std::vector<Entry> out;
    const auto sep = dir.getChildFile ("sep").isDirectory() ? dir.getChildFile ("sep") : dir;
    std::vector<float> mono;
    std::vector<double> kickOnsets;
    for (const char* stemName : { "drums", "bass", "other", "vocals" })
    {
        const juce::String stem (stemName);
        const auto file = sep.getChildFile (stem + ".wav");
        if (! file.existsAsFile() || ! readMono (file, mono))
            continue;
        int kept = 0;
        for (const auto& s : sliceOnsets (mono))
        {
            if (kept >= maxPerStem)
                break;
            Entry e;
            e.onsetSec = s.a / kFingerprintRate;
            e.onsetConf = s.conf;
            const float* x = mono.data() + s.a;
            const int n = s.b - s.a;
            e.fp = fingerprintOf (x, n, kFingerprintRate, stem == "other" ? kPitchFloorTonalHz : kPitchFloorHz);
            if (! e.fp.valid)
                continue;
            if (stem == "drums")
            {
                e.lane = drumLane (x, n, e.fp, e.labelConf);
                if (e.lane == "KICK")
                    kickOnsets.push_back (e.onsetSec);
            }
            else if (stem == "bass")
            {
                bool nearKick = false;
                for (double k : kickOnsets)
                    nearKick = nearKick || std::fabs (k - e.onsetSec) <= 0.015;
                if (nearKick && bandShares (x, n, 0.0, 0.06, { 45.0 })[0] >= 0.5)
                    continue; // kick bleed in the bass stem
                e.lane = "BASS";
                e.labelConf = nearKick ? 0.6f : 1.f;
            }
            else if (stem == "other")
                e.lane = otherLane (e.fp, e.labelConf);
            else
                e.lane = "VOX";
            if (std::min (e.onsetConf, e.labelConf) < minConf)
                continue;
            e.track = info.id;
            e.stem = stem;
            e.style = info.style;
            out.push_back (std::move (e));
            ++kept;
        }
    }
    return out;
}

inline std::vector<Entry> sliceStems (const juce::File& dir, const std::map<juce::String, TrackInfo>& styles, float minConf)
{
    std::vector<Entry> out;
    auto tracks = dir.findChildFiles (juce::File::findDirectories, false);
    tracks.sort();
    for (const auto& track : tracks)
    {
        if (! track.getChildFile ("sep").isDirectory())
            continue;
        const auto found = styles.find (track.getFileName());
        const TrackInfo info = found != styles.end() ? found->second : TrackInfo { track.getFileName(), {} };
        auto more = sliceTrack (track, info, minConf);
        out.insert (out.end(), more.begin(), more.end());
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
        if (e.path.isNotEmpty())
            o->setProperty ("path", e.path);
        o->setProperty ("lane", e.lane);
        if (e.track.isNotEmpty())
        {
            o->setProperty ("track", e.track);
            o->setProperty ("stem", e.stem);
            o->setProperty ("onset_s", e.onsetSec);
            o->setProperty ("onset_conf", e.onsetConf);
            o->setProperty ("label_conf", e.labelConf);
        }
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
        f->setProperty ("truncated", e.fp.truncated);
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

// Every string in a stem fp.json is an id, a label or a style: nothing else may
// carry a name. Returns the first offending key, empty when clean.
inline juce::String nonIdString (const juce::var& v, const juce::String& key = {})
{
    static const juce::StringArray allowed { "extractor", "track", "stem", "lane", "style", "quality" };
    if (v.isString())
        return allowed.contains (key) ? juce::String() : (key.isEmpty() ? juce::String ("<root>") : key);
    if (const auto* arr = v.getArray())
        for (const auto& item : *arr)
            if (auto bad = nonIdString (item, key); bad.isNotEmpty())
                return bad;
    if (const auto* obj = v.getDynamicObject())
        for (const auto& p : obj->getProperties())
            if (auto bad = nonIdString (p.value, p.name.toString()); bad.isNotEmpty())
                return bad;
    return {};
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
    fp.truncated = (bool) f["truncated"];
    fp.subRatio = (float) (double) f["sub_ratio"];
    fp.rmsDb = (float) (double) f["rms_db"];
    fp.valid = true;
    return fp;
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
            e.stem = f["stem"].toString();
            e.style = f["style"].toString();
            e.fp = fingerprintFromJson (f["f"]);
            if (e.fp.valid && e.lane.isNotEmpty())
                out.push_back (std::move (e));
        }
    return out;
}

// NaN (a truncated sound's decay40) is written as null.
inline juce::Array<juce::var> floatsJson (const std::vector<float>& v)
{
    juce::Array<juce::var> a;
    for (float x : v)
        a.add (std::isnan (x) ? juce::var() : juce::var (x));
    return a;
}

// Lanes with under 8 hits in a style take the clusters fitted on every style's
// hits of that lane ("pooled": true).
inline juce::var buildTargets (const std::vector<Entry>& entries, int k)
{
    std::vector<std::vector<float>> all;
    for (const auto& e : entries)
        if (e.style.isNotEmpty() && e.fp.valid)
            all.push_back (e.fp.vector());
    const auto norm = FeatureNorm::of (all);
    std::map<juce::String, std::map<juce::String, std::vector<std::vector<float>>>> groups;
    std::map<juce::String, std::vector<std::vector<float>>> byLaneAll;
    for (const auto& e : entries)
        if (e.style.isNotEmpty() && e.fp.valid)
        {
            const auto v = norm.apply (e.fp.vector());
            groups[e.style][e.lane].push_back (v);
            byLaneAll[e.lane].push_back (v);
        }

    std::map<juce::String, LaneTarget> pooled;
    auto* styles = new juce::DynamicObject();
    for (const auto& [style, byLane] : groups)
    {
        auto* lanesObj = new juce::DynamicObject();
        for (const auto& [lane, xs] : byLane)
        {
            LaneTarget t;
            if ((int) xs.size() >= 8)
                t = fitTarget (xs, k);
            else
            {
                if (pooled.count (lane) == 0)
                    pooled[lane] = fitTarget (byLaneAll[lane], k);
                t = pooled[lane];
                t.pooled = true;
                t.count = (int) xs.size();
            }
            juce::Array<juce::var> clusters;
            for (const auto& c : t.clusters)
            {
                auto* co = new juce::DynamicObject();
                co->setProperty ("centroid", floatsJson (c.centroid));
                co->setProperty ("spread", floatsJson (c.spread));
                co->setProperty ("weight", c.weight);
                clusters.add (juce::var (co));
            }
            auto* lo = new juce::DynamicObject();
            lo->setProperty ("clusters", clusters);
            lo->setProperty ("threshold", t.threshold);
            lo->setProperty ("count", t.count);
            if (t.pooled)
                lo->setProperty ("pooled", true);
            lanesObj->setProperty (lane, juce::var (lo));
        }
        styles->setProperty (style, juce::var (lanesObj));
    }
    juce::Array<juce::var> names;
    for (int i = 0; i < kFingerprintDims; ++i)
        names.add (juce::String (fingerprintName (i)));
    auto* norms = new juce::DynamicObject();
    norms->setProperty ("mean", floatsJson (norm.mean));
    norms->setProperty ("std", floatsJson (norm.std));
    auto* root = new juce::DynamicObject();
    root->setProperty ("version", 1);
    root->setProperty ("extractor", "rolla-fingerprint/1");
    root->setProperty ("features", names);
    root->setProperty ("norm", juce::var (norms));
    root->setProperty ("styles", juce::var (styles));
    return juce::var (root);
}

inline int usage()
{
    std::cerr << "usage:\n"
                 "  rolla-fingerprint scan <dir> [--lane LANE] --out fp.json\n"
                 "  rolla-fingerprint stems <dir> [--styles track_style.csv] [--min-conf 0.5] --out fp.json\n"
                 "  rolla-fingerprint scan-stems <track_dir> --track-id T [--style S] [--min-conf 0.5] --out fp.json\n"
                 "  rolla-fingerprint targets <fp.json>... [--k 3] --out genre_targets.json\n";
    return 2;
}

// args exclude the program name.
inline int run (const juce::StringArray& args)
{
    if (args.size() < 2)
        return usage();
    const auto mode = args[0];
    juce::String out, lane, styles, trackId, style;
    int k = 3;
    float minConf = 0.f;
    juce::StringArray inputs;
    for (int i = 1; i < args.size(); ++i)
    {
        const bool more = i + 1 < args.size();
        if (args[i] == "--out" && more)
            out = args[++i];
        else if (args[i] == "--lane" && more)
            lane = args[++i].toUpperCase();
        else if (args[i] == "--styles" && more)
            styles = args[++i];
        else if (args[i] == "--track-id" && more)
            trackId = args[++i];
        else if (args[i] == "--style" && more)
            style = args[++i].toLowerCase();
        else if (args[i] == "--k" && more)
            k = juce::jlimit (1, 8, args[++i].getIntValue());
        else if (args[i] == "--min-conf" && more)
            minConf = juce::jlimit (0.f, 1.f, args[++i].getFloatValue());
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
    else if (mode == "stems" || mode == "scan-stems")
    {
        std::vector<Entry> entries;
        if (mode == "stems")
        {
            std::map<juce::String, TrackInfo> map;
            if (styles.isNotEmpty())
                map = readStyles (cwd.getChildFile (styles));
            entries = sliceStems (cwd.getChildFile (inputs[0]), map, minConf);
        }
        else
        {
            if (trackId.isEmpty())
                return usage();
            entries = sliceTrack (cwd.getChildFile (inputs[0]), { trackId, style }, minConf);
        }
        std::cout << "fingerprinted " << entries.size() << " slices\n";
        doc = toJson (entries);
        if (const auto bad = nonIdString (doc); bad.isNotEmpty())
        {
            std::cerr << "refusing to write: string field '" << bad << "' is not an id, label or style\n";
            return 1;
        }
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
