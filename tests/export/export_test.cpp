// rolla-export-test: what the plugin exports is what it plays.
//
//   rolla-export-test [--samples DIR] [--only PART]
//
// With --samples (CI builds a tone pack with tests/roll/renderer_smoke.py's
// build_pack, nothing committed) it also checks that stems are not silent.
//
// --only runs one slice, so CTest can run the slices in parallel (each one well
// under the timeout). Without it everything runs in one process, as before.
//   midi          every style: full MIDI == played notes, host-loop checks
//                 (incl. "some style has bars 5-8 unlike 1-4"), lane MIDI
//   audio:STYLE   one style (ROLLER, DEEP, BOUNCY, JACKIN): stems, every lane
//                 WAV, folder export
//   vox:A-B       vox presets A..B (inclusive; B = end: to the last preset),
//                 each right after its pick
//   length2       LENGTH 2 on an 8-bar groove, then a fresh 2-bar roll (all checks)
// Each run works in its own scratch profile (pid + time stamp), never a shared
// temp folder, so parallel runs never collide.
// For every style it ROLLs, then, without playing first:
//  - writes the full multitrack MIDI and compares every note-on with the
//    pattern MIDI processBlock sends to the host over the same loop
//    (channel, note, velocity, tick within 4 of 480 PPQ)
//  - checks MIDI hygiene: no velocity <= 2, no zero-length notes, nothing
//    past the loop, one tempo, the name carries style / key / bpm / lane / seed
//  - bounces the 7 stems: exact loop length, stereo 48 kHz, the bar-1 kick
//    sounds at sample 0, lanes with notes are not silent
//  - exportMidiTo / exportStemsTo write lanes + full into a folder
// Then the same at LENGTH 2 bars for one style.

#include "PluginProcessor.h"
#include "Engine/RollaSettings.h"
#include "Export/RollExport.h"
#include "../common/ScratchProfile.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <thread>
#include <vector>


#if JUCE_LINUX
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue (bool returnIfNoPendingMessages);
}
#endif

namespace
{
int fails = 0;
constexpr double kRate = 48000.0;
constexpr double kBpm = 130.0;

void expect (bool ok, const std::string& msg)
{
    std::cout << (ok ? "PASS " : "FAIL ") << msg << "\n";
    if (! ok)
        ++fails;
}

bool pumpUntil (const std::function<bool()>& done, int ms = 6000)
{
    const auto start = juce::Time::getMillisecondCounter();
    while (! done() && juce::Time::getMillisecondCounter() - start < (juce::uint32) ms)
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

void setDenorm (rolla::RollaProcessor& proc, const char* id, float value)
{
    if (auto* p = proc.apvts.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    else
        expect (false, std::string ("parameter ") + id);
}

bool reshapeTo (rolla::RollaProcessor& proc, const char* id, float value)
{
    const auto before = proc.copyGroove().generation;
    setDenorm (proc, id, value);
    return pumpUntil ([&] { return proc.copyGroove().generation != before; });
}

// How the fake host moves its playhead.
struct HostSim
{
    double wrapBeats = 0.0;  // > 0: the reported ppq wraps every wrapBeats, like FL's pattern loop
    bool loopPoints = false; // also report isLooping and loop points [0, wrapBeats)
    int stopAt = -1;         // >= 0: stop here for a moment, then restart from ppq 0
};

struct Head : juce::AudioPlayHead
{
    double ppq = 0.0;
    bool playing = true;
    HostSim sim;
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override
    {
        juce::AudioPlayHead::PositionInfo info;
        info.setBpm (kBpm);
        info.setIsPlaying (playing);
        info.setPpqPosition (sim.wrapBeats > 0.0 ? std::fmod (ppq, sim.wrapBeats) : ppq);
        if (sim.wrapBeats > 0.0 && sim.loopPoints)
        {
            juce::AudioPlayHead::LoopPoints loop;
            loop.ppqStart = 0.0;
            loop.ppqEnd = sim.wrapBeats;
            info.setIsLooping (true);
            info.setLoopPoints (loop);
        }
        return info;
    }
};

struct NoteOn
{
    int channel, note, vel;
    double tick;
};

// Pattern MIDI the plugin sends while it plays one loop, in 480-PPQ ticks.
// processBlock delays it by the plugin latency, so that is taken back out.
// With sim.stopAt the loop is counted from the restart.
std::vector<NoteOn> playedNotes (rolla::RollaProcessor& proc, int loopN, const HostSim& sim = {})
{
    Head head;
    head.sim = sim;
    proc.setPlayHead (&head);
    proc.releaseResources();
    juce::AudioProcessor::BusesLayout layout;
    layout.outputBuses.add (juce::AudioChannelSet::stereo());
    layout.outputBuses.add (juce::AudioChannelSet::disabled());
    proc.setBusesLayout (layout);
    proc.prepareToPlay (kRate, 256);
    const int lat = proc.getLatencySamples();
    std::vector<NoteOn> out;
    juce::AudioBuffer<float> buffer (2, 256);
    int origin = 0;
    bool restarted = false;
    for (int done = 0; done < origin + loopN + lat + 256;)
    {
        const int n = 256;
        if (sim.stopAt >= 0 && ! restarted && done >= sim.stopAt)
        {
            restarted = true;
            // Stop long enough to flush the delayed MIDI, then play from the top.
            head.playing = false;
            for (int stopped = 0; stopped < lat + 1024; stopped += n)
            {
                juce::MidiBuffer midi;
                proc.processBlock (buffer, midi);
            }
            head.playing = true;
            head.ppq = 0.0;
            out.clear();
            origin = done;
        }
        juce::MidiBuffer midi;
        proc.processBlock (buffer, midi);
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const int at = done - origin + meta.samplePosition - lat;
            if (! m.isNoteOn() || at < 0 || at >= loopN)
                continue;
            const double tick = at * kBpm / 60.0 / kRate * 480.0;
            // The next loop's downbeat can reach the host a sample before the wrap.
            if (tick > (double) loopN * kBpm / 60.0 / kRate * 480.0 - 2.0)
                continue;
            out.push_back ({ m.getChannel(), m.getNoteNumber(), m.getVelocity(), tick });
        }
        head.ppq += (double) n * kBpm / (60.0 * kRate);
        done += n;
    }
    proc.setPlayHead (nullptr);
    return out;
}

struct FileNotes
{
    std::vector<NoteOn> ons;
    int badVel = 0, zeroLen = 0, pastLoop = 0, tempos = 0, tracks = 0, format = -1;
};

FileNotes readMidi (const juce::File& file, int loopTicks)
{
    FileNotes r;
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
        return r;
    r.tracks = mf.getNumTracks();
    juce::MemoryBlock head;
    file.loadFileAsData (head);
    if (head.getSize() > 10)
        r.format = (int) static_cast<const unsigned char*> (head.getData())[9];
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        auto seq = *mf.getTrack (t);
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto* e = seq.getEventPointer (i);
            const auto& m = e->message;
            if (m.isTempoMetaEvent())
                ++r.tempos;
            if (! m.isNoteOn())
                continue;
            if (m.getVelocity() <= 2)
                ++r.badVel;
            const double on = m.getTimeStamp();
            const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() : on;
            if (off <= on)
                ++r.zeroLen;
            if (on >= loopTicks || off > loopTicks)
                ++r.pastLoop;
            r.ons.push_back ({ m.getChannel(), m.getNoteNumber(), m.getVelocity(), on });
        }
    }
    return r;
}

// Greedy match within 4 ticks on (channel, note, velocity). Returns misses each way.
void matchNotes (const std::vector<NoteOn>& file, const std::vector<NoteOn>& played, int& onlyFile, int& onlyPlayed,
                 std::string& example)
{
    std::vector<bool> used (played.size(), false);
    onlyFile = 0;
    for (const auto& f : file)
    {
        int best = -1;
        double bestD = 4.01;
        for (size_t i = 0; i < played.size(); ++i)
        {
            const auto& p = played[i];
            if (used[i] || p.channel != f.channel || p.note != f.note)
                continue;
            const double d = std::abs (p.tick - f.tick);
            if (d < bestD)
            {
                bestD = d;
                best = (int) i;
            }
        }
        if (best < 0 || played[(size_t) best].vel != f.vel)
        {
            if (++onlyFile == 1)
                example = "file ch " + std::to_string (f.channel) + " note " + std::to_string (f.note) + " vel "
                          + std::to_string (f.vel) + " tick " + std::to_string ((int) f.tick)
                          + (best >= 0 ? " (played vel " + std::to_string (played[(size_t) best].vel) + ")" : " (not played)");
            if (best >= 0)
                used[(size_t) best] = true;
            continue;
        }
        used[(size_t) best] = true;
    }
    onlyPlayed = 0;
    for (size_t i = 0; i < played.size(); ++i)
        if (! used[i])
        {
            if (++onlyPlayed == 1 && example.empty())
                example = "played ch " + std::to_string (played[i].channel) + " note " + std::to_string (played[i].note)
                          + " tick " + std::to_string ((int) played[i].tick) + " (not in file)";
        }
}

struct Wav
{
    int channels = 0, length = 0;
    double rate = 0.0;
    float peak = 0.f;
    int firstLoud = -1; // first sample above -30 dB re the file peak
};

Wav readWav (const juce::File& file)
{
    Wav w;
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
        return w;
    w.channels = (int) reader->numChannels;
    w.length = (int) reader->lengthInSamples;
    w.rate = reader->sampleRate;
    juce::AudioBuffer<float> a ((int) reader->numChannels, w.length);
    reader->read (&a, 0, w.length, 0, true, true);
    for (int ch = 0; ch < a.getNumChannels(); ++ch)
        w.peak = std::max (w.peak, a.getMagnitude (ch, 0, w.length));
    const float thr = w.peak * 0.0316f;
    for (int i = 0; i < w.length && w.firstLoud < 0 && w.peak > 0.f; ++i)
        for (int ch = 0; ch < a.getNumChannels(); ++ch)
            if (std::abs (a.getSample (ch, i)) > thr)
            {
                w.firstLoud = i;
                break;
            }
    return w;
}

bool nameOk (const juce::File& f, const rolla::Groove& g, int part);

// Absolute tick of the first track's end-of-track meta event, -1 if unreadable.
long endOfTrackTick (const juce::File& file)
{
    juce::MemoryBlock block;
    if (! file.loadFileAsData (block) || block.getSize() < 22)
        return -1;
    const auto* d = static_cast<const unsigned char*> (block.getData());
    const size_t size = block.getSize();
    size_t pos = 14;
    if (d[pos] != 'M' || d[pos + 1] != 'T' || d[pos + 2] != 'r' || d[pos + 3] != 'k')
        return -1;
    const size_t len = ((size_t) d[pos + 4] << 24) | ((size_t) d[pos + 5] << 16) | ((size_t) d[pos + 6] << 8) | (size_t) d[pos + 7];
    pos += 8;
    const size_t end = std::min (size, pos + len);
    long tick = 0;
    int status = 0;
    auto vlq = [&] () -> long
    {
        long v = 0;
        for (int i = 0; i < 4 && pos < end; ++i)
        {
            const int b = d[pos++];
            v = (v << 7) | (b & 0x7f);
            if ((b & 0x80) == 0)
                break;
        }
        return v;
    };
    while (pos < end)
    {
        tick += vlq();
        if (pos >= end)
            break;
        if ((d[pos] & 0x80) != 0)
            status = d[pos++];
        if (status == 0xff)
        {
            if (pos >= end)
                break;
            const int type = d[pos++];
            const long n = vlq();
            if (type == 0x2f)
                return tick;
            pos += (size_t) n;
            status = 0;
        }
        else if (status == 0xf0 || status == 0xf7)
            pos += (size_t) vlq();
        else if ((status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0)
            pos += 1;
        else
            pos += 2;
    }
    return -1;
}

int lengthBars (rolla::RollaProcessor& proc)
{
    return rolla::barsFromIndex ((int) proc.apvts.getRawParameterValue ("length")->load());
}

// Full MIDI vs what processBlock plays over one loop of LENGTH bars.
// Returns the played notes (empty if there was no file to compare against), so
// the host-loop checks reuse this straight-host render instead of making the
// same one again.
std::vector<NoteOn> compareMidi (rolla::RollaProcessor& proc, const std::string& tag, const rolla::Groove& g, int bars)
{
    const int loopTicks = bars * 4 * 480;
    const int loopN = rolla::rollexport::loopSamples (bars, kBpm, kRate);
    const auto mids = proc.writeTempMidi();
    expect (mids.size() == 1, tag + ": one full MIDI file");
    if (mids.isEmpty())
        return {};
    const juce::File full (mids[0]);
    expect (nameOk (full, g, rolla::rollexport::kFull), tag + ": full MIDI name " + full.getFileName().toStdString());
    const auto fn = readMidi (full, loopTicks);
    expect (fn.format == 1 && fn.tracks == rolla::NumParts + 1 && fn.tempos == 1, tag + ": type-1, tempo track + 7 lanes");
    expect (fn.badVel == 0 && fn.zeroLen == 0 && fn.pastLoop == 0,
            tag + ": MIDI hygiene (vel<=2 " + std::to_string (fn.badVel) + ", zero-length " + std::to_string (fn.zeroLen)
                + ", past loop " + std::to_string (fn.pastLoop) + ")");
    const auto played = playedNotes (proc, loopN);
    int onlyFile = 0, onlyPlayed = 0;
    std::string example;
    matchNotes (fn.ons, played, onlyFile, onlyPlayed, example);
    expect (onlyFile == 0 && onlyPlayed == 0 && ! fn.ons.empty(),
            tag + ": exported notes == played notes (" + std::to_string (fn.ons.size()) + " in file, "
                + std::to_string (played.size()) + " played, " + std::to_string (onlyFile) + " only in file, "
                + std::to_string (onlyPlayed) + " only played) " + example);
    return played;
}

// Styles whose bars 5-8 differ from bars 1-4, so the host-loop checks below
// would catch a phrase that restarts every 4 bars.
int distinctHalves = 0;

// FL Studio loops its pattern: the host plays on while its ppq wraps every 4
// bars. ROLLA plays its own phrase through the wrap, so an 8-bar roll sends the
// same notes as under a straight host, bars 5-8 included. A stop and restart
// goes back to bar 1.
// `straightPlayed` is compareMidi's straight-host render of the same loop: playedNotes
// re-prepares the processor every call, and the checks below already rely on
// two calls giving the same notes, so a second straight render is redundant.
// Empty (compareMidi had no file): render it here.
void testHostLoop (rolla::RollaProcessor& proc, const std::string& tag, int bars, const std::vector<NoteOn>& straightPlayed)
{
    if (bars != 8)
        return;
    const int loopN = rolla::rollexport::loopSamples (8, kBpm, kRate);
    const double half = 4.0 * 4.0 * 480.0;
    auto lateCount = [&] (const std::vector<NoteOn>& v)
    {
        int c = 0;
        for (const auto& x : v)
            c += x.tick >= half - 4.0;
        return c;
    };
    const auto straight = straightPlayed.empty() ? playedNotes (proc, loopN) : straightPlayed;
    const int late = lateCount (straight);
    {
        std::vector<NoteOn> first, second;
        for (auto x : straight)
        {
            if (x.tick >= half - 4.0)
            {
                x.tick -= half;
                second.push_back (x);
            }
            else
                first.push_back (x);
        }
        int a = 0, b = 0;
        std::string unused;
        matchNotes (first, second, a, b, unused);
        distinctHalves += a + b > 0;
    }
    auto check = [&] (const HostSim& sim, const std::string& what)
    {
        const auto played = playedNotes (proc, loopN, sim);
        int onlyStraight = 0, onlyPlayed = 0;
        std::string example;
        matchNotes (straight, played, onlyStraight, onlyPlayed, example);
        expect (onlyStraight == 0 && onlyPlayed == 0 && late > 0 && lateCount (played) == late,
                tag + ": " + what + " (" + std::to_string (lateCount (played)) + " of " + std::to_string (late)
                    + " bar 5-8 notes, " + std::to_string (onlyStraight) + " missing, " + std::to_string (onlyPlayed)
                    + " extra) " + example);
    };
    check ({ 16.0, false, -1 }, "8 bars play through a 4-bar host loop (no loop points)");
    check ({ 16.0, true, -1 }, "8 bars play through a 4-bar host loop (loop points)");
    check ({ 16.0, false, loopN * 13 / 16 }, "stop and restart in host loop 2 goes back to bar 1");
}

bool nameOk (const juce::File& f, const rolla::Groove& g, int part)
{
    const auto name = f.getFileNameWithoutExtension();
    const juce::String lane = part < 0 ? "full" : rolla::partId (part);
    return name.startsWith ("ROLLA_" + juce::String (rolla::kStyleLabels[g.dialect]) + "_")
           && name.contains ("min_") && name.contains ("bpm_" + lane + "_")
           && name.fromLastOccurrenceOf ("_", false, false).length() == 8;
}

// The 7 stems (drag-all), straight after the roll, before anything has played.
void testStems (rolla::RollaProcessor& proc, const std::string& tag, const rolla::Groove& g, int bars, bool pack)
{
    const int loopN = rolla::rollexport::loopSamples (bars, kBpm, kRate);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto stems = proc.writeStemAudio();
    std::cout << "INFO " << tag << ": 7 stems bounced in " << (int) (juce::Time::getMillisecondCounterHiRes() - t0) << " ms\n";
    expect (stems.size() == rolla::NumParts, tag + ": 7 stems");
    int silent = 0;
    std::string silentNames;
    for (int i = 0; i < stems.size(); ++i)
    {
        const juce::File f (stems[i]);
        const auto w = readWav (f);
        const std::string lane = rolla::partId (i);
        expect (w.channels == 2 && w.rate == kRate && w.length == loopN,
                tag + " " + lane + " stem: stereo 48 kHz, exactly " + std::to_string (bars) + " bars ("
                    + std::to_string (w.length) + " vs " + std::to_string (loopN) + " samples)");
        expect (nameOk (f, g, i), tag + " " + lane + " stem name " + f.getFileName().toStdString());
        const bool hasNotes = i == rolla::Drums || g.lane[i].count > 0;
        // A lane the user (or the style default) muted never sounds, stems included.
        const bool muted = proc.apvts.getRawParameterValue ((lane + "Mute").c_str())->load() >= 0.5f;
        if (pack && muted)
            expect (w.peak < 1.0e-4f, tag + " " + lane + ": muted lane stem is silent");
        if (pack && hasNotes && ! muted && w.peak < 1.0e-4f)
        {
            ++silent;
            silentNames += lane + " ";
        }
        if (i == rolla::Drums && pack && g.drums.vel[rolla::Kick][0] > 0)
            expect (w.firstLoud >= 0 && w.firstLoud <= 48,
                    tag + ": bar-1 kick sounds at the start of the drum stem (first sound at sample "
                        + std::to_string (w.firstLoud) + ")");
        if (pack)
            expect (w.peak <= 0.8913f, tag + " " + lane + " stem under the -1 dBFS sample-true safety ceiling");
    }
    if (pack)
        expect (silent == 0, tag + ": no silent stems right after a roll " + silentNames);
}

// Full multitrack MIDI vs what processBlock plays, host loops, lane MIDI.
// Returns false if the full MIDI file could not be written (the rest is skipped).
bool testMidiExports (rolla::RollaProcessor& proc, const std::string& tag, const rolla::Groove& g, int bars)
{
    const int loopTicks = bars * 4 * 480;
    const auto straight = compareMidi (proc, tag, g, bars);
    testHostLoop (proc, tag, bars, straight);
    const auto mids = proc.writeTempMidi();
    if (mids.isEmpty())
        return false;
    const auto fn = readMidi (juce::File (mids[0]), loopTicks);

    // Lane MIDI = that lane's track of the full file.
    for (int part : { (int) rolla::Bass, (int) rolla::Stab, (int) rolla::Drums })
    {
        const auto lane = proc.writeLaneMidi (part);
        if (lane.isEmpty())
        {
            expect (false, tag + ": lane MIDI " + rolla::partId (part));
            continue;
        }
        const juce::File lf (lane[0]);
        const auto ln = readMidi (lf, loopTicks);
        int inFull = 0;
        const int ch = part == rolla::Drums ? 10 : part + 1;
        for (const auto& n : fn.ons)
            inFull += n.channel == ch;
        expect (nameOk (lf, g, part) && ln.tracks == 1 && (int) ln.ons.size() == inFull,
                tag + ": " + rolla::partId (part) + " lane MIDI matches its track (" + std::to_string (ln.ons.size())
                    + " vs " + std::to_string (inFull) + ") " + lf.getFileName().toStdString());
    }

    // Every lane as its own MIDI file (Shift-drag): 7 files, one folder, each
    // one track, named per lane, exactly one loop long, = that lane's track.
    {
        const auto laneFiles = proc.writeLaneMidiFiles();
        expect (laneFiles.size() == rolla::NumParts, tag + ": one MIDI file per lane (" + std::to_string (laneFiles.size()) + ")");
        for (int part = 0; part < laneFiles.size() && part < rolla::NumParts; ++part)
        {
            const juce::File lf (laneFiles[part]);
            const std::string lane = rolla::partId (part);
            const auto ln = readMidi (lf, loopTicks);
            int inFull = 0;
            const int ch = part == rolla::Drums ? 10 : part + 1;
            for (const auto& n : fn.ons)
                inFull += n.channel == ch;
            expect (lf.getParentDirectory() == juce::File (laneFiles[0]).getParentDirectory(),
                    tag + " " + lane + ": lane MIDI files share one drag folder");
            expect (nameOk (lf, g, part) && ln.tracks == 1 && ln.tempos == 1 && (int) ln.ons.size() == inFull
                        && ln.pastLoop == 0 && ln.badVel == 0,
                    tag + " " + lane + " lane MIDI file: " + std::to_string (ln.ons.size()) + " notes vs "
                        + std::to_string (inFull) + " in the full file, " + lf.getFileName().toStdString());
            // The track ends on the loop end, so FL sizes the pattern to the loop.
            const long end = endOfTrackTick (lf);
            expect (end == loopTicks, tag + " " + lane + " lane MIDI file is exactly " + std::to_string (bars)
                                          + " bars (end of track at " + std::to_string (end) + " ticks)");
        }
    }
    return true;
}

// Every lane's WAV stem on its own (lane drag in audio mode), then the folder export.
void testLaneAudioAndFolder (rolla::RollaProcessor& proc, const std::string& tag, const rolla::Groove& g, int bars,
                             const juce::File& scratch)
{
    const int loopN = rolla::rollexport::loopSamples (bars, kBpm, kRate);
    // The 7 lane bounces are independent (each builds its own offline engine,
    // like writeStemAudio's 7 workers), so they run at once instead of one
    // after another. Same calls, same files; only the checks run on this thread.
    std::vector<juce::StringArray> laneWavs ((size_t) rolla::NumParts);
    {
        std::vector<std::thread> workers;
        workers.reserve ((size_t) rolla::NumParts);
        for (int part = 0; part < rolla::NumParts; ++part)
            workers.emplace_back ([&proc, &laneWavs, part] { laneWavs[(size_t) part] = proc.writeLaneAudio (part); });
        for (auto& w : workers)
            w.join();
    }
    for (int part = 0; part < rolla::NumParts; ++part)
    {
        const auto& wav = laneWavs[(size_t) part];
        const std::string lane = rolla::partId (part);
        if (wav.isEmpty())
        {
            expect (false, tag + " " + lane + ": lane WAV");
            continue;
        }
        const juce::File wf (wav[0]);
        const auto w = readWav (wf);
        expect (nameOk (wf, g, part) && w.channels == 2 && w.rate == kRate && w.length == loopN,
                tag + " " + lane + " lane WAV: " + std::to_string (w.length) + " vs " + std::to_string (loopN)
                    + " samples, " + wf.getFileName().toStdString());
    }

    // Folder exports.
    const auto dir = scratch.getChildFile (tag);
    dir.deleteRecursively();
    proc.exportMidiTo (dir);
    proc.exportStemsTo (dir);
    int mid = 0, wav = 0;
    for (const auto& e : juce::RangedDirectoryIterator (dir, false, "ROLLA_*"))
    {
        mid += e.getFile().hasFileExtension ("mid");
        wav += e.getFile().hasFileExtension ("wav");
    }
    // Every lane gets a MIDI file and a stem, empty lanes included.
    expect (mid == rolla::NumParts + 1 && wav == rolla::NumParts + 1,
            tag + ": folder export has " + std::to_string (mid) + " MIDI (want 8: 7 lanes + full) and "
                + std::to_string (wav) + " WAV (want 8: 7 stems + full mix)");
}

// One style: a fresh roll, then the MIDI checks, the audio checks or both
// (both = the original order: stems, MIDI, lane WAVs, folder).
void testStyle (rolla::RollaProcessor& proc, int style, bool pack, const juce::File& scratch, bool doMidi = true,
                bool doAudio = true)
{
    const std::string tag = std::string (rolla::kStyleLabels[style]);
    if ((int) proc.apvts.getRawParameterValue ("dialect")->load() != style)
        expect (reshapeTo (proc, "dialect", (float) style), tag + ": style reshape");
    // A fresh roll that has never played: its files may not be loaded yet.
    {
        const auto before = proc.copyGroove().generation;
        proc.triggerRoll();
        expect (pumpUntil ([&] { return proc.copyGroove().generation != before; }), tag + ": roll");
    }
    const auto g = proc.copyGroove();
    const int bars = lengthBars (proc);
    if (doAudio)
        testStems (proc, tag, g, bars, pack);
    if (doMidi && ! testMidiExports (proc, tag, g, bars))
        return;
    if (doAudio)
        testLaneAudioAndFolder (proc, tag, g, bars, scratch);
}

// Vox presets first..last (inclusive), each right after its pick: each recipe
// has to find a file in the library (the "loops" recipe has no tempo-locked
// loop in the tone pack and used to leave the lane silent, a different style
// each CI run).
void testVoxPresets (rolla::RollaProcessor& proc, int first, int last)
{
    auto* voxPreset = proc.apvts.getParameter ("voxPreset");
    const int n = voxPreset != nullptr ? (int) voxPreset->getNormalisableRange().end + 1 : 0;
    expect (n > 0, "voxPreset parameter has presets (" + std::to_string (n) + ")");
    for (int i = std::max (0, first); i < n && i <= last; ++i)
    {
        setDenorm (proc, "voxPreset", (float) i);
        pumpUntil ([&] { return (int) proc.apvts.getRawParameterValue ("voxPreset")->load() == i; }, 2000);
        if (proc.copyGroove().lane[rolla::Vox].count <= 0)
        {
            std::cout << "vox preset " << i << ": no vox notes in this groove, skipped\n";
            continue;
        }
        const auto lane = proc.writeLaneAudio (rolla::Vox);
        const auto w = lane.isEmpty() ? Wav {} : readWav (juce::File (lane[0]));
        expect (w.peak > 1.0e-4f, "vox preset " + std::to_string (i) + " sounds right after it is picked (peak "
                                      + std::to_string (w.peak) + ")");
    }
}

// LENGTH 2 without a re-roll: the engine loops the first 2 bars of the 8-bar
// groove, and the export has to follow it. Then a fresh 2-bar roll.
void testLength2 (rolla::RollaProcessor& proc, bool pack, const juce::File& scratch)
{
    setDenorm (proc, "length", 0.f);
    pumpUntil ([&] { return lengthBars (proc) == 2; });
    expect (lengthBars (proc) == 2 && proc.copyGroove().bars == 8, "LENGTH 2 set on an 8-bar groove, no re-roll");
    {
        const auto g = proc.copyGroove();
        compareMidi (proc, "LENGTH 2 (no re-roll)", g, 2);
        const auto lane = proc.writeLaneAudio (rolla::Drums);
        const auto w = lane.isEmpty() ? Wav {} : readWav (juce::File (lane[0]));
        expect (w.length == rolla::rollexport::loopSamples (2, kBpm, kRate),
                "LENGTH 2 (no re-roll): drum stem is 2 bars (" + std::to_string (w.length) + " samples)");
    }
    // And a fresh 2-bar roll.
    testStyle (proc, rolla::Pop, pack, scratch);
}

// The slice that --only picks. Default: everything, in the original order.
struct Slice
{
    bool midi = true;      // every style's MIDI + host-loop checks, distinct-halves check
    int audioStyle = -2;   // -2: every style's audio checks, -1: none, else that style only
    int voxFirst = 0, voxLast = 1 << 30; // vox presets; voxFirst > voxLast: none
    bool length2 = true;
    std::string name = "all";
};

bool parseSlice (const juce::String& s, Slice& out)
{
    Slice none;
    none.midi = false;
    none.audioStyle = -1;
    none.voxFirst = 1;
    none.voxLast = 0;
    none.length2 = false;
    none.name = s.toStdString();
    if (s == "all")
    {
        out = Slice {};
        return true;
    }
    if (s == "midi")
    {
        out = none;
        out.midi = true;
        return true;
    }
    if (s == "length2")
    {
        out = none;
        out.length2 = true;
        return true;
    }
    if (s.startsWith ("audio:"))
    {
        const auto style = s.fromFirstOccurrenceOf (":", false, false);
        for (int i = 0; i < rolla::NumDialects; ++i)
            if (style.equalsIgnoreCase (rolla::kStyleLabels[i]))
            {
                out = none;
                out.audioStyle = i;
                return true;
            }
        return false;
    }
    if (s.startsWith ("vox:"))
    {
        const auto range = s.fromFirstOccurrenceOf (":", false, false);
        const auto a = range.upToFirstOccurrenceOf ("-", false, false);
        const auto b = range.fromFirstOccurrenceOf ("-", false, false);
        const bool toEnd = b == "end";
        if (! a.containsOnly ("0123456789") || a.isEmpty() || (! toEnd && (! b.containsOnly ("0123456789") || b.isEmpty())))
            return false;
        out = none;
        out.voxFirst = a.getIntValue();
        out.voxLast = toEnd ? (1 << 30) : b.getIntValue();
        return out.voxFirst <= out.voxLast;
    }
    return false;
}
} // namespace

int main (int argc, char** argv)
{
    Slice slice;
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--only" && i + 1 < argc)
        {
            const juce::String what (argv[++i]);
            if (! parseSlice (what, slice))
            {
                std::cerr << "unknown --only " << what.toStdString() << " (midi, audio:ROLLER|DEEP|BOUNCY|JACKIN, vox:A-B, vox:A-end, length2, all)\n";
                return 2;
            }
        }

    // setSamplesRoot saves ROLLA.settings, and drags write to the export cache.
    // Everything goes to a scratch profile, never the real one.
    const auto home = rolla::test::useScratchProfile ("export");
    if (home.empty())
    {
        std::cerr << "could not set up a scratch profile\n";
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto settingsFile = rolla::settings::file();
    std::cout << "settings file for this run: " << settingsFile.getFullPathName() << "\n";
    expect (settingsFile.isAChildOf (juce::File (home)), "settings live in the scratch profile");
    juce::File pack;
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--samples" && i + 1 < argc)
            pack = juce::File::getCurrentWorkingDirectory().getChildFile (argv[++i]);
    std::cout << "slice: " << slice.name << "\n";

    // Folder exports go inside this run's own scratch profile (unique per pid and
    // time stamp), never a shared $TMPDIR/rolla-export-test that parallel runs share.
    const auto scratch = juce::File (juce::String (home)).getChildFile ("folder-export");
    scratch.deleteRecursively();
    scratch.createDirectory();
    {
        rolla::RollaProcessor proc;
        proc.waitUntilReady();
        bool havePack = false;
        if (pack.isDirectory())
        {
            proc.setSamplesRoot (pack);
            pumpUntil ([&] { return proc.samplesFound() > 0; }, 15000);
            havePack = proc.samplesFound() > 0;
            expect (havePack, "tone pack indexed (" + std::to_string (proc.samplesFound()) + " files)");
        }
        else
            std::cout << "no --samples: stem audio content not checked (factory sounds are samples only)\n";

        // One processor, one indexed library for every case in this run.
        bool rolled = false;
        for (int style = 0; style < rolla::NumDialects; ++style)
        {
            const bool audio = slice.audioStyle == -2 || slice.audioStyle == style;
            if (! slice.midi && ! audio)
                continue;
            testStyle (proc, style, havePack, scratch, slice.midi, audio);
            rolled = true;
        }
        // Needs every style's host-loop checks, so only where all of them ran.
        if (slice.midi)
            expect (distinctHalves > 0, "host loop: " + std::to_string (distinctHalves)
                                            + " style(s) with bars 5-8 unlike bars 1-4");

        const bool vox = slice.voxFirst <= slice.voxLast;
        if (vox && ! havePack)
            std::cout << "no --samples: vox presets not checked\n";
        // A slice that starts here begins where the full run is at this point:
        // a fresh 8-bar roll of the last style.
        if (((vox && havePack) || slice.length2) && ! rolled)
            testStyle (proc, rolla::NumDialects - 1, havePack, scratch, false, false);
        if (vox && havePack)
            testVoxPresets (proc, slice.voxFirst, slice.voxLast);
        if (slice.length2)
            testLength2 (proc, havePack, scratch);
    }
    scratch.deleteRecursively();
    juce::File (juce::String (home)).deleteRecursively();
    std::cout << (fails == 0 ? "export tests passed\n" : std::to_string (fails) + " export test failure(s)\n");
    return fails == 0 ? 0 : 1;
}
