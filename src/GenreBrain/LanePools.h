#pragma once

// The seam between the sound selector and today's pick path (Job G2, stream A).
//
// A lane draws its file as sampleIndexAt (index, part, filter, pick % n). So when
// a lane's whole pool has features, featurePool() lists them in exactly that
// order, and the selector's choice j (0 <= j < n) goes back as the lane's pick:
// pick % n == j selects the same file. A pool with any file missing from the
// feature index returns empty and the caller keeps today's seed % pool draw.

#include "SampleFeaturesJson.h"
#include "SoundSelector.h"
#include "../Engine/SampleLibrary.h"

namespace rolla::genre
{

// ROLLA part (and drum voice for Drums) to the genre lane. Rim and toms sit in PERC,
// as poolFilterFor and the fingerprint folders do. Lane::Count when none fits.
inline Lane laneForPart (int part, int voice) noexcept
{
    switch (part)
    {
        case rolla::Bass: return Lane::Bass;
        case rolla::Stab: return Lane::Stab;
        case rolla::Hits: return Lane::Hits;
        case rolla::Pluck: return Lane::Pluck;
        case rolla::Pad: return Lane::Pad;
        case rolla::Vox: return Lane::Vox;
        case rolla::Drums: break;
        default: return Lane::Count;
    }
    switch (voice)
    {
        case rolla::Kick: return Lane::Kick;
        case rolla::Snare: return Lane::Snare;
        case rolla::Clap: return Lane::Clap;
        case rolla::HatC: return Lane::HatC;
        case rolla::HatO: return Lane::HatO;
        case rolla::Ride: return Lane::Ride;
        case rolla::Rim:
        case rolla::TomLow:
        case rolla::TomMid:
        case rolla::Perc: return Lane::Perc;
        default: return Lane::Count;
    }
}

// The pool sampleIndexAt walks (strict list, or the role-lane fallback when the
// strict list is empty), as features relabelled to `lane`. One pass over the
// index. Files the feature index lacks come back `excluded`; a pool where none
// has features is empty (the caller keeps today's draw).
inline std::vector<SampleFeatures> featurePool (const SampleIndex& index, int part, const PoolFilter& filter,
                                                const FeatureIndex& features, Lane lane)
{
    std::vector<SampleFeatures> out;
    if (features.size() == 0 || lane == Lane::Count)
        return out;
    const int poolLane = filter.lane >= 0 ? filter.lane : part;
    int strict = 0;
    for (const auto& entry : index.entries)
        if (sampleEntryMatches (entry, part, filter))
            ++strict;
    const bool fallback = strict == 0 && roleBit (poolLane) != 0;
    for (const auto& entry : index.entries)
    {
        const bool keep = fallback ? sampleEntryFallback (entry, part, filter)
                                   : sampleEntryMatches (entry, part, filter);
        if (! keep)
            continue;
        // A file the index lacks (e.g. digitally silent, skipped by the box) keeps its
        // place, so ordinals line up, but is never drawn.
        const auto* f = features.find (std::string (entry.path));
        if (f == nullptr)
        {
            SampleFeatures missing;
            missing.id = entry.path;
            missing.excluded = true;
            out.push_back (missing);
        }
        else
            out.push_back (*f);
        out.back().lane = lane;
    }
    if (std::all_of (out.begin(), out.end(), [] (const SampleFeatures& s) { return s.excluded; }))
        out.clear();
    return out;
}

} // namespace rolla::genre
