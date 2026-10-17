#include "SampleLibrary.h"
#include "../Generator/Presets.h"

#include <cmath>

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
    {
        std::lock_guard<std::mutex> lock (rootMu);
        rootDir = root;
    }
    ensureSampleFolders (root);
}

juce::File SampleLibrary::root() const
{
    std::lock_guard<std::mutex> lock (rootMu);
    return rootDir;
}

void SampleLibrary::watch (const PresetBank& bank)
{
    std::vector<std::string> paths;
    for (int i = 0; i < PresetBank::kPer; ++i)
    {
        considerSpec (paths, bank.bass[i].sample);
        considerSpec (paths, bank.stab[i].sample);
        considerSpec (paths, bank.hits[i].sample);
        considerSpec (paths, bank.pluck[i].sample);
        considerSpec (paths, bank.pad[i].sample);
        considerSpec (paths, bank.vox[i].sample);
        considerSpec (paths, bank.drums[i].sample);
    }
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

void SampleLibrary::threadMain()
{
    while (! stop.load())
    {
        std::string job;
        {
            std::unique_lock<std::mutex> lock (jobMu);
            jobCv.wait_for (lock, std::chrono::milliseconds (200), [this]
            {
                return stop.load() || ! jobs.empty();
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

} // namespace rolla
