#pragma once

// Every test and CI tool that can reach ROLLA's settings, caches or sample
// folders calls rolla::test::useScratchProfile() first thing in main, before
// JUCE starts. It points HOME, ROLLA.settings, the caches, ~/Music/ROLLA/Samples
// and the bundle's sample folder at a fresh temp folder, so no run (even a
// killed one) reads or writes a real user's settings, caches or sample library.
//
// It also sets ROLLA_TEST_MODE=1 and ROLLA_SCRATCH_DIR, which turn on the guard
// in src/Engine/RollaSettings.h: any settings / cache / sample-root path that
// still lands in a real ROLLA folder is logged and moved into the scratch folder.
// ROLLA_REAL_HOME keeps the original $HOME so the guard knows what to refuse.
//
// $HOME alone is not enough: on macOS JUCE finds the home folder through
// NSHomeDirectory(), which ignores it. The ROLLA_* variables are what move
// the files (src/Engine/RollaSettings.h).

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>

#if defined(_WIN32)
 #include <process.h>
#else
 #include <unistd.h>
#endif

namespace rolla::test
{

inline bool putEnv (const char* name, const std::string& value)
{
#if defined(_WIN32)
    return _putenv_s (name, value.c_str()) == 0;
#else
    return setenv (name, value.c_str(), 1) == 0;
#endif
}

inline std::string envValue (const char* name)
{
    const char* v = std::getenv (name);
    return v != nullptr ? std::string (v) : std::string();
}

// A complete scratch profile handed down by a parent (tests/common/scratch_env.py),
// or an empty string.
inline std::string inheritedScratchProfile()
{
    namespace fs = std::filesystem;
    if (envValue ("ROLLA_TEST_MODE") != "1")
        return {};
    const auto dir = envValue ("ROLLA_SCRATCH_DIR");
    std::error_code ec;
    if (dir.empty() || ! fs::path (dir).is_absolute() || ! fs::is_directory (fs::path (dir), ec))
        return {};
    for (const char* name : { "ROLLA_SETTINGS_DIR", "ROLLA_CACHE_DIR", "ROLLA_DEFAULT_ROOT", "ROLLA_BUNDLE_ROOT" })
        if (envValue (name).empty())
            return {};
    return dir;
}

// Returns the scratch folder, or an empty string if it could not be set up
// (the caller should then stop before touching anything).
// inheritParent: tools launched by a script that already set up a scratch
// profile keep it (so parallel runs share one cache). Tests always get a fresh one.
inline std::string useScratchProfile (const char* tag, bool inheritParent = false)
{
    if (inheritParent)
        if (auto parent = inheritedScratchProfile(); ! parent.empty())
            return parent;

    namespace fs = std::filesystem;
    std::error_code ec;
    const auto base = fs::temp_directory_path (ec);
    if (ec)
        return {};
#if defined(_WIN32)
    const auto pid = (long long) _getpid();
#else
    const auto pid = (long long) getpid();
#endif
    const auto stamp = (long long) std::chrono::steady_clock::now().time_since_epoch().count();
    const auto home = base / ("rolla-" + std::string (tag) + "-" + std::to_string (pid) + "-" + std::to_string (stamp));
    if (! fs::create_directories (home, ec) || ec)
        return {};
    const std::string h = home.string();
    const std::string sep (1, (char) fs::path::preferred_separator);
    const auto realHome = envValue ("HOME");
    if (envValue ("ROLLA_REAL_HOME").empty() && ! realHome.empty() && envValue ("ROLLA_TEST_MODE") != "1")
        if (! putEnv ("ROLLA_REAL_HOME", realHome))
            return {};
    const bool ok = putEnv ("HOME", h)
                 && putEnv ("ROLLA_TEST_MODE", "1")
                 && putEnv ("ROLLA_SCRATCH_DIR", h)
                 && putEnv ("ROLLA_SETTINGS_DIR", h + sep + "settings")
                 && putEnv ("ROLLA_CACHE_DIR", h + sep + "cache")
                 && putEnv ("ROLLA_ORIG_INDEX", "") // the originality index then sits in the scratch settings folder
                 && putEnv ("ROLLA_DEFAULT_ROOT", h + sep + "Music" + sep + "ROLLA" + sep + "Samples")
                 && putEnv ("ROLLA_BUNDLE_ROOT", h + sep + "no-bundle" + sep + "Contents" + sep + "Resources" + sep + "Samples");
    return ok ? h : std::string();
}

// Tools on a scratch profile: `--samples DIR` anywhere on the command line becomes
// ROLLA_BUNDLE_ROOT, so that pack is read but never written (no lane folders, no
// USER copies; caches stay in the scratch profile). Call after useScratchProfile.
// Returns false if the variable could not be set.
inline bool useSamplesArg (int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string (argv[i]) == "--samples")
        {
            std::error_code ec;
            const auto dir = std::filesystem::absolute (std::filesystem::path (argv[i + 1]), ec);
            return ! ec && putEnv ("ROLLA_BUNDLE_ROOT", dir.string());
        }
    return true;
}

} // namespace rolla::test
