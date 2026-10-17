#include "SampleLibrary.h"
#include "../Generator/Presets.h"
#include "../Sampler/SamplePrep.h"

#include <algorithm>
#include <cctype>
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
    {
        std::lock_guard<std::mutex> lock (gainMu);
        gains.clear();
        gainsLoaded = false;
    }
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
    // Raw copies stay on disk. The gain lives only in this buffer and in the cache.
    const auto bytes = file.getSize();
    float gainDb = 0.f;
    if (! cachedGain (relative, bytes, gainDb))
    {
        gainDb = loudnessGainDb (audio->left, audio->right, audio->rate);
        rememberGain (relative, bytes, gainDb);
    }
    const float lin = std::pow (10.f, gainDb / 20.f);
    if (std::isfinite (lin) && std::fabs (lin - 1.f) > 1.0e-4f)
    {
        for (int i = 0; i < cap; ++i)
        {
            audio->left[(size_t) i] *= lin;
            audio->right[(size_t) i] *= lin;
        }
    }
    findSlices (*audio);

    {
        std::lock_guard<std::mutex> lock (slotMu);
        kept.push_back (std::move (audio));
        slot->audio.store (kept.back().get(), std::memory_order_release);
    }
    slot->status.store (2, std::memory_order_release);
}

bool SampleLibrary::cachedGain (const std::string& relative, juce::int64 bytes, float& gainDb)
{
    juce::File file;
    {
        std::lock_guard<std::mutex> rootLock (rootMu);
        file = rootDir.getChildFile (".rolla-loudness.json");
    }
    std::lock_guard<std::mutex> lock (gainMu);
    if (! gainsLoaded)
    {
        gainsLoaded = true;
        if (file.existsAsFile())
        {
            const auto parsed = juce::JSON::parse (file);
            if (auto* arr = parsed.getProperty ("entries", juce::var()).getArray())
            {
                for (const auto& item : *arr)
                {
                    const auto path = item.getProperty ("path", "").toString();
                    if (path.isEmpty())
                        continue;
                    GainRow row;
                    row.bytes = (juce::int64) (double) item.getProperty ("bytes", 0.0);
                    row.gainDb = (float) (double) item.getProperty ("gainDb", 0.0);
                    gains[path.toStdString()] = row;
                }
            }
        }
    }
    const auto found = gains.find (relative);
    if (found == gains.end() || found->second.bytes != bytes)
        return false;
    gainDb = found->second.gainDb;
    return true;
}

void SampleLibrary::rememberGain (const std::string& relative, juce::int64 bytes, float gainDb)
{
    juce::File file;
    {
        std::lock_guard<std::mutex> rootLock (rootMu);
        file = rootDir.getChildFile (".rolla-loudness.json");
    }
    std::lock_guard<std::mutex> lock (gainMu);
    GainRow row;
    row.bytes = bytes;
    row.gainDb = gainDb;
    gains[relative] = row;
    gainsLoaded = true;
    juce::Array<juce::var> entries;
    for (const auto& item : gains)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty ("path", juce::String (item.first));
        obj->setProperty ("bytes", (double) item.second.bytes);
        obj->setProperty ("gainDb", (double) item.second.gainDb);
        entries.add (juce::var (obj));
    }
    auto* root = new juce::DynamicObject();
    root->setProperty ("entries", juce::var (entries));
    file.replaceWithText (juce::JSON::toString (juce::var (root)));
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
    int altLane = -1;
    int altVoice = -1;
    int root = -1;
    int fileBpm = 0;
    bool phraseLoop = false;
    std::uint8_t styles = 0;
    std::string pack;
    std::string artist;
    std::string category;
    std::string blob;
};

int bpmFromFilename (const std::string& path)
{
    int found = 0;
    const auto slash = path.rfind ('/');
    const auto name = slash == std::string::npos ? path : path.substr (slash + 1);
    for (size_t i = 0; i < name.size(); )
    {
        if (! std::isdigit ((unsigned char) name[i]))
        {
            ++i;
            continue;
        }
        int value = 0;
        const size_t start = i;
        while (i < name.size() && std::isdigit ((unsigned char) name[i]))
        {
            value = value * 10 + (name[i] - '0');
            ++i;
        }
        const bool boundary = start == 0 || name[start - 1] == '_' || name[start - 1] == '-';
        const bool tail = i >= name.size() || name[i] == '_' || name[i] == '-' || name[i] == '.';
        if (boundary && tail && value >= 70 && value <= 190)
            found = value;
    }
    return found;
}

void classifyCategory (ManifestMeta& row, const std::string& raw)
{
    if (raw == "vox_phrase_loop")
    {
        row.phraseLoop = true;
        row.category = "phrase";
    }
    else if (raw == "vox_phrase")
        row.category = "phrase";
    else if (raw == "vox_shot")
        row.category = "chop";
    else if (raw == "atmos" || raw == "texture_loop" || raw == "atmosphere")
        row.category = "atmosphere";
    else if (raw == "v1_preset" || raw.empty())
        row.category.clear();
    else
        row.category = raw;
}

void applyAltLanes (ManifestMeta& row, const juce::var& alts)
{
    auto* arr = alts.getArray();
    if (arr == nullptr)
        return;
    for (const auto& item : *arr)
    {
        const auto text = item.toString().trim();
        if (text.isEmpty())
            continue;
        int lane = -1;
        int voice = -1;
        const auto slash = text.indexOfChar ('/');
        if (slash >= 0)
        {
            lane = partFromLaneName (text.substring (0, slash).toRawUTF8());
            voice = voiceFromFolderName (text.substring (slash + 1).toRawUTF8());
        }
        else
            lane = partFromLaneName (text.toRawUTF8());
        if (lane < 0)
            continue;
        if (lane == row.lane && (voice < 0 || voice == row.voice))
            continue;
        row.altLane = lane;
        row.altVoice = voice;
    }
}

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
    entry.altLane = (int) item.getProperty ("altLane", -1);
    entry.altVoice = (int) item.getProperty ("altVoice", -1);
    entry.fileBpm = (int) item.getProperty ("fileBpm", 0);
    entry.phraseLoop = (bool) item.getProperty ("phraseLoop", false);
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
    obj->setProperty ("altLane", entry.altLane);
    obj->setProperty ("altVoice", entry.altVoice);
    obj->setProperty ("fileBpm", entry.fileBpm);
    obj->setProperty ("phraseLoop", entry.phraseLoop);
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
    signature = fnv1a (signature, "rolla-index-v3");
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
            classifyCategory (row, item.getProperty ("category", "").toString().toLowerCase().toStdString());
            applyAltLanes (row, item.getProperty ("alt_lanes", juce::var()));
            if (row.phraseLoop)
                row.fileBpm = bpmFromFilename (path.toStdString());
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
            entry.altLane = row.altLane;
            entry.altVoice = row.altVoice;
            entry.phraseLoop = row.phraseLoop;
            entry.fileBpm = row.fileBpm;
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
        entry.altLane = item.second.altLane;
        entry.altVoice = item.second.altVoice;
        entry.phraseLoop = item.second.phraseLoop;
        entry.fileBpm = item.second.fileBpm;
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

namespace
{

bool directoryHasAudio (const juce::File& dir)
{
    if (! dir.isDirectory())
        return false;
    for (const auto& entry : juce::RangedDirectoryIterator (dir, true, "*.wav;*.aif;*.aiff;*.flac", juce::File::findFiles))
    {
        juce::ignoreUnused (entry);
        return true;
    }
    return false;
}

void requestOrdinal (SampleLibrary& library, const SampleIndex& index, int part, int voice, std::vector<std::string>& paths)
{
    PoolFilter filter;
    filter.on = true;
    filter.lane = part;
    filter.voice = voice;
    if (const auto* entry = sampleIndexAt (index, part, filter, 0))
    {
        library.request (entry->path);
        paths.emplace_back (entry->path);
    }
}

} // namespace

InstalledPack openInstalledPack (const char* manifestJson, int manifestSize)
{
    InstalledPack pack;
    const auto root = SampleLibrary::defaultRoot();
    if (! directoryHasAudio (root) || manifestJson == nullptr || manifestSize <= 0)
        return pack;
    pack.library = std::make_unique<SampleLibrary>();
    pack.library->setRoot (root);
    pack.library->startIndex (manifestJson, manifestSize);
    const auto indexedBy = juce::Time::getMillisecondCounter() + 20000;
    while (pack.library->indexedFound() < 0 && juce::Time::getMillisecondCounter() < indexedBy)
        juce::Thread::sleep (20);
    pack.index = pack.library->index();
    if (pack.index == nullptr)
        return pack;
    std::vector<std::string> paths;
    const int parts[] = { Bass, Stab, Hits, Pluck, Pad, Vox };
    for (int part : parts)
        requestOrdinal (*pack.library, *pack.index, part, -1, paths);
    const int voices[] = { Kick, Snare, Clap, Rim, HatC, HatO, Ride, Perc };
    for (int voice : voices)
        requestOrdinal (*pack.library, *pack.index, Drums, voice, paths);
    const auto readyBy = juce::Time::getMillisecondCounter() + 15000;
    while (juce::Time::getMillisecondCounter() < readyBy)
    {
        bool waiting = false;
        for (const auto& path : paths)
            if (! pack.library->ready (path.c_str()) && ! pack.library->missing (path.c_str()))
                waiting = true;
        if (! waiting)
            break;
        juce::Thread::sleep (20);
    }
    return pack;
}

} // namespace rolla
