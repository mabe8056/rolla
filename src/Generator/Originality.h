#pragma once

// Originality floor (GENERATOR_RULES v2 H4, the owner's "never copy" rule).
//
// generate() runs check() on every roll it makes and redraws the lanes that hit,
// with derived seeds (caller seed + k), so a copy never ships. Two sources:
//
//   1. Built in, always on, no reference data: the banned figures that
//      GENERATOR_RULES H4 already names (the root on 16ths 2 and 4 that
//      avoidReaction guards, the b3 tied over the barline from 16th 15, and the dotted-pulse
//      skeleton the A&R synthesised from its key-free description). These are
//      rules from the repo, not transcriptions.
//   2. Optional: a hashed fingerprint index built on the owner's box from the
//      private reference MIDI (rolla-rolls --build-orig-index). It holds only
//      64-bit hashes and MinHash signatures keyed by opaque ids: no notes, no
//      names, no paths. It lives outside the repo and the bundle (see
//      OriginalityJson.h for where), so nothing reference-derived ships.
//
// The features follow tools/originality_check.py (OC), the A&R gate:
//   skel_ngram  (interval, inter-onset gap) 3-grams, gaps +-1 16th. Exact set
//               containment over hashes, so this matches OC.
//   skel_win    (bar, 16th, pitch class) token sets over 2- and 4-bar windows,
//               best of 12 transpositions x shifts -2..+2. Jaccard is estimated
//               with a 72-hash MinHash and found with LSH (24 bands x 3 rows).
//   voicing     chord hits: same place in the reference loop (+-1 16th, best
//               whole-bar offset and transposition) and the same pitch-class
//               set (or one note more / less, OC's Jaccard >= 0.75), one-to-one
//               per reference hit. A bar-free pass picks the references worth
//               the full match.
// OC's triviality guards are kept (>= 6 onsets, >= 2 non-zero moves, generic
// rhythm and pulse windows skipped, containment / max(n, 5), >= 3 chord hits).
//
// No JUCE here, so every target that builds the generator gets it for free.

#include "Pattern.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace rolla::orig
{

inline constexpr int kExtractor = 1;     // bump when a feature or hash changes
inline constexpr int kSigK = 72;         // MinHash size (Jaccard error ~0.06 at 0.5)
inline constexpr int kBands = 24;        // LSH bands: a true 0.55 is found with p ~0.99
inline constexpr int kRows = 3;          // rows per band (kBands * kRows == kSigK)
inline constexpr int kMinEvents = 6;     // OC MIN_EVENTS
inline constexpr int kMinNgrams = 5;     // OC MIN_NGRAMS
inline constexpr int kMaxBarsRef = 64;   // OC MAX_BARS
inline constexpr int kMerged = -1;       // stream id of the merged lowest voice (OC MERGED_LOW)
static_assert (kBands * kRows == kSigK, "LSH bands must cover the signature");

enum Metric : int
{
    MetricNone = 0,
    MetricSkelWin,
    MetricSkelNgram,
    MetricVoicing,
    MetricBannedFigure
};

inline const char* metricName (int m) noexcept
{
    static const char* names[] = { "none", "skel_win", "skel_ngram", "voicing", "banned_figure" };
    return names[(m < 0 || m > MetricBannedFigure) ? 0 : m];
}

// Redraw at or above these. Each sits a little under OC's FAIL line
// (0.55 / 0.45 / 0.75) to absorb MinHash noise and swing rounding.
struct Thresholds
{
    float skelWin = 0.50f;
    float skelNgram = 0.40f;
    float voicing = 0.70f;
};

// ------------------------------------------------------------------ hashing
// splitmix64 finaliser. The index file stores these values, so never change
// them without bumping kExtractor.
inline std::uint64_t mix64 (std::uint64_t x) noexcept
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline std::uint64_t hashInts (std::uint64_t domain, const int* v, int n) noexcept
{
    std::uint64_t h = mix64 (domain);
    for (int i = 0; i < n; ++i)
        h = mix64 (h ^ (std::uint64_t) (std::uint32_t) v[i]);
    return h;
}

inline std::uint64_t hashText (const std::string& s) noexcept
{
    std::uint64_t h = 0xCBF29CE484222325ull; // FNV-1a
    for (unsigned char c : s)
    {
        h ^= c;
        h *= 0x100000001B3ull;
    }
    return mix64 (h);
}

inline constexpr std::uint64_t kDomNgram = 0x5EE1A11ull;
inline constexpr std::uint64_t kDomSig = 0x51A7ull;
inline constexpr std::uint64_t kDomBand = 0xBA4Dull;
inline constexpr std::uint64_t kDomVoice = 0x7015Eull;
inline constexpr std::uint64_t kDomVoiceAt = 0x7015EA7ull;

inline int floorDiv (int a, int b) noexcept
{
    int q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        --q;
    return q;
}

inline int floorMod (int a, int b) noexcept
{
    const int m = a % b;
    return m < 0 ? m + b : m;
}

inline int popcount12 (unsigned m) noexcept
{
    int n = 0;
    for (int i = 0; i < 12; ++i)
        n += (int) ((m >> i) & 1u);
    return n;
}

inline unsigned rotateMask (unsigned mask, int t) noexcept
{
    t = floorMod (t, 12);
    if (t == 0)
        return mask & 0xFFFu;
    return ((mask << t) | (mask >> (12 - t))) & 0xFFFu;
}

// ------------------------------------------------------------------ streams
struct RawNote
{
    double start16 = 0.0; // onset in 16ths from the loop start
    int pitch = 0;
};

struct Ev
{
    int slot = 0;
    int pitch = 0;
};

struct ChordHit
{
    int slot = 0;
    unsigned mask = 0; // pitch classes, bit 0 = C (or the key root for lanes)
};

struct Stream
{
    std::vector<Ev> ev;        // lowest note per 16th onset (OC to_events)
    std::vector<ChordHit> ch;  // onsets with >= 2 pitch classes (OC to_chords)
};

// OC rounds onsets with Python's round(), which is half-to-even: so does
// std::nearbyint in the default rounding mode.
inline Stream streamFromNotes (const std::vector<RawNote>& notes)
{
    std::map<int, int> low;
    std::map<int, unsigned> pcs;
    for (const auto& n : notes)
    {
        const int slot = (int) std::nearbyint (n.start16);
        if (slot < 0 || slot >= kMaxBarsRef * 16)
            continue;
        auto it = low.find (slot);
        if (it == low.end())
            low[slot] = n.pitch;
        else
            it->second = std::min (it->second, n.pitch);
        pcs[slot] |= 1u << floorMod (n.pitch, 12);
    }
    Stream s;
    for (const auto& kv : low)
        s.ev.push_back ({ kv.first, kv.second });
    for (const auto& kv : pcs)
        if (popcount12 (kv.second) >= 2)
            s.ch.push_back ({ kv.first, kv.second });
    return s;
}

// The MIDI export's octave per lane, without the key (MidiFile.cpp basePitch).
// Only the merged lowest voice needs it: every feature is transposition-free.
inline int exportBase (int part, int dialect) noexcept
{
    if (part == Bass)
        return 12 * ((dialect == Pop ? 2 : 1) + 1);
    if (part == Pluck)
        return 72;
    if (part == Vox)
        return 64;
    return 60;
}

// A lane as the MIDI export writes it (notes at velocity <= 2 are skipped there too).
inline std::vector<RawNote> rawNotes (const Lane& lane, int base = 0)
{
    std::vector<RawNote> out;
    for (int i = 0; i < lane.count && i < kMaxNotes; ++i)
    {
        const Note& n = lane.notes[i];
        if (n.vel <= 2)
            continue;
        const int count = std::max (1, std::min (4, (int) n.nChord));
        for (int c = 0; c < count; ++c)
            out.push_back ({ (double) n.start, base + (int) n.chord[c] });
    }
    return out;
}

inline int nbarsOf (const std::vector<Ev>& ev) noexcept
{
    return ev.empty() ? 1 : floorDiv (ev.back().slot, 16) + 1;
}

inline int distinctPcs (const std::vector<Ev>& ev) noexcept
{
    unsigned m = 0;
    for (const auto& e : ev)
        m |= 1u << floorMod (e.pitch, 12);
    return popcount12 (m);
}

// ------------------------------------------------------------------ skel_ngram
struct Gram
{
    int iv[3] {};
    int gp[3] {};
};

inline bool nontrivial (const int* iv) noexcept
{
    int moves = 0;
    for (int k = 0; k < 3; ++k)
        moves += iv[k] != 0 ? 1 : 0;
    return moves >= 2;
}

inline std::uint64_t gramKey (const int iv[3], const int gp[3]) noexcept
{
    const int v[6] = { iv[0], iv[1], iv[2], gp[0], gp[1], gp[2] };
    return hashInts (kDomNgram, v, 6);
}

// Every (interval, gap) 3-gram with >= 2 non-zero moves, keyed and de-duplicated.
inline std::map<std::uint64_t, Gram> gramsOf (const std::vector<Ev>& ev)
{
    std::map<std::uint64_t, Gram> out;
    const int n = (int) ev.size();
    for (int i = 0; i + 3 < n; ++i)
    {
        Gram g;
        for (int k = 0; k < 3; ++k)
        {
            g.iv[k] = ev[(size_t) (i + k + 1)].pitch - ev[(size_t) (i + k)].pitch;
            g.gp[k] = ev[(size_t) (i + k + 1)].slot - ev[(size_t) (i + k)].slot;
        }
        if (nontrivial (g.iv))
            out.emplace (gramKey (g.iv, g.gp), g);
    }
    return out;
}

// ------------------------------------------------------------------ skel_win
using Bars = std::vector<std::vector<int>>; // per bar: sorted unique slot * 12 + pc

inline Bars barSets (const std::vector<Ev>& ev, int nb, int shift)
{
    Bars bars ((size_t) std::max (1, nb));
    for (const auto& e : ev)
    {
        const int s = e.slot + shift;
        const int b = floorMod (floorDiv (s, 16), (int) bars.size());
        bars[(size_t) b].push_back (floorMod (s, 16) * 12 + floorMod (e.pitch, 12));
    }
    for (auto& b : bars)
    {
        std::sort (b.begin(), b.end());
        b.erase (std::unique (b.begin(), b.end()), b.end());
    }
    return bars;
}

// Token = bar-in-window * 192 + slot * 12 + pc. Windows wrap like OC's window().
inline std::vector<int> windowTokens (const Bars& bars, int start, int w)
{
    std::vector<int> out;
    const int nb = (int) bars.size();
    for (int k = 0; k < w; ++k)
        for (int x : bars[(size_t) ((start + k) % nb)])
            out.push_back (k * 192 + x);
    return out;
}

inline std::vector<int> transposeTokens (const std::vector<int>& toks, int t)
{
    std::vector<int> out;
    out.reserve (toks.size());
    for (int tok : toks)
    {
        const int k = tok / 192;
        const int rem = tok % 192;
        out.push_back (k * 192 + (rem / 12) * 12 + floorMod (rem % 12 + t, 12));
    }
    return out;
}

using Sig = std::array<std::uint32_t, kSigK>;

inline constexpr int kTokens = 4 * 192; // every window token is below this

inline std::uint64_t tokenHash (int i, int tok) noexcept
{
    const std::uint64_t salt = mix64 (kDomSig + (std::uint64_t) (i + 1));
    return mix64 (salt ^ (std::uint64_t) (std::uint32_t) tok);
}

// tokenHash for every (hash, token), built once: windows are hashed thousands of times a roll.
inline const std::vector<std::uint64_t>& tokenHashTable()
{
    static const std::vector<std::uint64_t> table = []
    {
        std::vector<std::uint64_t> t ((size_t) kSigK * kTokens);
        for (int i = 0; i < kSigK; ++i)
            for (int tok = 0; tok < kTokens; ++tok)
                t[(size_t) i * kTokens + (size_t) tok] = tokenHash (i, tok);
        return t;
    }();
    return table;
}

inline Sig minhash (const std::vector<int>& toks)
{
    const auto& table = tokenHashTable();
    Sig sig {};
    for (int i = 0; i < kSigK; ++i)
    {
        std::uint64_t best = ~0ull;
        for (int tok : toks)
        {
            const std::uint64_t h = (tok >= 0 && tok < kTokens) ? table[(size_t) i * kTokens + (size_t) tok] : tokenHash (i, tok);
            best = std::min (best, h);
        }
        sig[(size_t) i] = (std::uint32_t) (best >> 32);
    }
    return sig;
}

inline std::uint64_t bandKey (const Sig& sig, int band) noexcept
{
    int v[kRows];
    for (int r = 0; r < kRows; ++r)
        v[r] = (int) sig[(size_t) (band * kRows + r)];
    return hashInts (kDomBand + (std::uint64_t) band, v, kRows);
}

inline float sigSimilarity (const Sig& a, const Sig& b) noexcept
{
    int same = 0;
    for (int i = 0; i < kSigK; ++i)
        same += a[(size_t) i] == b[(size_t) i] ? 1 : 0;
    return (float) same / (float) kSigK;
}

// OC GENERIC_BAR_RHYTHMS as 16-bit onset masks.
inline bool genericBar (unsigned onsets) noexcept
{
    return onsets == 0u || onsets == 0xFFFFu || onsets == 0x5555u || onsets == 0xAAAAu
        || onsets == 0x1111u || onsets == 0x4444u;
}

// ------------------------------------------------------------------ voicing
inline std::uint64_t voiceKey (int slot16, unsigned mask) noexcept
{
    const int v[2] = { floorMod (slot16, 16), (int) (mask & 0xFFFu) };
    return hashInts (kDomVoice, v, 2);
}

// A chord hit at its place in the reference loop (slot mod the loop length).
inline std::uint64_t voiceAtKey (int slot, int loopLen, unsigned mask) noexcept
{
    const int v[3] = { floorMod (slot, std::max (1, loopLen)), (int) (mask & 0xFFFu), loopLen };
    return hashInts (kDomVoiceAt, v, 3);
}

// ------------------------------------------------------------------ index
struct RefData
{
    std::string id;     // opaque: hash of the private file path + stream number
    int chordHits = 0;  // OC needs >= 3 on both sides
    std::vector<std::uint64_t> ngrams;                 // sorted unique gram keys
    std::vector<Sig> sigs;                             // 2- and 4-bar windows
    std::vector<std::pair<std::uint64_t, int>> voices; // voicing key (16th in bar), count
    int voiceBars = 0;                                 // OC rlen_bars
    std::vector<std::uint64_t> voiceAt;                // sorted voiceAtKey per chord hit
};

struct Index
{
    Thresholds th;
    std::vector<RefData> refs;

    // Lookup tables, rebuilt by finalize().
    std::unordered_map<std::uint64_t, std::vector<int>> ngramRefs;
    std::vector<std::pair<int, int>> sigOwner; // (ref, sig)
    std::unordered_map<std::uint64_t, std::vector<int>> bands;
    std::unordered_map<std::uint64_t, std::vector<std::pair<int, int>>> voiceRefs; // key -> (ref, count)

    bool empty() const noexcept { return refs.empty(); }

    void finalize()
    {
        ngramRefs.clear();
        sigOwner.clear();
        bands.clear();
        voiceRefs.clear();
        for (int r = 0; r < (int) refs.size(); ++r)
        {
            const auto& ref = refs[(size_t) r];
            for (auto key : ref.ngrams)
                ngramRefs[key].push_back (r);
            for (int s = 0; s < (int) ref.sigs.size(); ++s)
            {
                const int at = (int) sigOwner.size();
                sigOwner.push_back ({ r, s });
                for (int b = 0; b < kBands; ++b)
                    bands[bandKey (ref.sigs[(size_t) s], b)].push_back (at);
            }
            for (const auto& kv : ref.voices)
                voiceRefs[kv.first].push_back ({ r, kv.second });
        }
    }

    // Reference side of every feature, from one OC stream. Call finalize() after the last.
    void addRef (const std::string& id, const Stream& s)
    {
        RefData ref;
        ref.id = id;
        ref.chordHits = (int) s.ch.size();
        if ((int) s.ev.size() >= kMinEvents)
        {
            for (const auto& kv : gramsOf (s.ev))
                ref.ngrams.push_back (kv.first);
            const int rnb = nbarsOf (s.ev);
            const Bars bars = barSets (s.ev, rnb, 0);
            for (int w : { 2, 4 })
            {
                if (w > rnb)
                    continue;
                for (int j = 0; j < rnb; ++j)
                {
                    const auto toks = windowTokens (bars, j, w);
                    if (! toks.empty())
                        ref.sigs.push_back (minhash (toks));
                }
            }
        }
        std::map<std::uint64_t, int> voices;
        for (const auto& c : s.ch)
            ++voices[voiceKey (c.slot % 16, c.mask)];
        for (const auto& kv : voices)
            ref.voices.push_back (kv);
        if (! s.ch.empty())
        {
            ref.voiceBars = std::max (nbarsOf (s.ev), floorDiv (s.ch.back().slot, 16) + 1);
            for (const auto& c : s.ch)
                ref.voiceAt.push_back (voiceAtKey (c.slot, ref.voiceBars * 16, c.mask));
            std::sort (ref.voiceAt.begin(), ref.voiceAt.end());
            ref.voiceAt.erase (std::unique (ref.voiceAt.begin(), ref.voiceAt.end()), ref.voiceAt.end());
        }
        if (ref.ngrams.empty() && ref.sigs.empty() && ref.voices.empty())
            return;
        refs.push_back (std::move (ref));
    }
};

// ------------------------------------------------------------------ scoring
struct Hit
{
    int stream = kMerged - 1; // part index, kMerged, or none
    int ref = -1;
    int metric = MetricNone;
    float score = 0.f;
    float severity = 0.f; // score / threshold: >= 1 is a copy
    bool builtin = false;
    std::string refId;
};

struct Report
{
    bool pass = true;
    std::uint32_t bumpMask = 0; // lanes to redraw
    Hit worst;                  // highest severity seen, hit or not
    int hits = 0;
};

namespace detail
{

inline void note (Report& rep, const Hit& h, std::uint32_t bump)
{
    if (h.severity >= 1.f)
    {
        rep.pass = false;
        rep.bumpMask |= bump;
        ++rep.hits;
    }
    if (h.severity > rep.worst.severity)
        rep.worst = h;
}

inline void scoreNgrams (const Stream& c, const Index& idx, int stream, bool builtin, Report& rep, std::uint32_t bump)
{
    if ((int) c.ev.size() < kMinEvents || idx.ngramRefs.empty())
        return;
    const auto grams = gramsOf (c.ev);
    if (grams.empty())
        return;
    std::map<int, int> count;
    for (const auto& kv : grams)
    {
        std::set<int> refsHit;
        const Gram& g = kv.second;
        for (int d0 = -1; d0 <= 1; ++d0)
            for (int d1 = -1; d1 <= 1; ++d1)
                for (int d2 = -1; d2 <= 1; ++d2)
                {
                    const int gp[3] = { g.gp[0] + d0, g.gp[1] + d1, g.gp[2] + d2 };
                    if (gp[0] <= 0 || gp[1] <= 0 || gp[2] <= 0)
                        continue;
                    auto it = idx.ngramRefs.find (gramKey (g.iv, gp));
                    if (it != idx.ngramRefs.end())
                        refsHit.insert (it->second.begin(), it->second.end());
                }
        for (int r : refsHit)
            ++count[r];
    }
    const float denom = (float) std::max ((int) grams.size(), kMinNgrams);
    for (const auto& kv : count)
    {
        Hit h;
        h.stream = stream;
        h.ref = kv.first;
        h.metric = MetricSkelNgram;
        h.score = (float) kv.second / denom;
        h.severity = h.score / std::max (0.01f, idx.th.skelNgram);
        h.builtin = builtin;
        h.refId = idx.refs[(size_t) kv.first].id;
        note (rep, h, bump);
    }
}

inline void scoreWindows (const Stream& c, const Index& idx, int stream, bool builtin, Report& rep, std::uint32_t bump)
{
    if ((int) c.ev.size() < kMinEvents || idx.bands.empty() || distinctPcs (c.ev) < 2)
        return;
    const int cnb = nbarsOf (c.ev);
    Bars shifted[5];
    for (int sh = -2; sh <= 2; ++sh)
        shifted[sh + 2] = barSets (c.ev, cnb, sh);
    const Bars& plain = shifted[2];
    std::map<int, float> best;
    for (int w : { 2, 4 })
    {
        if (w > cnb)
            continue;
        for (int i = 0; i + w <= cnb; ++i)
        {
            // OC guard: a window whose every bar is a generic rhythm or one pitch class is a pulse, not a skeleton.
            bool pulse = true;
            for (int k = 0; k < w && pulse; ++k)
            {
                unsigned onsets = 0, pcs = 0;
                for (int x : plain[(size_t) (i + k)])
                {
                    onsets |= 1u << (x / 12);
                    pcs |= 1u << (x % 12);
                }
                if (! (genericBar (onsets) || popcount12 (pcs) <= 1))
                    pulse = false;
            }
            if (pulse)
                continue;
            for (int s = 0; s < 5; ++s)
            {
                const auto toks = windowTokens (shifted[s], i, w);
                unsigned pcs = 0;
                for (int tok : toks)
                    pcs |= 1u << ((tok % 192) % 12);
                if ((int) toks.size() < 4 || popcount12 (pcs) < 2)
                    continue;
                for (int t = 0; t < 12; ++t)
                {
                    const Sig sig = minhash (transposeTokens (toks, t));
                    std::set<int> seen;
                    for (int b = 0; b < kBands; ++b)
                    {
                        auto it = idx.bands.find (bandKey (sig, b));
                        if (it == idx.bands.end())
                            continue;
                        for (int at : it->second)
                        {
                            if (! seen.insert (at).second)
                                continue;
                            const auto& own = idx.sigOwner[(size_t) at];
                            const float j = sigSimilarity (sig, idx.refs[(size_t) own.first].sigs[(size_t) own.second]);
                            auto& slot = best[own.first];
                            slot = std::max (slot, j);
                        }
                    }
                }
            }
        }
    }
    for (const auto& kv : best)
    {
        Hit h;
        h.stream = stream;
        h.ref = kv.first;
        h.metric = MetricSkelWin;
        h.score = kv.second;
        h.severity = h.score / std::max (0.01f, idx.th.skelWin);
        h.builtin = builtin;
        h.refId = idx.refs[(size_t) kv.first].id;
        note (rep, h, bump);
    }
}

// Pitch-class sets within OC's Jaccard >= 0.75 of m: m itself, one note less
// (n >= 4: (n-1)/n), one note more (n >= 3: n/(n+1)).
inline std::vector<unsigned> voicingVariants (unsigned m)
{
    const int n = popcount12 (m);
    std::vector<unsigned> masks { m };
    for (int p = 0; p < 12; ++p)
    {
        const unsigned bit = 1u << p;
        if ((m & bit) != 0 && n >= 4)
            masks.push_back (m & ~bit);
        if ((m & bit) == 0 && n >= 3)
            masks.push_back (m | bit);
    }
    return masks;
}

// OC voicing_match for one reference and one transposition: the best whole-bar
// offset, each candidate hit matched to at most one unused reference hit within
// +-1 16th (cyclic over the reference length).
inline int voicingFine (const std::vector<ChordHit>& cc, const RefData& ref, int t)
{
    if (ref.voiceBars <= 0 || ref.voiceAt.empty())
        return 0;
    const int len = ref.voiceBars * 16;
    int best = 0;
    for (int o = 0; o < ref.voiceBars; ++o)
    {
        std::set<std::uint64_t> used;
        int matched = 0;
        for (const auto& hit : cc)
        {
            const auto masks = voicingVariants (rotateMask (hit.mask, t));
            bool found = false;
            for (int d : { 0, -1, 1 })
            {
                for (unsigned mm : masks)
                {
                    const auto key = voiceAtKey (hit.slot + 16 * o + d, len, mm);
                    if (used.count (key) == 0 && std::binary_search (ref.voiceAt.begin(), ref.voiceAt.end(), key))
                    {
                        used.insert (key);
                        found = true;
                        break;
                    }
                }
                if (found)
                    break;
            }
            matched += found ? 1 : 0;
        }
        best = std::max (best, matched);
    }
    return best;
}

inline void scoreVoicing (const Stream& c, const Index& idx, int stream, bool builtin, Report& rep, std::uint32_t bump)
{
    if ((int) c.ch.size() < 3 || idx.voiceRefs.empty())
        return;
    const float threshold = std::max (0.01f, idx.th.voicing);
    std::map<int, int> bestMatched;
    for (int t = 0; t < 12; ++t)
    {
        // Coarse pass, bar position ignored: an upper bound on the fine count,
        // so only references that could reach the threshold get the full match.
        std::map<std::pair<int, std::uint64_t>, int> used;
        std::map<int, int> coarse;
        for (const auto& hit : c.ch)
        {
            const auto masks = voicingVariants (rotateMask (hit.mask, t));
            std::set<int> thisHit;
            for (int d : { 0, -1, 1 })
                for (unsigned mm : masks)
                {
                    const auto key = voiceKey (hit.slot % 16 + d, mm);
                    auto it = idx.voiceRefs.find (key);
                    if (it == idx.voiceRefs.end())
                        continue;
                    for (const auto& rc : it->second)
                    {
                        if (idx.refs[(size_t) rc.first].chordHits < 3 || thisHit.count (rc.first) != 0)
                            continue;
                        int& u = used[{ rc.first, key }];
                        if (u >= rc.second)
                            continue;
                        ++u;
                        ++coarse[rc.first];
                        thisHit.insert (rc.first);
                    }
                }
        }
        for (const auto& kv : coarse)
        {
            if (kv.second < 3 || (float) kv.second / (float) c.ch.size() < 0.5f * threshold)
                continue;
            const int fine = voicingFine (c.ch, idx.refs[(size_t) kv.first], t);
            bestMatched[kv.first] = std::max (bestMatched[kv.first], fine);
        }
    }
    for (const auto& kv : bestMatched)
    {
        if (kv.second < 3)
            continue;
        Hit h;
        h.stream = stream;
        h.ref = kv.first;
        h.metric = MetricVoicing;
        h.score = (float) kv.second / (float) c.ch.size();
        h.severity = h.score / std::max (0.01f, idx.th.voicing);
        h.builtin = builtin;
        h.refId = idx.refs[(size_t) kv.first].id;
        note (rep, h, bump);
    }
}

} // namespace detail

inline void scoreStream (const Stream& c, const Index& idx, int stream, bool builtin, Report& rep, std::uint32_t bump)
{
    if (idx.empty())
        return;
    detail::scoreNgrams (c, idx, stream, builtin, rep, bump);
    detail::scoreWindows (c, idx, stream, builtin, rep, bump);
    detail::scoreVoicing (c, idx, stream, builtin, rep, bump);
}

// ------------------------------------------------------------------ built in
// GENERATOR_RULES H4: root on 16ths 2 and 4 (0-based 1 and 3) in more than half
// the bars, or a b3 from 16th 15 (0-based 14) held over the barline in more
// than half the bars. Bass pitches are relative to the key root.
inline bool bannedBassFigure (const Lane& bass, int bars) noexcept
{
    bars = std::clamp (bars, 1, kMaxBars);
    int reaction = 0;
    int tie = 0;
    for (int b = 0; b < bars; ++b)
    {
        bool r1 = false, r3 = false, held = false;
        for (int i = 0; i < bass.count && i < kMaxNotes; ++i)
        {
            const Note& n = bass.notes[i];
            if (n.vel <= 2)
                continue;
            const int st = (int) std::lround (n.start);
            if (floorDiv (st, 16) != b)
                continue;
            const int slot = floorMod (st, 16);
            const int pc = floorMod (n.chord[0], 12);
            if (pc == 0 && slot == 1)
                r1 = true;
            if (pc == 0 && slot == 3)
                r3 = true;
            if (pc == 3 && slot == 14 && n.start + n.length > (float) ((b + 1) * 16) + 0.01f)
                held = true;
        }
        reaction += (r1 && r3) ? 1 : 0;
        tie += held ? 1 : 0;
    }
    return reaction * 2 > bars || tie * 2 > bars;
}

// The dotted-pulse banned skeletons (SCORER_CHANGELOG item 5): the upper note on
// 16ths 1 and 4, a whole step lower on 7, 10 and 13, key-free; plus the tail and
// neighbour onset sets the generator already avoids (lower note on the extras).
// Synthesised from that description, four bars each, so no transcription is used.
inline const Index& builtinIndex()
{
    static const Index idx = []
    {
        Index out;
        const int core[] = { 0, 3, 6, 9, 12 };
        const int tail[] = { 0, 3, 6, 9, 12, 14 };
        const int neighbour[] = { 0, 3, 6, 8, 9, 12, 14 };
        auto add = [&out] (const char* id, const int* slots, int n)
        {
            std::vector<RawNote> notes;
            for (int bar = 0; bar < 4; ++bar)
                for (int k = 0; k < n; ++k)
                    notes.push_back ({ (double) (bar * 16 + slots[k]), slots[k] < 6 ? 2 : 0 });
            out.addRef (id, streamFromNotes (notes));
        };
        add ("banned:dotted_pulse_core", core, 5);
        add ("banned:dotted_pulse_tail", tail, 6);
        add ("banned:dotted_pulse_neighbour", neighbour, 7);
        out.finalize();
        return out;
    }();
    return idx;
}

// ------------------------------------------------------------------ external index slot
// The process-wide index the generator checks besides the built-in one. Null or
// empty means only the built-in guard runs. Same seed + same index = same roll.
inline std::shared_ptr<const Index>& indexSlot()
{
    static std::shared_ptr<const Index> slot;
    return slot;
}

inline void setIndex (std::shared_ptr<const Index> index)
{
    std::atomic_store (&indexSlot(), std::move (index));
}

inline std::shared_ptr<const Index> currentIndex()
{
    return std::atomic_load (&indexSlot());
}

// ------------------------------------------------------------------ check
// Scores the regenerated pitched lanes in partMask (and the merged lowest voice
// of every pitched lane, as OC does) against the built-in guard and `external`.
// bumpMask names the lanes to redraw: the lane that hit, or for a merged hit
// the bass when it was regenerated (else every regenerated pitched lane).
inline Report check (const Groove& g, std::uint32_t partMask, const Index* external)
{
    Report rep;
    const std::uint32_t pitched = partMask & ((1u << Drums) - 1u);
    if (pitched == 0)
        return rep;
    const std::uint32_t mergedBump = (pitched & (1u << Bass)) != 0 ? (1u << Bass) : pitched;

    struct S
    {
        int part;
        Stream s;
    };
    std::vector<S> streams;
    std::vector<RawNote> merged;
    int lanesWithNotes = 0;
    for (int p = 0; p < Drums; ++p)
    {
        const auto raw = rawNotes (g.lane[p], exportBase (p, g.dialect));
        if (raw.empty())
            continue;
        ++lanesWithNotes;
        merged.insert (merged.end(), raw.begin(), raw.end());
        if ((pitched >> p) & 1u)
            streams.push_back ({ p, streamFromNotes (raw) });
    }
    if (lanesWithNotes > 1)
        streams.push_back ({ kMerged, streamFromNotes (merged) });

    if ((pitched & (1u << Bass)) != 0 && bannedBassFigure (g.lane[Bass], g.bars))
    {
        Hit h;
        h.stream = Bass;
        h.metric = MetricBannedFigure;
        h.score = 1.f;
        h.severity = 1.f;
        h.builtin = true;
        h.refId = "banned:reaction_or_b3_tie";
        detail::note (rep, h, 1u << Bass);
    }
    const Index& builtin = builtinIndex();
    const bool useExternal = external != nullptr && ! external->empty();
    for (const auto& st : streams)
    {
        const std::uint32_t bump = st.part == kMerged ? mergedBump : (1u << st.part);
        scoreStream (st.s, builtin, st.part, true, rep, bump);
        if (useExternal)
            scoreStream (st.s, *external, st.part, false, rep, bump);
    }
    return rep;
}

// ------------------------------------------------------------------ hex helpers (index file)
inline std::string toHex (std::uint64_t v, int digits)
{
    static const char* hex = "0123456789abcdef";
    std::string s ((size_t) digits, '0');
    for (int i = digits - 1; i >= 0; --i)
    {
        s[(size_t) i] = hex[v & 0xFu];
        v >>= 4;
    }
    return s;
}

inline bool fromHex (const std::string& s, size_t at, int digits, std::uint64_t& out) noexcept
{
    if (at + (size_t) digits > s.size())
        return false;
    std::uint64_t v = 0;
    for (int i = 0; i < digits; ++i)
    {
        const char c = s[at + (size_t) i];
        int d = -1;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        if (d < 0)
            return false;
        v = (v << 4) | (std::uint64_t) d;
    }
    out = v;
    return true;
}

inline std::string sigToHex (const Sig& sig)
{
    std::string s;
    s.reserve (kSigK * 8);
    for (auto v : sig)
        s += toHex (v, 8);
    return s;
}

inline bool sigFromHex (const std::string& s, Sig& sig) noexcept
{
    if (s.size() != (size_t) kSigK * 8)
        return false;
    for (int i = 0; i < kSigK; ++i)
    {
        std::uint64_t v = 0;
        if (! fromHex (s, (size_t) i * 8, 8, v))
            return false;
        sig[(size_t) i] = (std::uint32_t) v;
    }
    return true;
}

} // namespace rolla::orig
