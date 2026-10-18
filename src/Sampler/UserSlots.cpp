#include "UserSlots.h"
#include "SamplePrep.h"
#include "../Engine/SampleChop.h"

#include <algorithm>
#include <cstring>

namespace rolla
{
namespace
{

void copyPath (char* dst, int n, const std::string& src)
{
    if (dst == nullptr || n <= 0)
        return;
    std::strncpy (dst, src.c_str(), (size_t) n - 1);
    dst[n - 1] = 0;
}

std::string forwardSlashes (juce::String text)
{
    return text.replaceCharacter ('\\', '/').toStdString();
}

} // namespace

UserSlots::UserSlots()
{
    rootDir = SampleLibrary::defaultRoot();
    publish();
    worker = std::thread ([this] { threadMain(); });
}

UserSlots::~UserSlots()
{
    stop.store (true);
    jobCv.notify_all();
    if (worker.joinable())
        worker.join();
}

bool UserSlots::validSlot (int slot) const noexcept
{
    return slot >= 0 && slot < kUserSlotCount;
}

bool UserSlots::safeRelative (const std::string& path) noexcept
{
    if (path.empty() || path.size() >= 160)
        return false;
    if (path[0] == '/' || path[0] == '\\')
        return false;
    if (path.find (":") != std::string::npos)
        return false;
    if (path.find ("..") != std::string::npos)
        return false;
    return true;
}

void UserSlots::setRoot (const juce::File& root)
{
    {
        std::lock_guard<std::mutex> lock (rootMu);
        rootDir = root;
    }
    ensureUserFolders();
    for (int i = 0; i < kUserSlotCount; ++i)
        enqueue (i);
}

juce::File UserSlots::root() const
{
    std::lock_guard<std::mutex> lock (rootMu);
    return rootDir;
}

void UserSlots::ensureUserFolders() const
{
    juce::File base;
    {
        std::lock_guard<std::mutex> lock (rootMu);
        base = rootDir;
    }
    base.createDirectory();
    for (int i = 0; i < kUserSlotCount; ++i)
        base.getChildFile ("USER").getChildFile (userSlotFolder (i)).createDirectory();
}

bool UserSlots::assign (int slot, const juce::File& file, juce::String& error)
{
    if (! validSlot (slot))
    {
        error = "Unknown sample slot";
        return false;
    }
    if (! file.existsAsFile())
    {
        error = "Could not read " + file.getFileName();
        return false;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
    if (reader == nullptr)
    {
        error = "Could not read " + file.getFileName();
        return false;
    }
    if (reader->sampleRate < 44000.0 || reader->sampleRate > 96000.0)
    {
        error = "Sample rate must be 44.1–96 kHz";
        return false;
    }
    const double seconds = (double) reader->lengthInSamples / reader->sampleRate;
    if (seconds > 60.0)
    {
        error = file.getFileName() + " is longer than 60 seconds";
        return false;
    }
    if (reader->lengthInSamples < 8)
    {
        error = file.getFileName() + " is too short";
        return false;
    }
    reader.reset();

    juce::File base;
    {
        std::lock_guard<std::mutex> lock (rootMu);
        base = rootDir;
    }
    ensureUserFolders();

    std::string relative;
    if (file.isAChildOf (base))
    {
        relative = forwardSlashes (file.getRelativePathFrom (base));
    }
    else
    {
        const juce::int64 before = file.getSize();
        auto dir = base.getChildFile ("USER").getChildFile (userSlotFolder (slot));
        dir.createDirectory();
        juce::File dest = dir.getChildFile (file.getFileName());
        int suffix = 2;
        while (dest.existsAsFile())
        {
            dest = dir.getChildFile (file.getFileNameWithoutExtension() + "-" + juce::String (suffix)
                                     + file.getFileExtension());
            ++suffix;
        }
        if (! file.copyFileTo (dest))
        {
            error = "Could not copy " + file.getFileName();
            return false;
        }
        if (file.getSize() != before)
        {
            error = "Source file changed while copying";
            dest.deleteFile();
            return false;
        }
        relative = forwardSlashes (dest.getRelativePathFrom (base));
    }

    if (! safeRelative (relative))
    {
        error = "Sample path is not under the samples folder";
        return false;
    }

    {
        std::lock_guard<std::mutex> lock (editMu);
        auto& row = edit[slot];
        row.file = relative;
        row.mode = (int) SampleMode::Sample;
        row.layerMix = userSlotIsDrum (slot) ? 0.75f : 0.5f;
        row.status = 1;
        row.audio = nullptr;
        row.error.clear();
        row.rootEdited = false;
        row.bytes = 0;
        row.prepGainDb = 0.f;
        row.prepCapped = false;
    }
    publish();
    enqueue (slot);
    error = juce::String (relative);
    return true;
}

void UserSlots::clear (int slot)
{
    if (! validSlot (slot))
        return;
    {
        std::lock_guard<std::mutex> lock (editMu);
        edit[slot] = {};
    }
    publish();
}

void UserSlots::setMode (int slot, int mode)
{
    if (! validSlot (slot))
        return;
    mode = std::clamp (mode, 0, 2);
    {
        std::lock_guard<std::mutex> lock (editMu);
        edit[slot].mode = mode;
        if (mode != 0 && edit[slot].file.empty())
        {
            edit[slot].status = 3;
            edit[slot].error = "No sample in this slot";
        }
    }
    publish();
    if (mode != 0)
        enqueue (slot);
}

void UserSlots::setRootNote (int slot, int midiNote, float cents)
{
    if (! validSlot (slot))
        return;
    {
        std::lock_guard<std::mutex> lock (editMu);
        edit[slot].root = std::clamp (midiNote, 0, 127);
        edit[slot].cents = std::clamp (cents, -100.f, 100.f);
        edit[slot].rootEdited = true;
    }
    publish();
}

void UserSlots::setStart (int slot, float seconds)
{
    if (! validSlot (slot))
        return;
    {
        std::lock_guard<std::mutex> lock (editMu);
        edit[slot].start = std::clamp (seconds, 0.f, 60.f);
    }
    publish();
}

void UserSlots::setReverse (int slot, bool reverse)
{
    if (! validSlot (slot))
        return;
    {
        std::lock_guard<std::mutex> lock (editMu);
        edit[slot].reverse = reverse;
    }
    publish();
}

void UserSlots::setLoop (int slot, bool on, float start, float end, float xfade)
{
    if (! validSlot (slot))
        return;
    {
        std::lock_guard<std::mutex> lock (editMu);
        edit[slot].loopOn = on;
        edit[slot].loopStart = std::max (0.f, start);
        edit[slot].loopEnd = std::max (0.f, end);
        edit[slot].loopXfade = std::max (0.f, xfade);
    }
    publish();
}

void UserSlots::setLayerMix (int slot, float mix)
{
    if (! validSlot (slot))
        return;
    {
        std::lock_guard<std::mutex> lock (editMu);
        edit[slot].layerMix = std::clamp (mix, 0.f, 1.f);
    }
    publish();
}

void UserSlots::setGainDb (int slot, float db)
{
    if (! validSlot (slot))
        return;
    {
        std::lock_guard<std::mutex> lock (editMu);
        edit[slot].gainDb = std::clamp (db, -12.f, 12.f);
    }
    publish();
}

int UserSlots::mode (int slot) const noexcept
{
    if (! validSlot (slot))
        return 0;
    return modeAtomic[slot].load (std::memory_order_acquire);
}

bool UserSlots::warning (int slot) const noexcept
{
    if (! validSlot (slot))
        return false;
    return warnAtomic[slot].load (std::memory_order_acquire) != 0;
}

int UserSlots::status (int slot) const noexcept
{
    if (! validSlot (slot))
        return 0;
    return statusAtomic[slot].load (std::memory_order_acquire);
}

juce::String UserSlots::file (int slot) const
{
    if (! validSlot (slot))
        return {};
    std::lock_guard<std::mutex> lock (editMu);
    return juce::String (edit[slot].file);
}

int UserSlots::rootNote (int slot) const
{
    if (! validSlot (slot))
        return 60;
    std::lock_guard<std::mutex> lock (editMu);
    return edit[slot].root;
}

float UserSlots::prepGainDb (int slot) const
{
    if (! validSlot (slot))
        return 0.f;
    std::lock_guard<std::mutex> lock (editMu);
    return edit[slot].prepGainDb;
}

bool UserSlots::prepCapped (int slot) const
{
    if (! validSlot (slot))
        return false;
    std::lock_guard<std::mutex> lock (editMu);
    return edit[slot].prepCapped;
}

juce::String UserSlots::lastError (int slot) const
{
    if (! validSlot (slot))
        return {};
    std::lock_guard<std::mutex> lock (editMu);
    return juce::String (edit[slot].error);
}

const UserSlotView* UserSlots::published() const noexcept
{
    return view.load (std::memory_order_acquire);
}

std::uint64_t UserSlots::residentBytes() const noexcept
{
    return resident.load (std::memory_order_acquire);
}

void UserSlots::publish()
{
    auto next = std::make_unique<UserSlotView>();
    std::uint64_t bytes = 0;
    std::lock_guard<std::mutex> lock (editMu);
    for (int i = 0; i < kUserSlotCount; ++i)
    {
        const auto& src = edit[i];
        auto& pod = next->slots[i];
        pod.mode = (std::int8_t) src.mode;
        pod.status = (std::int8_t) src.status;
        pod.reverse = src.reverse ? 1 : 0;
        pod.loopOn = src.loopOn ? 1 : 0;
        pod.root = src.root;
        pod.cents = src.cents;
        pod.start = src.start;
        pod.loopStart = src.loopStart;
        pod.loopEnd = src.loopEnd;
        pod.loopXfade = src.loopXfade;
        pod.layerMix = src.layerMix;
        pod.gainDb = src.gainDb;
        pod.prepGainDb = src.prepGainDb;
        pod.prepCapped = src.prepCapped ? 1 : 0;
        copyPath (pod.file, 160, src.file);
        pod.audio = src.status == 2 ? src.audio : nullptr;
        const bool warn = src.mode != 0 && (src.status == 3 || src.file.empty());
        modeAtomic[i].store (src.mode, std::memory_order_relaxed);
        statusAtomic[i].store (src.status, std::memory_order_relaxed);
        warnAtomic[i].store (warn ? 1 : 0, std::memory_order_relaxed);
        bytes += src.bytes;
    }
    const UserSlotView* raw = next.get();
    keptViews.push_back (std::move (next));
    view.store (raw, std::memory_order_release);
    resident.store (bytes, std::memory_order_relaxed);
}

void UserSlots::writeState (juce::ValueTree& parent) const
{
    auto slots = juce::ValueTree ("userSlots");
    slots.setProperty ("v", 1, nullptr);
    std::lock_guard<std::mutex> lock (editMu);
    for (int i = 0; i < kUserSlotCount; ++i)
    {
        const auto& src = edit[i];
        if (src.mode == 0 && src.file.empty())
            continue;
        auto child = juce::ValueTree ("slot");
        child.setProperty ("id", i, nullptr);
        child.setProperty ("mode", src.mode, nullptr);
        child.setProperty ("file", juce::String (src.file), nullptr);
        child.setProperty ("root", src.root, nullptr);
        child.setProperty ("cents", src.cents, nullptr);
        child.setProperty ("rootEdited", src.rootEdited ? 1 : 0, nullptr);
        child.setProperty ("start", src.start, nullptr);
        child.setProperty ("reverse", src.reverse ? 1 : 0, nullptr);
        child.setProperty ("loopOn", src.loopOn ? 1 : 0, nullptr);
        child.setProperty ("loopStart", src.loopStart, nullptr);
        child.setProperty ("loopEnd", src.loopEnd, nullptr);
        child.setProperty ("loopXfade", src.loopXfade, nullptr);
        child.setProperty ("layerMix", src.layerMix, nullptr);
        child.setProperty ("gainDb", src.gainDb, nullptr);
        slots.appendChild (child, nullptr);
    }
    parent.appendChild (slots, nullptr);
}

void UserSlots::readState (const juce::ValueTree& parent)
{
    auto slots = parent.getChildWithName ("userSlots");
    {
        std::lock_guard<std::mutex> lock (editMu);
        for (auto& row : edit)
            row = {};
        if (slots.isValid())
        {
            for (int i = 0; i < slots.getNumChildren(); ++i)
            {
                auto child = slots.getChild (i);
                const int id = (int) child.getProperty ("id");
                if (! validSlot (id))
                    continue;
                auto& row = edit[id];
                row.mode = std::clamp ((int) child.getProperty ("mode"), 0, 2);
                row.file = child.getProperty ("file").toString().toStdString();
                if (! safeRelative (row.file))
                    row.file.clear();
                row.root = std::clamp ((int) child.getProperty ("root", 60), 0, 127);
                row.cents = std::clamp ((float) child.getProperty ("cents"), -100.f, 100.f);
                row.rootEdited = (int) child.getProperty ("rootEdited") != 0;
                row.start = std::clamp ((float) child.getProperty ("start"), 0.f, 60.f);
                row.reverse = (int) child.getProperty ("reverse") != 0;
                row.loopOn = (int) child.getProperty ("loopOn") != 0;
                row.loopStart = std::max (0.f, (float) child.getProperty ("loopStart"));
                row.loopEnd = std::max (0.f, (float) child.getProperty ("loopEnd"));
                row.loopXfade = std::max (0.f, (float) child.getProperty ("loopXfade"));
                row.layerMix = std::clamp ((float) child.getProperty ("layerMix", row.layerMix), 0.f, 1.f);
                row.gainDb = std::clamp ((float) child.getProperty ("gainDb"), -12.f, 12.f);
                row.status = row.file.empty() ? (row.mode != 0 ? 3 : 0) : 1;
            }
        }
    }
    publish();
    for (int i = 0; i < kUserSlotCount; ++i)
        enqueue (i);
}

void UserSlots::enqueue (int slot)
{
    if (! validSlot (slot))
        return;
    {
        std::lock_guard<std::mutex> lock (jobMu);
        jobs.push_back (slot);
    }
    jobCv.notify_one();
}

void UserSlots::threadMain()
{
    while (! stop.load())
    {
        int slot = -1;
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
            slot = jobs.back();
            jobs.pop_back();
        }
        loadSlot (slot);
    }
}

void UserSlots::loadSlot (int slot)
{
    if (! validSlot (slot))
        return;
    std::string relative;
    {
        std::lock_guard<std::mutex> lock (editMu);
        relative = edit[slot].file;
        if (relative.empty() || edit[slot].mode == 0)
            return;
        edit[slot].status = 1;
        edit[slot].audio = nullptr;
    }
    publish();

    auto fail = [&] (const std::string& message)
    {
        std::lock_guard<std::mutex> lock (editMu);
        if (edit[slot].file != relative)
            return;
        edit[slot].status = 3;
        edit[slot].audio = nullptr;
        edit[slot].error = message;
        edit[slot].bytes = 0;
    };

    if (! safeRelative (relative))
    {
        fail ("Sample path is not under the samples folder");
        publish();
        return;
    }

    juce::File file;
    {
        std::lock_guard<std::mutex> lock (rootMu);
        file = rootDir.getChildFile (juce::String (relative).replaceCharacter ('\\', '/'));
    }
    const juce::int64 sizeBefore = file.getSize();
    if (! file.existsAsFile())
    {
        fail ("Missing " + relative);
        publish();
        return;
    }

    try
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->lengthInSamples < 8
            || reader->sampleRate < 44000.0 || reader->sampleRate > 96000.0)
        {
            fail ("Could not read " + relative);
            publish();
            return;
        }
        const double seconds = (double) reader->lengthInSamples / reader->sampleRate;
        if (seconds > 60.0)
        {
            fail (relative + " is longer than 60 seconds");
            publish();
            return;
        }
        const int frames = (int) reader->lengthInSamples;
        const int channels = std::max (1, (int) reader->numChannels);
        juce::AudioBuffer<float> buffer (channels, frames);
        if (! reader->read (&buffer, 0, frames, 0, true, true))
        {
            fail ("Could not read " + relative);
            publish();
            return;
        }
        if (file.getSize() != sizeBefore)
        {
            fail ("Sample file changed while reading");
            publish();
            return;
        }

        std::vector<const float*> ptrs ((size_t) channels);
        for (int c = 0; c < channels; ++c)
            ptrs[(size_t) c] = buffer.getReadPointer (c);
        auto prepared = prepareSample (ptrs.data(), channels, frames, reader->sampleRate);
        if (prepared.frames < 8)
        {
            fail ("Could not read " + relative);
            publish();
            return;
        }
        const std::uint64_t incoming = (std::uint64_t) prepared.frames * (std::uint64_t) sizeof (float) * 2ull;
        std::uint64_t already = 0;
        {
            std::lock_guard<std::mutex> lock (editMu);
            for (int i = 0; i < kUserSlotCount; ++i)
                if (i != slot)
                    already += edit[i].bytes;
        }
        if (! prepared.report.silent && ! sampleBytesFit (already, incoming))
        {
            fail ("Sample memory budget (128 MB)");
            publish();
            return;
        }

        std::vector<float> mono ((size_t) std::max (1, prepared.frames));
        for (int i = 0; i < prepared.frames; ++i)
            mono[(size_t) i] = 0.5f * (prepared.left[(size_t) i] + prepared.right[(size_t) i]);
        const int detected = chooseSlotRoot (slot, mono.data(), prepared.frames, prepared.rate, relative.c_str());

        auto audio = std::make_unique<SampleAudio>();
        audio->path = relative;
        audio->frames = prepared.frames;
        audio->rate = prepared.rate;
        audio->left = std::move (prepared.left);
        audio->right = std::move (prepared.right);
        audio->mono = audio->right.empty() || audio->right == audio->left;
        audio->sliceCount = 1;
        audio->slices[0] = 0;
        // A dropped vox file starts on its voice too (in memory; the file is not written).
        audio->loudStart = loudestRegionStart (audio->left.data(), audio->frames, audio->rate);
        audio->voicedCount = voicedOnsets (audio->left.data(), audio->right.empty() ? nullptr : audio->right.data(),
                                           audio->frames, audio->rate, audio->slices, audio->sliceCount,
                                           audio->loudStart, audio->voiced, 24, audio->voicedFloor);

        {
            std::lock_guard<std::mutex> lock (editMu);
            if (edit[slot].file != relative)
                return;
            keptAudio.push_back (std::move (audio));
            edit[slot].audio = keptAudio.back().get();
            edit[slot].status = 2;
            edit[slot].error.clear();
            edit[slot].bytes = incoming;
            edit[slot].prepGainDb = prepared.report.gainDb;
            edit[slot].prepCapped = prepared.report.capped;
            if (! edit[slot].rootEdited)
                edit[slot].root = detected;
        }
        publish();
    }
    catch (...)
    {
        fail ("Could not read " + relative);
        publish();
    }
}

} // namespace rolla
