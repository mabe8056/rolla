#pragma once

// The one place ROLLA.settings is opened (processor and editor).
//
// ROLLA_SETTINGS_DIR (environment) moves the file into that folder. Tests set
// it so they never read or write a real user's settings: on macOS JUCE finds
// the home folder through NSHomeDirectory(), which ignores $HOME, so a $HOME
// redirect does not protect the real file.
//
// ROLLA_BUNDLE_ROOT, ROLLA_DEFAULT_ROOT and ROLLA_CACHE_DIR do the same for the
// sample folders and caches (see testFolder and cacheOverride below).
//
// A sample root under a temp folder (the system temp directory, /tmp,
// /private/tmp, /var/folders, /private/var/folders) is never written: a test's
// generated tone pack must not become a user's library.
//
// Test-mode guard. ROLLA_TEST_MODE=1 (set by tests/common/ScratchProfile.h,
// with ROLLA_SCRATCH_DIR) marks a test or CI tool run. In that mode the
// settings file, every cache folder and the default sample root go through
// guardPath(): a path inside one of the real user's ROLLA folders
// (realProfileFolders()), or outside both the scratch folder and the temp
// folders, is logged and replaced by the same place under the scratch folder.
// writeAllowed() refuses folder creation and user-slot copies inside a real
// ROLLA folder. Shipped plugin builds compile the guard out, as they do the
// test hooks: without ROLLA_TEST_MODE nothing below changes a path.

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdlib>
#include <memory>

#if ! JUCE_WINDOWS
 #include <pwd.h>
 #include <unistd.h>
#endif

namespace rolla::settings
{

// An absolute folder from the environment, or an empty File.
inline juce::File envFolder (const char* name)
{
    if (const char* value = std::getenv (name))
        if (value[0] != 0 && juce::File::isAbsolutePath (juce::String::fromUTF8 (value)))
            return juce::File (juce::String::fromUTF8 (value));
    return {};
}

// Test hooks. Tools and tests (and a plugin-rules test build with ROLLA_TEST_HOOKS=1)
// read ROLLA_BUNDLE_ROOT and ROLLA_DEFAULT_ROOT in place of the bundle's sample
// folder and ~/Music/ROLLA/Samples. A shipped plugin ignores both.
inline juce::File testFolder (const char* name)
{
#if ! ROLLA_PLUGIN_BUILD || ROLLA_TEST_HOOKS
    return envFolder (name);
#else
    juce::ignoreUnused (name);
    return {};
#endif
}

// ROLLA_CACHE_DIR moves every cache (sample index, loudness, drag exports).
inline juce::File cacheOverride()
{
    return envFolder ("ROLLA_CACHE_DIR");
}

inline juce::PropertiesFile::Options options()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "ROLLA";
    o.filenameSuffix = "settings";
    o.osxLibrarySubFolder = "Application Support";
    o.folderName = "ROLLA";
    o.storageFormat = juce::PropertiesFile::storeAsXML;
    return o;
}

inline bool isTempLocation (const juce::File& path)
{
    const auto full = path.getFullPathName();
    if (full.isEmpty())
        return false;
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    if (path == temp || path.isAChildOf (temp))
        return true;
    for (const char* prefix : { "/tmp/", "/private/tmp/", "/var/folders/", "/private/var/folders/" })
        if (full.startsWith (prefix) || full + "/" == prefix)
            return true;
    return false;
}

// ---- Test-mode guard -------------------------------------------------------

inline bool testMode()
{
#if ! ROLLA_PLUGIN_BUILD || ROLLA_TEST_HOOKS
    if (const char* v = std::getenv ("ROLLA_TEST_MODE"))
        return v[0] != 0 && ! (v[0] == '0' && v[1] == 0);
#endif
    return false;
}

// ROLLA_SCRATCH_DIR, else <temp>/rolla-test-scratch.
inline juce::File scratchDir()
{
    if (const auto dir = envFolder ("ROLLA_SCRATCH_DIR"); dir != juce::File())
        return dir;
    return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("rolla-test-scratch");
}

inline bool insideScratchOrTemp (const juce::File& path)
{
    const auto scratch = scratchDir();
    return path == scratch || path.isAChildOf (scratch) || isTempLocation (path);
}

// The real user's home folder(s), never the scratch one. $HOME is not trusted:
// a scratch profile moves it, and on Linux JUCE's userHomeDirectory follows it.
inline juce::Array<juce::File> realHomes()
{
    juce::Array<juce::File> homes;
    auto add = [&homes] (const juce::String& path)
    {
        if (path.isEmpty() || ! juce::File::isAbsolutePath (path))
            return;
        const juce::File f (path);
        if (! insideScratchOrTemp (f))
            homes.addIfNotAlreadyThere (f);
    };
#if ! JUCE_WINDOWS
    if (const auto* pw = getpwuid (getuid()))
        if (pw->pw_dir != nullptr)
            add (juce::String::fromUTF8 (pw->pw_dir));
#endif
    if (const char* real = std::getenv ("ROLLA_REAL_HOME"))
        add (juce::String::fromUTF8 (real));
    // NSHomeDirectory() on macOS and the profile folder on Windows ignore $HOME.
    add (juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName());
    return homes;
}

// Every folder a real ROLLA install reads or writes: Music/ROLLA, Documents/ROLLA,
// Application Support/ROLLA, Library/Caches/ROLLA, ~/.cache/ROLLA, ~/.config/ROLLA,
// AppData/{Roaming,Local}/ROLLA, plus JUCE's own app-data / music / documents ROLLA
// folders and the default ROLLA.settings folder.
inline juce::Array<juce::File> realProfileFolders()
{
    juce::Array<juce::File> out;
    auto add = [&out] (const juce::File& f)
    {
        if (f.getFullPathName().isNotEmpty() && ! insideScratchOrTemp (f))
            out.addIfNotAlreadyThere (f);
    };
    for (const auto& home : realHomes())
    {
        add (home.getChildFile ("Music").getChildFile ("ROLLA"));
        add (home.getChildFile ("Documents").getChildFile ("ROLLA"));
        add (home.getChildFile ("Library").getChildFile ("Application Support").getChildFile ("ROLLA"));
        add (home.getChildFile ("Library").getChildFile ("Caches").getChildFile ("ROLLA"));
        add (home.getChildFile (".cache").getChildFile ("ROLLA"));
        add (home.getChildFile (".config").getChildFile ("ROLLA"));
        add (home.getChildFile ("AppData").getChildFile ("Roaming").getChildFile ("ROLLA"));
        add (home.getChildFile ("AppData").getChildFile ("Local").getChildFile ("ROLLA"));
    }
    for (auto loc : { juce::File::userApplicationDataDirectory, juce::File::userMusicDirectory, juce::File::userDocumentsDirectory })
        add (juce::File::getSpecialLocation (loc).getChildFile ("ROLLA"));
    add (options().getDefaultFile().getParentDirectory());
    return out;
}

inline bool isRealProfilePath (const juce::File& path)
{
    for (const auto& folder : realProfileFolders())
        if (path == folder || path.isAChildOf (folder))
            return true;
    return false;
}

// Test mode: `path` if it is inside the scratch or a temp folder and not inside a
// real ROLLA folder; otherwise <scratch>/<fallback>, logged. Else `path` as is.
inline juce::File guardPath (const juce::File& path, const char* what, const char* fallback)
{
    if (! testMode())
        return path;
    if (insideScratchOrTemp (path) && ! isRealProfilePath (path))
        return path;
    const auto safe = scratchDir().getChildFile (fallback);
    juce::Logger::writeToLog ("ROLLA_TEST_MODE: refusing " + juce::String (what) + " at "
                              + path.getFullPathName() + "; using " + safe.getFullPathName());
    return safe;
}

// False (logged) only in test mode for a path inside a real ROLLA folder.
inline bool writeAllowed (const juce::File& path, const char* what)
{
    if (! testMode() || ! isRealProfilePath (path))
        return true;
    juce::Logger::writeToLog ("ROLLA_TEST_MODE: refusing to write " + juce::String (what) + " in "
                              + path.getFullPathName());
    return false;
}

// ---------------------------------------------------------------------------

inline juce::File file()
{
    auto f = options().getDefaultFile();
    if (const auto dir = envFolder ("ROLLA_SETTINGS_DIR"); dir != juce::File())
        f = dir.getChildFile ("ROLLA.settings");
    return guardPath (f, "settings", "settings/ROLLA.settings");
}

inline std::unique_ptr<juce::PropertiesFile> open()
{
    const auto f = file();
    f.getParentDirectory().createDirectory();
    return std::make_unique<juce::PropertiesFile> (f, options());
}

} // namespace rolla::settings
