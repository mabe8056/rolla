#pragma once

// Adaptive per-sample treatment (genre brain stream C). Given one drawn sample's
// measured features and the style's targets for its lane, decide how to play it
// so it fits: loop a short pad, tempo-fit a phrase loop, flag a thin sample for a
// layer, trim a tail that rings past the lane's decay band, soften an attack that
// is sharper than the lane allows, and pull the level toward the lane's slot.
//
// Melak's rule: no fixed per-sound settings. Every number below is derived from
// the sample's features against the lane's bands (or the roll's tempo and note
// length). The only constants are global policy: how far outside a band counts as
// "outside" (slack), and how far a gain move may go. Inside the bands the sample
// plays exactly as recorded: a default Treatment changes nothing.
//
// Pure C++, header-only, no JUCE. Sampler.h applies a Treatment to a voice when one
// is published through EngineParams::treatments (TreatmentSet); nothing else reads it.

#include "GenreTargets.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rolla::genre
{

// Which feature makes a sample thin against its lane, so a second sample could be
// layered under or over it. The pick of that second sample is the selector's job.
enum class LayerReason : std::uint8_t
{
    None,
    Sub,  // sub-ratio under the band: a kick or bass with no weight below 120 Hz
    Body, // centroid over the band: thin, all top, no body
    Top,  // centroid under the band: dull, needs a top layer (clap over snare)
    Tail  // decays faster than the band's shortest: a tail or room layer
};

inline const char* layerReasonName (LayerReason r) noexcept
{
    switch (r)
    {
        case LayerReason::Sub:  return "sub";
        case LayerReason::Body: return "body";
        case LayerReason::Top:  return "top";
        case LayerReason::Tail: return "tail";
        case LayerReason::None: break;
    }
    return "none";
}

struct Treatment
{
    // Loop: sustain a short pad or texture over a long note. Region in ms from the
    // file start; the crossfade sits at the loop end (Sampler's existing loop path).
    bool loop = false;
    float loopStartMs = 0.f, loopEndMs = 0.f, xfadeMs = 0.f;

    // Tempo fit. stretch is host bpm / file bpm for a tempo-tagged phrase loop, the
    // ratio Sampler's tempo-sync path already applies (pitch kept). 1 = none.
    // stretchMarked: the sample would need a tempo fit the sampler cannot do (a
    // phrase loop with no known tempo). It plays as recorded; the flag is for the
    // selector (prefer another pick) and for status.
    float stretch = 1.f;
    bool stretchMarked = false;

    // Layer: the sample is thin against the lane. Decision and reason only.
    bool wantsLayer = false;
    LayerReason reason = LayerReason::None;
    float layerMiss = 0.f; // how far outside the band, in band widths (Band::off)

    // Envelope. attackMs: a softer attack than the sample has (0 = as recorded).
    // decayTrimMs: the voice is silent by this time after the note starts (0 = no
    // trim). releaseMs: the raised-cosine fade that ends at decayTrimMs.
    float attackMs = 0.f;
    float decayTrimMs = 0.f;
    float releaseMs = 0.f;

    // Level move toward the lane's loudness slot, dB (0 = as loaded).
    float gainDb = 0.f;

    // True when applying this treatment changes how the voice sounds. wantsLayer and
    // stretchMarked are advice for the selector and do not touch the voice.
    bool changesVoice() const noexcept
    {
        return loop || attackMs > 0.f || decayTrimMs > 0.f || gainDb != 0.f;
    }
};

// Extra inputs the plain signature does not carry.
struct TreatmentOptions
{
    // Phrase loop tempo from the index (SampleEntry::fileBpm), 0 unknown.
    int fileBpm = 0;
    // The sample is a tempo-locked phrase loop (SampleEntry::phraseLoop).
    bool phraseLoop = false;
    // The level the lane's loudness slot is relative to: the roll's kick, measured the
    // same way (SampleFeatures::loudnessDb of the drawn kick). NaN: no level move.
    float referenceDb = std::numeric_limits<float>::quiet_NaN();
};

namespace treatment
{

// How far outside a band (in band widths, Band::off) a feature must sit before the
// sample is treated. Measurement noise and the edge of a percentile band are not a
// reason to touch a sound.
inline constexpr float kBandSlack = 0.1f;
// A layer is a bigger move than an envelope trim: flag it only when clearly thin.
inline constexpr float kLayerSlack = 0.25f;
// The sampler already ramps every voice in over 3 ms (Sampler::armAttack). A target
// attack at or under that is what every voice gets anyway.
inline constexpr float kClickGuardMs = 3.f;
// Level moves: inside +-kGainDeadbandDb of the slot counts as in the slot. Outside,
// move half the distance (the selector and the mix do the rest), never more than
// kGainMaxDb either way: a treatment fits a sample, it does not rebalance the mix.
inline constexpr float kGainDeadbandDb = 1.5f;
inline constexpr float kGainPull = 0.5f;
inline constexpr float kGainMaxDb = 6.f;

// Lanes whose job is to sustain under a held note: a short pad or texture there is
// looped. Drums, bass and one-shot stabs are never looped (a looped hit pulses).
inline bool sustainLane (Lane lane) noexcept
{
    return lane == Lane::Pad || lane == Lane::Fx;
}

// Note length in ms; 0 when unknown (no tempo, or a held note the engine releases itself).
inline float noteMs (double bpm, float noteLength16) noexcept
{
    if (bpm < 20.0 || ! (noteLength16 > 0.f) || noteLength16 > 1.0e6f)
        return 0.f;
    return noteLength16 * (float) (15000.0 / bpm); // one 16th = 60000 / bpm / 4 ms
}

} // namespace treatment

inline Treatment decideTreatment (const SampleFeatures& f, const LaneTargets& lt, double bpm, float noteLength16,
                                  const TreatmentOptions& opt)
{
    using namespace treatment;
    Treatment t;

    // Tempo fit first: it does not depend on the bands. Same rule and clamp as
    // phraseTempoStretch (SampleSpec.h), so the advice matches what the sampler does.
    if (opt.fileBpm >= 40 && bpm >= 20.0)
        t.stretch = std::clamp ((float) (bpm / (double) opt.fileBpm), 0.25f, 4.f);
    else if (opt.phraseLoop)
        t.stretchMarked = true;

    // A lane the style does not use has no targets to fit to; an unmeasured sample
    // (no fingerprint: centroid 0) has nothing to compare. Both play as recorded.
    if (! lt.present || ! (f.centroidHz > 0.f))
        return t;

    // A phrase loop is played by the tempo-sync path end to end; its envelope is the
    // recording. Only the level and the layer flag may change.
    const bool phrase = opt.phraseLoop || opt.fileBpm > 0;
    const float note = noteMs (bpm, noteLength16);
    const float len = f.lengthMs > 0.f ? f.lengthMs : 0.f;
    const bool hasDecay = f.decay20Ms > 0.f;

    // ---- Loop -----------------------------------------------------------------
    // A pad or texture that dies before the note ends and before the lane's shortest
    // decay would leave a hole under a held chord. Loop its sustained body:
    //   * the note outlasts the file (note > len), and
    //   * the file rings shorter than the band's low edge (it is "short" for this
    //     style, by more than the slack), and
    //   * it holds a body: it stays within 20 dB of its peak for at least half its
    //     length. A decaying pluck would pulse once per loop; that wants a layer.
    if (! phrase && sustainLane (f.lane) && len > 0.f && note > len && hasDecay && lt.decayMs.set)
    {
        const float body = std::min (f.decay20Ms, len);
        const bool shortForLane = body < lt.decayMs.lo && lt.decayMs.off (body) > kBandSlack;
        const bool holds = f.decay20Ms >= 0.5f * len;
        if (shortForLane && holds)
        {
            // End inside the loud part and clear of the file's own fade-out (its last
            // 5%). Start past the attack (the onset must not repeat) and at least 10%
            // in, so a soft swell is skipped too.
            const float end = std::min (len * 0.95f, f.decay20Ms + f.attackMs);
            const float start = std::max (f.attackMs, len * 0.1f);
            const float span = end - start;
            // Shorter than 20 ms the loop would be a buzz at 50 Hz and up, not a sustain.
            if (span >= 20.f)
            {
                t.loop = true;
                t.loopStartMs = start;
                t.loopEndMs = end;
                // A quarter of the span hides the seam on a noisy texture and leaves
                // three quarters of clean body. Never under 5 ms (a click).
                t.xfadeMs = std::max (5.f, 0.25f * span);
            }
        }
    }

    // ---- Decay trim -----------------------------------------------------------
    // A tail longer than the lane's band (an open hat ringing a bar, a stab with a
    // long reverb) smears the groove. Land it at the band's high edge: the longest
    // the style allows, so the trim is the smallest one that fits. The fade is half
    // the band's width (capped at half the target), so a narrow band (tight hats)
    // gets a short fade and a wide one (rides, pads) a long, natural one.
    if (! phrase && ! t.loop && hasDecay && lt.decayMs.set && f.decay20Ms > lt.decayMs.hi
        && lt.decayMs.off (f.decay20Ms) > kBandSlack && lt.decayMs.hi > 0.f)
    {
        const float target = lt.decayMs.hi;
        // decay20 runs from the peak: count the attack in, so the trim does not eat it.
        const float trimAt = target + std::max (0.f, f.attackMs);
        // Nothing to trim if the file ends there anyway.
        if (len <= 0.f || trimAt < len)
        {
            const float width = std::max (0.f, lt.decayMs.hi - lt.decayMs.lo);
            t.decayTrimMs = trimAt;
            t.releaseMs = std::max (5.f, std::min (0.5f * width, 0.5f * target));
        }
    }

    // ---- Attack softening -----------------------------------------------------
    // A sample sharper than the lane's slowest-onset edge (a pad with a click, a
    // vox with a hard consonant where the style swells) gets the band's low edge as
    // its attack: the gentlest change that lands it inside. Only when the target is
    // longer than the click guard every voice already has, and never longer than
    // half the note (the sound must still arrive).
    if (! phrase && lt.attackMs.set && f.attackMs < lt.attackMs.lo && lt.attackMs.lo > kClickGuardMs
        && lt.attackMs.off (f.attackMs) > kBandSlack)
    {
        float attack = lt.attackMs.lo;
        if (note > 0.f)
            attack = std::min (attack, 0.5f * note);
        if (t.decayTrimMs > 0.f)
            attack = std::min (attack, 0.5f * (t.decayTrimMs - t.releaseMs));
        if (attack > kClickGuardMs)
            t.attackMs = attack;
    }

    // ---- Layer ----------------------------------------------------------------
    // Thin against the lane: the feature furthest on the thin side of its band, if
    // it is clearly outside. Too much sub, too bright a "dark" lane etc. are not
    // thinness; those are selector misses, not layer calls.
    {
        float miss = 0.f;
        LayerReason why = LayerReason::None;
        auto consider = [&] (bool thinSide, float off, LayerReason r)
        {
            if (thinSide && off > miss)
            {
                miss = off;
                why = r;
            }
        };
        if (lt.subRatio.set)
            consider (f.subRatio < lt.subRatio.lo, lt.subRatio.off (f.subRatio), LayerReason::Sub);
        if (lt.centroidHz.set)
        {
            consider (f.centroidHz > lt.centroidHz.hi, lt.centroidHz.off (f.centroidHz), LayerReason::Body);
            consider (f.centroidHz < lt.centroidHz.lo, lt.centroidHz.off (f.centroidHz), LayerReason::Top);
        }
        // A short tail is a layer call only where the loop above did not fix it.
        if (lt.decayMs.set && hasDecay && ! t.loop && ! phrase)
            consider (f.decay20Ms < lt.decayMs.lo, lt.decayMs.off (f.decay20Ms), LayerReason::Tail);
        if (miss > kLayerSlack)
        {
            t.wantsLayer = true;
            t.reason = why;
            t.layerMiss = miss;
        }
    }

    // ---- Level ----------------------------------------------------------------
    // The slot is the lane's level relative to the kick. The sample's level relative
    // to the reference, against the slot, is the error; move part of the way.
    if (std::isfinite (opt.referenceDb) && std::isfinite (f.loudnessDb) && f.loudnessDb > -100.f)
    {
        const float err = lt.loudnessDb - (f.loudnessDb - opt.referenceDb);
        if (std::fabs (err) > kGainDeadbandDb)
            t.gainDb = std::clamp (kGainPull * err, -kGainMaxDb, kGainMaxDb);
    }
    return t;
}

// The signature every stream codes against: no tempo tag, no level reference.
inline Treatment decideTreatment (const SampleFeatures& f, const LaneTargets& lt, double bpm, float noteLength16)
{
    return decideTreatment (f, lt, bpm, noteLength16, TreatmentOptions {});
}

} // namespace rolla::genre
