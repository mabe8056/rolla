#pragma once

// Loads mesh_rules.json (repo copy: data/genre/mesh_rules.json) over
// MeshRules::fromMixSpec(): every number the file has wins, anything it leaves out
// keeps the mix-spec default, and no file (or one that does not parse) means the
// defaults. So tolerances are tuned as data, never at a call site.
//
// Where the file lives: $ROLLA_GENRE_DATA_DIR/mesh_rules.json, else the plugin
// bundle's Contents/Resources/Genre/mesh_rules.json (next to genre_targets.json).

#include "KitMesh.h"

#include <juce_core/juce_core.h>

namespace rolla::genre
{

namespace meshjson
{

inline void number (const juce::var& group, const char* key, float& out)
{
    const auto& v = group[key];
    if (v.isDouble() || v.isInt() || v.isInt64())
        out = (float) (double) v;
}

// [lo, hi] pairs (ramps, ranges): both numbers or neither.
inline void pair (const juce::var& group, const char* key, float& lo, float& hi)
{
    if (const auto* a = group[key].getArray())
        if (a->size() == 2 && ! (*a)[0].isVoid() && ! (*a)[1].isVoid())
        {
            lo = (float) (double) (*a)[0];
            hi = (float) (double) (*a)[1];
        }
}

} // namespace meshjson

inline MeshRules parseMeshRules (const juce::var& doc)
{
    using namespace meshjson;
    MeshRules r = MeshRules::fromMixSpec();
    if (! doc.isObject())
        return r;
    number (doc, "conflict_at", r.conflictAt);

    const auto& kb = doc["kick_bass"];
    number (kb, "sub_own_min", r.subOwnMin);
    number (kb, "sub_heavy", r.subHeavy);
    number (kb, "sub_weight", r.subWeight);
    number (kb, "fund_unison_st", r.fundUnisonSt);
    pair (kb, "fund_clash_clear_st", r.fundClashSt, r.fundClearSt);
    number (kb, "fund_weight", r.fundWeight);
    number (kb, "fund_proxy_weight", r.fundProxyWeight);
    number (kb, "kick_pitch_conf_min", r.kickPitchConfMin);
    pair (kb, "kick_f0_hz", r.kickF0MinHz, r.kickF0MaxHz);
    number (kb, "kick_f0_per_centroid", r.kickF0PerCentroid);
    number (kb, "default_bpm", r.defaultBpm);
    number (kb, "tail_ramp_ms", r.tailRampMs);
    number (kb, "tail_weight", r.tailWeight);

    const auto& bt = doc["bass_tonal"];
    number (bt, "tonal_sub_max", r.tonalSubMax);
    number (bt, "tonal_sub_ramp", r.tonalSubRampSpan);
    number (bt, "stab_centroid_min_hz", r.stabCentroidMinHz);
    number (bt, "mask_weight", r.maskWeight);
    pair (bt, "register_full_clear_st", r.registerFullSt, r.registerClearSt);
    pair (bt, "register_centroid_full_clear_oct", r.registerCentroidFullOct, r.registerCentroidClearOct);
    number (bt, "register_weight", r.registerWeight);

    const auto& cr = doc["crowding"];
    pair (cr, "full_clear_st", r.crowdFullSt, r.crowdClearSt);
    pair (cr, "centroid_full_clear_oct", r.crowdCentroidFullOct, r.crowdCentroidClearOct);
    number (cr, "weight", r.crowdWeight);

    const auto& key = doc["key"];
    number (key, "tone_weight", r.keyToneWeight);
    number (key, "min_conf", r.keyMinConf);

    const auto& top = doc["top_band"];
    number (top, "min_hz", r.topBandMinHz);
    pair (top, "full_clear_oct", r.topBandFullOct, r.topBandClearOct);
    number (top, "weight", r.topBandWeight);
    return r;
}

inline juce::File meshRulesFile()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("ROLLA_GENRE_DATA_DIR", {});
    if (env.isNotEmpty())
        return juce::File (env).getChildFile ("mesh_rules.json");
    // <bundle>/Contents/MacOS/<binary> -> <bundle>/Contents/Resources/Genre
    const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    return exe.getParentDirectory().getParentDirectory().getChildFile ("Resources/Genre/mesh_rules.json");
}

// The mix-spec defaults when there is no file or it does not parse.
inline MeshRules loadMeshRules (const juce::File& file = meshRulesFile())
{
    if (! file.existsAsFile())
        return MeshRules::fromMixSpec();
    return parseMeshRules (juce::JSON::parse (file));
}

} // namespace rolla::genre
