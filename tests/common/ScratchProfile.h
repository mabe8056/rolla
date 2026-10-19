#pragma once

// Every test that builds a RollaProcessor (or its editor) calls
// rolla::test::useScratchProfile() first thing in main, before JUCE starts.
// It points HOME, ROLLA.settings, the caches, ~/Music/ROLLA/Samples and the
// bundle's sample folder at a fresh temp folder, so no run (even a killed one)
// reads or writes a real user's settings, caches or sample library.
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

// Returns the scratch folder, or an empty string if it could not be set up
// (the caller should then stop before touching anything).
inline std::string useScratchProfile (const char* tag)
{
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
    const bool ok = putEnv ("HOME", h)
                 && putEnv ("ROLLA_SETTINGS_DIR", h + sep + "settings")
                 && putEnv ("ROLLA_CACHE_DIR", h + sep + "cache")
                 && putEnv ("ROLLA_ORIG_INDEX", "") // the originality index then sits in the scratch settings folder
                 && putEnv ("ROLLA_DEFAULT_ROOT", h + sep + "Music" + sep + "ROLLA" + sep + "Samples")
                 && putEnv ("ROLLA_BUNDLE_ROOT", h + sep + "no-bundle" + sep + "Contents" + sep + "Resources" + sep + "Samples");
    return ok ? h : std::string();
}

} // namespace rolla::test
