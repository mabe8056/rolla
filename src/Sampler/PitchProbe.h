#pragma once

// Pitch truth for library samples, measured once when a file loads.
// No JUCE: the plugin, the tests and the manifest tools share this code.
//
// probePitch: an averaged spectrum after the attack, then a harmonic sieve. The
// lowest strong peak is a note and its harmonics (2..10) are explained; repeat on
// what is left. One note is a single tone (its pitch refined by NSDF), two or
// more distinct pitch classes are a chord, and anything the sieve cannot explain
// (noise, sweeps, clusters) is unpitched.
//
// tonalShift: how far to retune a lane sample so it sounds a MIDI note.
//  - Vox one-shots and chops, unpitched and low-confidence files play as recorded.
//  - The filename note counts only when it agrees with the measurement within 1 st.
//  - A shift past the cap (7 st, 12 for bass) folds by octaves: the file keeps its
//    register and the pitch class stays right. Bass keeps a full octave, so a
//    line's octave moves survive.

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace rolla
{

enum class ChordQuality : std::uint8_t
{
    None = 0,
    Major,
    Minor,
    Neutral // fifth, sus2, sus4: fits either mode
};

struct PitchInfo
{
    // MIDI pitch of the root (fractional: a file 30 cents sharp is 60.3). -1 when unpitched.
    float rootPitch = -1.f;
    // 0..1. Under kPitchConfMin the file is not retuned on the measurement alone.
    float conf = 0.f;
    bool isChord = false;
    // Bit i set: pitch class (root + i) sounds. Bit 0 is the root.
    std::uint16_t chordPcs = 0;
    ChordQuality quality = ChordQuality::None;

    bool pitched() const noexcept { return rootPitch >= 0.f; }
};

inline constexpr float kPitchConfMin = 0.5f;

namespace pitchprobe
{

constexpr double kPi = 3.14159265358979323846;

inline void fft (std::vector<std::complex<double>>& a) noexcept
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

inline double midiOf (double hz) noexcept
{
    return 69.0 + 12.0 * std::log2 (hz / 440.0);
}

inline int pcOf (double midi) noexcept
{
    const int r = (int) std::lround (midi);
    return ((r % 12) + 12) % 12;
}

// Circular pitch-class distance in semitones (0..6).
inline double pcDistance (double a, double b) noexcept
{
    double d = std::fmod (std::fabs (a - b), 12.0);
    return d > 6.0 ? 12.0 - d : d;
}

struct Peak
{
    double midi = 0.0;
    double energy = 0.0;
    bool used = false;
};

// Averaged magnitude spectrum peaks, minHz (30 Hz = B0 by default) to 5 kHz, of mono[0..n).
inline std::vector<Peak> spectrumPeaks (const std::vector<float>& mono, double rate, double minHz = 30.0)
{
    std::vector<Peak> peaks;
    const size_t N = rate > 50000.0 ? 32768 : 16384;
    const size_t hop = N / 2;
    // A file shorter than the FFT is windowed over its own length and zero-padded,
    // so a short stab is not weighted towards its decayed tail.
    const size_t L = std::max<size_t> (2, std::min (N, mono.size()));
    std::vector<double> mag (N / 2, 0.0);
    std::vector<std::complex<double>> buf (N);
    int frames = 0;
    for (size_t start = 0; frames < 4 && (frames == 0 || start + N <= mono.size()); start += hop)
    {
        for (size_t i = 0; i < N; ++i)
        {
            const double w = i < L ? 0.5 - 0.5 * std::cos (2.0 * kPi * (double) i / (double) (L - 1)) : 0.0;
            const float s = start + i < mono.size() ? mono[start + i] : 0.f;
            buf[i] = std::complex<double> ((double) s * w, 0.0);
        }
        fft (buf);
        for (size_t k = 0; k < N / 2; ++k)
            mag[k] += std::abs (buf[k]);
        ++frames;
    }
    double top = 0.0;
    for (double m : mag)
        top = std::max (top, m);
    if (top <= 1.0e-9)
        return peaks;
    const double binHz = rate / (double) N;
    const size_t lo = std::max<size_t> (2, (size_t) (std::max (30.0, minHz) / binHz));
    const size_t hi = std::min (N / 2 - 2, (size_t) (5000.0 / binHz));
    for (size_t k = lo; k <= hi; ++k)
    {
        const double m = mag[k];
        if (m < top * 0.02 || m < mag[k - 1] || m <= mag[k + 1])
            continue;
        // Parabolic interpolation on log magnitude.
        const double a = std::log (std::max (1.0e-12, mag[k - 1]));
        const double b = std::log (std::max (1.0e-12, m));
        const double c = std::log (std::max (1.0e-12, mag[k + 1]));
        const double den = a - 2.0 * b + c;
        const double off = std::fabs (den) > 1.0e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        Peak p;
        p.midi = midiOf (((double) k + off) * binHz);
        p.energy = (m / top) * (m / top);
        peaks.push_back (p);
    }
    return peaks;
}

// NSDF clarity and pitch near a guess (within +-6%). Returns clarity, sets midi.
inline double refineNsdf (const std::vector<float>& mono, double rate, double guessMidi, double& midi) noexcept
{
    midi = guessMidi;
    const double f = 440.0 * std::pow (2.0, (guessMidi - 69.0) / 12.0);
    const int lagLo = std::max (2, (int) std::floor (rate / (f * 1.06)));
    const int lagHi = (int) std::ceil (rate / (f / 1.06));
    // The window shrinks to fit short files (80-200 ms stabs), down to about two
    // periods at the longest lag. Only a file shorter than that gives no clarity.
    const int minW = 2 * lagHi + 2;
    const int fitW = (int) mono.size() - lagHi - 2;
    if (fitW < minW)
        return 0.0;
    const int W = std::min (std::max (minW, rate > 50000.0 ? 8192 : 4096), fitW);
    double best = -1.0, bestClarity = 0.0;
    int hops = 0;
    for (int start = 0; start + W + lagHi + 2 <= (int) mono.size() && hops < 6; start += W, ++hops)
    {
        std::vector<double> nsdf ((size_t) (lagHi + 2), 0.0);
        for (int tau = lagLo - 1; tau <= lagHi + 1; ++tau)
        {
            double r = 0.0, m = 0.0;
            for (int i = 0; i < W; ++i)
            {
                const double x = mono[(size_t) (start + i)];
                const double y = mono[(size_t) (start + i + tau)];
                r += x * y;
                m += x * x + y * y;
            }
            nsdf[(size_t) tau] = m > 1.0e-12 ? 2.0 * r / m : 0.0;
        }
        int at = lagLo;
        for (int tau = lagLo; tau <= lagHi; ++tau)
            if (nsdf[(size_t) tau] > nsdf[(size_t) at])
                at = tau;
        const double a = nsdf[(size_t) at - 1], b = nsdf[(size_t) at], c = nsdf[(size_t) at + 1];
        const double den = a - 2.0 * b + c;
        const double off = std::fabs (den) > 1.0e-12 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
        if (b > best)
        {
            best = b;
            bestClarity = b;
            midi = midiOf (rate / ((double) at + off));
        }
    }
    return std::clamp (bestClarity, 0.0, 1.0);
}

} // namespace pitchprobe

// Lowest root a lane can have. Sub rumble under this on a stab or pad is not
// read as the root (bass, vox and unknown lanes keep the full 30 Hz range).
inline constexpr double kPitchFloorHz = 30.0;
inline constexpr double kPitchFloorTonalHz = 80.0;

// minHz: the lane's floor, kPitchFloorTonalHz for stabs, hits, plucks and pads.
inline PitchInfo probePitch (const float* left, const float* right, int frames, double rate,
                             double minHz = kPitchFloorHz)
{
    using namespace pitchprobe;
    PitchInfo info;
    if (left == nullptr || frames < 256 || rate < 8000.0)
        return info;

    // After the attack: from the first audible frame + 30 ms, up to 1 s.
    int lead = 0;
    while (lead < frames && std::fabs (left[lead]) < 0.01f && (right == nullptr || std::fabs (right[lead]) < 0.01f))
        ++lead;
    if (lead >= frames)
        return info;
    int begin = lead + (int) (0.03 * rate);
    if (frames - begin < (int) (0.1 * rate))
        begin = lead;
    const int end = std::min (frames, begin + (int) rate);
    std::vector<float> mono ((size_t) std::max (0, end - begin));
    for (int i = begin; i < end; ++i)
        mono[(size_t) (i - begin)] = right != nullptr ? 0.5f * (left[i] + right[i]) : left[i];
    if (mono.size() < 256)
        return info;

    auto peaks = spectrumPeaks (mono, rate, minHz);
    if (peaks.empty())
        return info;
    double total = 0.0;
    for (const auto& p : peaks)
        total += p.energy;

    // Harmonic sieve: the lowest strong unexplained peak is a note.
    struct Note
    {
        double midi;
        double energy;
    };
    std::vector<Note> notes;
    double explained = 0.0;
    for (int round = 0; round < 6; ++round)
    {
        double strongest = 0.0;
        for (const auto& p : peaks)
            if (! p.used)
                strongest = std::max (strongest, p.energy);
        if (strongest < 0.015 || total - explained < total * 0.08)
            break;
        Peak* root = nullptr;
        for (auto& p : peaks)
            if (! p.used && p.energy >= strongest * 0.09 && (root == nullptr || p.midi < root->midi))
                root = &p;
        if (root == nullptr)
            break;
        Note note { root->midi, 0.0 };
        for (auto& p : peaks)
        {
            if (p.used)
                continue;
            const double ratio = std::pow (2.0, (p.midi - root->midi) / 12.0);
            const double h = std::round (ratio);
            if (h < 1.0 || h > 10.0)
                continue;
            if (std::fabs (p.midi - (root->midi + 12.0 * std::log2 (h))) < 0.35)
            {
                p.used = true;
                note.energy += p.energy;
            }
        }
        explained += note.energy;
        notes.push_back (note);
    }
    if (notes.empty() || explained < total * 0.5)
        return info;

    // Merge octave doublings: one entry per pitch class, the lowest kept.
    std::vector<Note> classes;
    for (const auto& n : notes)
    {
        bool merged = false;
        for (auto& c : classes)
            if (pcDistance (c.midi, n.midi) < 0.5)
            {
                c.energy += n.energy;
                c.midi = std::min (c.midi, n.midi);
                merged = true;
            }
        if (! merged)
            classes.push_back (n);
    }
    // A pitch class with under 4% of the explained energy is a partial, not a voice.
    classes.erase (std::remove_if (classes.begin(), classes.end(),
                                   [&] (const Note& c) { return c.energy < explained * 0.04; }),
                   classes.end());
    if (classes.empty() || classes.size() > 5)
        return info;
    const double fit = explained / total;

    if (classes.size() == 1)
    {
        double refined = classes[0].midi;
        const double clarity = refineNsdf (mono, rate, classes[0].midi, refined);
        const bool agree = clarity > 0.5 && std::fabs (refined - classes[0].midi) < 0.6;
        info.rootPitch = (float) (agree ? refined : classes[0].midi);
        info.conf = (float) std::clamp (fit * (agree ? std::max (0.7, clarity) : 0.6), 0.0, 1.0);
        info.chordPcs = 1;
        return info;
    }

    // Chord: find the root that gives a known triad.
    std::sort (classes.begin(), classes.end(), [] (const Note& a, const Note& b) { return a.midi < b.midi; });
    auto maskFor = [&] (const Note& r)
    {
        std::uint16_t mask = 0;
        for (const auto& c : classes)
            mask |= (std::uint16_t) (1u << (pcOf (c.midi - r.midi + 120.0) % 12));
        return mask;
    };
    // A third decides the quality; no third with a fifth or sus tone is neutral.
    // Anything else (both thirds, a tritone cluster) has no root this can trust.
    auto qualityOf = [] (std::uint16_t m)
    {
        const auto has = [m] (int iv) { return (m & (1u << iv)) != 0; };
        if (has (6) && ! has (7))
            return ChordQuality::None;
        if (has (4) && ! has (3))
            return ChordQuality::Major;
        if (has (3) && ! has (4))
            return ChordQuality::Minor;
        if (! has (3) && ! has (4) && (has (7) || has (2) || has (5)))
            return ChordQuality::Neutral;
        return ChordQuality::None;
    };
    // Root: a third and a fifth beat a third alone, which beats no third (so an
    // inversion like Eb-G-C still reads as C minor). Ties go to the lowest note.
    auto scoreOf = [&] (std::uint16_t m)
    {
        const ChordQuality cq = qualityOf (m);
        if (cq == ChordQuality::None)
            return 0;
        if (cq == ChordQuality::Neutral)
            return 1;
        return (m & (1u << 7)) != 0 ? 3 : 2;
    };
    const Note* rootNote = &classes[0];
    int bestScore = -1;
    for (const auto& c : classes)
    {
        const int s = scoreOf (maskFor (c));
        if (s > bestScore)
        {
            bestScore = s;
            rootNote = &c;
        }
    }
    const ChordQuality q = qualityOf (maskFor (*rootNote));
    info.isChord = true;
    info.quality = q;
    info.rootPitch = (float) rootNote->midi;
    info.chordPcs = maskFor (*rootNote);
    info.conf = (float) std::clamp (fit * (q == ChordQuality::None ? 0.4 : 0.85), 0.0, 1.0);
    return info;
}

struct ShiftRule
{
    bool retune = true;    // false: play as recorded (vox one-shots and chops)
    double cap = 7.0;      // largest shift before folding by an octave
};

// Semitones to retune so the sample sounds midiNote. 0 plays it as recorded.
// namedRoot: the note in the filename, -1 when none.
inline double tonalShift (const PitchInfo& m, int namedRoot, double midiNote, const ShiftRule& rule) noexcept
{
    if (! rule.retune || ! m.pitched())
        return 0.0;
    double root = -1.0;
    const bool named = namedRoot >= 0 && pitchprobe::pcDistance ((double) namedRoot, (double) m.rootPitch) <= 1.0;
    if (m.conf >= kPitchConfMin)
        root = m.rootPitch;
    else if (named)
        // A weak measurement the label agrees with: the label's pitch class, the measured octave.
        root = (double) namedRoot + 12.0 * std::round (((double) m.rootPitch - (double) namedRoot) / 12.0);
    else
        return 0.0;
    double semis = midiNote - root;
    while (semis > rule.cap)
        semis -= 12.0;
    while (semis < -rule.cap)
        semis += 12.0;
    return semis;
}

// Where a chord file goes when its quality clashes with the chord asked for:
// a minor file on a major chord plays from the third (Cmaj -> Em = Cmaj7 upper),
// a major file on a minor chord from the minor third (Cm -> Eb = Cm7 upper).
inline int chordFileOffset (ChordQuality file, ChordQuality want) noexcept
{
    if (file == ChordQuality::Minor && want == ChordQuality::Major)
        return 4;
    if (file == ChordQuality::Major && want == ChordQuality::Minor)
        return 3;
    return 0;
}

// What a chord of played notes asks of a recorded chord: its quality, and where
// its root is relative to the first note (voicings come in any inversion).
struct ChordAsk
{
    ChordQuality quality = ChordQuality::None;
    int rootDelta = 0;
};

// Root search as in probePitch: a third and a fifth beat a third alone, which
// beats no third. Ties go to the first note.
inline ChordAsk chordAskOf (const int* iv, int count) noexcept
{
    ChordAsk ask;
    if (iv == nullptr || count < 2)
        return ask;
    int bestScore = -1;
    for (int r = 0; r < count; ++r)
    {
        bool has[12] {};
        for (int c = 0; c < count; ++c)
            has[(((iv[c] - iv[r]) % 12) + 12) % 12] = true;
        ChordQuality q = ChordQuality::Neutral;
        if (has[4] && ! has[3])
            q = ChordQuality::Major;
        else if (has[3] && ! has[4])
            q = ChordQuality::Minor;
        const int score = q == ChordQuality::Neutral ? 1 : (has[7] ? 3 : 2);
        if (score > bestScore)
        {
            bestScore = score;
            ask.quality = q;
            ask.rootDelta = (((iv[r] - iv[0]) % 12) + 12) % 12;
        }
    }
    return ask;
}

} // namespace rolla
