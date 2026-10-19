#!/usr/bin/env python3
"""Repo hygiene guard. Fails (exit 1, offending files listed) when the tracked
tree holds anything the public repo must not carry:

  audio       .wav .aif .aiff .flac .mp3 .ogg ... (any tracked audio file)
  midi        .mid / .midi outside the fixture allowlist
  secret      private-key blocks, common API-token shapes, .env files, key stores
  dump        generated corpus / transcription / roll-output paths
  reference   data keyed to named reference tracks:
                - structural (always on): JSON keys or code string literals that
                  look like "Artist - Title (... Mix)" sitting on long note/event data
                - by name (optional): names read from a file given with --names or
                  $ROLLA_REFERENCE_NAMES. The list is never stored in the repo.
                  A hit in code or data fails; a hit in Markdown prose is a warning.
  generator   the removed reference-derived generator tables (owner rule: references
              only as pooled statistics). Their files (and any PATTERN_VOCAB.json, at
              any depth) must not come back, their identifiers must not reappear in
              src/ or resources/, and no reference-named cell may sit there.
              The public genre model (resources/genre/*) also may not carry
              provenance, source ids / counts, names, files or raw grids as JSON
              keys, nor a reference name or reference id anywhere.
  private     the private A&R gate bundle and originality indexes, at any depth:
              rolla-gates/, rolla-gates-private/, run_all_gates.sh, candidate_gate.sh,
              measure_renders.py, orig_index_*.json, orig_builtin*.json,
              AR_REVIEW_genre_writing_model*
  refname     built-in list of the private reference artists / tracks and their
              provenance paths (always on: every tracked path, and the content of
              every tracked non-binary file of any type, tools/ and Markdown included).
              Whole names only (artist, artist + title, or a distinctive title), never
              a single common word, so ordinary English and the owner's own sample
              names don't trip it. Between words: one or two of space, _, -, / or
              quotes, or none ("AB" spellings).
  refid       reference ids in any tracked text file (.github/ excepted): labels of the
              form REF_ + letter + digits, ref<N>, and two-digit t<NN> track ids (plus
              one-digit t<N> outside code, where t0 / t1 are ordinary variables).
  workspace   a path into the old scratch workspace (slash + "workspace") in any tracked
              text file: provenance of private measurement runs.
  large      data files over 512 KB (.json .h .hpp .inc .csv) unless allowlisted
  audio, hard cap: an allowlisted audio fixture still fails over 64 KB. Real packs
              go into a built bundle (tools/inject_pack.sh), never the repo.

Names are never stored, not even encoded: the guard keeps salted SHA-256 digests
only (SALT below is public; the salt only stops ready-made lookup tables). Text is
split into words (runs of A-Z a-z 0-9, compared lowercase except for the
case-sensitive generator cells); every run of 1 to 4 adjacent words is hashed
space-joined and, for 2-4 words, run together, and a digest in a list is a hit.
Reports give the list position (#N), never the name.
  python3 tools/check_repo_clean.py --hash-name "first last" [--case]
prints the digests to add for a new name (--case for a generator cell).
Extra digests (tests, or a private list) come from --extra-digests FILE or
$ROLLA_GUARD_EXTRA_DIGESTS: JSON {"refname": [[d, ...], ...], "generator": [...],
"genre": [...], "refname_skip": {"<path>": [d, ...]}}.

Only `git ls-files` counts, so ignored build output never trips it.
Exceptions live in tools/repo_guard_allow.txt, one per line:
    <category> <glob>   # reason
e.g.  midi tests/fixtures/*.mid   # two-bar fixture for the MIDI parser test

  python3 tools/check_repo_clean.py [--root DIR] [--names FILE] [--allow FILE] [--extra-digests FILE] [--json]
"""

import argparse
import fnmatch
import hashlib
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
# Any file of this name, at any depth: the old reference pattern vocabulary.
GENERATOR_BANNED_BASENAMES = {"pattern_vocab.json"}
# Reference-named cells: see GENERATOR_NAME_DIGESTS (case-sensitive) below.
# The sample-library manifest lists pack file names, not references.
GENERATOR_NAME_SKIP = {"resources/SAMPLE_MANIFEST.json"}
GENERATOR_SCOPES = ("src/", "resources/")

# Private A&R gate bundle and originality indexes: never part of a checkout, at any depth.
PRIVATE_BUNDLE_DIRS = {"rolla-gates", "rolla-gates-private"}
PRIVATE_BUNDLE_FILES = ["orig_index_*.json", "orig_builtin*.json", "run_all_gates.sh", "candidate_gate.sh", "measure_renders.py",
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


# ------------------------------------------------------------------ hashed name lists
# Private reference artists / tracks, their provenance paths and reference-named cells,
# as salted SHA-256 digests (see the module doc). One tuple per name: the digest of its
# words space-joined and, for several words, run together. Public docs and code describe
# references only as pooled statistics. Add a name with --hash-name (--case for cells).
SALT = "rolla-repo-guard/v1:"

# Reference-named generator cells, case-sensitive (a lowercase ordinary word passes in code).
GENERATOR_NAME_DIGESTS = [
    ("1be7189a5972f8a074aebe5f4f01f41721d12c4dbab28b289921e0ff714829d7", "1a587233ddd6b01f531ccaeb0f66c0100ef1ab13835a13f6276fe15626edb567"),
    ("38731658018d61ecc33983e2568ab2a97b868bd45e1694e5f58049b2336c3b09",),
    ("3504afcd8f5480c9b9895bff0f99dfbc52e8ad9fd725c522c863032d05557a0f", "851ce5d67ea867346682164053db78e5c4152c56a04e732d46cc68d09edff1de"),
    ("da6d0d27cbc8d7626747d22b3ac38d9add8a6744bee309b2389b362111597a40",),
    ("51119567f5d5cd55c2aef4b8c1eb99b0713ab56fd9baf2dcb55b5b79b931482f",),
    ("05f40398ca472bfe446e7b7cd7500e77f82b0656d820c8fc8ab82b11101360d2", "12446d01879ab9d9a9ef80666f4cc50107f00ca392b4196c0225922e393db210"),
]
# Names the public genre model may not carry (resources/genre/*), case-insensitive.
GENRE_NAME_DIGESTS = [
    ("ebbb1cfda77e64de174775c87ab5f001dec4ecfef033827812b73023927b2655", "05e94f2ed5605274478f6e2cfbc6d5d2dd4c2317cb7b4be149e6f22bfd2dc8f2"),
    ("18b93ed497484c1a82403ee3a24340c92c62670566fd50793bdd375103c7a711", "74ac8f1ccb5cdf05efa3c1d7281b74257ea3eb23b5e3dba3adbd6f0987c15690"),
    ("b9846206a90fe5dbcc3cf860a01f75863e4705d506c1f78ecf339f57d93fdf83",),
    ("aea38c184403e188c1a018241e9422aec344e86a578e1cef0bf9868e0d6208fc", "368bcefb07a910dc1a7bf1cde8a897215234f4bea4e2114cd61b437d4cd1a4ae"),
    ("991a4b9745a3c68f53a82dadac61b8d83e1074062f1281894a70c80636a1474a", "34b620caeee00a499182514f4c56df8f6b60efbb7fdfe5a1bd6f83cbb75877be"),
    ("eb96b5427e4553d5e46011217a9bd0b3cfb2c692e45d8588c1cc751ec0c4eda3", "3a5170d714a82bcd0c484e73265ef75d458f6a8b2430e203bf6f1273efd900d5"),
    ("a870958ef680accc6f92e688a41f68cfaa6090f700d86f67442a5e33490dc08f",),
    ("24455ac8354b05824c2a055bd6eacbe8f16f2741530dace833bea1d15e979308",),
    ("35917e0c9259126c84a85e33f114db6623d7b590e8f658ff7ed03834a5bb2088", "70f8bb4a868fb724927d50c1b8777770910304a55e82feccf5d97411d768011b"),
]
# Private reference artists / tracks and provenance paths (refname), case-insensitive.
REFERENCE_NAME_DIGESTS = [
    ("87d54d2db01fef08dc79969f5fbeba8781d8c4b86e8281f74788a6404380cb9e",),
    ("750c9580d754f288a39b6656f09a7f25abfa73bcfd526ca1be00b18c2db5bb87", "688a0abbf580d55c1526c1d7b5edad75480a9986b9324a8c7e7e6cbb06b3aebd"),
    ("e2a18ae6b52c0c73eff705068ba76e4c6ea5d80ab2e8f4cd3ae803d8036e23b1", "68f034f6993f84fe3cbde59872fbaf8bbd07d5587d69fe765d8cbeeb317d5954"),
    ("991a4b9745a3c68f53a82dadac61b8d83e1074062f1281894a70c80636a1474a", "34b620caeee00a499182514f4c56df8f6b60efbb7fdfe5a1bd6f83cbb75877be"),
    ("aea38c184403e188c1a018241e9422aec344e86a578e1cef0bf9868e0d6208fc", "368bcefb07a910dc1a7bf1cde8a897215234f4bea4e2114cd61b437d4cd1a4ae"),
    ("258dcbf894dc4231847b48ecf3309ac8dbb07fc76e22d36aeca063fd50ce5824",),
    ("18b93ed497484c1a82403ee3a24340c92c62670566fd50793bdd375103c7a711", "74ac8f1ccb5cdf05efa3c1d7281b74257ea3eb23b5e3dba3adbd6f0987c15690"),
    ("58a3e5f549403b9b443209d50b425f6caf195e2e3c38539f112c06b891bf6ce4",),
    ("e0f78bbdb33608ddca1e1dc985cf966f14c1853b8885497b7bd7fafc93293338", "52616221526da6bfe761cdf237c0fcf5f0b68e96fc3e832af620e3ccdbc68764"),
    ("f76c330fcdfde893c8cf5006a696848132f7b75f8a937a06b5930eec69943836", "5616e1f6bc86af8422acfcd283134a70bac96bbf8c686600719b2125ddf6e32e"),
    ("a870958ef680accc6f92e688a41f68cfaa6090f700d86f67442a5e33490dc08f",),
    ("ebbb1cfda77e64de174775c87ab5f001dec4ecfef033827812b73023927b2655", "05e94f2ed5605274478f6e2cfbc6d5d2dd4c2317cb7b4be149e6f22bfd2dc8f2"),
    ("3af8f09e6640294d950b369a700cb6c4316aff30949675ee766cb0f41f0f1a36", "39c7bd7510439f042c187559cea5393855dbbe8968f59151efccf540bae85742"),
    ("eb96b5427e4553d5e46011217a9bd0b3cfb2c692e45d8588c1cc751ec0c4eda3", "3a5170d714a82bcd0c484e73265ef75d458f6a8b2430e203bf6f1273efd900d5"),
    ("f9102e3e5a48236b128597b66391fdbca5acf9083429e04a3bda2ae70e6f4803", "0eec58a157aa5564b975beb0beedde80983a4942c116d73ba040bff5d3be2c29"),
    ("25a3797d8f0fb695efcd4c5beb394db42196f454d2154958ad22558d8b885f44", "653c4078d68ff87eaf53a9ccdb0de5c2e171ea8ae63a21e2a58b2b5d18fe9a80"),
    ("0c129c1ffd79903f6ac99798b7ae574f7d11a00219d5c36646da0d4187cc79ad", "5c544b60b8655bd7f8239ddaa39e68da363c0a7c72e578b1b2d29ab55bdd401b"),
    ("cb537b3d10728e6db27bdb3ce854f510d1d86c706787acecefe8757681f4190e", "4ae795937e01d1483605d8cf8db73668c52fd555da446af05dd5e659a68850a4"),
    ("a88457f8d53c6f4f5e6860bc252c322b859a46c645707486e05573f4bd20a954", "2a02a0a952619fb48acc898960b529fb2cb26360b2189bd16bba43581822b13c"),
    ("597db87b50a9bf9e2b0d3538692d561cdbb710e790012539ac703c0e425d6b4e",),
    ("90d8657638653ad0e9f57ee9b7907afde4b66ca46908278bfd19c35c2b1fd2e1", "bbd99281044428462a03c9689182f8d4fddca886dda9a6de1b572d67c9dbf192"),
    ("8d4962806cd6de0e7974a04c2db978a992b2b4b959f2ea1a4dbf1513a9b9e553", "e0f362ac13949d18dc28bacfe9be7d0ed7b51ffcdbab328fe356b16c86ee6f8b"),
    ("1925516abc2ec3ad7a40dcd5fb9c7629b59cd2740c98baf0b2e72e4235333322", "b3c80373cec9c71f925601be82ba4c368006d8adea590f6ee2fb7d58387b8df0"),
    ("712c308768d7beb27ccc1c13f183d21d40d06fa768bd247ebd46ef6c0ced9c8b", "01045b54ed00e0302be56907d3f3ebbefd1d87fbe34033d6bbf83eb840cccd4d"),
    ("3dc2acaf7def2efb5092dd87a8990fac9ccaa15c893d07c399059945e7262476",),
    ("d808e52af10ea03f1f66cdb22cc75983670fe038cd81206c5f0e6f4e23ca0c18", "fae5ec88655aa2a33f282435913536d5a8f689e9e4a9da85db4350d24cc58178"),
]
# Files where a listed name is something else. The sample-library manifest credits the owner's own
# sample pack, whose artist shares one listed name; every other name is still checked there.
REFERENCE_NAME_SKIP = {"resources/SAMPLE_MANIFEST.json": {"e2a18ae6b52c0c73eff705068ba76e4c6ea5d80ab2e8f4cd3ae803d8036e23b1"}}

WORD = re.compile(r"[A-Za-z0-9]+")
# Between two words of a name: one or two of space, _, -, / or quotes. "a - b" (three) is math, not a name.
NAME_GAP = re.compile(r"[\s_\-/\"'‘’“”]{1,2}")
# Longest listed name, in words: every run of 1..MAX_NAME_WORDS adjacent words is hashed.
MAX_NAME_WORDS = 4
_DIGEST_CACHE = {}


def name_digest(s):
    d = _DIGEST_CACHE.get(s)
    if d is None:
        d = _DIGEST_CACHE[s] = hashlib.sha256((SALT + s).encode("utf-8")).hexdigest()
    return d


def name_digests(name, case=False):
    """The digest tuple stored for one name."""
    words = WORD.findall(name if case else name.lower())
    return tuple(dict.fromkeys((name_digest(" ".join(words)), name_digest("".join(words)))))


class NameList:
    """Digest lists matched together over one pass of the text (all lowercase, or all case-sensitive)."""

    def __init__(self, lists, case=False):
        self.case = case
        self.index = {}                     # digest -> [(list no, entry no)]
        for li, entries in enumerate(lists):
            for ei, entry in enumerate(entries):
                for d in entry:
                    if (li, ei) not in self.index.setdefault(d, []):
                        self.index[d].append((li, ei))
        self.memo = {}                      # word n-gram -> hits

    def _hits(self, s):
        h = self.memo.get(s)
        if h is None:
            h = self.memo[s] = tuple(self.index.get(name_digest(s), ()))
        return h

    def first_hits(self, text, skip=None):
        """{list no: (offset, entry no)} of the first listed name per list. skip: {list no: {entry no}}."""
        if not self.case:
            text = text.lower()
        toks = [(m.start(), m.end(), m.group()) for m in WORD.finditer(text)]
        n = len(toks)
        linked = [i + 1 < n and NAME_GAP.fullmatch(text, toks[i][1], toks[i + 1][0]) is not None for i in range(n)]
        found = {}
        for i in range(n):
            pos, _, w0 = toks[i]
            cands = [w0]
            spaced = joined = w0
            j = i
            while linked[j] and j - i + 2 <= MAX_NAME_WORDS:
                j += 1
                spaced += " " + toks[j][2]
                joined += toks[j][2]
                cands += [spaced, joined]
            for c in cands:
                for li, ei in self._hits(c):
                    if li in found or (skip and ei in skip.get(li, ())):
                        continue
                    found[li] = (pos, ei)
        return found


def load_extra_digests(path):
    """Extra digest lists (test hook / private list), merged after the built-in ones."""
    extra = {"refname": [], "generator": [], "genre": [], "refname_skip": {}}
    if path:
        with open(path, encoding="utf-8") as f:
            doc = json.load(f)
        for k in ("refname", "generator", "genre"):
            extra[k] = [tuple(e) for e in doc.get(k, [])]
        extra["refname_skip"] = {p: set(v) for p, v in doc.get("refname_skip", {}).items()}
    return extra


def build_name_lists(extra):
    ref = list(REFERENCE_NAME_DIGESTS) + extra["refname"]
    skip_digests = {p: set(v) for p, v in REFERENCE_NAME_SKIP.items()}
    for p, v in extra["refname_skip"].items():
        skip_digests.setdefault(p, set()).update(v)
    skip = {p: {i for i, e in enumerate(ref) if set(e) & ds} for p, ds in skip_digests.items()}
    lower = NameList([ref, list(GENRE_NAME_DIGESTS) + extra["genre"]])            # 0 refname, 1 genre
    cased = NameList([list(GENERATOR_NAME_DIGESTS) + extra["generator"]], case=True)
    return lower, cased, skip


def word_re(word):
    return re.compile(r"(?<![A-Za-z0-9_])" + re.escape(word) + r"(?![A-Za-z0-9_])")


GENERATOR_TOKEN_RES = [(t, word_re(t)) for t in GENERATOR_BANNED_TOKENS]

# The public genre model (resources/genre/*): pooled numbers only. No key may
# carry provenance, source ids or counts, names or raw grids, and no value or
# prose may name a reference (GENRE_NAME_DIGESTS) or use a reference id.
GENRE_SCOPE = "resources/genre/"
GENRE_FORBIDDEN_KEY = re.compile(
    r"(?:^|_)(?:provenance|refs?|ref_?ids?|ref_?counts?|n_refs|artists?|tracks?|titles?|songs?|packs?|names?|raw|"
    r"raw_?grids?|ids?|files?|paths?)(?:_|$)", re.IGNORECASE)
GENRE_ID_RES = [("reference id", re.compile(r"(?<![A-Za-z0-9_])(?:t\d{1,2}|ref\d{1,2})(?![A-Za-z0-9_])")),
                ("review content", re.compile(r"AR_REVIEW|A&R review", re.IGNORECASE))]

# Reference ids anywhere (refid) and scratch-workspace paths (workspace). The patterns are
# assembled so this file itself holds no literal id or path.
REFID_RES = [
    ("reference label", re.compile(r"(?<![A-Za-z0-9])" + "REF" + r"_[A-Z]\d+", re.IGNORECASE)),
    ("reference number", re.compile(r"(?<![A-Za-z0-9_])" + "ref" + r"\d{1,2}(?![A-Za-z0-9_])", re.IGNORECASE)),
    ("track id", re.compile(r"(?<![A-Za-z0-9_])t\d{2}(?![A-Za-z0-9_])")),
]
REFID_SHORT_RE = ("track id", re.compile(r"(?<![A-Za-z0-9_])t\d(?![A-Za-z0-9_])"))
CODE_ONLY_EXT = {".h", ".hpp", ".c", ".cc", ".cpp", ".inc", ".mm", ".py", ".js", ".ts", ".sh", ".cmake"}
REFID_SKIP_PREFIXES = (".github/",)
WORKSPACE_RE = re.compile("/" + "work" + r"space(?![A-Za-z0-9_])", re.IGNORECASE)


def refid_hit(rel, ext, text):
    """(line, what) of the first reference id in the text, or None."""
    res = list(REFID_RES)
    if ext not in CODE_ONLY_EXT and os.path.basename(rel) != "CMakeLists.txt":
        res.append(REFID_SHORT_RE)
    best = None
    for what, rx in res:
        m = rx.search(text)
        if m and (best is None or m.start() < best[0]):
            best = (m.start(), what)
    if best is None:
        return None
    return text.count("\n", 0, best[0]) + 1, best[1]


def genre_key_hits(doc, path, out, where=""):
    if isinstance(doc, dict):
        for k, v in doc.items():
            if GENRE_FORBIDDEN_KEY.search(str(k)):
                out.append(f"{path}: forbidden key '{where}{k}' in the public genre model")
            genre_key_hits(v, path, out, f"{where}{k}.")
    elif isinstance(doc, list):
        for v in doc:
            genre_key_hits(v, path, out, where)


def genre_hits(rel, text, name_hit):
    """Leak findings for one file under resources/genre/. name_hit: offset of a genre-list name, or None."""
    out = []
    if rel.endswith(".json"):
        try:
            genre_key_hits(json.loads(text), rel, out)
        except ValueError:
            out.append(f"{rel}: genre model is not valid JSON")
    if name_hit is not None:
        out.append(f"{rel}:{text.count(chr(10), 0, name_hit) + 1}: names a reference in the public genre model")
    for what, rx in GENRE_ID_RES:
        m = rx.search(text)
        if m:
            out.append(f"{rel}:{text.count(chr(10), 0, m.start()) + 1}: {what} in the public genre model")
    return out


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


def scan(root, names, allow_rules, extra=None):
    fails = {k: [] for k in ("audio", "midi", "secret", "dump", "reference", "generator", "refname", "refid",
                             "workspace", "private", "large")}
    lower, cased, ref_skip = build_name_lists(extra or load_extra_digests(None))
    warns = []
    for rel in git_files(root):
        full = os.path.join(root, rel)
        base = os.path.basename(rel)
        ext = os.path.splitext(base)[1].lower()
        low = rel.lower()

        # Reference names / ids in the path itself (any file type, binary included).
        skip_here = {0: ref_skip.get(rel, set())}
        if not allowed(allow_rules, "refname", rel):
            hit = lower.first_hits(rel, skip_here).get(0)
            if hit:
                fails["refname"].append(f"{rel}: path holds private reference name #{hit[1] + 1}")
        if not rel.startswith(REFID_SKIP_PREFIXES) and not allowed(allow_rules, "refid", rel):
            hit = refid_hit(rel, "", rel)
            if hit:
                fails["refid"].append(f"{rel}: path holds a {hit[1]}")

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
        if (rel in GENERATOR_BANNED_PATHS or base.lower() in GENERATOR_BANNED_BASENAMES) \
                and not allowed(allow_rules, "generator", rel):
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

        # Reference names, ids and workspace paths in the content: every tracked non-binary file,
        # whatever its extension.
        text = read_text(full)
        name_hits = lower.first_hits(text, skip_here) if text is not None else {}
        if text is not None and not allowed(allow_rules, "refname", rel) and 0 in name_hits:
            line = text.count("\n", 0, name_hits[0][0]) + 1
            fails["refname"].append(f"{rel}:{line}: private reference name #{name_hits[0][1] + 1} "
                                    f"(describe references only as pooled statistics)")
        if text is not None and not rel.startswith(REFID_SKIP_PREFIXES):
            if not allowed(allow_rules, "refid", rel):
                hit = refid_hit(rel, ext, text)
                if hit:
                    fails["refid"].append(f"{rel}:{hit[0]}: {hit[1]} (describe references only as pooled statistics)")
            m = WORKSPACE_RE.search(text)
            if m and not allowed(allow_rules, "workspace", rel):
                fails["workspace"].append(f"{rel}:{text.count(chr(10), 0, m.start()) + 1}: scratch workspace path")

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
                hit = cased.first_hits(text).get(0)
                if hit:
                    line = text.count("\n", 0, hit[0]) + 1
                    fails["generator"].append(f"{rel}:{line}: reference-named cell #{hit[1] + 1}")
            if rel.startswith(GENRE_SCOPE):
                genre_name = name_hits.get(1)
                fails["generator"].extend(genre_hits(rel, text, genre_name[0] if genre_name else None))

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
    ap.add_argument("--extra-digests", default=os.environ.get("ROLLA_GUARD_EXTRA_DIGESTS", ""),
                    help="JSON of extra name digests (tests / a private list)")
    ap.add_argument("--hash-name", default=None, help="print the digest tuple for a name and exit")
    ap.add_argument("--case", action="store_true", help="with --hash-name: case-sensitive (a generator cell)")
    ap.add_argument("--json", action="store_true")
    a = ap.parse_args(argv)
    if a.hash_name is not None:
        print(json.dumps(list(name_digests(a.hash_name, a.case))))
        return 0
    root = os.path.abspath(a.root)
    allow_path = a.allow or os.path.join(root, "tools", "repo_guard_allow.txt")
    names = load_names(a.names)
    allow_rules = load_allow(allow_path)
    fails, warns = scan(root, names, allow_rules, load_extra_digests(a.extra_digests))
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
