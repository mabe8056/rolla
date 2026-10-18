#include "PluginProcessor.h"
#include "../common/ScratchProfile.h"
#include "Generator/MidiFile.h"
#include "Generator/Generator.h"
#include "Generator/PhraseBank.h"

#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#if defined (_MSC_VER)
#include <intrin.h>
#endif

// JUCE builds with modal loops off, so MessageManager::runDispatchLoopUntil is not in the
// library. The roll and shuffle handlers post to the message queue; this drains it.
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
}

namespace
{

int fails = 0;

void expect (bool ok, const std::string& msg)
{
    if (ok)
        return;
    std::cerr << "FAIL " << msg << "\n";
    ++fails;
}

std::uint32_t seedOf (const rolla::Groove& groove, int part)
{
    return part == rolla::Drums ? groove.drums.seed : groove.lane[part].seed;
}

bool flag (rolla::RollaProcessor& proc, const char* id)
{
    if (auto* value = proc.apvts.getRawParameterValue (id))
        return value->load() >= 0.5f;
    return false;
}

void setFlag (rolla::RollaProcessor& proc, const char* id, bool on)
{
    auto* param = proc.apvts.getParameter (id);
    expect (param != nullptr, std::string ("missing parameter ") + id);
    if (param == nullptr)
        return;
    param->setValueNotifyingHost (on ? 1.f : 0.f);
}

bool pumpUntil (const std::function<bool()>& done)
{
    const auto start = juce::Time::getMillisecondCounter();
    while (! done() && juce::Time::getMillisecondCounter() - start < 5000)
    {
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (1);
    }
    return done();
}

void snapshot (rolla::RollaProcessor& proc, std::uint32_t out[rolla::NumParts])
{
    const auto groove = proc.copyGroove();
    for (int i = 0; i < rolla::NumParts; ++i)
        out[i] = seedOf (groove, i);
}

// Roll again if a fresh seed collided with the one already on the lane.
void rollUntil (rolla::RollaProcessor& proc, int part, std::uint32_t before)
{
    for (int attempt = 0; attempt < 3 && seedOf (proc.copyGroove(), part) == before; ++attempt)
    {
        proc.triggerRoll();
        pumpUntil ([&] { return seedOf (proc.copyGroove(), part) != before; });
    }
}

void shuffleUntil (rolla::RollaProcessor& proc, int part, std::uint32_t before)
{
    for (int attempt = 0; attempt < 3 && seedOf (proc.copyGroove(), part) == before; ++attempt)
    {
        proc.triggerShuffle (part);
        pumpUntil ([&] { return seedOf (proc.copyGroove(), part) != before; });
    }
}

void testRollSkipsMutedAndKept()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();

    expect (flag (proc, "hitsMute"), "hits starts muted");
    expect (! flag (proc, "bassMute"), "bass starts unmuted");
    expect (! flag (proc, "stabKeep"), "stab starts unkept");

    setFlag (proc, "bassMute", true);
    setFlag (proc, "stabKeep", true);
    expect (! proc.laneEdited (rolla::Stab), "manual keep is not an auto-keep");

    std::uint32_t before[rolla::NumParts] {};
    snapshot (proc, before);
    rollUntil (proc, rolla::Pluck, before[rolla::Pluck]);

    const auto after = proc.copyGroove();
    expect (seedOf (after, rolla::Bass) == before[rolla::Bass], "roll leaves a muted lane");
    expect (seedOf (after, rolla::Stab) == before[rolla::Stab], "roll leaves a kept lane");
    expect (seedOf (after, rolla::Hits) == before[rolla::Hits], "roll leaves the default-muted hits lane");
    expect (seedOf (after, rolla::Pluck) != before[rolla::Pluck], "roll rewrites pluck");
    expect (seedOf (after, rolla::Pad) != before[rolla::Pad], "roll rewrites pad");
    expect (seedOf (after, rolla::Vox) != before[rolla::Vox], "roll rewrites vox");
    expect (seedOf (after, rolla::Drums) != before[rolla::Drums], "roll rewrites drums");
    expect (flag (proc, "bassMute") && flag (proc, "hitsMute"), "roll does not clear mute");
    expect (flag (proc, "stabKeep"), "roll does not clear a manual keep");
}

void testShuffleClearsAutoKeep()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();

    proc.markEdited (rolla::Pluck);
    setFlag (proc, "pluckMute", true);
    expect (proc.laneEdited (rolla::Pluck), "an edit marks the lane");
    expect (flag (proc, "pluckKeep"), "an edit turns keep on");

    const auto before = seedOf (proc.copyGroove(), rolla::Pluck);
    std::uint32_t others[rolla::NumParts] {};
    snapshot (proc, others);
    shuffleUntil (proc, rolla::Pluck, before);

    expect (seedOf (proc.copyGroove(), rolla::Pluck) != before, "shuffle re-rolls a muted auto-kept lane");
    expect (! proc.laneEdited (rolla::Pluck), "shuffle clears the auto-keep flag");
    expect (! flag (proc, "pluckKeep"), "shuffle turns the automatic keep off");
    expect (flag (proc, "pluckMute"), "shuffle leaves mute on");
    const auto after = proc.copyGroove();
    for (int i = 0; i < rolla::NumParts; ++i)
        if (i != rolla::Pluck)
            expect (seedOf (after, i) == others[i], std::string ("shuffle stays on ") + rolla::partId (i));
}

void setDenorm (rolla::RollaProcessor& proc, const char* id, float denorm)
{
    auto* param = proc.apvts.getParameter (id);
    expect (param != nullptr, std::string ("missing parameter ") + id);
    if (param == nullptr)
        return;
    param->setValueNotifyingHost (param->convertTo0to1 (denorm));
}

std::uint64_t noteSig (const rolla::Groove& groove, int part)
{
    const auto& lane = groove.lane[part];
    std::uint64_t h = (std::uint64_t) lane.count;
    for (int i = 0; i < lane.count; ++i)
    {
        h = h * 1315423911ull + (std::uint64_t) (lane.notes[i].start * 100.f);
        h = h * 1315423911ull + lane.notes[i].vel;
        h = h * 1315423911ull + (std::uint8_t) lane.notes[i].chord[0];
    }
    return h;
}

void testIntensityChangesOutput()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();

    const auto at75 = proc.copyGroove();
    expect (at75.lane[rolla::Pluck].count == 0, "default intensity leaves pluck out");
    expect (at75.lane[rolla::Bass].count > 0, "bass stays in at the default");
    expect (at75.lane[rolla::Stab].count > 0, "the stab knob keeps stab in");
    const auto bass75 = noteSig (at75, rolla::Bass);
    const auto seeds = at75;

    setDenorm (proc, "intensity", 100.f);
    expect (pumpUntil ([&]
    {
        return proc.copyGroove().lane[rolla::Pluck].count == 0
            && noteSig (proc.copyGroove(), rolla::Bass) == bass75;
    }), "roller stays without pluck at full intensity");
    const auto full = proc.copyGroove();
    for (int i = 0; i < rolla::NumParts; ++i)
        expect (seedOf (full, i) == seedOf (seeds, i), std::string ("intensity keeps the ") + rolla::partId (i) + " seed");

    setDenorm (proc, "intensity", 75.f);
    expect (pumpUntil ([&]
    {
        const auto g = proc.copyGroove();
        return g.lane[rolla::Pluck].count == 0 && noteSig (g, rolla::Bass) == bass75;
    }), "intensity 75 restores the same bass and drops pluck");

    setDenorm (proc, "stab", 0.f);
    setDenorm (proc, "intensity", 0.f);
    expect (pumpUntil ([&]
    {
        return proc.copyGroove().lane[rolla::Stab].count == 0;
    }), "intensity 0 with the stab knob down takes stab out");
    expect (seedOf (proc.copyGroove(), rolla::Stab) == seedOf (seeds, rolla::Stab), "an out lane keeps its seed");
    expect (noteSig (proc.copyGroove(), rolla::Bass) != bass75, "low intensity changes the bar-8 arrangement");

    setDenorm (proc, "stab", 0.8f);
    expect (pumpUntil ([&]
    {
        return proc.copyGroove().lane[rolla::Stab].count > 0;
    }), "turning the stab knob back up brings the lane in");
    expect (seedOf (proc.copyGroove(), rolla::Stab) == seedOf (seeds, rolla::Stab), "the knob does not re-roll stab");
}

int noteOnVelocityOne (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    std::vector<std::uint8_t> b ((std::istreambuf_iterator<char> (in)), {});
    if (b.size() < 14)
        return 99;
    const int tracks = (b[10] << 8) | b[11];
    std::size_t i = 14;
    int bad = 0;
    for (int t = 0; t < tracks; ++t)
    {
        if (i + 8 > b.size() || b[i] != 'M')
            return 99;
        const std::uint32_t len = ((std::uint32_t) b[i + 4] << 24) | ((std::uint32_t) b[i + 5] << 16)
                                 | ((std::uint32_t) b[i + 6] << 8) | b[i + 7];
        std::size_t p = i + 8;
        const std::size_t end = std::min (b.size(), p + len);
        std::uint8_t run = 0;
        while (p < end)
        {
            int guard = 0;
            while (p < end && guard++ < 5)
            {
                const std::uint8_t v = b[p++];
                if ((v & 0x80) == 0)
                    break;
            }
            if (p >= end)
                break;
            std::uint8_t st = b[p];
            if (st >= 0x80)
            {
                run = st;
                ++p;
            }
            else
                st = run;
            if (st == 0xff)
            {
                if (p >= end)
                    break;
                ++p;
                std::uint32_t n = 0;
                int g2 = 0;
                while (p < end && g2++ < 5)
                {
                    const std::uint8_t v = b[p++];
                    n = (n << 7) | (v & 0x7f);
                    if ((v & 0x80) == 0)
                        break;
                }
                p += n;
            }
            else
            {
                const int need = ((st & 0xf0) == 0xc0 || (st & 0xf0) == 0xd0) ? 1 : 2;
                if ((st & 0xf0) == 0x90 && need == 2 && p + 1 < end && b[p + 1] == 1 && b[p] != 0)
                    ++bad;
                p += (std::size_t) need;
            }
        }
        i = end;
    }
    return bad;
}

int pcOfNote (int semi)
{
    int pc = semi % 12;
    if (pc < 0)
        pc += 12;
    return pc;
}

void testEarReview()
{
    const std::uint32_t base = 2517220185u;
    std::cout << "ear seeds from " << base << "\n";
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    int flavour[4] {};
    int darkN = 0;
    double darkNpb = 0;
    double brightNpb = 0;
    int brightN = 0;
    int pulseN = 0;
    double popOct = 0;
    int popN = 0;
    std::set<std::string> jackinRhythms;
    double jackinLen = 0;
    int jackinStabs = 0;

    for (int style = 0; style < 4; ++style)
    {
        int reaction = 0;
        int stackedBass = 0;
        int stackedStab = 0;
        int pluck = 0;
        for (int roll = 0; roll < 20; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts] {};
            for (int p = 0; p < rolla::NumParts; ++p)
                seeds[p] = base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u
                         + (std::uint32_t) style * 101u;
            rolla::Groove g {};
            rolla::generate (g, s, seeds, 0x7fu);

            std::set<int> bassAt;
            std::string rhythm;
            for (int i = 0; i < g.lane[rolla::Bass].count; ++i)
            {
                const int st = (int) std::lround (g.lane[rolla::Bass].notes[i].start);
                if (! bassAt.insert (st).second)
                    ++stackedBass;
                rhythm.push_back ((char) (st + 1));
            }
            std::set<int> stabAt;
            for (int i = 0; i < g.lane[rolla::Stab].count; ++i)
            {
                const int st = (int) std::lround (g.lane[rolla::Stab].notes[i].start);
                if (! stabAt.insert (st).second)
                    ++stackedStab;
                if (style == rolla::Gaskin)
                {
                    jackinLen += g.lane[rolla::Stab].notes[i].length;
                    ++jackinStabs;
                }
            }
            for (int b = 0; b < g.bars; ++b)
            {
                bool s1 = false;
                bool s3 = false;
                for (int i = 0; i < g.lane[rolla::Bass].count; ++i)
                {
                    const int st = (int) std::lround (g.lane[rolla::Bass].notes[i].start);
                    if (st / 16 != b || pcOfNote (g.lane[rolla::Bass].notes[i].chord[0]) != 0)
                        continue;
                    if (st % 16 == 1)
                        s1 = true;
                    if (st % 16 == 3)
                        s3 = true;
                }
                if (s1 && s3)
                    ++reaction;
            }
            pluck += g.lane[rolla::Pluck].count;

            if (style == rolla::Gaskin)
            {
                jackinRhythms.insert (rhythm);
                const int flav = (int) (((seeds[rolla::Bass] ? seeds[rolla::Bass] : 1u) * 0x9E3779B9u) % 4u);
                if (flav >= 0 && flav < 4)
                    ++flavour[flav];
            }
            if (style == rolla::Long)
            {
                const float npb = g.bars > 0 ? (float) g.lane[rolla::Bass].count / (float) g.bars : 0.f;
                const bool dark = s.dark >= 0.75f || (s.dark > 0.25f && ((seeds[rolla::Bass] * 0x85EBCA6Bu) % 100u) < 40u);
                if (dark)
                {
                    ++darkN;
                    darkNpb += npb;
                }
                else
                {
                    ++brightN;
                    brightNpb += npb;
                }
            }
            if (style == rolla::Pop)
            {
                ++popN;
                int moves = 0;
                int oct = 0;
                const auto& lane = g.lane[rolla::Bass];
                for (int i = 1; i < lane.count; ++i)
                {
                    const int d = std::abs ((int) lane.notes[i].chord[0] - (int) lane.notes[i - 1].chord[0]);
                    ++moves;
                    if (d == 12 || d == 24)
                        ++oct;
                }
                if (moves > 0)
                    popOct += 100.0 * oct / moves;
                double lens = 0;
                for (int i = 0; i < lane.count; ++i)
                    lens += lane.notes[i].length;
                if (lane.count > 0 && lens / lane.count >= 1.8)
                    ++pulseN;
            }
            if (roll == 0)
            {
                const auto dir = std::filesystem::path ("/tmp/rolla_ear_midi") / names[style];
                std::filesystem::create_directories (dir);
                std::vector<std::string> written;
                std::string error;
                expect (rolla::MidiExport::writeAll (dir.string(), g, s.key, 0.5f, 130.0, written, error),
                        "midi export");
                int vel1 = 0;
                for (const auto& path : written)
                    vel1 += noteOnVelocityOne (path);
                expect (vel1 == 0, std::string (names[style]) + " export has no velocity-1 note");
            }
        }
        std::cout << names[style] << " stacked bass " << stackedBass << " stacked stab " << stackedStab
                  << " reaction bars " << reaction << " pluck notes " << pluck << "\n";
        expect (stackedBass == 0, std::string (names[style]) + " bass is monophonic");
        expect (stackedStab == 0, std::string (names[style]) + " stabs do not stack");
        expect (reaction == 0, std::string (names[style]) + " avoids the reaction figure");
        if (style == rolla::Long || style == rolla::Pop)
            expect (pluck > 0, std::string (names[style]) + " plays pluck");
        else
            expect (pluck == 0, std::string (names[style]) + " gates pluck");
    }
    std::cout << "JACKIN distinct 8-bar rhythms " << jackinRhythms.size() << "/20"
              << " flavours dotted " << flavour[0] << " b2 " << flavour[1]
              << " sustained " << flavour[2] << " offbeat " << flavour[3] << "\n";
    if (jackinStabs > 0)
        std::cout << "JACKIN stab length mean " << (jackinLen / jackinStabs) << "\n";
    if (darkN > 0)
        std::cout << "DEEP dark " << darkN << "/20 npb " << (darkNpb / darkN) << "\n";
    if (brightN > 0)
        std::cout << "DEEP bright " << brightN << "/20 npb " << (brightNpb / brightN) << "\n";
    if (popN > 0)
        std::cout << "BOUNCY octave moves " << (popOct / popN) << "%  pulse-length rolls " << pulseN << "/20\n";
    expect (popN > 0 && popOct / popN >= 15.0 && popOct / popN <= 30.0, "BOUNCY octaves land in 15-30%");
    expect (pulseN >= 3, "BOUNCY has a pulse mode");
    expect (jackinRhythms.size() >= 18, "JACKIN 8-bar rhythms are almost all distinct");
    expect (flavour[0] > 0 && flavour[1] > 0 && flavour[2] > 0 && flavour[3] > 0, "JACKIN uses all four flavours");
    expect (darkN >= 4 && darkN <= 16, "DEEP dark is about two fifths of the rolls");
    if (darkN > 0)
        expect (darkNpb / darkN >= 5.5, "DEEP dark bass is busier");
}

struct MidiHit
{
    std::uint32_t on = 0;
    std::uint32_t off = 0;
    int ch = 0;
    int pitch = 0;
    int vel = 0;
};

bool readMidi (const std::string& path, std::vector<MidiHit>& hits, int& duplicateOns, int& lowVel)
{
    duplicateOns = 0;
    lowVel = 0;
    hits.clear();
    std::ifstream in (path, std::ios::binary);
    std::vector<std::uint8_t> b ((std::istreambuf_iterator<char> (in)), {});
    if (b.size() < 14 || std::string (b.begin(), b.begin() + 4) != "MThd")
        return false;
    const int tracks = (b[10] << 8) | b[11];
    std::size_t i = 14;
    std::set<std::uint64_t> ons;
    bool open[16][128] {};
    std::uint32_t openAt[16][128] {};
    int openVel[16][128] {};
    for (int t = 0; t < tracks; ++t)
    {
        if (i + 8 > b.size())
            return false;
        const std::uint32_t len = ((std::uint32_t) b[i + 4] << 24) | ((std::uint32_t) b[i + 5] << 16)
                                 | ((std::uint32_t) b[i + 6] << 8) | b[i + 7];
        std::size_t p = i + 8;
        const std::size_t end = std::min (b.size(), p + len);
        std::uint32_t tick = 0;
        std::uint8_t run = 0;
        while (p < end)
        {
            std::uint32_t delta = 0;
            int guard = 0;
            while (p < end && guard++ < 5)
            {
                const std::uint8_t v = b[p++];
                delta = (delta << 7) | (v & 0x7f);
                if ((v & 0x80) == 0)
                    break;
            }
            tick += delta;
            if (p >= end)
                break;
            std::uint8_t st = b[p];
            if (st >= 0x80)
            {
                run = st;
                ++p;
            }
            else
                st = run;
            if (st == 0xff)
            {
                if (p >= end)
                    break;
                ++p;
                std::uint32_t n = 0;
                int g2 = 0;
                while (p < end && g2++ < 5)
                {
                    const std::uint8_t v = b[p++];
                    n = (n << 7) | (v & 0x7f);
                    if ((v & 0x80) == 0)
                        break;
                }
                if (p + n > end)
                    break;
                p += n;
                continue;
            }
            const int need = ((st & 0xf0) == 0xc0 || (st & 0xf0) == 0xd0) ? 1 : 2;
            if (p + (std::size_t) need > end)
                break;
            const int d1 = b[p];
            const int d2 = need == 2 ? b[p + 1] : 0;
            p += (std::size_t) need;
            const int kind = st & 0xf0;
            if (kind != 0x90 && kind != 0x80)
                continue;
            const int ch = st & 0x0f;
            const int pitch = std::clamp (d1, 0, 127);
            const bool noteOff = kind == 0x80 || d2 == 0;
            if (! noteOff)
            {
                if (d2 <= 2)
                    ++lowVel;
                const std::uint64_t key = (std::uint64_t) tick << 16 | (std::uint64_t) (ch << 8) | (std::uint64_t) pitch;
                if (! ons.insert (key).second)
                    ++duplicateOns;
                if (open[ch][pitch])
                {
                    hits.push_back ({ openAt[ch][pitch], tick, ch, pitch, openVel[ch][pitch] });
                }
                open[ch][pitch] = true;
                openAt[ch][pitch] = tick;
                openVel[ch][pitch] = d2;
            }
            else if (open[ch][pitch])
            {
                hits.push_back ({ openAt[ch][pitch], tick, ch, pitch, openVel[ch][pitch] });
                open[ch][pitch] = false;
            }
        }
        i = end;
    }
    return true;
}

int bassOverlapTicks (const std::vector<MidiHit>& hits)
{
    std::vector<MidiHit> bass;
    for (const auto& h : hits)
        if (h.ch != 9)
            bass.push_back (h);
    std::sort (bass.begin(), bass.end(), [] (const MidiHit& a, const MidiHit& b) { return a.on < b.on; });
    int worst = 0;
    for (std::size_t i = 0; i + 1 < bass.size(); ++i)
    {
        const int over = (int) bass[i].off - (int) bass[i + 1].on;
        if (over > worst)
            worst = over;
    }
    return worst;
}

bool bassSoundsAt (const rolla::Lane& bass, int slot)
{
    for (int i = 0; i < bass.count; ++i)
    {
        const int a = (int) std::lround (bass.notes[i].start);
        const int b = (int) std::lround (bass.notes[i].start + bass.notes[i].length);
        if (slot >= a && slot < b)
            return true;
    }
    return false;
}

int laneDuplicateOns (const rolla::Lane& lane)
{
    std::set<std::uint64_t> seen;
    int dups = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        const int nc = std::max (1, (int) lane.notes[i].nChord);
        for (int c = 0; c < nc && c < 4; ++c)
        {
            const std::uint64_t key = ((std::uint64_t) st << 16) ^ (std::uint64_t) (std::uint8_t) lane.notes[i].chord[c];
            if (! seen.insert (key).second)
                ++dups;
        }
    }
    return dups;
}

int bassHoldTicks (const rolla::Lane& lane)
{
    std::vector<std::pair<float, float>> notes;
    for (int i = 0; i < lane.count; ++i)
        notes.push_back ({ lane.notes[i].start, lane.notes[i].start + lane.notes[i].length });
    std::sort (notes.begin(), notes.end());
    int worst = -100000;
    for (std::size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const int over = (int) std::lround ((notes[i].second - notes[i + 1].first) * 120.f);
        if (over > worst)
            worst = over;
    }
    return notes.size() < 2 ? 0 : worst;
}

int popcount32 (std::uint32_t x)
{
#if defined (_MSC_VER)
    return (int) __popcnt (x);
#else
    return __builtin_popcount (x);
#endif
}

int popcount64 (std::uint64_t x)
{
#if defined (_MSC_VER)
    return (int) __popcnt64 (x);
#else
    return __builtin_popcountll (x);
#endif
}

float jaccard32 (std::uint32_t a, std::uint32_t b)
{
    const std::uint32_t uni = a | b;
    if (uni == 0)
        return 0.f;
    return (float) popcount32 (a & b) / (float) popcount32 (uni);
}

float jaccard64 (std::uint64_t alo, std::uint64_t ahi, std::uint64_t blo, std::uint64_t bhi)
{
    const int inter = popcount64 (alo & blo) + popcount64 (ahi & bhi);
    const int uni = popcount64 (alo | blo) + popcount64 (ahi | bhi);
    if (uni <= 0)
        return 0.f;
    return (float) inter / (float) uni;
}

// Scorer bar-4 answer: a token is (slot, pitch class). A match allows +-1 slot
// at the same pitch class. An answer changes at least one token and keeps
// at least a quarter of bar 2.
bool bar4Answers (const rolla::Lane& lane)
{
    struct Tok { int slot; int pc; };
    Tok b2[24];
    Tok b4[24];
    int n2 = 0;
    int n4 = 0;
    for (int i = 0; i < lane.count; ++i)
    {
        const int st = (int) std::lround (lane.notes[i].start);
        if (st < 0)
            continue;
        const int bar = st / 16;
        Tok t { st % 16, pcOfNote (lane.notes[i].chord[0]) };
        if (bar == 1 && n2 < 24)
            b2[n2++] = t;
        if (bar == 3 && n4 < 24)
            b4[n4++] = t;
    }
    if (n2 <= 0 || n4 <= 0)
        return false;
    bool used[24] {};
    int matched = 0;
    for (int i = 0; i < n2; ++i)
    {
        for (int j = 0; j < n4; ++j)
        {
            if (used[j] || b4[j].pc != b2[i].pc || std::abs (b4[j].slot - b2[i].slot) > 1)
                continue;
            used[j] = true;
            ++matched;
            break;
        }
    }
    const int changed = (n2 - matched) + (n4 - matched);
    return changed >= 1 && matched * 4 >= n2;
}

void testScorerV23()
{
    const std::uint32_t base = 20261018u;
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    std::cout << "v2.3 fresh seeds from " << base << "\n";
    for (int style = 0; style < 4; ++style)
    {
        int answers = 0;
        int pulse = 0;
        int stabHits = 0;
        int stabGaps = 0;
        int turn = 0;
        int callOk = 0;
        std::vector<float> lengths;
        std::uint32_t b2s[20] {};
        std::uint64_t lo[20] {};
        std::uint64_t hi[20] {};
        int exportDups = 0;
        int exportLow = 0;
        int exportOverlap = 0;
        for (int roll = 0; roll < 20; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts] {};
            for (int p = 0; p < rolla::NumParts; ++p)
                seeds[p] = base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u
                         + (std::uint32_t) style * 101u;
            rolla::Groove g {};
            rolla::generate (g, s, seeds, 0x7fu);

            int dups = 0;
            int low = 0;
            for (int p = 0; p < rolla::Drums; ++p)
            {
                dups += laneDuplicateOns (g.lane[p]);
                for (int i = 0; i < g.lane[p].count; ++i)
                    if (g.lane[p].notes[i].vel <= 2)
                        ++low;
            }
            for (int v = 0; v < rolla::NumDrumVoices; ++v)
                for (int t = 0; t < g.bars * 16; ++t)
                    if (g.drums.vel[v][t] > 0 && g.drums.vel[v][t] <= 2)
                        ++low;
            const int hold = bassHoldTicks (g.lane[rolla::Bass]);
            expect (dups == 0, std::string (names[style]) + " roll " + std::to_string (roll) + " has no duplicate note-ons");
            expect (low == 0, std::string (names[style]) + " roll " + std::to_string (roll) + " drops velocity <= 2");
            expect (hold <= 3, std::string (names[style]) + " roll " + std::to_string (roll) + " bass overlap " + std::to_string (hold) + " ticks");

            const auto& bass = g.lane[rolla::Bass];
            if (bar4Answers (bass))
                ++answers;
            for (int i = 0; i < bass.count; ++i)
            {
                const int st = (int) std::lround (bass.notes[i].start);
                if (st < 0 || st >= 128)
                    continue;
                lengths.push_back (bass.notes[i].length);
                if (st < 64)
                    lo[roll] |= 1ull << st;
                else
                    hi[roll] |= 1ull << (st - 64);
                if (st < 32)
                    b2s[roll] |= 1u << st;
            }

            int sounding[128];
            for (int s = 0; s < 128; ++s)
                sounding[s] = -2;
            for (int i = 0; i < bass.count; ++i)
            {
                const int a = (int) std::lround (bass.notes[i].start);
                const int b = (int) std::lround (bass.notes[i].start + bass.notes[i].length);
                for (int s = a; s < b && s < 128; ++s)
                    if (s >= 0)
                        sounding[s] = pcOfNote (bass.notes[i].chord[0]);
            }
            if (style == rolla::Pop)
            {
                int rest = 0;
                int bestRest = 0;
                for (int s = 112; s < 128; ++s)
                {
                    rest = sounding[s] < 0 ? rest + 1 : 0;
                    if (rest > bestRest)
                        bestRest = rest;
                }
                bool same = true;
                for (int s = 0; s < 16; ++s)
                    if (sounding[48 + s] != sounding[112 + s])
                        same = false;
                if (bestRest >= 4 && ! same)
                    ++turn;
                double lens = 0;
                for (int i = 0; i < bass.count; ++i)
                    lens += bass.notes[i].length;
                if (bass.count > 0 && lens / bass.count >= 1.8)
                    ++pulse;
            }
            if (style == rolla::Gaskin)
            {
                int match = 0;
                const int slots = 112;
                for (int s = 0; s < slots; ++s)
                    if (sounding[s] == sounding[s + 16])
                        ++match;
                if (match * 5 <= slots)
                    ++callOk;
            }
            for (int i = 0; i < g.lane[rolla::Stab].count; ++i)
            {
                ++stabHits;
                const int st = (int) std::lround (g.lane[rolla::Stab].notes[i].start);
                if (! bassSoundsAt (g.lane[rolla::Bass], st))
                    ++stabGaps;
            }

            const auto dir = std::filesystem::path ("/tmp/rolla_v23") / names[style] / std::to_string (roll);
            std::filesystem::create_directories (dir);
            std::vector<std::string> written;
            std::string error;
            expect (rolla::MidiExport::writeAll (dir.string(), g, s.key, 0.57f, 130.0, written, error),
                    "v2.3 midi export");
            for (const auto& path : written)
            {
                std::vector<MidiHit> hits;
                int fileDups = 0;
                int fileLow = 0;
                expect (readMidi (path, hits, fileDups, fileLow), "readable midi " + path);
                exportDups += fileDups;
                exportLow += fileLow;
                if (path.size() >= 9 && path.rfind ("_bass.mid") == path.size() - 9)
                {
                    const int over = bassOverlapTicks (hits);
                    if (over > 3)
                        ++exportOverlap;
                }
            }
        }
        int pairs = 0;
        for (int i = 0; i < 20; ++i)
            for (int j = i + 1; j < 20; ++j)
                if (jaccard32 (b2s[i], b2s[j]) >= 0.80f || jaccard64 (lo[i], hi[i], lo[j], hi[j]) >= 0.80f)
                    ++pairs;
        const double gapPct = stabHits > 0 ? 100.0 * stabGaps / stabHits : 0.0;
        std::sort (lengths.begin(), lengths.end());
        const float medLen = lengths.empty() ? 0.f : lengths[lengths.size() / 2];
        std::cout << names[style]
                  << " bar4-answers " << answers << "/20"
                  << " twin-pairs " << pairs
                  << " stab-in-gap " << gapPct << "%"
                  << " med-len " << medLen;
        if (style == rolla::Pop)
            std::cout << " pulse " << pulse << "/20 turn " << turn << "/20";
        if (style == rolla::Gaskin)
            std::cout << " call-answer " << callOk << "/20";
        std::cout << "\n";
        expect (answers >= 8 && answers <= 18, std::string (names[style]) + " bar 4 answers bar 2");
        expect (pairs <= 2, std::string (names[style]) + " near-identical rhythm pairs <= 2");
        if (style == rolla::Gaskin)
            expect (gapPct >= 12.0 && gapPct <= 45.0, "JACKIN stabs land in bass gaps");
        else if (style == rolla::UkMin)
            expect (gapPct >= 10.0 && gapPct <= 28.0, "ROLLER stabs in bass gaps stay between the floor and the bonus");
        else
            expect (gapPct < 50.0, std::string (names[style]) + " stabs-in-gaps stays off the 50% bonus");
        expect (exportDups == 0, std::string (names[style]) + " midi has no duplicate note-ons");
        expect (exportLow == 0, std::string (names[style]) + " midi drops velocity <= 2");
        expect (exportOverlap == 0, std::string (names[style]) + " midi bass ends before the next note");
        if (style == rolla::UkMin)
            expect (medLen >= 1.2f, "ROLLER bass mixes in 2-3/16 notes");
        if (style == rolla::Pop)
        {
            expect (pulse >= 6 && pulse <= 14, "BOUNCY pulse mode is about half the rolls");
            expect (turn >= 14, "BOUNCY bar 8 turns around");
        }
        if (style == rolla::Gaskin)
            expect (callOk >= 12, "JACKIN call and answer");
    }
}

// 4 styles x 50 seeds: no instant where the bass and a stab/pad tone are a
// semitone apart (m2 / m9 / M7), the bass b2 share of bass sounding time stays
// under the style cap (ROLLER 12%, DEEP 6%, BOUNCY 3%, JACKIN 6%; phrygian
// rolls 30% with no natural 2 in stab/pad), the major 6 only shows up in
// dorian rolls, and a dorian stab never carries the b6.
void testHarmonyClashes()
{
    const std::uint32_t base = 1u;
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    const double styleCap[] = { 0.12, 0.06, 0.03, 0.06 };
    for (int style = 0; style < 4; ++style)
    {
        int natural2 = 0;
        int clashes = 0;
        int b2Over = 0;
        int sixthBad = 0;
        int phrygianRolls = 0;
        for (int roll = 0; roll < 50; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts] {};
            for (int p = 0; p < rolla::NumParts; ++p)
                seeds[p] = base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u
                         + (std::uint32_t) style * 101u;
            rolla::Groove g {};
            rolla::generate (g, s, seeds, 0x7fu);
            const int mode = rolla::rollMode (s, seeds[rolla::Bass]);
            if (mode == rolla::phrase::Phrygian)
                ++phrygianRolls;

            auto bass = g.lane[rolla::Bass];
            std::sort (bass.notes, bass.notes + bass.count,
                       [] (const rolla::Note& a, const rolla::Note& b) { return a.start < b.start; });
            double total = 0;
            double b2 = 0;
            for (int i = 0; i < bass.count; ++i)
            {
                double len = bass.notes[i].length;
                if (i + 1 < bass.count)
                    len = std::min (len, (double) (bass.notes[i + 1].start - bass.notes[i].start));
                len = std::max (0.0, len);
                total += len;
                const int pc = pcOfNote (bass.notes[i].chord[0]);
                if (pc == 1)
                    b2 += len;
                if (pc == 9 && mode != rolla::phrase::Dorian)
                    ++sixthBad;
            }
            const double cap = mode == rolla::phrase::Phrygian ? 0.30 : styleCap[style];
            if (total > 0 && b2 / total > cap + 1e-5)
                ++b2Over;
            if (mode == rolla::phrase::Phrygian)
                for (int part : { (int) rolla::Stab, (int) rolla::Pad })
                    for (int k = 0; k < g.lane[part].count; ++k)
                        for (int c = 0; c < std::max (1, (int) g.lane[part].notes[k].nChord) && c < 4; ++c)
                            if (pcOfNote (g.lane[part].notes[k].chord[c]) == 2)
                                ++natural2;
            if (mode == rolla::phrase::Dorian)
            {
                const auto& stab = g.lane[rolla::Stab];
                for (int k = 0; k < stab.count; ++k)
                    for (int c = 0; c < std::max (1, (int) stab.notes[k].nChord) && c < 4; ++c)
                        if (pcOfNote (stab.notes[k].chord[c]) == 8)
                            ++sixthBad;
            }

            // Sample every quarter-16th midpoint across the loop.
            for (int q = 0; q < g.bars * 64; ++q)
            {
                const float t = (float) q * 0.25f + 0.125f;
                int bpc = -1;
                for (int i = 0; i < bass.count; ++i)
                    if (bass.notes[i].vel > 2 && t >= bass.notes[i].start && t < bass.notes[i].start + bass.notes[i].length)
                        bpc = pcOfNote (bass.notes[i].chord[0]);
                if (bpc < 0)
                    continue;
                bool clash = false;
                for (int part : { (int) rolla::Stab, (int) rolla::Pad })
                {
                    const auto& l = g.lane[part];
                    for (int k = 0; k < l.count && ! clash; ++k)
                    {
                        const auto& n = l.notes[k];
                        if (n.vel <= 2 || t < n.start || t >= n.start + n.length)
                            continue;
                        for (int c = 0; c < std::max (1, (int) n.nChord) && c < 4; ++c)
                        {
                            const int d = (pcOfNote (n.chord[c]) - bpc + 12) % 12;
                            if (d == 1 || d == 11)
                                clash = true;
                        }
                    }
                }
                if (clash)
                {
                    ++clashes;
                    break;
                }
            }
        }
        std::cout << names[style] << " harmony: clash rolls " << clashes << "/50 b2-over " << b2Over
                  << " sixth-bad " << sixthBad << " phrygian rolls " << phrygianRolls
                  << " phrygian natural-2 " << natural2 << "\n";
        expect (clashes == 0, std::string (names[style]) + " bass never sits a semitone from stab/pad");
        expect (b2Over == 0, std::string (names[style]) + " b2 under the style cap (phrygian 30%)");
        expect (natural2 == 0, std::string (names[style]) + " phrygian stab/pad carry no natural 2");
        expect (sixthBad == 0, std::string (names[style]) + " major 6 only in dorian, no b6 stab in dorian");
    }
}

// 4 styles x 50 seeds: no lane has two note-ons of one pitch on one 16th or
// two same-pitch notes sounding at once, no bar has more than 3 stab chord
// hits, and about 20-40% of rolls end with a bar-8 bass pickup.
void testLaneHygiene()
{
    const std::uint32_t base = 77u;
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    int pickups = 0;
    int rolls = 0;
    for (int style = 0; style < 4; ++style)
    {
        int dupOrOverlap = 0;
        int stabOver = 0;
        int stylePickups = 0;
        for (int roll = 0; roll < 50; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts] {};
            for (int p = 0; p < rolla::NumParts; ++p)
                seeds[p] = base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u
                         + (std::uint32_t) style * 101u;
            rolla::Groove g {};
            rolla::generate (g, s, seeds, 0x7fu);
            ++rolls;

            for (int p = 0; p < rolla::Drums; ++p)
            {
                const auto& l = g.lane[p];
                dupOrOverlap += laneDuplicateOns (l);
                for (int i = 0; i < l.count; ++i)
                    for (int j = i + 1; j < l.count; ++j)
                    {
                        const auto& a = l.notes[i];
                        const auto& b = l.notes[j];
                        const bool sameOn = std::lround (a.start) == std::lround (b.start);
                        const bool overlap = a.start < b.start + b.length - 1e-3f && b.start < a.start + a.length - 1e-3f;
                        if (sameOn || ! overlap)
                            continue; // same-onset repeats are counted by laneDuplicateOns
                        bool shared = false;
                        for (int c = 0; c < std::max (1, (int) a.nChord) && c < 4; ++c)
                            for (int d = 0; d < std::max (1, (int) b.nChord) && d < 4; ++d)
                                if (a.chord[c] == b.chord[d])
                                    shared = true;
                        if (shared)
                            ++dupOrOverlap;
                    }
            }

            const auto& stab = g.lane[rolla::Stab];
            for (int bar = 0; bar < g.bars; ++bar)
            {
                std::set<long> hits;
                for (int k = 0; k < stab.count; ++k)
                {
                    const long st = std::lround (stab.notes[k].start);
                    if (st / 16 == bar)
                        hits.insert (st);
                }
                if (hits.size() > 3)
                    ++stabOver;
            }

            if (rolla::rollWantsPickup (seeds[rolla::Bass]))
            {
                const auto& bass = g.lane[rolla::Bass];
                const int origin = (g.bars - 1) * 16;
                bool landed = false;
                for (int i = 0; i < bass.count; ++i)
                {
                    const int st = (int) std::lround (bass.notes[i].start);
                    if (st >= origin + 13 && st < origin + 16)
                        landed = true;
                }
                if (landed)
                    ++stylePickups;
            }
        }
        pickups += stylePickups;
        std::cout << names[style] << " hygiene: dup/overlap " << dupOrOverlap << " stab bars over 3 " << stabOver
                  << " pickups " << stylePickups << "/50\n";
        expect (dupOrOverlap == 0, std::string (names[style]) + " no duplicate or overlapping same-pitch notes in any lane");
        expect (stabOver == 0, std::string (names[style]) + " at most 3 stab hits per bar");
    }
    const double rate = rolls > 0 ? (double) pickups / rolls : 0.0;
    std::cout << "bar-8 pickups " << pickups << "/" << rolls << "\n";
    expect (rate >= 0.20 && rate <= 0.40, "about 20-40% of rolls have a bar-8 pickup");
}

void testShuffleKeepsManualLock()
{
    rolla::RollaProcessor proc;
    proc.waitUntilReady();

    setFlag (proc, "stabKeep", true);
    setFlag (proc, "stabMute", true);
    expect (! proc.laneEdited (rolla::Stab), "no edit, so the lock is manual");

    const auto before = seedOf (proc.copyGroove(), rolla::Stab);
    shuffleUntil (proc, rolla::Stab, before);

    expect (seedOf (proc.copyGroove(), rolla::Stab) != before, "shuffle re-rolls a muted manual keep");
    expect (flag (proc, "stabKeep"), "shuffle leaves a manual keep on");
    expect (! proc.laneEdited (rolla::Stab), "shuffle does not invent an auto-keep");
    expect (flag (proc, "stabMute"), "shuffle leaves the muted lane muted");
}

} // namespace

int main()
{
    // Never the real user profile: settings, caches and sample folders go to a scratch folder.
    if (rolla::test::useScratchProfile ("roll").empty())
    {
        std::cerr << "could not set up a scratch profile\n";
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI juce;

    testEarReview();
    testScorerV23();
    testHarmonyClashes();
    testLaneHygiene();
    testRollSkipsMutedAndKept();
    testShuffleClearsAutoKeep();
    testShuffleKeepsManualLock();
    testIntensityChangesOutput();

    if (fails == 0)
        std::cout << "roll tests passed\n";
    else
        std::cerr << fails << " roll test failure(s)\n";
    return fails == 0 ? 0 : 1;
}
