#pragma once

// Kit meshing rules (genre brain stream B): do the sounds the selector drew sit
// together? The selector (stream A) draws one sample per lane and asks a pair
// hook for a penalty; on a high one it re-picks. This file is that hook's brain.
//
//   pairPenalty (a, b, ctx)   one pair, dispatched by lanes. 0 = fine.
//   keyPenalty (s, ctx)       one pitched sample against the roll's key + scale,
//                             after the retune the sampler would apply.
//   kitConflicts (kit, ctx)   every pair and key conflict in a drawn kit.
//   pairPenaltyHook (ctx)     the std::function the selector takes.
//
// Penalties are in "conflict units": each check adds weight x severity, where
// severity runs 0..1, and a pair at or over MeshRules::conflictAt (1.0 by
// default) is a conflict the selector should re-pick. Every tolerance lives in
// MeshRules (defaults from the mix spec, overridable by data/genre/mesh_rules.json
// through KitMeshJson.h), never at a call site.
//
// What the features mean here (SampleFeatures, Fingerprint.h): subRatio is the
// energy share under 120 Hz of the raw file, before the lane chain; centroidHz
// and decay20Ms are of the raw file too; pitch is PitchProbe's reading (on a
// kick, the tail's f0). No JUCE.

#include "GenreTargets.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>

namespace rolla::genre
{

// Scale masks: bit i set = pitch class (key root + i) is in the scale.
inline constexpr std::uint16_t scaleMaskOf (std::initializer_list<int> degrees) noexcept
{
    std::uint16_t m = 0;
    for (int d : degrees)
        m = (std::uint16_t) (m | (1u << (((d % 12) + 12) % 12)));
    return m;
}
inline constexpr std::uint16_t kScaleMinor = scaleMaskOf ({ 0, 2, 3, 5, 7, 8, 10 }); // natural minor (Aeolian)
inline constexpr std::uint16_t kScaleDorian = scaleMaskOf ({ 0, 2, 3, 5, 7, 9, 10 });
inline constexpr std::uint16_t kScalePhrygian = scaleMaskOf ({ 0, 1, 3, 5, 7, 8, 10 });
inline constexpr std::uint16_t kScaleMajor = scaleMaskOf ({ 0, 2, 4, 5, 7, 9, 11 });
inline constexpr std::uint16_t kScaleMixolydian = scaleMaskOf ({ 0, 2, 4, 5, 7, 9, 10 });

enum class MeshReason : std::uint8_t
{
    None = 0,
    SubOverlap,       // kick and bass both heavy under 120 Hz
    FundamentalClash, // kick f0 a semitone or two off the bass note: beating in the sub
    KickTailOverlap,  // kick still ringing (over -20 dB) when the bass note is up
    LowMidMasking,    // a tonal lane with sub weight or a dark centroid over the bass
    RegisterOverlap,  // a tonal lane's root inside the bass register
    RegisterCrowding, // stab / pluck / pad roots or centroids on top of each other
    OutOfKey,         // pitch classes outside the scale after the sampler's retune
    TopBandClash,     // two bright hat / ride / perc sounds in the same top band
    Count
};

inline const char* meshReasonName (MeshReason r) noexcept
{
    static const char* const names[] = { "none", "sub_overlap", "fundamental_clash", "kick_tail_overlap",
                                         "low_mid_masking", "register_overlap", "register_crowding",
                                         "out_of_key", "top_band_clash" };
    const int i = (int) r;
    return i >= 0 && i < (int) MeshReason::Count ? names[i] : "";
}

// Every tolerance. fromMixSpec() gives the defaults and says where each comes from
// (MIX_SPEC = docs/mix/MIX_SPEC.md, targets = tests/mix/targets.json, mock =
// GenreTargets::mock() bands). Weights and ramps that the spec does not measure
// are marked [N] (neutral choice).
struct MeshRules
{
    float conflictAt = 1.f; // a pair penalty at or over this is a conflict (re-pick)

    // Kick vs bass
    float subOwnMin = 0.5f;      // subRatio at which a sound "owns" the sub
    float subHeavy = 0.75f;      // both over this: they fight for the same energy
    float subWeight = 0.5f;
    float fundUnisonSt = 0.35f;  // pitch-class distance that counts as the same note
    float fundClashSt = 2.f;     // up to here (from unison) the two fundamentals beat
    float fundClearSt = 3.f;     // from here on no clash; linear between clash and clear
    float fundWeight = 1.f;
    float fundProxyWeight = 0.5f; // the kick f0 is a centroid estimate, not measured
    float kickPitchConfMin = 0.3f; // a kick tail reading at least this sure counts as f0
    float kickF0MinHz = 40.f;    // plausible kick f0 range (measured or proxy)
    float kickF0MaxHz = 80.f;
    float kickF0PerCentroid = 0.66f; // f0 ~ centroid x this, when the tail has no pitch
    float defaultBpm = 126.f;    // the bass offbeat gap is an 8th at this tempo
    float tailRampMs = 100.f;    // overlap at which the tail check reaches full severity
    float tailWeight = 1.f;

    // Bass vs tonal lanes (stab, pluck, hits, pad)
    float tonalSubMax = 0.15f;   // raw subRatio a tonal file may carry before it muddies the bass
    float tonalSubRampSpan = 0.15f; // over the max by this much = full severity
    float stabCentroidMinHz = 389.f; // stab / pluck / hits darker than this mask the low mids
    float maskWeight = 1.f;
    float registerFullSt = 7.f;  // tonal root within this above the bass root: full overlap
    float registerClearSt = 12.f; // an octave or more above: clear
    float registerCentroidFullOct = 1.f;  // centroid fallback when either root is unknown
    float registerCentroidClearOct = 2.f;
    float registerWeight = 1.f;

    // Stab vs pad vs pluck
    float crowdFullSt = 3.f;     // roots this close collide
    float crowdClearSt = 7.f;    // a fifth or more apart: clear
    float crowdCentroidFullOct = 0.25f;
    float crowdCentroidClearOct = 0.5f;
    float crowdWeight = 1.f;

    // Key agreement
    float keyToneWeight = 1.f;   // per out-of-scale tone (one wrong tone at full weight = a conflict)
    float keyMinConf = 0.2f;     // an unretuned file with a weaker reading is not judged

    // Top band (hats, ride, perc)
    float topBandMinHz = 6473.f; // both centroids at least this bright
    float topBandFullOct = 0.1f; // centroids this close collide
    float topBandClearOct = 0.33f;
    float topBandWeight = 1.f;

    static MeshRules fromMixSpec() noexcept
    {
        MeshRules r;
        // Kick vs bass. MIX_SPEC 2.1.4 / 2.2: kick and bass own everything under
        // 120 Hz between them (both mono, corr <120 >= 0.99; every other lane's <120
        // energy is capped at -15 dB or less). So sub weight on both is normal; it is
        // only a clash when both are extreme or their fundamentals beat.
        //  subOwnMin 0.5: mock KICK sub band 0.35..1, BASS 0.3..1; the A&R bass
        //    target is about 50 % below 60 Hz (MIX_SPEC 3 BASS EQ 2).
        //  subHeavy 0.75: just over the 72.6 % below 60 Hz the spec calls too much on
        //    the bass render and shelves down (MIX_SPEC 3 BASS EQ 2).
        r.subOwnMin = 0.5f;
        r.subHeavy = 0.75f;
        //  Kick f0 50.1..55.9 Hz (IQR), 47.3 Hz min (MIX_SPEC 3 KICK HPF, 4.2); kick
        //  centroid 80 Hz (75..86) (MIX_SPEC 3 KICK EQ 1). 53 / 80 = 0.66 for the proxy;
        //  40..80 Hz brackets the measured f0 with room for tuned kicks.
        r.kickF0MinHz = 40.f;
        r.kickF0MaxHz = 80.f;
        r.kickF0PerCentroid = 0.66f;
        //  "Within a couple of semitones but not equal": 35 cents is unison [N];
        //  1 to 2 st apart beats at 3..7 Hz in the sub at A1, the audible wobble.
        r.fundUnisonSt = 0.35f;
        r.fundClashSt = 2.f;
        r.fundClearSt = 3.f;
        //  Tail: kick decay to -20 dB 166 ms (85..261) (MIX_SPEC 3 KICK comp release);
        //  the bass sits on the offbeat 8th, 238 ms at 126 BPM (MIX_SPEC cites 126 BPM
        //  for the reese release). A 261 ms kick against a fast bass already overlaps.
        r.defaultBpm = 126.f;
        r.tailRampMs = 100.f;

        // Bass vs tonal lanes.
        //  tonalSubMax 0.15: mock STAB / PAD sub band 0..0.15. After the stab chain's
        //    120 Hz HPF the spec wants <120 Hz <= -15 dB re total (MIX_SPEC 2.2 STAB,
        //    HITS; targets STAB lt120_max_db -15); a raw file well over 0.15 keeps body
        //    there that the HPF only thins, and the rest masks the bass harmonics.
        r.tonalSubMax = 0.15f;
        r.tonalSubRampSpan = 0.15f;
        //  stabCentroidMinHz 389: targets STAB centroid [389, 1325]; MIX_SPEC 3 STAB
        //    EQ 1 centroid 674 Hz (432..1204).
        r.stabCentroidMinHz = 389.f;
        //  Register: bass root around D1 36.7 Hz, centroid 89 Hz (77..102) (MIX_SPEC 3
        //    BASS); stab centroid 674 Hz is about 3 octaves up. A tonal root within a
        //    fifth of the bass root is in its register; an octave up is clear [N].
        //    Centroid fallback: bass 89 Hz x 2 oct = 356 Hz, just under the stab floor.
        r.registerFullSt = 7.f;
        r.registerClearSt = 12.f;
        r.registerCentroidFullOct = 1.f;
        r.registerCentroidClearOct = 2.f;

        // Stab vs pad vs pluck: PLUCK shares the stab's "other" budget (MIX_SPEC 2.2:
        // if both play, each -3 dB), and the pad sits 6 dB under the stab (-32.7 vs
        // -26.8) behind a 150 Hz HPF (MIX_SPEC 3 PAD). Roots within a minor third
        // collide; a fifth apart is clear. The sampler can retune each by up to 7 st
        // (PitchProbe.h ShiftRule cap) without changing its register, so register is
        // judged from the file root [N].
        r.crowdFullSt = 3.f;
        r.crowdClearSt = 7.f;
        r.crowdCentroidFullOct = 0.25f;
        r.crowdCentroidClearOct = 0.5f;

        // Key: one out-of-scale tone at the tonic is a conflict [N]. keyMinConf 0.2:
        // under kPitchConfMin (0.5) a file plays as recorded (tonalShift), so a strong
        // but low-confidence reading at a wrong pitch class is a real risk; under 0.2
        // the reading is noise.
        r.keyToneWeight = 1.f;
        r.keyMinConf = 0.2f;

        // Top band: CH centroid 8726 Hz (8091..9459), OH/CH targets [6473, 10878]
        // (MIX_SPEC 3 DRUMS, targets DRUMS:OH/CH). Two sounds both in that band and
        // within a third of an octave sit on each other [N].
        r.topBandMinHz = 6473.f;
        r.topBandFullOct = 0.1f;
        r.topBandClearOct = 0.33f;
        return r;
    }
};

// What the roll is doing that the rules need.
struct MeshContext
{
    int keyRootPc = -1;                   // 0..11, C = 0. -1: key unknown, key rules off
    std::uint16_t scaleMask = kScaleMinor;
    // Where the pitched lanes' notes land, relative to the key root (a lane's
    // pcWeights from GenreTargets fit here). Default: the tonic only. Out-of-scale
    // degrees are ignored (the roll plays in the scale).
    std::array<float, 12> degreeWeights { 1.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f };
    // The tonal lanes play written triads (Engine chordAskOf): the sampler moves a
    // chord file whose quality clashes with the asked one (chordFileOffset). False:
    // one-finger lines, where a chord file sounds its own quality on every note.
    bool chordAsk = false;
    float bpm = 0.f; // <= 0: rules.defaultBpm
    MeshRules rules = MeshRules::fromMixSpec();
};

struct MeshResult
{
    float penalty = 0.f;
    MeshReason reason = MeshReason::None; // the largest contributor

    void add (float p, MeshReason r) noexcept
    {
        if (! (p > 0.f))
            return;
        if (p > best)
        {
            best = p;
            reason = r;
        }
        penalty += p;
    }

private:
    float best = 0.f;
};

struct MeshConflict
{
    int a = -1, b = -1; // kit indices; b = -1 for a single-sample (key) conflict
    float penalty = 0.f;
    MeshReason reason = MeshReason::None;
};

namespace mesh
{

// 1 at x <= full, 0 at x >= clear, linear between.
inline float ramp (float x, float full, float clear) noexcept
{
    if (std::isnan (x))
        return 0.f;
    if (x <= full)
        return 1.f;
    if (x >= clear || clear <= full)
        return 0.f;
    return (clear - x) / (clear - full);
}

inline float clamp01 (float x) noexcept
{
    return std::isnan (x) ? 0.f : std::clamp (x, 0.f, 1.f);
}

inline float midiOfHz (float hz) noexcept
{
    return 69.f + 12.f * std::log2 (hz / 440.f);
}

inline float hzOfMidi (float midi) noexcept
{
    return 440.f * std::pow (2.f, (midi - 69.f) / 12.f);
}

inline float pcDistance (float a, float b) noexcept
{
    float d = std::fmod (std::fabs (a - b), 12.f);
    return d > 6.f ? 12.f - d : d;
}

inline int pcOf (float midi) noexcept
{
    const int r = (int) std::lround (midi);
    return ((r % 12) + 12) % 12;
}

inline bool inMask (std::uint16_t mask, int pc) noexcept
{
    return (mask & (1u << (((pc % 12) + 12) % 12))) != 0;
}

inline int popcount12 (std::uint16_t m) noexcept
{
    int n = 0;
    for (int i = 0; i < 12; ++i)
        n += (m >> i) & 1;
    return n;
}

inline bool tonalLane (Lane l) noexcept
{
    return l == Lane::Stab || l == Lane::Pluck || l == Lane::Hits || l == Lane::Pad;
}

inline bool topLane (Lane l) noexcept
{
    return l == Lane::HatC || l == Lane::HatO || l == Lane::Ride || l == Lane::Perc;
}

// The sampler retunes this file to the played note (Sampler.cpp + tonalShift):
// pitched, confident, not vox. A low-confidence file whose filename note agrees
// would be retuned too, but SampleFeatures has no filename note, so it is judged
// as recorded (the cautious side).
inline bool retuned (const SampleFeatures& s) noexcept
{
    return s.lane != Lane::Vox && s.pitch.pitched() && s.pitch.conf >= kPitchConfMin;
}

// The kick's fundamental as MIDI, and whether it was measured (vs a centroid proxy).
// -1 when neither gives a plausible kick f0.
inline float kickFundamental (const SampleFeatures& kick, const MeshRules& r, bool& measured) noexcept
{
    measured = false;
    const float lo = midiOfHz (r.kickF0MinHz), hi = midiOfHz (r.kickF0MaxHz);
    if (kick.pitch.pitched() && kick.pitch.conf >= r.kickPitchConfMin)
    {
        // The probe can lock onto the body's 2nd harmonic: fold an octave down into range.
        float m = kick.pitch.rootPitch;
        while (m > hi && m - 12.f >= lo - 0.5f)
            m -= 12.f;
        if (m >= lo - 0.5f && m <= hi + 0.5f)
        {
            measured = true;
            return m;
        }
    }
    if (kick.centroidHz > 0.f)
    {
        const float m = midiOfHz (kick.centroidHz * r.kickF0PerCentroid);
        if (m >= lo && m <= hi)
            return m;
    }
    return -1.f;
}

// Triad quality the scale builds on a degree (for chordFileOffset).
inline ChordQuality scaleTriad (std::uint16_t scale, int degree) noexcept
{
    const bool m3 = inMask (scale, degree + 3), M3 = inMask (scale, degree + 4);
    if (M3 && ! m3)
        return ChordQuality::Major;
    if (m3 && ! M3)
        return ChordQuality::Minor;
    return ChordQuality::Neutral;
}

// Out-of-scale tones of chordPcs (bit i = root + i) with the root on pitch class
// rootPc (relative to the key root).
inline int outOfScaleTones (std::uint16_t chordPcs, int rootPc, std::uint16_t scale) noexcept
{
    const std::uint16_t pcs = chordPcs != 0 ? chordPcs : 1;
    int out = 0;
    for (int i = 0; i < 12; ++i)
        if ((pcs >> i) & 1)
            out += inMask (scale, rootPc + i) ? 0 : 1;
    return out;
}

} // namespace mesh

// ---- Key agreement (one sample) -------------------------------------------------

inline MeshResult keyPenalty (const SampleFeatures& s, const MeshContext& ctx) noexcept
{
    using namespace mesh;
    MeshResult res;
    const auto& r = ctx.rules;
    if (ctx.keyRootPc < 0 || ! (pitchedLane (s.lane) || s.lane == Lane::Vox) || ! s.pitch.pitched())
        return res;
    const auto& p = s.pitch;
    const std::uint16_t pcs = p.chordPcs != 0 ? p.chordPcs : 1;

    if (retuned (s))
    {
        // The root lands on the played note, so a single note is always in key; only
        // a chord file's other tones can fall out. Bass never takes chordFileOffset
        // (Sampler.cpp: chordFile excludes Bass and Vox).
        if (! p.isChord || popcount12 (pcs) < 2)
            return res;
        float wSum = 0.f, outSum = 0.f;
        for (int d = 0; d < 12; ++d)
        {
            const float w = ctx.degreeWeights[(size_t) d];
            if (! (w > 0.f) || ! inMask (ctx.scaleMask, d))
                continue;
            int offset = 0;
            if (ctx.chordAsk && s.lane != Lane::Bass)
                offset = chordFileOffset (p.quality, scaleTriad (ctx.scaleMask, d));
            outSum += w * (float) outOfScaleTones (pcs, d + offset, ctx.scaleMask);
            wSum += w;
        }
        if (wSum > 0.f)
            res.add (r.keyToneWeight * outSum / wSum, MeshReason::OutOfKey);
        return res;
    }

    // Played as recorded (vox, or a reading under kPitchConfMin): its own pitch
    // classes must sit in the scale. A weak reading counts less, down to keyMinConf.
    if (p.conf < r.keyMinConf)
        return res;
    const float sure = s.lane == Lane::Vox ? 1.f : clamp01 (p.conf / kPitchConfMin);
    const int rootPc = ((pcOf (p.rootPitch) - ctx.keyRootPc) % 12 + 12) % 12;
    res.add (r.keyToneWeight * sure * (float) outOfScaleTones (pcs, rootPc, ctx.scaleMask), MeshReason::OutOfKey);
    return res;
}

// ---- Pair rules -----------------------------------------------------------------

inline MeshResult kickBassPenalty (const SampleFeatures& kick, const SampleFeatures& bass, const MeshContext& ctx) noexcept
{
    using namespace mesh;
    MeshResult res;
    const auto& r = ctx.rules;

    // Both owning the sub: normal up to a point (they share it, MIX_SPEC 2.1.4),
    // a fight when both are extreme.
    const float lowShared = std::min (kick.subRatio, bass.subRatio);
    if (lowShared > r.subHeavy)
        res.add (r.subWeight * clamp01 ((lowShared - r.subHeavy) / std::max (1.e-3f, 1.f - r.subHeavy)),
                 MeshReason::SubOverlap);
    // How much the low end is shared scales the two checks below: a thin kick does
    // not beat against the bass, whatever its pitch.
    const float shared = clamp01 (lowShared / std::max (1.e-3f, r.subOwnMin));

    // Fundamentals: the kick is not retuned (drums), the bass is (to the line's notes,
    // mostly the key root), so the clash is kick f0 vs where the bass notes land.
    bool measured = false;
    const float kf = kickFundamental (kick, r, measured);
    if (kf >= 0.f)
    {
        auto clash = [&] (float d) { return d <= r.fundUnisonSt ? 0.f : ramp (d, r.fundClashSt, r.fundClearSt); };
        float sev = 0.f;
        if (ctx.keyRootPc >= 0 && retuned (bass))
        {
            float wSum = 0.f;
            for (int d = 0; d < 12; ++d)
            {
                const float w = ctx.degreeWeights[(size_t) d];
                if (! (w > 0.f) || ! inMask (ctx.scaleMask, d))
                    continue;
                sev += w * clash (pcDistance (kf, (float) (ctx.keyRootPc + d)));
                wSum += w;
            }
            sev = wSum > 0.f ? sev / wSum : 0.f;
        }
        else if (bass.pitch.pitched())
        {
            sev = clash (pcDistance (kf, bass.pitch.rootPitch)); // plays as recorded
        }
        res.add (r.fundWeight * sev * shared * (measured ? 1.f : r.fundProxyWeight), MeshReason::FundamentalClash);
    }

    // Kick tail over the bass note: the bass offbeat starts an 8th after the kick and
    // is up after its attack; a kick still over -20 dB then stacks two subs.
    if (kick.decay20Ms > 0.f)
    {
        const float bpm = ctx.bpm > 0.f ? ctx.bpm : r.defaultBpm;
        const float gapMs = 30000.f / std::max (40.f, bpm);
        const float overlap = kick.decay20Ms - (gapMs + std::max (0.f, bass.attackMs));
        if (overlap > 0.f)
            res.add (r.tailWeight * clamp01 (overlap / std::max (1.f, r.tailRampMs)) * shared,
                     MeshReason::KickTailOverlap);
    }
    return res;
}

// bass vs stab / pluck / hits / pad: low-mid masking and register overlap.
inline MeshResult bassTonalPenalty (const SampleFeatures& bass, const SampleFeatures& tonal, const MeshContext& ctx) noexcept
{
    using namespace mesh;
    MeshResult res;
    const auto& r = ctx.rules;

    float mask = clamp01 ((tonal.subRatio - r.tonalSubMax) / std::max (1.e-3f, r.tonalSubRampSpan));
    if (tonal.lane != Lane::Pad && tonal.centroidHz > 0.f && tonal.centroidHz < r.stabCentroidMinHz)
        mask = std::max (mask, clamp01 (std::log2 (r.stabCentroidMinHz / tonal.centroidHz))); // a full octave dark = 1
    res.add (r.maskWeight * mask, MeshReason::LowMidMasking);

    // Register: the retune keeps each file's register (stab cap 7 st, bass 12), so
    // the file roots say where they will sound.
    float reg = 0.f;
    if (bass.pitch.pitched() && tonal.pitch.pitched())
        reg = ramp (tonal.pitch.rootPitch - bass.pitch.rootPitch, r.registerFullSt, r.registerClearSt);
    else if (bass.centroidHz > 0.f && tonal.centroidHz > 0.f)
        reg = ramp (std::log2 (tonal.centroidHz / bass.centroidHz), r.registerCentroidFullOct, r.registerCentroidClearOct);
    res.add (r.registerWeight * reg, MeshReason::RegisterOverlap);
    return res;
}

// stab vs pad, stab vs pluck, pluck vs pad: register spacing.
inline MeshResult crowdingPenalty (const SampleFeatures& a, const SampleFeatures& b, const MeshContext& ctx) noexcept
{
    using namespace mesh;
    MeshResult res;
    const auto& r = ctx.rules;
    float sev = 0.f;
    if (a.pitch.pitched() && b.pitch.pitched())
        sev = ramp (std::fabs (a.pitch.rootPitch - b.pitch.rootPitch), r.crowdFullSt, r.crowdClearSt);
    else if (a.centroidHz > 0.f && b.centroidHz > 0.f)
        sev = ramp (std::fabs (std::log2 (a.centroidHz / b.centroidHz)), r.crowdCentroidFullOct, r.crowdCentroidClearOct);
    res.add (r.crowdWeight * sev, MeshReason::RegisterCrowding);
    return res;
}

// Two bright top sounds in one band. A closed and an open hat are a pair by design
// (often one kit, one choke group), so that pair is left alone.
inline MeshResult topBandPenalty (const SampleFeatures& a, const SampleFeatures& b, const MeshContext& ctx) noexcept
{
    using namespace mesh;
    MeshResult res;
    const auto& r = ctx.rules;
    const bool hatPair = (a.lane == Lane::HatC && b.lane == Lane::HatO) || (a.lane == Lane::HatO && b.lane == Lane::HatC);
    if (hatPair || a.centroidHz < r.topBandMinHz || b.centroidHz < r.topBandMinHz)
        return res;
    res.add (r.topBandWeight * ramp (std::fabs (std::log2 (a.centroidHz / b.centroidHz)), r.topBandFullOct, r.topBandClearOct),
             MeshReason::TopBandClash);
    return res;
}

// One pair, either order. Key agreement is per sample: see keyPenalty and the hook.
inline MeshResult pairPenalty (const SampleFeatures& a, const SampleFeatures& b, const MeshContext& ctx) noexcept
{
    using namespace mesh;
    if (a.lane == Lane::Kick && b.lane == Lane::Bass)
        return kickBassPenalty (a, b, ctx);
    if (b.lane == Lane::Kick && a.lane == Lane::Bass)
        return kickBassPenalty (b, a, ctx);
    if (a.lane == Lane::Bass && tonalLane (b.lane))
        return bassTonalPenalty (a, b, ctx);
    if (b.lane == Lane::Bass && tonalLane (a.lane))
        return bassTonalPenalty (b, a, ctx);
    const auto crowdLane = [] (Lane l) { return l == Lane::Stab || l == Lane::Pluck || l == Lane::Pad; };
    if (crowdLane (a.lane) && crowdLane (b.lane) && a.lane != b.lane)
        return crowdingPenalty (a, b, ctx);
    if (topLane (a.lane) && topLane (b.lane) && a.lane != b.lane)
        return topBandPenalty (a, b, ctx);
    return {};
}

// Every conflict (penalty >= rules.conflictAt) in a drawn kit, pairs first then key.
inline std::vector<MeshConflict> kitConflicts (const std::vector<SampleFeatures>& kit, const MeshContext& ctx)
{
    std::vector<MeshConflict> out;
    const float at = ctx.rules.conflictAt;
    for (size_t i = 0; i < kit.size(); ++i)
        for (size_t j = i + 1; j < kit.size(); ++j)
        {
            const auto res = pairPenalty (kit[i], kit[j], ctx);
            if (res.penalty >= at)
                out.push_back ({ (int) i, (int) j, res.penalty, res.reason });
        }
    for (size_t i = 0; i < kit.size(); ++i)
    {
        const auto res = keyPenalty (kit[i], ctx);
        if (res.penalty >= at)
            out.push_back ({ (int) i, -1, res.penalty, res.reason });
    }
    return out;
}

using PairPenaltyFn = std::function<float (const SampleFeatures&, const SampleFeatures&)>;

// The selector's hook. The selector only asks about pairs, so by default each
// member's key penalty is added too: an out-of-key file then scores high against
// whatever it is paired with, and the selector (which re-picks the candidate it
// just drew) drops it. ctx is captured by value.
inline PairPenaltyFn pairPenaltyHook (MeshContext ctx, bool withKey = true)
{
    return [ctx, withKey] (const SampleFeatures& a, const SampleFeatures& b)
    {
        float p = pairPenalty (a, b, ctx).penalty;
        if (withKey)
            p += keyPenalty (a, ctx).penalty + keyPenalty (b, ctx).penalty;
        return p;
    };
}

} // namespace rolla::genre
