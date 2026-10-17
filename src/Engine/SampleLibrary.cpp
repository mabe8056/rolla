#include "SampleLibrary.h"
#include "../Generator/Presets.h"
#include "../Sampler/SamplePrep.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace rolla
{
namespace
{

std::string normalisePath (const char* text)
{
    if (text == nullptr)
        return {};
    std::string s (text);
    for (char& c : s)
        if (c == '\\')
            c = '/';
    while (! s.empty() && s.front() == '/')
        s.erase (s.begin());
    return s;
}

void consider (std::vector<std::string>& out, const char* path)
{
    auto n = normalisePath (path);
    if (n.empty())
        return;
    for (const auto& have : out)
        if (have == n)
            return;
    out.push_back (std::move (n));
}

void considerSpec (std::vector<std::string>& out, const SampleSpec& spec)
{
    consider (out, spec.file);
    for (int i = 0; i < spec.poolCount; ++i)
        consider (out, spec.pool[i]);
    for (int i = 0; i < spec.zoneCount; ++i)
        consider (out, spec.zones[i].file);
    for (int v = 0; v < NumDrumVoices; ++v)
        consider (out, spec.voiceFile[v]);
}

void findSlices (SampleAudio& audio)
{
    audio.sliceCount = 1;
    audio.slices[0] = 0;
    if (audio.frames < 64)
        return;
    const int hop = std::max (1, (int) (audio.rate / 200.0));
    const int gap = std::max (hop, (int) (audio.rate * 0.04));
    float env = 0.f;
    int last = -gap;
    for (int i = 0; i < audio.frames; i += hop)
    {
        const float e = std::fabs (audio.left[(size_t) i]);
        env = env * 0.85f + e * 0.15f;
        if (e > 0.05f && e > env * 2.2f && i - last >= gap && audio.sliceCount < 24)
        {
            audio.slices[audio.sliceCount++] = i;
            last = i;
        }
    }
}

void gatherReferenced (std::vector<std::string>& paths, const PresetBank& bank)
{
    for (int part = 0; part < NumParts; ++part)
    {
        const int n = bank.count[part] > 0 ? bank.count[part] : PresetBank::kPer;
        for (int i = 0; i < n; ++i)
        {
            considerSpec (paths, specAt (bank, part, i));
            if (part == Drums)
                for (int v = 0; v < NumDrumVoices; ++v)
                    for (const auto& file : bank.drums[i].voicePool[v])
                        consider (paths, file.c_str());
        }
    }
}

} // namespace

juce::File SampleLibrary::defaultRoot()
{
    return juce::File::getSpecialLocation (juce::File::userMusicDirectory)
        .getChildFile ("ROLLA")
        .getChildFile ("Samples");
}

void ensureSampleFolders (const juce::File& root)
{
    const char* parts[] = { "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX" };
    const char* drums[] = { "KICK", "CLAP", "SNARE", "RIM", "CH", "OH", "RIDE", "PERC" };
    root.createDirectory();
    for (const char* p : parts)
        root.getChildFile (p).createDirectory();
    auto drumsRoot = root.getChildFile ("DRUMS");
    drumsRoot.createDirectory();
    for (const char* d : drums)
        drumsRoot.getChildFile (d).createDirectory();
}

SampleLibrary::SampleLibrary()
{
    rootDir = defaultRoot();
    worker = std::thread ([this] { threadMain(); });
}

SampleLibrary::~SampleLibrary()
{
    stop.store (true);
    jobCv.notify_all();
    if (worker.joinable())
        worker.join();
}

void SampleLibrary::setRoot (const juce::File& root)
{
    indexGen.fetch_add (1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock (rootMu);
        rootDir = root;
    }
    {
        std::lock_guard<std::mutex> lock (indexMu);
        indexSnap.reset();
    }
    indexedFoundCount.store (-1, std::memory_order_release);
    indexedMissingCount.store (-1, std::memory_order_release);
    ensureSampleFolders (root);
}

juce::File SampleLibrary::root() const
{
    std::lock_guard<std::mutex> lock (rootMu);
    return rootDir;
}

void collectReferencedSamples (const PresetBank& bank, std::vector<std::string>& out)
{
    gatherReferenced (out, bank);
}

void SampleLibrary::watch (const PresetBank& bank)
{
    std::vector<std::string> paths;
    gatherReferenced (paths, bank);
    for (const auto& p : paths)
        enqueue (p);
}

void SampleLibrary::enqueue (const std::string& relative)
{
    if (relative.empty())
        return;
    if (ready (relative.c_str()) || missing (relative.c_str()))
        return;
    {
        std::lock_guard<std::mutex> lock (jobMu);
        jobs.push_back (relative);
    }
    jobCv.notify_one();
}

void SampleLibrary::startIndex (const char* manifestJson, int size)
{
    {
        std::lock_guard<std::mutex> lock (manifestMu);
        if (manifestJson != nullptr && size > 0)
            embeddedManifest.assign (manifestJson, manifestJson + size);
        else
            embeddedManifest.clear();
    }
    indexGen.fetch_add (1, std::memory_order_acq_rel);
    indexKick.store (true, std::memory_order_release);
    jobCv.notify_all();
}

std::shared_ptr<const SampleIndex> SampleLibrary::index() const
{
    std::lock_guard<std::mutex> lock (indexMu);
    return indexSnap;
}

void SampleLibrary::request (const char* relative) const noexcept
{
    if (relative == nullptr || relative[0] == 0)
        return;
    if (ready (relative) || missing (relative))
        return;
    const int tail = reqTail.load (std::memory_order_relaxed);
    const int head = reqHead.load (std::memory_order_acquire);
    if (tail - head >= kReq)
        return;
    for (int i = head; i < tail; ++i)
        if (std::strcmp (reqRing[i % kReq], relative) == 0)
            return;
    char* dst = reqRing[tail % kReq];
    std::strncpy (dst, relative, sizeof (reqRing[0]) - 1);
    dst[sizeof (reqRing[0]) - 1] = 0;
    reqTail.store (tail + 1, std::memory_order_release);
    jobCv.notify_one();
}

void SampleLibrary::threadMain()
{
    while (! stop.load())
    {
        if (indexKick.exchange (false, std::memory_order_acq_rel))
        {
            buildIndex();
            continue;
        }

        const int head = reqHead.load (std::memory_order_relaxed);
        const int tail = reqTail.load (std::memory_order_acquire);
        if (head != tail)
        {
            const std::string path (reqRing[head % kReq]);
            reqHead.store (head + 1, std::memory_order_release);
            if (! stop.load())
                loadOne (path);
            continue;
        }

        std::string job;
        {
            std::unique_lock<std::mutex> lock (jobMu);
            jobCv.wait_for (lock, std::chrono::milliseconds (200), [this]
            {
                return stop.load()
                    || ! jobs.empty()
                    || indexKick.load (std::memory_order_acquire)
                    || reqHead.load (std::memory_order_relaxed) != reqTail.load (std::memory_order_acquire);
            });
            if (stop.load())
                return;
            if (jobs.empty())
                continue;
            job = std::move (jobs.back());
            jobs.pop_back();
        }
        loadOne (job);
    }
}

SampleLibrary::Slot* SampleLibrary::slotFor (const char* relative) noexcept
{
    if (relative == nullptr)
        return nullptr;
    for (auto& slot : slots)
        if (slot.status.load (std::memory_order_acquire) != 0 && std::strcmp (slot.path, relative) == 0)
            return &slot;
    return nullptr;
}

const SampleLibrary::Slot* SampleLibrary::slotFor (const char* relative) const noexcept
{
    return const_cast<SampleLibrary*> (this)->slotFor (relative);
}

const SampleAudio* SampleLibrary::tryGet (const char* relative) const noexcept
{
    const auto* slot = slotFor (relative);
    if (slot == nullptr || slot->status.load (std::memory_order_acquire) != 2)
        return nullptr;
    return slot->audio.load (std::memory_order_acquire);
}

bool SampleLibrary::ready (const char* relative) const noexcept
{
    return tryGet (relative) != nullptr;
}

bool SampleLibrary::missing (const char* relative) const noexcept
{
    if (relative == nullptr || relative[0] == 0)
        return true;
    const auto* slot = slotFor (relative);
    if (slot == nullptr)
        return false;
    return slot->status.load (std::memory_order_acquire) == 3;
}

void SampleLibrary::loadOne (const std::string& relative)
{
    Slot* slot = nullptr;
    {
        std::lock_guard<std::mutex> lock (slotMu);
        slot = slotFor (relative.c_str());
        if (slot == nullptr)
        {
            for (auto& candidate : slots)
            {
                if (candidate.status.load (std::memory_order_acquire) == 0)
                {
                    std::strncpy (candidate.path, relative.c_str(), sizeof (candidate.path) - 1);
                    candidate.path[sizeof (candidate.path) - 1] = 0;
                    candidate.status.store (1, std::memory_order_release);
                    slot = &candidate;
                    break;
                }
            }
        }
    }
    if (slot == nullptr || slot->status.load (std::memory_order_acquire) == 2)
        return;

    juce::File file;
    {
        std::lock_guard<std::mutex> lock (rootMu);
        file = rootDir.getChildFile (juce::String (relative).replaceCharacter ('\\', '/'));
    }

    auto fail = [&]
    {
        slot->status.store (3, std::memory_order_release);
    };

    if (! file.existsAsFile())
    {
        fail();
        return;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr || reader->sampleRate < 1000.0 || reader->lengthInSamples < 8)
    {
        fail();
        return;
    }

    const int cap = (int) std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 16.0));
    juce::AudioBuffer<float> buf ((int) std::max (1, (int) reader->numChannels), cap);
    if (! reader->read (&buf, 0, cap, 0, true, true))
    {
        fail();
        return;
    }

    auto audio = std::make_unique<SampleAudio>();
    audio->frames = cap;
    audio->rate = reader->sampleRate;
    audio->left.resize ((size_t) cap);
    audio->right.resize ((size_t) cap);
    const float* l = buf.getReadPointer (0);
    const float* r = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : l;
    for (int i = 0; i < cap; ++i)
    {
        audio->left[(size_t) i] = l[i];
        audio->right[(size_t) i] = r[i];
    }
    findSlices (*audio);

    {
        std::lock_guard<std::mutex> lock (slotMu);
        kept.push_back (std::move (audio));
        slot->audio.store (kept.back().get(), std::memory_order_release);
    }
    slot->status.store (2, std::memory_order_release);
}

namespace
{

bool audioName (const juce::String& name)
{
    return name.endsWithIgnoreCase (".wav")
        || name.endsWithIgnoreCase (".aif")
        || name.endsWithIgnoreCase (".aiff")
        || name.endsWithIgnoreCase (".flac");
}

void copyField (char* dst, int n, const juce::String& text)
{
    if (dst == nullptr || n <= 0)
        return;
    text.copyToUTF8 (dst, (size_t) n);
}

bool containsFold (const std::string& hay, const char* needle)
{
    if (needle == nullptr || needle[0] == 0)
        return false;
    const auto found = hay.find (needle);
    return found != std::string::npos;
}

std::string lowerCopy (juce::String text)
{
    return text.toLowerCase().toStdString();
}

std::uint64_t fnv1a (std::uint64_t hash, const std::string& text)
{
    const auto* bytes = reinterpret_cast<const unsigned char*> (text.data());
    for (size_t i = 0; i < text.size(); ++i)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    hash ^= 0xffu;
    hash *= 1099511628211ull;
    return hash;
}

struct ManifestMeta
{
    int lane = -1;
    int voice = -1;
    int root = -1;
    std::uint8_t styles = 0;
    std::string pack;
    std::string artist;
    std::string category;
    std::string blob;
};

std::uint8_t stylesFromTags (const juce::var& presets)
{
    std::uint8_t styles = 0;
    if (auto* arr = presets.getArray())
    {
        for (const auto& item : *arr)
        {
            const auto tag = item.toString().toLowerCase();
            const auto second = tag.fromFirstOccurrenceOf (".", false, false)
                                    .upToFirstOccurrenceOf (".", false, false);
            if (second == "ukmin")
                styles = (std::uint8_t) (styles | 1u);
            else if (second == "long")
                styles = (std::uint8_t) (styles | 2u);
            else if (second == "pop")
                styles = (std::uint8_t) (styles | 4u);
            else if (second == "gaskin")
                styles = (std::uint8_t) (styles | 8u);
        }
    }
    return styles;
}

std::string firstSourceComponent (const juce::String& source)
{
    auto text = source.trim();
    if (text.isEmpty())
        return {};
    const auto slash = text.indexOfChar ('/');
    if (slash < 0)
        return text.toStdString();
    return text.substring (0, slash).toStdString();
}

std::string artistFromSource (const juce::String& source)
{
    auto text = source.trim();
    juce::StringArray parts;
    parts.addTokens (text, "/", "");
    parts.removeEmptyStrings();
    if (parts.size() >= 3)
        return parts[1].toStdString();
    if (parts.size() == 2 && ! parts[1].containsChar ('.'))
        return parts[1].toStdString();
    return {};
}

void inferCategory (SampleEntry& entry, const std::string& blob)
{
    if (entry.category[0] != 0)
        return;
    if (entry.lane == Vox)
    {
        if (containsFold (blob, "chop"))
            copyField (entry.category, 32, "chop");
        else if (containsFold (blob, "phrase") || containsFold (blob, "adlib") || containsFold (blob, "ad-lib"))
            copyField (entry.category, 32, "phrase");
        else
            copyField (entry.category, 32, "one-shot");
    }
    else if (entry.lane == Pad)
    {
        if (containsFold (blob, "atmos") || containsFold (blob, "atmo") || containsFold (blob, "texture") || containsFold (blob, "drone"))
            copyField (entry.category, 32, "atmosphere");
        else
            copyField (entry.category, 32, "pad");
    }
}

int laneFromPath (const std::string& path)
{
    const auto slash = path.find ('/');
    const auto head = slash == std::string::npos ? path : path.substr (0, slash);
    return partFromLaneName (head.c_str());
}

int voiceFromPath (const std::string& path)
{
    const auto first = path.find ('/');
    if (first == std::string::npos)
        return -1;
    const auto second = path.find ('/', first + 1);
    if (second == std::string::npos)
        return -1;
    const auto folder = path.substr (first + 1, second - first - 1);
    return voiceFromFolderName (folder.c_str());
}

std::string folderBetween (const std::string& path)
{
    const auto first = path.find ('/');
    if (first == std::string::npos)
        return {};
    const auto second = path.find ('/', first + 1);
    if (second == std::string::npos)
        return {};
    return path.substr (first + 1, second - first - 1);
}

void collectAudio (const juce::File& dir, const juce::File& root, std::vector<std::string>& out, int depth)
{
    if (! dir.isDirectory() || depth > 1)
        return;
    for (const auto& child : dir.findChildFiles (juce::File::findFiles, false))
    {
        if (! audioName (child.getFileName()))
            continue;
        auto rel = child.getRelativePathFrom (root).replaceCharacter ('\\', '/');
        while (rel.startsWithChar ('/'))
            rel = rel.substring (1);
        if (rel.isNotEmpty())
            out.push_back (rel.toStdString());
    }
    if (depth == 1)
        return;
    for (const auto& sub : dir.findChildFiles (juce::File::findDirectories, false))
    {
        if (sub.getFileName().startsWithChar ('.'))
            continue;
        collectAudio (sub, root, out, depth + 1);
    }
}

SampleEntry entryFromCache (const juce::var& item)
{
    SampleEntry entry;
    const auto path = item.getProperty ("path", "").toString();
    copyField (entry.path, 256, path);
    entry.lane = (int) item.getProperty ("lane", -1);
    entry.voice = (int) item.getProperty ("voice", -1);
    entry.root = (int) item.getProperty ("root", 60);
    entry.rootPc = (int) item.getProperty ("rootPc", -1);
    entry.styles = (std::uint8_t) (int) item.getProperty ("styles", 0);
    copyField (entry.pack, 64, item.getProperty ("pack", "").toString());
    copyField (entry.artist, 64, item.getProperty ("artist", "").toString());
    copyField (entry.category, 32, item.getProperty ("category", "").toString());
    entry.onDisk = (bool) item.getProperty ("onDisk", false);
    return entry;
}

juce::var entryToVar (const SampleEntry& entry)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("path", juce::String (entry.path));
    obj->setProperty ("lane", entry.lane);
    obj->setProperty ("voice", entry.voice);
    obj->setProperty ("root", entry.root);
    obj->setProperty ("rootPc", entry.rootPc);
    obj->setProperty ("styles", (int) entry.styles);
    obj->setProperty ("pack", juce::String (entry.pack));
    obj->setProperty ("artist", juce::String (entry.artist));
    obj->setProperty ("category", juce::String (entry.category));
    obj->setProperty ("onDisk", entry.onDisk);
    return juce::var (obj);
}

} // namespace

void SampleLibrary::buildIndex()
{
    const auto gen = indexGen.load (std::memory_order_acquire);
    if (stop.load())
        return;

    const auto folder = root();
    std::vector<std::string> disk;
    const char* lanes[] = { "BASS", "STAB", "HITS", "PLUCK", "PAD", "VOX", "DRUMS" };
    for (const char* lane : lanes)
        collectAudio (folder.getChildFile (lane), folder, disk, 0);
    std::sort (disk.begin(), disk.end());
    disk.erase (std::unique (disk.begin(), disk.end()), disk.end());
    if (disk.size() > 8192)
        disk.resize (8192);

    std::string manifestText;
    {
        const auto v2 = folder.getChildFile ("SAMPLE_MANIFEST_v2.json");
        const auto v1 = folder.getChildFile ("SAMPLE_MANIFEST.json");
        if (v2.existsAsFile())
            manifestText = v2.loadFileAsString().toStdString();
        else if (v1.existsAsFile())
            manifestText = v1.loadFileAsString().toStdString();
        else
        {
            std::lock_guard<std::mutex> lock (manifestMu);
            manifestText = embeddedManifest;
        }
    }

    std::uint64_t signature = 14695981039346656037ull;
    signature = fnv1a (signature, "rolla-index-v1");
    for (const auto& path : disk)
        signature = fnv1a (signature, path);
    signature = fnv1a (signature, manifestText);
    char signatureText[32];
    std::snprintf (signatureText, sizeof signatureText, "%llu", (unsigned long long) signature);

    const auto cacheFile = folder.getChildFile (".rolla-index.json");
    if (cacheFile.existsAsFile())
    {
        const auto cached = juce::JSON::parse (cacheFile);
        const auto cachedSig = cached.getProperty ("signature", "").toString();
        if (cachedSig == signatureText)
        {
            auto snap = std::make_shared<SampleIndex>();
            if (auto* arr = cached.getProperty ("entries", juce::var()).getArray())
            {
                snap->entries.reserve ((size_t) arr->size());
                for (const auto& item : *arr)
                {
                    auto entry = entryFromCache (item);
                    if (entry.path[0] == 0)
                        continue;
                    if (entry.onDisk)
                        ++snap->found;
                    else
                        ++snap->missing;
                    snap->entries.push_back (entry);
                }
            }
            if (indexGen.load (std::memory_order_acquire) != gen || stop.load())
            {
                indexKick.store (true, std::memory_order_release);
                return;
            }
            {
                std::lock_guard<std::mutex> lock (indexMu);
                indexSnap = snap;
            }
            indexedFoundCount.store (snap->found, std::memory_order_release);
            indexedMissingCount.store (snap->missing, std::memory_order_release);
            const auto line = "ROLLA samples: " + std::to_string (snap->found) + " found, "
                            + std::to_string (snap->missing) + " missing";
            juce::Logger::writeToLog (juce::String (line));
            std::fprintf (stderr, "%s\n", line.c_str());
            return;
        }
    }

    std::map<std::string, ManifestMeta> meta;
    const auto parsed = juce::JSON::parse (juce::String (manifestText));
    if (auto* files = parsed.getProperty ("files", juce::var()).getArray())
    {
        for (const auto& item : *files)
        {
            auto path = item.getProperty ("file", "").toString().replaceCharacter ('\\', '/');
            while (path.startsWithChar ('/'))
                path = path.substring (1);
            if (path.isEmpty())
                continue;
            ManifestMeta row;
            const auto laneName = item.getProperty ("lane", "").toString();
            row.lane = partFromLaneName (laneName.toRawUTF8());
            if (row.lane < 0)
                row.lane = laneFromPath (path.toStdString());
            const auto voiceName = item.getProperty ("voice", "").toString();
            row.voice = voiceFromFolderName (voiceName.toRawUTF8());
            if (row.voice < 0)
                row.voice = voiceFromPath (path.toStdString());
            const auto rootName = item.getProperty ("root", "").toString();
            if (rootName.isNotEmpty())
                row.root = midiFromNoteName (rootName.toRawUTF8(), -1);
            row.styles = stylesFromTags (item.getProperty ("presets", juce::var()));
            auto pack = item.getProperty ("source_pack", "").toString();
            if (pack.isEmpty())
                pack = juce::String (firstSourceComponent (item.getProperty ("source", "").toString()));
            row.pack = pack.toStdString();
            auto artist = item.getProperty ("artist", "").toString();
            if (artist.isEmpty())
                artist = juce::String (artistFromSource (item.getProperty ("source", "").toString()));
            row.artist = artist.toStdString();
            row.category = item.getProperty ("category", "").toString().toStdString();
            juce::String blob = path + " " + item.getProperty ("category", "").toString();
            if (auto* tags = item.getProperty ("presets", juce::var()).getArray())
                for (const auto& tag : *tags)
                    blob << " " << tag.toString();
            if (auto* roles = item.getProperty ("roles", juce::var()).getArray())
                for (const auto& role : *roles)
                    blob << " " << role.toString();
            row.blob = lowerCopy (blob);
            meta[path.toStdString()] = std::move (row);
        }
    }

    auto snap = std::make_shared<SampleIndex>();
    snap->entries.reserve (disk.size() + meta.size());
    std::map<std::string, bool> seen;
    for (const auto& path : disk)
    {
        if (stop.load())
            return;
        SampleEntry entry;
        copyField (entry.path, 256, juce::String (path));
        entry.onDisk = true;
        entry.lane = laneFromPath (path);
        entry.voice = voiceFromPath (path);
        const auto found = meta.find (path);
        std::string blob = lowerCopy (juce::String (path));
        if (found != meta.end())
        {
            const auto& row = found->second;
            if (row.lane >= 0)
                entry.lane = row.lane;
            if (row.voice >= 0)
                entry.voice = row.voice;
            entry.styles = row.styles;
            if (row.root >= 0)
            {
                entry.root = row.root;
                entry.rootPc = row.root % 12;
            }
            copyField (entry.pack, 64, juce::String (row.pack));
            copyField (entry.artist, 64, juce::String (row.artist));
            copyField (entry.category, 32, juce::String (row.category));
            blob = row.blob;
        }
        if (entry.rootPc < 0)
        {
            const int named = midiFromFilename (path.c_str());
            if (named >= 0)
            {
                entry.root = named;
                entry.rootPc = named % 12;
            }
        }
        if (entry.lane == Drums && entry.voice < 0)
        {
            entry.voice = Perc;
            if (entry.category[0] == 0)
                copyField (entry.category, 32, juce::String (folderBetween (path)).toLowerCase());
        }
        else if (entry.category[0] == 0)
        {
            const auto mid = folderBetween (path);
            const int known = voiceFromFolderName (mid.c_str());
            if (! mid.empty() && known < 0)
            {
                const auto midName = juce::String (mid).toLowerCase();
                if (midName == "chops" || midName == "chop")
                    copyField (entry.category, 32, "chop");
                else if (midName == "phrases" || midName == "phrase")
                    copyField (entry.category, 32, "phrase");
                else if (midName == "atmospheres" || midName == "atmosphere" || midName == "atmos")
                    copyField (entry.category, 32, "atmosphere");
            }
        }
        inferCategory (entry, blob);
        if (entry.lane == Drums && entry.category[0] == 0)
        {
            const auto mid = folderBetween (path);
            if (! mid.empty())
                copyField (entry.category, 32, juce::String (mid).toLowerCase());
        }
        snap->entries.push_back (entry);
        ++snap->found;
        seen[path] = true;
    }

    for (const auto& item : meta)
    {
        if (seen[item.first])
            continue;
        SampleEntry entry;
        copyField (entry.path, 256, juce::String (item.first));
        entry.onDisk = false;
        entry.lane = item.second.lane >= 0 ? item.second.lane : laneFromPath (item.first);
        entry.voice = item.second.voice >= 0 ? item.second.voice : voiceFromPath (item.first);
        if (item.second.root >= 0)
        {
            entry.root = item.second.root;
            entry.rootPc = item.second.root % 12;
        }
        entry.styles = item.second.styles;
        copyField (entry.pack, 64, juce::String (item.second.pack));
        copyField (entry.artist, 64, juce::String (item.second.artist));
        copyField (entry.category, 32, juce::String (item.second.category));
        inferCategory (entry, item.second.blob);
        snap->entries.push_back (entry);
        ++snap->missing;
    }

    if (indexGen.load (std::memory_order_acquire) != gen || stop.load())
    {
        indexKick.store (true, std::memory_order_release);
        return;
    }

    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("signature", juce::String (signatureText));
        juce::Array<juce::var> arr;
        for (const auto& entry : snap->entries)
            arr.add (entryToVar (entry));
        obj->setProperty ("entries", juce::var (arr));
        cacheFile.replaceWithText (juce::JSON::toString (juce::var (obj)));
    }

    {
        std::lock_guard<std::mutex> lock (indexMu);
        indexSnap = snap;
    }
    indexedFoundCount.store (snap->found, std::memory_order_release);
    indexedMissingCount.store (snap->missing, std::memory_order_release);
    const auto line = "ROLLA samples: " + std::to_string (snap->found) + " found, "
                    + std::to_string (snap->missing) + " missing";
    juce::Logger::writeToLog (juce::String (line));
    std::fprintf (stderr, "%s\n", line.c_str());
}

} // namespace rolla
