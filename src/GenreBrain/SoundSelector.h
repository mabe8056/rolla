#pragma once

// The genre brain's sound selector (Job G2, stream A): one roll's kit, drawn lane
// by lane from the library by how well each file fits the style's targets.
//
//   score   timbre distance to the lane's regions, plus how far the features miss
//           the bands, plus a pitch-confidence shortfall on pitched lanes, minus a
//           small manifest-tier bonus.
//   weight  exp(-(score - best) / T): the best file in the pool weighs 1.
//   pack    after the anchor (the first lane drawn, the kick when it has a pool):
//           same pack x3 (drum lane on a drum anchor x6, so the drums lean to one
//           pack), else same family x1.5. A tendency: other packs keep their weight.
//   draw    seeded weighted random among the top k (max(8, 15% of the pool)), not
//           a fixed best pick.
//   mesh    stream B's pairPenalty: a draw that clashes with the lanes already in
//           the kit is re-drawn from the rest of the top k, a bounded number of
//           times; then the least-bad draw stays.
//
// Melak's rule: distance only weights the draw. Nothing is filtered out on fit,
// nothing falls back to a uniform draw or a stand-in sound, and an empty pool is
// no pick (-1). The only file a lane skips is the one it played in the last kit,
// and only when the pool has another. No JUCE.

#include "GenreTargets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

namespace rolla::genre
{

// splitmix64: tiny, seedable and the same on every platform (std distributions
// differ between standard libraries, so they would break per-seed determinism).
struct SelectorRng
{
    std::uint64_t state = 0;

    explicit SelectorRng (std::uint64_t seed) noexcept : state (seed) {}

    std::uint64_t next() noexcept
    {
        std::uint64_t z = (state += 0x9e3779b97f4a7c15ull);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    }

    // [0, 1)
    double uniform() noexcept { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
};

// One roll's choice: an index into each lane's pool, -1 for no pick.
struct Kit
{
    std::array<int, kLanes> pick {};

    Kit() noexcept { pick.fill (-1); }

    int at (Lane lane) const noexcept
    {
        const int i = (int) lane;
        return i >= 0 && i < kLanes ? pick[(size_t) i] : -1;
    }

    bool operator== (const Kit& other) const noexcept { return pick == other.pick; }
    bool operator!= (const Kit& other) const noexcept { return pick != other.pick; }
};

struct SelectorOptions
{
    // Stream B's meshing rules: how badly a candidate clashes with a sample already
    // in the kit (0 meshes). Empty: no penalty, one draw per lane.
    std::function<float (const SampleFeatures&, const SampleFeatures&)> pairPenalty;
    float maxPenalty = 1.f; // summed over the lanes already chosen; over it, re-draw
    int maxTries = 4;       // draws per lane before the least-bad one stays

    // 0.35 and an 8% shortlist (Sound Designer, Relay 376): at 1.0 the best file got
    // only 4-8% of the weight and every style drew the same files.
    float temperature = 0.35f; // score units per e-fold of weight
    int topMin = 6;
    float topShare = 0.08f;
    float monoWeight = 3.f;  // bass/kick L/R correlation under 150 Hz below 0.997
    float bassBandWeight = 2.f; // bass under 91% below 200 Hz / over 0.5% above 1 kHz
    float packBoost = 3.f;
    float drumPackBoost = 6.f;
    float familyBoost = 1.5f;
    float timbreWeight = 1.f;
    float bandWeight = 1.f;
    float pitchWeight = 2.f; // a pitched file with no confidence costs this much
    float tierWeight = 0.25f;
};

inline bool isDrumLane (Lane lane) noexcept
{
    return (int) lane <= (int) Lane::Ride;
}

// Lower fits better. The pool's lane wins over the file's own (a file flagged for
// two lanes, PLUCK/STAB, is judged as the lane it is drawn for).
inline float fitScore (const GenreTargets& targets, Style style, Lane lane, const SampleFeatures& f,
                       const SelectorOptions& opt = {})
{
    SampleFeatures relabelled;
    const SampleFeatures* s = &f;
    if (f.lane != lane)
    {
        relabelled = f;
        relabelled.lane = lane;
        s = &relabelled;
    }
    float score = opt.bandWeight * targets.bandMiss (style, *s);
    // A vector that does not match the regions reads as 1e9 away; cap it so the
    // weights stay finite and such a file is merely unlikely.
    score += opt.timbreWeight * std::min (targets.timbreDistance (style, *s), 50.f);
    // A pitched file the sampler won't retune (conf under kPitchConfMin) plays at its
    // own pitch, maybe out of key: costs up to pitchWeight. The reference minimum
    // (often ~0.05) is too low to bite on its own, so kPitchConfMin is the floor.
    const float minConf = std::max (targets.lane (style, lane).minPitchConf, kPitchConfMin);
    if (pitchedLane (lane) && f.pitch.conf < minConf)
        score += opt.pitchWeight * (minConf - std::max (0.f, f.pitch.conf)) / minConf;
    // BASS (and the kick's low end) is summed to mono: a stereo source with low L/R
    // correlation under 150 Hz loses its low end. Weights only, never a filter.
    if ((lane == Lane::Bass || lane == Lane::Kick) && f.lowCorr < 0.997f)
        score += opt.monoWeight * std::clamp ((0.997f - f.lowCorr) / 0.10f, 0.f, 1.f);
    // Bass energy where a house bass lives: >= 91% under 200 Hz, little over 1 kHz.
    if (lane == Lane::Bass && f.lt200 >= 0.f)
        score += opt.bassBandWeight * std::clamp ((0.91f - f.lt200) / 0.05f, 0.f, 1.f);
    if (lane == Lane::Bass && f.gt1k >= 0.f)
        score += opt.bassBandWeight * std::clamp ((f.gt1k - 0.005f) / 0.01f, 0.f, 1.f);
    score -= opt.tierWeight * (float) std::max (0, f.tier);
    return std::isfinite (score) ? score : 1.0e3f;
}

namespace selector
{

struct Candidate
{
    int index = 0;
    double weight = 0.0;
};

// One weighted draw from the candidates, re-drawn while it clashes with the kit.
inline int drawLane (const std::vector<SampleFeatures>& pool, std::vector<Candidate> cands, const Kit& kit,
                     const std::array<std::vector<SampleFeatures>, kLanes>& pools, SelectorRng& rng,
                     const SelectorOptions& opt)
{
    const bool mesh = static_cast<bool> (opt.pairPenalty);
    const int tries = mesh ? std::max (1, opt.maxTries) : 1;
    int leastBad = -1;
    float leastPenalty = std::numeric_limits<float>::infinity();
    for (int t = 0; t < tries && ! cands.empty(); ++t)
    {
        double total = 0.0;
        for (const auto& c : cands)
            total += c.weight;
        size_t at = 0;
        if (total > 0.0)
        {
            double r = rng.uniform() * total;
            for (; at + 1 < cands.size(); ++at)
            {
                r -= cands[at].weight;
                if (r < 0.0)
                    break;
            }
        }
        const int index = cands[at].index;
        if (! mesh)
            return index;
        float penalty = 0.f;
        for (int j = 0; j < kLanes; ++j)
        {
            const int other = kit.pick[(size_t) j];
            if (other >= 0 && other < (int) pools[(size_t) j].size())
                penalty += opt.pairPenalty (pool[(size_t) index], pools[(size_t) j][(size_t) other]);
        }
        if (! std::isfinite (penalty))
            penalty = std::numeric_limits<float>::max();
        if (penalty <= opt.maxPenalty)
            return index;
        if (penalty < leastPenalty)
        {
            leastPenalty = penalty;
            leastBad = index;
        }
        cands.erase (cands.begin() + (std::ptrdiff_t) at);
    }
    return leastBad;
}

} // namespace selector

// Lanes are drawn in Lane order (drums first, kick as the anchor). `last` is the
// previous kit over the same pools; each lane skips its last file when it has
// another, so a re-roll never repeats the kit unless every pool holds one file.
inline Kit drawKit (const std::array<std::vector<SampleFeatures>, kLanes>& pools, Style style, const GenreTargets& targets,
                    std::uint64_t seed, const Kit* last = nullptr, const SelectorOptions& opt = {})
{
    if (style >= Style::Count)
        style = Style::Roller; // dialects are 0..3; never index past the styles
    Kit kit;
    SelectorRng rng (seed);
    int anchorLane = -1;
    std::vector<float> scores;
    std::vector<selector::Candidate> cands;
    const double temperature = std::max (1.0e-3, (double) opt.temperature);
    for (int li = 0; li < kLanes; ++li)
    {
        const Lane lane = (Lane) li;
        const auto& pool = pools[(size_t) li];
        const int n = (int) pool.size();
        if (n == 0)
            continue;
        const int previous = last != nullptr ? last->pick[(size_t) li] : -1;
        const int skip = n > 1 && previous >= 0 && previous < n ? previous : -1;

        scores.assign ((size_t) n, 0.f);
        float best = std::numeric_limits<float>::max();
        for (int i = 0; i < n; ++i)
            if (i != skip && ! pool[(size_t) i].excluded)
            {
                scores[(size_t) i] = fitScore (targets, style, lane, pool[(size_t) i], opt);
                best = std::min (best, scores[(size_t) i]);
            }

        const SampleFeatures* anchor = anchorLane >= 0 ? &pools[(size_t) anchorLane][(size_t) kit.pick[(size_t) anchorLane]] : nullptr;
        const float samePack = isDrumLane (lane) && anchorLane >= 0 && isDrumLane ((Lane) anchorLane) ? opt.drumPackBoost : opt.packBoost;
        cands.clear();
        for (int i = 0; i < n; ++i)
        {
            // Excluded files (missing from the index, silent) are not candidates.
            if (i == skip || pool[(size_t) i].excluded)
                continue;
            double w = std::exp (-(double) (scores[(size_t) i] - best) / temperature);
            if (anchor != nullptr)
            {
                const auto& f = pool[(size_t) i];
                if (! f.pack.empty() && f.pack == anchor->pack)
                    w *= samePack;
                else if (! f.family.empty() && f.family == anchor->family)
                    w *= opt.familyBoost;
            }
            // A seeded hair of jitter breaks exact ties (a lane with no timbre regions
            // scores every file alike), so the shortlist isn't the alphabetically first files.
            w *= 1.0 + 1.0e-6 * rng.uniform();
            cands.push_back ({ i, w });
        }
        // Heaviest first; the jitter is seeded, so the top k is still reproducible.
        std::stable_sort (cands.begin(), cands.end(),
                          [] (const selector::Candidate& a, const selector::Candidate& b) { return a.weight > b.weight; });
        const int k = std::max (std::max (1, opt.topMin), (int) std::ceil (opt.topShare * (float) n));
        if ((int) cands.size() > k)
            cands.resize ((size_t) k);

        const int chosen = selector::drawLane (pool, cands, kit, pools, rng, opt);
        kit.pick[(size_t) li] = chosen;
        if (anchorLane < 0 && chosen >= 0)
            anchorLane = li;
    }
    return kit;
}

} // namespace rolla::genre
