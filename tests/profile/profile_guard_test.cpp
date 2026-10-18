// rolla-profile-test: nothing in a test touches a real ROLLA profile.
//
//   rolla-profile-test
//
// On a scratch profile (tests/common/ScratchProfile.h) every path ROLLA resolves
// for settings, caches, the default sample root, the drag export cache and the
// user-slot copy folders is inside the scratch folder. The test-mode guard
// (src/Engine/RollaSettings.h) turns a real user's ROLLA folder back into the
// scratch folder. Real folders are only handed to resolvers that create nothing
// (settings::file, defaultRoot, guardPath, writeAllowed), and the run checks
// that no real ROLLA folder appeared while it ran.

#include "PluginProcessor.h"
#include "Engine/RollaSettings.h"
#include "Engine/SampleLibrary.h"
#include "Sampler/UserSlots.h"
#include "../common/ScratchProfile.h"

#include <juce_events/juce_events.h>

#include <iostream>
#include <string>
#include <vector>

namespace
{
int fails = 0;

void expect (bool ok, const std::string& msg)
{
    std::cout << (ok ? "PASS " : "FAIL ") << msg << "\n";
    if (! ok)
        ++fails;
}

std::string str (const juce::File& f)
{
    return f.getFullPathName().toStdString();
}
} // namespace

int main()
{
    const auto home = rolla::test::useScratchProfile ("profile");
    if (home.empty())
    {
        std::cerr << "could not set up a scratch profile\n";
        return 1;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    const juce::File scratch (juce::String::fromUTF8 (home.c_str()));
    std::cout << "scratch profile: " << str (scratch) << "\n";

    // Read-only snapshot: which real ROLLA folders exist before the run.
    const auto realFolders = rolla::settings::realProfileFolders();
    std::vector<bool> existed;
    for (const auto& f : realFolders)
    {
        std::cout << "real ROLLA folder (never touched): " << str (f) << "\n";
        existed.push_back (f.exists());
    }

    expect (rolla::settings::testMode(), "ROLLA_TEST_MODE is on");
    expect (rolla::settings::scratchDir() == scratch, "ROLLA_SCRATCH_DIR is the scratch folder");
    expect (! rolla::settings::isRealProfilePath (scratch), "the scratch folder is not a real ROLLA folder");

    auto inside = [&] (const char* what, const juce::File& f)
    {
        expect (f.isAChildOf (scratch) && ! rolla::settings::isRealProfilePath (f),
                std::string (what) + " is in the scratch profile (" + str (f) + ")");
    };

    // 1. Every resolved path is inside the scratch folder.
    using rolla::SampleLibrary;
    inside ("settings file", rolla::settings::file());
    inside ("default sample root", SampleLibrary::defaultRoot());
    inside ("index cache", rolla::sampleCacheFor (SampleLibrary::defaultRoot(), "index"));
    inside ("loudness cache", rolla::loudnessCacheFor (SampleLibrary::defaultRoot()));
    inside ("user-slot root for the bundle", SampleLibrary::userSlotRootFor (SampleLibrary::bundledRoot()));
    {
        rolla::UserSlots slots;
        inside ("UserSlots root", slots.root());
        for (int slot = 0; slot < rolla::kUserSlotCount; ++slot)
            inside (("user-slot copy folder " + std::to_string (slot)).c_str(), slots.copyFolder (slot));
    }
    {
        rolla::RollaProcessor proc;
        proc.waitUntilReady();
        inside ("export cache", proc.exportCache());
        inside ("processor sample root", proc.samplesRoot());
        inside ("processor user-slot copy folder", proc.userSlots().copyFolder (rolla::SlotBass));
    }

    // 2. The guard refuses real folders. Side-effect-free resolvers only.
    if (realFolders.isEmpty())
        std::cout << "no real home found; guard refusal checks skipped\n";
    for (const auto& f : realFolders)
    {
        const auto child = f.getChildFile ("Samples");
        expect (rolla::settings::isRealProfilePath (child), "recognised as real: " + str (child));
        inside (("guardPath of " + str (child)).c_str(), rolla::settings::guardPath (child, "test path", "guarded"));
        expect (! rolla::settings::writeAllowed (child, "test path"), "writeAllowed refuses " + str (child));
    }
    const auto homes = rolla::settings::realHomes();
    if (! homes.isEmpty())
    {
        const auto realHome = homes.getFirst();
        const auto saveSettings = rolla::test::envValue ("ROLLA_SETTINGS_DIR");
        const auto saveRoot = rolla::test::envValue ("ROLLA_DEFAULT_ROOT");

        rolla::test::putEnv ("ROLLA_SETTINGS_DIR",
                             str (realHome.getChildFile ("Library").getChildFile ("Application Support").getChildFile ("ROLLA")));
        inside ("settings file with ROLLA_SETTINGS_DIR at the real Application Support/ROLLA", rolla::settings::file());
        rolla::test::putEnv ("ROLLA_SETTINGS_DIR", str (realHome.getChildFile (".config").getChildFile ("ROLLA")));
        inside ("settings file with ROLLA_SETTINGS_DIR at the real ~/.config/ROLLA", rolla::settings::file());

        rolla::test::putEnv ("ROLLA_DEFAULT_ROOT", str (realHome.getChildFile ("Music").getChildFile ("ROLLA").getChildFile ("Samples")));
        inside ("default root with ROLLA_DEFAULT_ROOT at the real ~/Music/ROLLA/Samples", SampleLibrary::defaultRoot());

        // Outside scratch and temp but not a ROLLA folder: still refused in test mode.
        inside ("guardPath of a non-temp folder", rolla::settings::guardPath (realHome.getChildFile ("rolla-not-scratch"), "test path", "guarded"));

        rolla::test::putEnv ("ROLLA_SETTINGS_DIR", saveSettings);
        rolla::test::putEnv ("ROLLA_DEFAULT_ROOT", saveRoot);
        inside ("settings file after restoring the scratch env", rolla::settings::file());

        // 3. Outside test mode the guard changes nothing (plugin behaviour).
        const auto real = realHome.getChildFile ("Music").getChildFile ("ROLLA").getChildFile ("Samples");
        rolla::test::putEnv ("ROLLA_TEST_MODE", "0");
        expect (! rolla::settings::testMode(), "ROLLA_TEST_MODE=0 turns the guard off");
        expect (rolla::settings::guardPath (real, "test path", "guarded") == real, "guard off: guardPath returns the path as is");
        expect (rolla::settings::writeAllowed (real, "test path"), "guard off: writeAllowed is true");
        rolla::test::putEnv ("ROLLA_TEST_MODE", "1");
        expect (rolla::settings::testMode(), "ROLLA_TEST_MODE back on");
    }

    // No real ROLLA folder was created by this run.
    for (int i = 0; i < realFolders.size(); ++i)
        expect (existed[(size_t) i] || ! realFolders[i].exists(), "not created: " + str (realFolders[i]));

    scratch.deleteRecursively();
    std::cout << (fails == 0 ? "profile tests passed\n" : std::to_string (fails) + " profile test failure(s)\n");
    return fails == 0 ? 0 : 1;
}
