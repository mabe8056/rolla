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
        ("dump", "tools/out/jackin/roll_00/x.txt", "x", "tools/out"),
        ("dump", "data/corpus.json", "{}", "corpus"),
        ("dump", "src/Generator/CorpusData.h", "int x;", "CorpusData"),
        ("large", "resources/big.json", json.dumps({"v": list(range(150000))}), "big.json"),
        # Removed reference-derived generator tables (rule-composer rewrite).
        ("generator", "src/Generator/VocabCells.h", "int x;\n", "VocabCells.h"),
        ("generator", "resources/phrase_bank.json", "{}", "phrase_bank.json"),
        ("generator", "src/Generator/G.cpp", "static const int kPhrase[] = { 1 };\n", "kPhrase"),
        ("generator", "src/Generator/H.cpp", "void baseSlots ();\n", "baseSlots"),
        # A reference-named cell, built from the shifted list so this file never holds the name.
        ("generator", "src/Generator/N.cpp", "// " + "".join(chr(ord(c) - 1) for c in "Dmptfs") + " cell\n", "reference-named"),
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

    # Public genre model leak test: pooled numbers pass; provenance / source
    # keys, a reference name (built from the shifted list) or a reference id fail.
    model = {"version": "1.2", "styles": {"ROLLER": {"bass": {"onset_prob": {"body": [0.5] * 16}, "kick": {"target_pct": 17}}}}}
    code, res = run(make_repo(dict(CLEAN, **{"resources/genre/genre_writing_model.json": json.dumps(model),
                                             "resources/genre/GENRE_WRITING_RULES.md": "# rules\nPooled numbers only.\n"})))
    check(code == 0, "genre: a pooled model and its rules pass")
    shifted = "".join(chr(ord(c) - 1) for c in "Xby!Nbtufs")
    genre_cases = [
        ({"styles": {"ROLLER": {"provenance": {"x": 1}}}}, "provenance"),
        ({"styles": {"DEEP": {"ref_counts": [3, 4]}}}, "ref_counts"),
        ({"styles": {"DEEP": {"stab": {"ref_ids": ["a"]}}}}, "ref_ids"),
        ({"styles": {"BOUNCY": {"track_name": "x"}}}, "track_name"),
        ({"styles": {"JACKIN": {"raw_grid": [1, 0, 1]}}}, "raw_grid"),
        ({"styles": {"JACKIN": {"note": "measured on " + shifted}}}, "names a reference"),
        ({"styles": {"JACKIN": {"note": "grid close to t04"}}}, "reference id"),
    ]
    for doc, needle in genre_cases:
        code, res = run(make_repo(dict(CLEAN, **{"resources/genre/genre_writing_model.json": json.dumps(doc)})))
        check(code == 1 and flagged(res, "generator", needle), f"genre: {needle} is a leak")
    code, res = run(make_repo(dict(CLEAN, **{"resources/genre/GENRE_WRITING_RULES.md": "Built from " + shifted.lower() + ".\n"})))
    check(code == 1 and flagged(res, "generator", "names a reference"), "genre: a reference name in the public rules fails")

    # Generator rule: whole words only, case-sensitive, and only in src/ and resources/.
    code, res = run(make_repo(dict(CLEAN, **{"src/Generator/K.cpp": "const float kDegreeEmphasis = 1.f; bool closer = true;\n",
                                             "docs/notes.md": "The old kPhrase table is gone.\n"})))
    check(code == 0, "generator: kDegreeEmphasis, a lowercase 'closer' and prose outside src/ pass")

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

    # Built-in reference names (refname). Planted names are decoded from the guard's own shifted
    # list at run time, so this file never holds one in plain text.
    import importlib.util
    spec = importlib.util.spec_from_file_location("check_repo_clean", GUARD)
    G = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(G)
    names = [G.unshift(s) for s in G.REFERENCE_SHIFTED_NAMES]
    check(len(names) >= 15 and all(n == n.lower() for n in names), "refname: shifted list decodes")
    artist = names[G.REFERENCE_SHIFTED_NAMES.index("mjhiumfbl")]
    two_words = names[G.REFERENCE_SHIFTED_NAMES.index("mpdbm!evc")]
    style_track = names[G.REFERENCE_SHIFTED_NAMES.index("hbtljo!dmptfs")].split(" ")
    pack_artist = names[G.REFERENCE_SHIFTED_NAMES.index("tufg!ebwjetf")]
    snake = two_words.replace(" ", "_") + "_some_title"
    quoted = style_track[0].capitalize() + ' "' + style_track[1].capitalize() + '"'
    for rel, content, what in [
        ("tools/score.py", f"REFS = ['{snake}']\n", "snake_case ref id in code"),
        ("docs/notes.md", f"Measured from {artist.upper()} main drop.\n", "artist in Markdown prose (fails, not a warning)"),
        ("docs/rules.md", f"dark lead = {quoted}\n", "artist + quoted title"),
        ("src/a.h", f"// {two_words.title().replace(' ', '')} counter-riff\n", "joined CamelCase name"),
    ]:
        code, res = run(make_repo(dict(CLEAN, **{rel: content})))
        check(code == 1 and flagged(res, "refname", rel), f"refname: {what}")
        check(not any(n in json.dumps(res).lower() for n in names), f"refname: report for {rel} does not print the name")
    for rel in ("tools/notes.html", "tools/Makefile", "tools/run.zsh", "docs/page.svg"):   # any text type, no extension too
        code, res = run(make_repo(dict(CLEAN, **{rel: f"<!-- {artist} -->\n"})))
        check(code == 1 and flagged(res, "refname", rel), f"refname: any text file type ({rel})")
    code, res = run(make_repo(dict(CLEAN, **{f"refs/{snake}/bass.txt": "x\n"})))
    check(code == 1 and flagged(res, "refname", "path holds"), "refname: name in a file path")
    # Not a reference: the style id on its own, ordinary English, the owner's own sample names.
    manifest = json.dumps({"samples": [{"file": "Vox_Get_Closer_To_Me_Cm.wav", "artist": pack_artist.title()},
                                       {"file": "Get Closer To Me.wav"}]})
    code, res = run(make_repo(dict(CLEAN, **{
        "src/style.cpp": f"// {style_track[0]} dialect: the stab sits closer to the kick.\nint {style_track[0]}Swing = 56;\nfloat d = (v - line) / span;\n",
        "resources/SAMPLE_MANIFEST.json": manifest,
        "docs/x.md": "The bass gets closer to the root on bar 8. A 2&4 root figure is banned.\n"})))
    check(code == 0, "refname: style id, ordinary 'closer', and the sample manifest's own names pass")
    code, res = run(make_repo(dict(CLEAN, **{"docs/pack.md": f"Pack by {pack_artist.title()}.\n"})))
    check(code == 1 and flagged(res, "refname", "docs/pack.md"), "refname: the manifest-only skip does not apply elsewhere")
    code, res = run(make_repo(dict(CLEAN, **{"docs/notes.md": f"{artist}\n", "tools/repo_guard_allow.txt": "refname docs/notes.md  # test\n"})))
    check(code == 0, "refname: allowlist category works")

    # Private A&R gate bundle and originality indexes: tracked anywhere fails.
    for rel in ("tools/orig_index_ukmin.json", "rolla-gates/README.txt", "x/rolla-gates-private/a.py", "run_all_gates.sh",
                "tools/candidate_gate.sh", "tools/measure_renders.py", "docs/AR_REVIEW_genre_writing_model_v2.md"):
        code, res = run(make_repo(dict(CLEAN, **{rel: "x\n"})))
        check(code == 1 and flagged(res, "private", rel), f"private: tracked {rel}")
    # ... and so does an untracked or git-ignored copy sitting in the checkout.
    code, res = run(make_repo(dict(CLEAN, **{"rolla-gates-private/run.sh": "x\n", "tools/orig_index_long.json": "{}"}),
                              gitignore="rolla-gates-private/\norig_index_*.json\n"))
    check(code == 1 and flagged(res, "private", "rolla-gates-private/") and flagged(res, "private", "orig_index_long.json"),
          "private: git-ignored gate bundle in the checkout fails")
    code, res = run(make_repo(dict(CLEAN, **{"tools/measure_render.py": "x\n", "docs/gates.md": "Gate notes.\n"})))
    check(code == 0, "private: similar but different names pass")

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
