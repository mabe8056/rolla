#pragma once

// Loads genre_targets.json (data/genre/genre_targets.schema.json) into GenreTargets.
// Real data is laid over GenreTargets::mock(): every field the file has wins, a
// field or lane it leaves out keeps the neutral value. So partial data from the
// box works, and an absent file means the mock.
//
// Where the file lives: $ROLLA_GENRE_DATA_DIR/genre_targets.json, else the plugin
// bundle's Contents/Resources/Genre/genre_targets.json (injected at packaging, like
// the sample pack). Never in the repo.

#include "GenreTargets.h"

#include <juce_core/juce_core.h>

namespace rolla::genre
{

namespace json
{

inline std::vector<float> floats (const juce::var& v)
{
    std::vector<float> out;
    if (const auto* a = v.getArray())
        for (const auto& x : *a)
            out.push_back (x.isVoid() ? std::nanf ("") : (float) (double) x); // null = NaN (truncated decay40)
    return out;
}

inline void band (const juce::var& v, Band& b)
{
    const auto f = floats (v);
    if (f.size() == 2 && ! std::isnan (f[0]) && ! std::isnan (f[1]))
    {
        b.lo = std::min (f[0], f[1]);
        b.hi = std::max (f[0], f[1]);
        b.set = true;
    }
}

inline void number (const juce::var& v, float& out)
{
    if (v.isDouble() || v.isInt() || v.isInt64())
        out = (float) (double) v;
}

inline void lane (const juce::var& v, LaneTargets& lt)
{
    lt.present = v.hasProperty ("present") ? (bool) v["present"] : true;
    if (const auto* clusters = v["clusters"].getArray())
    {
        lt.timbre.clusters.clear();
        for (const auto& c : *clusters)
        {
            TargetCluster tc;
            tc.centroid = floats (c["centroid"]);
            tc.spread = floats (c["spread"]);
            if (tc.spread.size() != tc.centroid.size())
                tc.spread.assign (tc.centroid.size(), 1.f);
            tc.weight = c.hasProperty ("weight") ? (float) (double) c["weight"] : 1.f;
            if (! tc.centroid.empty())
                lt.timbre.clusters.push_back (std::move (tc));
        }
    }
    number (v["threshold"], lt.timbre.threshold);
    lt.pooled = (bool) v["pooled"];
    if (v.hasProperty ("count"))
        lt.count = (int) v["count"];
    const auto& bands = v["bands"];
    band (bands["centroid_hz"], lt.centroidHz);
    band (bands["tilt_db_oct"], lt.tiltDbPerOct);
    band (bands["attack_ms"], lt.attackMs);
    band (bands["decay_ms"], lt.decayMs);
    band (bands["sub_ratio"], lt.subRatio);
    const auto& pitch = v["pitch"];
    number (pitch["min_conf"], lt.minPitchConf);
    const auto pcs = floats (pitch["pc_weights"]);
    if (pcs.size() == 12)
        for (size_t i = 0; i < 12; ++i)
            lt.pcWeights[i] = std::isnan (pcs[i]) ? 0.f : pcs[i];
    number (v["loudness_db"], lt.loudnessDb);
}

// The box's extension block (schema rolla.genre_targets.ext/1): per-style feature
// percentiles over reference hits, in its own lane grouping. p10..p90 becomes the
// band. Applied before the per-lane v1 fields, so an explicit v1 "bands" wins.
inline std::vector<Lane> extLanes (const juce::String& name)
{
    const auto n = name.toLowerCase();
    if (n == "kick")
        return { Lane::Kick };
    if (n == "hat" || n == "hats")
        return { Lane::HatC, Lane::HatO };
    if (n == "clap_perc")
        return { Lane::Clap, Lane::Snare, Lane::Perc };
    if (n == "perc_hats")
        return { Lane::HatC, Lane::HatO, Lane::Clap, Lane::Snare, Lane::Perc, Lane::Ride };
    if (n == "bass")
        return { Lane::Bass };
    if (n == "stab_chord" || n == "stab")
        return { Lane::Stab, Lane::Pluck, Lane::Hits };
    if (n == "pad")
        return { Lane::Pad, Lane::Fx };
    if (n == "vox")
        return { Lane::Vox };
    return {};
}

// {p10, p90} -> band. Percent features are left in their own units.
inline void percentileBand (const juce::var& v, Band& b)
{
    if (! v.isObject() || ! v.hasProperty ("p10") || ! v.hasProperty ("p90"))
        return;
    const auto lo = (float) (double) v["p10"], hi = (float) (double) v["p90"];
    if (std::isnan (lo) || std::isnan (hi))
        return;
    b.lo = std::min (lo, hi);
    b.hi = std::max (lo, hi);
    b.set = true;
}

inline void extLaneFeatures (const juce::var& laneObj, LaneTargets& lt)
{
    const auto& f = laneObj.hasProperty ("features") ? laneObj["features"] : laneObj;
    percentileBand (f["centroid_hz"], lt.centroidHz);
    percentileBand (f["tilt_db_per_oct"], lt.tiltDbPerOct);
    percentileBand (f["attack_ms"], lt.attackMs);
    percentileBand (f["decay20_ms"], lt.decayMs);
    const auto& conf = f["pitch_conf"];
    if (conf.isObject() && conf.hasProperty ("p10"))
        lt.minPitchConf = std::clamp ((float) (double) conf["p10"], 0.f, 1.f);
}

inline void ext (const juce::var& e, GenreTargets& g)
{
    if (! e.isObject())
        return;
    // ALL first, then each style on top.
    auto applyBands = [&g] (const juce::var& byLane, int onlyStyle)
    {
        if (auto* lanes = byLane.getDynamicObject())
            for (const auto& l : lanes->getProperties())
                for (Lane lane : extLanes (l.name.toString()))
                    for (int s = 0; s < kStyles; ++s)
                        if (onlyStyle < 0 || onlyStyle == s)
                            extLaneFeatures (l.value, g.styles[(size_t) s].lanes[(size_t) lane]);
    };
    if (auto* bands = e["bands"].getDynamicObject())
    {
        for (const auto& s : bands->getProperties())
            if (s.name.toString().equalsIgnoreCase ("ALL"))
                applyBands (s.value, -1);
        for (const auto& s : bands->getProperties())
        {
            const Style style = styleFromId (s.name.toString().toStdString());
            if (style != Style::Count)
                applyBands (s.value, (int) style);
        }
    }
    if (auto* groove = e["groove"].getDynamicObject())
        for (const auto& s : groove->getProperties())
        {
            const Style style = styleFromId (s.name.toString().toStdString());
            if (style == Style::Count)
                continue;
            if (auto* metrics = s.value.getDynamicObject())
                for (const auto& m : metrics->getProperties())
                    if (m.name.toString().containsIgnoreCase ("swing"))
                    {
                        Band b;
                        percentileBand (m.value, b);
                        if (b.set && b.hi > 1.f) // percent (53.7) -> ratio (0.537)
                        {
                            b.lo /= 100.f;
                            b.hi /= 100.f;
                        }
                        if (b.set)
                            g.styles[(size_t) style].groove.swing = b;
                    }
        }
}

} // namespace json

inline GenreTargets parseGenreTargets (const juce::var& doc)
{
    GenreTargets g = GenreTargets::mock();
    if (! doc.isObject())
        return g;
    g.fromData = true;
    if (doc.hasProperty ("version"))
        g.version = (int) doc["version"];
    const auto& norm = doc["norm"];
    g.norm.mean = json::floats (norm["mean"]);
    g.norm.std = json::floats (norm["std"]);
    json::ext (doc["ext"], g);
    if (auto* styles = doc["styles"].getDynamicObject())
        for (const auto& s : styles->getProperties())
        {
            const Style style = styleFromId (s.name.toString().toStdString());
            if (style == Style::Count)
                continue;
            auto& st = g.styles[(size_t) style];
            if (auto* lanes = s.value.getDynamicObject())
                for (const auto& l : lanes->getProperties())
                {
                    const Lane lane = laneFromName (l.name.toString().toStdString());
                    if (lane != Lane::Count)
                        json::lane (l.value, st.lanes[(size_t) lane]);
                }
            const auto& groove = s.value["groove"];
            json::band (groove["swing"], st.groove.swing);
            json::number (groove["ghost_prob"], st.groove.ghostProb);
            json::number (groove["hat_density"], st.groove.hatDensity);
            json::number (groove["perc_density"], st.groove.percDensity);
        }
    return g;
}

inline juce::File genreDataFile()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("ROLLA_GENRE_DATA_DIR", {});
    if (env.isNotEmpty())
        return juce::File (env).getChildFile ("genre_targets.json");
    // <bundle>/Contents/MacOS/<binary> -> <bundle>/Contents/Resources/Genre
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    return exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/Genre/genre_targets.json");
}

// The mock when there is no file or it does not parse.
inline GenreTargets loadGenreTargets (const juce::File& file = genreDataFile())
{
    if (! file.existsAsFile())
        return GenreTargets::mock();
    return parseGenreTargets (juce::JSON::parse (file));
}

} // namespace rolla::genre
