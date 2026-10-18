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

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <cstdlib>
#include <memory>

namespace rolla::settings
{

// Test hooks. Tools and tests (and a plugin-rules test build with ROLLA_TEST_HOOKS=1)
// read ROLLA_BUNDLE_ROOT and ROLLA_DEFAULT_ROOT in place of the bundle's sample
// folder and ~/Music/ROLLA/Samples. A shipped plugin ignores both.
inline juce::File testFolder (const char* name)
{
#if ! ROLLA_PLUGIN_BUILD || ROLLA_TEST_HOOKS
    if (const char* value = std::getenv (name))
        if (value[0] != 0 && juce::File::isAbsolutePath (juce::String::fromUTF8 (value)))
            return juce::File (juce::String::fromUTF8 (value));
#else
    juce::ignoreUnused (name);
#endif
    return {};
}

// ROLLA_CACHE_DIR moves every cache (sample index, loudness, drag exports).
inline juce::File cacheOverride()
{
    if (const char* value = std::getenv ("ROLLA_CACHE_DIR"))
        if (value[0] != 0 && juce::File::isAbsolutePath (juce::String::fromUTF8 (value)))
            return juce::File (juce::String::fromUTF8 (value));
    return {};
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

inline juce::File file()
{
    if (const char* dir = std::getenv ("ROLLA_SETTINGS_DIR"))
        if (dir[0] != 0 && juce::File::isAbsolutePath (juce::String::fromUTF8 (dir)))
            return juce::File (juce::String::fromUTF8 (dir)).getChildFile ("ROLLA.settings");
    return options().getDefaultFile();
}

inline std::unique_ptr<juce::PropertiesFile> open()
{
    const auto f = file();
    f.getParentDirectory().createDirectory();
    return std::make_unique<juce::PropertiesFile> (f, options());
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

} // namespace rolla::settings
