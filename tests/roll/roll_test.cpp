#include "PluginProcessor.h"

#include <juce_events/juce_events.h>

#include <cstdint>
#include <functional>
#include <iostream>
#include <string>

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
        return proc.copyGroove().lane[rolla::Pluck].count > 0;
    }), "intensity 100 brings pluck back");
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
