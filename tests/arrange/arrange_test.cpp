// rolla-arrange-test: bar-8 turnaround and 7+1 harmony helpers. No JUCE.

#include "Generator/Arrangement.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>

using namespace rolla;
using namespace rolla::arrange;

namespace
{
int failures = 0;

void check (bool ok, const char* what, const std::string& detail = {})
{
    std::printf ("%s %s%s%s\n", ok ? "PASS" : "FAIL", what, detail.empty() ? "" : "  ", detail.c_str());
    if (! ok)
        ++failures;
}

// A busy stand-in riff: a note every off-8th, kick four on the floor.
void fillRiff (Lane& bass, DrumLane& drums, int bars)
{
    bass.count = 0;
    clearDrums (drums);
    for (int s = 0; s < bars * 16; ++s)
    {
        if (s % 4 == 0)
            drums.vel[Kick][s] = 120;
        if (s % 4 == 2 && bass.count < kMaxNotes)
        {
            Note n;
            n.start = (float) s;
            n.length = 3.f; // rings into the next beat, and across bar lines
            n.chord[0] = 0;
            bass.notes[bass.count++] = n;
        }
    }
}

void testTurnaround (std::uint32_t base)
{
    int planned = 0, full = 0, phraseEnds = 0, bad = 0, lifted = 0;
    char why[200] = "";
    for (int i = 0; i < 1000; ++i)
    {
        const std::uint32_t seed = base + (std::uint32_t) i * 0x9E3779B9u;
        const int mode = i % NumModes;
        TurnaroundPlan p = planTurnaround (seed, 8);
        ++phraseEnds;
        planned += p.count;
        Lane bass;
        DrumLane drums;
        fillRiff (bass, drums, 8);
        Lane before = bass;
        applyTurnaround (p, bass, drums, 8, mode);
        for (int t = 0; t < p.count; ++t)
        {
            const int bar = p.bars[t];
            if (bar != 7)
            {
                ++bad;
                std::snprintf (why, sizeof (why), "seed %u: turnaround on bar %d", seed, bar + 1);
            }
            int kicks = 0;
            for (int s = bar * 16; s < bar * 16 + 16; ++s)
                kicks += drums.vel[Kick][s] > 0;
            if (p.type[t] == GapType::Full)
            {
                ++full;
                lifted += p.lift;
                int inBar = 0;
                float held = 0.f;
                bool inKey = true, ringing = false;
                for (int k = 0; k < bass.count; ++k)
                {
                    const Note& n = bass.notes[k];
                    if (n.start >= bar * 16 && n.start < bar * 16 + 16)
                    {
                        ++inBar;
                        held += n.length;
                        inKey = inKey && inMode (n.chord[0], mode);
                    }
                    else if (n.start < bar * 16 && n.start + n.length > bar * 16)
                        ringing = true;
                }
                if (kicks != 0 || inBar != 2 || held < 15.99f || ! inKey || ringing)
                {
                    ++bad;
                    std::snprintf (why, sizeof (why), "seed %u: full gap kicks %d notes %d held %.1f key %d ringing %d",
                                   seed, kicks, inBar, held, (int) inKey, (int) ringing);
                }
            }
            else
            {
                int lateKicks = 0;
                for (int s = bar * 16 + 8; s < bar * 16 + 16; ++s)
                    lateKicks += drums.vel[Kick][s] > 0;
                if (lateKicks != 0 || drums.vel[Kick][bar * 16] == 0)
                {
                    ++bad;
                    std::snprintf (why, sizeof (why), "seed %u: kick dip wrong", seed);
                }
            }
        }
        // Bars 1-7 untouched, apart from a note cut where it rang into bar 8.
        int changed = 0;
        for (int k = 0; k < before.count; ++k)
        {
            const Note& n = before.notes[k];
            if (n.start >= 7 * 16 - 3)
                continue;
            bool found = false;
            for (int j = 0; j < bass.count; ++j)
                if (bass.notes[j].start == n.start && bass.notes[j].length == n.length)
                    found = true;
            changed += ! found;
        }
        if (changed != 0)
        {
            ++bad;
            std::snprintf (why, sizeof (why), "seed %u: %d notes changed outside bar 8", seed, changed);
        }
    }
    const float share = (float) planned / (float) phraseEnds;
    char d[200];
    std::snprintf (d, sizeof (d), "%d bad %s", bad, why);
    check (bad == 0, "turnaround: only bar 8, kick/sub gap, two held in-key notes, rest untouched", d);
    std::snprintf (d, sizeof (d), "%.2f of phrase ends (chance 0.75), %.2f full, %.2f of full lifted an octave",
                   share, (float) full / std::max (1, planned), (float) lifted / std::max (1, full));
    check (share > 0.65f && share < 0.85f && full > 0 && full < planned, "turnaround stays a soft option", d);

    // Short loops: one candidate at the end, at a reduced chance.
    int shortPlanned = 0;
    for (int i = 0; i < 1000; ++i)
    {
        const TurnaroundPlan p = planTurnaround (base + (std::uint32_t) i, 4);
        shortPlanned += p.count;
        if (p.count == 1 && p.bars[0] != 3)
            ++bad;
    }
    std::snprintf (d, sizeof (d), "%.2f of 4-bar loops", shortPlanned / 1000.f);
    check (shortPlanned > 250 && shortPlanned < 500 && bad == 0, "turnaround: 4-bar loops get it less often, on their last bar", d);
}

void testHarmony (std::uint32_t base)
{
    int n = 0, bad = 0, thirds = 0, chords = 0, open = 0, midVar = 0;
    int voicingUse[16] {};
    char why[200] = "";
    for (int i = 0; i < 1200; ++i)
    {
        const std::uint32_t seed = base + (std::uint32_t) i * 0x2545F491u;
        const int mode = i % NumModes;
        const HarmonyLoop h = makeSevenPlusOne (seed, mode);
        ++n;
        bool sevenSame = true;
        for (int b = 1; b < 7; ++b)
            if (b != 3 && std::memcmp (h.bar[b].notes, h.bar[0].notes, sizeof (h.bar[0].notes)) != 0)
                sevenSame = false;
        const bool turnDiffers = std::memcmp (h.bar[7].notes, h.bar[0].notes, sizeof (h.bar[0].notes)) != 0;
        if (std::memcmp (h.bar[3].notes, h.bar[0].notes, sizeof (h.bar[0].notes)) != 0)
        {
            ++midVar;
            if (h.bar[3].root != h.bar[0].root || h.bar[3].statesThird)
                sevenSame = false;
        }
        bool inKey = true;
        for (int b = 0; b < 8; ++b)
        {
            const Chord& c = h.bar[b];
            if (b == 0 || b == 7)
            {
                ++chords;
                thirds += c.statesThird;
                ++voicingUse[c.voicing];
                int lo = 127, hi = -127;
                for (int k = 0; k < c.n; ++k)
                {
                    lo = std::min (lo, (int) c.notes[k]);
                    hi = std::max (hi, (int) c.notes[k]);
                }
                open += (hi - lo) >= 12;
            }
            for (int k = 0; k < c.n; ++k)
                inKey = inKey && inMode (c.notes[k], h.mode);
            if (c.n < 3)
                inKey = false;
        }
        if (! sevenSame || ! turnDiffers || ! inKey)
        {
            ++bad;
            std::snprintf (why, sizeof (why), "seed %u mode %d: seven %d turn %d key %d", seed, mode, (int) sevenSame,
                           (int) turnDiffers, (int) inKey);
        }
    }
    char d[200];
    std::snprintf (d, sizeof (d), "%d bad %s", bad, why);
    check (bad == 0, "harmony: 7 bars hold one chord, bar 8 turns, every note in mode", d);
    std::snprintf (d, sizeof (d), "%.2f of chords state the 3rd", (float) thirds / chords);
    check ((float) thirds / chords <= 0.15f, "harmony: the 3rd is rarely stated", d);
    std::snprintf (d, sizeof (d), "%.2f of chords span an octave or more", (float) open / chords);
    check ((float) open / chords >= 0.9f, "harmony: voicings are open", d);
    int used = 0;
    for (int v = 0; v < numVoicings(); ++v)
        used += voicingUse[v] > 0;
    std::snprintf (d, sizeof (d), "%d of %d shapes used, bar-4 colour in %.2f of loops", used, numVoicings(), (float) midVar / n);
    check (used >= numVoicings() - 1 && midVar > 0 && midVar < n / 2, "harmony: shapes vary across seeds", d);

    // Lane output: 8 bars held, last bar is the turn; a 4-bar loop still ends on it.
    const HarmonyLoop h = makeSevenPlusOne (base, Lydian);
    Lane lane;
    const int w8 = writeHarmony (h, lane, 8, ChordRhythm::Hold);
    const bool endsOnTurn8 = std::memcmp (lane.notes[7].chord, h.bar[7].notes, 4) == 0 && lane.notes[7].length == 16.f;
    const int w4 = writeHarmony (h, lane, 4, ChordRhythm::Offbeat);
    const bool endsOnTurn4 = std::memcmp (lane.notes[w4 - 1].chord, h.bar[7].notes, 4) == 0 && lane.notes[w4 - 1].start == 62.f;
    check (w8 == 8 && endsOnTurn8 && w4 == 16 && endsOnTurn4, "writeHarmony: held and offbeat lanes end on the turn chord");
}

void testBasics()
{
    check (degreeToSemis (3, Lydian) == 6 && degreeToSemis (-1, Mixolydian) == -2 && degreeToSemis (7, Ionian) == 12
               && degreeToSemis (-3, Aeolian) == -5 && degreeToSemis (1, Phrygian) == 1,
           "degree -> semitone mapping");
    const HarmonyLoop a = makeSevenPlusOne (99, Lydian);
    const HarmonyLoop b = makeSevenPlusOne (99, Lydian);
    const TurnaroundPlan p = planTurnaround (99, 8);
    const TurnaroundPlan q = planTurnaround (99, 8);
    check (std::memcmp (a.bar, b.bar, sizeof (a.bar)) == 0 && p.count == q.count && p.heldDegree[0] == q.heldDegree[0]
               && p.splitStep == q.splitStep,
           "deterministic for a seed");
}
} // namespace

int main()
{
    std::random_device rd;
    const std::uint32_t base = rd();
    std::printf ("seed base %u\n", base);
    testBasics();
    testTurnaround (1u);
    testTurnaround (base);
    testHarmony (1u);
    testHarmony (base);
    std::printf ("%s: %d failure(s)\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
