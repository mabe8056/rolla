#include "PluginProcessor.h"
#include "../common/ScratchProfile.h"
#include "Generator/MidiFile.h"
#include "Generator/Generator.h"
#include "Generator/Originality.h"
#include "Generator/OriginalityJson.h"
#include "Generator/Intensity.h"
#include "Generator/RuleComposer.h"
#include "Generator/Presets.h"

#include <BinaryData.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#if defined (_MSC_VER)
#include <intrin.h>
#endif

// The roll and shuffle handlers post to the message queue; pumpUntil drains it.
// Linux uses the internal hook. macOS and Windows use
// MessageManager::runDispatchLoopUntil, which needs JUCE_MODAL_LOOPS_PERMITTED=1
// (set for this target in CMakeLists.txt).
#if JUCE_LINUX
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
}
#endif

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
#if JUCE_LINUX
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (1);
#else
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
#endif
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

// BOUNCY pulse mode is a steady held grid (Composer: 5-7 notes a bar). The
// mode now ships on the groove (Groove::bassMode), so this only runs on pulse
// rolls: at least 5 of the 7 body bars carry 5-7 onsets.
bool popPulseGrid (const rolla::Lane& lane, int bars)
{
    int steady = 0;
    const int body = std::min (bars, 8) - 1;
    for (int b = 0; b < body; ++b)
    {
        int n = 0;
        for (int i = 0; i < lane.count; ++i)
            if ((int) std::lround (lane.notes[i].start) / 16 == b)
                ++n;
        if (n >= 5 && n <= 7)
            ++steady;
    }
    return body > 0 && steady * 7 >= body * 5;
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
        int rootTwoFour = 0;
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
                if (style == rolla::Jackin)
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
                    ++rootTwoFour;
            }
            pluck += g.lane[rolla::Pluck].count;

            if (style == rolla::Jackin)
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
                if (g.bassMode == rolla::BassModePulse)
                {
                    ++pulseN;
                    expect (popPulseGrid (lane, g.bars), "BOUNCY ear roll " + std::to_string (roll) + " pulse is a steady grid");
                }
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
                  << " root 2&4 bars " << rootTwoFour << " pluck notes " << pluck << "\n";
        expect (stackedBass == 0, std::string (names[style]) + " bass is monophonic");
        expect (stackedStab == 0, std::string (names[style]) + " stabs do not stack");
        expect (rootTwoFour == 0, std::string (names[style]) + " avoids the banned 2&4 root figure");
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
                if (g.bassMode == rolla::BassModePulse)
                {
                    ++pulse;
                    expect (popPulseGrid (bass, g.bars), "BOUNCY roll " + std::to_string (roll) + " pulse is a steady grid");
                }
            }
            if (style == rolla::Jackin)
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
        if (style == rolla::Jackin)
            std::cout << " call-answer " << callOk << "/20";
        std::cout << "\n";
        expect (answers >= 8 && answers <= 18, std::string (names[style]) + " bar 4 answers bar 2");
        expect (pairs <= 2, std::string (names[style]) + " near-identical rhythm pairs <= 2");
        if (style == rolla::Jackin)
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
            // Genre model v1.2: pulse is a 30-40% batch quota (6-8 of 20).
            expect (pulse >= 6 && pulse <= 8, "BOUNCY pulse mode is 30-40% of the batch (6-8 of 20), got " + std::to_string (pulse));
            expect (turn >= 14, "BOUNCY bar 8 turns around");
        }
        if (style == rolla::Jackin)
            expect (callOk >= 12, "JACKIN call and answer");
    }
}

// A&R msg 423 #1 and #2: bass on the kick near the reference medians (hard cap
// 35%), and bass held for most of the loop. 4 styles x 20 seeds.
void testAr423Bass()
{
    const std::uint32_t base = 423423u;
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    const double target[] = { 0.17, 0.07, 0.32, 0.19 };
    for (int style = 0; style < 4; ++style)
    {
        int onsets = 0;
        int onKick = 0;
        double sndSum = 0.0;
        double sndMin = 1.0;
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
            const auto& bass = g.lane[rolla::Bass];
            const double loop = (double) (g.bars * 16);
            int rollOn = 0;
            int rollKick = 0;
            double held = 0.0;
            for (int i = 0; i < bass.count; ++i)
            {
                const int st = (int) std::lround (bass.notes[i].start);
                ++rollOn;
                if (st % 4 == 0)
                    ++rollKick;
                held += bass.notes[i].length;
                expect (bass.notes[i].start + bass.notes[i].length <= (float) loop + 0.01f,
                        std::string (names[style]) + " roll " + std::to_string (roll) + " bass stays inside the loop");
            }
            onsets += rollOn;
            onKick += rollKick;
            const double snd = loop > 0.0 ? held / loop : 0.0;
            sndSum += snd;
            sndMin = std::min (sndMin, snd);
            expect (rollOn > 0 && (double) rollKick <= 0.35 * rollOn + 1.0e-6,
                    std::string (names[style]) + " roll " + std::to_string (roll) + " bass on kick <= 35%");
            // Held bass: ROLLER / JACKIN >= 0.72, DEEP 0.72-0.88, BOUNCY pulse
            // 0.85-0.975. BOUNCY bounce keeps short notes (0.29-0.50, no hold).
            double sndLo = 0.72;
            double sndHi = 1.0;
            if (style == rolla::Long)
                sndHi = 0.88;
            if (style == rolla::Pop)
            {
                const bool pulseRoll = g.bassMode == rolla::BassModePulse;
                sndLo = pulseRoll ? 0.85 : 0.29;
                sndHi = pulseRoll ? 0.975 : 0.50;
            }
            expect (snd >= sndLo - 1.0e-6 && snd <= sndHi + 1.0e-6,
                    std::string (names[style]) + " roll " + std::to_string (roll) + " bass sounding "
                        + std::to_string (snd) + " in " + std::to_string (sndLo) + "-" + std::to_string (sndHi));
            expect (bassHoldTicks (bass) <= 3,
                    std::string (names[style]) + " roll " + std::to_string (roll) + " held bass does not overlap");
        }
        const double share = onsets > 0 ? (double) onKick / onsets : 0.0;
        std::cout << names[style] << " A&R423 bass on kick " << (100.0 * share) << "% (target "
                  << (100.0 * target[style]) << "%) sounding mean " << (sndSum / 20.0) << " min " << sndMin << "\n";
        expect (std::abs (share - target[style]) <= 0.10,
                std::string (names[style]) + " bass on-kick share within 10 points of the reference");
        expect (share <= 0.35, std::string (names[style]) + " bass on-kick share <= 35%");
    }
}

std::string fileBytes (const std::string& path)
{
    std::ifstream in (path, std::ios::binary);
    return std::string ((std::istreambuf_iterator<char> (in)), {});
}

double medianOf (std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

// Composer gate after A&R 423: BOUNCY is BOUNCE or PULSE, pulse on a 30-40%
// batch quota (genre model v1.2) from the roll's drums seed. BOUNCE: sounding
// 0.29-0.50, octave moves 10-30%, onsets-per-bar sd >= 0.7, mean note length
// 0.8-1.6 sixteenths. PULSE: sounding 0.85-0.975, 5-7 notes a bar, octave
// moves <= 8%. DEEP held bass: median sounding >= 0.78, never over 0.88. The
// bass MIDI carries the mode.
void testBouncyModes()
{
    const std::uint32_t base = 0xB0C7u;
    int pulseN = 0;
    int bounceN = 0;
    std::vector<double> bounceOct;
    std::vector<double> deepSnd;
    for (int style : { (int) rolla::Pop, (int) rolla::Long })
    {
        for (int roll = 0; roll < 40; ++roll)
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
            const auto& bass = g.lane[rolla::Bass];
            const double loop = (double) (g.bars * 16);
            double held = 0.0;
            int bar[8] {};
            for (int i = 0; i < bass.count; ++i)
            {
                held += bass.notes[i].length;
                const int b = (int) std::lround (bass.notes[i].start) / 16;
                if (b >= 0 && b < 8)
                    ++bar[b];
            }
            const double snd = held / loop;
            const std::string tag = std::string (style == rolla::Pop ? "BOUNCY" : "DEEP") + " seed " + std::to_string (roll);

            if (roll < 4)
            {
                const auto dir = std::filesystem::path ("/tmp/rolla_bouncy_modes") / std::to_string (style) / std::to_string (roll);
                std::filesystem::create_directories (dir);
                std::vector<std::string> written;
                std::string error;
                expect (rolla::MidiExport::writeAll (dir.string(), g, s.key, 0.567f, 124.0, written, error), tag + " midi export");
                for (const auto& path : written)
                {
                    const bool bassFile = path.size() >= 9 && path.rfind ("_bass.mid") == path.size() - 9;
                    const bool allFile = path.size() >= 8 && path.rfind ("_all.mid") == path.size() - 8;
                    const auto bytes = fileBytes (path);
                    const bool hasMode = bytes.find ("mode:") != std::string::npos;
                    if (style == rolla::Pop && (bassFile || allFile))
                    {
                        const char* want = g.bassMode == rolla::BassModePulse ? "mode:pulse" : "mode:bounce";
                        expect (bytes.find (want) != std::string::npos, tag + " midi carries " + want + " in " + path);
                    }
                    else
                        expect (! hasMode, tag + " midi has no mode marker in " + path);
                }
            }

            if (style == rolla::Long)
            {
                deepSnd.push_back (snd);
                expect (snd <= 0.88 + 1.0e-6, tag + " sounding " + std::to_string (snd) + " <= 0.88");
                continue;
            }
            expect (g.bassMode == rolla::BassModeBounce || g.bassMode == rolla::BassModePulse, tag + " has a BOUNCY mode");
            int moves = 0;
            int oct = 0;
            for (int i = 1; i < bass.count; ++i)
            {
                const int d = std::abs ((int) bass.notes[i].chord[0] - (int) bass.notes[i - 1].chord[0]);
                ++moves;
                if (d == 12 || d == 24)
                    ++oct;
            }
            const double octPct = moves > 0 ? 100.0 * oct / moves : 0.0;
            if (g.bassMode == rolla::BassModePulse)
            {
                ++pulseN;
                const double npb = (double) bass.count / (double) g.bars;
                expect (snd >= 0.85 - 1.0e-6 && snd <= 0.975 + 1.0e-6, tag + " pulse sounding " + std::to_string (snd) + " in 0.85-0.975");
                // Composer spec for #70: pulse is 5-7 notes a bar (was 5-8).
                expect (npb >= 5.0 && npb <= 7.0, tag + " pulse notes per bar " + std::to_string (npb) + " in 5-7");
                // Composer spec for #70: pulse octave moves at most 8%.
                expect (octPct <= 8.0 + 1.0e-6, tag + " pulse octave moves " + std::to_string (octPct) + "% <= 8%");
                expect (popPulseGrid (bass, g.bars), tag + " pulse is a steady grid");
                continue;
            }
            ++bounceN;
            expect (snd >= 0.29 - 1.0e-6 && snd <= 0.50 + 1.0e-6, tag + " bounce sounding " + std::to_string (snd) + " in 0.29-0.50");
            // Composer spec for #70: bounce mean note length 0.8-1.6 sixteenths.
            const double meanLen = bass.count > 0 ? held / (double) bass.count : 0.0;
            expect (meanLen >= 0.8 - 1.0e-6 && meanLen <= 1.6 + 1.0e-6,
                    tag + " bounce mean note length " + std::to_string (meanLen) + " in 0.8-1.6 sixteenths");
            double mean = 0.0;
            for (int b = 0; b < 8; ++b)
                mean += bar[b];
            mean /= 8.0;
            double var = 0.0;
            for (int b = 0; b < 8; ++b)
                var += (bar[b] - mean) * (bar[b] - mean);
            const double sd = std::sqrt (var / 8.0);
            expect (sd >= 0.7 - 1.0e-6, tag + " bounce density spread " + std::to_string (sd) + " >= 0.7");
            if (moves > 0)
                bounceOct.push_back (octPct);
        }
    }
    const double share = 100.0 * pulseN / 40.0;
    const double octMed = medianOf (bounceOct);
    const double deepMed = medianOf (deepSnd);
    std::cout << "BOUNCY modes: pulse " << pulseN << "/40 (" << share << "%) bounce " << bounceN
              << "/40 octave-move median " << octMed << "%; DEEP sounding median " << deepMed << "\n";
    // Genre model v1.2: pulse is a 30-40% batch quota through the seed index.
    expect (share >= 30.0 && share <= 40.0, "BOUNCY pulse share lands in 30-40%");
    expect (octMed >= 10.0 && octMed <= 30.0, "BOUNCY bounce octave moves land in 10-30%");
    expect (deepMed >= 0.78, "DEEP held bass median sounding >= 0.78");
}

// A&R msg 423 #3: 16th swing defaults per style; the knob still moves freely.
void testAr423Swing()
{
    // Genre model v1.2: every style default inside the 53-57% hat band (ROLLER 52 -> 53.7).
    const float want[] = { 0.537f, 0.54f, 0.567f, 0.565f };
    auto defaults = std::make_unique<rolla::PresetBank>();
    rolla::loadDefaultPresets (*defaults);
    auto json = std::make_unique<rolla::PresetBank>();
    expect (rolla::loadPresetsFromJson (*json, BinaryData::presets_json, BinaryData::presets_jsonSize),
            "presets.json loads");
    for (int d = 0; d < rolla::NumDialects; ++d)
    {
        expect (std::abs (defaults->dialect[d].swing - want[d]) < 1.0e-4f,
                std::string (rolla::dialectName (d)) + " built-in swing default");
        expect (std::abs (json->dialect[d].swing - want[d]) < 1.0e-4f,
                std::string (rolla::dialectName (d)) + " presets.json swing default");
    }

    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    auto swingNow = [&] { return proc.apvts.getRawParameterValue ("swing")->load(); };
    for (int d : { (int) rolla::Long, (int) rolla::Pop, (int) rolla::Jackin, (int) rolla::UkMin })
    {
        setDenorm (proc, "dialect", (float) d);
        expect (pumpUntil ([&] { return std::abs (swingNow() - want[d]) < 0.0015f; }),
                std::string (rolla::dialectName (d)) + " style sets the swing knob to its default");
    }
    setDenorm (proc, "swing", 0.555f);
    expect (std::abs (swingNow() - 0.555f) < 0.0015f, "the swing knob still takes a user value");
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
            // The mode of the roll that shipped (an originality redraw draws it again).
            const int mode = g.mode;
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
                // Every upper lane (Composer gate): stab, pad, pluck, hits and vox.
                // Vox is written from MIDI 64, four semitones over the key root.
                for (int part : { (int) rolla::Stab, (int) rolla::Pad, (int) rolla::Pluck, (int) rolla::Hits, (int) rolla::Vox })
                {
                    const auto& l = g.lane[part];
                    const int bias = part == rolla::Vox ? 4 : 0;
                    for (int k = 0; k < l.count && ! clash; ++k)
                    {
                        const auto& n = l.notes[k];
                        if (n.vel <= 2 || t < n.start || t >= n.start + n.length)
                            continue;
                        for (int c = 0; c < std::max (1, (int) n.nChord) && c < 4; ++c)
                        {
                            const int d = (pcOfNote (n.chord[c] + bias) - bpc + 12) % 12;
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
        expect (clashes == 0, std::string (names[style]) + " bass never sits a semitone from stab/pad/pluck/hits/vox");
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

// The originality redraw log (Groove::origRedraws / guardRedraws / redrawLog)
// agrees with the "|orig:N" tag, and every entry names a lane and a source.
void testRedrawLog()
{
    int rolls = 0;
    int bad = 0;
    int total = 0;
    for (int style = 0; style < 4; ++style)
        for (int roll = 0; roll < 10; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts] {};
            for (int p = 0; p < rolla::NumParts; ++p)
                seeds[p] = 31u + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u + (std::uint32_t) style * 101u;
            rolla::Groove g {};
            if (! rolla::generate (g, s, seeds, 0x7fu))
                continue;
            ++rolls;
            const int n = g.origRedraws + g.guardRedraws;
            total += n;
            int tagged = 0;
            if (const char* at = std::strstr (g.brokenRules, "|orig:"))
                tagged = std::atoi (at + 6);
            if (tagged != n || g.redrawLogCount != std::min (n, (int) rolla::Groove::kMaxRedrawLog))
                ++bad;
            for (int i = 0; i < g.redrawLogCount; ++i)
                if (g.redrawLog[i].ref[0] == '\0')
                    ++bad;
        }
    std::cout << "redraw log: " << total << " redraws over " << rolls << " rolls\n";
    expect (rolls > 0 && bad == 0, "the redraw log matches the |orig: tag and names a source for every redraw");
}

// Stab chord hits per bar (one chord = one hit) over 40 seeds x 8 bars.
double stabHitsPerBar (int style, float stab, std::uint32_t base)
{
    long hits = 0;
    long bars = 0;
    for (int roll = 0; roll < 40; ++roll)
    {
        rolla::Settings s;
        s.dialect = style;
        s.bars = 8;
        s.stab = stab;
        s.busy = 0.40f;
        std::uint32_t seeds[rolla::NumParts] {};
        for (int p = 0; p < rolla::NumParts; ++p)
            seeds[p] = base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u
                     + (std::uint32_t) style * 101u;
        rolla::Groove g {};
        rolla::generate (g, s, seeds, 0x7fu);
        std::set<long> onsets;
        const auto& lane = g.lane[rolla::Stab];
        for (int k = 0; k < lane.count; ++k)
            onsets.insert (std::lround (lane.notes[k].start));
        hits += (long) onsets.size();
        bars += g.bars;
    }
    return bars > 0 ? (double) hits / (double) bars : 0.0;
}

// STAB density per style (main red: sample_pack_check --stab-sweep saturated
// at 0.55 == 1 after #62's 3-hit cap). The default knob (0.55) has to land in
// the style's stab_hpb range from data/genre/style_rules.json (ROLLER
// 0.34-4.5, DEEP 0.05-2.76, BOUNCY 0.25-2.97, JACKIN 0.34-2.97), and
// 0 -> 0.55 -> 1 has to raise the density strictly in every style.
void testStabDensity()
{
    const std::uint32_t base = 5505u;
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    const double lo[] = { 0.34, 0.05, 0.25, 0.34 };
    const double hi[] = { 4.5, 2.76, 2.97, 2.97 };
    for (int style : { (int) rolla::UkMin, (int) rolla::Long, (int) rolla::Pop, (int) rolla::Jackin })
    {
        const double at0 = stabHitsPerBar (style, 0.f, base);
        const double atDefault = stabHitsPerBar (style, 0.55f, base);
        const double at1 = stabHitsPerBar (style, 1.f, base);
        std::cout << names[style] << " stab hits/bar " << at0 << " / " << atDefault << " / " << at1
                  << " at STAB 0 / 0.55 / 1 (range " << lo[style] << "-" << hi[style] << ")\n";
        expect (atDefault >= lo[style] - 1.0e-9 && atDefault <= hi[style] + 1.0e-9,
                std::string (names[style]) + " default STAB stab hits/bar " + std::to_string (atDefault)
                    + " inside the style range");
        expect (at0 < atDefault && atDefault < at1,
                std::string (names[style]) + " STAB 0 / 0.55 / 1 raises stab density strictly");
        expect (at1 <= 3.0 + 1.0e-9,
                std::string (names[style]) + " full STAB stays at 3 hits/bar or fewer (no 4-hit riff)");
    }
}

// Composer 409/466: a pickup roll has 1-3 bass notes on sixteenths 13-16 of the
// last bar (0-based 12-15), and the last of them approaches bar 1's first bass
// pitch by step (1-2 semitones) or by a fifth (5 or 7). 4 styles x 50 seeds.
void testBar8Pickup()
{
    const std::uint32_t base = 913u;
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    for (int style = 0; style < 4; ++style)
    {
        int pickupRolls = 0;
        int countOk = 0;
        int approachOk = 0;
        for (int roll = 0; roll < 50; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts] {};
            for (int p = 0; p < rolla::NumParts; ++p)
                seeds[p] = base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u
                         + (std::uint32_t) style * 101u;
            if (! rolla::rollWantsPickup (seeds[rolla::Bass]))
                continue;
            rolla::Groove g {};
            rolla::generate (g, s, seeds, 0x7fu);
            ++pickupRolls;

            const auto& bass = g.lane[rolla::Bass];
            const int origin = (g.bars - 1) * 16;
            int inWindow = 0;
            int lastStart = -1;
            int lastPitch = -1;
            int firstStart = 1 << 30;
            int firstPitch = -1;
            for (int i = 0; i < bass.count; ++i)
            {
                const int st = (int) std::lround (bass.notes[i].start);
                const int pitch = bass.notes[i].chord[0];
                if (st >= origin + 12 && st < origin + 16)
                {
                    ++inWindow;
                    if (st > lastStart)
                    {
                        lastStart = st;
                        lastPitch = pitch;
                    }
                }
                if (st < firstStart)
                {
                    firstStart = st;
                    firstPitch = pitch;
                }
            }
            if (inWindow >= 1 && inWindow <= 3)
                ++countOk;
            (void) firstPitch;
            if (inWindow > 0)
            {
                // The pickup resolves to the key root (bass pitches are relative to it).
                const int iv = ((0 - lastPitch) % 12 + 12) % 12;
                if (iv == 1 || iv == 2 || iv == 10 || iv == 11 || iv == 5 || iv == 7)
                    ++approachOk;
            }
        }
        std::cout << names[style] << " bar-8 pickup: rolls " << pickupRolls << " count 1-3 " << countOk
                  << " step/fifth approach " << approachOk << "\n";
        expect (pickupRolls > 0, std::string (names[style]) + " some rolls want a pickup");
        expect (countOk == pickupRolls, std::string (names[style]) + " every pickup is 1-3 notes on sixteenths 13-16 of the last bar");
        expect (approachOk * 10 >= pickupRolls * 9,
                std::string (names[style]) + " pickups approach bar 1's bass by step or a fifth (>= 90%)");
    }
}

// A&R MIDI gate (seed 2026101852, 20 rolls a style), checked over 40 seeds a
// style from that base:
//  - mean bass onsets per bar inside the reference range (style_rules.json
//    bass_onsets_per_bar: targets ROLLER 5.0, DEEP 5.21, BOUNCY 5.0; the
//    floor sits under them, the ceiling is the style max);
//  - mean sounding ratio <= 0.82 outside BOUNCY pulse (was 0.86-0.90);
//  - DEEP's back half (bars 5-7) moves its density against the front half
//    (bars 1-3) in most rolls (was flat in 17 of 20);
//  - DEEP stabs land in the bass gaps (was none in 14 of 20), under the
//    50% bonus line.
void testArGateBass()
{
    const std::uint32_t base = 2026101852u;
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    const double npbLo[] = { 4.5, 4.6, 4.5, 0.0 };
    const double npbHi[] = { 7.44, 6.69, 6.03, 99.0 };
    const int rolls = 40;
    for (int style = 0; style < 4; ++style)
    {
        double npbSum = 0.0;
        double sndSum = 0.0;
        int sndN = 0;
        int backMoves = 0;
        int stabHits = 0;
        int stabGaps = 0;
        int gapRolls = 0;
        for (int roll = 0; roll < rolls; ++roll)
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
            const auto& bass = g.lane[rolla::Bass];
            const double loop = (double) (g.bars * 16);
            npbSum += (double) bass.count / (double) g.bars;

            double held = 0.0;
            int bar[8] {};
            for (int i = 0; i < bass.count; ++i)
            {
                held += bass.notes[i].length;
                const int b = (int) std::lround (bass.notes[i].start) / 16;
                if (b >= 0 && b < 8)
                    ++bar[b];
            }
            if (g.bassMode != rolla::BassModePulse)
            {
                sndSum += held / loop;
                ++sndN;
            }

            if (style == rolla::Long)
            {
                if (bar[4] != bar[0] || bar[5] != bar[1] || bar[6] != bar[2])
                    ++backMoves;
                const auto& stab = g.lane[rolla::Stab];
                int rollHits = 0;
                int rollGaps = 0;
                for (int k = 0; k < stab.count; ++k)
                {
                    ++rollHits;
                    if (! bassSoundsAt (bass, (int) std::lround (stab.notes[k].start)))
                        ++rollGaps;
                }
                stabHits += rollHits;
                stabGaps += rollGaps;
                if (rollHits > 0 && rollGaps * 100 >= rollHits * 15)
                    ++gapRolls;
            }
        }
        const double npb = npbSum / rolls;
        const double snd = sndN > 0 ? sndSum / sndN : 0.0;
        std::cout << names[style] << " A&R gate: bass onsets/bar " << npb << " sounding (no pulse) " << snd;
        if (style == rolla::Long)
            std::cout << " back-half moves " << backMoves << "/" << rolls << " stab-in-gap "
                      << (stabHits > 0 ? 100.0 * stabGaps / stabHits : 0.0) << "% rolls >= 15% " << gapRolls << "/" << rolls;
        std::cout << "\n";
        if (style != rolla::Jackin)
            expect (npb >= npbLo[style] && npb <= npbHi[style],
                    std::string (names[style]) + " mean bass onsets per bar " + std::to_string (npb) + " in "
                        + std::to_string (npbLo[style]) + "-" + std::to_string (npbHi[style]));
        expect (sndN > 0 && snd <= 0.82 + 1.0e-6,
                std::string (names[style]) + " mean sounding " + std::to_string (snd) + " <= 0.82 outside pulse");
        if (style == rolla::Long)
        {
            const double gapPct = stabHits > 0 ? 100.0 * stabGaps / stabHits : 0.0;
            expect (backMoves * 10 >= rolls * 8, "DEEP back half moves its density against the front half (>= 80% of rolls)");
            expect (gapPct >= 20.0 && gapPct < 50.0, "DEEP stabs in bass gaps " + std::to_string (gapPct) + "% in 20-50%");
            expect (gapRolls * 4 >= rolls * 3, "DEEP stabs answer in the bass gaps in >= 75% of rolls");
        }
    }
}

// ---- A&R MIDI gate repros (msg 490): rolla-rolls --seed 2026101852 ----

// tools/roll_batch.cpp renderBatch, MIDI side: the default knobs (DARK 0.55,
// BUSY 0.40, BOUNCE 0.45, STAB 0.55, STYLE 0.5 = Settings defaults), key D,
// 8 bars, no fill; per-part seeds from the batch seed; the skeleton redraw
// against the earlier rolls of the batch; then INTENSITY 75, which is what
// the batch writes to MIDI. generate() remembers recent rhythms per style for
// the whole process (separateTwinRhythm), so the tests that use this run first
// in main(): a fresh process is what rolla-rolls starts from.
struct BatchRoll
{
    rolla::Groove arranged {};
    std::uint32_t seeds[rolla::NumParts] {};
};

rolla::Settings batchSettings (int style)
{
    rolla::Settings s;
    s.dialect = style;
    s.key = 2;
    s.bars = 8;
    s.fill = false;
    return s;
}

std::vector<BatchRoll> batchRolls (std::uint32_t batchSeed, int style, int count)
{
    const rolla::Settings s = batchSettings (style);
    std::vector<BatchRoll> out;
    std::vector<rolla::Groove> seen;
    out.reserve ((std::size_t) count);
    seen.reserve ((std::size_t) count);
    for (int roll = 0; roll < count; ++roll)
    {
        out.emplace_back();
        BatchRoll& r = out.back();
        for (int p = 0; p < rolla::NumParts; ++p)
            r.seeds[p] = batchSeed + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u
                       + (std::uint32_t) style * 101u;
        rolla::Groove groove {};
        rolla::generate (groove, s, r.seeds, 0x7fu);
        for (int guard = 0; guard < 8; ++guard)
        {
            bool close = false;
            for (const auto& prev : seen)
                if (rolla::bassSkeletonSimilarity (groove, prev) >= 0.55f)
                    close = true;
            if (! close)
                break;
            r.seeds[rolla::Bass] ^= 0x6D2B79F5u * (std::uint32_t) (guard + 1);
            rolla::generate (groove, s, r.seeds, 0x7fu);
        }
        seen.push_back (groove);
        const auto plan = rolla::planIntensity (style, 75, s.stab);
        rolla::applyIntensity (groove, r.arranged, plan, 0, 0);
    }
    return out;
}

const char* modeName (int mode)
{
    if (mode == rolla::phrase::Dorian)
        return "dorian";
    if (mode == rolla::phrase::Phrygian)
        return "phrygian";
    return "aeolian";
}

// A&R 490 #2: ROLLER roll_04 of batch 2026101852 played seven Eb bass notes in
// D minor (gate run before #62). Every bass pitch must be in the roll's mode
// (rollMode). The one exception is the style rule's b2 (style_rules.json
// ROLLER scales.chromatic, Composer 434): outside phrygian only as a one-16th
// passing note between the root and the 2, at most 12% of bass time
// (phrygian 30%). The root must also be one of the two most-sounding pitch
// classes (H1), so a listener and the scorer hear D as the key.
void testBatchRollerKey()
{
    const std::uint32_t batch = 2026101852u;
    const auto rolls = batchRolls (batch, rolla::UkMin, 5);
    const auto& r = rolls[4];
    const int mode = rolla::rollMode (batchSettings (rolla::UkMin), r.seeds[rolla::Bass]);
    auto bass = r.arranged.lane[rolla::Bass];
    std::sort (bass.notes, bass.notes + bass.count,
               [] (const rolla::Note& a, const rolla::Note& b) { return a.start < b.start; });

    auto passing = [&] (int i)
    {
        if (i <= 0 || i + 1 >= bass.count || bass.notes[i].length > 1.05f)
            return false;
        const auto& p = bass.notes[i - 1];
        const auto& n = bass.notes[i + 1];
        const int pp = pcOfNote (p.chord[0]);
        const int np = pcOfNote (n.chord[0]);
        return (pp == 0 || pp == 2) && (np == 0 || np == 2)
            && bass.notes[i].start - (p.start + p.length) <= 1.05f
            && n.start - (bass.notes[i].start + bass.notes[i].length) <= 1.05f;
    };

    double total = 0.0;
    double b2Time = 0.0;
    double pcTime[12] {};
    int b2Notes = 0;
    int outOfMode = 0;
    int stranded = 0;
    std::string outList;
    for (int i = 0; i < bass.count; ++i)
    {
        const auto& n = bass.notes[i];
        if (n.vel <= 2)
            continue; // MidiExport drops these
        double len = n.length;
        if (i + 1 < bass.count)
            len = std::min (len, (double) (bass.notes[i + 1].start - n.start));
        len = std::max (0.0, len);
        const int pc = pcOfNote (n.chord[0]);
        total += len;
        pcTime[pc] += len;
        if (pc == 1)
        {
            ++b2Notes;
            b2Time += len;
        }
        if (rolla::phrase::inMode (pc, mode))
            continue;
        if (pc == 1 && mode != rolla::phrase::Phrygian)
        {
            if (! passing (i))
            {
                ++stranded;
                outList += " b2@" + std::to_string (n.start);
            }
            continue;
        }
        ++outOfMode;
        outList += " pc" + std::to_string (pc) + "@" + std::to_string (n.start);
    }
    int rootRank = 0;
    for (int pc = 1; pc < 12; ++pc)
        if (pcTime[pc] > pcTime[0] + 1.0e-9)
            ++rootRank;
    const double cap = mode == rolla::phrase::Phrygian ? 0.30 : 0.12;
    const double b2Share = total > 0.0 ? b2Time / total : 0.0;
    std::cout << "ROLLER batch " << batch << " roll_04: bass seed " << r.seeds[rolla::Bass] << " mode " << modeName (mode)
              << ", " << bass.count << " bass notes, Eb (b2) notes " << b2Notes << " (" << (100.0 * b2Share)
              << "% of bass time), out of mode " << outOfMode << ", stranded b2 " << stranded
              << ", root rank " << (rootRank + 1) << (outList.empty() ? "" : " :") << outList << "\n";
    expect (bass.count > 0, "ROLLER roll_04 has a bass line");
    expect (outOfMode == 0, "ROLLER roll_04: every bass pitch is in the roll's " + std::string (modeName (mode)) + " mode");
    expect (stranded == 0, "ROLLER roll_04: a b2 outside phrygian is only a one-16th passing note between 1 and 2");
    expect (b2Share <= cap + 1.0e-5, "ROLLER roll_04: b2 within the style cap");
    expect (rootRank <= 1, "ROLLER roll_04: the root is one of the two most-sounding bass pitch classes");
}

// A&R 490 #4: the gate's BOUNCY rolls were all pulse. Genre model v1.2 makes
// pulse a 30-40% batch quota through the seed index (the roll's drums seed),
// so batch 2026101852 plays 6-8 pulse rolls of 20, and pulse holds the A&R
// mode-coverage gate (MODE_MIN_SHARE pop pulse >= 25%).
void testBatchBouncyModes()
{
    const std::uint32_t batch = 2026101852u;
    const auto rolls = batchRolls (batch, rolla::Pop, 20);
    int bounce = 0;
    int pulse = 0;
    std::string order;
    for (const auto& r : rolls)
    {
        if (r.arranged.bassMode == rolla::BassModeBounce)
        {
            ++bounce;
            order.push_back ('B');
        }
        else if (r.arranged.bassMode == rolla::BassModePulse)
        {
            ++pulse;
            order.push_back ('P');
        }
        else
            order.push_back ('?');
    }
    std::cout << "BOUNCY batch " << batch << ": bounce " << bounce << " pulse " << pulse << " (" << order << ")\n";
    expect (bounce + pulse == 20, "every BOUNCY batch roll carries a mode");
    expect (pulse >= 6 && pulse <= 8, "BOUNCY batch " + std::to_string (batch) + " plays pulse on 6-8 of 20 (30-40%), got "
                                          + std::to_string (pulse));
    expect (pulse * 4 >= 20, "BOUNCY batch " + std::to_string (batch) + ": pulse covers >= 25% of the batch (A&R mode coverage)");
}

// tools/score_rolls.py v2.2.1, read on the exported MIDI the way the A&R gate
// does: LOOP_NO_REPEAT and the JACKIN ("jackin") lane-fit features F1-F11
// (F12 is the duck, which the scorer does not read on a full mix). A roll is
// LANE_FIT_LOW when more than two measurable features miss.
struct ScoreNote
{
    double s = 0.0; // 16ths
    double e = 0.0;
    int p = 0;
};

int pyRound (double x)
{
    return (int) std::nearbyint (x); // round half to even, as Python's round()
}

double pyMedian (std::vector<double> v)
{
    if (v.empty())
        return 0.0;
    std::sort (v.begin(), v.end());
    const std::size_t n = v.size();
    return (n & 1u) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double roundTo (double x, double scale)
{
    return std::nearbyint (x * scale) / scale;
}

struct JackinScore
{
    double gm2 = 0.0;
    double gm4 = 0.0;
    double rep1 = -1.0;
    double rep2 = -1.0;
    bool loopNoRepeat = true;
    int measurable = 0;
    int misses = 0;
    std::string missList;
    std::string detail;
};

JackinScore scoreJackin (const std::vector<ScoreNote>& bassIn, const std::vector<ScoreNote>& stabIn,
                         const std::vector<ScoreNote>& drums, int bars)
{
    JackinScore out;
    const int L = bars * 16;
    // mono_bass: the lowest pitch per 16th, sounding until the next attack.
    std::map<int, ScoreNote> by;
    for (const auto& n : bassIn)
    {
        const int k = pyRound (n.s);
        auto it = by.find (k);
        if (it == by.end() || n.p < it->second.p)
            by[k] = n;
    }
    struct Bn { double s, e; int p; double ln; };
    std::vector<Bn> all;
    for (const auto& kv : by)
        all.push_back ({ kv.second.s, kv.second.e, kv.second.p, 0.0 });
    for (std::size_t i = 0; i < all.size(); ++i)
        all[i].ln = i + 1 < all.size() ? std::max (0.05, std::min (all[i].e, all[i + 1].s) - all[i].s)
                                       : std::max (0.05, all[i].e - all[i].s);
    std::vector<Bn> bn;
    for (const auto& n : all)
        if (n.s < L - 0.5)
            bn.push_back (n);
    if (bn.size() < 4)
    {
        out.misses = 99;
        out.missList = "NO_BASS";
        return out;
    }
    const double nb = (double) bn.size();

    std::vector<double> kick;
    std::vector<double> hats;
    for (const auto& d : drums)
    {
        if (d.p == 35 || d.p == 36)
            kick.push_back (d.s);
        if (d.p == 42 || d.p == 44 || d.p == 46)
            hats.push_back (d.s);
    }
    std::sort (kick.begin(), kick.end());
    std::sort (hats.begin(), hats.end());
    if (! kick.empty())
    {
        const int nbar = std::max (1, (int) (kick.back() / 16.0) + 1);
        std::map<int, int> cnt;
        for (double t : kick)
            if (pyRound (t) % 4 != 0)
                ++cnt[pyRound (t) % 16];
        std::vector<double> keep;
        for (double t : kick)
            if (pyRound (t) % 4 == 0 || cnt[pyRound (t) % 16] >= 0.5 * nbar)
                keep.push_back (t);
        kick = keep;
    }

    // F1 off the kick
    int off = 0;
    for (const auto& n : bn)
    {
        bool onKick = false;
        for (double k : kick)
            if (std::fabs (n.s - k) <= 0.49)
                onKick = true;
        if (! onKick)
            ++off;
    }
    const double offkick = roundTo (100.0 * off / nb, 10.0);
    // F2 pitch classes (time-weighted; ties keep first appearance, like Counter)
    double tpc[12] {};
    int firstAt[12];
    for (int& f : firstAt)
        f = 1 << 30;
    double tot = 0.0;
    for (std::size_t i = 0; i < bn.size(); ++i)
    {
        const int pc = pcOfNote (bn[i].p);
        tpc[pc] += bn[i].ln;
        tot += bn[i].ln;
        firstAt[pc] = std::min (firstAt[pc], (int) i);
    }
    std::vector<int> pcsOrder;
    for (int pc = 0; pc < 12; ++pc)
        if (firstAt[pc] < (1 << 30))
            pcsOrder.push_back (pc);
    std::stable_sort (pcsOrder.begin(), pcsOrder.end(), [&] (int a, int b)
    {
        if (tpc[a] != tpc[b])
            return tpc[a] > tpc[b];
        return firstAt[a] < firstAt[b];
    });
    const int tonic = pcsOrder[0];
    const double top2 = roundTo (100.0 * (tpc[pcsOrder[0]] + (pcsOrder.size() > 1 ? tpc[pcsOrder[1]] : 0.0)) / tot, 10.0);
    int pcs5 = 0;
    for (int pc = 0; pc < 12; ++pc)
        if (tpc[pc] > 0.0 && tpc[pc] >= 0.05 * tot)
            ++pcs5;
    // F3 length
    std::vector<double> lens;
    int long6 = 0;
    for (const auto& n : bn)
    {
        lens.push_back (n.ln);
        if (n.ln >= 6.0)
            ++long6;
    }
    const double lenMed = roundTo (pyMedian (lens), 100.0);
    const double long6Pct = roundTo (100.0 * long6 / nb, 10.0);
    // F4 density
    std::set<int> att;
    for (const auto& n : bn)
        att.insert (pyRound (n.s));
    int run = 1;
    int bestRun = 1;
    int prevSlot = -100;
    for (int sl : att)
    {
        run = sl == prevSlot + 1 ? run + 1 : 1;
        bestRun = std::max (bestRun, run);
        prevSlot = sl;
    }
    const double npb = roundTo (nb / bars, 100.0);
    int eaa = 0;
    for (int b = 0; b < L; b += 4)
        if (att.count (b + 1) && att.count (b + 2) && att.count (b + 3))
            ++eaa;
    const double eaaPct = roundTo (100.0 * eaa / (L / 4), 10.0);
    // F5 register
    std::vector<double> pitches;
    for (const auto& n : bn)
        pitches.push_back ((double) n.p);
    const double regMed = pyMedian (pitches);
    // F7 octave moves
    int moves = 0;
    int octs = 0;
    for (std::size_t i = 0; i + 1 < bn.size(); ++i)
    {
        const int x = std::abs (bn[i + 1].p - bn[i].p);
        ++moves;
        if (x == 12 || x == 24)
            ++octs;
    }
    const double octPct = roundTo (100.0 * octs / std::max (1, moves), 10.0);
    // F6 loop: attack/pitch-class tokens, and the flavour
    std::vector<int> tok ((std::size_t) L, -1); // -1 rest, -2 sustain, else pc
    for (const auto& n : bn)
    {
        const int a = pyRound (n.s);
        if (a < 0 || a >= L)
            continue;
        tok[(std::size_t) a] = pcOfNote (n.p);
        for (int k = a + 1; k < std::min (L, pyRound (n.s + n.ln)); ++k)
            if (tok[(std::size_t) k] == -1)
                tok[(std::size_t) k] = -2;
    }
    auto gridMatch = [&] (int lag) -> double
    {
        int pairs = 0;
        int same = 0;
        for (int i = 0; i + lag < L; ++i)
        {
            const int a = tok[(std::size_t) i] < 0 ? -1 : tok[(std::size_t) i];
            const int b = tok[(std::size_t) (i + lag)] < 0 ? -1 : tok[(std::size_t) (i + lag)];
            if (a == -1 && b == -1)
                continue;
            ++pairs;
            if (a == b)
                ++same;
        }
        return pairs > 0 ? roundTo ((double) same / pairs, 1000.0) : -1.0;
    };
    out.rep1 = gridMatch (16);
    out.rep2 = gridMatch (32);
    int dottedN = 0;
    for (int k : att)
    {
        const int sl = k % 16;
        if (sl == 0 || sl == 3 || sl == 6 || sl == 9 || sl == 12 || sl == 15)
            ++dottedN;
    }
    const double dotted = roundTo (100.0 * dottedN / (double) att.size(), 10.0);
    const double b2Pct = roundTo (100.0 * tpc[(tonic + 1) % 12] / tot, 10.0);
    // LOOP_NO_REPEAT: sounding pitch (or rest) per 16th, 2 and 4 bars apart
    std::vector<int> sp ((std::size_t) L, -1);
    for (const auto& n : bn)
        for (int k = pyRound (n.s); k < std::min (L, pyRound (n.s + n.ln)); ++k)
            if (k >= 0)
                sp[(std::size_t) k] = n.p;
    auto gm = [&] (int lag) -> double
    {
        if (L <= lag)
            return -1.0;
        int same = 0;
        for (int i = 0; i + lag < L; ++i)
            if (sp[(std::size_t) i] == sp[(std::size_t) (i + lag)])
                ++same;
        return roundTo ((double) same / (L - lag), 1000.0);
    };
    out.gm2 = gm (32);
    out.gm4 = gm (64);
    out.loopNoRepeat = out.gm2 < 0.50 && (out.gm4 < 0.0 || out.gm4 < 0.47);
    // F8 / F9 stab
    std::map<int, std::vector<ScoreNote>> hitsBy;
    for (const auto& n : stabIn)
        if (n.s < L - 0.5)
            hitsBy[pyRound (n.s)].push_back (n);
    const int stabHits = (int) hitsBy.size();
    double stabOnbeat = -1.0;
    double stabPcsMed = 0.0;
    double stabDurMed = 0.0;
    if (stabHits > 0)
    {
        int onbeat = 0;
        std::vector<double> pcsPer;
        std::vector<double> durs;
        for (const auto& kv : hitsBy)
        {
            if (kv.first % 4 == 0)
                ++onbeat;
            std::set<int> pcs;
            double dur = 0.0;
            for (const auto& n : kv.second)
            {
                pcs.insert (pcOfNote (n.p));
                dur = std::max (dur, n.e - n.s);
            }
            pcsPer.push_back ((double) pcs.size());
            durs.push_back (dur);
        }
        stabOnbeat = roundTo (100.0 * onbeat / stabHits, 10.0);
        stabPcsMed = pyMedian (pcsPer);
        stabDurMed = roundTo (pyMedian (durs), 100.0);
    }
    // F10 / F11 drums
    int covered = 0;
    for (int q = 0; q < L; q += 4)
    {
        bool hit = false;
        for (double k : kick)
            if (std::fabs (k - q) <= 0.5)
                hit = true;
        if (hit)
            ++covered;
    }
    const double kickCov = roundTo ((double) covered / (L / 4), 100.0);
    std::set<int> claps;
    std::set<int> hatSlots;
    int closed = 0;
    for (const auto& d : drums)
    {
        if (d.s >= L - 0.5)
            continue;
        if (d.p == 38 || d.p == 39 || d.p == 40)
            claps.insert (pyRound (d.s));
        if (d.p == 42 || d.p == 44 || d.p == 46)
            hatSlots.insert (pyRound (d.s));
        if (d.p == 42 || d.p == 44)
            ++closed;
    }
    int clapBars = 0;
    int hatBars = 0;
    for (int b = 0; b < bars; ++b)
    {
        if (claps.count (b * 16 + 4) && claps.count (b * 16 + 12))
            ++clapBars;
        if (hatSlots.count (b * 16 + 2) || hatSlots.count (b * 16 + 6) || hatSlots.count (b * 16 + 10)
            || hatSlots.count (b * 16 + 14))
            ++hatBars;
    }
    const double clapPct = claps.empty() ? 0.0 : roundTo (100.0 * clapBars / bars, 10.0);
    const double hatOffPct = roundTo (100.0 * hatBars / bars, 10.0);
    const double closedPerBar = roundTo ((double) closed / bars, 100.0);
    double hatSwing = -1.0;
    {
        std::vector<double> odd;
        std::vector<double> even;
        for (double t : hats)
            ((pyRound (t) % 2) == 1 ? odd : even).push_back (t - pyRound (t));
        if (odd.size() >= 4 && even.size() >= 4)
            hatSwing = roundTo (50.0 + 50.0 * (pyMedian (odd) - pyMedian (even)), 10.0);
    }

    // LANE["jackin"] (score_rolls.py): range membership per feature.
    struct F { const char* id; bool measurable; bool ok; };
    const bool flav = dotted >= 55.0 || b2Pct >= 10.0 || stabDurMed >= 6.0
                   || (stabHits > 0 && stabDurMed <= 1.5 && stabOnbeat >= 0.0 && stabOnbeat <= 10.0);
    const F feats[] = {
        { "F1_offkick", true, offkick >= 65.0 && offkick <= 100.0 },
        { "F2_pitch", true, top2 >= 45.0 && pcs5 >= 2 && pcs5 <= 5 },
        { "F3_length", true, lenMed >= 1.5 && lenMed <= 2.6 && long6Pct <= 8.0 },
        { "F4_density", true, npb >= 3.9 && npb <= 8.7 && bestRun <= 7 && eaaPct <= 20.0 },
        { "F5_register", true, regMed >= 25.0 && regMed <= 36.0 },
        { "F6_loop", out.rep1 >= 0.0, out.rep1 >= 0.0 && out.rep1 <= 0.10 && out.rep2 >= 0.60 && flav },
        { "F7_octaves", true, (octPct >= 0.0 && octPct <= 8.5) || (octPct >= 15.0 && octPct <= 26.0) },
        { "F8_stab", stabHits > 0, stabHits > 0 && stabOnbeat <= 50.0 && stabPcsMed >= 1.0 && stabPcsMed <= 4.0 },
        { "F9_stab_vs_bass", stabHits > 0, true },
        { "F10_kick_clap", ! kick.empty(), kickCov >= 0.85 && clapPct >= 75.0 },
        { "F11_hats", ! drums.empty(), hatOffPct >= 50.0 && closedPerBar <= 10.0 && (hatSwing < 0.0 || (hatSwing >= 50.0 && hatSwing <= 57.0)) },
    };
    for (const auto& f : feats)
    {
        if (! f.measurable)
            continue;
        ++out.measurable;
        if (! f.ok)
        {
            ++out.misses;
            out.missList += std::string (out.missList.empty() ? "" : " ") + f.id;
        }
    }
    out.detail = "offkick " + std::to_string (offkick) + " top2 " + std::to_string (top2) + " pcs " + std::to_string (pcs5)
               + " len " + std::to_string (lenMed) + " long6 " + std::to_string (long6Pct) + " npb " + std::to_string (npb)
               + " run " + std::to_string (bestRun) + " eaa " + std::to_string (eaaPct) + " reg " + std::to_string (regMed)
               + " dotted " + std::to_string (dotted) + " b2 " + std::to_string (b2Pct) + " oct " + std::to_string (octPct)
               + " stabDur " + std::to_string (stabDurMed) + " stabOnbeat " + std::to_string (stabOnbeat);
    return out;
}

std::vector<ScoreNote> midiNotes (const std::string& path, bool drums)
{
    std::vector<ScoreNote> out;
    std::vector<MidiHit> hits;
    int dups = 0;
    int low = 0;
    if (! readMidi (path, hits, dups, low))
        return out;
    for (const auto& h : hits)
        if ((h.ch == 9) == drums)
            out.push_back ({ h.on / 120.0, h.off / 120.0, h.pitch });
    return out;
}

bool endsWith (const std::string& text, const std::string& tail)
{
    return text.size() >= tail.size() && text.compare (text.size() - tail.size(), tail.size(), tail) == 0;
}

// Stab tokens of one bar: (16th, pitch-class set) per chord onset.
std::set<std::pair<int, int>> stabBarTokens (const rolla::Lane& stab, int bar)
{
    std::set<std::pair<int, int>> out;
    std::map<int, int> mask;
    for (int i = 0; i < stab.count; ++i)
    {
        const auto& n = stab.notes[i];
        const int st = (int) std::lround (n.start);
        if (st / 16 != bar || n.vel <= 2)
            continue;
        for (int c = 0; c < std::max (1, (int) n.nChord) && c < 4; ++c)
            mask[st % 16] |= 1 << pcOfNote (n.chord[c]);
    }
    for (const auto& kv : mask)
        out.insert ({ kv.first, kv.second });
    return out;
}

// A&R 490 #3: JACKIN "loop doesn't repeat" on 14 of 20 rolls, plus 4
// LANE_FIT_LOW. Batch 2026101852, the 20 JACKIN rolls, scored as the gate
// does: no LOOP_NO_REPEAT and the F6 2-bar repeat >= 0.60 on every roll,
// LANE_FIT_LOW (more than 2 of F1-F11 out of range) on at most one. Every
// feature and miss is printed per roll. The stab follows the bass, so its
// bars repeat on the same 2-bar grid away from the bar-4 answer and the
// bar-8 turn.
void testBatchJackinLoop()
{
    const std::uint32_t batch = 2026101852u;
    const auto rolls = batchRolls (batch, rolla::Jackin, 20);
    auto bank = std::make_unique<rolla::PresetBank>();
    expect (rolla::loadPresetsFromJson (*bank, BinaryData::presets_json, BinaryData::presets_jsonSize),
            "presets.json loads for the JACKIN batch");
    const float swing = bank->dialect[rolla::Jackin].swing > 0.f ? bank->dialect[rolla::Jackin].swing : 0.5f;
    int noRepeat = 0;
    int laneLow = 0;
    int f6Ok = 0;
    double stabRepeat = 0.0;
    int stabPairs = 0;
    double gm2Sum = 0.0;
    double rep2Sum = 0.0;
    int rep1Ok = 0;
    for (int roll = 0; roll < (int) rolls.size(); ++roll)
    {
        const auto& g = rolls[(std::size_t) roll].arranged;
        const std::string tag = "JACKIN batch roll_" + std::string (roll < 10 ? "0" : "") + std::to_string (roll);
        const auto dir = std::filesystem::path ("/tmp/rolla_batch_jackin") / std::to_string (roll);
        std::filesystem::create_directories (dir);
        std::vector<std::string> written;
        std::string error;
        expect (rolla::MidiExport::writeAll (dir.string(), g, 2, swing, 130.0, written, error), tag + " midi export");
        std::vector<ScoreNote> bass, stab, hits, drums;
        for (const auto& path : written)
        {
            if (endsWith (path, "_bass.mid"))
                bass = midiNotes (path, false);
            else if (endsWith (path, "_stab.mid"))
                stab = midiNotes (path, false);
            else if (endsWith (path, "_hits.mid"))
                hits = midiNotes (path, false);
            // The scorer reads every MIDI in the roll folder: drums from the
            // drums file and again from the combined file.
            if (endsWith (path, "_drums.mid") || endsWith (path, "_all.mid"))
            {
                const auto d = midiNotes (path, true);
                drums.insert (drums.end(), d.begin(), d.end());
            }
        }
        // assign_parts: the named stab group with the most notes (hits first on a tie).
        const auto& stabPart = hits.size() >= 4 && hits.size() >= stab.size() ? hits : stab;
        const auto sc = scoreJackin (bass, stabPart, drums, g.bars);
        gm2Sum += sc.gm2;
        rep2Sum += sc.rep2;
        if (sc.rep1 >= 0.0 && sc.rep1 <= 0.10)
            ++rep1Ok;
        if (sc.loopNoRepeat)
            ++noRepeat;
        if (sc.misses > 2)
            ++laneLow;
        const bool f6 = sc.missList.find ("F6_loop") == std::string::npos && sc.rep1 >= 0.0;
        if (f6)
            ++f6Ok;
        std::cout << tag << " match@2 " << sc.gm2 << " match@4 " << sc.gm4 << " rep1 " << sc.rep1 << " rep2 " << sc.rep2
                  << " lane " << (sc.measurable - sc.misses) << "/" << sc.measurable
                  << (sc.missList.empty() ? "" : " miss: " + sc.missList) << " | " << sc.detail << "\n";
        expect (! sc.loopNoRepeat, tag + " repeats on the bar grid (LOOP_NO_REPEAT: match@2 " + std::to_string (sc.gm2)
                                      + ", match@4 " + std::to_string (sc.gm4) + ")");
        expect (sc.rep2 >= 0.60, tag + " 2-bar attack/pitch repeat >= 0.60 (lane F6), got " + std::to_string (sc.rep2));

        const auto& stabLane = g.lane[rolla::Stab];
        const int pairs[3][2] = { { 2, 4 }, { 4, 6 }, { 1, 5 } };
        for (const auto& pr : pairs)
        {
            const auto a = stabBarTokens (stabLane, pr[0]);
            const auto b = stabBarTokens (stabLane, pr[1]);
            std::set<std::pair<int, int>> uni = a;
            uni.insert (b.begin(), b.end());
            int inter = 0;
            for (const auto& t : a)
                if (b.count (t))
                    ++inter;
            if (! uni.empty())
            {
                stabRepeat += (double) inter / (double) uni.size();
                ++stabPairs;
            }
        }
    }
    const double stabMean = stabPairs > 0 ? stabRepeat / stabPairs : 0.0;
    std::cout << "JACKIN batch " << batch << ": LOOP_NO_REPEAT " << noRepeat << "/20, LANE_FIT_LOW " << laneLow
              << "/20, F6 loop " << f6Ok << "/20, match@2 mean " << (gm2Sum / 20.0)
              << ", stab 2-bar repeat " << stabMean << "\n";
    expect (noRepeat == 0, "JACKIN batch: every roll repeats on the bar grid");
    // The gate saw 4/20. A roll fails when 3+ of the 11 features miss; F6 was
    // the shared one, so with the loop fixed at most one roll may still miss.
    expect (laneLow <= 1, "JACKIN batch: LANE_FIT_LOW on at most 1 of 20 (was 4), got " + std::to_string (laneLow));
    expect (stabMean >= 0.80, "JACKIN batch: stab bars repeat on the 2-bar grid (Jaccard " + std::to_string (stabMean) + ")");
    // Genre model v1.2 JACKIN loop: 1-bar repeat <= 0.10, 2-bar repeat >= 0.70.
    std::cout << "JACKIN batch v1.2 loop: rep1 <= 0.10 on " << rep1Ok << "/20, rep2 mean " << (rep2Sum / 20.0) << "\n";
    expect (rep1Ok >= 18, "JACKIN batch: 1-bar repeat <= 0.10 on >= 18 of 20 rolls (v1.2), got " + std::to_string (rep1Ok));
    expect (rep2Sum / 20.0 >= 0.70, "JACKIN batch: mean 2-bar repeat >= 0.70 (v1.2), got " + std::to_string (rep2Sum / 20.0));
}

// ---- originality floor (Generator/Originality.h) ----

bool sameLane (const rolla::Lane& a, const rolla::Lane& b)
{
    if (a.count != b.count)
        return false;
    for (int i = 0; i < a.count; ++i)
    {
        const auto& x = a.notes[i];
        const auto& y = b.notes[i];
        if (x.start != y.start || x.length != y.length || x.nChord != y.nChord || x.vel != y.vel)
            return false;
        for (int c = 0; c < std::max (1, (int) x.nChord) && c < 4; ++c)
            if (x.chord[c] != y.chord[c])
                return false;
    }
    return true;
}

rolla::orig::Stream laneStream (const rolla::Groove& g, int part)
{
    return rolla::orig::streamFromNotes (rolla::orig::rawNotes (g.lane[part], rolla::orig::exportBase (part, g.dialect)));
}

void seedsForRoll (int style, int roll, std::uint32_t seeds[rolla::NumParts])
{
    for (int p = 0; p < rolla::NumParts; ++p)
        seeds[p] = 2026101852u + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u + (std::uint32_t) style * 101u;
}

// A synthetic fingerprint: an index built from a roll's own bass and stab, i.e.
// a "reference" this roll copies. With it loaded, the same seeds must redraw
// into a roll that clears it, deterministically, and keep the caller seeds.
void testOriginalityResample()
{
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    const std::uint32_t lanes = (1u << rolla::Drums) - 1u;
    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        const std::string tag = std::string ("originality ") + names[style] + ": ";
        rolla::Settings s;
        s.dialect = style;
        s.bars = 8;
        std::uint32_t seeds[rolla::NumParts];
        seedsForRoll (style, 13, seeds);

        rolla::orig::setIndex (nullptr);
        rolla::Groove base {};
        expect (rolla::generate (base, s, seeds, 0x7fu), tag + "the built-in guard clears without an index");

        auto index = std::make_shared<rolla::orig::Index>();
        index->addRef ("synthetic/bass", laneStream (base, rolla::Bass));
        index->addRef ("synthetic/stab", laneStream (base, rolla::Stab));
        index->finalize();
        const auto before = rolla::orig::check (base, lanes, index.get());
        expect (! before.pass, tag + "a roll matches an index built from itself");
        expect (before.worst.severity >= 1.f && ! before.worst.builtin, tag + "the hit comes from the external index");

        rolla::orig::setIndex (index);
        rolla::Groove fresh {};
        expect (rolla::generate (fresh, s, seeds, 0x7fu), tag + "the redraw clears, so generate() succeeds");
        const auto after = rolla::orig::check (fresh, lanes, index.get());
        expect (after.pass, tag + "the redrawn roll clears the synthetic reference");
        expect (std::strstr (fresh.brokenRules, "|orig:") != nullptr, tag + "the tag records the redraws");
        expect (! sameLane (fresh.lane[rolla::Bass], base.lane[rolla::Bass]) || ! sameLane (fresh.lane[rolla::Stab], base.lane[rolla::Stab]),
                tag + "a redraw changes the matching lanes");
        expect (fresh.lane[rolla::Bass].seed == seeds[rolla::Bass] && fresh.lane[rolla::Stab].seed == seeds[rolla::Stab],
                tag + "the caller seeds still land on the lanes");
        expect (fresh.generation == base.generation, tag + "one generate() call is one generation");

        rolla::Groove again {};
        rolla::generate (again, s, seeds, 0x7fu);
        bool same = std::strcmp (again.brokenRules, fresh.brokenRules) == 0;
        for (int p = 0; p < rolla::Drums; ++p)
            same = same && sameLane (again.lane[p], fresh.lane[p]);
        expect (same, tag + "same seeds + same index = same roll");

        rolla::orig::setIndex (nullptr);
        rolla::Groove plain {};
        rolla::generate (plain, s, seeds, 0x7fu);
        bool back = true;
        for (int p = 0; p < rolla::Drums; ++p)
            back = back && sameLane (plain.lane[p], base.lane[p]);
        expect (back, tag + "without the index the seeds give the original roll again");

        // The file format round-trips and holds no note data, only hashes.
        juce::String error;
        const auto parsed = rolla::orig::parseIndex (juce::JSON::parse (rolla::orig::toJson (*index)), &error);
        expect (parsed != nullptr, tag + "index JSON parses back: " + error.toStdString());
        if (parsed != nullptr)
        {
            expect (parsed->refs.size() == index->refs.size(), tag + "index JSON keeps every reference stream");
            expect (! rolla::orig::check (base, lanes, parsed.get()).pass, tag + "the parsed index still catches the copy");
        }
    }
    juce::String error;
    expect (rolla::orig::parseIndex (juce::JSON::parse ("{\"schema\":\"rolla.orig_index/1\",\"extractor\":999,\"refs\":[]}"), &error) == nullptr,
            "originality: an index from another extractor is refused");
    expect (rolla::orig::loadIndex (juce::File ("/nonexistent/orig_index.json")) == nullptr, "originality: no file means no index");
}

bool sameGrooveLanes (const rolla::Groove& a, const rolla::Groove& b)
{
    for (int p = 0; p < rolla::Drums; ++p)
        if (! sameLane (a.lane[p], b.lane[p]) || a.lane[p].seed != b.lane[p].seed)
            return false;
    return a.generation == b.generation && std::strcmp (a.brokenRules, b.brokenRules) == 0
        && std::memcmp (a.drums.vel, b.drums.vel, sizeof a.drums.vel) == 0;
}

// The redraw loop always ends, and a roll that cannot clear is refused, never
// returned. Bass-only redraws make every attempt predictable: composition r,
// attempt k draws from (r == 0 ? bass seed : originalityRecomposeSeed) + k.
// An index holding the bass of every one of those kOrigRounds x kOrigAttempts
// draws leaves nothing clean, so generate() must return false with the groove
// untouched. The expected outcome is computed independently of generate().
void testOriginalityTerminates()
{
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    const std::uint32_t bassOnly = 1u << rolla::Bass;
    int refused = 0;
    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        const std::string tag = std::string ("originality exhaust ") + names[style] + ": ";
        rolla::Settings s;
        s.dialect = style;
        s.bars = 8;
        std::uint32_t seeds[rolla::NumParts];
        seedsForRoll (style, 3, seeds);

        rolla::orig::setIndex (nullptr);
        rolla::Groove base {};
        expect (rolla::generate (base, s, seeds, 0x7fu), tag + "the base roll clears");

        const int total = rolla::kOrigRounds * rolla::kOrigAttempts;
        std::vector<rolla::Groove> attempt ((size_t) total);
        std::vector<bool> plainDraw ((size_t) total, false);
        auto index = std::make_shared<rolla::orig::Index>();
        for (int r = 0; r < rolla::kOrigRounds; ++r)
            for (int k = 0; k < rolla::kOrigAttempts; ++k)
            {
                const int at = r * rolla::kOrigAttempts + k;
                std::uint32_t sk[rolla::NumParts];
                std::copy (seeds, seeds + rolla::NumParts, sk);
                const std::uint32_t start = r == 0 ? seeds[rolla::Bass] : rolla::originalityRecomposeSeed (seeds[rolla::Bass], rolla::Bass, r);
                sk[rolla::Bass] = start + (std::uint32_t) k;
                attempt[(size_t) at] = base;
                const bool ok = rolla::generate (attempt[(size_t) at], s, sk, bassOnly);
                // Without the index, a draw that needed no redraw is exactly what attempt (r, k) makes.
                plainDraw[(size_t) at] = ok && std::strstr (attempt[(size_t) at].brokenRules, "|orig:") == nullptr;
                if (ok)
                    index->addRef ("synthetic/" + std::to_string (at), laneStream (attempt[(size_t) at], rolla::Bass));
            }
        index->finalize();

        // A draw the built-in guard redrew fails inside generate() too; a plain one clears only if it clears the index.
        int expectClean = -1;
        for (int at = 0; at < total && expectClean < 0; ++at)
            if (plainDraw[(size_t) at] && rolla::orig::check (attempt[(size_t) at], bassOnly, index.get()).pass)
                expectClean = at;

        rolla::orig::setIndex (index);
        rolla::Groove g = base;
        const bool ok = rolla::generate (g, s, seeds, bassOnly);
        if (expectClean < 0)
        {
            ++refused;
            expect (! ok, tag + "nothing clean left: generate() refuses");
            expect (sameGrooveLanes (g, base), tag + "a refused roll leaves the groove exactly as it was");
        }
        else
        {
            expect (ok, tag + "a clean attempt exists, so generate() succeeds");
            expect (sameLane (g.lane[rolla::Bass], attempt[(size_t) expectClean].lane[rolla::Bass]),
                    tag + "the first clean attempt ships");
            expect (rolla::orig::check (g, bassOnly, index.get()).pass, tag + "what ships clears the index");
        }

        rolla::Groove again = base;
        const bool okAgain = rolla::generate (again, s, seeds, bassOnly);
        expect (okAgain == ok && sameGrooveLanes (again, g), tag + "the refusal path is deterministic too");
        std::cout << names[style] << " originality exhaust: first clean draw " << expectClean
                  << (ok ? " shipped " : " refused ") << g.brokenRules << "\n";
    }
    expect (refused > 0, "originality: at least one style exercised the refusal path");
    rolla::orig::setIndex (nullptr);
}

void pumpFor (int ms)
{
    const auto start = juce::Time::getMillisecondCounter();
    while ((int) (juce::Time::getMillisecondCounter() - start) < ms)
    {
#if JUCE_LINUX
        if (! juce::detail::dispatchNextMessageOnSystemQueue (true))
            juce::Thread::sleep (1);
#else
        juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
#endif
    }
}

// No refused roll ever reaches output: generate() hands back the groove it was
// given, and the plugin keeps playing (and exporting) the previous roll, its
// seeds and presets. Driven by the in-memory refuse-all hook, so it needs no
// private index; the built-in guard stays on throughout.
void testOriginalityRefusalNeverShips()
{
    auto refuseAll = std::make_shared<rolla::orig::Index>();
    refuseAll->refuseAll = true;

    // Generator level, every style: false, groove untouched, every time.
    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        rolla::Settings s;
        s.dialect = style;
        std::uint32_t seeds[rolla::NumParts];
        seedsForRoll (style, 7, seeds);
        rolla::orig::setIndex (nullptr);
        rolla::Groove prev {};
        expect (rolla::generate (prev, s, seeds, 0x7fu), "refusal: the previous roll clears");
        rolla::orig::setIndex (refuseAll);
        std::uint32_t other[rolla::NumParts];
        seedsForRoll (style, 8, other);
        for (std::uint32_t mask : { 0x7fu, 1u << rolla::Bass, 1u << rolla::Stab })
        {
            rolla::Groove g = prev;
            expect (! rolla::generate (g, s, other, mask), "refusal: generate() returns false");
            expect (sameGrooveLanes (g, prev), "refusal: the refused roll never replaces the previous one");
        }
        rolla::Groove empty {};
        rolla::Groove fromEmpty {};
        expect (! rolla::generate (fromEmpty, s, other, 0x7fu) && sameGrooveLanes (fromEmpty, empty),
                "refusal: from an empty groove nothing is written");
    }
    rolla::orig::setIndex (nullptr);

    // Plugin level: ROLL and a lane shuffle under refusal keep the playing roll.
    rolla::RollaProcessor proc;
    proc.waitUntilReady();
    const auto before = proc.copyGroove();
    const auto noticeBefore = proc.rollNotice();
    expect (noticeBefore.text.isEmpty(), "refusal: no notice before any refusal");
    rolla::orig::setIndex (refuseAll);
    proc.triggerRoll();
    pumpUntil ([&] { return proc.rollNotice().serial != noticeBefore.serial; });
    const auto afterRoll = proc.rollNotice();
    expect (afterRoll.serial != noticeBefore.serial, "refusal: a refused ROLL changes the notice");
    expect (afterRoll.text == "Roll refused: too close to a reference, kept previous roll",
            "refusal: a refused ROLL exposes the status line, got '" + afterRoll.text.toStdString() + "'");
    proc.triggerShuffle (rolla::Bass);
    pumpUntil ([&] { return proc.rollNotice().text.startsWith ("Shuffle refused"); });
    expect (proc.rollNotice().text.startsWith ("Shuffle refused") && proc.rollNotice().text.endsWith ("kept previous roll"),
            "refusal: a refused shuffle exposes its own status line");
    const auto during = proc.copyGroove();
    bool kept = true;
    for (int p = 0; p < rolla::NumParts; ++p)
        kept = kept && seedOf (during, p) == seedOf (before, p);
    for (int p = 0; p < rolla::Drums; ++p)
        kept = kept && sameLane (during.lane[p], before.lane[p]);
    expect (kept, "refusal: the plugin keeps the previous roll when generate() refuses");

    rolla::orig::setIndex (nullptr);
    const auto bassBefore = seedOf (proc.copyGroove(), rolla::Bass);
    rollUntil (proc, rolla::Bass, bassBefore);
    expect (seedOf (proc.copyGroove(), rolla::Bass) != bassBefore, "refusal: ROLL works again once rolls clear");
    expect (proc.rollNotice().text.isEmpty(), "refusal: an accepted roll clears the status line");
}

// The built-in guard: the shipped banned-skeleton index (the repo holds no
// banned skeleton; the bundle carries a private index in the external format)
// and the built-in rules. The index path is exercised with a synthetic figure:
// supplied in memory, through the file format, and from orig_builtin.json in
// $ROLLA_GENRE_DATA_DIR; its hits count as built-in, and without it the lane
// passes. Then the 2&4 root rule, and 4 styles x 20 gate-seed rolls all clear.
void testOriginalityBuiltin()
{
    rolla::orig::setIndex (nullptr);
    rolla::orig::setBuiltinIndex (nullptr);
    rolla::Groove synth {};
    synth.dialect = rolla::Long;
    synth.bars = 8;
    auto& lane = synth.lane[rolla::Bass];
    const int slots[] = { 1, 5, 7, 10, 14 };      // synthetic: no reference figure
    const int pitch[] = { 0, 10, 3, 7, 5 };
    for (int bar = 0; bar < 8; ++bar)
        for (int k = 0; k < 5; ++k)
        {
            auto& n = lane.notes[lane.count++];
            n.start = (float) (bar * 16 + slots[k]);
            n.length = 1.5f;
            n.chord[0] = (std::int8_t) pitch[(k + bar % 2) % 5];
            n.nChord = 1;
            n.vel = 100;
        }
    expect (rolla::orig::check (synth, 1u << rolla::Bass, nullptr).pass,
            "originality: with no shipped index the synthetic figure is not banned");

    auto figure = std::make_shared<rolla::orig::Index>();
    figure->addRef ("banned:synthetic_test_figure", laneStream (synth, rolla::Bass));
    figure->finalize();
    rolla::orig::setBuiltinIndex (figure);
    auto rep = rolla::orig::check (synth, 1u << rolla::Bass, nullptr);
    expect (! rep.pass && rep.worst.builtin && rep.worst.refId == "banned:synthetic_test_figure",
            "originality: a figure in the shipped index is caught as built-in, with no external index");

    juce::String error;
    auto parsed = rolla::orig::parseIndex (juce::JSON::parse (rolla::orig::toJson (*figure)), &error);
    expect (parsed != nullptr, "originality: the shipped index format round-trips: " + error.toStdString());
    rolla::orig::setBuiltinIndex (parsed);
    expect (! rolla::orig::check (synth, 1u << rolla::Bass, nullptr).pass, "originality: the parsed shipped index still catches it");

    const auto dataDir = juce::File::createTempFile ("rolla-genre-data");
    dataDir.createDirectory();
    const char* oldDir = std::getenv ("ROLLA_GENRE_DATA_DIR");
    const std::string keep = oldDir != nullptr ? oldDir : "";
    rolla::test::putEnv ("ROLLA_GENRE_DATA_DIR", dataDir.getFullPathName().toStdString());
    expect (rolla::orig::builtinIndexFile() == dataDir.getChildFile ("orig_builtin.json"),
            "originality: the shipped index is read from $ROLLA_GENRE_DATA_DIR/orig_builtin.json");
    expect (rolla::orig::loadIndex (rolla::orig::builtinIndexFile()) == nullptr, "originality: no shipped file, no shipped index");
    expect (rolla::orig::writeIndex (*figure, rolla::orig::builtinIndexFile()), "originality: the test shipped index is written");
    rolla::orig::setBuiltinIndex (rolla::orig::loadIndex (rolla::orig::builtinIndexFile(), &error));
    rep = rolla::orig::check (synth, 1u << rolla::Bass, nullptr);
    expect (! rep.pass && rep.worst.builtin, "originality: the shipped index loaded from its file catches the figure");
    rolla::test::putEnv ("ROLLA_GENRE_DATA_DIR", keep);
    dataDir.deleteRecursively();

    // Determinism and the redraw: generate() never ships a roll the shipped index bans.
    rolla::Settings ls;
    ls.dialect = rolla::Long;
    ls.bars = 8;
    std::uint32_t lseeds[rolla::NumParts];
    seedsForRoll (rolla::Long, 2, lseeds);
    rolla::orig::setBuiltinIndex (nullptr);
    rolla::Groove plain {};
    expect (rolla::generate (plain, ls, lseeds, 0x7fu), "originality: the plain roll clears");
    auto own = std::make_shared<rolla::orig::Index>();
    own->addRef ("banned:synthetic_own_bass", laneStream (plain, rolla::Bass));
    own->finalize();
    rolla::orig::setBuiltinIndex (own);
    rolla::Groove redrawn {};
    expect (rolla::generate (redrawn, ls, lseeds, 0x7fu), "originality: a roll the shipped index bans is redrawn");
    expect (rolla::orig::check (redrawn, 0x3fu, nullptr).pass && ! sameLane (redrawn.lane[rolla::Bass], plain.lane[rolla::Bass]),
            "originality: what ships clears the shipped index");
    rolla::orig::setBuiltinIndex (nullptr);

    rolla::Groove rootTwoFour {};
    rootTwoFour.dialect = rolla::UkMin;
    rootTwoFour.bars = 8;
    auto& r = rootTwoFour.lane[rolla::Bass];
    for (int bar = 0; bar < 8; ++bar)
        for (int slot : { 1, 3, 8 })
        {
            auto& n = r.notes[r.count++];
            n.start = (float) (bar * 16 + slot);
            n.length = 1.f;
            n.chord[0] = (std::int8_t) (slot == 8 ? 7 : 0);
            n.nChord = 1;
            n.vel = 100;
        }
    expect (rolla::orig::bannedBassFigure (r, 8), "originality: root on 16ths 2 and 4 is the banned 2&4 root figure");
    expect (! rolla::orig::check (rootTwoFour, 1u << rolla::Bass, nullptr).pass, "originality: the 2&4 root figure fails the check");

    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    for (int style = 0; style < rolla::NumDialects; ++style)
    {
        int clean = 0, redrawn = 0;
        for (int roll = 0; roll < 20; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts];
            seedsForRoll (style, roll, seeds);
            rolla::Groove g {};
            if (rolla::generate (g, s, seeds, 0x7fu) && rolla::orig::check (g, 0x3fu, nullptr).pass)
                ++clean;
            if (std::strstr (g.brokenRules, "|orig:") != nullptr)
                ++redrawn;
        }
        std::cout << names[style] << " built-in originality: clean " << clean << "/20, redrawn " << redrawn << "\n";
        expect (clean == 20, std::string (names[style]) + " every roll clears the built-in originality guard");
    }
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

// ---- rule composer: no reference-derived tables, variety, back-half guard ----

void seedsFrom (std::uint32_t base, int style, int roll, std::uint32_t seeds[rolla::NumParts])
{
    for (int p = 0; p < rolla::NumParts; ++p)
        seeds[p] = base + (std::uint32_t) roll * 0x9E3779B9u + (std::uint32_t) p * 17u + (std::uint32_t) style * 101u;
}

std::filesystem::path repoRoot()
{
    namespace fs = std::filesystem;
    const fs::path p = fs::path (__FILE__).parent_path().parent_path().parent_path();
    std::error_code ec;
    if (fs::exists (p / "src" / "Generator", ec))
        return p;
    return {};
}

bool isWordChar (char c)
{
    return std::isalnum ((unsigned char) c) != 0 || c == '_';
}

// Whole-word, case-sensitive occurrences of `word` in `text`.
int countWord (const std::string& text, const std::string& word)
{
    int n = 0;
    for (std::size_t at = text.find (word); at != std::string::npos; at = text.find (word, at + 1))
    {
        const bool left = at == 0 || ! isWordChar (text[at - 1]);
        const bool right = at + word.size() >= text.size() || ! isWordChar (text[at + word.size()]);
        if (left && right)
            ++n;
    }
    return n;
}

// The owner's rule: no reference-derived sequence, cell, skeleton, codebook or
// stab line in the generator. The removed tables and their files must not come
// back into src/ or resources/. Reference-named cells are checked by
// tools/check_repo_clean.py (CI), which keeps only salted digests of the names.
void testRemovedTables()
{
    namespace fs = std::filesystem;
    const fs::path root = repoRoot();
    if (root.empty())
    {
        std::cout << "SKIP removed-table scan: source tree not found from __FILE__ (tools/check_repo_clean.py covers CI)\n";
        return;
    }
    const char* goneFiles[] = {
        "resources/phrase_bank.json", "src/Generator/PhraseBankData.h", "src/Generator/PhraseBank.h",
        "src/Generator/PhraseBank.cpp", "src/Generator/VocabCells.h", "tools/apply_ar_vocab.py",
        "tools/extract_corpus.py", "tools/build_phrase_bank.py",
    };
    for (const char* f : goneFiles)
    {
        std::error_code ec;
        expect (! fs::exists (root / f, ec), std::string ("removed table file is back: ") + f);
    }
    const char* tokens[] = {
        "kPhrase", "kLongPhrase", "kJackinPhrase", "kDeg", "kLongDeg", "kGaskDeg", "PhraseBankData", "VocabCells",
        "baseSlots", "deepBook", "bouncyBook", "jackinBook", "kPrefer0", "kPrefer1", "kPrefer2",
        "pushJackinVariationBack", "lockFromBook", "lockPhrase", "makeBassPhrase", "phrase_bank", "rolesFor", "assignSemi",
    };
    int scanned = 0;
    for (const char* dir : { "src", "resources" })
    {
        std::error_code ec;
        for (fs::recursive_directory_iterator it (root / dir, ec), end; ! ec && it != end; it.increment (ec))
        {
            if (! it->is_regular_file())
                continue;
            const auto ext = it->path().extension().string();
            if (ext != ".h" && ext != ".hpp" && ext != ".cpp" && ext != ".json" && ext != ".txt" && ext != ".inc" && ext != ".py")
                continue;
            const std::string text = fileBytes (it->path().string());
            const std::string rel = fs::relative (it->path(), root, ec).string();
            ++scanned;
            for (const char* t : tokens)
                expect (countWord (text, t) == 0, "removed table symbol '" + std::string (t) + "' is back in " + rel);
        }
    }
    std::cout << "removed-table scan: " << scanned << " source/resource files checked\n";
    expect (scanned > 10, "the removed-table scan saw the source tree");
}

// 2-bar bass rhythm skeletons (the bars 1-2 onset mask of the shipped bass)
// across 4 styles x 50 seeds: no single skeleton in more than 10% of rolls.
void testBassSkeletonDiversity()
{
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    for (int style = 0; style < 4; ++style)
    {
        std::map<std::uint32_t, int> count;
        for (int roll = 0; roll < 50; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts];
            seedsFrom (5150u, style, roll, seeds);
            rolla::Groove g {};
            rolla::generate (g, s, seeds, 0x7fu);
            std::uint32_t mask = 0;
            for (int i = 0; i < g.lane[rolla::Bass].count; ++i)
            {
                const int st = (int) std::lround (g.lane[rolla::Bass].notes[i].start);
                if (st >= 0 && st < 32)
                    mask |= 1u << st;
            }
            ++count[mask];
        }
        int worst = 0;
        for (const auto& kv : count)
            worst = std::max (worst, kv.second);
        std::cout << names[style] << " 2-bar bass skeletons: " << count.size() << " distinct in 50, most common " << worst << "\n";
        expect (worst <= 5, std::string (names[style]) + " no 2-bar bass skeleton in more than 10% of rolls");
    }
}

// 200 rolls a style: distinct 1-bar rhythm (onset mask) and interval patterns
// per lane, and no single pattern dominating. Also the originality redraw
// rate (rolls the backstop had to redraw) and the A&R back-half guard sweep.
void testLanePatternDiversity()
{
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    int redrawn = 0;
    int total = 0;
    for (int style = 0; style < 4; ++style)
    {
        std::map<std::uint32_t, int> bassRhythm;
        std::map<std::string, int> bassIntervals;
        std::map<std::uint32_t, int> stabRhythm;
        int bassBars = 0;
        int intervalBars = 0;
        int stabBars = 0;
        int thin = 0;
        for (int roll = 0; roll < 200; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts];
            seedsFrom (6262u, style, roll, seeds);
            rolla::Groove g {};
            if (! rolla::generate (g, s, seeds, 0x7fu))
                continue;
            ++total;
            if (std::strstr (g.brokenRules, "|orig:") != nullptr)
                ++redrawn;
            if (rolla::rules::thinRepeatedBackHalf (g.lane[rolla::Bass], g.bars))
                ++thin;
            for (int bar = 0; bar < g.bars; ++bar)
            {
                std::uint32_t mask = 0;
                std::string iv;
                int prev = -1000;
                int notes = 0;
                for (int i = 0; i < g.lane[rolla::Bass].count; ++i)
                {
                    const auto& n = g.lane[rolla::Bass].notes[i];
                    const int st = (int) std::lround (n.start);
                    if (st / 16 != bar)
                        continue;
                    mask |= 1u << (st % 16);
                    if (prev > -1000)
                        iv += std::to_string ((int) n.chord[0] - prev) + ",";
                    prev = n.chord[0];
                    ++notes;
                }
                if (mask != 0)
                {
                    ++bassRhythm[mask];
                    ++bassBars;
                }
                if (notes >= 3)
                {
                    ++bassIntervals[iv];
                    ++intervalBars;
                }
                std::uint32_t smask = 0;
                for (int i = 0; i < g.lane[rolla::Stab].count; ++i)
                {
                    const int st = (int) std::lround (g.lane[rolla::Stab].notes[i].start);
                    if (st / 16 == bar)
                        smask |= 1u << (st % 16);
                }
                if (smask != 0)
                {
                    ++stabRhythm[smask];
                    ++stabBars;
                }
            }
        }
        auto top = [] (const auto& m)
        {
            int w = 0;
            for (const auto& kv : m)
                w = std::max (w, kv.second);
            return w;
        };
        const double bassTop = bassBars > 0 ? (double) top (bassRhythm) / bassBars : 1.0;
        const double ivTop = intervalBars > 0 ? (double) top (bassIntervals) / intervalBars : 1.0;
        const double stabTop = stabBars > 0 ? (double) top (stabRhythm) / stabBars : 1.0;
        std::cout << names[style] << " 1-bar patterns over 200 rolls: bass rhythm " << bassRhythm.size() << " distinct (top "
                  << 100.0 * bassTop << "%), bass intervals " << bassIntervals.size() << " (top " << 100.0 * ivTop
                  << "%), stab rhythm " << stabRhythm.size() << " (top " << 100.0 * stabTop << "%), thin back halves " << thin << "\n";
        expect (bassTop <= 0.10, std::string (names[style]) + " no 1-bar bass rhythm takes more than 10% of bars");
        expect (ivTop <= 0.25, std::string (names[style]) + " no 1-bar bass interval pattern takes more than 25% of bars");
        expect (stabTop <= 0.35, std::string (names[style]) + " no 1-bar stab rhythm takes more than 35% of bars");
        expect (thin == 0, std::string (names[style]) + " no shipped roll has a thin repeated back half (A&R guard)");
    }
    const double rate = total > 0 ? (double) redrawn / total : 0.0;
    std::cout << "originality backstop redraw rate " << 100.0 * rate << "% (" << redrawn << "/" << total << ")\n";
    expect (rate <= 0.30, "the composer rarely needs the originality backstop (<= 30% of rolls redrawn)");
}

// ROLLER roll_17 (A&R): bars 5-8 bass on one pitch class at a few offbeat
// 16ths every bar. Seed sweep over every style and three DARK settings: no
// shipped roll carries a thin repeated back half.
void testThinBackHalfSweep()
{
    int shipped = 0;
    int thin = 0;
    for (int style = 0; style < 4; ++style)
        for (float dark : { 0.1f, 0.55f, 0.9f })
            for (int roll = 0; roll < 30; ++roll)
            {
                rolla::Settings s;
                s.dialect = style;
                s.bars = 8;
                s.dark = dark;
                std::uint32_t seeds[rolla::NumParts];
                seedsFrom (171717u, style, roll, seeds);
                rolla::Groove g {};
                if (! rolla::generate (g, s, seeds, 0x7fu))
                    continue;
                ++shipped;
                if (rolla::rules::thinRepeatedBackHalf (g.lane[rolla::Bass], g.bars))
                    ++thin;
            }
    std::cout << "back-half guard sweep: " << thin << " thin of " << shipped << " shipped\n";
    expect (shipped > 300 && thin == 0, "back-half guard: no thin repeated back half ships");
}

// Offline n-gram / skeleton overlap against the private reference index. Runs
// only when ROLLA_ORIG_INDEX names a loadable index; skips cleanly otherwise.
void testOfflineIndexOverlap()
{
    const char* env = std::getenv ("ROLLA_ORIG_INDEX");
    if (env == nullptr || *env == '\0' || ! juce::File::isAbsolutePath (juce::String (env)))
    {
        std::cout << "SKIP offline index overlap: ROLLA_ORIG_INDEX is not set\n";
        return;
    }
    const auto index = rolla::orig::loadIndex (juce::File (juce::String (env)));
    if (index == nullptr)
    {
        std::cout << "SKIP offline index overlap: no loadable index at " << env << "\n";
        return;
    }
    const char* names[] = { "ROLLER", "DEEP", "BOUNCY", "JACKIN" };
    for (int style = 0; style < 4; ++style)
    {
        int raw = 0;
        int shippedFail = 0;
        for (int roll = 0; roll < 200; ++roll)
        {
            rolla::Settings s;
            s.dialect = style;
            s.bars = 8;
            std::uint32_t seeds[rolla::NumParts];
            seedsFrom (8080u, style, roll, seeds);
            rolla::orig::setIndex (nullptr);
            rolla::Groove plain {};
            if (rolla::generate (plain, s, seeds, 0x7fu) && ! rolla::orig::check (plain, 0x3fu, index.get()).pass)
                ++raw;
            rolla::orig::setIndex (index);
            rolla::Groove guarded {};
            if (rolla::generate (guarded, s, seeds, 0x7fu) && ! rolla::orig::check (guarded, 0x3fu, index.get()).pass)
                ++shippedFail;
        }
        rolla::orig::setIndex (nullptr);
        std::cout << names[style] << " offline index overlap: composer hits " << raw << "/200, shipped hits " << shippedFail << "/200\n";
        expect (shippedFail == 0, std::string (names[style]) + " nothing that ships overlaps the private index");
        expect (raw <= 20, std::string (names[style]) + " the composer itself overlaps the index in <= 10% of rolls");
    }
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

    // First: these reproduce a fresh rolla-rolls process (generate() keeps a
    // per-style rhythm history for the process).
    testBatchRollerKey();
    testBatchBouncyModes();
    testBatchJackinLoop();
    testEarReview();
    testScorerV23();
    testAr423Bass();
    testBouncyModes();
    testAr423Swing();
    testHarmonyClashes();
    testLaneHygiene();
    testStabDensity();
    testRedrawLog();
    testBar8Pickup();
    testArGateBass();
    testOriginalityBuiltin();
    testOriginalityResample();
    testOriginalityTerminates();
    testOriginalityRefusalNeverShips();
    testRollSkipsMutedAndKept();
    testShuffleClearsAutoKeep();
    testShuffleKeepsManualLock();
    testIntensityChangesOutput();
    // Rule composer gates. Last, so the per-process rhythm history the earlier
    // (batch-reproducing) tests rely on is untouched.
    testRemovedTables();
    testBassSkeletonDiversity();
    testLanePatternDiversity();
    testThinBackHalfSweep();
    testOfflineIndexOverlap();

    if (fails == 0)
        std::cout << "roll tests passed\n";
    else
        std::cerr << fails << " roll test failure(s)\n";
    return fails == 0 ? 0 : 1;
}
