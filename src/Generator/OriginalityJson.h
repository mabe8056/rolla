#pragma once

// The hashed originality index on disk (see Originality.h for what it holds).
// Same pattern as GenreTargetsJson.h / rolla-fingerprint (#42): the repo holds
// only the code, the data is built from private references on the owner's
// machine and stays there.
//
// Where it lives: $ROLLA_ORIG_INDEX (an absolute file path), else
// <ROLLA settings folder>/Originality/orig_index.json, which on macOS is
// ~/Library/Application Support/ROLLA/Originality/orig_index.json.
// ROLLA_SETTINGS_DIR moves it with the settings, so tests never see a real one.
// Never in the repo and never in the bundle: it is reference-derived.
//
// Build:  rolla-rolls --build-orig-index <reference MIDI folder> [--orig-out FILE]
// The folder is scanned recursively for .mid/.midi and read the way
// tools/originality_check.py reads it (one stream per track and channel, drums
// on channel 10 skipped, plus the merged lowest voice).
//
// Schema "rolla.orig_index/1":
//   { schema, extractor, thresholds: { skel_win, skel_ngram, voicing },
//     refs: [ { id, chord_hits, ngrams: "<16 hex per key>...",
//               sigs: [ "<72 x 8 hex>" ... ], voices: [ "<16 hex>:<count>" ... ],
//               voice_bars, voice_at: "<16 hex per key>..." } ] }
// Hashes and MinHash values only: no notes, names or paths. A ref id is a hash
// of the file's path relative to the scanned folder plus "/<stream>"; the
// builder prints the id -> path list to stdout for the owner and keeps it out
// of the file. An index from another extractor version is refused (rebuild it).

#include "Originality.h"
#include "../Engine/RollaSettings.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rolla::orig
{

inline constexpr const char* kSchema = "rolla.orig_index/1";

inline juce::File defaultIndexFile()
{
    const auto env = juce::SystemStats::getEnvironmentVariable ("ROLLA_ORIG_INDEX", {});
    if (env.isNotEmpty() && juce::File::isAbsolutePath (env))
        return juce::File (env);
    return settings::file().getParentDirectory().getChildFile ("Originality").getChildFile ("orig_index.json");
}

inline juce::var toVar (const Index& idx)
{
    auto* root = new juce::DynamicObject();
    root->setProperty ("schema", kSchema);
    root->setProperty ("extractor", kExtractor);
    auto* th = new juce::DynamicObject();
    th->setProperty ("skel_win", (double) idx.th.skelWin);
    th->setProperty ("skel_ngram", (double) idx.th.skelNgram);
    th->setProperty ("voicing", (double) idx.th.voicing);
    root->setProperty ("thresholds", juce::var (th));
    juce::Array<juce::var> refs;
    for (const auto& r : idx.refs)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty ("id", juce::String (r.id));
        o->setProperty ("chord_hits", r.chordHits);
        std::string ngrams;
        ngrams.reserve (r.ngrams.size() * 16);
        for (auto key : r.ngrams)
            ngrams += toHex (key, 16);
        o->setProperty ("ngrams", juce::String (ngrams));
        juce::Array<juce::var> sigs;
        for (const auto& s : r.sigs)
            sigs.add (juce::String (sigToHex (s)));
        o->setProperty ("sigs", sigs);
        juce::Array<juce::var> voices;
        for (const auto& v : r.voices)
            voices.add (juce::String (toHex (v.first, 16) + ":" + std::to_string (v.second)));
        o->setProperty ("voices", voices);
        o->setProperty ("voice_bars", r.voiceBars);
        std::string at;
        at.reserve (r.voiceAt.size() * 16);
        for (auto key : r.voiceAt)
            at += toHex (key, 16);
        o->setProperty ("voice_at", juce::String (at));
        refs.add (juce::var (o));
    }
    root->setProperty ("refs", refs);
    return juce::var (root);
}

inline juce::String toJson (const Index& idx)
{
    return juce::JSON::toString (toVar (idx), true);
}

// Null (with a reason in *error) when the data is not a usable index.
inline std::shared_ptr<Index> parseIndex (const juce::var& v, juce::String* error = nullptr)
{
    auto fail = [error] (const juce::String& why) -> std::shared_ptr<Index>
    {
        if (error != nullptr)
            *error = why;
        return nullptr;
    };
    if (v["schema"].toString() != kSchema)
        return fail ("not a rolla.orig_index/1 file");
    if ((int) v["extractor"] != kExtractor)
        return fail ("built by extractor " + v["extractor"].toString() + ", this build reads "
                     + juce::String (kExtractor) + ": rebuild it with rolla-rolls --build-orig-index");
    auto idx = std::make_shared<Index>();
    const auto& th = v["thresholds"];
    auto number = [] (const juce::var& x, float& out)
    {
        if (x.isDouble() || x.isInt() || x.isInt64())
            out = (float) (double) x;
    };
    number (th["skel_win"], idx->th.skelWin);
    number (th["skel_ngram"], idx->th.skelNgram);
    number (th["voicing"], idx->th.voicing);
    const auto* refs = v["refs"].getArray();
    if (refs == nullptr)
        return fail ("no refs array");
    for (const auto& ref : *refs)
    {
        RefData r;
        r.id = ref["id"].toString().toStdString();
        r.chordHits = (int) ref["chord_hits"];
        const std::string ngrams = ref["ngrams"].toString().toStdString();
        if (ngrams.size() % 16 != 0)
            return fail ("bad ngrams in " + juce::String (r.id));
        for (size_t at = 0; at < ngrams.size(); at += 16)
        {
            std::uint64_t key = 0;
            if (! fromHex (ngrams, at, 16, key))
                return fail ("bad ngrams in " + juce::String (r.id));
            r.ngrams.push_back (key);
        }
        if (const auto* sigs = ref["sigs"].getArray())
            for (const auto& s : *sigs)
            {
                Sig sig {};
                if (! sigFromHex (s.toString().toStdString(), sig))
                    return fail ("bad sig in " + juce::String (r.id));
                r.sigs.push_back (sig);
            }
        if (const auto* voices = ref["voices"].getArray())
            for (const auto& x : *voices)
            {
                const std::string text = x.toString().toStdString();
                std::uint64_t key = 0;
                if (text.size() < 18 || text[16] != ':' || ! fromHex (text, 0, 16, key))
                    return fail ("bad voice in " + juce::String (r.id));
                r.voices.push_back ({ key, std::max (1, std::atoi (text.c_str() + 17)) });
            }
        r.voiceBars = std::max (0, (int) ref["voice_bars"]);
        const std::string at = ref["voice_at"].toString().toStdString();
        if (at.size() % 16 != 0)
            return fail ("bad voice_at in " + juce::String (r.id));
        for (size_t i = 0; i < at.size(); i += 16)
        {
            std::uint64_t key = 0;
            if (! fromHex (at, i, 16, key))
                return fail ("bad voice_at in " + juce::String (r.id));
            r.voiceAt.push_back (key);
        }
        std::sort (r.voiceAt.begin(), r.voiceAt.end());
        idx->refs.push_back (std::move (r));
    }
    idx->finalize();
    return idx;
}

// Null when the file is missing or unusable (then only the built-in guard runs).
inline std::shared_ptr<const Index> loadIndex (const juce::File& file, juce::String* error = nullptr)
{
    if (! file.existsAsFile())
    {
        if (error != nullptr)
            *error = "no index at " + file.getFullPathName();
        return nullptr;
    }
    juce::var v;
    const auto parsed = juce::JSON::parse (file.loadFileAsString(), v);
    if (parsed.failed())
    {
        if (error != nullptr)
            *error = "index does not parse: " + parsed.getErrorMessage();
        return nullptr;
    }
    return parseIndex (v, error);
}

inline bool writeIndex (const Index& idx, const juce::File& file)
{
    file.getParentDirectory().createDirectory();
    return file.replaceWithText (toJson (idx));
}

// One MIDI file as OC's load_streams() reads it: a stream per (track, channel)
// with notes, channel 10 skipped, notes paired on/off per (channel, note), and
// the merged lowest voice when there is more than one stream. SMPTE-timed files
// are skipped.
inline std::vector<Stream> streamsFromMidi (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk())
        return {};
    juce::MidiFile mf;
    if (! mf.readFrom (in))
        return {};
    const int ppq = (int) mf.getTimeFormat();
    if (ppq <= 0)
        return {};
    std::map<std::pair<int, int>, std::vector<RawNote>> tracks;
    for (int ti = 0; ti < mf.getNumTracks(); ++ti)
    {
        const auto* seq = mf.getTrack (ti);
        if (seq == nullptr)
            continue;
        std::map<std::pair<int, int>, double> on;
        for (int e = 0; e < seq->getNumEvents(); ++e)
        {
            const auto& m = seq->getEventPointer (e)->message;
            const bool noteOn = m.isNoteOn();
            if (! noteOn && ! m.isNoteOff())
                continue;
            const int channel = m.getChannel() - 1;
            if (channel == 9)
                continue;
            const auto key = std::make_pair (channel, m.getNoteNumber());
            if (noteOn)
                on[key] = m.getTimeStamp();
            else
            {
                auto it = on.find (key);
                if (it != on.end())
                {
                    tracks[{ ti, channel }].push_back ({ it->second / (double) ppq * 4.0, key.second });
                    on.erase (it);
                }
            }
        }
    }
    std::vector<Stream> out;
    std::vector<RawNote> all;
    for (const auto& kv : tracks)
    {
        if (kv.second.empty())
            continue;
        out.push_back (streamFromNotes (kv.second));
        all.insert (all.end(), kv.second.begin(), kv.second.end());
    }
    if (out.size() > 1)
        out.push_back (streamFromNotes (all));
    return out;
}

struct BuildReport
{
    int files = 0;
    int streams = 0;
    std::vector<std::pair<std::string, std::string>> idToPath; // for the owner's eyes only
};

// Adds every .mid/.midi under `folder` (sorted, recursive) to `idx` and finalizes it.
inline BuildReport buildIndex (const juce::File& folder, Index& idx)
{
    BuildReport rep;
    auto files = folder.findChildFiles (juce::File::findFiles, true, "*.mid;*.midi;*.MID;*.MIDI");
    files.sort();
    for (const auto& f : files)
    {
        const std::string rel = f.getRelativePathFrom (folder).toStdString();
        const std::string base = toHex (hashText (rel), 12);
        const auto streams = streamsFromMidi (f);
        if (streams.empty())
            continue;
        ++rep.files;
        for (size_t i = 0; i < streams.size(); ++i)
        {
            const std::string id = base + "/" + std::to_string (i);
            const size_t before = idx.refs.size();
            idx.addRef (id, streams[i]);
            if (idx.refs.size() > before)
            {
                ++rep.streams;
                rep.idToPath.push_back ({ id, rel });
            }
        }
    }
    idx.finalize();
    return rep;
}

} // namespace rolla::orig
