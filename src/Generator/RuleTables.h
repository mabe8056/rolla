#pragma once

// Every number the rule composer (RuleComposer.cpp) samples from.
//
// These are ABSTRACTED STATISTICS, pooled per style over the whole reference
// set and smoothed. No row, cell or value here is a note sequence, a rhythm
// cell, a motif or a voicing taken from any one reference. Sources:
//   SR  data/genre/style_rules.json (Composer, v1.0): per-style p10/p50/p90 bands
//       pooled over 9-11 audio refs and 3-6 MIDI refs per style. Only the
//       aggregate fields are used: slot_class_weights, bass_onsets_per_bar,
//       bass_on_kick_pct, len*_pct, degree_emphasis, max_leap / leap_gt7_pct,
//       stab_in_gap / stab_on_bass, voicings (a genre-wide chord-quality palette),
//       open_hat.offbeat_8th_prob, perc_syncopation.e_a_share.
//   GR  docs/generator/GENERATOR_RULES.md numeric bands (bar-8 turn rest,
//       35% on-kick cap, DEEP back-half density contour).
//   SC  tools/score_rolls.py thresholds (onsets/bar windows, stab-in-gap
//       floor/bonus lines, 1-bar repeat <= 0.10 for JACKIN).
//   EST an estimate inside the SR band where SR gives only a band. Marked.
//
// Composer's next rule set can replace this header wholesale: RuleComposer.cpp
// reads nothing else. Every distribution is mixed with a uniform floor
// (kEntropyFloor) when it is sampled, so no slot, degree or voicing ever has
// zero weight unless a hard rule (kick cap, turn rest, mode) removes it.
//
// Style index order is the Dialect enum: ROLLER (UkMin), DEEP (Long),
// BOUNCY (Pop), JACKIN (Gaskin).

namespace rolla::rules::tables
{

inline constexpr int kStyles = 4;

// Share of each sampled distribution replaced by a uniform draw over the legal
// choices. Keeps per-style tables above an entropy floor.
inline constexpr float kEntropyFloor = 0.06f;

// ---------------------------------------------------------------- bass rhythm

// Composer onsets per bar at the shipped BUSY (0.40), before the density fill
// (fillBassDensity) and the BUSY scaling (applyBusyDensity).
//   ROLLER 5.0  SR bass_onsets_per_bar target 5.0
//   DEEP   4.6  SR target 5.21; the fill (4.9) and back-half contour add the rest
//   BOUNCY 5.0  SR target 5.0
//   JACKIN 5.6  SR audio target 4.44, MIDI target 6.88: the midpoint (no fill runs)
inline constexpr float kOnsetsPerBar[kStyles] = { 5.0f, 4.6f, 5.0f, 5.6f };

// Bar-to-bar spread of the onset count (+-, triangular). SR npb_sd p50:
// ROLLER 0.95, DEEP 1.12, BOUNCY 0.64, JACKIN 0.91.
inline constexpr float kOnsetSpread[kStyles] = { 0.95f, 1.12f, 0.64f, 0.91f };

// In-beat onset class weights: beat (16th 1 of the beat), e, and, a.
// SR slot_class_weights (audio in-beat histogram, mean over each style's refs).
inline constexpr float kSlotClass[kStyles][4] = {
    { 0.200f, 0.252f, 0.304f, 0.244f }, // ROLLER (11 refs)
    { 0.172f, 0.306f, 0.395f, 0.128f }, // DEEP
    { 0.343f, 0.118f, 0.329f, 0.211f }, // BOUNCY
    { 0.204f, 0.140f, 0.367f, 0.289f }, // JACKIN (9 refs)
};

// Share of composed onsets that may sit on a kick step. placeBassOnKicks then
// tops the line up to the style's on-kick target (SR bass_on_kick_pct p50:
// ROLLER 16.6, DEEP 6.5, BOUNCY 32.2, JACKIN 18.9), so the composer draws about
// half of it. The 35% cap (GR) is never approached.
inline constexpr float kKickShareRaw[kStyles] = { 0.08f, 0.03f, 0.16f, 0.09f };

// Bar-position factors (SR slot_weight_rule): the downbeat x0.8 (it is the
// kick), the last two 16ths x1.2 in bars 4 and 8 (answers and pickups).
inline constexpr float kDownbeatFactor = 0.8f;
inline constexpr float kAnswerTailFactor = 1.2f;

// Longest run of attacks on adjacent 16ths (SC F4: eaa <= 20%, run <= 7).
inline constexpr int kMaxAdjacentRun = 2;

// ------------------------------------------------------------- bass lengths

// Length menu in 16ths and per-style weights. SR len1 / len2 / len3_4 / len_ge5
// p50, with the unlabeled remainder on 1.5 and len3_4 split 60/40 over 3 and 4.
// The held-line pass (holdBassNotes) then sets the sounding ratio to about 0.80
// (SR bass_sounding_frac p50 0.79-0.83). BOUNCY's row is the BOUNCE mode
// (short notes, SR len1 48.9); BOUNCY pulse rewrites its own lengths.
inline constexpr int kLenChoices = 6;
inline constexpr float kLenMenu[kLenChoices] = { 1.f, 1.5f, 2.f, 3.f, 4.f, 5.f };
inline constexpr float kLenWeights[kStyles][kLenChoices] = {
    { 29.5f, 18.3f, 35.5f, 8.5f, 5.6f, 2.6f }, // ROLLER
    { 26.1f, 14.3f, 35.4f, 12.9f, 8.6f, 2.7f }, // DEEP
    { 48.9f, 20.0f, 15.2f, 10.0f, 5.3f, 0.6f }, // BOUNCY (bounce)
    { 30.6f, 6.5f, 41.2f, 12.2f, 8.1f, 1.4f },  // JACKIN
};

// ---------------------------------------------------------------- bass pitch

// Degree emphasis by scale-step index of the locked mode: 1, 2 (b2 in
// phrygian), 3, 4, 5, 6, 7. SR degree_emphasis; the b2 "neighbour only" weight
// is left to the harmony pass, which owns the b2 caps.
inline constexpr float kDegreeEmphasis[kStyles][7] = {
    { 0.45f, 0.18f, 0.12f, 0.07f, 0.08f, 0.05f, 0.03f }, // ROLLER
    { 0.45f, 0.10f, 0.12f, 0.10f, 0.13f, 0.08f, 0.02f }, // DEEP
    { 0.50f, 0.15f, 0.04f, 0.15f, 0.12f, 0.05f, 0.03f }, // BOUNCY (no b3 in SR: EST floor 0.04)
    { 0.33f, 0.10f, 0.13f, 0.10f, 0.12f, 0.15f, 0.07f }, // JACKIN (phrygian: index 1 is the b2)
};

// Interval preference by melodic distance in semitones (0 = repeat .. 12).
// EST shaped by SR max_leap and leap_gt7_pct p50 (ROLLER 12 / 15.4%, DEEP 8 /
// 13.9%... JACKIN 6 / 0%) and octave_pct; steps and fifths lead in every style.
inline constexpr float kIntervalPref[kStyles][13] = {
    { 0.55f, 0.50f, 1.00f, 0.90f, 0.60f, 0.80f, 0.20f, 0.80f, 0.40f, 0.30f, 0.50f, 0.10f, 0.40f }, // ROLLER
    { 0.50f, 0.40f, 1.00f, 0.90f, 0.60f, 0.80f, 0.15f, 0.90f, 0.30f, 0.30f, 0.45f, 0.10f, 0.30f }, // DEEP
    { 0.70f, 0.30f, 0.90f, 0.60f, 0.50f, 0.90f, 0.10f, 1.00f, 0.30f, 0.30f, 0.40f, 0.10f, 0.60f }, // BOUNCY
    { 0.60f, 0.80f, 1.00f, 0.90f, 0.60f, 0.70f, 0.15f, 0.30f, 0.05f, 0.05f, 0.10f, 0.02f, 0.05f }, // JACKIN
};

// P(the first onset of a call bar is the root). EST from SR root_pct p50
// (ROLLER 37.9, DEEP 41.0, BOUNCY 54.6, JACKIN 27.7) scaled to a bar anchor.
inline constexpr float kFirstRoot[kStyles] = { 0.65f, 0.70f, 0.80f, 0.55f };

// P(the answer bar ends off the root: the open answer). EST.
inline constexpr float kOpenAnswer[kStyles] = { 0.60f, 0.55f, 0.50f, 0.65f };

// DARK tilt on the degree emphasis at the knob ends: bright boosts the 5th,
// dark boosts the 3rd and the 2nd (the b2 in phrygian). EST, GR knob notes.
inline constexpr float kBrightFifth = 3.0f;
inline constexpr float kDarkThird = 2.5f;
inline constexpr float kDarkSecond = 2.0f;

// ------------------------------------------------------- phrase structure

// Call / answer (bars 1-2 vs 3-4). The answer bar keeps the call's front half
// with this probability and redraws the back half (JACKIN instead draws its
// answer into the call's silence: SC JACKIN 1-bar repeat <= 0.10, SR m1 p50 0.015).
inline constexpr float kAnswerKeepFront[kStyles] = { 0.50f, 0.60f, 0.55f, 0.0f };
// Weight on a call slot when the JACKIN answer is drawn (complement rule) and
// on a call back-half slot when another style's answer redraws it.
inline constexpr float kAnswerCallSlotWeight[kStyles] = { 0.45f, 0.50f, 0.50f, 0.12f };
inline constexpr float kAnswerSilentBoost[kStyles] = { 1.0f, 1.0f, 1.0f, 1.8f };

// 8-bar density arc: onset-count change for bars 5, 6 and 7 against bars 1-3.
// SR npb_range p50 2-4 and S_density_varies (sd >= 0.5). Four arc shapes,
// drawn per roll: flat, build, dip, swell. The shapes are abstract deltas.
inline constexpr int kArcShapes = 4;
inline constexpr int kArcDelta[kArcShapes][3] = {
    { 0, 0, 0 },  // flat
    { 1, 1, 0 },  // build into bars 5-6
    { -1, 0, 0 }, // dip at bar 5
    { 0, 1, -1 }, // swell at bar 6
};
inline constexpr float kArcWeights[kStyles][kArcShapes] = {
    { 0.30f, 0.30f, 0.20f, 0.20f }, // ROLLER
    { 0.25f, 0.35f, 0.15f, 0.25f }, // DEEP
    { 0.20f, 0.30f, 0.25f, 0.25f }, // BOUNCY (bounce wants bar-to-bar spread)
    { 0.40f, 0.25f, 0.15f, 0.20f }, // JACKIN (the loop picks one bar pair anyway)
};
inline constexpr const char* kArcNames[kArcShapes] = { "arc_flat", "arc_build", "arc_dip", "arc_swell" };

// DEEP density fill (fillBassDensity): extra (or, on a line already busy,
// fewer) attacks in bars 5, 6 and 7. GR deep_back_half "+0..2 a bar on a
// seeded contour". Abstract density deltas, five shapes, one drawn per roll.
inline constexpr int kDeepBackHalfShapes = 5;
inline constexpr int kDeepBackHalf[kDeepBackHalfShapes][3] = {
    { 0, 1, 2 }, { 1, 0, 2 }, { 1, 2, 1 }, { 2, 1, 1 }, { 0, 2, 1 },
};

// P(bar 5 or 6 redraws its back half: the back-half variation). EST, SR m4
// p50 0.66-0.84 (an 8-bar loop that is not two copies of four bars).
inline constexpr float kBackHalfVariation[kStyles] = { 0.55f, 0.65f, 0.60f, 0.40f };

// Bar 8 (the last bar): rest from this 16th on (GR four-16th turn rest).
inline constexpr int kTurnRestFrom = 12;
// P(the turn bar also drops its latest remaining attack). GR bar8_turnaround
// palette: truncation / drop-out share. EST.
inline constexpr float kTurnDropOne = 0.45f;

// Back-half guard (A&R flag): a back-half bar is thin when it plays one pitch
// class on at most this many attacks; two thin back-half bars = recompose.
inline constexpr int kThinBarMaxAttacks = 3;
inline constexpr int kThinBarsToFail = 2;
inline constexpr int kComposeRetries = 6;

// ------------------------------------------------------------------- mode

// Mode draw from DARK (Dorian bright, Phrygian dark). SR mode_weights over the
// minor modes (Aeolian 0.35-0.45, Dorian 0.25-0.30, Phrygian 0.08-0.25),
// expressed as a DARK curve: P(dorian) = a (1 - dark), P(phrygian) = b dark^2.
inline constexpr float kModeDorianA = 0.35f;
inline constexpr float kModePhrygianB = 0.22f;

// ------------------------------------------------------------------- stabs

// Stab chord-quality palette per style: interval sets above the chord root and
// weights. SR voicings (genre-wide quality palette, not reference voicings).
struct Voicing
{
    int n;
    int iv[4];
    float w;
};
inline constexpr int kVoicings = 8;
inline constexpr Voicing kStabVoicings[kStyles][kVoicings] = {
    { // ROLLER
      { 4, { 0, 3, 7, 10 }, 0.20f }, { 3, { 0, 3, 10, 0 }, 0.15f }, { 3, { 0, 2, 7, 0 }, 0.15f },
      { 3, { 0, 5, 10, 0 }, 0.15f }, { 3, { 0, 3, 7, 0 }, 0.10f }, { 4, { 0, 2, 3, 7 }, 0.10f },
      { 2, { 0, 7, 0, 0 }, 0.10f }, { 4, { 0, 4, 7, 11 }, 0.05f } },
    { // DEEP (SR: every 4 bars state a minor 3rd; open 5th <= 25%)
      { 4, { 0, 3, 7, 10 }, 0.25f }, { 3, { 0, 3, 10, 0 }, 0.20f }, { 4, { 0, 2, 3, 7 }, 0.15f },
      { 4, { 0, 3, 5, 10 }, 0.15f }, { 3, { 0, 5, 10, 0 }, 0.15f }, { 2, { 0, 7, 0, 0 }, 0.10f },
      { 3, { 0, 3, 7, 0 }, 0.0f }, { 3, { 0, 3, 7, 0 }, 0.0f } },
    { // BOUNCY (brighter set)
      { 4, { 0, 3, 7, 10 }, 0.20f }, { 2, { 0, 5, 0, 0 }, 0.15f }, { 4, { 0, 4, 7, 11 }, 0.15f },
      { 3, { 0, 2, 7, 0 }, 0.15f }, { 3, { 0, 4, 7, 0 }, 0.10f }, { 3, { 0, 5, 7, 0 }, 0.10f },
      { 4, { 0, 4, 7, 10 }, 0.10f }, { 2, { 0, 7, 0, 0 }, 0.05f } },
    { // JACKIN (dyad jabs; the b2 cluster only sounds in phrygian, which JACKIN is)
      { 4, { 0, 3, 7, 10 }, 0.20f }, { 4, { 0, 4, 7, 10 }, 0.15f }, { 4, { 0, 3, 5, 10 }, 0.15f },
      { 2, { 0, 2, 0, 0 }, 0.10f }, { 2, { 0, 3, 0, 0 }, 0.10f }, { 2, { 0, 5, 0, 0 }, 0.10f },
      { 3, { 0, 3, 7, 0 }, 0.10f }, { 3, { 0, 1, 5, 0 }, 0.10f } },
};
// Index of the JACKIN b2 cluster: dropped outside phrygian.
inline constexpr int kJackinB2Cluster = 7;

// Chord root by scale-step index of the mode. EST over SR voicing notes
// (a chord on the tonic, the 3rd, 4th, 5th, 6th and 7th; rarely the 2nd).
inline constexpr float kStabRootDegree[7] = { 0.30f, 0.04f, 0.20f, 0.12f, 0.15f, 0.09f, 0.10f };

// Register band, semitones over the stab lane's export base: the lowest tone
// sits in [lo, lo + 11], nothing above hi. SR stab_reg_med p50 MIDI 74-81.
inline constexpr int kStabRegLo[kStyles] = { 0, 0, 3, 0 };
inline constexpr int kStabRegHi[kStyles] = { 19, 17, 19, 19 };
// P(a 2-bar cell's voicing is lifted an octave, when it fits the band). EST
// from SR stab_voicing_change p50 0.43-0.49 (two voicings per roll carry most).
inline constexpr float kStabOctaveMove[kStyles] = { 0.15f, 0.10f, 0.15f, 0.12f };

// Where a stab hit sits against the bass: in a bass gap, over a bass sustain,
// or on a bass attack. Gap share: SR stab_in_gap p50 inside the SC windows
// (ROLLER 10-28%, DEEP 20-50%, JACKIN 12-45%). Stabs never double a bass
// attack in ROLLER, DEEP and JACKIN. BOUNCY is the one style whose pooled
// stab_on_bass is 100% (SR) with short notes and almost no sustain, so it
// keeps a share on the bass attack; SC holds its gap share under 50%.
inline constexpr float kStabGapShare[kStyles] = { 0.18f, 0.32f, 0.20f, 0.29f };
inline constexpr float kStabOnsetShare[kStyles] = { 0.0f, 0.0f, 0.55f, 0.0f };

// Stab in-beat class weights (beats are kick steps: never used). EST from SR
// stab_onbeat p50 (0-16%): the 8th offbeat leads, e / a follow.
inline constexpr float kStabSlotClass[3] = { 0.30f, 0.45f, 0.25f }; // e, and, a

// ------------------------------------------------------------------- drums

// Offbeat-8th hat presence per bar slot. SR open_hat.offbeat_8th_prob.
inline constexpr float kHatOffbeatProb[kStyles] = { 0.85f, 0.75f, 0.95f, 0.90f };
// P(an offbeat hat is the open one): the last offbeat of an odd bar, else
// rarely (SR open_hat.extra_slot_prob 0.1-0.2, EST split).
inline constexpr float kOpenHatTail = 0.85f;
inline constexpr float kOpenHatOther = 0.05f;
// Perc / shaker placement: e / a against the 8th offbeat. SR
// perc_syncopation.e_a_share 0.45-0.55: eight e/a slots, four "and" slots,
// so the "and" weight is about twice the e/a weight.
inline constexpr float kPercEaWeight = 1.0f;
inline constexpr float kPercAndWeight = 2.0f;
// Rim and tom favour the 8th offbeat (EST).
inline constexpr float kRimAndWeight = 2.5f;
inline constexpr float kRimEaWeight = 1.0f;
// P(a bar carries a ghost snare). SR ghost_notes.prob_per_16th band, per bar EST.
inline constexpr float kSnareGhostBar = 0.5f;

} // namespace rolla::rules::tables
