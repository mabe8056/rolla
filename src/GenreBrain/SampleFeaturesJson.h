#pragma once

// Loads sample_features.json (data/genre/sample_features.schema.json): the box's
// measurements of every library file, keyed by library-relative path, into
// per-lane pools for the sound selector. Each entry is fp.json's "f" block plus
// the lane and an optional pack, family and tier. Numbers and ids only.
//
// Where it lives: beside genre_targets.json ($ROLLA_GENRE_DATA_DIR, else the
// bundle's Contents/Resources/Genre). Never in the repo. No file means an empty
// index, and every lane keeps today's draw.

#include "GenreTargetsJson.h"

#include <unordered_map>
#include <utility>

namespace rolla::genre
{

struct FeatureIndex
{
    bool fromData = false; // a file was read and parsed
    std::array<std::vector<SampleFeatures>, kLanes> pools {};
    std::unordered_map<std::string, std::pair<int, int>> byId; // id -> (lane, index in that pool)

    const SampleFeatures* find (const std::string& id) const
    {
        const auto it = byId.find (id);
        if (it == byId.end())
            return nullptr;
        return &pools[(size_t) it->second.first][(size_t) it->second.second];
    }

    size_t size() const noexcept { return byId.size(); }
};

namespace json
{

inline float numberOr (const juce::var& v, float fallback)
{
    return v.isDouble() || v.isInt() || v.isInt64() ? (float) (double) v : fallback;
}

// fp.json's "f" block (FingerprintTool.h toJson), pitch included. False without MFCCs.
inline bool fingerprint (const juce::var& f, Fingerprint& fp)
{
    const auto mean = floats (f["mfcc_mean"]);
    const auto spread = floats (f["mfcc_std"]);
    if (mean.size() != (size_t) kMfcc || spread.size() != (size_t) kMfcc)
        return false;
    for (int i = 0; i < kMfcc; ++i)
    {
        fp.mfccMean[i] = mean[(size_t) i];
        fp.mfccStd[i] = spread[(size_t) i];
    }
    fp.centroidHz = numberOr (f["centroid_hz"], 0.f);
    fp.rolloffHz = numberOr (f["rolloff85_hz"], 0.f);
    fp.flatness = numberOr (f["flatness"], 0.f);
    fp.attackMs = numberOr (f["attack_ms"], 0.f);
    fp.decay20Ms = numberOr (f["decay20_ms"], 0.f);
    fp.decay40Ms = numberOr (f["decay40_ms"], 0.f);
    // A null decay40 is an unknown one: treat it as truncated so distances skip it.
    fp.truncated = (bool) f["truncated"] || f["decay40_ms"].isVoid();
    fp.subRatio = numberOr (f["sub_ratio"], 0.f);
    fp.rmsDb = numberOr (f["rms_db"], -120.f);
    fp.tiltDbPerOct = numberOr (f["tilt_db_oct"], 0.f);
    fp.pitch.rootPitch = numberOr (f["root_pitch"], -1.f);
    fp.pitch.conf = numberOr (f["root_conf"], 0.f);
    fp.pitch.isChord = (bool) f["is_chord"];
    const auto quality = f["quality"].toString();
    fp.pitch.quality = quality == "maj" ? ChordQuality::Major
                     : quality == "min" ? ChordQuality::Minor
                     : quality == "neutral" ? ChordQuality::Neutral
                                            : ChordQuality::None;
    fp.valid = true;
    return true;
}

inline bool sampleEntry (const juce::String& id, const juce::var& e, SampleFeatures& out)
{
    const Lane lane = laneFromName (e["lane"].toString().toUpperCase().toStdString());
    Fingerprint fp;
    if (id.isEmpty() || lane == Lane::Count || ! fingerprint (e["f"], fp))
        return false;
    out = SampleFeatures::from (fp, id.replaceCharacter ('\\', '/').toStdString(), lane, numberOr (e["length_ms"], 0.f));
    out.pack = e["pack"].toString().toStdString();
    out.family = e["family"].toString().toStdString();
    if (e.hasProperty ("tier"))
        out.tier = (int) e["tier"];
    return true;
}

} // namespace json

// "files" is an object keyed by library-relative path. An fp.json-style array
// (entries with "path" or "id") is read too, so `rolla-fingerprint scan` output
// loads as is. Entries without a known lane or MFCCs are skipped; a repeated id
// keeps its first entry. Pool order is file order, so draws are reproducible.
inline FeatureIndex parseSampleFeatures (const juce::var& doc)
{
    FeatureIndex index;
    if (! doc.isObject())
        return index;
    index.fromData = true;
    auto add = [&index] (const juce::String& id, const juce::var& e)
    {
        SampleFeatures s;
        if (! json::sampleEntry (id, e, s) || index.byId.count (s.id) > 0)
            return;
        auto& pool = index.pools[(size_t) s.lane];
        index.byId[s.id] = { (int) s.lane, (int) pool.size() };
        pool.push_back (std::move (s));
    };
    const auto& files = doc["files"];
    if (auto* obj = files.getDynamicObject())
    {
        for (const auto& p : obj->getProperties())
            add (p.name.toString(), p.value);
    }
    else if (const auto* arr = files.getArray())
    {
        for (const auto& e : *arr)
            add (e.hasProperty ("id") ? e["id"].toString() : e["path"].toString(), e);
    }
    return index;
}

inline juce::File sampleFeaturesFile()
{
    return genreDataFile().getSiblingFile ("sample_features.json");
}

// Empty (fromData false) when there is no file or it does not parse.
inline FeatureIndex loadSampleFeatures (const juce::File& file = sampleFeaturesFile())
{
    if (! file.existsAsFile())
        return {};
    return parseSampleFeatures (juce::JSON::parse (file));
}

} // namespace rolla::genre
