#!/usr/bin/env python3
"""Repo hygiene guard. Fails (exit 1, offending files listed) when the tracked
tree holds anything the public repo must not carry:

  audio       .wav .aif .aiff .flac .mp3 .ogg ... (any tracked audio file)
  midi        .mid / .midi outside the fixture allowlist
  secret      private-key blocks, common API-token shapes, .env files, key stores
  dump        generated corpus / transcription / roll-output paths
  reference   data keyed to named reference tracks:
                - structural (always on): JSON keys or code string literals that
                  look like "Artist - Title (… Mix)" sitting on long note/event data
                - by name (optional): names read from a file given with --names or
                  $ROLLA_REFERENCE_NAMES. The list is never stored in the repo.
                  A hit in code or data fails; a hit in Markdown prose is a warning.
  generator   the removed reference-derived generator tables (owner rule: references
              only as learned statistics). Their files must not come back, their
              identifiers must not reappear in src/ or resources/, and no
              reference-named cell may sit there. The names are stored shifted by
              one character, so the plain names never live in the repo.
              The public genre model (resources/genre/*) also may not carry
              provenance, source ids / counts, names, files or raw grids as JSON
              keys, nor a reference name or reference id anywhere.
  private     the private A&R gate bundle and originality indexes, at any depth:
              rolla-gates/, rolla-gates-private/, run_all_gates.sh, candidate_gate.sh,
              measure_renders.py, orig_index_*.json, AR_REVIEW_genre_writing_model*
  refname     built-in list of the private reference artists / tracks and their
              provenance paths (always on: every tracked path, and the content of
              every tracked non-binary file of any type, tools/ and Markdown included). Stored shifted by one character so the plain
              names never live in the repo. Whole names only (artist, artist +
              title, or a distinctive title), never a single common word, so
              ordinary English and the owner's own sample names don't trip it.
              Between words: up to two of space, _, -, / or quotes, or none.
  large      data files over 512 KB (.json .h .hpp .inc .csv) unless allowlisted
  audio, hard cap: an allowlisted audio fixture still fails over 64 KB. Real packs
              go into a built bundle (tools/inject_pack.sh), never the repo.

Only `git ls-files` counts, so ignored build output never trips it.
Exceptions live in tools/repo_guard_allow.txt, one per line:
    <category> <glob>   # reason
e.g.  midi tests/fixtures/*.mid   # two-bar fixture for the MIDI parser test

  python3 tools/check_repo_clean.py [--root DIR] [--names FILE] [--allow FILE] [--json]
"""

import argparse
import fnmatch
import json
import os
import re
import subprocess
import sys

AUDIO = {".wav", ".wave", ".aif", ".aiff", ".aifc", ".flac", ".mp3", ".ogg", ".oga", ".opus",
         ".m4a", ".aac", ".wma", ".caf", ".alac", ".rx2", ".rex"}
MIDI = {".mid", ".midi", ".smf", ".kar"}
KEY_FILES = {".pem", ".p12", ".pfx", ".key", ".keystore", ".jks", ".ppk", ".mobileprovision"}
KEY_NAMES = {"id_rsa", "id_dsa", "id_ecdsa", "id_ed25519", ".netrc", ".npmrc", ".pypirc", "credentials.json"}
DATA_EXT = {".json", ".h", ".hpp", ".inc", ".csv", ".tsv"}
CODE_EXT = {".h", ".hpp", ".c", ".cc", ".cpp", ".inc", ".py", ".js", ".ts", ".json", ".csv", ".tsv", ".txt", ".yml", ".yaml", ".xml"}
TEXT_EXT = CODE_EXT | {".md", ".cmake", ".sh", ".mm", ".rst", ".ini", ".cfg", ".toml", ".gitignore"}
LARGE_BYTES = 512 * 1024
AUDIO_FIXTURE_BYTES = 64 * 1024

# Generated dumps that were removed from the tree (#12) and must not come back.
DUMP_GLOBS = [
    "tools/out/*", "out/*", "renders/*", "data/corpus*", "*/CorpusData.h", "CorpusData.h",
    "transcriptions/*", "*/transcriptions/*", "*measured_drops*.json", "*ar_vocab_measure*.json",
    "*_transcription*.json", "*.mid.json",
]

# Generator tables removed by the rule-composer rewrite. A note sequence, rhythm
# cell, motif, skeleton, codebook or stab line taken from a reference must never
# be stored again; the composer samples from RuleTables.h statistics only.
GENERATOR_BANNED_PATHS = {
    "resources/phrase_bank.json", "src/Generator/PhraseBankData.h", "src/Generator/PhraseBank.h",
    "src/Generator/PhraseBank.cpp", "src/Generator/VocabCells.h", "tools/apply_ar_vocab.py",
    "tools/extract_corpus.py", "tools/build_phrase_bank.py",
}
GENERATOR_BANNED_TOKENS = [
    "kPhrase", "kLongPhrase", "kJackinPhrase", "kDeg", "kLongDeg", "kGaskDeg", "PhraseBankData", "VocabCells",
    "baseSlots", "deepBook", "bouncyBook", "jackinBook", "kPrefer0", "kPrefer1", "kPrefer2",
    "pushJackinVariationBack", "lockFromBook", "lockPhrase", "makeBassPhrase", "phrase_bank", "rolesFor", "assignSemi",
]
# Reference-named cells, each character shifted up by one (decoded at run time).
GENERATOR_SHIFTED_NAMES = ["N.Ijhi", "NIJHI", "NIjhi", "W!Mjof", "WMJOF", "WMjof", "W`Mjof", "Dmptfs", "Mpdbm!Evc", "MpdbmEvc"]
# The sample-library manifest lists pack file names, not references.
GENERATOR_NAME_SKIP = {"resources/SAMPLE_MANIFEST.json"}
GENERATOR_SCOPES = ("src/", "resources/")

# Private A&R gate bundle and originality indexes: never part of a checkout, at any depth.
PRIVATE_BUNDLE_DIRS = {"rolla-gates", "rolla-gates-private"}
PRIVATE_BUNDLE_FILES = ["orig_index_*.json", "run_all_gates.sh", "candidate_gate.sh", "measure_renders.py",
                        "AR_REVIEW_genre_writing_model*"]


def private_bundle_hit(rel):
    parts = rel.split("/")
    for d in parts[:-1]:
        if d.lower() in PRIVATE_BUNDLE_DIRS:
            return f"inside private gate bundle folder '{d}/'"
    base = parts[-1]
    for g in PRIVATE_BUNDLE_FILES:
        if fnmatch.fnmatch(base.lower(), g.lower()):
            return f"private gate bundle / originality index file ({g})"
    return None


# Private reference artists / tracks and their provenance paths, each character shifted up by one
# (decoded at run time; lowercase, space between words). Public docs and code use generic labels
# (REF_R1.., REF_D1.., REF_B1.., REF_J1..) instead. Add new names here shifted the same way.
REFERENCE_SHIFTED_NAMES = [
    "mbjembx", "ipvtf!pg!evc", "tufg!ebwjetf", "kpti!cblfs", "mpdbm!evc", "dsvj{b", "w!mjof",
    "fmmjb", "fmjzb!kbzb", "mpwfst!jo!uif!ebodf", "mjhiumfbl", "n!ijhi", "b!gsfti!fofshz",
    "xby!nbtufs", "hbtljo!dmptfs", "sbohfs!usvddp", "lzmf!xbmlfs",
    "nfmbl!dmjqt", "nvtjd!ufbn!usbotdsjqujpot",
]
# Files where a listed name is something else. The sample-library manifest credits the owner's own
# sample pack, whose artist shares one listed name; every other name is still checked there.
REFERENCE_NAME_SKIP = {"resources/SAMPLE_MANIFEST.json": {"tufg!ebwjetf"}}
# At most two separator characters between words: covers "A B", "A_B", "AB", 'A "B"', not "a - b" math.
REF_SEP = r"[\s_\-/\"'‘’“”]{0,2}"


def unshift(s):
    return "".join(chr(ord(c) - 1) for c in s)


def word_re(word):
    return re.compile(r"(?<![A-Za-z0-9_])" + re.escape(word) + r"(?![A-Za-z0-9_])")


GENERATOR_TOKEN_RES = [(t, word_re(t)) for t in GENERATOR_BANNED_TOKENS]
GENERATOR_NAME_RES = [word_re(unshift(n)) for n in GENERATOR_SHIFTED_NAMES]

# The public genre model (resources/genre/*): pooled numbers only. No key may
# carry provenance, source ids or counts, names or raw grids, and no value or
# prose may name a reference or use a reference id. The reference names are
# stored shifted by one character (as above) and matched case-insensitively.
GENRE_SCOPE = "resources/genre/"
GENRE_SHIFTED_NAMES = GENERATOR_SHIFTED_NAMES + ["Kpti!Cblfs", "Xby!Nbtufs", "MJHIUMFBL", "JU[", "Gsfti!Fofshz"]
GENRE_NAME_RES = [re.compile(r"(?<![A-Za-z0-9_])" + re.escape(unshift(n)) + r"(?![A-Za-z0-9_])", re.IGNORECASE)
                  for n in GENRE_SHIFTED_NAMES]
GENRE_FORBIDDEN_KEY = re.compile(
    r"(?:^|_)(?:provenance|refs?|ref_?ids?|ref_?counts?|n_refs|artists?|tracks?|titles?|songs?|packs?|names?|raw|"
    r"raw_?grids?|ids?|files?|paths?)(?:_|$)", re.IGNORECASE)
GENRE_ID_RES = [("reference id", re.compile(r"(?<![A-Za-z0-9_])(?:t\d{1,2}|ref\d{1,2})(?![A-Za-z0-9_])")),
                ("review content", re.compile(r"AR_REVIEW|A&R review", re.IGNORECASE))]


def genre_key_hits(doc, path, out, where=""):
    if isinstance(doc, dict):
        for k, v in doc.items():
            if GENRE_FORBIDDEN_KEY.search(str(k)):
                out.append(f"{path}: forbidden key '{where}{k}' in the public genre model")
            genre_key_hits(v, path, out, f"{where}{k}.")
    elif isinstance(doc, list):
        for v in doc:
            genre_key_hits(v, path, out, where)


def genre_hits(rel, text):
    """Leak findings for one file under resources/genre/."""
    out = []
    if rel.endswith(".json"):
        try:
            genre_key_hits(json.loads(text), rel, out)
        except ValueError:
            out.append(f"{rel}: genre model is not valid JSON")
    for rx in GENRE_NAME_RES:
        m = rx.search(text)
        if m:
            out.append(f"{rel}:{text.count(chr(10), 0, m.start()) + 1}: names a reference in the public genre model")
            break
    for what, rx in GENRE_ID_RES:
        m = rx.search(text)
        if m:
            out.append(f"{rel}:{text.count(chr(10), 0, m.start()) + 1}: {what} in the public genre model")
    return out

def reference_re(shifted):
    words = unshift(shifted).split(" ")
    return re.compile(r"(?<![A-Za-z0-9])" + REF_SEP.join(re.escape(w) for w in words) + r"(?![A-Za-z0-9])", re.I)


REFERENCE_RES = [(s, reference_re(s)) for s in REFERENCE_SHIFTED_NAMES]


def reference_name_hit(rel, text):
    """First built-in reference name in the path or text: (line, index into the list) or None."""
    skip = REFERENCE_NAME_SKIP.get(rel, set())
    for i, (s, rx) in enumerate(REFERENCE_RES):
        if s in skip:
            continue
        if rx.search(rel):
            return 0, i
        if text is not None:
            m = rx.search(text)
            if m:
                return text.count("\n", 0, m.start()) + 1, i
    return None


SECRETS = [
    ("private key block", r"-----BEGIN (?:RSA |EC |DSA |OPENSSH |PGP |ENCRYPTED )?PRIVATE KEY(?: BLOCK)?-----"),
    ("AWS access key id", r"\b(?:AKIA|ASIA)[0-9A-Z]{16}\b"),
    ("GitHub token", r"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{36,}\b|\bgithub_pat_[A-Za-z0-9_]{60,}\b"),
    ("Slack token", r"\bxox[abprs]-[A-Za-z0-9-]{10,}\b"),
    ("Google API key", r"\bAIza[0-9A-Za-z_\-]{35}\b"),
    ("Anthropic API key", r"\bsk-ant-[A-Za-z0-9_\-]{20,}"),
    ("OpenAI-style key", r"\bsk-(?:proj-)?[A-Za-z0-9]{32,}\b"),
    ("Stripe live key", r"\b[rs]k_live_[0-9A-Za-z]{20,}\b"),
    ("assigned secret", r"(?i)\b(?:api[_-]?key|secret[_-]?key|client[_-]?secret|password|passwd|auth[_-]?token|access[_-]?token)\b"
                        r"[\"']?\s*[:=]\s*[\"'][^\"'\s]{16,}[\"']"),
]
SECRET_RES = [(name, re.compile(rx)) for name, rx in SECRETS]

# "Artist - Title", "Title (Extended Mix)", "... Remix", "feat." — the shape of a track name.
TRACKY = re.compile(
    r"^\s*[A-Za-z0-9][A-Za-z0-9&.,'’ ]{0,40}[A-Za-z]\s[-–—]\s[A-Za-z0-9][A-Za-z0-9&.,'’()\[\] -]{0,80}$"  # Artist - Title
    r"|(?i:\b(?:original|extended|radio|club|dub|vocal|instrumental)\s+mix\b|\bremix\b|\bfeat\.\s|\bft\.\s)")
EVENT_KEYS = {"note", "notes", "pitch", "midi", "vel", "velocity", "start", "tick", "ticks", "time", "step",
              "steps", "dur", "duration", "len", "length", "on", "off", "onset", "onsets", "events"}
STRING_LIT = re.compile(r'"((?:[^"\\\n]|\\.){4,120})"')
NUMBER = re.compile(r"(?<![\w.])-?\d+(?:\.\d+)?f?(?![\w.])")


def git_files(root):
    out = subprocess.run(["git", "ls-files", "-z"], cwd=root, capture_output=True, check=True).stdout
    return [p for p in out.decode("utf-8", "replace").split("\0") if p]


def load_allow(path):
    rules = []
    if path and os.path.exists(path):
        for raw in open(path, encoding="utf-8"):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            parts = line.split(None, 1)
            if len(parts) == 2:
                rules.append((parts[0], parts[1].strip()))
    return rules


def allowed(rules, category, path):
    return any(cat in (category, "*") and fnmatch.fnmatch(path, glob) for cat, glob in rules)


def load_names(path):
    names = []
    if path and os.path.exists(path):
        for raw in open(path, encoding="utf-8"):
            line = raw.split("#", 1)[0].strip()
            if len(line) >= 3:
                names.append(line.lower())
    return names


def read_text(full, size_cap=8 * 1024 * 1024):
    try:
        with open(full, "rb") as f:
            data = f.read(size_cap)
    except OSError:
        return None
    if b"\0" in data[:4096]:
        return None
    return data.decode("utf-8", "replace")


def count_event_data(value):
    """Numbers and event-shaped dicts under a JSON value."""
    nums = 0
    events = 0
    stack = [value]
    while stack:
        v = stack.pop()
        if isinstance(v, bool):
            continue
        if isinstance(v, (int, float)):
            nums += 1
        elif isinstance(v, list):
            stack.extend(v)
        elif isinstance(v, dict):
            if len(EVENT_KEYS & {str(k).lower() for k in v.keys()}) >= 2:
                events += 1
            stack.extend(v.values())
    return nums, events


def json_reference_hits(doc, path, out, depth=0):
    if depth > 40:
        return
    if isinstance(doc, dict):
        for k, v in doc.items():
            key = str(k)
            if TRACKY.search(key):
                nums, events = count_event_data(v)
                if nums >= 64 or events >= 8:
                    out.append(f"{path}: key '{key[:80]}' holds {nums} numbers / {events} note events")
                    continue
            json_reference_hits(v, path, out, depth + 1)
        # A record that names a track and carries events.
        label = next((str(doc[k]) for k in ("track", "title", "song", "reference", "ref") if isinstance(doc.get(k), str)), None)
        if label and TRACKY.search(label):
            nums, events = count_event_data(doc)
            if nums >= 64 or events >= 8:
                out.append(f"{path}: record '{label[:80]}' holds {nums} numbers / {events} note events")
    elif isinstance(doc, list):
        for v in doc:
            json_reference_hits(v, path, out, depth + 1)


def code_reference_hits(text, path, out):
    """A track-like string literal in a file that also carries a big numeric table."""
    numbers = len(NUMBER.findall(text))
    if numbers < 256:
        return
    for m in STRING_LIT.finditer(text):
        lit = m.group(1)
        if TRACKY.search(lit) and re.search(r"[A-Za-z]{3}", lit):
            out.append(f"{path}: literal \"{lit[:80]}\" in a file with {numbers} numeric literals")
            return


def scan(root, names, allow_rules):
    fails = {k: [] for k in ("audio", "midi", "secret", "dump", "reference", "generator", "refname", "private", "large")}
    warns = []
    for rel in git_files(root):
        full = os.path.join(root, rel)
        base = os.path.basename(rel)
        ext = os.path.splitext(base)[1].lower()
        low = rel.lower()

        # Reference names in the path itself (any file type, binary included).
        if not allowed(allow_rules, "refname", rel):
            hit = reference_name_hit(rel, None)
            if hit:
                fails["refname"].append(f"{rel}: path holds private reference name #{hit[1] + 1}")

        why = private_bundle_hit(rel)
        if why and not allowed(allow_rules, "private", rel):
            fails["private"].append(f"{rel}: {why}")
            continue

        if ext in AUDIO:
            if not allowed(allow_rules, "audio", rel):
                fails["audio"].append(rel)
                continue
            try:
                audio_size = os.path.getsize(full)
            except OSError:
                audio_size = 0
            if audio_size > AUDIO_FIXTURE_BYTES:
                fails["audio"].append(f"{rel}: {audio_size // 1024} KB audio (allowlisted fixtures are capped at 64 KB)")
            continue
        if ext in MIDI and not allowed(allow_rules, "midi", rel):
            fails["midi"].append(rel)
            continue
        if any(fnmatch.fnmatch(rel, g) for g in DUMP_GLOBS) and not allowed(allow_rules, "dump", rel):
            fails["dump"].append(rel)
            continue
        if rel in GENERATOR_BANNED_PATHS and not allowed(allow_rules, "generator", rel):
            fails["generator"].append(f"{rel}: removed reference-derived generator table")
            continue
        is_env = base == ".env" or (base.startswith(".env.") and not base.endswith((".example", ".sample", ".template")))
        if (is_env or ext in KEY_FILES or base in KEY_NAMES) and not allowed(allow_rules, "secret", rel):
            fails["secret"].append(f"{rel}: secret-bearing file type")
            continue

        try:
            size = os.path.getsize(full)
        except OSError:
            continue
        if ext in DATA_EXT and size > LARGE_BYTES and not allowed(allow_rules, "large", rel):
            fails["large"].append(f"{rel}: {size // 1024} KB data file")

        # Reference names in the content: every tracked non-binary file, whatever its extension.
        text = read_text(full)
        if text is not None and not allowed(allow_rules, "refname", rel):
            hit = reference_name_hit(rel, text)
            if hit and hit[0] > 0:   # path hits were reported above
                fails["refname"].append(f"{rel}:{hit[0]}: private reference name #{hit[1] + 1} "
                                        f"(use a generic label such as REF_R1)")

        if ext not in TEXT_EXT and base not in (".gitignore", "CMakeLists.txt"):
            continue
        if text is None:
            continue

        if not allowed(allow_rules, "secret", rel):
            for name, rx in SECRET_RES:
                m = rx.search(text)
                if m:
                    line = text.count("\n", 0, m.start()) + 1
                    fails["secret"].append(f"{rel}:{line}: {name}")
                    break

        if rel.startswith(GENERATOR_SCOPES) and not allowed(allow_rules, "generator", rel):
            for token, rx in GENERATOR_TOKEN_RES:
                m = rx.search(text)
                if m:
                    line = text.count("\n", 0, m.start()) + 1
                    fails["generator"].append(f"{rel}:{line}: removed table symbol '{token}'")
                    break
            if rel not in GENERATOR_NAME_SKIP:
                for rx in GENERATOR_NAME_RES:
                    m = rx.search(text)
                    if m:
                        line = text.count("\n", 0, m.start()) + 1
                        fails["generator"].append(f"{rel}:{line}: reference-named cell")
                        break
            if rel.startswith(GENRE_SCOPE):
                fails["generator"].extend(genre_hits(rel, text))

        if not allowed(allow_rules, "reference", rel):
            hits = []
            if ext == ".json":
                try:
                    json_reference_hits(json.loads(text), rel, hits)
                except ValueError:
                    pass
            if ext in CODE_EXT - {".json"}:
                code_reference_hits(text, rel, hits)
            fails["reference"].extend(hits)

            if names:
                hay_path = low
                hay = text.lower()
                for n in names:
                    if n in hay_path or n in hay:
                        line = hay[: hay.find(n)].count("\n") + 1 if n in hay else 0
                        msg = f"{rel}:{line}: names reference '{n}'"
                        if ext == ".md":
                            warns.append(msg)
                        else:
                            fails["reference"].append(msg)
                        break
    return fails, warns


def scan_worktree_private(root, allow_rules):
    """The private gate bundle must not sit anywhere in the checkout, tracked, untracked or git-ignored."""
    out = []
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root).replace(os.sep, "/")
        rel_dir = "" if rel_dir == "." else rel_dir + "/"
        dirnames[:] = [d for d in dirnames if d != ".git"]
        for d in list(dirnames):
            if d.lower() in PRIVATE_BUNDLE_DIRS:
                rel = rel_dir + d + "/"
                if not allowed(allow_rules, "private", rel):
                    out.append(f"{rel}: private gate bundle folder in the checkout")
                dirnames.remove(d)
        for f in filenames:
            rel = rel_dir + f
            why = private_bundle_hit(rel)
            if why and not allowed(allow_rules, "private", rel):
                out.append(f"{rel}: {why}")
    return out


def main(argv=None):
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", default=os.path.dirname(here))
    ap.add_argument("--names", default=os.environ.get("ROLLA_REFERENCE_NAMES", ""),
                    help="file of reference artist / track names, one per line (kept outside the repo)")
    ap.add_argument("--allow", default=None, help="allowlist (default: <root>/tools/repo_guard_allow.txt)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    root = os.path.abspath(a.root)
    allow_path = a.allow or os.path.join(root, "tools", "repo_guard_allow.txt")
    names = load_names(a.names)
    allow_rules = load_allow(allow_path)
    fails, warns = scan(root, names, allow_rules)
    fails["private"].extend(x for x in scan_worktree_private(root, allow_rules) if x not in fails["private"])
    total = sum(len(v) for v in fails.values())
    if a.json:
        print(json.dumps({"pass": total == 0, "fails": fails, "warnings": warns, "names_checked": len(names)}, indent=1))
    else:
        for cat, items in fails.items():
            for item in items:
                print(f"FAIL {cat}: {item}")
        for w in warns:
            print(f"WARN reference (prose): {w}")
        print(f"repo guard: {'PASS' if total == 0 else 'FAIL'} ({total} finding(s), {len(warns)} warning(s), "
              f"{len(names)} reference name(s) checked)")
    return 0 if total == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
