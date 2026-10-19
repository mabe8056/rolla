"""Environment for launching ROLLA binaries on a scratch profile.

The Python twin of tests/common/ScratchProfile.h: HOME, ROLLA.settings, the
caches, ~/Music/ROLLA/Samples and the bundle's sample folder all point inside
`root`, and ROLLA_TEST_MODE=1 turns on the guard in src/Engine/RollaSettings.h.
Tools that call useScratchProfile(tag, true) keep this profile instead of
making their own, so parallel runs share one cache.
"""

import os


def scratch_env(root, cache=None, base=None):
    root = os.path.abspath(str(root))
    os.makedirs(root, exist_ok=True)
    env = dict(os.environ if base is None else base)
    if not env.get("ROLLA_REAL_HOME") and env.get("ROLLA_TEST_MODE") != "1" and env.get("HOME"):
        env["ROLLA_REAL_HOME"] = env["HOME"]
    env.update({
        "ROLLA_TEST_MODE": "1",
        "ROLLA_SCRATCH_DIR": root,
        "HOME": root,
        "ROLLA_SETTINGS_DIR": os.path.join(root, "settings"),
        "ROLLA_CACHE_DIR": os.path.abspath(str(cache)) if cache else os.path.join(root, "cache"),
        "ROLLA_DEFAULT_ROOT": os.path.join(root, "Music", "ROLLA", "Samples"),
        "ROLLA_BUNDLE_ROOT": os.path.join(root, "no-bundle", "Contents", "Resources", "Samples"),
    })
    return env
