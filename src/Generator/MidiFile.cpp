#include "MidiFile.h"

#include "../DspUtil.h"

#include <algorithm>
#include <cmath>
#include <fstream>

namespace rolla
{
namespace
{

void pushByte (std::vector<std::uint8_t>& b, std::uint8_t v) { b.push_back (v); }

void pushU16 (std::vector<std::uint8_t>& b, int v)
{
    pushByte (b, (std::uint8_t) ((v >> 8) & 0xff));
    pushByte (b, (std::uint8_t) (v & 0xff));
}

void pushU32 (std::vector<std::uint8_t>& b, std::uint32_t v)
{
    pushByte (b, (std::uint8_t) ((v >> 24) & 0xff));
    pushByte (b, (std::uint8_t) ((v >> 16) & 0xff));
    pushByte (b, (std::uint8_t) ((v >> 8) & 0xff));
    pushByte (b, (std::uint8_t) (v & 0xff));
}

void pushVlq (std::vector<std::uint8_t>& b, std::uint32_t n)
{
    std::uint8_t tmp[5];
    int i = 0;
    tmp[i++] = (std::uint8_t) (n & 0x7f);
    while (n >>= 7)
        tmp[i++] = (std::uint8_t) ((n & 0x7f) | 0x80);
    while (i--)
        b.push_back (tmp[i]);
}

struct Event
{
    std::uint32_t tick;
    std::uint8_t status;
    std::uint8_t d1;
    std::uint8_t d2;
    bool operator< (const Event& o) const
    {
        if (tick != o.tick)
            return tick < o.tick;
        // note-offs before note-ons at the same tick
        return status > o.status;
    }
};

int basePitch (int part, int key, int dialect)
{
    const int k = (key % 12 + 12) % 12;
    // Every factory bass preset plays octave 1 (BOUNCY's "click" too), so the
    // file holds the bass where it sounds: D1 = 26 in D. BOUNCY used to write
    // octave 2, a register above what played.
    (void) dialect;
    if (part == Bass)
        return 12 * (1 + 1) + k;
    if (part == Pluck)
        return 72 + k;
    if (part == Vox)
        return 64 + k;
    return 60 + k;
}

void addNote (std::vector<Event>& ev, int channel, int pitch, int vel, std::uint32_t on, std::uint32_t off)
{
    // Velocity 1-2 is a placeholder (the old beat-1 anchor). It does not ship.
    if (vel <= 2)
        return;
    pitch = std::clamp (pitch, 0, 127);
    vel = std::clamp (vel, 1, 127);
    if (off <= on)
        off = on + 1;
    ev.push_back ({ on, (std::uint8_t) (0x90 | (channel & 15)), (std::uint8_t) pitch, (std::uint8_t) vel });
    ev.push_back ({ off, (std::uint8_t) (0x80 | (channel & 15)), (std::uint8_t) pitch, 0 });
}

bool noteOnTaken (const std::vector<Event>& ev, std::uint32_t tick, int pitch)
{
    for (const auto& e : ev)
        if (e.tick == tick && (e.status & 0xf0) == 0x90 && e.d1 == (std::uint8_t) pitch)
            return true;
    return false;
}

std::vector<std::uint8_t> trackBytes (const std::vector<Event>& events, double bpm, int bars, bool withTempo)
{
    std::vector<Event> ev = events;
    std::sort (ev.begin(), ev.end());
    std::vector<std::uint8_t> trk;
    if (withTempo)
    {
        const int us = (int) std::clamp (60000000.0 / std::max (1.0, bpm), 1.0, 16777215.0);
        pushByte (trk, 0);
        pushByte (trk, 0xff);
        pushByte (trk, 0x51);
        pushByte (trk, 0x03);
        pushByte (trk, (std::uint8_t) ((us >> 16) & 0xff));
        pushByte (trk, (std::uint8_t) ((us >> 8) & 0xff));
        pushByte (trk, (std::uint8_t) (us & 0xff));
        pushByte (trk, 0);
        pushByte (trk, 0xff);
        pushByte (trk, 0x58);
        pushByte (trk, 0x04);
        pushByte (trk, 0x04);
        pushByte (trk, 0x02);
        pushByte (trk, 24);
        pushByte (trk, 8);
        // Marker at tick 0 so FL's "realign events" has something on the downbeat.
        const char* marker = "ROLLA";
        pushByte (trk, 0);
        pushByte (trk, 0xff);
        pushByte (trk, 0x06);
        pushByte (trk, 5);
        for (int i = 0; i < 5; ++i)
            pushByte (trk, (std::uint8_t) marker[i]);
    }
    constexpr int ppq = 480;
    const std::uint32_t endTick = (std::uint32_t) (bars * 4 * ppq);
    std::uint32_t last = 0;
    for (const auto& e : ev)
    {
        const auto tick = std::min (e.tick, endTick > 0 ? endTick - 1 : 0);
        pushVlq (trk, tick - last);
        pushByte (trk, e.status);
        pushByte (trk, e.d1);
        pushByte (trk, e.d2);
        last = tick;
    }
    pushVlq (trk, endTick > last ? endTick - last : 0);
    pushByte (trk, 0xff);
    pushByte (trk, 0x2f);
    pushByte (trk, 0x00);
    return trk;
}

void wrapTrack (std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& trk)
{
    pushByte (out, 'M');
    pushByte (out, 'T');
    pushByte (out, 'r');
    pushByte (out, 'k');
    pushU32 (out, (std::uint32_t) trk.size());
    out.insert (out.end(), trk.begin(), trk.end());
}

std::vector<std::uint8_t> smf (const std::vector<std::vector<std::uint8_t>>& tracks)
{
    std::vector<std::uint8_t> out;
    pushByte (out, 'M');
    pushByte (out, 'T');
    pushByte (out, 'h');
    pushByte (out, 'd');
    pushU32 (out, 6);
    pushU16 (out, tracks.size() > 1 ? 1 : 0);
    pushU16 (out, (int) tracks.size());
    pushU16 (out, 480);
    for (const auto& t : tracks)
        wrapTrack (out, t);
    return out;
}

std::uint32_t tickAt (float start16, float swing)
{
    constexpr int ticksPer16 = 120; // 480 PPQ
    const float s = swungStart (start16, swing);
    return (std::uint32_t) std::max (0, (int) std::round (s * ticksPer16));
}

void collectPart (std::vector<Event>& ev, const Groove& g, int part, int key, float swing)
{
    const int loopTicks = g.bars * 4 * 480;
    if (part == Drums)
    {
        const int steps = g.bars * 16;
        for (int v = 0; v < NumDrumVoices; ++v)
        {
            for (int t = 0; t < steps; ++t)
            {
                const int vel = g.drums.vel[v][t];
                if (vel <= 2)
                    continue;
                const auto on = tickAt ((float) t, swing);
                int gate = 60; // half a 16th
                if (v == HatO || v == Ride)
                    gate = 200;
                if (v == Kick)
                    gate = 80;
                auto off = on + (std::uint32_t) gate;
                if ((int) off >= loopTicks)
                    off = (std::uint32_t) (loopTicks - 1);
                int accented = g.drums.accent[t] ? std::min (127, vel + 6) : vel;
                const int pitch = drumMidiNote (v);
                if (noteOnTaken (ev, on, pitch))
                    continue;
                addNote (ev, 9, pitch, accented, on, off);
            }
        }
        return;
    }

    const int channel = part; // 0..5
    const int base = basePitch (part, key, g.dialect);
    const auto& lane = g.lane[part];

    struct Hit
    {
        std::uint32_t on = 0;
        std::uint32_t off = 0;
        int pitch = 0;
        int vel = 0;
    };
    std::vector<Hit> hits;
    for (int i = 0; i < lane.count; ++i)
    {
        const auto& n = lane.notes[i];
        if (n.vel <= 2)
            continue;
        const auto on = tickAt (n.start, swing);
        auto off = on + (std::uint32_t) std::max (1, (int) std::round (n.length * 120.f));
        if ((int) off >= loopTicks)
            off = (std::uint32_t) std::max (1, loopTicks - 1);
        const int count = std::max (1, (int) n.nChord);
        for (int c = 0; c < count; ++c)
        {
            const int pitch = std::clamp (base + n.chord[c], 0, 127);
            bool taken = false;
            for (const auto& h : hits)
                if (h.on == on && h.pitch == pitch)
                    taken = true;
            if (taken)
                continue;
            hits.push_back ({ on, off, pitch, n.vel });
        }
    }

    if (part == Bass && hits.size() > 1)
    {
        std::sort (hits.begin(), hits.end(), [] (const Hit& a, const Hit& b)
        {
            if (a.on != b.on)
                return a.on < b.on;
            return a.vel > b.vel;
        });
        // One bass pitch at a time. The note ends a tick before the next onset.
        std::vector<Hit> mono;
        for (const auto& h : hits)
        {
            if (! mono.empty() && mono.back().on == h.on)
                continue;
            mono.push_back (h);
        }
        for (std::size_t i = 0; i + 1 < mono.size(); ++i)
        {
            if (mono[i + 1].on <= mono[i].on)
                continue;
            const auto limit = mono[i + 1].on - 1;
            if (mono[i].off > limit)
                mono[i].off = std::max (mono[i].on + 1, limit);
        }
        hits.swap (mono);
    }

    for (const auto& h : hits)
        addNote (ev, channel, h.pitch, h.vel, h.on, h.off);
}

bool writeFile (const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error)
{
    std::ofstream out (path, std::ios::binary);
    if (! out)
    {
        error = "could not write " + path;
        return false;
    }
    out.write (reinterpret_cast<const char*> (bytes.data()), (std::streamsize) bytes.size());
    return out.good();
}

std::vector<Event> laneEvents (const Groove& groove, int part, int key, float swing)
{
    std::vector<Event> ev;
    collectPart (ev, groove, part, key, swing);
    return ev;
}

} // namespace

bool MidiExport::writePart (const std::string& path,
                           const Groove& groove,
                           int part,
                           int key,
                           float swing,
                           double bpm,
                           std::string& error)
{
    if (part < 0 || part >= NumParts)
    {
        error = "bad lane";
        return false;
    }
    auto bytes = smf ({ trackBytes (laneEvents (groove, part, key, swing), bpm, groove.bars, true) });
    return writeFile (path, bytes, error);
}

bool MidiExport::writeMultitrack (const std::string& path,
                                 const Groove& groove,
                                 int key,
                                 float swing,
                                 double bpm,
                                 std::string& error)
{
    std::vector<std::vector<std::uint8_t>> tracks;
    {
        std::vector<Event> none;
        tracks.push_back (trackBytes (none, bpm, groove.bars, true));
    }
    for (int part = 0; part < NumParts; ++part)
        tracks.push_back (trackBytes (laneEvents (groove, part, key, swing), bpm, groove.bars, false));
    return writeFile (path, smf (tracks), error);
}

bool MidiExport::writeAll (const std::string& directory,
                           const Groove& groove,
                           int key,
                           float swing,
                           double bpm,
                           std::vector<std::string>& writtenPaths,
                           std::string& error)
{
    writtenPaths.clear();
    const std::string keyTag = keyName (key);
    const std::string dial = dialectId (groove.dialect);
    auto pathFor = [&] (const std::string& suffix)
    {
        return directory + "/ROLLA_" + keyTag + "min_" + dial + "_" + suffix + ".mid";
    };

    std::vector<std::vector<std::uint8_t>> combined;
    {
        std::vector<Event> none;
        combined.push_back (trackBytes (none, bpm, groove.bars, true));
    }

    for (int p = 0; p < NumParts; ++p)
    {
        if (p != Drums && groove.lane[p].count <= 0)
            continue;
        const auto ev = laneEvents (groove, p, key, swing);
        auto bytes = smf ({ trackBytes (ev, bpm, groove.bars, true) });
        const auto path = pathFor (partId (p));
        if (! writeFile (path, bytes, error))
            return false;
        writtenPaths.push_back (path);
        combined.push_back (trackBytes (ev, bpm, groove.bars, false));
    }

    auto all = smf (combined);
    const auto allPath = pathFor ("all");
    if (! writeFile (allPath, all, error))
        return false;
    writtenPaths.push_back (allPath);
    return true;
}

} // namespace rolla
