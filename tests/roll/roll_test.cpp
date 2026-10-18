#include "PluginProcessor.h"
#include "Generator/MidiFile.h"

#include <juce_events/juce_events.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <set>
#include <string>
#include <vector>

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
    juce::ScopedJuceInitialiser_GUI juce;

    testEarReview();
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
