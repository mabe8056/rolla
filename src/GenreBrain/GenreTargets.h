#pragma once

// The genre brain's shared interface (Job G2+, Relay msg 332). Every stream codes
// against these types: the sound selector (A), meshing rules (B), adaptive
// treatment (C) and Key + Scale with the retune guard (D).
//
//   GenreTargets    what each style wants per lane: timbre regions (from
//                   rolla-fingerprint targets), feature bands, pitch, loudness
//                   slot, plus the style's groove stats.
//   SampleFeatures  what one library sample is: its fingerprint vector and the
//                   measured features the bands are checked against.
//
// Real targets come from the box as genre_targets.json (schema:
// data/genre/genre_targets.schema.json), injected into the bundle at
// Contents/Resources/Genre/ or pointed to by ROLLA_GENRE_DATA_DIR. They drop in as
// data with no code change. Until then, and in CI, GenreTargets::mock() gives
// plausible neutral values. No JUCE here; GenreTargetsJson.h loads the file.

#include "../Sampler/Fingerprint.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace rolla::genre
{

enum class Lane : std::uint8_t
{
    Kick, Clap, Snare, HatC, HatO, Perc, Ride, Bass, Stab, Pluck, Pad, Fx, Hits, Vox, Count
};
inline constexpr int kLanes = (int) Lane::Count;

// Names as in fp.json / genre_targets.json.
inline const char* laneName (Lane lane) noexcept
{
    static const char* const names[] = { "KICK", "CLAP", "SNARE", "HAT_C", "HAT_O", "PERC", "RIDE",
                                         "BASS", "STAB", "PLUCK", "PAD", "FX", "HITS", "VOX" };
    const int i = (int) lane;
    return i >= 0 && i < kLanes ? names[i] : "";
}

inline Lane laneFromName (const std::string& name) noexcept
{
    for (int i = 0; i < kLanes; ++i)
        if (name == laneName ((Lane) i))
            return (Lane) i;
    return Lane::Count;
}

inline bool pitchedLane (Lane lane) noexcept
{
    return lane == Lane::Bass || lane == Lane::Stab || lane == Lane::Pluck || lane == Lane::Pad || lane == Lane::Hits;
}

// Style ids as in Pattern.h: ROLLER = ukmin, DEEP = long, BOUNCY = pop, JACKIN = gaskin.
enum class Style : std::uint8_t
{
    Roller, Deep, Bouncy, Jackin, Count
};
inline constexpr int kStyles = (int) Style::Count;

inline const char* styleId (Style s) noexcept
{
    static const char* const ids[] = { "ukmin", "long", "pop", "gaskin" };
    const int i = (int) s;
    return i >= 0 && i < kStyles ? ids[i] : "";
}

// Accepts the internal ids (ukmin, long, pop, gaskin) and the style names the box
// writes (roller, deep, bouncy, jackin), in any case.
inline Style styleFromId (const std::string& id) noexcept
{
    std::string s;
    for (char c : id)
        s += (char) ((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
    static const char* const names[] = { "roller", "deep", "bouncy", "jackin" };
    for (int i = 0; i < kStyles; ++i)
        if (s == styleId ((Style) i) || s == names[i])
            return (Style) i;
    return Style::Count;
}

// A closed range. off() is 0 inside and grows with the distance outside,
// in units of the band's width (a band of zero width uses 1).
struct Band
{
    float lo = 0.f, hi = 0.f;
    bool set = false;

    bool contains (float v) const noexcept { return ! set || (v >= lo && v <= hi); }

    float off (float v) const noexcept
    {
        if (! set || std::isnan (v) || contains (v))
            return 0.f;
        const float w = hi > lo ? hi - lo : 1.f;
        return (v < lo ? lo - v : v - hi) / w;
    }
};

// One library sample, measured at index time (or supplied by the box's feature index).
struct SampleFeatures
{
    std::string id;     // library-relative path, or the box index id
    Lane lane = Lane::Count;
    std::string pack;   // source pack code (tendency: same pack x3)
    std::string family; // pack family (same family x1.5)
    int tier = 0;       // manifest tier, higher is better; 0 unknown
    // Missing from the feature index or unusable (e.g. digitally silent): kept in
    // the pool so pick ordinals still line up, but never drawn.
    bool excluded = false;

    std::vector<float> vector; // Fingerprint::vector(), raw (normalise with GenreTargets::norm)
    float centroidHz = 0.f;
    float tiltDbPerOct = 0.f;  // spectral slope, 100 Hz - 10 kHz
    float attackMs = 0.f;
    float decay20Ms = 0.f;
    float decay40Ms = 0.f;     // NaN when truncated
    bool truncated = false;
    float subRatio = 0.f;      // energy share under 120 Hz
    float loudnessDb = -120.f; // RMS over the first 300 ms, before load gain
    float lengthMs = 0.f;
    PitchInfo pitch;

    static SampleFeatures from (const Fingerprint& fp, const std::string& id, Lane lane, float lengthMs = 0.f)
    {
        SampleFeatures s;
        s.id = id;
        s.lane = lane;
        s.vector = fp.vector();
        s.centroidHz = fp.centroidHz;
        s.tiltDbPerOct = fp.tiltDbPerOct;
        s.attackMs = fp.attackMs;
        s.decay20Ms = fp.decay20Ms;
        s.decay40Ms = fp.truncated ? std::nanf ("") : fp.decay40Ms;
        s.truncated = fp.truncated;
        s.subRatio = fp.subRatio;
        s.loudnessDb = fp.rmsDb;
        s.lengthMs = lengthMs;
        s.pitch = fp.pitch;
        return s;
    }
};

// What a style wants from one lane.
struct LaneTargets
{
    bool present = false;   // the style uses this lane at all
    LaneTarget timbre;      // regions in normalised fingerprint space (empty: no timbre data)
    Band centroidHz, tiltDbPerOct, attackMs, decayMs, subRatio;
    // Pitch: the lowest confidence a pitched pick should have, and how often each
    // pitch class (relative to the roll's key root) shows up in the references.
    float minPitchConf = 0.f;
    std::array<float, 12> pcWeights {};
    float loudnessDb = 0.f; // the lane's slot relative to the kick (mix spec)
    bool pooled = false;    // regions borrowed from all styles (too few hits)
    int count = 0;          // reference hits behind these numbers (0 for mock)
};

// The style's groove tendencies (stream G3 reads these; the selector ignores them).
struct GrooveTargets
{
    Band swing;              // 0.50 straight .. 0.62 heavy, on 16ths
    float ghostProb = 0.f;   // chance an off-step hat or perc is a ghost
    float hatDensity = 0.f;  // hat hits per bar / 16
    float percDensity = 0.f; // perc hits per bar / 16
};

struct StyleTargets
{
    std::array<LaneTargets, kLanes> lanes {};
    GrooveTargets groove;

    const LaneTargets& lane (Lane l) const noexcept { return lanes[(size_t) l]; }
};

struct GenreTargets
{
    int version = 1;
    bool fromData = false; // true when loaded from genre_targets.json
    FeatureNorm norm;
    std::array<StyleTargets, kStyles> styles {};

    const StyleTargets& style (Style s) const noexcept { return styles[(size_t) s]; }
    const LaneTargets& lane (Style s, Lane l) const noexcept { return styles[(size_t) s].lane (l); }

    // Timbre distance of a sample to the style's lane regions (normalised space).
    // 0 when the lane has no timbre data, so the bands alone decide.
    float timbreDistance (Style s, const SampleFeatures& f) const
    {
        const auto& lt = lane (s, f.lane);
        if (lt.timbre.clusters.empty() || f.vector.empty())
            return 0.f;
        return targetDistance (norm.apply (f.vector), lt.timbre);
    }

    // How far the measured features sit outside the lane's bands (sum of off()).
    float bandMiss (Style s, const SampleFeatures& f) const noexcept
    {
        const auto& lt = lane (s, f.lane);
        return lt.centroidHz.off (f.centroidHz) + lt.tiltDbPerOct.off (f.tiltDbPerOct) + lt.attackMs.off (f.attackMs)
             + lt.decayMs.off (f.decay20Ms) + lt.subRatio.off (f.subRatio);
    }

    // Plausible neutral values from the mix spec and the genre notes; no reference
    // data. DEEP darker and longer, BOUNCY punchier and brighter, JACKIN raw and
    // swung, ROLLER tight. For CI and for builds without injected data.
    static GenreTargets mock()
    {
        GenreTargets g;
        auto band = [] (float lo, float hi) { Band b; b.lo = lo; b.hi = hi; b.set = true; return b; };
        // brightness scale per style: centroid multiplier and decay multiplier
        const float bright[kStyles] = { 1.0f, 0.75f, 1.25f, 1.1f };
        const float longer[kStyles] = { 1.0f, 1.5f, 0.8f, 1.1f };
        const float swingLo[kStyles] = { 0.51f, 0.52f, 0.50f, 0.54f };
        const float swingHi[kStyles] = { 0.55f, 0.57f, 0.53f, 0.60f };
        struct Base { Lane lane; float cLo, cHi, aHi, dLo, dHi, subLo, subHi, loud; };
        const Base base[] = {
            { Lane::Kick,  60.f,   400.f,  8.f,  80.f,  400.f, 0.35f, 1.0f,  0.f },
            { Lane::Clap,  900.f,  4000.f, 10.f, 60.f,  350.f, 0.0f,  0.1f, -6.f },
            { Lane::Snare, 600.f,  3500.f, 8.f,  60.f,  300.f, 0.0f,  0.15f, -6.f },
            { Lane::HatC,  4000.f, 12000.f, 4.f, 10.f,  120.f, 0.0f,  0.02f, -12.f },
            { Lane::HatO,  3500.f, 11000.f, 6.f, 120.f, 600.f, 0.0f,  0.02f, -14.f },
            { Lane::Perc,  400.f,  5000.f, 8.f,  30.f,  300.f, 0.0f,  0.2f, -12.f },
            { Lane::Ride,  3000.f, 10000.f, 6.f, 300.f, 1500.f, 0.0f, 0.02f, -16.f },
            { Lane::Bass,  60.f,   600.f,  20.f, 80.f,  1500.f, 0.3f, 1.0f, -2.f },
            { Lane::Stab,  400.f,  3000.f, 15.f, 60.f,  500.f, 0.0f,  0.15f, -8.f },
            { Lane::Pluck, 600.f,  4000.f, 10.f, 30.f,  250.f, 0.0f,  0.1f, -10.f },
            { Lane::Pad,   300.f,  2500.f, 200.f, 400.f, 1500.f, 0.0f, 0.15f, -14.f },
            { Lane::Fx,    500.f,  8000.f, 300.f, 200.f, 1500.f, 0.0f, 0.2f, -16.f },
            { Lane::Hits,  400.f,  4000.f, 20.f, 60.f,  800.f, 0.0f,  0.2f, -10.f },
            { Lane::Vox,   300.f,  3500.f, 30.f, 80.f,  1500.f, 0.0f, 0.1f, -9.f },
        };
        // Minor-key pitch-class tendency (root, fifth, minor third, seventh first).
        const std::array<float, 12> minorPcs { 1.f, 0.05f, 0.3f, 0.6f, 0.05f, 0.4f, 0.05f, 0.8f, 0.3f, 0.05f, 0.5f, 0.05f };
        for (int s = 0; s < kStyles; ++s)
        {
            auto& st = g.styles[(size_t) s];
            for (const auto& b : base)
            {
                auto& lt = st.lanes[(size_t) b.lane];
                lt.present = ! (s == (int) Style::Roller && b.lane == Lane::Ride);
                lt.centroidHz = band (b.cLo * bright[s], b.cHi * bright[s]);
                lt.tiltDbPerOct = band (-9.f + 2.f * (bright[s] - 1.f), -1.f + 2.f * (bright[s] - 1.f));
                lt.attackMs = band (0.f, b.aHi);
                lt.decayMs = band (b.dLo * longer[s], b.dHi * longer[s]);
                lt.subRatio = band (b.subLo, b.subHi);
                lt.minPitchConf = pitchedLane (b.lane) ? kPitchConfMin : 0.f;
                lt.pcWeights = minorPcs;
                lt.loudnessDb = b.loud;
            }
            st.groove.swing = band (swingLo[s], swingHi[s]);
            st.groove.ghostProb = s == (int) Style::Jackin ? 0.35f : 0.2f;
            st.groove.hatDensity = s == (int) Style::Bouncy ? 0.5f : 0.375f;
            st.groove.percDensity = s == (int) Style::Deep ? 0.15f : 0.25f;
        }
        return g;
    }
};

} // namespace rolla::genre
