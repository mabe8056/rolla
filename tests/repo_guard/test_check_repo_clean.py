#!/usr/bin/env python3
"""Self-test for tools/check_repo_clean.py.

Builds throwaway git repos in a temp folder, plants one problem at a time,
and checks the guard names it (and passes the clean tree, and ignores files
git ignores). Fake secrets are assembled at run time so this file holds none.

  python3 tests/repo_guard/test_check_repo_clean.py
"""

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
GUARD = os.path.join(REPO, "tools", "check_repo_clean.py")

fails = 0


def check(ok, what):
    global fails
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


def git(root, *args):
    subprocess.run(["git", *args], cwd=root, check=True, capture_output=True)


def make_repo(files, gitignore=""):
    root = tempfile.mkdtemp(prefix="repo-guard-")
    git(root, "init", "-q")
    for rel, content in files.items():
        full = os.path.join(root, rel)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        mode = "wb" if isinstance(content, bytes) else "w"
        with open(full, mode) as f:
            f.write(content)
    if gitignore:
        with open(os.path.join(root, ".gitignore"), "w") as f:
            f.write(gitignore)
    git(root, "add", "-A")
    return root


def run(root, *extra):
    p = subprocess.run([sys.executable, GUARD, "--root", root, "--json", *extra], capture_output=True, text=True)
    return p.returncode, json.loads(p.stdout)


def flagged(res, category, needle):
    return any(needle in item for item in res["fails"][category])


CLEAN = {
    "src/a.cpp": "int main() { return 0; }\n",
    "README.md": "# clean\n",
    "resources/presets.json": json.dumps({"bass": {"octave": 1, "notes": [0, 3, 7]}}),
}


def main():
    code, res = run(make_repo(CLEAN))
    check(code == 0 and res["pass"], "clean tree passes")

    code, res = run(make_repo(dict(CLEAN, **{"build/x.wav": b"RIFF....WAVE"}), gitignore="build/\n*.wav\n"))
    check(code == 0, "ignored audio is not tracked, so it does not count")

    cases = [
        ("audio", "samples/kick.wav", b"RIFF\x00\x00\x00\x00WAVEfmt ", "kick.wav"),
        ("audio", "loops/hook.mp3", b"ID3\x03", "hook.mp3"),
        ("midi", "exports/roll.mid", b"MThd\x00\x00\x00\x06", "roll.mid"),
        ("secret", ".env", "TOKEN=abc\n", ".env"),
        ("secret", "keys/server.pem", "x\n", "server.pem"),
        ("secret", "src/k.h", "// " + "-----BEGIN " + "RSA PRIVATE KEY-----\n", "private key"),
        ("secret", "cfg.py", "GH = '" + "gh" + "p_" + "a1B2" * 9 + "'\n", "GitHub token"),
        ("secret", "cfg2.py", "key = '" + "AK" + "IA" + "ABCDEFGHIJKLMNOP" + "'\n", "AWS"),
        ("secret", "app.json", json.dumps({"api_key": "Zx9" + "q" * 20}), "assigned secret"),
        ("dump", "tools/out/gaskin/roll_00/x.txt", "x", "tools/out"),
        ("dump", "data/corpus.json", "{}", "corpus"),
        ("dump", "src/Generator/CorpusData.h", "int x;", "CorpusData"),
        ("large", "resources/big.json", json.dumps({"v": list(range(150000))}), "big.json"),
    ]
    for cat, rel, content, needle in cases:
        code, res = run(make_repo(dict(CLEAN, **{rel: content})))
        check(code == 1 and flagged(res, cat, needle), f"{cat}: {rel}")

    # Reference data, structural: a key shaped like a track name over note events.
    events = [{"start": i, "note": 36 + i % 12, "vel": 100} for i in range(32)]
    code, res = run(make_repo(dict(CLEAN, **{"research/drops.json": json.dumps({"Some Artist - Some Tune (Extended Mix)": events})})))
    check(code == 1 and flagged(res, "reference", "Extended Mix"), "reference: track-keyed note events in JSON")
    code, res = run(make_repo(dict(CLEAN, **{"research/drops.json": json.dumps({"tracks": [{"title": "Tune (Original Mix)", "notes": events}]})})))
    check(code == 1 and flagged(res, "reference", "Original Mix"), "reference: record with a track title and events")
    table = "const int kDrop[] = { " + ", ".join(str(i % 97) for i in range(400)) + " };\n"
    code, res = run(make_repo(dict(CLEAN, **{"src/Drop.h": '// source: "Some Artist - Some Tune"\n' + table})))
    check(code == 1 and flagged(res, "reference", "Some Artist"), "reference: track-named literal over a big table in code")
    code, res = run(make_repo(dict(CLEAN, **{"src/Table.h": table})))
    check(code == 0, "a big table with no track name passes")
    code, res = run(make_repo(dict(CLEAN, **{"docs/x.json": json.dumps({"Kick - Snare": [1, 2, 3]})})))
    check(code == 0, "a short track-shaped key without event data passes")

    # Reference data, by name: the list lives outside the repo.
    names_dir = tempfile.mkdtemp(prefix="repo-guard-names-")
    names = os.path.join(names_dir, "names.txt")
    with open(names, "w") as f:
        f.write("# reference names\nZed Example\n")
    root = make_repo(dict(CLEAN, **{"src/rules.py": "STYLE = 'zed example pool'\n", "docs/notes.md": "Measured from Zed Example.\n"}))
    code, res = run(root, "--names", names)
    check(code == 1 and flagged(res, "reference", "src/rules.py"), "reference by name: code hit fails")
    check(any("docs/notes.md" in w for w in res["warnings"]), "reference by name: prose hit is a warning")
    code, res = run(root)
    check(code == 0 and res["names_checked"] == 0, "no names file: name check is off")

    # Audio fixtures: allowlisted under tests/fixtures, but never over 64 KB.
    allow_audio = "audio tests/fixtures/*.wav  # tiny decoder fixture\n"
    small = b"RIFF" + b"\x00" * 1000
    big = b"RIFF" + b"\x00" * (65 * 1024)
    code, res = run(make_repo(dict(CLEAN, **{"tests/fixtures/click.wav": small, "tools/repo_guard_allow.txt": allow_audio})))
    check(code == 0, "audio: allowlisted fixture under 64 KB passes")
    code, res = run(make_repo(dict(CLEAN, **{"tests/fixtures/loop.wav": big, "tools/repo_guard_allow.txt": allow_audio})))
    check(code == 1 and flagged(res, "audio", "64 KB"), "audio: allowlisted fixture over 64 KB fails")

    # Allowlist.
    root = make_repo(dict(CLEAN, **{"tests/fixtures/two_bars.mid": b"MThd", "tools/repo_guard_allow.txt": "midi tests/fixtures/*.mid  # parser fixture\n"}))
    code, res = run(root)
    check(code == 0, "allowlisted MIDI fixture passes")

    # The real tree.
    p = subprocess.run([sys.executable, GUARD, "--root", REPO], capture_output=True, text=True)
    check(p.returncode == 0, "this repository passes: " + p.stdout.strip().splitlines()[-1])

    print(("repo guard self-test passed" if fails == 0 else f"{fails} repo guard self-test failure(s)"))
    return 0 if fails == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
